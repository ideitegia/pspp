/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2006, 2007, 2009, 2010, 2011, 2012 Free Software Foundation, Inc

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

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "data/subcase.h"
#include "data/casewriter.h"
#include "data/short-names.h"

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "language/stats/sort-criteria.h"

#include "math/sort.h"

#include "libpspp/assertion.h"
#include "libpspp/misc.h"
#include "libpspp/taint.h"
#include "libpspp/pool.h"
#include "libpspp/message.h"


#include "output/tab.h"

#include <math.h>

#include <gsl/gsl_cdf.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

struct rank;

typedef double (*rank_function_t) (const struct rank*, double c, double cc, double cc_1,
				   int i, double w);

static double rank_proportion (const struct rank *, double c, double cc, double cc_1,
			       int i, double w);

static double rank_normal (const struct rank *, double c, double cc, double cc_1,
			   int i, double w);

static double rank_percent (const struct rank *, double c, double cc, double cc_1,
			    int i, double w);

static double rank_rfraction (const struct rank *, double c, double cc, double cc_1,
			      int i, double w);

static double rank_rank (const struct rank *, double c, double cc, double cc_1,
			 int i, double w);

static double rank_n (const struct rank *, double c, double cc, double cc_1,
		      int i, double w);

static double rank_savage (const struct rank *, double c, double cc, double cc_1,
			   int i, double w);

static double rank_ntiles (const struct rank *, double c, double cc, double cc_1,
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


enum ties
  {
    TIES_LOW,
    TIES_HIGH,
    TIES_MEAN,
    TIES_CONDENSE
  };

enum fraction
  {
    FRAC_BLOM,
    FRAC_RANKIT,
    FRAC_TUKEY,
    FRAC_VW
  };

struct rank_spec
{
  enum RANK_FUNC rfunc;
  struct variable **destvars;
};


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

struct rank
{
  struct dictionary *dict;

  struct subcase sc;

  const struct variable **vars;
  size_t n_vars;

  bool ascending;

  const struct variable **group_vars;
  size_t n_group_vars;


  enum mv_class exclude;

  struct rank_spec *rs;
  size_t n_rs;

  enum ties ties;

  enum fraction fraction;
  int k_ntiles;

  bool print;

  /* Pool on which cell functions may allocate data */
  struct pool *pool;
};


static void
destroy_rank (struct rank *rank)
{
 free (rank->vars);
 free (rank->group_vars);
 subcase_destroy (&rank->sc);
 pool_destroy (rank->pool);
}

static bool
parse_into (struct lexer *lexer, struct rank *cmd)
{
  int var_count = 0;
  struct rank_spec *rs = NULL;

  cmd->rs = pool_realloc (cmd->pool, cmd->rs, sizeof (*cmd->rs) * (cmd->n_rs + 1));
  rs = &cmd->rs[cmd->n_rs];
      
  if (lex_match_id (lexer, "RANK"))
    {
      rs->rfunc = RANK;
    }
  else if (lex_match_id (lexer, "NORMAL"))
    {
      rs->rfunc = NORMAL;
    }
  else if (lex_match_id (lexer, "RFRACTION"))
    {
      rs->rfunc = RFRACTION;
    }
  else if (lex_match_id (lexer, "N"))
    {
      rs->rfunc = N;
    }
  else if (lex_match_id (lexer, "SAVAGE"))
    {
      rs->rfunc = SAVAGE;
    }
  else if (lex_match_id (lexer, "PERCENT"))
    {
      rs->rfunc = PERCENT;
    }
  else if (lex_match_id (lexer, "PROPORTION"))
    {
      rs->rfunc = PROPORTION;
    }
  else if (lex_match_id (lexer, "NTILES"))
    {
      if ( !lex_force_match (lexer, T_LPAREN))
	return false;
      
      if (! lex_force_int (lexer) )
	return false;
      
      cmd->k_ntiles = lex_integer (lexer);
      lex_get (lexer);
      
      if ( !lex_force_match (lexer, T_RPAREN))
	return false;

      rs->rfunc = NTILES;
    }
  else
    {
      return false;
    }

  cmd->n_rs++;
  rs->destvars = NULL;
  rs->destvars = pool_calloc (cmd->pool, cmd->n_vars, sizeof (*rs->destvars));

  if (lex_match_id (lexer, "INTO"))
    {
      while( lex_token (lexer) == T_ID )
	{
	  const char *name = lex_tokcstr (lexer);
	  if ( dict_lookup_var (cmd->dict, name) != NULL )
	    {
	      msg (SE, _("Variable %s already exists."), name);
	      return false;
	    }
	  
	  if ( var_count >= subcase_get_n_fields (&cmd->sc) )
	    {
	      msg (SE, _("Too many variables in INTO clause."));
	      return false;
	    }
	  rs->destvars[var_count] = 
	    create_rank_variable (cmd->dict, rs->rfunc, cmd->vars[var_count], name);
	  ++var_count;
	  lex_get (lexer);
	}
    }

  return true;
}

/* Hardly a rank function !! */
static double
rank_n (const struct rank *cmd UNUSED, double c UNUSED, double cc UNUSED, double cc_1 UNUSED,
	int i UNUSED, double w)
{
  return w;
}


static double
rank_rank (const struct rank *cmd, double c, double cc, double cc_1,
	   int i, double w UNUSED)
{
  double rank;

  if ( c >= 1.0 )
    {
      switch (cmd->ties)
	{
	case TIES_LOW:
	  rank = cc_1 + 1;
	  break;
	case TIES_HIGH:
	  rank = cc;
	  break;
	case TIES_MEAN:
	  rank = cc_1 + (c + 1.0)/ 2.0;
	  break;
	case TIES_CONDENSE:
	  rank = i;
	  break;
	default:
	  NOT_REACHED ();
	}
    }
  else
    {
      switch (cmd->ties)
	{
	case TIES_LOW:
	  rank = cc_1;
	  break;
	case TIES_HIGH:
	  rank = cc;
	  break;
	case TIES_MEAN:
	  rank = cc_1 + c / 2.0 ;
	  break;
	case TIES_CONDENSE:
	  rank = i;
	  break;
	default:
	  NOT_REACHED ();
	}
    }

  return rank;
}


static double
rank_rfraction (const struct rank *cmd, double c, double cc, double cc_1,
		int i, double w)
{
  return rank_rank (cmd, c, cc, cc_1, i, w) / w ;
}


static double
rank_percent (const struct rank *cmd, double c, double cc, double cc_1,
	      int i, double w)
{
  return rank_rank (cmd, c, cc, cc_1, i, w) * 100.0 / w ;
}


static double
rank_proportion (const struct rank *cmd, double c, double cc, double cc_1,
		 int i, double w)
{
  const double r =  rank_rank (cmd, c, cc, cc_1, i, w) ;

  double f;

  switch ( cmd->fraction )
    {
    case FRAC_BLOM:
      f =  (r - 3.0/8.0) / (w + 0.25);
      break;
    case FRAC_RANKIT:
      f = (r - 0.5) / w ;
      break;
    case FRAC_TUKEY:
      f = (r - 1.0/3.0) / (w + 1.0/3.0);
      break;
    case FRAC_VW:
      f = r / ( w + 1.0);
      break;
    default:
      NOT_REACHED ();
    }


  return (f > 0) ? f : SYSMIS;
}

static double
rank_normal (const struct rank *cmd, double c, double cc, double cc_1,
	     int i, double w)
{
  double f = rank_proportion (cmd, c, cc, cc_1, i, w);

  return gsl_cdf_ugaussian_Pinv (f);
}

static double
rank_ntiles (const struct rank *cmd, double c, double cc, double cc_1,
	     int i, double w)
{
  double r = rank_rank (cmd, c, cc, cc_1, i, w);


  return ( floor (( r * cmd->k_ntiles) / ( w + 1) ) + 1);
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
rank_savage (const struct rank *cmd UNUSED, double c, double cc, double cc_1,
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
                  int dest_idx,
		  const struct rank *cmd
		  )
{
  struct casereader *pass1, *pass2, *pass2_1;
  struct casegrouper *tie_grouper;
  struct ccase *c;
  double w = 0.0;
  double cc = 0.0;
  int tie_group = 1;

  input = casereader_create_filter_missing (input, &cmd->vars[dest_idx], 1,
                                            cmd->exclude, NULL, output);
  input = casereader_create_filter_weight (input, dict, NULL, output);

  casereader_split (input, &pass1, &pass2);

  /* Pass 1: Get total group weight. */
  for (; (c = casereader_read (pass1)) != NULL; case_unref (c))
    w += dict_get_case_weight (dict, c, NULL);
  casereader_destroy (pass1);

  /* Pass 2: Do ranking. */
  tie_grouper = casegrouper_create_vars (pass2, &cmd->vars[dest_idx], 1);
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
          for (i = 0; i < cmd->n_rs; ++i)
            {
              const struct variable *dst_var = cmd->rs[i].destvars[dest_idx];
              double *dst_value = &case_data_rw (c, dst_var)->f;
              *dst_value = rank_func[cmd->rs[i].rfunc] (cmd, tw, cc, cc_1, tie_group, w);
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

static bool
rank_cmd (struct dataset *ds,  const struct rank *cmd);


static const char *
fraction_name (const struct rank *cmd)
{
  static char name[10];
  switch (cmd->fraction )
    {
    case FRAC_BLOM:
      strcpy (name, "BLOM");
      break;
    case FRAC_RANKIT:
      strcpy (name, "RANKIT");
      break;
    case FRAC_TUKEY:
      strcpy (name, "TUKEY");
      break;
    case FRAC_VW:
      strcpy (name, "VW");
      break;
    default:
      NOT_REACHED ();
    }
  return name;
}

/* Create a label on DEST_VAR, describing its derivation from SRC_VAR and F */
static void
create_var_label (struct rank *cmd, struct variable *dest_var,
		  const struct variable *src_var, enum RANK_FUNC f)
{
  struct string label;
  ds_init_empty (&label);

  if ( cmd->n_group_vars > 0 )
    {
      struct string group_var_str;
      int g;

      ds_init_empty (&group_var_str);

      for (g = 0 ; g < cmd->n_group_vars ; ++g )
	{
	  if ( g > 0 ) ds_put_cstr (&group_var_str, " ");
	  ds_put_cstr (&group_var_str, var_get_name (cmd->group_vars[g]));
	}

      ds_put_format (&label, _("%s of %s by %s"), function_name[f],
		     var_get_name (src_var), ds_cstr (&group_var_str));
      ds_destroy (&group_var_str);
    }
  else
    ds_put_format (&label, _("%s of %s"),
                   function_name[f], var_get_name (src_var));

  var_set_label (dest_var, ds_cstr (&label), false);
  
  ds_destroy (&label);
}

int
cmd_rank (struct lexer *lexer, struct dataset *ds)
{
  struct rank rank;
  struct variable *order;
  bool result = true;
  int i;

  subcase_init_empty (&rank.sc);

  rank.rs = NULL;
  rank.n_rs = 0;
  rank.exclude = MV_ANY;
  rank.n_group_vars = 0;
  rank.group_vars = NULL;
  rank.dict = dataset_dict (ds);
  rank.ties = TIES_MEAN;
  rank.fraction = FRAC_BLOM;
  rank.print = true;
  rank.pool = pool_create ();

  if (lex_match_id (lexer, "VARIABLES"))
    lex_force_match (lexer, T_EQUALS);

  if (!parse_sort_criteria (lexer, rank.dict,
			    &rank.sc,
			    &rank.vars,
			    &rank.ascending))
    goto error;

  rank.n_vars = rank.sc.n_fields;

  if (lex_match (lexer, T_BY) )
    {
      if ( ! parse_variables_const (lexer, rank.dict,
				    &rank.group_vars, &rank.n_group_vars,
				    PV_NO_DUPLICATE | PV_NO_SCRATCH))
	goto error;
    }


  while (lex_token (lexer) != T_ENDCMD )
    {
      lex_force_match (lexer, T_SLASH);
      if (lex_match_id (lexer, "TIES"))
	{
	  lex_force_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "MEAN"))
	    {
	      rank.ties = TIES_MEAN;
	    }
	  else if (lex_match_id (lexer, "LOW"))
	    {
	      rank.ties = TIES_LOW;
	    }
	  else if (lex_match_id (lexer, "HIGH"))
	    {
	      rank.ties = TIES_HIGH;
	    }
	  else if (lex_match_id (lexer, "CONDENSE"))
	    {
	      rank.ties = TIES_CONDENSE;
	    }
	  else
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "FRACTION"))
	{
	  lex_force_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "BLOM"))
	    {
	      rank.fraction = FRAC_BLOM;
	    }
	  else if (lex_match_id (lexer, "TUKEY"))
	    {
	      rank.fraction = FRAC_TUKEY;
	    }
	  else if (lex_match_id (lexer, "VW"))
	    {
	      rank.fraction = FRAC_VW;
	    }
	  else if (lex_match_id (lexer, "RANKIT"))
	    {
	      rank.fraction = FRAC_RANKIT;
	    }
	  else
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "PRINT"))
	{
	  lex_force_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "YES"))
	    {
	      rank.print = true;
	    }
	  else if (lex_match_id (lexer, "NO"))
	    {
	      rank.print = false;
	    }
	  else
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "MISSING"))
	{
	  lex_force_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "INCLUDE"))
	    {
	      rank.exclude = MV_SYSTEM;
	    }
	  else if (lex_match_id (lexer, "EXCLUDE"))
	    {
	      rank.exclude = MV_ANY;
	    }
	  else
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }
	}
      else if (! parse_into (lexer, &rank))
	goto error;
    }


  /* If no rank specs are given, then apply a default */
  if ( rank.n_rs == 0)
    {
      rank.rs = pool_calloc (rank.pool, 1, sizeof (*rank.rs));
      rank.n_rs = 1;
      rank.rs[0].rfunc = RANK;
      rank.rs[0].destvars = pool_calloc (rank.pool, rank.n_vars, sizeof (*rank.rs[0].destvars));
    }

  /* Create variables for all rank destinations which haven't
     already been created with INTO.
     Add labels to all the destination variables.
  */
  for (i = 0 ; i <  rank.n_rs ; ++i )
    {
      int v;
      struct rank_spec *rs = &rank.rs[i];

      for ( v = 0 ; v < rank.n_vars ;  v ++ )
	{
	  if ( rs->destvars[v] == NULL )
	    {
	      rs->destvars[v] =
		create_rank_variable (rank.dict, rs->rfunc, rank.vars[v], NULL);
	    }

	  create_var_label (&rank, rs->destvars[v],
			    rank.vars[v],
			    rs->rfunc);
	}
    }

  if ( rank.print )
    {
      int v;

      tab_output_text (0, _("Variables Created By RANK"));
      tab_output_text (0, "");

      for (i = 0 ; i <  rank.n_rs ; ++i )
	{
	  for ( v = 0 ; v < rank.n_vars ;  v ++ )
	    {
	      if ( rank.n_group_vars > 0 )
		{
		  struct string varlist;
		  int g;

		  ds_init_empty (&varlist);
		  for ( g = 0 ; g < rank.n_group_vars ; ++g )
		    {
		      ds_put_cstr (&varlist, var_get_name (rank.group_vars[g]));

		      if ( g < rank.n_group_vars - 1)
			ds_put_cstr (&varlist, " ");
		    }

		  if ( rank.rs[i].rfunc == NORMAL ||
		       rank.rs[i].rfunc == PROPORTION )
		    tab_output_text_format (0,
                                            _("%s into %s(%s of %s using %s BY %s)"),
                                            var_get_name (rank.vars[v]),
                                            var_get_name (rank.rs[i].destvars[v]),
                                            function_name[rank.rs[i].rfunc],
                                            var_get_name (rank.vars[v]),
                                            fraction_name (&rank),
                                            ds_cstr (&varlist));

		  else
		    tab_output_text_format (0,
                                            _("%s into %s(%s of %s BY %s)"),
                                            var_get_name (rank.vars[v]),
                                            var_get_name (rank.rs[i].destvars[v]),
                                            function_name[rank.rs[i].rfunc],
                                            var_get_name (rank.vars[v]),
                                            ds_cstr (&varlist));
		  ds_destroy (&varlist);
		}
	      else
		{
		  if ( rank.rs[i].rfunc == NORMAL ||
		       rank.rs[i].rfunc == PROPORTION )
		    tab_output_text_format (0,
                                            _("%s into %s(%s of %s using %s)"),
                                            var_get_name (rank.vars[v]),
                                            var_get_name (rank.rs[i].destvars[v]),
                                            function_name[rank.rs[i].rfunc],
                                            var_get_name (rank.vars[v]),
                                            fraction_name (&rank));

		  else
		    tab_output_text_format (0,
                                            _("%s into %s(%s of %s)"),
                                            var_get_name (rank.vars[v]),
                                            var_get_name (rank.rs[i].destvars[v]),
                                            function_name[rank.rs[i].rfunc],
                                            var_get_name (rank.vars[v]));
		}
	    }
	}
    }

  /* Add a variable which we can sort by to get back the original
     order */
  order = dict_create_var_assert (dataset_dict (ds), "$ORDER_", 0);

  add_transformation (ds, create_resort_key, 0, order);

  /* Do the ranking */
  result = rank_cmd (ds, &rank);
  
  /* Put the active dataset back in its original order.  Delete
     our sort key, which we don't need anymore.  */
  {
    struct casereader *sorted;


    /* FIXME: loses error conditions. */

    proc_discard_output (ds);
    sorted = sort_execute_1var (proc_open (ds), order);
    result = proc_commit (ds) && result;

    dict_delete_var (dataset_dict (ds), order);
    result = dataset_set_source (ds, sorted) && result;
    if ( result != true)
      goto error;
  }

  destroy_rank (&rank);
  return CMD_SUCCESS;

 error:

  destroy_rank (&rank);
  return CMD_FAILURE;
}



static bool
rank_cmd (struct dataset *ds, const struct rank *cmd)
{
  struct dictionary *d = dataset_dict (ds);
  bool ok = true;
  int i;

  for (i = 0 ; i < subcase_get_n_fields (&cmd->sc) ; ++i )
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
          subcase_init_vars (&ordering, cmd->group_vars, cmd->n_group_vars);
          subcase_add_var (&ordering, cmd->vars[i],
                           subcase_get_direction (&cmd->sc, i));
          ordered = sort_execute (split_group, &ordering);
          subcase_destroy (&ordering);

          /* Rank the rank variable within this split group. */
          by_grouper = casegrouper_create_vars (ordered,
                                                cmd->group_vars, cmd->n_group_vars);
          while (casegrouper_get_next_group (by_grouper, &by_group))
            {
              /* Rank the rank variable within this BY group
                 within the split group. */

              rank_sorted_file (by_group, output, d,  i, cmd);

            }
          ok = casegrouper_destroy (by_grouper) && ok;
        }
      ok = casegrouper_destroy (split_grouper);
      ok = proc_commit (ds) && ok;
      ok = (dataset_set_source (ds, casewriter_make_reader (output))
            && ok);
      if (!ok)
        break;
    }

  return ok;
}
