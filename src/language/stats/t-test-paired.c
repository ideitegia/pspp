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

#include <math.h>
#include <gsl/gsl_cdf.h>

#include "t-test.h"

#include "math/moments.h"
#include "math/correlation.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "libpspp/hmapx.h"
#include "libpspp/hash-functions.h"

#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


struct pair_stats
{
  int posn;
  double sum_of_prod;
  struct moments *mom0;
  const struct variable *var0;

  struct moments *mom1;
  const struct variable *var1;

  struct moments *mom_diff;
};

struct paired_samp
{
  struct hmapx hmap;
};

static void paired_summary (const struct tt *tt, struct paired_samp *os);
static void paired_correlations (const struct tt *tt, struct paired_samp *os);
static void paired_test (const struct tt *tt, const struct paired_samp *os);

void
paired_run (const struct tt *tt, size_t n_pairs, vp *pairs, struct casereader *reader)
{
  int i;
  struct ccase *c;
  struct paired_samp ps;
  struct casereader *r;
  struct hmapx_node *node;
  struct pair_stats *pp = NULL;

  hmapx_init (&ps.hmap);

  for (i = 0; i < n_pairs; ++i)
    {
      vp *pair = &pairs[i];
      unsigned int hash;
      struct pair_stats *pp = xzalloc (sizeof *pp);
      pp->posn = i;
      pp->var0 = (*pair)[0];
      pp->var1 = (*pair)[1];
      pp->mom0 = moments_create (MOMENT_VARIANCE);
      pp->mom1 = moments_create (MOMENT_VARIANCE);
      pp->mom_diff = moments_create (MOMENT_VARIANCE);

      hash = hash_pointer ((*pair)[0], 0);
      hash = hash_pointer ((*pair)[1], hash);

      hmapx_insert (&ps.hmap, pp, hash);
    }

  r = casereader_clone (reader);
  for ( ; (c = casereader_read (r) ); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);

      struct hmapx_node *node;
      struct pair_stats *pp = NULL;
      HMAPX_FOR_EACH (pp, node, &ps.hmap)
	{
	  const union value *val0 = case_data (c, pp->var0);
	  const union value *val1 = case_data (c, pp->var1);
          if (var_is_value_missing (pp->var0, val0, tt->exclude))
	    continue;

          if (var_is_value_missing (pp->var1, val1, tt->exclude))
	    continue;

	  moments_pass_one (pp->mom0, val0->f, w);
	  moments_pass_one (pp->mom1, val1->f, w);
	  moments_pass_one (pp->mom_diff, val0->f - val1->f, w);
	}
    }
  casereader_destroy (r);

  r = reader;
  for ( ; (c = casereader_read (r) ); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);

      struct hmapx_node *node;
      struct pair_stats *pp = NULL;
      HMAPX_FOR_EACH (pp, node, &ps.hmap)
	{
	  const union value *val0 = case_data (c, pp->var0);
	  const union value *val1 = case_data (c, pp->var1);
          if (var_is_value_missing (pp->var0, val0, tt->exclude))
	    continue;

          if (var_is_value_missing (pp->var1, val1, tt->exclude))
	    continue;

	  moments_pass_two (pp->mom0, val0->f, w);
	  moments_pass_two (pp->mom1, val1->f, w);
	  moments_pass_two (pp->mom_diff, val0->f - val1->f, w);
	  pp->sum_of_prod += val0->f * val1->f;
	}
    }
  casereader_destroy (r);

  paired_summary (tt, &ps);
  paired_correlations (tt, &ps);
  paired_test (tt, &ps);

  /* Clean up */
  HMAPX_FOR_EACH (pp, node, &ps.hmap)
    {
      moments_destroy (pp->mom0);
      moments_destroy (pp->mom1);
      moments_destroy (pp->mom_diff);
      free (pp);
    }

  hmapx_destroy (&ps.hmap);
}

static void
paired_summary (const struct tt *tt, struct paired_samp *os)
{
  size_t n_pairs = hmapx_count (&os->hmap);
  struct hmapx_node *node;
  struct pair_stats *pp = NULL;

  const int heading_rows = 1;
  const int heading_cols = 2;

  const int cols = 4 + heading_cols;
  const int rows = n_pairs * 2 + heading_rows;
  struct tab_table *t = tab_create (cols, rows);
  const struct fmt_spec *wfmt = tt->wv ? var_get_print_format (tt->wv) : & F_8_0;
  tab_set_format (t, RC_WEIGHT, wfmt);
  tab_headers (t, 0, 0, heading_rows, 0);
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_0, 0, 0, cols - 1, rows - 1);
  tab_box (t, -1, -1,       TAL_0, TAL_1, heading_cols, 0, cols - 1, rows - 1);

  tab_hline (t, TAL_2, 0, cols - 1, 1);

  tab_title (t, _("Paired Sample Statistics"));
  tab_vline (t, TAL_2, heading_cols, 0, rows - 1);
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (t, 5, 0, TAB_CENTER | TAT_TITLE, _("S.E. Mean"));

  HMAPX_FOR_EACH (pp, node, &os->hmap)
    {
      int v = pp->posn;
      double cc, mean, sigma;

      tab_text_format (t, 0, v * 2 + heading_rows, TAB_LEFT, _("Pair %d"), pp->posn + 1);

      /* first var */
      moments_calculate (pp->mom0, &cc, &mean, &sigma, NULL, NULL);
      tab_text (t, 1, v * 2 + heading_rows, TAB_LEFT, var_to_string (pp->var0));
      tab_double (t, 3, v * 2 + heading_rows, TAB_RIGHT, cc, NULL, RC_WEIGHT);
      tab_double (t, 2, v * 2 + heading_rows, TAB_RIGHT, mean, NULL, RC_OTHER);
      tab_double (t, 4, v * 2 + heading_rows, TAB_RIGHT, sqrt (sigma), NULL, RC_OTHER);
      tab_double (t, 5, v * 2 + heading_rows, TAB_RIGHT, sqrt (sigma / cc), NULL, RC_OTHER);

      /* second var */
      moments_calculate (pp->mom1, &cc, &mean, &sigma, NULL, NULL);
      tab_text (t, 1, v * 2 + 1 + heading_rows, TAB_LEFT, var_to_string (pp->var1));      
      tab_double (t, 3, v * 2 + 1 + heading_rows, TAB_RIGHT, cc, NULL, RC_WEIGHT);
      tab_double (t, 2, v * 2 + 1 + heading_rows, TAB_RIGHT, mean, NULL, RC_OTHER);
      tab_double (t, 4, v * 2 + 1 + heading_rows, TAB_RIGHT, sqrt (sigma), NULL, RC_OTHER);
      tab_double (t, 5, v * 2 + 1 + heading_rows, TAB_RIGHT, sqrt (sigma / cc), NULL, RC_OTHER);
    }

  tab_submit (t);
}


static void
paired_correlations (const struct tt *tt, struct paired_samp *os)
{
  size_t n_pairs = hmapx_count (&os->hmap);
  struct hmapx_node *node;
  struct pair_stats *pp = NULL;
  const int heading_rows = 1;
  const int heading_cols = 2;

  const int cols = 5;
  const int rows = n_pairs + heading_rows;
  struct tab_table *t = tab_create (cols, rows);
  const struct fmt_spec *wfmt = tt->wv ? var_get_print_format (tt->wv) : & F_8_0;
  tab_set_format (t, RC_WEIGHT, wfmt);
  tab_headers (t, 0, 0, heading_rows, 0);
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, cols - 1, rows - 1);

  tab_hline (t, TAL_2, 0, cols - 1, 1);

  tab_title (t, _("Paired Samples Correlations"));
  tab_vline (t, TAL_2, heading_cols, 0, rows - 1);
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Correlation"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("Sig."));

  HMAPX_FOR_EACH (pp, node, &os->hmap)
    {
      double corr;
      double cc0, mean0, sigma0;
      double cc1, mean1, sigma1;
      int v = pp->posn;

      tab_text_format (t, 0, v + heading_rows, TAB_LEFT, _("Pair %d"), pp->posn + 1);

      tab_text_format (t, 1, v + heading_rows, TAB_LEFT, _("%s & %s"), 
		       var_to_string (pp->var0),
		       var_to_string (pp->var1));

      moments_calculate (pp->mom0, &cc0, &mean0, &sigma0, NULL, NULL);
      moments_calculate (pp->mom1, &cc1, &mean1, &sigma1, NULL, NULL);

      /* If this fails, then we're not dealing with missing values properly */
      assert (cc0 == cc1);

      tab_double (t, 2, v + heading_rows, TAB_RIGHT, cc0, NULL, RC_WEIGHT);

      corr = pp->sum_of_prod / cc0  - (mean0 * mean1);
      corr /= sqrt (sigma0 * sigma1);
      corr *= cc0 / (cc0 - 1);

      tab_double (t, 3, v + heading_rows, TAB_RIGHT, corr, NULL, RC_OTHER);
      tab_double (t, 4, v + heading_rows, TAB_RIGHT, 
		  2.0 * significance_of_correlation (corr, cc0), NULL, RC_OTHER);
    }

  tab_submit (t);
}


static void
paired_test (const struct tt *tt, const struct paired_samp *os)
{
  size_t n_pairs = hmapx_count (&os->hmap);
  struct hmapx_node *node;
  struct pair_stats *pp = NULL;

  const int heading_rows = 3;
  const int heading_cols = 2;
  const size_t rows = heading_rows + n_pairs;
  const size_t cols = 10;
  const struct fmt_spec *wfmt = tt->wv ? var_get_print_format (tt->wv) : & F_8_0;

  struct tab_table *t = tab_create (cols, rows);
  tab_set_format (t, RC_WEIGHT, wfmt);
  tab_headers (t, 0, 0, heading_rows, 0);
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_0, 0, 0, cols - 1, rows - 1);
  tab_hline (t, TAL_2, 0, cols - 1, 3);

  tab_title (t, _("Paired Samples Test"));
  tab_hline (t, TAL_1, heading_cols, 6, 1);
  tab_vline (t, TAL_2, heading_cols, 0, rows - 1);

  tab_box (t, -1, -1, -1, TAL_1, heading_cols, 0, cols - 1, rows - 1);

  tab_joint_text (t, 2, 0, 6, 0, TAB_CENTER,
		  _("Paired Differences"));

  tab_joint_text_format (t, 5, 1, 6, 1, TAB_CENTER,
                         _("%g%% Confidence Interval of the Difference"),
                         tt->confidence * 100.0);

  tab_vline (t, TAL_GAP, 6, 1, 1);
  tab_hline (t, TAL_1, 5, 6, 2);
  tab_text (t, 7, 2, TAB_CENTER | TAT_TITLE, _("t"));
  tab_text (t, 8, 2, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (t, 9, 2, TAB_CENTER | TAT_TITLE, _("Sig. (2-tailed)"));
  tab_text (t, 4, 2, TAB_CENTER | TAT_TITLE, _("Std. Error Mean"));
  tab_text (t, 3, 2, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (t, 2, 2, TAB_CENTER | TAT_TITLE, _("Mean"));

  tab_text (t, 5, 2, TAB_CENTER | TAT_TITLE, _("Lower"));
  tab_text (t, 6, 2, TAB_CENTER | TAT_TITLE, _("Upper"));

  HMAPX_FOR_EACH (pp, node, &os->hmap)
    {
      int v = pp->posn;
      double cc, mean, sigma;
      double df ;
      double tval;
      double p, q;
      double se_mean;

      moments_calculate (pp->mom_diff, &cc, &mean, &sigma, NULL, NULL);

      df = cc - 1.0;
      tab_text_format (t, 0, v + heading_rows, TAB_LEFT, _("Pair %d"), v + 1);

      tab_text_format (t, 1, v + heading_rows, TAB_LEFT, _("%s - %s"), 
		       var_to_string (pp->var0),
		       var_to_string (pp->var1));

      tval = mean * sqrt (cc / sigma);
      se_mean = sqrt (sigma / cc);

      tab_double (t, 2, v + heading_rows, TAB_RIGHT, mean, NULL, RC_OTHER);
      tab_double (t, 3, v + heading_rows, TAB_RIGHT, sqrt (sigma), NULL, RC_OTHER);
      tab_double (t, 4, v + heading_rows, TAB_RIGHT, se_mean, NULL, RC_OTHER);

      tab_double (t, 7, v + heading_rows, TAB_RIGHT, tval, NULL, RC_OTHER);
      tab_double (t, 8, v + heading_rows, TAB_RIGHT, df, NULL, RC_WEIGHT);


      p = gsl_cdf_tdist_P (tval, df);
      q = gsl_cdf_tdist_Q (tval, df);

      tab_double (t, 9, v + heading_rows, TAB_RIGHT, 2.0 * (tval > 0 ? q : p), NULL, RC_PVALUE);

      tval = gsl_cdf_tdist_Qinv ( (1.0 - tt->confidence) / 2.0, df);

      tab_double (t, 5, v + heading_rows, TAB_RIGHT, mean - tval * se_mean, NULL, RC_OTHER);
      tab_double (t, 6, v + heading_rows, TAB_RIGHT, mean + tval * se_mean, NULL, RC_OTHER);
    }

  tab_submit (t);
}
