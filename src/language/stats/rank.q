/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2006, 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include <gsl/gsl_cdf.h>
#include <limits.h>
#include <math.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "data/short-names.h"
#include "data/subcase.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/stats/sort-criteria.h"
#include "libpspp/compiler.h"
#include "libpspp/taint.h"
#include "math/sort.h"
#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* (headers) */

/* (specification)
   "RANK" (rank_):
   *^variables=custom;
   +rank=custom;
   +normal=custom;
   +percent=custom;
   +ntiles=custom;
   +rfraction=custom;
   +proportion=custom;
   +n=custom;
   +savage=custom;
   +print=print:!yes/no;
   +fraction=fraction:!blom/tukey/vw/rankit;
   +ties=ties:!mean/low/high/condense;
   missing=miss:!exclude/include.
*/
/* (declarations) */
/* (functions) */

typedef double (*rank_function_t) (double c, double cc, double cc_1,
				 int i, double w);

static double rank_proportion (double c, double cc, double cc_1,
			       int i, double w);

static double rank_normal (double c, double cc, double cc_1,
			   int i, double w);

static double rank_percent (double c, double cc, double cc_1,
			    int i, double w);

static double rank_rfraction (double c, double cc, double cc_1,
			      int i, double w);

static double rank_rank (double c, double cc, double cc_1,
			 int i, double w);

static double rank_n (double c, double cc, double cc_1,
		      int i, double w);

static double rank_savage (double c, double cc, double cc_1,
		      int i, double w);

static double rank_ntiles (double c, double cc, double cc_1,
		      int i, double w);


enum RANK_FUNC
  {
    RANK,
    NORMAL,
    PERCENT,
    RFRACTION,
    PROPORTION,
    N,
    NTILES,
    SAVAGE,
    n_RANK_FUNCS
  };

static const struct fmt_spec dest_format[n_RANK_FUNCS] = {
  {FMT_F, 9, 3}, /* rank */
  {FMT_F, 6, 4}, /* normal */
  {FMT_F, 6, 2}, /* percent */
  {FMT_F, 6, 4}, /* rfraction */
  {FMT_F, 6, 4}, /* proportion */
  {FMT_F, 6, 0}, /* n */
  {FMT_F, 3, 0}, /* ntiles */
  {FMT_F, 8, 4}  /* savage */
};

static const char * const function_name[n_RANK_FUNCS] = {
  "RANK",
  "NORMAL",
  "PERCENT",
  "RFRACTION",
  "PROPORTION",
  "N",
  "NTILES",
  "SAVAGE"
};

static const rank_function_t rank_func[n_RANK_FUNCS] = {
  rank_rank,
  rank_normal,
  rank_percent,
  rank_rfraction,
  rank_proportion,
  rank_n,
  rank_ntiles,
  rank_savage
  };


struct rank_spec
{
  enum RANK_FUNC rfunc;
  struct variable **destvars;
};


/* Categories of missing values to exclude. */
static enum mv_class exclude_values;

static struct rank_spec *rank_specs;
static size_t n_rank_specs;

static struct subcase sc;

static const struct variable **group_vars;
static size_t n_group_vars;

static const struct variable **src_vars;
static size_t n_src_vars;


static int k_ntiles;

static struct cmd_rank cmd;

static void rank_sorted_file (struct casereader *,
                              struct casewriter *,
                              const struct dictionary *,
                              const struct rank_spec *rs,
                              int n_rank_specs,
                              int idx,
                              const struct variable *rank_var);

static const char *
fraction_name(void)
{
  static char name[10];
  switch ( cmd.fraction )
    {
    case RANK_BLOM:
      strcpy (name, "BLOM");
      break;
    case RANK_RANKIT:
      strcpy (name, "RANKIT");
      break;
    case RANK_TUKEY:
      strcpy (name, "TUKEY");
      break;
    case RANK_VW:
      strcpy (name, "VW");
      break;
    default:
      NOT_REACHED ();
    }
  return name;
}

/* Create a label on DEST_VAR, describing its derivation from SRC_VAR and F */
static void
create_var_label (struct variable *dest_var,
		  const struct variable *src_var, enum RANK_FUNC f,
                  const char *dict_encoding)
{
  struct string label;
  ds_init_empty (&label);

  if ( n_group_vars > 0 )
    {
      struct string group_var_str;
      int g;

      ds_init_empty (&group_var_str);

      for (g = 0 ; g < n_group_vars ; ++g )
	{
	  if ( g > 0 ) ds_put_cstr (&group_var_str, " ");
	  ds_put_cstr (&group_var_str, var_get_name (group_vars[g]));
	}

      ds_put_format (&label, _("%s of %s by %s"), function_name[f],
		     var_get_name (src_var), ds_cstr (&group_var_str));
      ds_destroy (&group_var_str);
    }
  else
    ds_put_format (&label, _("%s of %s"),
                   function_name[f], var_get_name (src_var));

  var_set_label (dest_var, ds_cstr (&label), dict_encoding, false);

  ds_destroy (&label);
}


static bool
rank_cmd (struct dataset *ds, const struct subcase *sc,
	  const struct rank_spec *rank_specs, int n_rank_specs)
{
  struct dictionary *d = dataset_dict (ds);
  bool ok = true;
  int i;

  for (i = 0 ; i < subcase_get_n_fields (sc) ; ++i )
    {
      /* Rank variable at index I in SC. */
      struct casegrouper *split_grouper;
      struct casereader *split_group;
      struct casewriter *output;

      proc_discard_output (ds);
      split_grouper = casegrouper_create_splits (proc_open (ds), d);
      output = autopaging_writer_create (dict_get_proto (d));

      while (casegrouper_get_next_group (split_grouper, &split_group))
        {
          struct subcase ordering;
          struct casereader *ordered;
          struct casegrouper *by_grouper;
          struct casereader *by_group;

          /* Sort this split group by the BY variables as primary
             keys and the rank variable as secondary key. */
          subcase_init_vars (&ordering, group_vars, n_group_vars);
          subcase_add_var (&ordering, src_vars[i],
                           subcase_get_direction (sc, i));
          ordered = sort_execute (split_group, &ordering);
          subcase_destroy (&ordering);

          /* Rank the rank variable within this split group. */
          by_grouper = casegrouper_create_vars (ordered,
                                                group_vars, n_group_vars);
          while (casegrouper_get_next_group (by_grouper, &by_group))
            {
              /* Rank the rank variable within this BY group
                 within the split group. */

              rank_sorted_file (by_group, output, d, rank_specs, n_rank_specs,
                                i, src_vars[i]);
            }
          ok = casegrouper_destroy (by_grouper) && ok;
        }
      ok = casegrouper_destroy (split_grouper);
      ok = proc_commit (ds) && ok;
      ok = (proc_set_active_file_data (ds, casewriter_make_reader (output))
            && ok);
      if (!ok)
        break;
    }

  return ok;
}

/* Hardly a rank function !! */
static double
rank_n (double c UNUSED, double cc UNUSED, double cc_1 UNUSED,
	  int i UNUSED, double w)
{
  return w;
}


static double
rank_rank (double c, double cc, double cc_1,
	  int i, double w UNUSED)
{
  double rank;

  if ( c >= 1.0 )
    {
      switch (cmd.ties)
	{
	case RANK_LOW:
	  rank = cc_1 + 1;
	  break;
	case RANK_HIGH:
	  rank = cc;
	  break;
	case RANK_MEAN:
	  rank = cc_1 + (c + 1.0)/ 2.0;
	  break;
	case RANK_CONDENSE:
	  rank = i;
	  break;
	default:
	  NOT_REACHED ();
	}
    }
  else
    {
      switch (cmd.ties)
	{
	case RANK_LOW:
	  rank = cc_1;
	  break;
	case RANK_HIGH:
	  rank = cc;
	  break;
	case RANK_MEAN:
	  rank = cc_1 + c / 2.0 ;
	  break;
	case RANK_CONDENSE:
	  rank = i;
	  break;
	default:
	  NOT_REACHED ();
	}
    }

  return rank;
}


static double
rank_rfraction (double c, double cc, double cc_1,
		int i, double w)
{
  return rank_rank (c, cc, cc_1, i, w) / w ;
}


static double
rank_percent (double c, double cc, double cc_1,
		int i, double w)
{
  return rank_rank (c, cc, cc_1, i, w) * 100.0 / w ;
}


static double
rank_proportion (double c, double cc, double cc_1,
		 int i, double w)
{
  const double r =  rank_rank (c, cc, cc_1, i, w) ;

  double f;

  switch ( cmd.fraction )
    {
    case RANK_BLOM:
      f =  (r - 3.0/8.0) / (w + 0.25);
      break;
    case RANK_RANKIT:
      f = (r - 0.5) / w ;
      break;
    case RANK_TUKEY:
      f = (r - 1.0/3.0) / (w + 1.0/3.0);
      break;
    case RANK_VW:
      f = r / ( w + 1.0);
      break;
    default:
      NOT_REACHED ();
    }


  return (f > 0) ? f : SYSMIS;
}

static double
rank_normal (double c, double cc, double cc_1,
	     int i, double w)
{
  double f = rank_proportion (c, cc, cc_1, i, w);

  return gsl_cdf_ugaussian_Pinv (f);
}

static double
rank_ntiles (double c, double cc, double cc_1,
		int i, double w)
{
  double r = rank_rank (c, cc, cc_1, i, w);


  return ( floor (( r * k_ntiles) / ( w + 1) ) + 1);
}

/* Expected value of the order statistics from an exponential distribution */
static double
ee (int j, double w_star)
{
  int k;
  double sum = 0.0;

  for (k = 1 ; k <= j; k++)
    sum += 1.0 / ( w_star + 1 - k );

  return sum;
}


static double
rank_savage (double c, double cc, double cc_1,
		int i UNUSED, double w)
{
  double int_part;
  const int i_1 = floor (cc_1);
  const int i_2 = floor (cc);

  const double w_star = (modf (w, &int_part) == 0 ) ? w : floor (w) + 1;

  const double g_1 = cc_1 - i_1;
  const double g_2 = cc - i_2;

  /* The second factor is infinite, when the first is zero.
     Therefore, evaluate the second, only when the first is non-zero */
  const double expr1 =  (1 - g_1) ? (1 - g_1) * ee(i_1+1, w_star) : ( 1 - g_1);
  const double expr2 =  g_2 ? g_2 * ee (i_2+1, w_star) : g_2 ;

  if ( i_1 == i_2 )
    return ee (i_1 + 1, w_star) - 1;

  if ( i_1 + 1 == i_2 )
    return ( ( expr1 + expr2 )/c ) - 1;

  if ( i_1 + 2 <= i_2 )
    {
      int j;
      double sigma = 0.0;
      for (j = i_1 + 2 ; j <= i_2; ++j )
	sigma += ee (j, w_star);
      return ( (expr1 + expr2 + sigma) / c) -1;
    }

  NOT_REACHED();
}

static void
rank_sorted_file (struct casereader *input,
                  struct casewriter *output,
                  const struct dictionary *dict,
                  const struct rank_spec *rs,
                  int n_rank_specs,
                  int dest_idx,
                  const struct variable *rank_var)
{
  struct casereader *pass1, *pass2, *pass2_1;
  struct casegrouper *tie_grouper;
  struct ccase *c;
  double w = 0.0;
  double cc = 0.0;
  int tie_group = 1;


  input = casereader_create_filter_missing (input, &rank_var, 1,
                                            exclude_values, NULL, output);
  input = casereader_create_filter_weight (input, dict, NULL, output);

  casereader_split (input, &pass1, &pass2);

  /* Pass 1: Get total group weight. */
  for (; (c = casereader_read (pass1)) != NULL; case_unref (c))
    w += dict_get_case_weight (dict, c, NULL);
  casereader_destroy (pass1);

  /* Pass 2: Do ranking. */
  tie_grouper = casegrouper_create_vars (pass2, &rank_var, 1);
  while (casegrouper_get_next_group (tie_grouper, &pass2_1))
    {
      struct casereader *pass2_2;
      double cc_1 = cc;
      double tw = 0.0;
      int i;

      pass2_2 = casereader_clone (pass2_1);
      taint_propagate (casereader_get_taint (pass2_2),
                       casewriter_get_taint (output));

      /* Pass 2.1: Sum up weight for tied cases. */
      for (; (c = casereader_read (pass2_1)) != NULL; case_unref (c))
        tw += dict_get_case_weight (dict, c, NULL);
      cc += tw;
      casereader_destroy (pass2_1);

      /* Pass 2.2: Rank tied cases. */
      while ((c = casereader_read (pass2_2)) != NULL)
        {
          c = case_unshare (c);
          for (i = 0; i < n_rank_specs; ++i)
            {
              const struct variable *dst_var = rs[i].destvars[dest_idx];
              double *dst_value = &case_data_rw (c, dst_var)->f;
              *dst_value = rank_func[rs[i].rfunc] (tw, cc, cc_1, tie_group, w);
            }
          casewriter_write (output, c);
        }
      casereader_destroy (pass2_2);

      tie_group++;
    }
  casegrouper_destroy (tie_grouper);
}

/* Transformation function to enumerate all the cases */
static int
create_resort_key (void *key_var_, struct ccase **cc, casenumber case_num)
{
  struct variable *key_var = key_var_;

  *cc = case_unshare (*cc);
  case_data_rw (*cc, key_var)->f = case_num;

  return TRNS_CONTINUE;
}


/* Create and return a new variable in which to store the ranks of SRC_VAR
   accoring to the rank function F.
   VNAME is the name of the variable to be created.
   If VNAME is NULL, then a name will be automatically chosen.
 */
static struct variable *
create_rank_variable (struct dictionary *dict, enum RANK_FUNC f,
		      const struct variable *src_var,
		      const char *vname)
{
  int i;
  struct variable *var = NULL;
  char name[SHORT_NAME_LEN + 1];

  if ( vname )
    var = dict_create_var(dict, vname, 0);

  if ( NULL == var )
    {
      snprintf (name, SHORT_NAME_LEN + 1, "%c%s",
                function_name[f][0], var_get_name (src_var));

      var = dict_create_var(dict, name, 0);
    }

  i = 1;
  while( NULL == var )
    {
      char func_abb[4];
      snprintf(func_abb, 4, "%s", function_name[f]);
      snprintf(name, SHORT_NAME_LEN + 1, "%s%03d", func_abb,
	       i);

      var = dict_create_var(dict, name, 0);
      if (i++ >= 999)
	break;
    }

  i = 1;
  while ( NULL == var )
    {
      char func_abb[3];
      snprintf(func_abb, 3, "%s", function_name[f]);

      snprintf(name, SHORT_NAME_LEN + 1,
	       "RNK%s%02d", func_abb, i);

      var = dict_create_var(dict, name, 0);
      if ( i++ >= 99 )
	break;
    }

  if ( NULL == var )
    {
      msg(ME, _("Cannot create new rank variable.  All candidates in use."));
      return NULL;
    }

  var_set_both_formats (var, &dest_format[f]);

  return var;
}


static void
rank_cleanup(void)
{
  int i;

  free (group_vars);
  group_vars = NULL;
  n_group_vars = 0;

  for (i = 0 ; i <  n_rank_specs ; ++i )
      free (rank_specs[i].destvars);

  free (rank_specs);
  rank_specs = NULL;
  n_rank_specs = 0;

  subcase_destroy (&sc);

  free (src_vars);
  src_vars = NULL;
  n_src_vars = 0;
}

int
cmd_rank (struct lexer *lexer, struct dataset *ds)
{
  bool result;
  struct variable *order;
  size_t i;
  n_rank_specs = 0;

  subcase_init_empty (&sc);
  if ( !parse_rank (lexer, ds, &cmd, NULL) )
    {
      rank_cleanup ();
    return CMD_FAILURE;
    }

  /* If /MISSING=INCLUDE is set, then user missing values are ignored */
  exclude_values = cmd.miss == RANK_INCLUDE ? MV_SYSTEM : MV_ANY;

  /* Default to /RANK if no function subcommands are given */
  if ( !( cmd.sbc_normal  || cmd.sbc_ntiles || cmd.sbc_proportion ||
	  cmd.sbc_rfraction || cmd.sbc_savage || cmd.sbc_n ||
	  cmd.sbc_percent || cmd.sbc_rank ) )
    {
      assert ( n_rank_specs == 0 );

      rank_specs = xmalloc (sizeof (*rank_specs));
      rank_specs[0].rfunc = RANK;
      rank_specs[0].destvars =
	xcalloc (subcase_get_n_fields (&sc), sizeof (struct variable *));

      n_rank_specs = 1;
    }

  assert ( subcase_get_n_fields (&sc) == n_src_vars);

  /* Create variables for all rank destinations which haven't
     already been created with INTO.
     Add labels to all the destination variables.
  */
  for (i = 0 ; i <  n_rank_specs ; ++i )
    {
      int v;
      for ( v = 0 ; v < n_src_vars ;  v ++ )
	{
          struct dictionary *dict = dataset_dict (ds);

	  if ( rank_specs[i].destvars[v] == NULL )
	    {
	      rank_specs[i].destvars[v] =
		create_rank_variable (dict, rank_specs[i].rfunc, src_vars[v], NULL);
	    }

	  create_var_label ( rank_specs[i].destvars[v],
			     src_vars[v],
			     rank_specs[i].rfunc,
                             dict_get_encoding (dict));
	}
    }

  if ( cmd.print == RANK_YES )
    {
      int v;

      tab_output_text (0, _("Variables Created By RANK"));
      tab_output_text (0, "");

      for (i = 0 ; i <  n_rank_specs ; ++i )
	{
	  for ( v = 0 ; v < n_src_vars ;  v ++ )
	    {
	      if ( n_group_vars > 0 )
		{
		  struct string varlist;
		  int g;

		  ds_init_empty (&varlist);
		  for ( g = 0 ; g < n_group_vars ; ++g )
		    {
		      ds_put_cstr (&varlist, var_get_name (group_vars[g]));

		      if ( g < n_group_vars - 1)
			ds_put_cstr (&varlist, " ");
		    }

		  if ( rank_specs[i].rfunc == NORMAL ||
		       rank_specs[i].rfunc == PROPORTION )
		    tab_output_text_format (0,
                                            _("%s into %s(%s of %s using %s BY %s)"),
                                            var_get_name (src_vars[v]),
                                            var_get_name (rank_specs[i].destvars[v]),
                                            function_name[rank_specs[i].rfunc],
                                            var_get_name (src_vars[v]),
                                            fraction_name(),
                                            ds_cstr (&varlist));

		  else
		    tab_output_text_format (0,
                                            _("%s into %s(%s of %s BY %s)"),
                                            var_get_name (src_vars[v]),
                                            var_get_name (rank_specs[i].destvars[v]),
                                            function_name[rank_specs[i].rfunc],
                                            var_get_name (src_vars[v]),
                                            ds_cstr (&varlist));
		  ds_destroy (&varlist);
		}
	      else
		{
		  if ( rank_specs[i].rfunc == NORMAL ||
		       rank_specs[i].rfunc == PROPORTION )
		    tab_output_text_format (0,
                                            _("%s into %s(%s of %s using %s)"),
                                            var_get_name (src_vars[v]),
                                            var_get_name (rank_specs[i].destvars[v]),
                                            function_name[rank_specs[i].rfunc],
                                            var_get_name (src_vars[v]),
                                            fraction_name());

		  else
		    tab_output_text_format (0,
                                            _("%s into %s(%s of %s)"),
                                            var_get_name (src_vars[v]),
                                            var_get_name (rank_specs[i].destvars[v]),
                                            function_name[rank_specs[i].rfunc],
                                            var_get_name (src_vars[v]));
		}
	    }
	}
    }

  if ( cmd.sbc_fraction &&
       ( ! cmd.sbc_normal && ! cmd.sbc_proportion) )
    msg(MW, _("FRACTION has been specified, but NORMAL and PROPORTION rank functions have not been requested.  The FRACTION subcommand will be ignored.") );

  /* Add a variable which we can sort by to get back the original
     order */
  order = dict_create_var_assert (dataset_dict (ds), "$ORDER_", 0);

  add_transformation (ds, create_resort_key, 0, order);

  /* Do the ranking */
  result = rank_cmd (ds, &sc, rank_specs, n_rank_specs);

  /* Put the active file back in its original order.  Delete
     our sort key, which we don't need anymore.  */
  {
    struct casereader *sorted;

    /* FIXME: loses error conditions. */

    proc_discard_output (ds);
    sorted = sort_execute_1var (proc_open (ds), order);
    result = proc_commit (ds) && result;

    dict_delete_var (dataset_dict (ds), order);
    result = proc_set_active_file_data (ds, sorted) && result;
  }

  rank_cleanup();


  return (result ? CMD_SUCCESS : CMD_CASCADING_FAILURE);
}


/* Parser for the variables sub command
   Returns 1 on success */
static int
rank_custom_variables (struct lexer *lexer, struct dataset *ds, struct cmd_rank *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, T_EQUALS);

  if ((lex_token (lexer) != T_ID || dict_lookup_var (dataset_dict (ds), lex_tokcstr (lexer)) == NULL)
      && lex_token (lexer) != T_ALL)
      return 2;

  if (!parse_sort_criteria (lexer, dataset_dict (ds), &sc, &src_vars, NULL))
    return 0;
  n_src_vars = subcase_get_n_fields (&sc);

  if ( lex_match (lexer, T_BY)  )
    {
      if ((lex_token (lexer) != T_ID || dict_lookup_var (dataset_dict (ds), lex_tokcstr (lexer)) == NULL))
	{
	  return 2;
	}

      if (!parse_variables_const (lexer, dataset_dict (ds),
			    &group_vars, &n_group_vars,
			    PV_NO_DUPLICATE | PV_NO_SCRATCH) )
	{
	  free (group_vars);
	  return 0;
	}
    }

  return 1;
}


/* Parse the [/rank INTO var1 var2 ... varN ] clause */
static int
parse_rank_function (struct lexer *lexer, struct dictionary *dict, struct cmd_rank *cmd UNUSED, enum RANK_FUNC f)
{
  int var_count = 0;

  n_rank_specs++;
  rank_specs = xnrealloc(rank_specs, n_rank_specs, sizeof *rank_specs);
  rank_specs[n_rank_specs - 1].rfunc = f;
  rank_specs[n_rank_specs - 1].destvars = NULL;

  rank_specs[n_rank_specs - 1].destvars =
	    xcalloc (subcase_get_n_fields (&sc), sizeof (struct variable *));

  if (lex_match_id (lexer, "INTO"))
    {
      struct variable *destvar;

      while( lex_token (lexer) == T_ID )
	{

	  if ( dict_lookup_var (dict, lex_tokcstr (lexer)) != NULL )
	    {
	      msg(SE, _("Variable %s already exists."), lex_tokcstr (lexer));
	      return 0;
	    }
	  if ( var_count >= subcase_get_n_fields (&sc) )
	    {
	      msg(SE, _("Too many variables in INTO clause."));
	      return 0;
	    }

	  destvar = create_rank_variable (dict, f, src_vars[var_count], lex_tokcstr (lexer));
	  rank_specs[n_rank_specs - 1].destvars[var_count] = destvar ;

	  lex_get (lexer);
	  ++var_count;
	}
    }

  return 1;
}


static int
rank_custom_rank (struct lexer *lexer, struct dataset *ds, struct cmd_rank *cmd, void *aux UNUSED )
{
  struct dictionary *dict = dataset_dict (ds);

  return parse_rank_function (lexer, dict, cmd, RANK);
}

static int
rank_custom_normal (struct lexer *lexer, struct dataset *ds, struct cmd_rank *cmd, void *aux UNUSED )
{
  struct  dictionary *dict = dataset_dict (ds);

  return parse_rank_function (lexer, dict, cmd, NORMAL);
}

static int
rank_custom_percent (struct lexer *lexer, struct dataset *ds, struct cmd_rank *cmd, void *aux UNUSED )
{
  struct dictionary *dict = dataset_dict (ds);

  return parse_rank_function (lexer, dict, cmd, PERCENT);
}

static int
rank_custom_rfraction (struct lexer *lexer, struct dataset *ds, struct cmd_rank *cmd, void *aux UNUSED )
{
  struct dictionary *dict = dataset_dict (ds);

  return parse_rank_function (lexer, dict, cmd, RFRACTION);
}

static int
rank_custom_proportion (struct lexer *lexer, struct dataset *ds, struct cmd_rank *cmd, void *aux UNUSED )
{
  struct dictionary *dict = dataset_dict (ds);

  return parse_rank_function (lexer, dict, cmd, PROPORTION);
}

static int
rank_custom_n (struct lexer *lexer, struct dataset *ds, struct cmd_rank *cmd, void *aux UNUSED )
{
  struct dictionary *dict = dataset_dict (ds);

  return parse_rank_function (lexer, dict, cmd, N);
}

static int
rank_custom_savage (struct lexer *lexer, struct dataset *ds, struct cmd_rank *cmd, void *aux UNUSED )
{
  struct dictionary *dict = dataset_dict (ds);

  return parse_rank_function (lexer, dict, cmd, SAVAGE);
}


static int
rank_custom_ntiles (struct lexer *lexer, struct dataset *ds, struct cmd_rank *cmd, void *aux UNUSED )
{
  struct dictionary *dict = dataset_dict (ds);

  if ( lex_force_match (lexer, T_LPAREN) )
    {
      if ( lex_force_int (lexer) )
	{
	  k_ntiles = lex_integer (lexer);
	  lex_get (lexer);
	  lex_force_match (lexer, T_RPAREN);
	}
      else
	return 0;
    }
  else
    return 0;

  return parse_rank_function (lexer, dict, cmd, NTILES);
}

/*
  Local Variables:
  mode: c
  End:
*/
