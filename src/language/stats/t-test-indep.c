/* PSPP - a program for statistical analysis.
   Copyright (C) 2011 Free Software Foundation, Inc.

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

#include "t-test.h"

#include <gsl/gsl_cdf.h>
#include <math.h>
#include "libpspp/misc.h"

#include "libpspp/str.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"

#include "math/moments.h"
#include "math/levene.h"

#include <output/tab.h>
#include "gettext.h"
#define _(msgid) gettext (msgid)


struct indep_samples
{
  const struct variable *gvar;
  bool cut;
  const union value *gval0;
  const union value *gval1;
};

struct pair_stats
{
  struct moments *mom[2];
  double lev ;
  struct levene *nl;
};


static void indep_summary (const struct tt *tt, struct indep_samples *is, const struct pair_stats *ps);
static void indep_test (const struct tt *tt, const struct pair_stats *ps);

static int
which_group (const union value *v, const struct indep_samples *is)
{
  int width = var_get_width (is->gvar);
  int cmp = value_compare_3way (v, is->gval0, width);
  if ( is->cut )
    return  (cmp < 0);

  if (cmp == 0)
    return 0;

  if (0 == value_compare_3way (v, is->gval1, width))
    return 1;

  return -1;
}

void
indep_run (struct tt *tt, const struct variable *gvar,
	   bool cut,
	   const union value *gval0, const union value *gval1,
	   struct casereader *reader)
{
  struct indep_samples is;
  struct ccase *c;
  struct casereader *r;

  struct pair_stats *ps = xcalloc (tt->n_vars, sizeof *ps);

  int v;

  for (v = 0; v < tt->n_vars; ++v)
    {
      ps[v].mom[0] = moments_create (MOMENT_VARIANCE);
      ps[v].mom[1] = moments_create (MOMENT_VARIANCE);
      ps[v].nl = levene_create (var_get_width (gvar), cut ? gval0: NULL);
    }

  is.gvar = gvar;
  is.gval0 = gval0;
  is.gval1 = gval1;
  is.cut = cut;

  r = casereader_clone (reader);
  for ( ; (c = casereader_read (r) ); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);

      const union value *gv = case_data (c, gvar);
      
      int grp = which_group (gv, &is);
      if ( grp < 0)
	continue;

      for (v = 0; v < tt->n_vars; ++v)
	{
	  const union value *val = case_data (c, tt->vars[v]);
	  if (var_is_value_missing (tt->vars[v], val, tt->exclude))
	    continue;

	  moments_pass_one (ps[v].mom[grp], val->f, w);
	  levene_pass_one (ps[v].nl, val->f, w, gv);
	}
    }
  casereader_destroy (r);

  r = casereader_clone (reader);
  for ( ; (c = casereader_read (r) ); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);

      const union value *gv = case_data (c, gvar);

      int grp = which_group (gv, &is);
      if ( grp < 0)
	continue;

      for (v = 0; v < tt->n_vars; ++v)
	{
	  const union value *val = case_data (c, tt->vars[v]);
	  if (var_is_value_missing (tt->vars[v], val, tt->exclude))
	    continue;

	  moments_pass_two (ps[v].mom[grp], val->f, w);
	  levene_pass_two (ps[v].nl, val->f, w, gv);
	}
    }
  casereader_destroy (r);

  r = reader;
  for ( ; (c = casereader_read (r) ); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);

      const union value *gv = case_data (c, gvar);

      int grp = which_group (gv, &is);
      if ( grp < 0)
	continue;

      for (v = 0; v < tt->n_vars; ++v)
	{
	  const union value *val = case_data (c, tt->vars[v]);
	  if (var_is_value_missing (tt->vars[v], val, tt->exclude))
	    continue;

	  levene_pass_three (ps[v].nl, val->f, w, gv);
	}
    }
  casereader_destroy (r);


  for (v = 0; v < tt->n_vars; ++v)
    ps[v].lev = levene_calculate (ps[v].nl);
  
  indep_summary (tt, &is, ps);
  indep_test (tt, ps);


  for (v = 0; v < tt->n_vars; ++v)
    {
      moments_destroy (ps[v].mom[0]);
      moments_destroy (ps[v].mom[1]);
      levene_destroy (ps[v].nl);
    }
  free (ps);
}


static void
indep_summary (const struct tt *tt, struct indep_samples *is, const struct pair_stats *ps)
{
  const struct fmt_spec *wfmt = tt->wv ? var_get_print_format (tt->wv) : & F_8_0;

  int v;
  int cols = 6;
  const int heading_rows = 1;
  int rows = tt->n_vars * 2 + heading_rows;

  struct string vallab0 ;
  struct string vallab1 ;
  struct tab_table *t = tab_create (cols, rows);
  tab_set_format (t, RC_WEIGHT, wfmt);
  ds_init_empty (&vallab0);
  ds_init_empty (&vallab1);

  tab_headers (t, 0, 0, 1, 0);
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, cols - 1, rows - 1);
  tab_hline (t, TAL_2, 0, cols - 1, 1);

  tab_vline (t, TAL_GAP, 1, 0, rows - 1);
  tab_title (t, _("Group Statistics"));
  tab_text  (t, 1, 0, TAB_CENTER | TAT_TITLE, var_to_string (is->gvar));
  tab_text  (t, 2, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text  (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text  (t, 4, 0, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text  (t, 5, 0, TAB_CENTER | TAT_TITLE, _("S.E. Mean"));

  if (is->cut)
    {
      ds_put_cstr (&vallab0, "≥");
      ds_put_cstr (&vallab1, "<");

      var_append_value_name (is->gvar, is->gval0, &vallab0);
      var_append_value_name (is->gvar, is->gval0, &vallab1);
    }
  else
    {
      var_append_value_name (is->gvar, is->gval0, &vallab0);
      var_append_value_name (is->gvar, is->gval1, &vallab1);
    }

  tab_vline (t, TAL_1, 1, heading_rows,  rows - 1);

  for (v = 0; v < tt->n_vars; ++v)
    {
      int i;
      const struct variable *var = tt->vars[v];

      tab_text (t, 0, v * 2 + heading_rows, TAB_LEFT,
                var_to_string (var));

      tab_text (t, 1, v * 2 + heading_rows, TAB_LEFT,
                       ds_cstr (&vallab0));

      tab_text (t, 1, v * 2 + 1 + heading_rows, TAB_LEFT,
                       ds_cstr (&vallab1));

      for (i = 0 ; i < 2; ++i)
	{
	  double cc, mean, sigma;
	  moments_calculate (ps[v].mom[i], &cc, &mean, &sigma, NULL, NULL);
      
	  tab_double (t, 2, v * 2 + i + heading_rows, TAB_RIGHT, cc, NULL, RC_WEIGHT);
	  tab_double (t, 3, v * 2 + i + heading_rows, TAB_RIGHT, mean, NULL, RC_OTHER);
	  tab_double (t, 4, v * 2 + i + heading_rows, TAB_RIGHT, sqrt (sigma), NULL, RC_OTHER);
	  tab_double (t, 5, v * 2 + i + heading_rows, TAB_RIGHT, sqrt (sigma / cc), NULL, RC_OTHER);
	}
    }

  tab_submit (t);

  ds_destroy (&vallab0);
  ds_destroy (&vallab1);
}


static void
indep_test (const struct tt *tt, const struct pair_stats *ps)
{
  int v;
  const int heading_rows = 3;
  const int rows= tt->n_vars * 2 + heading_rows;

  const size_t cols = 11;

  struct tab_table *t = tab_create (cols, rows);
  tab_headers (t, 0, 0, 3, 0);
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_0, 0, 0, cols - 1, rows - 1);
  tab_hline (t, TAL_2, 0, cols - 1, 3);

  tab_title (t, _("Independent Samples Test"));

  tab_hline (t, TAL_1, 2, cols - 1, 1);
  tab_vline (t, TAL_2, 2, 0, rows - 1);
  tab_vline (t, TAL_1, 4, 0, rows - 1);
  tab_box (t, -1, -1, -1, TAL_1, 2, 1, cols - 2, rows - 1);
  tab_hline (t, TAL_1, cols - 2, cols - 1, 2);
  tab_box (t, -1, -1, -1, TAL_1, cols - 2, 2, cols - 1, rows - 1);
  tab_joint_text (t, 2, 0, 3, 0, TAB_CENTER, _("Levene's Test for Equality of Variances"));
  tab_joint_text (t, 4, 0, cols - 1, 0, TAB_CENTER, _("t-test for Equality of Means"));

  tab_text (t, 2, 2, TAB_CENTER | TAT_TITLE, _("F"));
  tab_text (t, 3, 2, TAB_CENTER | TAT_TITLE, _("Sig."));
  tab_text (t, 4, 2, TAB_CENTER | TAT_TITLE, _("t"));
  tab_text (t, 5, 2, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (t, 6, 2, TAB_CENTER | TAT_TITLE, _("Sig. (2-tailed)"));
  tab_text (t, 7, 2, TAB_CENTER | TAT_TITLE, _("Mean Difference"));
  tab_text (t, 8, 2, TAB_CENTER | TAT_TITLE, _("Std. Error Difference"));
  tab_text (t, 9, 2, TAB_CENTER | TAT_TITLE, _("Lower"));
  tab_text (t, 10, 2, TAB_CENTER | TAT_TITLE, _("Upper"));

  tab_joint_text_format (t, 9, 1, 10, 1, TAB_CENTER,
                         _("%g%% Confidence Interval of the Difference"),
                         tt->confidence * 100.0);

  tab_vline (t, TAL_1, 1, heading_rows,  rows - 1);

  for (v = 0; v < tt->n_vars; ++v)
  {
    double df, pooled_variance, mean_diff, tval;
    double se2, std_err_diff;
    double p, q;
    double cc0, mean0, sigma0;
    double cc1, mean1, sigma1;
    moments_calculate (ps[v].mom[0], &cc0, &mean0, &sigma0, NULL, NULL);
    moments_calculate (ps[v].mom[1], &cc1, &mean1, &sigma1, NULL, NULL);

    tab_text (t, 0, v * 2 + heading_rows, TAB_LEFT, var_to_string (tt->vars[v]));
    tab_text (t, 1, v * 2 + heading_rows, TAB_LEFT, _("Equal variances assumed"));

    df = cc0 + cc1 - 2.0;
    tab_double (t, 5, v * 2 + heading_rows, TAB_RIGHT, df, NULL, RC_OTHER);
    
    pooled_variance = ((cc0 - 1)* sigma0 + (cc1 - 1) * sigma1) / df ;

    tval = (mean0 - mean1) / sqrt (pooled_variance);
    tval /= sqrt ((cc0 + cc1) / (cc0 * cc1));

    tab_double (t, 4, v * 2 + heading_rows, TAB_RIGHT, tval, NULL, RC_OTHER);

    p = gsl_cdf_tdist_P (tval, df);
    q = gsl_cdf_tdist_Q (tval, df);

    mean_diff = mean0 - mean1;

    tab_double (t, 6, v * 2 + heading_rows, TAB_RIGHT, 2.0 * (tval > 0 ? q : p),   NULL, RC_PVALUE);
    tab_double (t, 7, v * 2 + heading_rows, TAB_RIGHT, mean_diff, NULL, RC_OTHER);

    std_err_diff = sqrt (pooled_variance * (1.0/cc0 + 1.0/cc1));
    tab_double (t, 8, v * 2 + heading_rows, TAB_RIGHT, std_err_diff, NULL, RC_OTHER);


    /* Now work out the confidence interval */
    q = (1 - tt->confidence)/2.0;  /* 2-tailed test */

    tval = gsl_cdf_tdist_Qinv (q, df);
    tab_double (t,  9, v * 2 + heading_rows, TAB_RIGHT, mean_diff - tval * std_err_diff, NULL, RC_OTHER);
    tab_double (t, 10, v * 2 + heading_rows, TAB_RIGHT, mean_diff + tval * std_err_diff, NULL, RC_OTHER);

    /* Equal variances not assumed */
    tab_text (t, 1, v * 2 + heading_rows + 1,  TAB_LEFT, _("Equal variances not assumed"));
    std_err_diff = sqrt ((sigma0 / cc0) + (sigma1 / cc1));

    se2 = sigma0 / cc0 + sigma1 / cc1;
    tval = mean_diff / sqrt (se2);
    tab_double (t, 4, v * 2 + heading_rows + 1, TAB_RIGHT, tval, NULL, RC_OTHER);

    {
      double p, q;
      const double s0 = sigma0 / (cc0);
      const double s1 = sigma1 / (cc1);
      double df = pow2 (s0 + s1) ;
      df /= pow2 (s0) / (cc0 - 1) + pow2 (s1) / (cc1 - 1);

      tab_double (t, 5, v * 2 + heading_rows + 1, TAB_RIGHT, df, NULL, RC_OTHER);

      p = gsl_cdf_tdist_P (tval, df);
      q = gsl_cdf_tdist_Q (tval, df);

      tab_double (t, 6, v * 2 + heading_rows + 1, TAB_RIGHT, 2.0 * (tval > 0 ? q : p), NULL, RC_PVALUE);

      /* Now work out the confidence interval */
      q = (1 - tt->confidence) / 2.0;  /* 2-tailed test */

      tval = gsl_cdf_tdist_Qinv (q, df);
    }

    tab_double (t, 7, v * 2 + heading_rows + 1, TAB_RIGHT, mean_diff, NULL, RC_OTHER);
    tab_double (t, 8, v * 2 + heading_rows + 1, TAB_RIGHT, std_err_diff, NULL, RC_OTHER);
    tab_double (t, 9, v * 2 + heading_rows + 1, TAB_RIGHT,  mean_diff - tval * std_err_diff, NULL, RC_OTHER);
    tab_double (t, 10, v * 2 + heading_rows + 1, TAB_RIGHT, mean_diff + tval * std_err_diff, NULL, RC_OTHER);

    tab_double (t, 2, v * 2 + heading_rows, TAB_CENTER, ps[v].lev, NULL, RC_OTHER);


    {
      /* Now work out the significance of the Levene test */
      double df1 = 1;
      double df2 = cc0 + cc1 - 2;
      double q = gsl_cdf_fdist_Q (ps[v].lev, df1, df2);
      tab_double (t, 3, v * 2 + heading_rows, TAB_CENTER, q, NULL, RC_PVALUE);
    }
  }

  tab_submit (t);
}
