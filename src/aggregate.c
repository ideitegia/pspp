/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <assert.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "file-handle.h"
#include "lexer.h"
#include "misc.h"
#include "settings.h"
#include "sfm.h"
#include "sort.h"
#include "stats.h"
#include "str.h"
#include "var.h"
#include "vfm.h"
#include "vfmP.h"

#include "debug-print.h"

/* Specifies how to make an aggregate variable. */
struct agr_var
  {
    struct agr_var *next;		/* Next in list. */

    /* Collected during parsing. */
    struct variable *src;	/* Source variable. */
    struct variable *dest;	/* Target variable. */
    int function;		/* Function. */
    int include_missing;	/* 1=Include user-missing values. */
    union value arg[2];		/* Arguments. */

    /* Accumulated during AGGREGATE execution. */
    double dbl[3];
    int int1, int2;
    char *string;
    int missing;
  };

/* Aggregation functions. */
enum
  {
    NONE, SUM, MEAN, SD, MAX, MIN, PGT, PLT, PIN, POUT, FGT, FLT, FIN,
    FOUT, N, NU, NMISS, NUMISS, FIRST, LAST,
    N_AGR_FUNCS, N_NO_VARS, NU_NO_VARS,
    FUNC = 0x1f, /* Function mask. */
    FSTRING = 1<<5, /* String function bit. */
  };

/* Attributes of an aggregation function. */
struct agr_func
  {
    const char *name;		/* Aggregation function name. */
    int n_args;			/* Number of arguments. */
    int alpha_type;		/* When given ALPHA arguments, output type. */
    struct fmt_spec format;	/* Format spec if alpha_type != ALPHA. */
  };

/* Attributes of aggregation functions. */
static struct agr_func agr_func_tab[] = 
  {
    {"<NONE>",  0, -1,      {0, 0, 0}},
    {"SUM",     0, -1,      {FMT_F, 8, 2}},
    {"MEAN",	0, -1,      {FMT_F, 8, 2}},
    {"SD",      0, -1,      {FMT_F, 8, 2}},
    {"MAX",     0, ALPHA,   {-1, -1, -1}}, 
    {"MIN",     0, ALPHA,   {-1, -1, -1}}, 
    {"PGT",     1, NUMERIC, {FMT_F, 5, 1}},      
    {"PLT",     1, NUMERIC, {FMT_F, 5, 1}},       
    {"PIN",     2, NUMERIC, {FMT_F, 5, 1}},       
    {"POUT",    2, NUMERIC, {FMT_F, 5, 1}},       
    {"FGT",     1, NUMERIC, {FMT_F, 5, 3}},       
    {"FLT",     1, NUMERIC, {FMT_F, 5, 3}},       
    {"FIN",     2, NUMERIC, {FMT_F, 5, 3}},       
    {"FOUT",    2, NUMERIC, {FMT_F, 5, 3}},       
    {"N",       0, NUMERIC, {FMT_F, 7, 0}},       
    {"NU",      0, NUMERIC, {FMT_F, 7, 0}},       
    {"NMISS",   0, NUMERIC, {FMT_F, 7, 0}},       
    {"NUMISS",  0, NUMERIC, {FMT_F, 7, 0}},       
    {"FIRST",   0, ALPHA,   {-1, -1, -1}}, 
    {"LAST",    0, ALPHA,   {-1, -1, -1}},
    {NULL,      0, -1,      {-1, -1, -1}},
    {"N",       0, NUMERIC, {FMT_F, 7, 0}},
    {"NU",      0, NUMERIC, {FMT_F, 7, 0}},
  };

/* Output file, or NULL for the active file. */
static struct file_handle *outfile;

/* Missing value types. */
enum
  {
    ITEMWISE,		/* Missing values item by item. */
    COLUMNWISE		/* Missing values column by column. */
  };

/* ITEMWISE or COLUMNWISE. */
static int missing;

/* Aggregate variables. */
static struct agr_var *agr_first, *agr_next;

/* Aggregate dictionary. */
static struct dictionary *agr_dict;

/* Number of cases passed through aggregation. */
static int case_count;

/* Last values of the break variables. */
static union value *prev_case;

/* Buffers for use by the 10x transformation. */
static flt64 *buf64_1xx;
static struct ccase *buf_1xx;

static void initialize_aggregate_info (void);

/* Prototypes. */
static int parse_aggregate_functions (void);
static void free_aggregate_functions (void);
static int aggregate_single_case (struct ccase *input, struct ccase *output);
static int create_sysfile (void);

static int agr_00x_trns_proc (struct trns_header *, struct ccase *);
static void agr_00x_end_func (void);
static int agr_10x_trns_proc (struct trns_header *, struct ccase *);
static void agr_10x_trns_free (struct trns_header *);
static void agr_10x_end_func (void);
static int agr_11x_func (void);

#if DEBUGGING
static void debug_print (int flags);
#endif

/* Parsing. */

/* Parses and executes the AGGREGATE procedure. */
int
cmd_aggregate (void)
{
  /* From sort.c. */
  int parse_sort_variables (void);
  
  /* Have we seen these subcommands? */
  unsigned seen = 0;

  outfile = NULL;
  missing = ITEMWISE;
  v_sort = NULL;
  prev_case = NULL;
  
  agr_dict = dict_create ();
  dict_set_label (agr_dict, dict_get_label (default_dict));
  dict_set_documents (agr_dict, dict_get_documents (default_dict));
  
  lex_match_id ("AGGREGATE");

  /* Read most of the subcommands. */
  for (;;)
    {
      lex_match('/');
      
      if (lex_match_id ("OUTFILE"))
	{
	  if (seen & 1)
	    {
	      free (v_sort);
	      dict_destroy (agr_dict);
	      msg (SE, _("%s subcommand given multiple times."),"OUTFILE");
	      return CMD_FAILURE;
	    }
	  seen |= 1;
	      
	  lex_match ('=');
	  if (lex_match ('*'))
	    outfile = NULL;
	  else 
	    {
	      outfile = fh_parse_file_handle ();
	      if (outfile == NULL)
		{
		  free (v_sort);
		  dict_destroy (agr_dict);
		  return CMD_FAILURE;
		}
	    }
	}
      else if (lex_match_id ("MISSING"))
	{
	  lex_match ('=');
	  if (!lex_match_id ("COLUMNWISE"))
	    {
	      free (v_sort);
	      dict_destroy (agr_dict);
	      lex_error (_("while expecting COLUMNWISE"));
	      return CMD_FAILURE;
	    }
	  missing = COLUMNWISE;
	}
      else if (lex_match_id ("DOCUMENT"))
	seen |= 2;
      else if (lex_match_id ("PRESORTED"))
	seen |= 4;
      else if (lex_match_id ("BREAK"))
	{
	  if (seen & 8)
	    {
	      free (v_sort);
	      dict_destroy (agr_dict);
	      msg (SE, _("%s subcommand given multiple times."),"BREAK");
	      return CMD_FAILURE;
	    }
	  seen |= 8;

	  lex_match ('=');
	  if (!parse_sort_variables ())
	    {
	      dict_destroy (agr_dict);
	      return CMD_FAILURE;
	    }
	  
	  {
	    int i;
	    
	    for (i = 0; i < nv_sort; i++)
	      {
		struct variable *v;
	      
		v = dict_clone_var (agr_dict, v_sort[i], v_sort[i]->name);
		assert (v != NULL);
	      }
	  }
	}
      else break;
    }

  /* Check for proper syntax. */
  if (!(seen & 8))
    msg (SW, _("BREAK subcommand not specified."));
      
  /* Read in the aggregate functions. */
  if (!parse_aggregate_functions ())
    {
      free_aggregate_functions ();
      free (v_sort);
      return CMD_FAILURE;
    }

  /* Delete documents. */
  if (!(seen & 2))
    dict_set_documents (agr_dict, NULL);

  /* Cancel SPLIT FILE. */
  dict_set_split_vars (agr_dict, NULL, 0);
  
#if DEBUGGING
  debug_print (seen);
#endif

  /* Initialize. */
  case_count = 0;
  initialize_aggregate_info ();

  /* How to implement all this... There are three important variables:
     whether output is going to the active file (0) or a separate file
     (1); whether the input data is presorted (0) or needs sorting
     (1); whether there is a temporary transformation (1) or not (0).
     The eight cases are as follows:

     000 (0): Pass it through an aggregate transformation that
     modifies the data.

     001 (1): Cancel the temporary transformation and handle as 000.

     010 (2): Set up a SORT CASES and aggregate the output, writing
     the results to the active file.
     
     011 (3): Cancel the temporary transformation and handle as 010.

     100 (4): Pass it through an aggregate transformation that doesn't
     modify the data but merely writes it to the output file.

     101 (5): Handled as 100.

     110 (6): Set up a SORT CASES and capture the output, aggregate
     it, write it to the output file without modifying the active
     file.

     111 (7): Handled as 110. */
  
  {
    unsigned type = 0;

    if (outfile != NULL)
      type |= 4;
    if (nv_sort != 0 && (seen & 4) == 0)
      type |= 2;
    if (temporary)
      type |= 1;

    switch (type)
      {
      case 3:
	cancel_temporary ();
	/* fall through */
      case 2:
	sort_cases (0);
	goto case0;
	  
      case 1:
	cancel_temporary ();
	/* fall through */
      case 0:
      case0:
	{
	  struct trns_header *t = xmalloc (sizeof *t);
	  t->proc = agr_00x_trns_proc;
	  t->free = NULL;
	  add_transformation (t);
	  
	  temporary = 2;
	  temp_dict = agr_dict;
	  temp_trns = n_trns;
	  
	  agr_dict = NULL;

	  procedure (NULL, NULL, agr_00x_end_func);
	  break;
	}

      case 4:
      case 5:
	{
	  if (!create_sysfile ())
	    goto lossage;
	  
	  {
	    struct trns_header *t = xmalloc (sizeof *t);
	    t->proc = agr_10x_trns_proc;
	    t->free = agr_10x_trns_free;
	    add_transformation (t);

	    procedure (NULL, NULL, agr_10x_end_func);
	  }
	  
	  break;
	}
	  
      case 6:
      case 7:
	sort_cases (1);
	
	if (!create_sysfile ())
	  goto lossage;
	read_sort_output (agr_11x_func);
	
	{
	  struct ccase *save_temp_case = temp_case;
	  temp_case = NULL;
	  agr_11x_func ();
	  temp_case = save_temp_case;
	}
	
	break;

      default:
	assert (0);
      }
  }
  
  free (buf64_1xx);
  free (buf_1xx);
  
  /* Clean up. */
  free (v_sort);
  free_aggregate_functions ();
  free (prev_case);
  
  return CMD_SUCCESS;

lossage:
  /* Clean up. */
  free (v_sort);
  free_aggregate_functions ();
  free (prev_case);

  return CMD_FAILURE;
}

/* Create a system file for use in aggregation to an external file,
   and allocate temporary buffers for writing out cases. */
static int
create_sysfile (void)
{
  struct sfm_write_info w;
  w.h = outfile;
  w.dict = agr_dict;
  w.compress = set_scompression;
  if (!sfm_write_dictionary (&w))
    {
      free_aggregate_functions ();
      free (v_sort);
      dict_destroy (agr_dict);
      return 0;
    }
    
  buf64_1xx = xmalloc (sizeof *buf64_1xx * w.case_size);
  buf_1xx = xmalloc (sizeof (struct ccase)
                     + (sizeof (union value)
                        * (dict_get_value_cnt (agr_dict) - 1)));

  return 1;
}

/* Parse all the aggregate functions. */
static int
parse_aggregate_functions (void)
{
  agr_first = agr_next = NULL;

  /* Parse everything. */
  for (;;)
    {
      char **dest;
      char **dest_label;
      int n_dest;

      int include_missing;
      struct agr_func *function;
      int func_index;

      union value arg[2];

      struct variable **src;
      int n_src;

      int i;

      dest = NULL;
      dest_label = NULL;
      n_dest = 0;
      src = NULL;
      n_src = 0;
      arg[0].c = NULL;
      arg[1].c = NULL;

      /* Parse the list of target variables. */
      while (!lex_match ('='))
	{
	  int n_dest_prev = n_dest;
	  
	  if (!parse_DATA_LIST_vars (&dest, &n_dest, PV_APPEND | PV_SINGLE | PV_NO_SCRATCH))
	    goto lossage;

	  /* Assign empty labels. */
	  {
	    int j;

	    dest_label = xrealloc (dest_label, sizeof *dest_label * n_dest);
	    for (j = n_dest_prev; j < n_dest; j++)
	      dest_label[j] = NULL;
	  }
	  
	  if (token == T_STRING)
	    {
	      ds_truncate (&tokstr, 120);
	      dest_label[n_dest - 1] = xstrdup (ds_value (&tokstr));
	      lex_get ();
	    }
	}

      /* Get the name of the aggregation function. */
      if (token != T_ID)
	{
	  lex_error (_("expecting aggregation function"));
	  goto lossage;
	}

      include_missing = 0;
      if (tokid[strlen (tokid) - 1] == '.')
	{
	  include_missing = 1;
	  tokid[strlen (tokid) - 1] = 0;
	}
      
      for (function = agr_func_tab; function->name; function++)
	if (!strcmp (function->name, tokid))
	  break;
      if (NULL == function->name)
	{
	  msg (SE, _("Unknown aggregation function %s."), tokid);
	  goto lossage;
	}
      func_index = function - agr_func_tab;
      lex_get ();

      /* Check for leading lparen. */
      if (!lex_match ('('))
	{
	  if (func_index == N)
	    func_index = N_NO_VARS;
	  else if (func_index == NU)
	    func_index = NU_NO_VARS;
	  else
	    {
	      lex_error (_("expecting `('"));
	      goto lossage;
	    }
	} else {
	  /* Parse list of source variables. */
	  {
	    int pv_opts = PV_NO_SCRATCH;

	    if (func_index == SUM || func_index == MEAN || func_index == SD)
	      pv_opts |= PV_NUMERIC;
	    else if (function->n_args)
	      pv_opts |= PV_SAME_TYPE;

	    if (!parse_variables (default_dict, &src, &n_src, pv_opts))
	      goto lossage;
	  }

	  /* Parse function arguments, for those functions that
	     require arguments. */
	  if (function->n_args != 0)
	    for (i = 0; i < function->n_args; i++)
	      {
		int type;
	    
		lex_match (',');
		if (token == T_STRING)
		  {
		    arg[i].c = xstrdup (ds_value (&tokstr));
		    type = ALPHA;
		  }
		else if (token == T_NUM)
		  {
		    arg[i].f = tokval;
		    type = NUMERIC;
		  } else {
		    msg (SE, _("Missing argument %d to %s."), i + 1, function->name);
		    goto lossage;
		  }
	    
		lex_get ();

		if (type != src[0]->type)
		  {
		    msg (SE, _("Arguments to %s must be of same type as "
			       "source variables."),
			 function->name);
		    goto lossage;
		  }
	      }

	  /* Trailing rparen. */
	  if (!lex_match(')'))
	    {
	      lex_error (_("expecting `)'"));
	      goto lossage;
	    }
	  
	  /* Now check that the number of source variables match the
	     number of target variables.  Do this here because if we
	     do it earlier then the user can get very misleading error
	     messages; i.e., `AGGREGATE x=SUM(y t).' will get this
	     error message when a proper message would be more like
	     `unknown variable t'. */
	  if (n_src != n_dest)
	    {
	      msg (SE, _("Number of source variables (%d) does not match "
			 "number of target variables (%d)."),
		   n_src, n_dest);
	      goto lossage;
	    }
	}
	
      /* Finally add these to the linked list of aggregation
         variables. */
      for (i = 0; i < n_dest; i++)
	{
	  struct agr_var *v = xmalloc (sizeof *v);

	  /* Add variable to chain. */
	  if (agr_first)
	    agr_next = agr_next->next = v;
	  else
	    agr_first = agr_next = v;
	  agr_next->next = NULL;
	  
	  /* Create the target variable in the aggregate
             dictionary. */
	  {
	    struct variable *destvar;
	    
	    agr_next->function = func_index;

	    if (src)
	      {
		int output_width;

		agr_next->src = src[i];
		
		if (src[i]->type == ALPHA)
		  {
		    agr_next->function |= FSTRING;
		    agr_next->string = xmalloc (src[i]->width);
		  }
		
		if (agr_next->src->type == NUMERIC || function->alpha_type == NUMERIC)
		  output_width = 0;
		else
		  output_width = agr_next->src->width;

		if (function->alpha_type == ALPHA)
		  destvar = dict_clone_var (agr_dict, agr_next->src, dest[i]);
		else
		  {
		    destvar = dict_create_var (agr_dict, dest[i], output_width);
		    if (output_width == 0)
		      destvar->print = destvar->write = function->format;
		    if (output_width == 0 && dict_get_weight (default_dict) != NULL
			&& (func_index == N || func_index == N_NO_VARS
			    || func_index == NU || func_index == NU_NO_VARS))
		      {
			struct fmt_spec f = {FMT_F, 8, 2};
		      
			destvar->print = destvar->write = f;
		      }
		  }
	      } else {
		agr_next->src = NULL;
		destvar = dict_create_var (agr_dict, dest[i], 0);
	      }
	  
	    if (!destvar)
	      {
		msg (SE, _("Variable name %s is not unique within the "
			   "aggregate file dictionary, which contains "
			   "the aggregate variables and the break "
			   "variables."),
		     dest[i]);
		free (dest[i]);
		goto lossage;
	      }

	    free (dest[i]);
	    if (dest_label[i])
	      {
		destvar->label = dest_label[i];
		dest_label[i] = NULL;
	      }
	    else if (function->alpha_type == ALPHA)
	      destvar->print = destvar->write = function->format;

	    agr_next->dest = destvar;
	  }
	  
	  agr_next->include_missing = include_missing;

	  if (agr_next->src != NULL)
	    {
	      int j;

	      if (agr_next->src->type == NUMERIC)
		for (j = 0; j < function->n_args; j++)
		  agr_next->arg[j].f = arg[j].f;
	      else
		for (j = 0; j < function->n_args; j++)
		  agr_next->arg[j].c = xstrdup (arg[j].c);
	    }
	}
      
      if (src != NULL && src[0]->type == ALPHA)
	for (i = 0; i < function->n_args; i++)
	  {
	    free (arg[i].c);
	    arg[i].c = NULL;
	  }

      free (src);
      free (dest);
      free (dest_label);

      if (!lex_match ('/'))
	{
	  if (token == '.')
	    return 1;

	  lex_error ("expecting end of command");
	  return 0;
	}
      continue;
      
    lossage:
      for (i = 0; i < n_dest; i++)
	{
	  free (dest[i]);
	  free (dest_label[i]);
	}
      free (dest);
      free (dest_label);
      free (arg[0].c);
      free (arg[1].c);
      if (src && n_src && src[0]->type == ALPHA)
	for (i = 0; i < function->n_args; i++)
	  {
	    free(arg[i].c);
	    arg[i].c = NULL;
	  }
      free (src);
	
      return 0;
    }
}

/* Frees all the state for the AGGREGATE procedure. */
static void
free_aggregate_functions (void)
{
  struct agr_var *iter, *next;

  if (agr_dict)
    dict_destroy (agr_dict);
  for (iter = agr_first; iter; iter = next)
    {
      next = iter->next;

      if (iter->function & FSTRING)
	{
	  int n_args;
	  int i;

	  n_args = agr_func_tab[iter->function & FUNC].n_args;
	  for (i = 0; i < n_args; i++)
	    free (iter->arg[i].c);
	  free (iter->string);
	}
      free (iter);
    }
}

/* Execution. */

static void accumulate_aggregate_info (struct ccase *input);
static void dump_aggregate_info (struct ccase *output);

/* Processes a single case INPUT for aggregation.  If output is
   warranted, it is written to case OUTPUT, which may be (but need not
   be) an alias to INPUT.  Returns -1 when output is performed, -2
   otherwise. */
/* The code in this function has an eerie similarity to
   vfm.c:SPLIT_FILE_procfunc()... */
static int
aggregate_single_case (struct ccase *input, struct ccase *output)
{
  /* The first case always begins a new break group.  We also need to
     preserve the values of the case for later comparison. */
  if (case_count++ == 0)
    {
      int n_elem = 0;
      
      {
	int i;

	for (i = 0; i < nv_sort; i++)
	  n_elem += v_sort[i]->nv;
      }
      
      prev_case = xmalloc (sizeof *prev_case * n_elem);

      /* Copy INPUT into prev_case. */
      {
	union value *iter = prev_case;
	int i;

	for (i = 0; i < nv_sort; i++)
	  {
	    struct variable *v = v_sort[i];
	    
	    if (v->type == NUMERIC)
	      (iter++)->f = input->data[v->fv].f;
	    else
	      {
		memcpy (iter->s, input->data[v->fv].s, v->width);
		iter += v->nv;
	      }
	  }
      }
	    
      accumulate_aggregate_info (input);
	
      return -2;
    }
      
  /* Compare the value of each break variable to the values on the
     previous case. */
  {
    union value *iter = prev_case;
    int i;
    
    for (i = 0; i < nv_sort; i++)
      {
	struct variable *v = v_sort[i];
      
	switch (v->type)
	  {
	  case NUMERIC:
	    if (input->data[v->fv].f != iter->f)
	      goto not_equal;
	    iter++;
	    break;
	  case ALPHA:
	    if (memcmp (input->data[v->fv].s, iter->s, v->width))
	      goto not_equal;
	    iter += v->nv;
	    break;
	  default:
	    assert (0);
	  }
      }
  }

  accumulate_aggregate_info (input);

  return -2;
  
not_equal:
  /* The values of the break variable are different from the values on
     the previous case.  That means that it's time to dump aggregate
     info. */
  dump_aggregate_info (output);
  initialize_aggregate_info ();
  accumulate_aggregate_info (input);

  /* Copy INPUT into prev_case. */
  {
    union value *iter = prev_case;
    int i;

    for (i = 0; i < nv_sort; i++)
      {
	struct variable *v = v_sort[i];
	    
	if (v->type == NUMERIC)
	  (iter++)->f = input->data[v->fv].f;
	else
	  {
	    memcpy (iter->s, input->data[v->fv].s, v->width);
	    iter += v->nv;
	  }
      }
  }
  
  return -1;
}

/* Accumulates aggregation data from the case INPUT. */
static void 
accumulate_aggregate_info (struct ccase *input)
{
  struct agr_var *iter;
  double weight;

  weight = dict_get_case_weight (default_dict, input);

  for (iter = agr_first; iter; iter = iter->next)
    if (iter->src)
      {
	union value *v = &input->data[iter->src->fv];

	if ((!iter->include_missing && is_missing (v, iter->src))
	    || (iter->include_missing && iter->src->type == NUMERIC
		&& v->f == SYSMIS))
	  {
	    switch (iter->function)
	      {
	      case NMISS:
		iter->dbl[0] += weight;
                break;
	      case NUMISS:
		iter->int1++;
		break;
	      }
	    iter->missing = 1;
	    continue;
	  }
	
	/* This is horrible.  There are too many possibilities. */
	switch (iter->function)
	  {
	  case SUM:
	    iter->dbl[0] += v->f;
	    break;
	  case MEAN:
            iter->dbl[0] += v->f * weight;
            iter->dbl[1] += weight;
            break;
	  case SD: 
            {
              double product = v->f * weight;
              iter->dbl[0] += product;
              iter->dbl[1] += product * v->f;
              iter->dbl[2] += weight;
              break; 
            }
	  case MAX:
	    iter->dbl[0] = max (iter->dbl[0], v->f);
	    iter->int1 = 1;
	    break;
	  case MAX | FSTRING:
	    if (memcmp (iter->string, v->s, iter->src->width) < 0)
	      memcpy (iter->string, v->s, iter->src->width);
	    iter->int1 = 1;
	    break;
	  case MIN:
	    iter->dbl[0] = min (iter->dbl[0], v->f);
	    iter->int1 = 1;
	    break;
	  case MIN | FSTRING:
	    if (memcmp (iter->string, v->s, iter->src->width) > 0)
	      memcpy (iter->string, v->s, iter->src->width);
	    iter->int1 = 1;
	    break;
	  case FGT:
	  case PGT:
            if (v->f > iter->arg[0].f)
              iter->dbl[0] += weight;
            iter->dbl[1] += weight;
            break;
	  case FGT | FSTRING:
	  case PGT | FSTRING:
            if (memcmp (iter->arg[0].c, v->s, iter->src->width) < 0)
              iter->dbl[0] += weight;
            iter->dbl[1] += weight;
            break;
	  case FLT:
	  case PLT:
            if (v->f < iter->arg[0].f)
              iter->dbl[0] += weight;
            iter->dbl[1] += weight;
            break;
	  case FLT | FSTRING:
	  case PLT | FSTRING:
            if (memcmp (iter->arg[0].c, v->s, iter->src->width) > 0)
              iter->dbl[0] += weight;
            iter->dbl[1] += weight;
            break;
	  case FIN:
	  case PIN:
            if (iter->arg[0].f <= v->f && v->f <= iter->arg[1].f)
              iter->dbl[0] += weight;
            iter->dbl[1] += weight;
            break;
	  case FIN | FSTRING:
	  case PIN | FSTRING:
            if (memcmp (iter->arg[0].c, v->s, iter->src->width) <= 0
                && memcmp (iter->arg[1].c, v->s, iter->src->width) >= 0)
              iter->dbl[0] += weight;
            iter->dbl[1] += weight;
            break;
	  case FOUT:
	  case POUT:
            if (iter->arg[0].f > v->f || v->f > iter->arg[1].f)
              iter->dbl[0] += weight;
            iter->dbl[1] += weight;
            break;
	  case FOUT | FSTRING:
	  case POUT | FSTRING:
            if (memcmp (iter->arg[0].c, v->s, iter->src->width) > 0
                && memcmp (iter->arg[1].c, v->s, iter->src->width) < 0)
              iter->dbl[0] += weight;
            iter->dbl[1] += weight;
            break;
	  case N:
	    iter->dbl[0] += weight;
	    break;
	  case NU:
	    iter->int1++;
	    break;
	  case FIRST:
	    if (iter->int1 == 0)
	      {
		iter->dbl[0] = v->f;
		iter->int1 = 1;
	      }
	    break;
	  case FIRST | FSTRING:
	    if (iter->int1 == 0)
	      {
		memcpy (iter->string, v->s, iter->src->width);
		iter->int1 = 1;
	      }
	    break;
	  case LAST:
	    iter->dbl[0] = v->f;
	    iter->int1 = 1;
	    break;
	  case LAST | FSTRING:
	    memcpy (iter->string, v->s, iter->src->width);
	    iter->int1 = 1;
	    break;
	  default:
	    assert (0);
	  }
    } else {
      switch (iter->function)
	{
	case N_NO_VARS:
	  iter->dbl[0] += weight;
	  break;
	case NU_NO_VARS:
	  iter->int1++;
	  break;
	default:
	  assert (0);
	}
    }
}

/* We've come to a record that differs from the previous in one or
   more of the break variables.  Make an output record from the
   accumulated statistics in the OUTPUT case. */
static void 
dump_aggregate_info (struct ccase *output)
{
  debug_printf (("(dumping "));
  
  {
    int n_elem = 0;
    
    {
      int i;

      for (i = 0; i < nv_sort; i++)
	n_elem += v_sort[i]->nv;
    }
    debug_printf (("n_elem=%d:", n_elem));
    memcpy (output->data, prev_case, sizeof (union value) * n_elem);
  }
  
  {
    struct agr_var *i;
  
    for (i = agr_first; i; i = i->next)
      {
	union value *v = &output->data[i->dest->fv];

	debug_printf ((" %d,%d", i->dest->fv, i->dest->nv));

	if (missing == COLUMNWISE && i->missing != 0
	    && (i->function & FUNC) != N && (i->function & FUNC) != NU
	    && (i->function & FUNC) != NMISS && (i->function & FUNC) != NUMISS)
	  {
	    if (i->function & FSTRING)
	      memset (v->s, ' ', i->dest->width);
	    else
	      v->f = SYSMIS;
	    continue;
	  }
	
	switch (i->function)
	  {
	  case SUM:
	    v->f = i->dbl[0];
	    break;
	  case MEAN:
	    v->f = i->dbl[1] != 0.0 ? i->dbl[0] / i->dbl[1] : SYSMIS;
	    break;
	  case SD:
	    v->f = ((i->dbl[2] > 1.0)
		    ? calc_stddev (calc_variance (i->dbl, i->dbl[2]))
		    : SYSMIS);
	    break;
	  case MAX:
	  case MIN:
	    v->f = i->int1 ? i->dbl[0] : SYSMIS;
	    break;
	  case MAX | FSTRING:
	  case MIN | FSTRING:
	    if (i->int1)
	      memcpy (v->s, i->string, i->dest->width);
	    else
	      memset (v->s, ' ', i->dest->width);
	    break;
	  case FGT | FSTRING:
	  case FLT | FSTRING:
	  case FIN | FSTRING:
	  case FOUT | FSTRING:
	    v->f = i->int2 ? (double) i->int1 / (double) i->int2 : SYSMIS;
	    break;
	  case FGT:
	  case FLT:
	  case FIN:
	  case FOUT:
	    v->f = i->dbl[1] ? i->dbl[0] / i->dbl[1] : SYSMIS;
	    break;
	  case PGT:
	  case PGT | FSTRING:
	  case PLT:
	  case PLT | FSTRING:
	  case PIN:
	  case PIN | FSTRING:
	  case POUT:
	  case POUT | FSTRING:
	    v->f = i->dbl[1] ? i->dbl[0] / i->dbl[1] * 100.0 : SYSMIS;
	    break;
	  case N:
	    v->f = i->dbl[0];
            break;
	  case NU:
	    v->f = i->int1;
	    break;
	  case FIRST:
	  case LAST:
	    v->f = i->int1 ? i->dbl[0] : SYSMIS;
	    break;
	  case FIRST | FSTRING:
	  case LAST | FSTRING:
	    if (i->int1)
	      memcpy (v->s, i->string, i->dest->width);
	    else
	      memset (v->s, ' ', i->dest->width);
	    break;
	  case N_NO_VARS:
	    v->f = i->dbl[0];
	    break;
	  case NU_NO_VARS:
	    v->f = i->int1;
	    break;
	  case NMISS:
	    v->f = i->dbl[0];
	    break;
	  case NUMISS:
	    v->f = i->int1;
	    break;
	  default:
	    assert (0);
	  }
      }
  }
  debug_printf ((") "));
}

/* Resets the state for all the aggregate functions. */
static void
initialize_aggregate_info (void)
{
  struct agr_var *iter;

  for (iter = agr_first; iter; iter = iter->next)
    {
      iter->missing = 0;
      switch (iter->function)
	{
	case MIN:
	  iter->dbl[0] = DBL_MAX;
	  break;
	case MIN | FSTRING:
	  memset (iter->string, 255, iter->src->width);
	  break;
	case MAX:
	  iter->dbl[0] = -DBL_MAX;
	  break;
	case MAX | FSTRING:
	  memset (iter->string, 0, iter->src->width);
	  break;
	default:
	  iter->dbl[0] = iter->dbl[1] = iter->dbl[2] = 0.0;
	  iter->int1 = iter->int2 = 0;
	  break;
	}
    }
}

/* Aggregate each case as it comes through.  Cases which aren't needed
   are dropped. */
static int
agr_00x_trns_proc (struct trns_header *h unused, struct ccase *c)
{
  int code = aggregate_single_case (c, compaction_case);
  debug_printf (("%d ", code));
  return code;
}

/* Output the last aggregate case.  It's okay to call the vfm_sink's
   write() method here because end_func is called so soon after all
   the cases have been output; very little has been cleaned up at this
   point. */
static void
agr_00x_end_func (void)
{
  /* Ensure that info for the last break group gets written to the
     active file. */
  dump_aggregate_info (compaction_case);
  vfm_sink_info.ncases++;
  vfm_sink->write ();
}

/* Transform the aggregate case buf_1xx, in internal format, to system
   file format, in buf64_1xx, and write the resultant case to the
   system file. */
static void
write_case_to_sfm (void)
{
  flt64 *p = buf64_1xx;
  int i;

  for (i = 0; i < dict_get_var_cnt (agr_dict); i++)
    {
      struct variable *v = dict_get_var (agr_dict, i);
      
      if (v->type == NUMERIC)
	{
	  double src = buf_1xx->data[v->fv].f;
	  if (src == SYSMIS)
	    *p++ = -FLT64_MAX;
	  else
	    *p++ = src;
	}
      else
	{
	  memcpy (p, buf_1xx->data[v->fv].s, v->width);
	  memset (&((char *) p)[v->width], ' ',
		  REM_RND_UP (v->width, sizeof (flt64)));
	  p += DIV_RND_UP (v->width, sizeof (flt64));
	}
    }

  sfm_write_case (outfile, buf64_1xx, p - buf64_1xx);
}

/* Aggregate the current case and output it if we passed a
   breakpoint. */
static int
agr_10x_trns_proc (struct trns_header *h unused, struct ccase *c)
{
  int code = aggregate_single_case (c, buf_1xx);

  assert (code == -2 || code == -1);
  if (code == -1)
    write_case_to_sfm ();
  return -1;
}

/* Close the system file now that we're done with it.  */
static void
agr_10x_trns_free (struct trns_header *h unused)
{
  fh_close_handle (outfile);
}

/* Ensure that info for the last break group gets written to the
   system file. */
static void
agr_10x_end_func (void)
{
  dump_aggregate_info (buf_1xx);
  write_case_to_sfm ();
}

/* When called with temp_case non-NULL (the normal case), runs the
   case through the aggregater and outputs it to the system file if
   appropriate.  If temp_case is NULL, finishes up writing the last
   case if necessary. */
static int
agr_11x_func (void)
{
  if (temp_case != NULL)
    {
      int code = aggregate_single_case (temp_case, buf_1xx);
      
      assert (code == -2 || code == -1);
      if (code == -1)
	write_case_to_sfm ();
    }
  else
    {
      if (case_count)
	{
	  dump_aggregate_info (buf_1xx);
	  write_case_to_sfm ();
	}
      fh_close_handle (outfile);
    }
  return 1;
}

/* Debugging. */
#if DEBUGGING
/* Print out useful debugging information. */
static void
debug_print (int flags)
{
  printf ("AGGREGATE\n /OUTFILE=%s\n",
	  outfile ? fh_handle_filename (outfile) : "*");

  if (missing == COLUMNWISE)
    puts (" /MISSING=COLUMNWISE");

  if (flags & 2)
    puts (" /DOCUMENT");
  if (flags & 4)
    puts (" /PRESORTED");
  
  {
    int i;

    printf (" /BREAK=");
    for (i = 0; i < nv_sort; i++)
      printf ("%s(%c) ", v_sort[i]->name,
	      v_sort[i]->p.srt.order == SRT_ASCEND ? 'A' : 'D');
    putc ('\n', stdout);
  }
  
  {
    struct agr_var *iter;
    
    for (iter = agr_first; iter; iter = iter->next)
      {
	struct agr_func *f = &agr_func_tab[iter->function & FUNC];
	
	printf (" /%s", iter->dest->name);
	if (iter->dest->label)
	  printf ("'%s'", iter->dest->label);
	printf ("=%s(%s", f->name, iter->src->name);
	if (f->n_args)
	  {
	    int i;
	    
	    for (i = 0; i < f->n_args; i++)
	      {
		putc (',', stdout);
		if (iter->src->type == NUMERIC)
		  printf ("%g", iter->arg[i].f);
		else
		  printf ("%.*s", iter->src->width, iter->arg[i].c);
	      }
	  }
	printf (")\n");
      }
  }
}

#endif /* DEBUGGING */
