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

#include "language/stats/ks-one-sample.h"

#include <gsl/gsl_cdf.h>
#include <math.h>
#include <stdlib.h>


#include "math/sort.h"
#include "data/case.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/stats/freq.h"
#include "language/stats/npar.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "output/tab.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


/* The per test variable statistics */
struct ks
{
  double obs_cc;

  double test_min ;
  double test_max;
  double mu;
  double sigma;

  double diff_pos;
  double diff_neg;

  double ssq;
  double sum;
};

typedef double theoretical (const struct ks *ks, double x);
typedef theoretical *theoreticalfp;

static double
theoretical_uniform (const struct ks *ks, double x)
{
  return gsl_cdf_flat_P (x, ks->test_min, ks->test_max);
}

static double
theoretical_normal (const struct ks *ks, double x)
{
  return gsl_cdf_gaussian_P (x - ks->mu, ks->sigma);
}

static double
theoretical_poisson (const struct ks *ks, double x)
{
  return gsl_cdf_poisson_P (x, ks->mu);
}

static double
theoretical_exponential (const struct ks *ks, double x)
{
  return gsl_cdf_exponential_P (x, 1/ks->mu);
}


static const  theoreticalfp theoreticalf[4] = 
{
  theoretical_normal,
  theoretical_uniform,
  theoretical_poisson,
  theoretical_exponential
};

/* 
   Return the assymptotic approximation to the significance of Z
 */
static double
ks_asymp_sig (double z)
{
  if (z < 0.27)
    return 1;
  
  if (z >= 3.1)
    return 0;

  if (z < 1)
    {
      double q = exp (-1.233701 * pow (z, -2));
      return 1 - 2.506628 * (q + pow (q, 9) + pow (q, 25))/ z ;
    }
  else
    {
      double q = exp (-2 * z * z);
      return 2 * (q - pow (q, 4) + pow (q, 9) - pow (q, 16))/ z ;
    }
}

static void show_results (const struct ks *, const struct ks_one_sample_test *,  const struct fmt_spec *);


void
ks_one_sample_execute (const struct dataset *ds,
		       struct casereader *input,
		       enum mv_class exclude,
		       const struct npar_test *test,
		       bool x UNUSED, double y UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);
  const struct ks_one_sample_test *kst = UP_CAST (test, const struct ks_one_sample_test, parent.parent);
  const struct one_sample_test *ost = &kst->parent;
  struct ccase *c;
  const struct variable *wvar = dict_get_weight (dict);
  const struct fmt_spec *wfmt = wvar ? var_get_print_format (wvar) : & F_8_0;
  bool warn = true;
  int v;
  struct casereader *r = casereader_clone (input);

  struct ks *ks = xcalloc (ost->n_vars, sizeof *ks);

  for (v = 0; v < ost->n_vars; ++v)
    {
      ks[v].obs_cc = 0;
      ks[v].test_min = DBL_MAX;
      ks[v].test_max = -DBL_MAX;
      ks[v].diff_pos = -DBL_MAX;
      ks[v].diff_neg = DBL_MAX;
      ks[v].sum = 0;
      ks[v].ssq = 0;
    }

  for (; (c = casereader_read (r)) != NULL; case_unref (c))
    {
      const double weight = dict_get_case_weight (dict, c, &warn);

      for (v = 0; v < ost->n_vars; ++v)
	{
	  const struct variable *var = ost->vars[v];
	  const union value *val = case_data (c, var);
      
	  if (var_is_value_missing (var, val, exclude))
	    continue;

	  minimize (&ks[v].test_min, val->f);
	  maximize (&ks[v].test_max, val->f);

	  ks[v].obs_cc += weight;
	  ks[v].sum += val->f;
	  ks[v].ssq += pow2 (val->f);
	}
    }
  casereader_destroy (r);

  for (v = 0; v < ost->n_vars; ++v)
    {
      const struct variable *var = ost->vars[v];
      double cc = 0;
      double prev_empirical = 0;

      switch (kst->dist)
	{
	case KS_UNIFORM:
	  if (kst->p[0] != SYSMIS)
	    ks[v].test_min = kst->p[0];

	  if (kst->p[1] != SYSMIS)
	    ks[v].test_max = kst->p[1];
	  break;
	case KS_NORMAL:
	  if (kst->p[0] != SYSMIS)
	    ks[v].mu = kst->p[0];
	  else
	    ks[v].mu = ks[v].sum / ks[v].obs_cc;

	  if (kst->p[1] != SYSMIS)
	    ks[v].sigma = kst->p[1];
	  else
	    {
	      ks[v].sigma = ks[v].ssq - pow2 (ks[v].sum) / ks[v].obs_cc;
	      ks[v].sigma /= ks[v].obs_cc - 1;
	      ks[v].sigma = sqrt (ks[v].sigma);
	    }

	  break;
	case KS_POISSON:
	case KS_EXPONENTIAL:
	  if (kst->p[0] != SYSMIS)
	    ks[v].mu = ks[v].sigma = kst->p[0];
	  else 
	    ks[v].mu = ks[v].sigma = ks[v].sum / ks[v].obs_cc;
	  break;
	default:
	  NOT_REACHED ();
	}

      r = sort_execute_1var (casereader_clone (input), var);
      for (; (c = casereader_read (r)) != NULL; case_unref (c))
	{
	  double theoretical, empirical;
	  double d, dp;
	  const double weight = dict_get_case_weight (dict, c, &warn);
	  const union value *val = case_data (c, var);

	  if (var_is_value_missing (var, val, exclude))
	    continue;

	  cc += weight;

	  empirical = cc / ks[v].obs_cc;
      
	  theoretical = theoreticalf[kst->dist] (&ks[v], val->f);
      
	  d = empirical - theoretical;
	  dp = prev_empirical - theoretical;

	  if (d > 0)
	    maximize (&ks[v].diff_pos, d); 
	  else
	    minimize (&ks[v].diff_neg, d);

	  if (dp > 0)
	    maximize (&ks[v].diff_pos, dp); 
	  else
	    minimize (&ks[v].diff_neg, dp);

	  prev_empirical = empirical;
	}

      casereader_destroy (r);
    }

  show_results (ks, kst, wfmt);

  free (ks);
  casereader_destroy (input);
}


static void
show_results (const struct ks *ks,
	      const struct ks_one_sample_test *kst,
	      const struct fmt_spec *wfmt)
{
  int i;
  const int row_headers = 1;
  const int column_headers = 2;
  const int nc = kst->parent.n_vars + column_headers;
  const int nr = 8 + row_headers;
  struct tab_table *table = tab_create (nc, nr);
  tab_set_format (table, RC_WEIGHT, wfmt);
  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("One-Sample Kolmogorov-Smirnov Test"));

  /* Box around the table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0,  0, nc - 1, nr - 1 );

  tab_hline (table, TAL_2, 0, nc - 1, row_headers);

  tab_vline (table, TAL_1, column_headers, 0, nr - 1);

  tab_text (table,  0, 1,
	    TAT_TITLE | TAB_LEFT , _("N"));

  switch (kst->dist)
    {
    case KS_NORMAL:
      tab_text (table,  0, 2,
		TAT_TITLE | TAB_LEFT , _("Normal Parameters"));
      
      tab_text (table,  1, 2,
		TAT_TITLE | TAB_LEFT , _("Mean"));
      tab_text (table,  1, 3,
		TAT_TITLE | TAB_LEFT , _("Std. Deviation"));
      break;
    case KS_UNIFORM:
      tab_text (table,  0, 2,
		TAT_TITLE | TAB_LEFT , _("Uniform Parameters"));
      
      tab_text (table,  1, 2,
		TAT_TITLE | TAB_LEFT , _("Minimum"));
      tab_text (table,  1, 3,
		TAT_TITLE | TAB_LEFT , _("Maximum"));
      break;
    case KS_POISSON:
      tab_text (table,  0, 2,
		TAT_TITLE | TAB_LEFT , _("Poisson Parameters"));
      
      tab_text (table,  1, 2,
		TAT_TITLE | TAB_LEFT , _("Lambda"));
      break;
    case KS_EXPONENTIAL:
      tab_text (table,  0, 2,
		TAT_TITLE | TAB_LEFT , _("Exponential Parameters"));
      
      tab_text (table,  1, 2,
		TAT_TITLE | TAB_LEFT , _("Scale"));
      break;

    default:
      NOT_REACHED ();
    }

  /* The variable columns */
  for (i = 0; i < kst->parent.n_vars; ++i)
    {
      double abs = 0;
      double z = 0;
      const int col = 2 + i;
      tab_text (table, col, 0,
		TAT_TITLE | TAB_CENTER , 
		var_to_string (kst->parent.vars[i]));

      switch (kst->dist)
	{
	case KS_UNIFORM:
	  tab_double (table, col, 1, 0, ks[i].obs_cc, NULL, RC_WEIGHT);
	  tab_double (table, col, 2, 0, ks[i].test_min, NULL, RC_OTHER);
	  tab_double (table, col, 3, 0, ks[i].test_max, NULL, RC_OTHER);
	  break;

	case KS_NORMAL:
	  tab_double (table, col, 1, 0, ks[i].obs_cc, NULL, RC_WEIGHT);
	  tab_double (table, col, 2, 0, ks[i].mu, NULL, RC_OTHER);
	  tab_double (table, col, 3, 0, ks[i].sigma, NULL, RC_OTHER);
	  break;

	case KS_POISSON:
	case KS_EXPONENTIAL:
	  tab_double (table, col, 1, 0, ks[i].obs_cc, NULL, RC_WEIGHT);
	  tab_double (table, col, 2, 0, ks[i].mu, NULL, RC_OTHER);
	  break;

	default:
	  NOT_REACHED ();
	}

      abs = ks[i].diff_pos;
      maximize (&abs, -ks[i].diff_neg);

      z = sqrt (ks[i].obs_cc) * abs;

      tab_double (table, col, 5, 0, ks[i].diff_pos, NULL, RC_OTHER);
      tab_double (table, col, 6, 0, ks[i].diff_neg, NULL, RC_OTHER);

      tab_double (table, col, 4, 0, abs, NULL, RC_OTHER);

      tab_double (table, col, 7, 0, z, NULL, RC_OTHER);
      tab_double (table, col, 8, 0, ks_asymp_sig (z), NULL, RC_PVALUE);
    }


  tab_text (table,  0, 4,
	    TAT_TITLE | TAB_LEFT , _("Most Extreme Differences"));

  tab_text (table,  1, 4,
	    TAT_TITLE | TAB_LEFT , _("Absolute"));

  tab_text (table,  1, 5,
	    TAT_TITLE | TAB_LEFT , _("Positive"));

  tab_text (table,  1, 6,
	    TAT_TITLE | TAB_LEFT , _("Negative"));

  tab_text (table,  0, 7,
	    TAT_TITLE | TAB_LEFT , _("Kolmogorov-Smirnov Z"));

  tab_text (table,  0, 8,
	    TAT_TITLE | TAB_LEFT , _("Asymp. Sig. (2-tailed)"));

  tab_submit (table);
}
