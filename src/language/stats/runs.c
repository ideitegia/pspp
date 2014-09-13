/* PSPP - a program for statistical analysis. -*-c-*-
   Copyright (C) 2010, 2011, 2014 Free Software Foundation, Inc.

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

#include "language/stats/runs.h"

#include <float.h>
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
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/percentiles.h"
#include "math/sort.h"
#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


struct run_state
{
  /* The value used to dichotimise the data */
  double cutpoint;

  /* The number of cases not less than cutpoint */
  double np;

  /* The number of cases less than cutpoint */
  double nn;

  /* The sum of np and nn */
  double n;

  /* The number of runs */
  long runs;

  /* The sign of the last case seen */
  short last_sign;
};



/* Return the Z statistic representing the assympototic
   distribution of the number of runs */
static double
runs_statistic (const struct run_state *rs)
{
  double z;
  double sigma;
  double mu  = 2 * rs->np * rs->nn;
  mu /= rs->np + rs->nn;
  mu += 1.0;

  z = rs->runs - mu;

  if ( rs->n < 50)
    {
      if (z <= -0.5)
	z += 0.5;
      else if (z >= 0.5)
	z -= 0.5;
      else
	return 0;
    }

  sigma = 2 * rs->np * rs->nn;
  sigma *= 2 * rs->np * rs->nn - rs->nn - rs->np;
  sigma /= pow2 (rs->np + rs->nn);
  sigma /= rs->np + rs->nn - 1.0;
  sigma = sqrt (sigma);

  z /= sigma;

  return z;
}

static void show_runs_result (const struct runs_test *, const struct run_state *, const struct dictionary *);

void 
runs_execute (const struct dataset *ds,
	      struct casereader *input,
	      enum mv_class exclude,
	      const struct npar_test *test,
	      bool exact UNUSED,
	      double timer UNUSED)
{
  int v;
  struct ccase *c;
  const struct dictionary *dict = dataset_dict (ds);
  const struct variable *weight = dict_get_weight (dict);

  struct one_sample_test *otp = UP_CAST (test, struct one_sample_test, parent);
  struct runs_test *rt = UP_CAST (otp, struct runs_test, parent);
  struct run_state *rs = xcalloc (otp->n_vars, sizeof (*rs));

  switch  ( rt->cp_mode)
    {
    case CP_MODE:
      {
	for (v = 0; v < otp->n_vars; ++v)
	  {
	    bool multimodal = false;
	    struct run_state *run = &rs[v];
	    double last_cc;
	    struct casereader *group = NULL;
	    struct casegrouper *grouper;
	    struct casereader *reader = casereader_clone (input);
	    const struct variable *var = otp->vars[v];

	    reader = sort_execute_1var (reader, var);
	    
	    grouper = casegrouper_create_vars (reader, &var, 1);
	    last_cc = SYSMIS;
	    while (casegrouper_get_next_group (grouper, &group))
	      {
		double x = SYSMIS;
		double cc = 0.0;
		struct ccase *c;
		for (; (c = casereader_read (group)); case_unref (c))
		  {
		    const double w = weight ? case_data (c, weight)->f: 1.0;
		    const union value *val = case_data (c, var);
		    if ( var_is_value_missing (var, val, exclude))
		      continue;
		    x = val->f;
		    cc += w;
		  }

		if ( cc > last_cc)
		  {
		    run->cutpoint = x;
		  }
		else if ( cc == last_cc)
		  {
		    multimodal = true;
		    if ( x > run->cutpoint)
		      run->cutpoint = x;
		  }
		last_cc = cc;
		casereader_destroy (group);
	      }
	    casegrouper_destroy (grouper);
	    if (multimodal)
	      msg (MW, _("Multiple modes exist for variable `%s'.  "
                         "Using %.*g as the threshold value."),
		   var_get_name (var), DBL_DIG + 1, run->cutpoint);
	  }
      }
      break;
    case CP_MEDIAN:
      {
	for (v = 0; v < otp->n_vars; ++v)
	  {
	    double cc = 0.0;
	    struct ccase *c;
	    struct run_state *run = &rs[v];
	    struct casereader *reader = casereader_clone (input);
	    const struct variable *var = otp->vars[v];
	    struct casewriter *writer;
	    struct percentile *median;
	    struct order_stats *os;
	    struct subcase sc;
	    subcase_init_var (&sc, var, SC_ASCEND);
	    writer = sort_create_writer (&sc, casereader_get_proto (reader));

 	    for (; (c = casereader_read (reader)); )
	      {
		const union value *val = case_data (c, var);
		const double w = weight ? case_data (c, weight)->f: 1.0;
		if ( var_is_value_missing (var, val, exclude))
		  {
		    case_unref (c);
		    continue;
		  }

		cc += w;
		casewriter_write (writer, c);
	      }
	    subcase_destroy (&sc);
	    casereader_destroy (reader);
	    reader = casewriter_make_reader (writer);

	    median = percentile_create (0.5, cc);
	    os = &median->parent;
	    
	    order_stats_accumulate (&os, 1,
				    reader,
				    weight,
				    var,
				    exclude);

	    run->cutpoint = percentile_calculate (median, PC_HAVERAGE);
	    statistic_destroy (&median->parent.parent);
	  }
      }
      break;
    case CP_MEAN:
      {
	struct casereader *reader = casereader_clone (input);
	for (; (c = casereader_read (reader)); case_unref (c))
	  {
	    const double w = weight ? case_data (c, weight)->f: 1.0;
	    for (v = 0; v < otp->n_vars; ++v)
	      {
		const struct variable *var = otp->vars[v];
		const union value *val = case_data (c, var);
		const double x = val->f;
		struct run_state *run = &rs[v];

		if ( var_is_value_missing (var, val, exclude))
		  continue;

		run->cutpoint += x * w;
		run->n += w;
	      }
	  }
	casereader_destroy (reader);
	for (v = 0; v < otp->n_vars; ++v)
	  {
	    struct run_state *run = &rs[v];
	    run->cutpoint /= run->n;
	  }
      }
      break;
    case CP_CUSTOM:
      {
      for (v = 0; v < otp->n_vars; ++v)
	{
	  struct run_state *run = &rs[v];
	  run->cutpoint = rt->cutpoint;
	}
      }
      break;
    }

  for (; (c = casereader_read (input)); case_unref (c))
    {
      const double w = weight ? case_data (c, weight)->f: 1.0;

      for (v = 0; v < otp->n_vars; ++v)
	{
	  struct run_state *run = &rs[v];
	  const struct variable *var = otp->vars[v];
	  const union value *val = case_data (c, var);
	  double x = val->f;
	  double d = x - run->cutpoint;
	  short sign = 0;

	  if ( var_is_value_missing (var, val, exclude))
	    continue;

	  if (d >= 0)
	    {
	      sign = +1;
	      run->np += w;
	    }
	  else
	    {
	      sign = -1;
	      run->nn += w;
	    }

	  if (sign != run->last_sign)
	    run->runs++;

	  run->last_sign = sign;
	}
    }
  casereader_destroy (input);

  for (v = 0; v < otp->n_vars; ++v)
    {
      struct run_state *run = &rs[v];
      run->n = run->np + run->nn;
    }

  show_runs_result (rt, rs, dict);

  free (rs);
}



static void
show_runs_result (const struct runs_test *rt, const struct run_state *rs, const struct dictionary *dict)
{
  const struct variable *weight = dict_get_weight (dict);
  const struct fmt_spec *wfmt = weight ? var_get_print_format (weight) : &F_8_0;

  const struct one_sample_test *otp = &rt->parent;

  int i;
  const int row_headers = 1;
  const int column_headers = 1;
  struct tab_table *table =
    tab_create (row_headers + otp->n_vars, column_headers + 7);
  tab_set_format (table, RC_WEIGHT, wfmt);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Runs Test"));

  /* Box around the table and vertical lines inside*/
  tab_box (table, TAL_2, TAL_2, -1, TAL_1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );

  tab_hline (table, TAL_2, 0, tab_nc (table) -1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);

  for (i = 0 ; i < otp->n_vars; ++i)
    {
      const struct run_state *run = &rs[i];

      double z = runs_statistic (run);

      tab_text (table,  row_headers + i, 0, 
		TAT_TITLE | TAB_CENTER ,
		var_to_string (otp->vars[i]));

      tab_double (table, row_headers +i, 1, 0,
		  run->cutpoint, NULL, RC_OTHER);

      tab_double (table, row_headers +i, 2, 0,
		  run->nn, NULL, RC_WEIGHT);
		  
      tab_double (table, row_headers +i, 3, 0,
		  run->np, NULL, RC_WEIGHT);

      tab_double (table, row_headers +i, 4, 0,
		  run->n, NULL, RC_WEIGHT);

      tab_double (table, row_headers +i, 5, 0,
		  run->runs, NULL, RC_INTEGER);

      tab_double (table, row_headers +i, 6, 0,
		  z, NULL, RC_OTHER);

      tab_double (table, row_headers +i, 7, 0,
		  2.0 * (1.0 - gsl_cdf_ugaussian_P (z)), NULL, RC_PVALUE);
    }

  switch  ( rt->cp_mode)
    {
    case CP_CUSTOM:
      tab_text (table,  0, column_headers ,
		TAT_TITLE | TAB_LEFT , _("Test Value"));
      break;
    case CP_MODE:
      tab_text (table,  0, column_headers ,
		TAT_TITLE | TAB_LEFT , _("Test Value (mode)"));
      break;
    case CP_MEAN:
      tab_text (table,  0, column_headers ,
		TAT_TITLE | TAB_LEFT , _("Test Value (mean)"));
      break;
    case CP_MEDIAN:
      tab_text (table,  0, column_headers ,
		TAT_TITLE | TAB_LEFT , _("Test Value (median)"));
      break;
    }

  tab_text (table,  0, column_headers + 1,
	    TAT_TITLE | TAB_LEFT , _("Cases < Test Value"));

  tab_text (table,  0, column_headers + 2,
	    TAT_TITLE | TAB_LEFT , _("Cases ≥ Test Value"));

  tab_text (table,  0, column_headers + 3,
	    TAT_TITLE | TAB_LEFT , _("Total Cases"));

  tab_text (table,  0, column_headers + 4,
	    TAT_TITLE | TAB_LEFT , _("Number of Runs"));

  tab_text (table,  0, column_headers + 5,
	    TAT_TITLE | TAB_LEFT , _("Z"));

  tab_text (table,  0, column_headers + 6,
	    TAT_TITLE | TAB_LEFT , _("Asymp. Sig. (2-tailed)"));

  tab_submit (table);
}


