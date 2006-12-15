/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>

#include <stdlib.h>

#include <data/any-writer.h>
#include <data/case-sink.h>
#include <data/case.h>
#include <data/casefile.h>
#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/format.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <data/storage-stream.h>
#include <data/sys-file-writer.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/file-handle.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <language/stats/sort-criteria.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>
#include <math/moments.h>
#include <math/sort.h>

#include "minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Argument for AGGREGATE function. */
union agr_argument
  {
    double f;                           /* Numeric. */
    char *c;                            /* Short or long string. */
  };

/* Specifies how to make an aggregate variable. */
struct agr_var
  {
    struct agr_var *next;		/* Next in list. */

    /* Collected during parsing. */
    struct variable *src;	/* Source variable. */
    struct variable *dest;	/* Target variable. */
    int function;		/* Function. */
    int include_missing;	/* 1=Include user-missing values. */
    union agr_argument arg[2];	/* Arguments. */

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
    size_t n_args;              /* Number of arguments. */
    enum var_type alpha_type;   /* When given ALPHA arguments, output type. */
    struct fmt_spec format;	/* Format spec if alpha_type != ALPHA. */
  };

/* Attributes of aggregation functions. */
static const struct agr_func agr_func_tab[] = 
  {
    {"<NONE>",  0, -1,          {0, 0, 0}},
    {"SUM",     0, -1,          {FMT_F, 8, 2}},
    {"MEAN",	0, -1,          {FMT_F, 8, 2}},
    {"SD",      0, -1,          {FMT_F, 8, 2}},
    {"MAX",     0, VAR_STRING,  {-1, -1, -1}}, 
    {"MIN",     0, VAR_STRING,  {-1, -1, -1}}, 
    {"PGT",     1, VAR_NUMERIC, {FMT_F, 5, 1}},      
    {"PLT",     1, VAR_NUMERIC, {FMT_F, 5, 1}},       
    {"PIN",     2, VAR_NUMERIC, {FMT_F, 5, 1}},       
    {"POUT",    2, VAR_NUMERIC, {FMT_F, 5, 1}},       
    {"FGT",     1, VAR_NUMERIC, {FMT_F, 5, 3}},       
    {"FLT",     1, VAR_NUMERIC, {FMT_F, 5, 3}},       
    {"FIN",     2, VAR_NUMERIC, {FMT_F, 5, 3}},       
    {"FOUT",    2, VAR_NUMERIC, {FMT_F, 5, 3}},       
    {"N",       0, VAR_NUMERIC, {FMT_F, 7, 0}},       
    {"NU",      0, VAR_NUMERIC, {FMT_F, 7, 0}},       
    {"NMISS",   0, VAR_NUMERIC, {FMT_F, 7, 0}},       
    {"NUMISS",  0, VAR_NUMERIC, {FMT_F, 7, 0}},       
    {"FIRST",   0, VAR_STRING,  {-1, -1, -1}}, 
    {"LAST",    0, VAR_STRING,  {-1, -1, -1}},
    {NULL,      0, -1,          {-1, -1, -1}},
    {"N",       0, VAR_NUMERIC, {FMT_F, 7, 0}},
    {"NU",      0, VAR_NUMERIC, {FMT_F, 7, 0}},
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
    struct any_writer *writer;          /* Output file, or null if none. */
    struct case_sink *sink;             /* Sink, or null if none. */

    /* Break variables. */
    struct sort_criteria *sort;         /* Sort criteria. */
    struct variable **break_vars;       /* Break variables. */
    size_t break_var_cnt;               /* Number of break variables. */
    struct ccase break_case;            /* Last values of break variables. */

    enum missing_treatment missing;     /* How to treat missing values. */
    struct agr_var *agr_vars;           /* First aggregate variable. */
    struct dictionary *dict;            /* Aggregate dictionary. */
    const struct dictionary *src_dict;  /* Dict of the source */
    int case_cnt;                       /* Counts aggregated cases. */
    struct ccase agr_case;              /* Aggregate case for output. */
  };

static void initialize_aggregate_info (struct agr_proc *,
                                       const struct ccase *);

/* Prototypes. */
static bool parse_aggregate_functions (struct lexer *, const struct dictionary *,
				       struct agr_proc *);
static void agr_destroy (struct agr_proc *);
static bool aggregate_single_case (struct agr_proc *agr,
				   const struct ccase *input,
				   struct ccase *output);
static void dump_aggregate_info (struct agr_proc *agr, struct ccase *output);

/* Aggregating to the active file. */
static bool agr_to_active_file (const struct ccase *, void *aux, const struct dataset *);

/* Aggregating to a system file. */
static bool presorted_agr_to_sysfile (const struct ccase *, void *aux, const struct dataset *);

/* Parsing. */

/* Parses and executes the AGGREGATE procedure. */
int
cmd_aggregate (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct agr_proc agr;
  struct file_handle *out_file = NULL;

  bool copy_documents = false;
  bool presorted = false;
  bool saw_direction;

  memset(&agr, 0 , sizeof (agr));
  agr.missing = ITEMWISE;
  case_nullify (&agr.break_case);
  
  agr.dict = dict_create ();
  agr.src_dict = dict;
  dict_set_label (agr.dict, dict_get_label (dict));
  dict_set_documents (agr.dict, dict_get_documents (dict));

  /* OUTFILE subcommand must be first. */
  if (!lex_force_match_id (lexer, "OUTFILE"))
    goto error;
  lex_match (lexer, '=');
  if (!lex_match (lexer, '*'))
    {
      out_file = fh_parse (lexer, FH_REF_FILE | FH_REF_SCRATCH);
      if (out_file == NULL)
        goto error;
    }
  
  /* Read most of the subcommands. */
  for (;;)
    {
      lex_match (lexer, '/');
      
      if (lex_match_id (lexer, "MISSING"))
	{
	  lex_match (lexer, '=');
	  if (!lex_match_id (lexer, "COLUMNWISE"))
	    {
	      lex_error (lexer, _("while expecting COLUMNWISE"));
              goto error;
	    }
	  agr.missing = COLUMNWISE;
	}
      else if (lex_match_id (lexer, "DOCUMENT"))
        copy_documents = true;
      else if (lex_match_id (lexer, "PRESORTED"))
        presorted = true;
      else if (lex_match_id (lexer, "BREAK"))
	{
          int i;

	  lex_match (lexer, '=');
          agr.sort = sort_parse_criteria (lexer, dict,
                                          &agr.break_vars, &agr.break_var_cnt,
                                          &saw_direction, NULL);
          if (agr.sort == NULL)
            goto error;
	  
          for (i = 0; i < agr.break_var_cnt; i++)
            dict_clone_var_assert (agr.dict, agr.break_vars[i],
                                   var_get_name (agr.break_vars[i]));

          /* BREAK must follow the options. */
          break;
	}
      else
        {
          lex_error (lexer, _("expecting BREAK"));
          goto error;
        }
    }
  if (presorted && saw_direction)
    msg (SW, _("When PRESORTED is specified, specifying sorting directions "
               "with (A) or (D) has no effect.  Output data will be sorted "
               "the same way as the input data."));
      
  /* Read in the aggregate functions. */
  lex_match (lexer, '/');
  if (!parse_aggregate_functions (lexer, dict, &agr))
    goto error;

  /* Delete documents. */
  if (!copy_documents)
    dict_set_documents (agr.dict, NULL);

  /* Cancel SPLIT FILE. */
  dict_set_split_vars (agr.dict, NULL, 0);
  
  /* Initialize. */
  agr.case_cnt = 0;
  case_create (&agr.agr_case, dict_get_next_value_idx (agr.dict));

  /* Output to active file or external file? */
  if (out_file == NULL) 
    {
      /* The active file will be replaced by the aggregated data,
         so TEMPORARY is moot. */
      proc_cancel_temporary_transformations (ds);

      if (agr.sort != NULL && !presorted) 
        {
          if (!sort_active_file_in_place (ds, agr.sort))
            goto error;
        }

      agr.sink = create_case_sink (&storage_sink_class, agr.dict, NULL);
      if (agr.sink->class->open != NULL)
        agr.sink->class->open (agr.sink);
      proc_set_sink (ds, 
		     create_case_sink (&null_sink_class, 
				       dict, NULL));
      if (!procedure (ds, agr_to_active_file, &agr))
        goto error;
      if (agr.case_cnt > 0) 
        {
          dump_aggregate_info (&agr, &agr.agr_case);
          if (!agr.sink->class->write (agr.sink, &agr.agr_case))
            goto error;
        }
      discard_variables (ds);
      dict_destroy (dict);
      dataset_set_dict (ds, agr.dict);
      agr.dict = NULL;
      proc_set_source (ds, 
		       agr.sink->class->make_source (agr.sink));
      free_case_sink (agr.sink);
    }
  else
    {
      agr.writer = any_writer_open (out_file, agr.dict);
      if (agr.writer == NULL)
        goto error;
      
      if (agr.sort != NULL && !presorted) 
        {
          /* Sorting is needed. */
          struct casefile *dst;
          struct casereader *reader;
          struct ccase c;
          bool ok = true;
          
          dst = sort_active_file_to_casefile (ds, agr.sort);
          if (dst == NULL)
            goto error;
          reader = casefile_get_destructive_reader (dst);
          while (ok && casereader_read_xfer (reader, &c)) 
            {
              if (aggregate_single_case (&agr, &c, &agr.agr_case)) 
                ok = any_writer_write (agr.writer, &agr.agr_case);
              case_destroy (&c);
            }
          casereader_destroy (reader);
          if (ok)
            ok = !casefile_error (dst);
          casefile_destroy (dst);
          if (!ok)
            goto error;
        }
      else 
        {
          /* Active file is already sorted. */
          if (!procedure (ds, presorted_agr_to_sysfile, &agr))
            goto error;
        }
      
      if (agr.case_cnt > 0) 
        {
          dump_aggregate_info (&agr, &agr.agr_case);
          any_writer_write (agr.writer, &agr.agr_case);
        }
      if (any_writer_error (agr.writer))
        goto error;
    }
  
  agr_destroy (&agr);
  return CMD_SUCCESS;

error:
  agr_destroy (&agr);
  return CMD_CASCADING_FAILURE;
}

/* Parse all the aggregate functions. */
static bool
parse_aggregate_functions (struct lexer *lexer, const struct dictionary *dict, struct agr_proc *agr)
{
  struct agr_var *tail; /* Tail of linked list starting at agr->vars. */

  /* Parse everything. */
  tail = NULL;
  for (;;)
    {
      char **dest;
      char **dest_label;
      size_t n_dest;
      struct string function_name;

      int include_missing;
      const struct agr_func *function;
      int func_index;

      union agr_argument arg[2];

      struct variable **src;
      size_t n_src;

      size_t i;

      dest = NULL;
      dest_label = NULL;
      n_dest = 0;
      src = NULL;
      function = NULL;
      n_src = 0;
      arg[0].c = NULL;
      arg[1].c = NULL;

      /* Parse the list of target variables. */
      while (!lex_match (lexer, '='))
	{
	  size_t n_dest_prev = n_dest;
	  
	  if (!parse_DATA_LIST_vars (lexer, &dest, &n_dest,
                                     PV_APPEND | PV_SINGLE | PV_NO_SCRATCH))
	    goto error;

	  /* Assign empty labels. */
	  {
	    int j;

	    dest_label = xnrealloc (dest_label, n_dest, sizeof *dest_label);
	    for (j = n_dest_prev; j < n_dest; j++)
	      dest_label[j] = NULL;
	  }


	  
	  if (lex_token (lexer) == T_STRING)
	    {
	      struct string label;
	      ds_init_string (&label, lex_tokstr (lexer));

	      ds_truncate (&label, 255);
	      dest_label[n_dest - 1] = ds_xstrdup (&label);
	      lex_get (lexer);
	      ds_destroy (&label);
	    }
	}

      /* Get the name of the aggregation function. */
      if (lex_token (lexer) != T_ID)
	{
	  lex_error (lexer, _("expecting aggregation function"));
	  goto error;
	}

      include_missing = 0;

      ds_init_string (&function_name, lex_tokstr (lexer));

      ds_chomp (&function_name, '.');

      if (lex_tokid(lexer)[strlen (lex_tokid (lexer)) - 1] == '.')
	  include_missing = 1;

      for (function = agr_func_tab; function->name; function++)
	if (!strcasecmp (function->name, ds_cstr (&function_name)))
	  break;
      if (NULL == function->name)
	{
	  msg (SE, _("Unknown aggregation function %s."), 
	       ds_cstr (&function_name));
	  goto error;
	}
      ds_destroy (&function_name);
      func_index = function - agr_func_tab;
      lex_get (lexer);

      /* Check for leading lparen. */
      if (!lex_match (lexer, '('))
	{
	  if (func_index == N)
	    func_index = N_NO_VARS;
	  else if (func_index == NU)
	    func_index = NU_NO_VARS;
	  else
	    {
	      lex_error (lexer, _("expecting `('"));
	      goto error;
	    }
	}
      else
        {
	  /* Parse list of source variables. */
	  {
	    int pv_opts = PV_NO_SCRATCH;

	    if (func_index == SUM || func_index == MEAN || func_index == SD)
	      pv_opts |= PV_NUMERIC;
	    else if (function->n_args)
	      pv_opts |= PV_SAME_TYPE;

	    if (!parse_variables (lexer, dict, &src, &n_src, pv_opts))
	      goto error;
	  }

	  /* Parse function arguments, for those functions that
	     require arguments. */
	  if (function->n_args != 0)
	    for (i = 0; i < function->n_args; i++)
	      {
		int type;
	    
		lex_match (lexer, ',');
		if (lex_token (lexer) == T_STRING)
		  {
		    arg[i].c = ds_xstrdup (lex_tokstr (lexer));
		    type = VAR_STRING;
		  }
		else if (lex_is_number (lexer))
		  {
		    arg[i].f = lex_tokval (lexer);
		    type = VAR_NUMERIC;
		  }
                else
                  {
		    msg (SE, _("Missing argument %d to %s."), i + 1,
                         function->name);
		    goto error;
		  }
	    
		lex_get (lexer);

		if (type != var_get_type (src[0]))
		  {
		    msg (SE, _("Arguments to %s must be of same type as "
			       "source variables."),
			 function->name);
		    goto error;
		  }
	      }

	  /* Trailing rparen. */
	  if (!lex_match (lexer, ')'))
	    {
	      lex_error (lexer, _("expecting `)'"));
	      goto error;
	    }
	  
	  /* Now check that the number of source variables match
	     the number of target variables.  If we check earlier
	     than this, the user can get very misleading error
	     message, i.e. `AGGREGATE x=SUM(y t).' will get this
	     error message when a proper message would be more
	     like `unknown variable t'. */
	  if (n_src != n_dest)
	    {
	      msg (SE, _("Number of source variables (%u) does not match "
			 "number of target variables (%u)."),
		   (unsigned) n_src, (unsigned) n_dest);
	      goto error;
	    }

          if ((func_index == PIN || func_index == POUT
              || func_index == FIN || func_index == FOUT) 
              && (var_is_numeric (src[0])
                  ? arg[0].f > arg[1].f
                  : str_compare_rpad (arg[0].c, arg[1].c) > 0))
            {
              union agr_argument t = arg[0];
              arg[0] = arg[1];
              arg[1] = t;
                  
              msg (SW, _("The value arguments passed to the %s function "
                         "are out-of-order.  They will be treated as if "
                         "they had been specified in the correct order."),
                   function->name);
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
		v->src = src[i];
		
		if (var_is_alpha (src[i]))
		  {
		    v->function |= FSTRING;
		    v->string = xmalloc (var_get_width (src[i]));
		  }

		if (function->alpha_type == VAR_STRING)
		  destvar = dict_clone_var (agr->dict, v->src, dest[i]);
		else
                  {
                    assert (var_is_numeric (v->src)
                            || function->alpha_type == VAR_NUMERIC);
                    destvar = dict_create_var (agr->dict, dest[i], 0);
                    if (destvar != NULL) 
                      {
                        struct fmt_spec f;
                        if ((func_index == N || func_index == NMISS)
                            && dict_get_weight (dict) != NULL)
                          f = fmt_for_output (FMT_F, 8, 2); 
                        else
                          f = function->format;
                        var_set_both_formats (destvar, &f);
                      }
                  }
	      } else {
                struct fmt_spec f;
		v->src = NULL;
		destvar = dict_create_var (agr->dict, dest[i], 0);
                if (func_index == N_NO_VARS && dict_get_weight (dict) != NULL)
                  f = fmt_for_output (FMT_F, 8, 2); 
                else
                  f = function->format;
                var_set_both_formats (destvar, &f);
	      }
	  
	    if (!destvar)
	      {
		msg (SE, _("Variable name %s is not unique within the "
			   "aggregate file dictionary, which contains "
			   "the aggregate variables and the break "
			   "variables."),
		     dest[i]);
		goto error;
	      }

	    free (dest[i]);
	    if (dest_label[i])
              var_set_label (destvar, dest_label[i]);

	    v->dest = destvar;
	  }
	  
	  v->include_missing = include_missing;

	  if (v->src != NULL)
	    {
	      int j;

	      if (var_is_numeric (v->src))
		for (j = 0; j < function->n_args; j++)
		  v->arg[j].f = arg[j].f;
	      else
		for (j = 0; j < function->n_args; j++)
		  v->arg[j].c = xstrdup (arg[j].c);
	    }
	}
      
      if (src != NULL && var_is_alpha (src[0]))
	for (i = 0; i < function->n_args; i++)
	  {
	    free (arg[i].c);
	    arg[i].c = NULL;
	  }

      free (src);
      free (dest);
      free (dest_label);

      if (!lex_match (lexer, '/'))
	{
	  if (lex_token (lexer) == '.')
	    return true;

	  lex_error (lexer, "expecting end of command");
	  return false;
	}
      continue;
      
    error:
      ds_destroy (&function_name);
      for (i = 0; i < n_dest; i++)
	{
	  free (dest[i]);
	  free (dest_label[i]);
	}
      free (dest);
      free (dest_label);
      free (arg[0].c);
      free (arg[1].c);
      if (src && n_src && var_is_alpha (src[0]))
	for (i = 0; i < function->n_args; i++)
	  {
	    free (arg[i].c);
	    arg[i].c = NULL;
	  }
      free (src);
	
      return false;
    }
}

/* Destroys AGR. */
static void
agr_destroy (struct agr_proc *agr)
{
  struct agr_var *iter, *next;

  any_writer_close (agr->writer);
  if (agr->sort != NULL)
    sort_destroy_criteria (agr->sort);
  free (agr->break_vars);
  case_destroy (&agr->break_case);
  for (iter = agr->agr_vars; iter; iter = next)
    {
      next = iter->next;

      if (iter->function & FSTRING)
	{
	  size_t n_args;
	  size_t i;

	  n_args = agr_func_tab[iter->function & FUNC].n_args;
	  for (i = 0; i < n_args; i++)
	    free (iter->arg[i].c);
	  free (iter->string);
	}
      else if (iter->function == SD)
        moments1_destroy (iter->moments);
      free (iter);
    }
  if (agr->dict != NULL)
    dict_destroy (agr->dict);

  case_destroy (&agr->agr_case);
}

/* Execution. */

static void accumulate_aggregate_info (struct agr_proc *,
                                       const struct ccase *);
static void dump_aggregate_info (struct agr_proc *, struct ccase *);

/* Processes a single case INPUT for aggregation.  If output is
   warranted, writes it to OUTPUT and returns true.
   Otherwise, returns false and OUTPUT is unmodified. */
static bool
aggregate_single_case (struct agr_proc *agr,
                       const struct ccase *input, struct ccase *output)
{
  bool finished_group = false;
  
  if (agr->case_cnt++ == 0)
    initialize_aggregate_info (agr, input);
  else if (case_compare (&agr->break_case, input,
                         agr->break_vars, agr->break_var_cnt))
    {
      dump_aggregate_info (agr, output);
      finished_group = true;

      initialize_aggregate_info (agr, input);
    }

  accumulate_aggregate_info (agr, input);
  return finished_group;
}

/* Accumulates aggregation data from the case INPUT. */
static void 
accumulate_aggregate_info (struct agr_proc *agr,
                           const struct ccase *input)
{
  struct agr_var *iter;
  double weight;
  bool bad_warn = true;

  weight = dict_get_case_weight (agr->src_dict, input, &bad_warn);

  for (iter = agr->agr_vars; iter; iter = iter->next)
    if (iter->src)
      {
	const union value *v = case_data (input, iter->src);
        int src_width = var_get_width (iter->src);

	if (iter->include_missing
            ? var_is_numeric (iter->src) && v->f == SYSMIS
            : var_is_value_missing (iter->src, v))
	  {
	    switch (iter->function)
	      {
	      case NMISS:
	      case NMISS | FSTRING:
		iter->dbl[0] += weight;
                break;
	      case NUMISS:
	      case NUMISS | FSTRING:
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
	    iter->dbl[0] += v->f * weight;
            iter->int1 = 1;
	    break;
	  case MEAN:
            iter->dbl[0] += v->f * weight;
            iter->dbl[1] += weight;
            break;
	  case SD:
            moments1_add (iter->moments, v->f, weight);
            break;
	  case MAX:
	    iter->dbl[0] = MAX (iter->dbl[0], v->f);
	    iter->int1 = 1;
	    break;
	  case MAX | FSTRING:
	    if (memcmp (iter->string, v->s, src_width) < 0)
	      memcpy (iter->string, v->s, src_width);
	    iter->int1 = 1;
	    break;
	  case MIN:
	    iter->dbl[0] = MIN (iter->dbl[0], v->f);
	    iter->int1 = 1;
	    break;
	  case MIN | FSTRING:
	    if (memcmp (iter->string, v->s, src_width) > 0)
	      memcpy (iter->string, v->s, src_width);
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
            if (memcmp (iter->arg[0].c, v->s, src_width) < 0)
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
            if (memcmp (iter->arg[0].c, v->s, src_width) > 0)
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
            if (memcmp (iter->arg[0].c, v->s, src_width) <= 0
                && memcmp (iter->arg[1].c, v->s, src_width) >= 0)
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
            if (memcmp (iter->arg[0].c, v->s, src_width) > 0
                || memcmp (iter->arg[1].c, v->s, src_width) < 0)
              iter->dbl[0] += weight;
            iter->dbl[1] += weight;
            break;
	  case N:
	  case N | FSTRING:
	    iter->dbl[0] += weight;
	    break;
	  case NU:
	  case NU | FSTRING:
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
		memcpy (iter->string, v->s, src_width);
		iter->int1 = 1;
	      }
	    break;
	  case LAST:
	    iter->dbl[0] = v->f;
	    iter->int1 = 1;
	    break;
	  case LAST | FSTRING:
	    memcpy (iter->string, v->s, src_width);
	    iter->int1 = 1;
	    break;
          case NMISS:
          case NMISS | FSTRING:
          case NUMISS:
          case NUMISS | FSTRING:
            /* Our value is not missing or it would have been
               caught earlier.  Nothing to do. */
            break;
	  default:
	    NOT_REACHED ();
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
	  NOT_REACHED ();
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
        struct variable *v = agr->break_vars[i];
        size_t value_cnt = var_get_value_cnt (v);
        memcpy (case_data_rw_idx (output, value_idx),
                case_data (&agr->break_case, v),
                sizeof (union value) * value_cnt);
        value_idx += value_cnt; 
      }
  }
  
  {
    struct agr_var *i;
  
    for (i = agr->agr_vars; i; i = i->next)
      {
	union value *v = case_data_rw (output, i->dest);

	if (agr->missing == COLUMNWISE && i->missing != 0
	    && (i->function & FUNC) != N && (i->function & FUNC) != NU
	    && (i->function & FUNC) != NMISS && (i->function & FUNC) != NUMISS)
	  {
	    if (var_is_alpha (i->dest))
	      memset (v->s, ' ', var_get_width (i->dest));
	    else
	      v->f = SYSMIS;
	    continue;
	  }
	
	switch (i->function)
	  {
	  case SUM:
	    v->f = i->int1 ? i->dbl[0] : SYSMIS;
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
	      memcpy (v->s, i->string, var_get_width (i->dest));
	    else
	      memset (v->s, ' ', var_get_width (i->dest));
	    break;
	  case FGT:
	  case FGT | FSTRING:
	  case FLT:
	  case FLT | FSTRING:
	  case FIN:
	  case FIN | FSTRING:
	  case FOUT:
	  case FOUT | FSTRING:
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
	  case N | FSTRING:
	    v->f = i->dbl[0];
            break;
	  case NU:
	  case NU | FSTRING:
	    v->f = i->int1;
	    break;
	  case FIRST:
	  case LAST:
	    v->f = i->int1 ? i->dbl[0] : SYSMIS;
	    break;
	  case FIRST | FSTRING:
	  case LAST | FSTRING:
	    if (i->int1)
	      memcpy (v->s, i->string, var_get_width (i->dest));
	    else
	      memset (v->s, ' ', var_get_width (i->dest));
	    break;
	  case N_NO_VARS:
	    v->f = i->dbl[0];
	    break;
	  case NU_NO_VARS:
	    v->f = i->int1;
	    break;
	  case NMISS:
	  case NMISS | FSTRING:
	    v->f = i->dbl[0];
	    break;
	  case NUMISS:
	  case NUMISS | FSTRING:
	    v->f = i->int1;
	    break;
	  default:
	    NOT_REACHED ();
	  }
      }
  }
}

/* Resets the state for all the aggregate functions. */
static void
initialize_aggregate_info (struct agr_proc *agr, const struct ccase *input)
{
  struct agr_var *iter;

  case_destroy (&agr->break_case);
  case_clone (&agr->break_case, input);

  for (iter = agr->agr_vars; iter; iter = iter->next)
    {
      iter->missing = 0;
      iter->dbl[0] = iter->dbl[1] = iter->dbl[2] = 0.0;
      iter->int1 = iter->int2 = 0;
      switch (iter->function)
	{
	case MIN:
	  iter->dbl[0] = DBL_MAX;
	  break;
	case MIN | FSTRING:
	  memset (iter->string, 255, var_get_width (iter->src));
	  break;
	case MAX:
	  iter->dbl[0] = -DBL_MAX;
	  break;
	case MAX | FSTRING:
	  memset (iter->string, 0, var_get_width (iter->src));
	  break;
        case SD:
          if (iter->moments == NULL)
            iter->moments = moments1_create (MOMENT_VARIANCE);
          else
            moments1_clear (iter->moments);
          break;
        default:
          break;
	}
    }
}

/* Aggregate each case as it comes through.  Cases which aren't needed
   are dropped.
   Returns true if successful, false if an I/O error occurred. */
static bool
agr_to_active_file (const struct ccase *c, void *agr_, const struct dataset *ds UNUSED)
{
  struct agr_proc *agr = agr_;

  if (aggregate_single_case (agr, c, &agr->agr_case)) 
    return agr->sink->class->write (agr->sink, &agr->agr_case);

  return true;
}

/* Aggregate the current case and output it if we passed a
   breakpoint. */
static bool
presorted_agr_to_sysfile (const struct ccase *c, void *agr_, 
			  const struct dataset *ds UNUSED) 
{
  struct agr_proc *agr = agr_;

  if (aggregate_single_case (agr, c, &agr->agr_case)) 
    return any_writer_write (agr->writer, &agr->agr_case);
  
  return true;
}
