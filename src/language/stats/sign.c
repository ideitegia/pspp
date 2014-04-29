/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/stats/sign.h"

#include <gsl/gsl_cdf.h>
#include <gsl/gsl_randist.h>

#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "data/variable.h"
#include "language/stats/npar.h"
#include "libpspp/str.h"
#include "output/tab.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct sign_test_params
{
  double pos;
  double ties;
  double neg;

  double one_tailed_sig;
  double point_prob;
};


static void
output_frequency_table (const struct two_sample_test *t2s,
			const struct sign_test_params *param,
			const struct dictionary *dict)
{
  int i;
  struct tab_table *table = tab_create (3, 1 + 4 * t2s->n_pairs);

  const struct variable *wv = dict_get_weight (dict);
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : & F_8_0;

  tab_set_format (table, RC_WEIGHT, wfmt);
  tab_title (table, _("Frequencies"));

  tab_headers (table, 2, 0, 1, 0);

  /* Vertical lines inside the box */
  tab_box (table, 0, 0, -1, TAL_1,
	   1, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  /* Box around entire table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  tab_text (table,  2, 0,  TAB_CENTER, _("N"));

  for (i = 0 ; i < t2s->n_pairs; ++i)
    {
      variable_pair *vp = &t2s->pairs[i];

      struct string pair_name;
      ds_init_cstr (&pair_name, var_to_string ((*vp)[0]));
      ds_put_cstr (&pair_name, " - ");
      ds_put_cstr (&pair_name, var_to_string ((*vp)[1]));


      tab_text (table, 0, 1 + i * 4, TAB_LEFT, ds_cstr (&pair_name));

      ds_destroy (&pair_name);

      tab_hline (table, TAL_1, 0, tab_nc (table) - 1, 1 + i * 4);

      tab_text (table,  1, 1 + i * 4,  TAB_LEFT, _("Negative Differences"));
      tab_text (table,  1, 2 + i * 4,  TAB_LEFT, _("Positive Differences"));
      tab_text (table,  1, 3 + i * 4,  TAB_LEFT, _("Ties"));
      tab_text (table,  1, 4 + i * 4,  TAB_LEFT, _("Total"));

      tab_double (table, 2, 1 + i * 4, TAB_RIGHT, param[i].neg, NULL, RC_WEIGHT);
      tab_double (table, 2, 2 + i * 4, TAB_RIGHT, param[i].pos, NULL, RC_WEIGHT);
      tab_double (table, 2, 3 + i * 4, TAB_RIGHT, param[i].ties, NULL, RC_WEIGHT);
      tab_double (table, 2, 4 + i * 4, TAB_RIGHT,
		  param[i].ties + param[i].neg + param[i].pos, NULL, RC_WEIGHT);
    }

  tab_submit (table);
}

static void
output_statistics_table (const struct two_sample_test *t2s,
			 const struct sign_test_params *param)
{
  int i;
  struct tab_table *table = tab_create (1 + t2s->n_pairs, 4);

  tab_title (table, _("Test Statistics"));

  tab_headers (table, 0, 1,  0, 1);

  tab_hline (table, TAL_2, 0, tab_nc (table) - 1, 1);
  tab_vline (table, TAL_2, 1, 0, tab_nr (table) - 1);


  /* Vertical lines inside the box */
  tab_box (table, -1, -1, -1, TAL_1,
	   0, 0,
	   tab_nc (table) - 1, tab_nr (table) - 1);

  /* Box around entire table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0, 0, tab_nc (table) - 1,
	   tab_nr (table) - 1);

  tab_text (table,  0, 1, TAT_TITLE | TAB_LEFT,
	    _("Exact Sig. (2-tailed)"));

  tab_text (table,  0, 2, TAT_TITLE | TAB_LEFT,
	    _("Exact Sig. (1-tailed)"));

  tab_text (table,  0, 3, TAT_TITLE | TAB_LEFT,
	    _("Point Probability"));

  for (i = 0 ; i < t2s->n_pairs; ++i)
    {
      variable_pair *vp = &t2s->pairs[i];

      struct string pair_name;
      ds_init_cstr (&pair_name, var_to_string ((*vp)[0]));
      ds_put_cstr (&pair_name, " - ");
      ds_put_cstr (&pair_name, var_to_string ((*vp)[1]));

      tab_text (table,  1 + i, 0, TAB_LEFT, ds_cstr (&pair_name));
      ds_destroy (&pair_name);

      tab_double (table, 1 + i, 1, TAB_RIGHT,
		  param[i].one_tailed_sig * 2, NULL, RC_PVALUE);

      tab_double (table, 1 + i, 2, TAB_RIGHT, param[i].one_tailed_sig, NULL, RC_PVALUE);
      tab_double (table, 1 + i, 3, TAB_RIGHT, param[i].point_prob, NULL, RC_PVALUE);
    }

  tab_submit (table);
}

void
sign_execute (const struct dataset *ds,
		  struct casereader *input,
		  enum mv_class exclude,
		  const struct npar_test *test,
		  bool exact UNUSED,
		  double timer UNUSED)
{
  int i;
  bool warn = true;
  const struct dictionary *dict = dataset_dict (ds);
  const struct two_sample_test *t2s = UP_CAST (test, const struct two_sample_test, parent);
  struct ccase *c;

  struct sign_test_params *stp = xcalloc (t2s->n_pairs, sizeof *stp);

  struct casereader *r = input;

  for (; (c = casereader_read (r)) != NULL; case_unref (c))
    {
      const double weight = dict_get_case_weight (dict, c, &warn);

      for (i = 0 ; i < t2s->n_pairs; ++i )
	{
	  variable_pair *vp = &t2s->pairs[i];
	  const union value *value0 = case_data (c, (*vp)[0]);
	  const union value *value1 = case_data (c, (*vp)[1]);
	  const double diff = value0->f - value1->f;

	  if (var_is_value_missing ((*vp)[0], value0, exclude))
	    continue;

	  if (var_is_value_missing ((*vp)[1], value1, exclude))
	    continue;

	  if ( diff > 0)
	    stp[i].pos += weight;
	  else if (diff < 0)
	    stp[i].neg += weight;
	  else
	    stp[i].ties += weight;
	}
    }

  casereader_destroy (r);

  for (i = 0 ; i < t2s->n_pairs; ++i )
    {
      int r = MIN (stp[i].pos, stp[i].neg);
      stp[i].one_tailed_sig = gsl_cdf_binomial_P (r,
						  0.5,
						  stp[i].pos + stp[i].neg);

      stp[i].point_prob = gsl_ran_binomial_pdf (r, 0.5,
						stp[i].pos + stp[i].neg);
    }

  output_frequency_table (t2s, stp, dict);

  output_statistics_table (t2s, stp);

  free (stp);
}
