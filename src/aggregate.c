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
#include "error.h"
#include <stdlib.h>
#include "alloc.h"
#include "case.h"
#include "casefile.h"
#include "command.h"
#include "error.h"
#include "file-handle.h"
#include "lexer.h"
#include "misc.h"
#include "moments.h"
#include "pool.h"
#include "settings.h"
#include "sfm.h"
#include "sort.h"
#include "str.h"
#include "var.h"
#include "vfm.h"
#include "vfmP.h"

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
    struct moments1 *moments;
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
static const struct agr_func agr_func_tab[] = 
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

/* Missing value types. */
enum missing_treatment
  {
    ITEMWISE,		/* Missing values item by item. */
    COLUMNWISE		/* Missing values column by column. */
  };

/* An entire AGGREGATE procedure. */
struct agr_proc 
  {
    /* We have either an output file or a sink. */
    struct file_handle *out_file;       /* Output file, or null if none. */
    struct case_sink *sink;             /* Sink, or null if none. */

    /* Break variables. */
    struct sort_criteria *sort;         /* Sort criteria. */
    struct variable **break_vars;       /* Break variables. */
    size_t break_var_cnt;               /* Number of break variables. */
    union value *prev_break;            /* Last values of break variables. */

    enum missing_treatment missing;     /* How to treat missing values. */
    struct agr_var *agr_vars;           /* First aggregate variable. */
    struct dictionary *dict;            /* Aggregate dictionary. */
    int case_cnt;                       /* Counts aggregated cases. */
    struct ccase agr_case;              /* Aggregate case for output. */
    flt64 *sfm_agr_case;                /* Aggregate case in SFM format. */
  };

static void initialize_aggregate_info (struct agr_proc *);

/* Prototypes. */
static int parse_aggregate_functions (struct agr_proc *);
static void agr_destroy (struct agr_proc *);
static int aggregate_single_case (struct agr_proc *agr,
                                  const struct ccase *input,
                                  struct ccase *output);
static void dump_aggregate_info (struct agr_proc *agr, struct ccase *output);
static int create_sysfile (struct agr_proc *);

/* Aggregating to the active file. */
static int agr_to_active_file (struct ccase *, void *aux);

/* Aggregating to a system file. */
static void write_case_to_sfm (struct agr_proc *agr);
static int presorted_agr_to_sysfile (struct ccase *, void *aux);

/* Parsing. */

/* Parses and executes the AGGREGATE procedure. */
int
cmd_aggregate (void)
{
  struct agr_proc agr;

  /* Have we seen these subcommands? */
  unsigned seen = 0;

  agr.out_file = NULL;
  agr.sink = NULL;
  agr.missing = ITEMWISE;
  agr.sort = NULL;
  agr.break_vars = NULL;
  agr.agr_vars = NULL;
  agr.dict = NULL;
  agr.case_cnt = 0;
  agr.prev_break = NULL;
  
  agr.dict = dict_create ();
  dict_set_label (agr.dict, dict_get_label (default_dict));
  dict_set_documents (agr.dict, dict_get_documents (default_dict));
  
  /* Read most of the subcommands. */
  for (;;)
    {
      lex_match ('/');
      
      if (lex_match_id ("OUTFILE"))
	{
	  if (seen & 1)
	    {
	      msg (SE, _("%s subcommand given multiple times."),"OUTFILE");
              goto lossage;
	    }
	  seen |= 1;
	      
	  lex_match ('=');
	  if (lex_match ('*'))
	    agr.out_file = NULL;
	  else 
	    {
	      agr.out_file = fh_parse_file_handle ();
	      if (agr.out_file == NULL)
                goto lossage;
	    }
	}
      else if (lex_match_id ("MISSING"))
	{
	  lex_match ('=');
	  if (!lex_match_id ("COLUMNWISE"))
	    {
	      lex_error (_("while expecting COLUMNWISE"));
              goto lossage;
	    }
	  agr.missing = COLUMNWISE;
	}
      else if (lex_match_id ("DOCUMENT"))
	seen |= 2;
      else if (lex_match_id ("PRESORTED"))
	seen |= 4;
      else if (lex_match_id ("BREAK"))
	{
          int i;

	  if (seen & 8)
	    {
	      msg (SE, _("%s subcommand given multiple times."),"BREAK");
              goto lossage;
	    }
	  seen |= 8;

	  lex_match ('=');
          agr.sort = sort_parse_criteria (default_dict,
                                          &agr.break_vars, &agr.break_var_cnt);
          if (agr.sort == NULL)
            goto lossage;
	  
          for (i = 0; i < agr.break_var_cnt; i++)
            {
              struct variable *v = dict_clone_var (agr.dict, agr.break_vars[i],
                                                   agr.break_vars[i]->name);
              assert (v != NULL);
            }
	}
      else break;
    }

  /* Check for proper syntax. */
  if (!(seen & 8))
    msg (SW, _("BREAK subcommand not specified."));
      
  /* Read in the aggregate functions. */
  if (!parse_aggregate_functions (&agr))
    goto lossage;

  /* Delete documents. */
  if (!(seen & 2))
    dict_set_documents (agr.dict, NULL);

  /* Cancel SPLIT FILE. */
  dict_set_split_vars (agr.dict, NULL, 0);
  
  /* Initialize. */
  agr.case_cnt = 0;
  case_create (&agr.agr_case, dict_get_next_value_idx (agr.dict));
  initialize_aggregate_info (&agr);

  /* Output to active file or external file? */
  if (agr.out_file == NULL) 
    {
      /* The active file will be replaced by the aggregated data,
         so TEMPORARY is moot. */
      cancel_temporary ();

      if (agr.sort != NULL && (seen & 4) == 0)
        sort_active_file_in_place (agr.sort);

      agr.sink = create_case_sink (&storage_sink_class, agr.dict, NULL);
      if (agr.sink->class->open != NULL)
        agr.sink->class->open (agr.sink);
      vfm_sink = create_case_sink (&null_sink_class, default_dict, NULL);
      procedure (agr_to_active_file, &agr);
      if (agr.case_cnt > 0) 
        {
          dump_aggregate_info (&agr, &agr.agr_case);
          agr.sink->class->write (agr.sink, &agr.agr_case);
        }
      dict_destroy (default_dict);
      default_dict = agr.dict;
      agr.dict = NULL;
      vfm_source = agr.sink->class->make_source (agr.sink);
      free_case_sink (agr.sink);
    }
  else
    {
      if (!create_sysfile (&agr))
        goto lossage;

      if (agr.sort != NULL && (seen & 4) == 0) 
        {
          /* Sorting is needed. */
          struct casefile *dst;
          struct casereader *reader;
          struct ccase c;
          
          dst = sort_active_file_to_casefile (agr.sort);
          if (dst == NULL)
            goto lossage;
          reader = casefile_get_destructive_reader (dst);
          while (casereader_read_xfer (reader, &c)) 
            {
              if (aggregate_single_case (&agr, &c, &agr.agr_case)) 
                write_case_to_sfm (&agr);
              case_destroy (&c);
            }
          casereader_destroy (reader);
          casefile_destroy (dst);
        }
      else 
        {
          /* Active file is already sorted. */
          procedure (presorted_agr_to_sysfile, &agr);
        }
      
      if (agr.case_cnt > 0) 
        {
          dump_aggregate_info (&agr, &agr.agr_case);
          write_case_to_sfm (&agr);
        }
      fh_close_handle (agr.out_file);
    }
  
  agr_destroy (&agr);
  return CMD_SUCCESS;

lossage:
  agr_destroy (&agr);
  return CMD_FAILURE;
}

/* Create a system file for use in aggregation to an external
   file. */
static int
create_sysfile (struct agr_proc *agr)
{
  struct sfm_write_info w;
  w.h = agr->out_file;
  w.dict = agr->dict;
  w.compress = get_scompression();
  if (!sfm_write_dictionary (&w))
    return 0;

  agr->sfm_agr_case = xmalloc (sizeof *agr->sfm_agr_case * w.case_size);
    
  return 1;
}

/* Parse all the aggregate functions. */
static int
parse_aggregate_functions (struct agr_proc *agr)
{
  struct agr_var *tail; /* Tail of linked list starting at agr->vars. */

  /* Parse everything. */
  tail = NULL;
  for (;;)
    {
      char **dest;
      char **dest_label;
      int n_dest;

      int include_missing;
      const struct agr_func *function;
      int func_index;

      union value arg[2];

      struct variable **src;
      int n_src;

      int i;

      dest = NULL;
      dest_label = NULL;
      n_dest = 0;
      src = NULL;
      function = NULL;
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
	      ds_truncate (&tokstr, 255);
	      dest_label[n_dest - 1] = xstrdup (ds_c_str (&tokstr));
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
		    arg[i].c = xstrdup (ds_c_str (&tokstr));
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
	  if (agr->agr_vars != NULL)
	    tail->next = v;
	  else
	    agr->agr_vars = v;
          tail = v;
	  tail->next = NULL;
          v->moments = NULL;
	  
	  /* Create the target variable in the aggregate
             dictionary. */
	  {
	    struct variable *destvar;
	    
	    v->function = func_index;

	    if (src)
	      {
		int output_width;

		v->src = src[i];
		
		if (src[i]->type == ALPHA)
		  {
		    v->function |= FSTRING;
		    v->string = xmalloc (src[i]->width);
		  }
		
		if (v->src->type == NUMERIC || function->alpha_type == NUMERIC)
		  output_width = 0;
		else
		  output_width = v->src->width;

		if (function->alpha_type == ALPHA)
		  destvar = dict_clone_var (agr->dict, v->src, dest[i]);
		else
		  {
		    destvar = dict_create_var (agr->dict, dest[i], output_width);
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
		v->src = NULL;
		destvar = dict_create_var (agr->dict, dest[i], 0);
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
            destvar->init = 0;
	    if (dest_label[i])
	      {
		destvar->label = dest_label[i];
		dest_label[i] = NULL;
	      }
	    else if (function->alpha_type == ALPHA)
	      destvar->print = destvar->write = function->format;

	    v->dest = destvar;
	  }
	  
	  v->include_missing = include_missing;

	  if (v->src != NULL)
	    {
	      int j;

	      if (v->src->type == NUMERIC)
		for (j = 0; j < function->n_args; j++)
		  v->arg[j].f = arg[j].f;
	      else
		for (j = 0; j < function->n_args; j++)
		  v->arg[j].c = xstrdup (arg[j].c);
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
	    free (arg[i].c);
	    arg[i].c = NULL;
	  }
      free (src);
	
      return 0;
    }
}

/* Destroys AGR. */
static void
agr_destroy (struct agr_proc *agr)
{
  struct agr_var *iter, *next;

  if (agr->dict != NULL)
    dict_destroy (agr->dict);
  if (agr->sort != NULL)
    sort_destroy_criteria (agr->sort);
  free (agr->break_vars);
  for (iter = agr->agr_vars; iter; iter = next)
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
      else if (iter->function == SD)
        moments1_destroy (iter->moments);
      free (iter);
    }
  free (agr->prev_break);
  case_destroy (&agr->agr_case);
}

/* Execution. */

static void accumulate_aggregate_info (struct agr_proc *,
                                       const struct ccase *);
static void dump_aggregate_info (struct agr_proc *, struct ccase *);

/* Processes a single case INPUT for aggregation.  If output is
   warranted, writes it to OUTPUT and returns nonzero.
   Otherwise, returns zero and OUTPUT is unmodified. */
static int
aggregate_single_case (struct agr_proc *agr,
                       const struct ccase *input, struct ccase *output)
{
  /* The first case always begins a new break group.  We also need to
     preserve the values of the case for later comparison. */
  if (agr->case_cnt++ == 0)
    {
      int n_elem = 0;
      
      {
	int i;

	for (i = 0; i < agr->break_var_cnt; i++)
	  n_elem += agr->break_vars[i]->nv;
      }
      
      agr->prev_break = xmalloc (sizeof *agr->prev_break * n_elem);

      /* Copy INPUT into prev_break. */
      {
	union value *iter = agr->prev_break;
	int i;

	for (i = 0; i < agr->break_var_cnt; i++)
	  {
	    struct variable *v = agr->break_vars[i];
	    
	    if (v->type == NUMERIC)
	      (iter++)->f = case_num (input, v->fv);
	    else
	      {
		memcpy (iter->s, case_str (input, v->fv), v->width);
		iter += v->nv;
	      }
	  }
      }
	    
      accumulate_aggregate_info (agr, input);
	
      return 0;
    }
      
  /* Compare the value of each break variable to the values on the
     previous case. */
  {
    union value *iter = agr->prev_break;
    int i;
    
    for (i = 0; i < agr->break_var_cnt; i++)
      {
	struct variable *v = agr->break_vars[i];
      
	switch (v->type)
	  {
	  case NUMERIC:
	    if (case_num (input, v->fv) != iter->f)
	      goto not_equal;
	    iter++;
	    break;
	  case ALPHA:
	    if (memcmp (case_str (input, v->fv), iter->s, v->width))
	      goto not_equal;
	    iter += v->nv;
	    break;
	  default:
	    assert (0);
	  }
      }
  }

  accumulate_aggregate_info (agr, input);

  return 0;
  
not_equal:
  /* The values of the break variable are different from the values on
     the previous case.  That means that it's time to dump aggregate
     info. */
  dump_aggregate_info (agr, output);
  initialize_aggregate_info (agr);
  accumulate_aggregate_info (agr, input);

  /* Copy INPUT into prev_break. */
  {
    union value *iter = agr->prev_break;
    int i;

    for (i = 0; i < agr->break_var_cnt; i++)
      {
	struct variable *v = agr->break_vars[i];
	    
	if (v->type == NUMERIC)
	  (iter++)->f = case_num (input, v->fv);
	else
	  {
	    memcpy (iter->s, case_str (input, v->fv), v->width);
	    iter += v->nv;
	  }
      }
  }
  
  return 1;
}

/* Accumulates aggregation data from the case INPUT. */
static void 
accumulate_aggregate_info (struct agr_proc *agr,
                           const struct ccase *input)
{
  struct agr_var *iter;
  double weight;
  int bad_warn = 1;

  weight = dict_get_case_weight (default_dict, input, &bad_warn);

  for (iter = agr->agr_vars; iter; iter = iter->next)
    if (iter->src)
      {
	const union value *v = case_data (input, iter->src->fv);

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
            moments1_add (iter->moments, v->f, weight);
            break;
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
dump_aggregate_info (struct agr_proc *agr, struct ccase *output)
{
  {
    int value_idx = 0;
    int i;

    for (i = 0; i < agr->break_var_cnt; i++) 
      {
        int nv = agr->break_vars[i]->nv;
        memcpy (case_data_rw (output, value_idx),
                &agr->prev_break[value_idx],
                sizeof (union value) * nv);
        value_idx += nv; 
      }
  }
  
  {
    struct agr_var *i;
  
    for (i = agr->agr_vars; i; i = i->next)
      {
	union value *v = case_data_rw (output, i->dest->fv);

	if (agr->missing == COLUMNWISE && i->missing != 0
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
            {
              double variance;

              /* FIXME: we should use two passes. */
              moments1_calculate (i->moments, NULL, NULL, &variance,
                                 NULL, NULL);
              if (variance != SYSMIS)
                v->f = sqrt (variance);
              else
                v->f = SYSMIS; 
            }
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
}

/* Resets the state for all the aggregate functions. */
static void
initialize_aggregate_info (struct agr_proc *agr)
{
  struct agr_var *iter;

  for (iter = agr->agr_vars; iter; iter = iter->next)
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
        case SD:
          if (iter->moments == NULL)
            iter->moments = moments1_create (MOMENT_VARIANCE);
          else
            moments1_clear (iter->moments);
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
agr_to_active_file (struct ccase *c, void *agr_)
{
  struct agr_proc *agr = agr_;

  if (aggregate_single_case (agr, c, &agr->agr_case)) 
    agr->sink->class->write (agr->sink, &agr->agr_case);

  return 1;
}

/* Writes AGR->agr_case to AGR->out_file. */
static void
write_case_to_sfm (struct agr_proc *agr)
{
  flt64 *p;
  int i;

  p = agr->sfm_agr_case;
  for (i = 0; i < dict_get_var_cnt (agr->dict); i++)
    {
      struct variable *v = dict_get_var (agr->dict, i);
      
      if (v->type == NUMERIC)
	{
	  double src = case_num (&agr->agr_case, v->fv);
	  if (src == SYSMIS)
	    *p++ = -FLT64_MAX;
	  else
	    *p++ = src;
	}
      else
	{
	  memcpy (p, case_str (&agr->agr_case, v->fv), v->width);
	  memset (&((char *) p)[v->width], ' ',
		  REM_RND_UP (v->width, sizeof (flt64)));
	  p += DIV_RND_UP (v->width, sizeof (flt64));
	}
    }

  sfm_write_case (agr->out_file, agr->sfm_agr_case, p - agr->sfm_agr_case);
}

/* Aggregate the current case and output it if we passed a
   breakpoint. */
static int
presorted_agr_to_sysfile (struct ccase *c, void *agr_) 
{
  struct agr_proc *agr = agr_;

  if (aggregate_single_case (agr, c, &agr->agr_case)) 
    write_case_to_sfm (agr);

  return 1;
}
