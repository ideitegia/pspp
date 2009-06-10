/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

#include "roc.h"
#include <data/procedure.h>
#include <language/lexer/variable-parser.h>
#include <language/lexer/value-parser.h>
#include <language/lexer/lexer.h>

#include <data/casegrouper.h>
#include <data/casereader.h>
#include <data/casewriter.h>
#include <data/dictionary.h>
#include <data/format.h>
#include <math/sort.h>

#include <libpspp/misc.h>

#include <gsl/gsl_cdf.h>
#include <output/table.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct cmd_roc
{
  size_t n_vars;
  const struct variable **vars;

  struct variable *state_var ;
  union value state_value;

  /* Plot the roc curve */
  bool curve;
  /* Plot the reference line */
  bool reference;

  double ci;

  bool print_coords;
  bool print_se;
  bool bi_neg_exp; /* True iff the bi-negative exponential critieria
		      should be used */
  enum mv_class exclude;

  bool invert ; /* True iff a smaller test result variable indicates
		   a positive result */
};

static int run_roc (struct dataset *ds, struct cmd_roc *roc);

int
cmd_roc (struct lexer *lexer, struct dataset *ds)
{
  struct cmd_roc roc ;
  const struct dictionary *dict = dataset_dict (ds);

  roc.vars = NULL;
  roc.n_vars = 0;
  roc.print_se = false;
  roc.print_coords = false;
  roc.exclude = MV_ANY;
  roc.curve = true;
  roc.reference = false;
  roc.ci = 95;
  roc.bi_neg_exp = false;
  roc.invert = false;

  if (!parse_variables_const (lexer, dict, &roc.vars, &roc.n_vars,
			      PV_APPEND | PV_NO_DUPLICATE | PV_NUMERIC))
    return 2;

  if ( ! lex_force_match (lexer, T_BY))
    {
      return 2;
    }

  roc.state_var = parse_variable (lexer, dict);

  if ( !lex_force_match (lexer, '('))
    {
      return 2;
    }

  parse_value (lexer, &roc.state_value, var_get_width (roc.state_var));


  if ( !lex_force_match (lexer, ')'))
    {
      return 2;
    }


  while (lex_token (lexer) != '.')
    {
      lex_match (lexer, '/');
      if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, '=');
          while (lex_token (lexer) != '.' && lex_token (lexer) != '/')
            {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  roc.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  roc.exclude = MV_ANY;
		}
	      else
		{
                  lex_error (lexer, NULL);
		  return 2;
		}
	    }
	}
      else if (lex_match_id (lexer, "PLOT"))
	{
	  lex_match (lexer, '=');
	  if (lex_match_id (lexer, "CURVE"))
	    {
	      roc.curve = true;
	      if (lex_match (lexer, '('))
		{
		  roc.reference = true;
		  lex_force_match_id (lexer, "REFERENCE");
		  lex_force_match (lexer, ')');
		}
	    }
	  else if (lex_match_id (lexer, "NONE"))
	    {
	      roc.curve = false;
	    }
	  else
	    {
	      lex_error (lexer, NULL);
	      return 2;
	    }
	}
      else if (lex_match_id (lexer, "PRINT"))
	{
	  lex_match (lexer, '=');
          while (lex_token (lexer) != '.' && lex_token (lexer) != '/')
	    {
	      if (lex_match_id (lexer, "SE"))
		{
		  roc.print_se = true;
		}
	      else if (lex_match_id (lexer, "COORDINATES"))
		{
		  roc.print_coords = true;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  return 2;
		}
	    }
	}
      else if (lex_match_id (lexer, "CRITERIA"))
	{
	  lex_match (lexer, '=');
          while (lex_token (lexer) != '.' && lex_token (lexer) != '/')
	    {
	      if (lex_match_id (lexer, "CUTOFF"))
		{
		  lex_force_match (lexer, '(');
		  if (lex_match_id (lexer, "INCLUDE"))
		    {
		      roc.exclude = MV_SYSTEM;
		    }
		  else if (lex_match_id (lexer, "EXCLUDE"))
		    {
		      roc.exclude = MV_USER | MV_SYSTEM;
		    }
		  else
		    {
		      lex_error (lexer, NULL);
		      return 2;
		    }
		  lex_force_match (lexer, ')');
		}
	      else if (lex_match_id (lexer, "TESTPOS"))
		{
		  lex_force_match (lexer, '(');
		  if (lex_match_id (lexer, "LARGE"))
		    {
		      roc.invert = false;
		    }
		  else if (lex_match_id (lexer, "SMALL"))
		    {
		      roc.invert = true;
		    }
		  else
		    {
		      lex_error (lexer, NULL);
		      return 2;
		    }
		  lex_force_match (lexer, ')');
		}
	      else if (lex_match_id (lexer, "CI"))
		{
		  lex_force_match (lexer, '(');
		  lex_force_num (lexer);
		  roc.ci = lex_number (lexer);
		  lex_get (lexer);
		  lex_force_match (lexer, ')');
		}
	      else if (lex_match_id (lexer, "DISTRIBUTION"))
		{
		  lex_force_match (lexer, '(');
		  if (lex_match_id (lexer, "FREE"))
		    {
		      roc.bi_neg_exp = false;
		    }
		  else if (lex_match_id (lexer, "NEGEXPO"))
		    {
		      roc.bi_neg_exp = true;
		    }
		  else
		    {
		      lex_error (lexer, NULL);
		      return 2;
		    }
		  lex_force_match (lexer, ')');
		}
	      else
		{
		  lex_error (lexer, NULL);
		  return 2;
		}
	    }
	}
      else
	{
	  lex_error (lexer, NULL);
	  break;
	}
    }

  run_roc (ds, &roc);

  return 1;
}




static void
do_roc (struct cmd_roc *roc, struct casereader *group, struct dictionary *dict);


static int
run_roc (struct dataset *ds, struct cmd_roc *roc)
{
  struct dictionary *dict = dataset_dict (ds);
  bool ok;
  struct casereader *group;

  struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), dict);
  while (casegrouper_get_next_group (grouper, &group))
    {
      do_roc (roc, group, dataset_dict (ds));
    }
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  return ok;
}


static void
dump_casereader (struct casereader *reader)
{
  struct ccase *c;
  struct casereader *r = casereader_clone (reader);

  for ( ; (c = casereader_read (r) ); case_unref (c))
    {
      int i;
      for (i = 0 ; i < case_get_value_cnt (c); ++i)
	{
	  printf ("%g ", case_data_idx (c, i)->f);
	}
      printf ("\n");
    }

  casereader_destroy (r);
}

static bool
match_positives (const struct ccase *c, void *aux)
{
  struct cmd_roc *roc = aux;

  return 0 == value_compare_3way (case_data (c, roc->state_var),
				 &roc->state_value,
				 var_get_width (roc->state_var));
}


#define VALUE  0
#define N_EQ   1
#define N_PRED 2

struct roc_state
{
  double auc;

  double n1;
  double n2;

  double q1hat;
  double q2hat;
};


static void output_roc (struct roc_state *rs, const struct cmd_roc *roc);



static struct casereader *
process_group (const struct variable *var, struct casereader *reader,
	       bool (*pred) (double, double),
	       const struct dictionary *dict,
	       double *cc)
{
  const struct variable *w = dict_get_weight (dict);
  const int weight_idx  = w ? var_get_case_index (w) :
    caseproto_get_n_widths (casereader_get_proto (reader)) - 1;

  struct casereader *r1 =
    casereader_create_distinct (sort_execute_1var (reader, var), var, w);

  struct ccase *c1;

  struct casereader *rclone = casereader_clone (r1);
  struct casewriter *wtr;
  struct caseproto *proto = caseproto_create ();

  proto = caseproto_add_width (proto, 0);
  proto = caseproto_add_width (proto, 0);
  proto = caseproto_add_width (proto, 0);

  wtr = autopaging_writer_create (proto);  

  *cc = 0;
  
  for ( ; (c1 = casereader_read (r1) ); case_unref (c1))
    {
      struct ccase *c2;
      struct casereader *r2 = casereader_clone (rclone);

      const double weight1 = case_data_idx (c1, weight_idx)->f;
      const double d1 = case_data (c1, var)->f;
      double n_eq = 0.0;
      double n_pred = 0.0;


      struct ccase *new_case = case_create (proto);

      *cc += weight1;

      for ( ; (c2 = casereader_read (r2) ); case_unref (c2))
	{
	  const double d2 = case_data (c2, var)->f;
	  const double weight2 = case_data_idx (c2, weight_idx)->f;

	  if ( d1 == d2 )
	    {
	      n_eq += weight2;
	      continue;
	    }
	  else  if ( pred (d2, d1))
	    {
	      n_pred += weight2;
	    }
	}

      case_data_rw_idx (new_case, VALUE)->f = d1;
      case_data_rw_idx (new_case, N_EQ)->f = n_eq;
      case_data_rw_idx (new_case, N_PRED)->f = n_pred;

      casewriter_write (wtr, new_case);

      casereader_destroy (r2);
    }

  casereader_destroy (r1);
  casereader_destroy (rclone);

  return casewriter_make_reader (wtr);
}

static bool
gt (double d1, double d2)
{
  return d1 > d2;
}

static bool
lt (double d1, double d2)
{
  return d1 < d2;
}


static void
do_roc (struct cmd_roc *roc, struct casereader *input, struct dictionary *dict)
{
  int i;

  struct roc_state *rs = xcalloc (roc->n_vars, sizeof *rs);

  const struct caseproto *proto = casereader_get_proto (input);

  struct casewriter *neg_wtr = autopaging_writer_create (proto);

  struct casereader *negatives = NULL;

  struct casereader *positives = 
    casereader_create_filter_func (input,
				   match_positives,
				   NULL,
				   roc,
				   neg_wtr);


  for (i = 0 ; i < roc->n_vars; ++i)
    {
      struct ccase *cpos;
      struct casereader *n_neg ;
      const struct variable *var = roc->vars[i];

      struct casereader *neg ;
      struct casereader *pos = casereader_clone (positives);

      struct casereader *n_pos = process_group (var, pos, gt, dict, &rs[i].n1);

      if ( negatives == NULL)
	{
	  negatives = casewriter_make_reader (neg_wtr);
	}
    
      neg = casereader_clone (negatives);

      n_neg = process_group (var, neg, lt, dict, &rs[i].n2);

      /* Simple join on VALUE */
      for ( ; (cpos = casereader_read (n_pos) ); case_unref (cpos))
	{
	  struct ccase *cneg = NULL;
	  double dneg = -DBL_MAX;
	  const double dpos = case_data_idx (cpos, VALUE)->f;
	  while (dneg < dpos)
	    {
	      if ( cneg )
		case_unref (cneg);

	      cneg = casereader_read (n_neg);
	      dneg = case_data_idx (cneg, VALUE)->f;
	    }
	
	  if ( dpos == dneg )
	    {
	      double n_pos_eq = case_data_idx (cpos, N_EQ)->f;
	      double n_neg_eq = case_data_idx (cneg, N_EQ)->f;
	      double n_pos_gt = case_data_idx (cpos, N_PRED)->f;
	      double n_neg_lt = case_data_idx (cneg, N_PRED)->f;

	      rs[i].auc += n_pos_gt * n_neg_eq + (n_pos_eq * n_neg_eq) / 2.0;
	      rs[i].q1hat +=
		n_neg_eq * ( pow2 (n_pos_gt) + n_pos_gt * n_pos_eq + pow2 (n_pos_eq) / 3.0);
	      rs[i].q2hat +=
		n_pos_eq * ( pow2 (n_neg_lt) + n_neg_lt * n_neg_eq + pow2 (n_neg_eq) / 3.0);
	    }

	  if ( cneg )
	    case_unref (cneg);
	}

      rs[i].auc /=  rs[i].n1 * rs[i].n2; 
      if ( roc->invert ) 
	rs[i].auc = 1 - rs[i].auc;

      if ( roc->bi_neg_exp )
	{
	  rs[i].q1hat = rs[i].auc / ( 2 - rs[i].auc);
	  rs[i].q2hat = 2 * pow2 (rs[i].auc) / ( 1 + rs[i].auc);
	}
      else
	{
	  rs[i].q1hat /= rs[i].n2 * pow2 (rs[i].n1);
	  rs[i].q2hat /= rs[i].n1 * pow2 (rs[i].n2);
	}
    }

  casereader_destroy (positives);
  casereader_destroy (negatives);

  output_roc (rs, roc);

  free (rs);
}




static void
show_auc  (struct roc_state *rs, const struct cmd_roc *roc)
{
  int i;
  const int n_fields = roc->print_se ? 5 : 1;
  const int n_cols = roc->n_vars > 1 ? n_fields + 1: n_fields;
  const int n_rows = 2 + roc->n_vars;
  struct tab_table *tbl = tab_create (n_cols, n_rows, 0);

  if ( roc->n_vars > 1)
    tab_title (tbl, _("Area Under the Curve"));
  else
    tab_title (tbl, _("Area Under the Curve (%s)"), var_to_string (roc->vars[0]));

  tab_headers (tbl, n_cols - n_fields, 0, 1, 0);

  tab_dim (tbl, tab_natural_dimensions, NULL);

  tab_text (tbl, n_cols - n_fields, 1, TAT_TITLE, _("Area"));

  tab_hline (tbl, TAL_2, 0, n_cols - 1, 2);

  tab_box (tbl,
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1,
	   n_rows - 1);

  if ( roc->print_se )
    {
      tab_text (tbl, n_cols - 4, 1, TAT_TITLE, _("Std. Error"));
      tab_text (tbl, n_cols - 3, 1, TAT_TITLE, _("Asymptotic Sig."));

      tab_text (tbl, n_cols - 2, 1, TAT_TITLE, _("Lower Bound"));
      tab_text (tbl, n_cols - 1, 1, TAT_TITLE, _("Upper Bound"));

      tab_joint_text (tbl, n_cols - 2, 0, 4, 0,
		      TAT_TITLE | TAB_CENTER | TAT_PRINTF,
		      _("Asymp. %g%% Confidence Interval"), roc->ci);
      tab_vline (tbl, 0, n_cols - 1, 0, 0);
      tab_hline (tbl, TAL_1, n_cols - 2, n_cols - 1, 1);
    }

  if ( roc->n_vars > 1)
    tab_text (tbl, 0, 1, TAT_TITLE, _("Variable under test"));

  if ( roc->n_vars > 1)
    tab_vline (tbl, TAL_2, 1, 0, n_rows - 1);


  for ( i = 0 ; i < roc->n_vars ; ++i )
    {
      tab_text (tbl, 0, 2 + i, TAT_TITLE, var_to_string (roc->vars[i]));

      tab_double (tbl, n_cols - n_fields, 2 + i, 0, rs[i].auc, NULL);

      if ( roc->print_se )
	{

	  double se ;
	  const double sd_0_5 = sqrt ((rs[i].n1 + rs[i].n2 + 1) /
				      (12 * rs[i].n1 * rs[i].n2));
	  double ci ;
	  double yy ;

	  se = rs[i].auc * (1 - rs[i].auc) + (rs[i].n1 - 1) * (rs[i].q1hat - pow2 (rs[i].auc)) +
	    (rs[i].n2 - 1) * (rs[i].q2hat - pow2 (rs[i].auc));

	  se /= rs[i].n1 * rs[i].n2;

	  se = sqrt (se);

	  tab_double (tbl, n_cols - 4, 2 + i, 0,
		      se,
		      NULL);

	  ci = 1 - roc->ci / 100.0;
	  yy = gsl_cdf_gaussian_Qinv (ci, se) ;

	  tab_double (tbl, n_cols - 2, 2 + i, 0,
		      rs[i].auc - yy,
		      NULL);

	  tab_double (tbl, n_cols - 1, 2 + i, 0,
		      rs[i].auc + yy,
		      NULL);

	  tab_double (tbl, n_cols - 3, 2 + i, 0,
		      2.0 * gsl_cdf_ugaussian_Q (fabs ((rs[i].auc - 0.5 ) / sd_0_5)),
		      NULL);
	}
    }

  tab_submit (tbl);
}


static void
show_summary (const struct cmd_roc *roc)
{
  const int n_cols = 3;
  const int n_rows = 4;
  struct tab_table *tbl = tab_create (n_cols, n_rows, 0);

  tab_title (tbl, _("Case Summary"));

  tab_headers (tbl, 1, 0, 2, 0);

  tab_dim (tbl, tab_natural_dimensions, NULL);

  tab_box (tbl,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1,
	   n_rows - 1);

  tab_hline (tbl, TAL_2, 0, n_cols - 1, 2);
  tab_vline (tbl, TAL_2, 1, 0, n_rows - 1);


  tab_hline (tbl, TAL_2, 1, n_cols - 1, 1);
  tab_vline (tbl, TAL_1, 2, 1, n_rows - 1);


  tab_text (tbl, 0, 1, TAT_TITLE | TAB_LEFT, var_to_string (roc->state_var));
  tab_text (tbl, 1, 1, TAT_TITLE, _("Unweighted"));
  tab_text (tbl, 2, 1, TAT_TITLE, _("Weighted"));

  tab_joint_text (tbl, 1, 0, 2, 0,
		  TAT_TITLE | TAB_CENTER,
		  _("Valid N (listwise)"));


  tab_text (tbl, 0, 2, TAB_LEFT, _("Positive"));
  tab_text (tbl, 0, 3, TAB_LEFT, _("Negative"));


#if 0
  tab_double (tbl, 1, 2, 0, roc->pos, &F_8_0);
  tab_double (tbl, 1, 3, 0, roc->neg, &F_8_0);

  tab_double (tbl, 2, 2, 0, roc->pos_weighted, 0);
  tab_double (tbl, 2, 3, 0, roc->neg_weighted, 0);
#endif

  tab_submit (tbl);
}


static void
output_roc (struct roc_state *rs, const struct cmd_roc *roc)
{
  show_summary (roc);

#if 0

  if ( roc->curve )
    draw_roc (rs, roc);
#endif

  show_auc (rs, roc);

#if 0
  if ( roc->print_coords )
    show_coords (rs, roc);
#endif
}
