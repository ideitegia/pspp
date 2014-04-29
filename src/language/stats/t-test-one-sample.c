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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>


#include "t-test.h"

#include <math.h>
#include <gsl/gsl_cdf.h>

#include "data/variable.h"
#include "data/format.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmapx.h"
#include "math/moments.h"

#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


struct per_var_stats
{
  const struct variable *var;

  /* The position for reporting purposes */
  int posn;

  /* N, Mean, Variance */
  struct moments *mom;

  /* Sum of the differences */
  double sum_diff;
};


struct one_samp
{
  struct hmapx hmap;
  double testval;
};


static void
one_sample_test (const struct tt *tt, const struct one_samp *os)
{
  struct hmapx_node *node;
  struct per_var_stats *per_var_stats;

  const int heading_rows = 3;
  const size_t rows = heading_rows + tt->n_vars;
  const size_t cols = 7;
  const struct fmt_spec *wfmt = tt->wv ? var_get_print_format (tt->wv) : & F_8_0;

  struct tab_table *t = tab_create (cols, rows);
  tab_set_format (t, RC_WEIGHT, wfmt);

  tab_headers (t, 0, 0, heading_rows, 0);
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_0, 0, 0, cols - 1, rows - 1);
  tab_hline (t, TAL_2, 0, cols - 1, 3);

  tab_title (t, _("One-Sample Test"));
  tab_hline (t, TAL_1, 1, cols - 1, 1);
  tab_vline (t, TAL_2, 1, 0, rows - 1);

  tab_joint_text_format (t, 1, 0, cols - 1, 0, TAB_CENTER,
                         _("Test Value = %f"), os->testval);

  tab_box (t, -1, -1, -1, TAL_1, 1, 1, cols - 1, rows - 1);

  tab_joint_text_format (t, 5, 1, 6, 1, TAB_CENTER,
                         _("%g%% Confidence Interval of the Difference"),
                         tt->confidence * 100.0);

  tab_vline (t, TAL_GAP, 6, 1, 1);
  tab_hline (t, TAL_1, 5, 6, 2);
  tab_text (t, 1, 2, TAB_CENTER | TAT_TITLE, _("t"));
  tab_text (t, 2, 2, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (t, 3, 2, TAB_CENTER | TAT_TITLE, _("Sig. (2-tailed)"));
  tab_text (t, 4, 2, TAB_CENTER | TAT_TITLE, _("Mean Difference"));
  tab_text (t, 5, 2, TAB_CENTER | TAT_TITLE, _("Lower"));
  tab_text (t, 6, 2, TAB_CENTER | TAT_TITLE, _("Upper"));

  HMAPX_FOR_EACH (per_var_stats, node, &os->hmap)
    {
      const struct moments *m = per_var_stats->mom;
      double cc, mean, sigma;
      double tval, df;
      double p, q;
      double mean_diff;
      double se_mean ;
      const int v = per_var_stats->posn;

      moments_calculate (m, &cc, &mean, &sigma, NULL, NULL);

      tval = (mean - os->testval) * sqrt (cc / sigma);

      mean_diff = per_var_stats->sum_diff / cc;
      se_mean = sqrt (sigma / cc);
      df = cc - 1.0;
      p = gsl_cdf_tdist_P (tval, df);
      q = gsl_cdf_tdist_Q (tval, df);

      tab_text (t, 0, v + heading_rows, TAB_LEFT, var_to_string (per_var_stats->var));
      tab_double (t, 1, v + heading_rows, TAB_RIGHT, tval, NULL, RC_OTHER);
      tab_double (t, 2, v + heading_rows, TAB_RIGHT, df, NULL, RC_WEIGHT);

      /* Multiply by 2 to get 2-tailed significance, makeing sure we've got
	 the correct tail*/
      tab_double (t, 3, v + heading_rows, TAB_RIGHT, 2.0 * (tval > 0 ? q : p), NULL, RC_PVALUE);

      tab_double (t, 4, v + heading_rows, TAB_RIGHT, mean_diff,  NULL, RC_OTHER);

      tval = gsl_cdf_tdist_Qinv ( (1.0 - tt->confidence) / 2.0, df);

      tab_double (t, 5, v + heading_rows, TAB_RIGHT, mean_diff - tval * se_mean, NULL, RC_OTHER);
      tab_double (t, 6, v + heading_rows, TAB_RIGHT, mean_diff + tval * se_mean, NULL, RC_OTHER);
    }

  tab_submit (t);
}

static void
one_sample_summary (const struct tt *tt, const struct one_samp *os)
{
  struct hmapx_node *node;
  struct per_var_stats *per_var_stats;

  const int cols = 5;
  const int heading_rows = 1;
  const int rows = tt->n_vars + heading_rows;
  struct tab_table *t = tab_create (cols, rows);
  const struct fmt_spec *wfmt = tt->wv ? var_get_print_format (tt->wv) : & F_8_0;
  tab_set_format (t, RC_WEIGHT, wfmt);
  tab_headers (t, 0, 0, heading_rows, 0);
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, cols - 1, rows - 1);
  tab_hline (t, TAL_2, 0, cols - 1, 1);

  tab_title (t, _("One-Sample Statistics"));
  tab_vline (t, TAL_2, 1, 0, rows - 1);
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("S.E. Mean"));

  HMAPX_FOR_EACH (per_var_stats, node, &os->hmap)
    {
      const struct moments *m = per_var_stats->mom;
      const int v = per_var_stats->posn;
      double cc, mean, sigma;
      moments_calculate (m, &cc, &mean, &sigma, NULL, NULL);

      tab_text (t, 0, v + heading_rows, TAB_LEFT, var_to_string (per_var_stats->var));
      tab_double (t, 1, v + heading_rows, TAB_RIGHT, cc, NULL, RC_WEIGHT);
      tab_double (t, 2, v + heading_rows, TAB_RIGHT, mean, NULL, RC_OTHER);
      tab_double (t, 3, v + heading_rows, TAB_RIGHT, sqrt (sigma), NULL, RC_OTHER);
      tab_double (t, 4, v + heading_rows, TAB_RIGHT, sqrt (sigma / cc), NULL, RC_OTHER);
    }

  tab_submit (t);
}

void
one_sample_run (const struct tt *tt, double testval, struct casereader *reader)
{
  int i;
  struct ccase *c;
  struct one_samp os;
  struct casereader *r;
  struct hmapx_node *node;
  struct per_var_stats *per_var_stats;

  os.testval = testval;
  hmapx_init (&os.hmap);

  /* Insert all the variables into the map */
  for (i = 0; i < tt->n_vars; ++i)
    {
      struct per_var_stats *per_var_stats = xzalloc (sizeof (*per_var_stats));

      per_var_stats->posn = i;
      per_var_stats->var = tt->vars[i];
      per_var_stats->mom = moments_create (MOMENT_VARIANCE);

      hmapx_insert (&os.hmap, per_var_stats, hash_pointer (per_var_stats->var, 0));
    }

  r = casereader_clone (reader);
  for ( ; (c = casereader_read (r) ); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);
      struct hmapx_node *node;
      struct per_var_stats *per_var_stats;
      HMAPX_FOR_EACH (per_var_stats, node, &os.hmap)
	{
	  const struct variable *var = per_var_stats->var;
	  const union value *val = case_data (c, var);
	  if (var_is_value_missing (var, val, tt->exclude))
	    continue;

	  moments_pass_one (per_var_stats->mom, val->f, w);
	}
    }
  casereader_destroy (r);

  r = reader;
  for ( ; (c = casereader_read (r) ); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);
      struct hmapx_node *node;
      struct per_var_stats *per_var_stats;
      HMAPX_FOR_EACH (per_var_stats, node, &os.hmap)
	{
	  const struct variable *var = per_var_stats->var;
	  const union value *val = case_data (c, var);
	  if (var_is_value_missing (var, val, tt->exclude))
	    continue;

	  moments_pass_two (per_var_stats->mom, val->f, w);
	  per_var_stats->sum_diff += w * (val->f - os.testval);
	}
    }
  casereader_destroy (r);

  one_sample_summary (tt, &os);
  one_sample_test (tt, &os);

  HMAPX_FOR_EACH (per_var_stats, node, &os.hmap)
    {
      moments_destroy (per_var_stats->mom);
      free (per_var_stats);
    }

  hmapx_destroy (&os.hmap);
}

