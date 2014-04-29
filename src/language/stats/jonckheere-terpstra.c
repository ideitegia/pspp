/* Pspp - a program for statistical analysis.
   Copyright (C) 2012 Free Software Foundation, Inc.

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

#include "jonckheere-terpstra.h"

#include <gsl/gsl_cdf.h>
#include <math.h>

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/subcase.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/hmap.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/sort.h"
#include "output/tab.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

/* Returns true iff the independent variable lies in the
   between val1 and val2. Regardless of which is the greater value.
*/
static bool
include_func_bi (const struct ccase *c, void *aux)
{
  const struct n_sample_test *nst = aux;
  const union value *bigger = NULL;
  const union value *smaller = NULL;
  
  if (0 > value_compare_3way (&nst->val1, &nst->val2, var_get_width (nst->indep_var)))
    {
      bigger = &nst->val2;
      smaller = &nst->val1;
    }
  else
    {
      smaller = &nst->val2;
      bigger = &nst->val1;
    }
  
  if (0 < value_compare_3way (smaller, case_data (c, nst->indep_var), var_get_width (nst->indep_var)))
    return false;

  if (0 > value_compare_3way (bigger, case_data (c, nst->indep_var), var_get_width (nst->indep_var)))
    return false;

  return true;
}

struct group_data
{
  /* The total of the caseweights in the group */
  double cc;
  
  /* A casereader containing the group data.
     This casereader contains just two values:
     0: The raw value of the data
     1: The cumulative caseweight
   */
  struct casereader *reader;
};


static double
u (const struct group_data *grp0, const struct group_data *grp1)
{
  struct ccase *c0;

  struct casereader *r0 = casereader_clone (grp0->reader);
  double usum = 0;
  double prev_cc0 = 0.0;
  for (; (c0 = casereader_read (r0)); case_unref (c0))
    {
      struct ccase *c1;
      struct casereader *r1 = casereader_clone (grp1->reader);
      double x0 = case_data_idx (c0, 0)->f;
      double cc0 = case_data_idx (c0, 1)->f;
      double w0 = cc0 - prev_cc0;

      double prev_cc1 = 0;

      for (; (c1 = casereader_read (r1)); case_unref (c1))
        {
          double x1 = case_data_idx (c1, 0)->f;
          double cc1 = case_data_idx (c1, 1)->f;
     
          if (x0 > x1)
            {
              /* Do nothing */
            }
          else if (x0 < x1)
            {
              usum += w0 * (grp1->cc - prev_cc1);
	      case_unref (c1);
              break;
            }
          else
            {
#if 1
              usum += w0 * ( (grp1->cc - prev_cc1) / 2.0);
#else
              usum += w0 * (grp1->cc - (prev_cc1 + cc1) / 2.0);
#endif
	      case_unref (c1);
              break;
            }

          prev_cc1 = cc1;
        }
      casereader_destroy (r1);
      prev_cc0 = cc0;
    }
  casereader_destroy (r0);

  return usum;
}


typedef double func_f (double e_l);

/* 
   These 3 functions are used repeatedly in the calculation of the 
   variance of the JT statistic.
   Having them explicitly defined makes the variance calculation 
   a lot simpler.
*/
static  double 
ff1 (double e)
{
  return e * (e - 1) * (2*e + 5);
}

static  double 
ff2 (double e)
{
  return e * (e - 1) * (e - 2);
}

static  double 
ff3 (double e)
{
  return e * (e - 1) ;
}

static  func_f *mff[3] = 
  {
    ff1, ff2, ff3
  };


/*
  This function does the following:
  It creates an ordered set of *distinct* values from IR.
  For each case in that set, it calls f[0..N] passing it the caseweight.
  It returns the sum of f[j] in result[j].

  result and f must be allocated prior to calling this function.
 */
static
void variance_calculation (struct casereader *ir, const struct variable *var,
                           const struct dictionary *dict, 
                           func_f **f, double *result, size_t n)
{
  int i;
  struct casereader *r = casereader_clone (ir);
  struct ccase *c;
  const struct variable *wv = dict_get_weight (dict);
  const int w_idx = wv ?
    var_get_case_index (wv) :
    caseproto_get_n_widths (casereader_get_proto (r)) ;

  r = sort_execute_1var (r, var);

  r = casereader_create_distinct (r, var, dict_get_weight (dict));
  
  for (; (c = casereader_read (r)); case_unref (c))
    {
      double w = case_data_idx (c, w_idx)->f;

      for (i = 0; i < n; ++i)
        result[i] += f[i] (w);
    }

  casereader_destroy (r);
}

struct jt
{
  int levels;
  double n;
  double obs;
  double mean;
  double stddev;
};

static void show_jt (const struct n_sample_test *nst, const struct jt *jt, const struct variable *wv);


void
jonckheere_terpstra_execute (const struct dataset *ds,
			struct casereader *input,
			enum mv_class exclude,
			const struct npar_test *test,
			bool exact UNUSED,
			double timer UNUSED)
{
  int v;
  bool warn = true;
  const struct dictionary *dict = dataset_dict (ds);
  const struct n_sample_test *nst = UP_CAST (test, const struct n_sample_test, parent);
  
  struct caseproto *proto = caseproto_create ();
  proto = caseproto_add_width (proto, 0);
  proto = caseproto_add_width (proto, 0);

  /* If the independent variable is missing, then we ignore the case */
  input = casereader_create_filter_missing (input, 
					    &nst->indep_var, 1,
					    exclude,
					    NULL, NULL);

  /* Remove cases with invalid weigths */
  input = casereader_create_filter_weight (input, dict, &warn, NULL);

  /* Remove all those cases which are outside the range (val1, val2) */
  input = casereader_create_filter_func (input, include_func_bi, NULL, 
	CONST_CAST (struct n_sample_test *, nst), NULL);

  /* Sort the data by the independent variable */
  input = sort_execute_1var (input, nst->indep_var);

  for (v = 0; v < nst->n_vars ; ++v)
  {
    struct jt jt;
    double variance;
    int g0;
    double nn = 0;
    int i;
    double sums[3] = {0,0,0};
    double e_sum[3] = {0,0,0};

    struct group_data *grp = NULL;
    double ccsq_sum = 0;

    struct casegrouper *grouper;
    struct casereader *group;
    struct casereader *vreader= casereader_clone (input);

    /* Get a few values into e_sum - we'll be needing these later */
    variance_calculation (vreader, nst->vars[v], dict, mff, e_sum, 3);
  
    grouper =
      casegrouper_create_vars (vreader, &nst->indep_var, 1);

    jt.obs = 0;
    jt.levels = 0;
    jt.n = 0;
    for (; casegrouper_get_next_group (grouper, &group); 
         casereader_destroy (group) )
      {
        struct casewriter *writer = autopaging_writer_create (proto);
        struct ccase *c;
        double cc = 0;

        group = sort_execute_1var (group, nst->vars[v]);
        for (; (c = casereader_read (group)); case_unref (c))
          {
            struct ccase *c_out = case_create (proto);
            const union value *x = case_data (c, nst->vars[v]);

            case_data_rw_idx (c_out, 0)->f = x->f;

            cc += dict_get_case_weight (dict, c, &warn);
            case_data_rw_idx (c_out, 1)->f = cc;
            casewriter_write (writer, c_out);
          }

        grp = xrealloc (grp, sizeof *grp * (jt.levels + 1));

        grp[jt.levels].reader = casewriter_make_reader (writer);
        grp[jt.levels].cc = cc;

        jt.levels++;
        jt.n += cc;
        ccsq_sum += pow2 (cc);
      }

    casegrouper_destroy (grouper);

    for (g0 = 0; g0 < jt.levels; ++g0)
      {
        int g1;
        for (g1 = g0 +1 ; g1 < jt.levels; ++g1)
          {
            double uu = u (&grp[g0], &grp[g1]);
            jt.obs += uu;
          }
        nn += pow2 (grp[g0].cc) * (2 * grp[g0].cc + 3);

        for (i = 0; i < 3; ++i)
          sums[i] += mff[i] (grp[g0].cc);

	casereader_destroy (grp[g0].reader);
      }

    free (grp);

    variance = (mff[0](jt.n) - sums[0] - e_sum[0]) / 72.0;
    variance += sums[1] * e_sum[1] / (36.0 * mff[1] (jt.n));
    variance += sums[2] * e_sum[2] / (8.0 * mff[2] (jt.n));

    jt.stddev = sqrt (variance);

    jt.mean = (pow2 (jt.n) - ccsq_sum) / 4.0;

    show_jt (nst, &jt, dict_get_weight (dict));
  }

  casereader_destroy (input);
  caseproto_unref (proto);
}



#include "gettext.h"
#define _(msgid) gettext (msgid)

static void
show_jt (const struct n_sample_test *nst, const struct jt *jt, const struct variable *wv)
{
  int i;
  const int row_headers = 1;
  const int column_headers = 1;
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : & F_8_0;

  struct tab_table *table =
    tab_create (row_headers + 7, column_headers + nst->n_vars);
  tab_set_format (table, RC_WEIGHT, wfmt);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Jonckheere-Terpstra Test"));

  /* Vertical lines inside the box */
  tab_box (table, 1, 0, -1, TAL_1,
	   row_headers, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  /* Box around the table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );

  tab_hline (table, TAL_2, 0, tab_nc (table) -1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);

  tab_text_format (table, 1, 0, TAT_TITLE | TAB_CENTER,
                   _("Number of levels in %s"),
                   var_to_string (nst->indep_var));
  tab_text (table, 2, 0, TAT_TITLE | TAB_CENTER, _("N"));
  tab_text (table, 3, 0, TAT_TITLE | TAB_CENTER, _("Observed J-T Statistic"));
  tab_text (table, 4, 0, TAT_TITLE | TAB_CENTER, _("Mean J-T Statistic"));
  tab_text (table, 5, 0, TAT_TITLE | TAB_CENTER, _("Std. Deviation of J-T Statistic"));
  tab_text (table, 6, 0, TAT_TITLE | TAB_CENTER, _("Std. J-T Statistic"));
  tab_text (table, 7, 0, TAT_TITLE | TAB_CENTER, _("Asymp. Sig. (2-tailed)"));


  for (i = 0; i < nst->n_vars; ++i)
    {
      double std_jt;

      tab_text (table, 0, i + row_headers, TAT_TITLE, 
                var_to_string (nst->vars[i]) );

      tab_double (table, 1, i + row_headers, TAT_TITLE, 
                  jt[0].levels, NULL, RC_INTEGER);
 
      tab_double (table, 2, i + row_headers, TAT_TITLE, 
                  jt[0].n, NULL, RC_WEIGHT);

      tab_double (table, 3, i + row_headers, TAT_TITLE, 
                  jt[0].obs, NULL, RC_OTHER);

      tab_double (table, 4, i + row_headers, TAT_TITLE, 
                  jt[0].mean, NULL, RC_OTHER);

      tab_double (table, 5, i + row_headers, TAT_TITLE, 
                  jt[0].stddev, NULL, RC_OTHER);

      std_jt = (jt[0].obs - jt[0].mean) / jt[0].stddev;
      tab_double (table, 6, i + row_headers, TAT_TITLE, 
                  std_jt, NULL, RC_OTHER);

      tab_double (table, 7, i + row_headers, TAT_TITLE, 
                  2.0 * ((std_jt > 0) ? gsl_cdf_ugaussian_Q (std_jt) : gsl_cdf_ugaussian_P (std_jt)), NULL, RC_PVALUE);
    }
  
  tab_submit (table);
}
