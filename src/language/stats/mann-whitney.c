/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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

#include "language/stats/mann-whitney.h"

#include <gsl/gsl_cdf.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "libpspp/cast.h"
#include "libpspp/misc.h"
#include "math/sort.h"
#include "output/tab.h"

/* Calculates the adjustment necessary for tie compensation */
static void
distinct_callback (double v UNUSED, casenumber t, double w UNUSED, void *aux)
{
  double *tiebreaker = aux;

  *tiebreaker += (pow3 (t) - t) / 12.0;
}

struct mw
{
  double rank_sum[2];
  double n[2];

  double u;  /* The Mann-Whitney U statistic */
  double w;  /* The Wilcoxon Rank Sum W statistic */
  double z;  
};

static void show_ranks_box (const struct n_sample_test *nst, const struct mw *mw);
static void show_statistics_box (const struct n_sample_test *nst, const struct mw *mw, bool exact);


void
mann_whitney_execute (const struct dataset *ds,
		      struct casereader *input,
		      enum mv_class exclude,
		      const struct npar_test *test,
		      bool exact,
		      double timer UNUSED)
{
  int i;
  const struct dictionary *dict = dataset_dict (ds);
  const struct n_sample_test *nst = UP_CAST (test, const struct n_sample_test, parent);

  const struct caseproto *proto = casereader_get_proto (input);
  size_t rank_idx = caseproto_get_n_widths (proto);

  struct mw *mw = xcalloc (nst->n_vars, sizeof *mw);

  for (i = 0; i < nst->n_vars; ++i)
    {
      double tiebreaker = 0.0;
      bool warn = true;
      enum rank_error rerr = 0;
      struct casereader *rr;
      struct ccase *c;
      const struct variable *var = nst->vars[i];
      
      struct casereader *reader =
	sort_execute_1var (casereader_clone (input), var);

      rr = casereader_create_append_rank (reader, var,
					  dict_get_weight (dict),
					  &rerr,
					  distinct_callback, &tiebreaker);

      for (; (c = casereader_read (rr)); case_unref (c))
	{
	  const union value *val = case_data (c, var);
	  const union value *group = case_data (c, nst->indep_var);
	  const size_t group_var_width = var_get_width (nst->indep_var);
	  const double rank = case_data_idx (c, rank_idx)->f;

	  if ( var_is_value_missing (var, val, exclude))
	    continue;

	  if ( value_equal (group, &nst->val1, group_var_width))
	    {
	      mw[i].rank_sum[0] += rank;
	      mw[i].n[0] += dict_get_case_weight (dict, c, &warn);
	    }
	  else if ( value_equal (group, &nst->val2, group_var_width))
	    {
	      mw[i].rank_sum[1] += rank;
	      mw[i].n[1] += dict_get_case_weight (dict, c, &warn);
	    }
	}
      casereader_destroy (rr);

      {
	double n;
	double denominator;
	struct mw *mwv = &mw[i];

	mwv->u = mwv->n[0] * mwv->n[1] ;
	mwv->u += mwv->n[0] * (mwv->n[0] + 1) / 2.0;
	mwv->u -= mwv->rank_sum[0];

	mwv->w = mwv->rank_sum[1];
	if ( mwv->u > mwv->n[0] * mwv->n[1] / 2.0)
	  {
	    mwv->u =  mwv->n[0] * mwv->n[1] - mwv->u;
	    mwv->w = mwv->rank_sum[0];
	  }
	mwv->z = mwv->u - mwv->n[0] * mwv->n[1] / 2.0;
	n = mwv->n[0] + mwv->n[1];
	denominator = pow3(n) - n;
	denominator /= 12;
	denominator -= tiebreaker;
	denominator *= mwv->n[0] * mwv->n[1];
	denominator /= n * (n - 1);
      
	mwv->z /= sqrt (denominator);
      }
    }
  casereader_destroy (input);

  show_ranks_box (nst, mw);
  show_statistics_box (nst, mw, exact);

  free (mw);
}



#include "gettext.h"
#define _(msgid) gettext (msgid)

static void
show_ranks_box (const struct n_sample_test *nst, const struct mw *mwv)
{
  int i;
  const int row_headers = 1;
  const int column_headers = 2;
  struct tab_table *table =
    tab_create (row_headers + 7, column_headers + nst->n_vars);

  struct string g1str, g2str;;
  ds_init_empty (&g1str);
  var_append_value_name (nst->indep_var, &nst->val1, &g1str);

  ds_init_empty (&g2str);
  var_append_value_name (nst->indep_var, &nst->val2, &g2str);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Ranks"));

  /* Vertical lines inside the box */
  tab_box (table, 1, 0, -1, TAL_1,
	   row_headers, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  /* Box around the table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );

  tab_hline (table, TAL_2, 0, tab_nc (table) -1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);

  tab_hline (table, TAL_1, row_headers, tab_nc (table) -1, 1);

  tab_text (table, 1, 1, TAT_TITLE | TAB_CENTER, ds_cstr (&g1str));
  tab_text (table, 2, 1, TAT_TITLE | TAB_CENTER, ds_cstr (&g2str));
  tab_text (table, 3, 1, TAT_TITLE | TAB_CENTER, _("Total"));
  tab_joint_text (table, 1, 0, 3, 0,
		  TAT_TITLE | TAB_CENTER, _("N"));
  tab_vline (table, TAL_2, 4, 0, tab_nr (table) - 1);

  tab_text (table, 4, 1, TAT_TITLE | TAB_CENTER, ds_cstr (&g1str));
  tab_text (table, 5, 1, TAT_TITLE | TAB_CENTER, ds_cstr (&g2str));
  tab_joint_text (table, 4, 0, 5, 0,
		  TAT_TITLE | TAB_CENTER, _("Mean Rank"));
  tab_vline (table, TAL_2, 6, 0, tab_nr (table) - 1);

  tab_text (table, 6, 1, TAT_TITLE | TAB_CENTER, ds_cstr (&g1str));
  tab_text (table, 7, 1, TAT_TITLE | TAB_CENTER, ds_cstr (&g2str));
  tab_joint_text (table, 6, 0, 7, 0,
		  TAT_TITLE | TAB_CENTER, _("Sum of Ranks"));

  ds_destroy (&g1str);
  ds_destroy (&g2str);

  for (i = 0 ; i < nst->n_vars ; ++i)
    {
      const struct mw *mw = &mwv[i];
      tab_text (table, 0, column_headers + i, TAT_TITLE,
		var_to_string (nst->vars[i]));

      tab_double (table, 1, column_headers + i, 0,
		  mw->n[0], NULL, RC_OTHER);

      tab_double (table, 2, column_headers + i, 0,
		  mw->n[1], NULL, RC_OTHER);

      tab_double (table, 3, column_headers + i, 0,
		  mw->n[1] + mw->n[0], NULL, RC_OTHER);

      /* Mean Ranks */
      tab_double (table, 4, column_headers + i, 0,
		  mw->rank_sum[0] / mw->n[0], NULL, RC_OTHER);

      tab_double (table, 5, column_headers + i, 0,
		  mw->rank_sum[1] / mw->n[1], NULL, RC_OTHER);

      /* Sum of Ranks */
      tab_double (table, 6, column_headers + i, 0,
		  mw->rank_sum[0], NULL, RC_OTHER);

      tab_double (table, 7, column_headers + i, 0,
		  mw->rank_sum[1], NULL, RC_OTHER);
    }

  tab_submit (table);
}

static void
show_statistics_box (const struct n_sample_test *nst, const struct mw *mwv, bool exact)
{
  int i;
  const int row_headers = 1;
  const int column_headers = 1;
  struct tab_table *table =
    tab_create (row_headers + (exact ? 6 : 4), column_headers + nst->n_vars);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Test Statistics"));

  /* Vertical lines inside the box */
  tab_box (table, 1, 0, -1, TAL_1,
	   row_headers, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  /* Box around the table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );

  tab_hline (table, TAL_2, 0, tab_nc (table) -1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);

  tab_text (table, 1, 0, TAT_TITLE | TAB_CENTER, _("Mann-Whitney U"));
  tab_text (table, 2, 0, TAT_TITLE | TAB_CENTER, _("Wilcoxon W"));
  tab_text (table, 3, 0, TAT_TITLE | TAB_CENTER, _("Z"));
  tab_text (table, 4, 0, TAT_TITLE | TAB_CENTER, _("Asymp. Sig. (2-tailed)"));

  if (exact) 
    {
      tab_text (table, 5, 0, TAT_TITLE | TAB_CENTER, _("Exact Sig. (2-tailed)"));
      tab_text (table, 6, 0, TAT_TITLE | TAB_CENTER, _("Point Probability"));
    }

  for (i = 0 ; i < nst->n_vars ; ++i)
    {
      const struct mw *mw = &mwv[i];

      tab_text (table, 0, column_headers + i, TAT_TITLE,
		var_to_string (nst->vars[i]));

      tab_double (table, 1, column_headers + i, 0,
		  mw->u, NULL, RC_OTHER);

      tab_double (table, 2, column_headers + i, 0,
		  mw->w, NULL, RC_OTHER);

      tab_double (table, 3, column_headers + i, 0,
		  mw->z, NULL, RC_OTHER);

      tab_double (table, 4, column_headers + i, 0,
		  2.0 * gsl_cdf_ugaussian_P (mw->z), NULL, RC_PVALUE);
    }

  tab_submit (table);
}
