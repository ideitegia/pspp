/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2008, 2009, 2010, 2011, 2012, 2014 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include "language/stats/aggregate.h"

#include <stdlib.h>

#include "data/any-writer.h"
#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/format.h"
#include "data/settings.h"
#include "data/subcase.h"
#include "data/sys-file-writer.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/data-io/file-handle.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "language/stats/sort-criteria.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "math/moments.h"
#include "math/percentiles.h"
#include "math/sort.h"
#include "math/statistic.h"

#include "gl/c-strcase.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

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
    const struct variable *src;	/* Source variable. */
    struct variable *dest;	/* Target variable. */
    int function;		/* Function. */
    enum mv_class exclude;      /* Classes of missing values to exclude. */
    union agr_argument arg[2];	/* Arguments. */

    /* Accumulated during AGGREGATE execution. */
    double dbl[3];
    int int1, int2;
    char *string;
    bool saw_missing;
    struct moments1 *moments;
    double cc;

    struct variable *subject;
    struct variable *weight;
    struct casewriter *writer;
  };


/* Attributes of aggregation functions. */
const struct agr_func agr_func_tab[] =
  {
    {"SUM",     N_("Sum of values"),                         AGR_SV_YES, 0, -1,          {FMT_F, 8, 2}},
    {"MEAN",	N_("Mean average"),                          AGR_SV_YES, 0, -1,          {FMT_F, 8, 2}},
    {"MEDIAN",	N_("Median average"),                        AGR_SV_YES, 0, -1,          {FMT_F, 8, 2}},
    {"SD",      N_("Standard deviation"),                    AGR_SV_YES, 0, -1,          {FMT_F, 8, 2}},
    {"MAX",     N_("Maximum value"),                         AGR_SV_YES, 0, VAL_STRING,  {-1, -1, -1}},
    {"MIN",     N_("Minimum value"),                         AGR_SV_YES, 0, VAL_STRING,  {-1, -1, -1}},
    {"PGT",     N_("Percentage greater than"),               AGR_SV_YES, 1, VAL_NUMERIC, {FMT_F, 5, 1}},
    {"PLT",     N_("Percentage less than"),                  AGR_SV_YES, 1, VAL_NUMERIC, {FMT_F, 5, 1}},
    {"PIN",     N_("Percentage included in range"),          AGR_SV_YES, 2, VAL_NUMERIC, {FMT_F, 5, 1}},
    {"POUT",    N_("Percentage excluded from range"),        AGR_SV_YES, 2, VAL_NUMERIC, {FMT_F, 5, 1}},
    {"FGT",     N_("Fraction greater than"),                 AGR_SV_YES, 1, VAL_NUMERIC, {FMT_F, 5, 3}},
    {"FLT",     N_("Fraction less than"),                    AGR_SV_YES, 1, VAL_NUMERIC, {FMT_F, 5, 3}},
    {"FIN",     N_("Fraction included in range"),            AGR_SV_YES, 2, VAL_NUMERIC, {FMT_F, 5, 3}},
    {"FOUT",    N_("Fraction excluded from range"),          AGR_SV_YES, 2, VAL_NUMERIC, {FMT_F, 5, 3}},
    {"N",       N_("Number of cases"),                       AGR_SV_NO,  0, VAL_NUMERIC, {FMT_F, 7, 0}},
    {"NU",      N_("Number of cases (unweighted)"),          AGR_SV_OPT, 0, VAL_NUMERIC, {FMT_F, 7, 0}},
    {"NMISS",   N_("Number of missing values"),              AGR_SV_YES, 0, VAL_NUMERIC, {FMT_F, 7, 0}},
    {"NUMISS",  N_("Number of missing values (unweighted)"), AGR_SV_YES, 0, VAL_NUMERIC, {FMT_F, 7, 0}},
    {"FIRST",   N_("First non-missing value"),               AGR_SV_YES, 0, VAL_STRING,  {-1, -1, -1}},
    {"LAST",    N_("Last non-missing value"),                AGR_SV_YES, 0, VAL_STRING,  {-1, -1, -1}},
    {NULL,      NULL,                                        AGR_SV_NO,  0, -1,          {-1, -1, -1}},
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
    /* Break variables. */
    struct subcase sort;                /* Sort criteria (break variables). */
    const struct variable **break_vars;       /* Break variables. */
    size_t break_var_cnt;               /* Number of break variables. */

    enum missing_treatment missing;     /* How to treat missing values. */
    struct agr_var *agr_vars;           /* First aggregate variable. */
    struct dictionary *dict;            /* Aggregate dictionary. */
    const struct dictionary *src_dict;  /* Dict of the source */
    int case_cnt;                       /* Counts aggregated cases. */

    bool add_variables;                 /* True iff the aggregated variables should
					   be appended to the existing dictionary */
  };

static void initialize_aggregate_info (struct agr_proc *);

static void accumulate_aggregate_info (struct agr_proc *,
                                       const struct ccase *);
/* Prototypes. */
static bool parse_aggregate_functions (struct lexer *, const struct dictionary *,
				       struct agr_proc *);
static void agr_destroy (struct agr_proc *);
static void dump_aggregate_info (const struct agr_proc *agr,
                                 struct casewriter *output,
				 const struct ccase *break_case);

/* Parsing. */

/* Parses and executes the AGGREGATE procedure. */
int
cmd_aggregate (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct agr_proc agr;
  struct file_handle *out_file = NULL;
  struct casereader *input = NULL, *group;
  struct casegrouper *grouper;
  struct casewriter *output = NULL;

  bool copy_documents = false;
  bool presorted = false;
  bool saw_direction;
  bool ok;

  memset(&agr, 0 , sizeof (agr));
  agr.missing = ITEMWISE;
  agr.src_dict = dict;
  subcase_init_empty (&agr.sort);

  /* OUTFILE subcommand must be first. */
  lex_match (lexer, T_SLASH);
  if (!lex_force_match_id (lexer, "OUTFILE"))
    goto error;
  lex_match (lexer, T_EQUALS);
  if (!lex_match (lexer, T_ASTERISK))
    {
      out_file = fh_parse (lexer, FH_REF_FILE, dataset_session (ds));
      if (out_file == NULL)
        goto error;
    }

  if (out_file == NULL && lex_match_id (lexer, "MODE"))
    {
      lex_match (lexer, T_EQUALS);
      if (lex_match_id (lexer, "ADDVARIABLES"))
	{
	  agr.add_variables = true;

	  /* presorted is assumed in ADDVARIABLES mode */
	  presorted = true;
	}
      else if (lex_match_id (lexer, "REPLACE"))
	{
	  agr.add_variables = false;
	}
      else
	goto error;
    }

  if ( agr.add_variables )
    agr.dict = dict_clone (dict);
  else
    agr.dict = dict_create (dict_get_encoding (dict));

  dict_set_label (agr.dict, dict_get_label (dict));
  dict_set_documents (agr.dict, dict_get_documents (dict));

  /* Read most of the subcommands. */
  for (;;)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "MISSING"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (!lex_match_id (lexer, "COLUMNWISE"))
	    {
	      lex_error_expecting (lexer, "COLUMNWISE", NULL);
              goto error;
	    }
	  agr.missing = COLUMNWISE;
	}
      else if (lex_match_id (lexer, "DOCUMENT"))
        copy_documents = true;
      else if (lex_match_id (lexer, "PRESORTED"))
        presorted = true;
      else if (lex_force_match_id (lexer, "BREAK"))
	{
          int i;

	  lex_match (lexer, T_EQUALS);
          if (!parse_sort_criteria (lexer, dict, &agr.sort, &agr.break_vars,
                                    &saw_direction))
            goto error;
          agr.break_var_cnt = subcase_get_n_fields (&agr.sort);

	  if  (! agr.add_variables)
	    for (i = 0; i < agr.break_var_cnt; i++)
	      dict_clone_var_assert (agr.dict, agr.break_vars[i]);

          /* BREAK must follow the options. */
          break;
	}
      else
        goto error;

    }
  if (presorted && saw_direction)
    msg (SW, _("When PRESORTED is specified, specifying sorting directions "
               "with (A) or (D) has no effect.  Output data will be sorted "
               "the same way as the input data."));

  /* Read in the aggregate functions. */
  lex_match (lexer, T_SLASH);
  if (!parse_aggregate_functions (lexer, dict, &agr))
    goto error;

  /* Delete documents. */
  if (!copy_documents)
    dict_clear_documents (agr.dict);

  /* Cancel SPLIT FILE. */
  dict_set_split_vars (agr.dict, NULL, 0);

  /* Initialize. */
  agr.case_cnt = 0;

  if (out_file == NULL)
    {
      /* The active dataset will be replaced by the aggregated data,
         so TEMPORARY is moot. */
      proc_cancel_temporary_transformations (ds);
      proc_discard_output (ds);
      output = autopaging_writer_create (dict_get_proto (agr.dict));
    }
  else
    {
      output = any_writer_open (out_file, agr.dict);
      if (output == NULL)
        goto error;
    }

  input = proc_open (ds);
  if (!subcase_is_empty (&agr.sort) && !presorted)
    {
      input = sort_execute (input, &agr.sort);
      subcase_clear (&agr.sort);
    }

  for (grouper = casegrouper_create_vars (input, agr.break_vars,
                                          agr.break_var_cnt);
       casegrouper_get_next_group (grouper, &group);
       casereader_destroy (group))
    {
      struct casereader *placeholder = NULL;
      struct ccase *c = casereader_peek (group, 0);

      if (c == NULL)
        {
          casereader_destroy (group);
          continue;
        }

      initialize_aggregate_info (&agr);

      if ( agr.add_variables )
	placeholder = casereader_clone (group);

      {
	struct ccase *cg;
	for (; (cg = casereader_read (group)) != NULL; case_unref (cg))
	  accumulate_aggregate_info (&agr, cg);
      }


      if  (agr.add_variables)
	{
	  struct ccase *cg;
	  for (; (cg = casereader_read (placeholder)) != NULL; case_unref (cg))
	    dump_aggregate_info (&agr, output, cg);

	  casereader_destroy (placeholder);
	}
      else
	{
	  dump_aggregate_info (&agr, output, c);
	}
      case_unref (c);
    }
  if (!casegrouper_destroy (grouper))
    goto error;

  if (!proc_commit (ds))
    {
      input = NULL;
      goto error;
    }
  input = NULL;

  if (out_file == NULL)
    {
      struct casereader *next_input = casewriter_make_reader (output);
      if (next_input == NULL)
        goto error;

      dataset_set_dict (ds, agr.dict);
      dataset_set_source (ds, next_input);
      agr.dict = NULL;
    }
  else
    {
      ok = casewriter_destroy (output);
      output = NULL;
      if (!ok)
        goto error;
    }

  agr_destroy (&agr);
  fh_unref (out_file);
  return CMD_SUCCESS;

error:
  if (input != NULL)
    proc_commit (ds);
  casewriter_destroy (output);
  agr_destroy (&agr);
  fh_unref (out_file);
  return CMD_CASCADING_FAILURE;
}

/* Parse all the aggregate functions. */
static bool
parse_aggregate_functions (struct lexer *lexer, const struct dictionary *dict,
			   struct agr_proc *agr)
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

      enum mv_class exclude;
      const struct agr_func *function;
      int func_index;

      union agr_argument arg[2];

      const struct variable **src;
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
      ds_init_empty (&function_name);

      /* Parse the list of target variables. */
      while (!lex_match (lexer, T_EQUALS))
	{
	  size_t n_dest_prev = n_dest;

	  if (!parse_DATA_LIST_vars (lexer, dict, &dest, &n_dest,
                                     (PV_APPEND | PV_SINGLE | PV_NO_SCRATCH
                                      | PV_NO_DUPLICATE)))
	    goto error;

	  /* Assign empty labels. */
	  {
	    int j;

	    dest_label = xnrealloc (dest_label, n_dest, sizeof *dest_label);
	    for (j = n_dest_prev; j < n_dest; j++)
	      dest_label[j] = NULL;
	  }



	  if (lex_is_string (lexer))
	    {
	      dest_label[n_dest - 1] = xstrdup (lex_tokcstr (lexer));
	      lex_get (lexer);
	    }
	}

      /* Get the name of the aggregation function. */
      if (lex_token (lexer) != T_ID)
	{
	  lex_error (lexer, _("expecting aggregation function"));
	  goto error;
	}

      ds_assign_substring (&function_name, lex_tokss (lexer));
      exclude = ds_chomp_byte (&function_name, '.') ? MV_SYSTEM : MV_ANY;

      for (function = agr_func_tab; function->name; function++)
	if (!c_strcasecmp (function->name, ds_cstr (&function_name)))
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
      if (!lex_match (lexer, T_LPAREN))
	{
	  if (function->src_vars == AGR_SV_YES)
	    {
              lex_force_match (lexer, T_LPAREN);
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

	    if (!parse_variables_const (lexer, dict, &src, &n_src, pv_opts))
	      goto error;
	  }

	  /* Parse function arguments, for those functions that
	     require arguments. */
	  if (function->n_args != 0)
	    for (i = 0; i < function->n_args; i++)
	      {
		int type;

		lex_match (lexer, T_COMMA);
		if (lex_is_string (lexer))
		  {
		    arg[i].c = recode_string (dict_get_encoding (agr->dict),
                                              "UTF-8", lex_tokcstr (lexer),
                                              -1);
		    type = VAL_STRING;
		  }
		else if (lex_is_number (lexer))
		  {
		    arg[i].f = lex_tokval (lexer);
		    type = VAL_NUMERIC;
		  }
                else
                  {
		    msg (SE, _("Missing argument %zu to %s."),
                         i + 1, function->name);
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
	  if (!lex_force_match (lexer, T_RPAREN))
            goto error;

	  /* Now check that the number of source variables match
	     the number of target variables.  If we check earlier
	     than this, the user can get very misleading error
	     message, i.e. `AGGREGATE x=SUM(y t).' will get this
	     error message when a proper message would be more
	     like `unknown variable t'. */
	  if (n_src != n_dest)
	    {
	      msg (SE, _("Number of source variables (%zu) does not match "
			 "number of target variables (%zu)."),
		    n_src, n_dest);
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
	  struct agr_var *v = xzalloc (sizeof *v);

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

		if (function->alpha_type == VAL_STRING)
		  destvar = dict_clone_var_as (agr->dict, v->src, dest[i]);
		else
                  {
                    assert (var_is_numeric (v->src)
                            || function->alpha_type == VAL_NUMERIC);
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
		if (destvar != NULL)
		  {
		    if ((func_index == N || func_index == NMISS)
			&& dict_get_weight (dict) != NULL)
		      f = fmt_for_output (FMT_F, 8, 2);
		    else
		      f = function->format;
		    var_set_both_formats (destvar, &f);
		  }
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

	  v->exclude = exclude;

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

      if (!lex_match (lexer, T_SLASH))
	{
	  if (lex_token (lexer) == T_ENDCMD)
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

  subcase_destroy (&agr->sort);
  free (agr->break_vars);
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

      dict_destroy_internal_var (iter->subject);
      dict_destroy_internal_var (iter->weight);

      free (iter);
    }
  if (agr->dict != NULL)
    dict_destroy (agr->dict);
}

/* Execution. */

/* Accumulates aggregation data from the case INPUT. */
static void
accumulate_aggregate_info (struct agr_proc *agr, const struct ccase *input)
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

        if (var_is_value_missing (iter->src, v, iter->exclude))
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
	    iter->saw_missing = true;
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
	  case MEDIAN:
	    {
	      double wv ;
	      struct ccase *cout;

              cout = case_create (casewriter_get_proto (iter->writer));

	      case_data_rw (cout, iter->subject)->f
                = case_data (input, iter->src)->f;

	      wv = dict_get_case_weight (agr->src_dict, input, NULL);

	      case_data_rw (cout, iter->weight)->f = wv;

	      iter->cc += wv;

	      casewriter_write (iter->writer, cout);
	    }
	    break;
	  case SD:
            moments1_add (iter->moments, v->f, weight);
            break;
	  case MAX:
	    iter->dbl[0] = MAX (iter->dbl[0], v->f);
	    iter->int1 = 1;
	    break;
	  case MAX | FSTRING:
            /* Need to do some kind of Unicode collation thingy here */
	    if (memcmp (iter->string, value_str (v, src_width), src_width) < 0)
	      memcpy (iter->string, value_str (v, src_width), src_width);
	    iter->int1 = 1;
	    break;
	  case MIN:
	    iter->dbl[0] = MIN (iter->dbl[0], v->f);
	    iter->int1 = 1;
	    break;
	  case MIN | FSTRING:
	    if (memcmp (iter->string, value_str (v, src_width), src_width) > 0)
	      memcpy (iter->string, value_str (v, src_width), src_width);
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
            if (memcmp (iter->arg[0].c,
                        value_str (v, src_width), src_width) < 0)
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
            if (memcmp (iter->arg[0].c,
                        value_str (v, src_width), src_width) > 0)
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
            if (memcmp (iter->arg[0].c,
                        value_str (v, src_width), src_width) <= 0
                && memcmp (iter->arg[1].c,
                           value_str (v, src_width), src_width) >= 0)
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
            if (memcmp (iter->arg[0].c,
                        value_str (v, src_width), src_width) > 0
                || memcmp (iter->arg[1].c,
                           value_str (v, src_width), src_width) < 0)
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
		memcpy (iter->string, value_str (v, src_width), src_width);
		iter->int1 = 1;
	      }
	    break;
	  case LAST:
	    iter->dbl[0] = v->f;
	    iter->int1 = 1;
	    break;
	  case LAST | FSTRING:
	    memcpy (iter->string, value_str (v, src_width), src_width);
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
	case N:
	  iter->dbl[0] += weight;
	  break;
	case NU:
	  iter->int1++;
	  break;
	default:
	  NOT_REACHED ();
	}
    }
}

/* Writes an aggregated record to OUTPUT. */
static void
dump_aggregate_info (const struct agr_proc *agr, struct casewriter *output, const struct ccase *break_case)
{
  struct ccase *c = case_create (dict_get_proto (agr->dict));

  if ( agr->add_variables)
    {
      case_copy (c, 0, break_case, 0, dict_get_var_cnt (agr->src_dict));
    }
  else
    {
      int value_idx = 0;
      int i;

      for (i = 0; i < agr->break_var_cnt; i++)
	{
	  const struct variable *v = agr->break_vars[i];
	  value_copy (case_data_rw_idx (c, value_idx),
		      case_data (break_case, v),
		      var_get_width (v));
	  value_idx++;
	}
    }

  {
    struct agr_var *i;

    for (i = agr->agr_vars; i; i = i->next)
      {
	union value *v = case_data_rw (c, i->dest);
        int width = var_get_width (i->dest);

	if (agr->missing == COLUMNWISE && i->saw_missing
	    && (i->function & FUNC) != N && (i->function & FUNC) != NU
	    && (i->function & FUNC) != NMISS && (i->function & FUNC) != NUMISS)
	  {
            value_set_missing (v, width);
	    casewriter_destroy (i->writer);
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
	  case MEDIAN:
	    {
	      if ( i->writer)
		{
		  struct percentile *median = percentile_create (0.5, i->cc);
		  struct order_stats *os = &median->parent;
		  struct casereader *sorted_reader = casewriter_make_reader (i->writer);
		  i->writer = NULL;

		  order_stats_accumulate (&os, 1,
					  sorted_reader,
					  i->weight,
					  i->subject,
					  i->exclude);
		  i->dbl[0] = percentile_calculate (median, PC_HAVERAGE);
		  statistic_destroy (&median->parent.parent);
		}
	      v->f = i->dbl[0];
	    }
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
	      memcpy (value_str_rw (v, width), i->string, width);
	    else
              value_set_missing (v, width);
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
	      memcpy (value_str_rw (v, width), i->string, width);
	    else
              value_set_missing (v, width);
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

  casewriter_write (output, c);
}

/* Resets the state for all the aggregate functions. */
static void
initialize_aggregate_info (struct agr_proc *agr)
{
  struct agr_var *iter;

  for (iter = agr->agr_vars; iter; iter = iter->next)
    {
      iter->saw_missing = false;
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
	case MEDIAN:
	  {
            struct caseproto *proto;
            struct subcase ordering;

            proto = caseproto_create ();
            proto = caseproto_add_width (proto, 0);
            proto = caseproto_add_width (proto, 0);

	    if ( ! iter->subject)
	      iter->subject = dict_create_internal_var (0, 0);

	    if ( ! iter->weight)
	      iter->weight = dict_create_internal_var (1, 0);

            subcase_init_var (&ordering, iter->subject, SC_ASCEND);
	    iter->writer = sort_create_writer (&ordering, proto);
            subcase_destroy (&ordering);
            caseproto_unref (proto);

	    iter->cc = 0;
	  }
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
