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

#include "mcnemar.h"

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
#include "libpspp/message.h"
 
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "data/value.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


struct mcnemar
{
  union value val0;
  union value val1;

  double n00;
  double n01;
  double n10;
  double n11;
};

static void
output_freq_table (variable_pair *vp,
		   const struct mcnemar *param,
		   const struct dictionary *dict);


static void
output_statistics_table (const struct two_sample_test *t2s,
			 const struct mcnemar *param,
			 const struct dictionary *dict);


void
mcnemar_execute (const struct dataset *ds,
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
  
  struct casereader *r = input;

  struct mcnemar *mc = xcalloc (t2s->n_pairs, sizeof *mc);

  for (i = 0 ; i < t2s->n_pairs; ++i )
    {
      mc[i].val0.f = mc[i].val1.f = SYSMIS;
    }

  for (; (c = casereader_read (r)) != NULL; case_unref (c))
    {
      const double weight = dict_get_case_weight (dict, c, &warn);

      for (i = 0 ; i < t2s->n_pairs; ++i )
	{
	  variable_pair *vp = &t2s->pairs[i];
	  const union value *value0 = case_data (c, (*vp)[0]);
	  const union value *value1 = case_data (c, (*vp)[1]);

	  if (var_is_value_missing ((*vp)[0], value0, exclude))
	    continue;

	  if (var_is_value_missing ((*vp)[1], value1, exclude))
	    continue;


	  if ( mc[i].val0.f == SYSMIS)
	    {
	      if (mc[i].val1.f != value0->f)
		mc[i].val0.f = value0->f;
	      else if (mc[i].val1.f != value1->f)
		mc[i].val0.f = value1->f;
	    }

	  if ( mc[i].val1.f == SYSMIS)
	    {
	      if (mc[i].val0.f != value1->f)
		mc[i].val1.f = value1->f;
	      else if (mc[i].val0.f != value0->f)
		mc[i].val1.f = value0->f;
	    }

	  if (mc[i].val0.f == value0->f && mc[i].val0.f == value1->f)
	    {
	      mc[i].n00 += weight;
	    }
	  else if ( mc[i].val0.f == value0->f && mc[i].val1.f == value1->f)
	    {
	      mc[i].n10 += weight;
	    }
	  else if ( mc[i].val1.f == value0->f && mc[i].val0.f == value1->f)
	    {
	      mc[i].n01 += weight;
	    }
	  else if ( mc[i].val1.f == value0->f && mc[i].val1.f == value1->f)
	    {
	      mc[i].n11 += weight;
	    }
	  else
	    {
	      msg (ME, _("The McNemar test is appropriate only for dichotomous variables"));
	    }
	}
    }

  casereader_destroy (r);

  for (i = 0 ; i < t2s->n_pairs ; ++i)
    output_freq_table (&t2s->pairs[i], mc + i, dict);

  output_statistics_table (t2s, mc, dict);

  free (mc);
}


static void
output_freq_table (variable_pair *vp,
		   const struct mcnemar *param,
		   const struct dictionary *dict)
{
  const int header_rows = 2;
  const int header_cols = 1;

  struct tab_table *table = tab_create (header_cols + 2, header_rows + 2);

  const struct variable *wv = dict_get_weight (dict);
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : & F_8_0;


  struct string pair_name;
  struct string val1str ;
  struct string val0str ;

  tab_set_format (table, RC_WEIGHT, wfmt);

  ds_init_empty (&val0str);
  ds_init_empty (&val1str);
  
  var_append_value_name ((*vp)[0], &param->val0, &val0str);
  var_append_value_name ((*vp)[1], &param->val1, &val1str);

  ds_init_cstr (&pair_name, var_to_string ((*vp)[0]));
  ds_put_cstr (&pair_name, " & ");
  ds_put_cstr (&pair_name, var_to_string ((*vp)[1]));

  tab_title (table, "%s", ds_cstr (&pair_name));

  ds_destroy (&pair_name);

  tab_headers (table, header_cols, 0, header_rows, 0);

  /* Vertical lines inside the box */
  tab_box (table, 0, 0, -1, TAL_1,
	   1, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  /* Box around entire table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  tab_vline (table, TAL_2, header_cols, 0, tab_nr (table) - 1);
  tab_hline (table, TAL_2, 0, tab_nc (table) - 1, header_rows);

  tab_text (table,  0, 0,  TAB_CENTER, var_to_string ((*vp)[0]));

  tab_joint_text (table,  1, 0,  2, 0, TAB_CENTER, var_to_string ((*vp)[1]));
  tab_hline (table, TAL_1, 1, tab_nc (table) - 1, 1);


  tab_text (table, 0, header_rows + 0, TAB_LEFT, ds_cstr (&val0str));
  tab_text (table, 0, header_rows + 1, TAB_LEFT, ds_cstr (&val1str));

  tab_text (table, header_cols + 0, 1, TAB_LEFT, ds_cstr (&val0str));
  tab_text (table, header_cols + 1, 1, TAB_LEFT, ds_cstr (&val1str));

  tab_double (table, header_cols + 0, header_rows + 0, TAB_RIGHT, param->n00, NULL, RC_WEIGHT);
  tab_double (table, header_cols + 0, header_rows + 1, TAB_RIGHT, param->n01, NULL, RC_WEIGHT);
  tab_double (table, header_cols + 1, header_rows + 0, TAB_RIGHT, param->n10, NULL, RC_WEIGHT);
  tab_double (table, header_cols + 1, header_rows + 1, TAB_RIGHT, param->n11, NULL, RC_WEIGHT);

  tab_submit (table);

  ds_destroy (&val0str);
  ds_destroy (&val1str);
}


static void
output_statistics_table (const struct two_sample_test *t2s,
			 const struct mcnemar *mc,
			 const struct dictionary *dict)
{
  int i;

  struct tab_table *table = tab_create (5, t2s->n_pairs + 1);

  const struct variable *wv = dict_get_weight (dict);
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : & F_8_0;

  tab_title (table, _("Test Statistics"));
  tab_set_format (table, RC_WEIGHT, wfmt);

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

  tab_text (table,  1, 0, TAT_TITLE | TAB_CENTER,
	    _("N"));

  tab_text (table,  2, 0, TAT_TITLE | TAB_CENTER,
	    _("Exact Sig. (2-tailed)"));

  tab_text (table,  3, 0, TAT_TITLE | TAB_CENTER,
	    _("Exact Sig. (1-tailed)"));

  tab_text (table,  4, 0, TAT_TITLE | TAB_CENTER,
	    _("Point Probability"));

  for (i = 0 ; i < t2s->n_pairs; ++i)
    {
      double sig;
      variable_pair *vp = &t2s->pairs[i];

      struct string pair_name;
      ds_init_cstr (&pair_name, var_to_string ((*vp)[0]));
      ds_put_cstr (&pair_name, " & ");
      ds_put_cstr (&pair_name, var_to_string ((*vp)[1]));

      tab_text (table,  0, 1 + i, TAB_LEFT, ds_cstr (&pair_name));
      ds_destroy (&pair_name);

      tab_double (table, 1, 1 + i, TAB_RIGHT, mc[i].n00 + mc[i].n01 + mc[i].n10 + mc[i].n11, NULL, RC_WEIGHT);

      sig = gsl_cdf_binomial_P (mc[i].n01,  0.5,  mc[i].n01 + mc[i].n10);

      tab_double (table, 2, 1 + i, TAB_RIGHT, 2 * sig, NULL, RC_PVALUE);
      tab_double (table, 3, 1 + i, TAB_RIGHT, sig, NULL, RC_PVALUE);

      tab_double (table, 4, 1 + i, TAB_RIGHT, gsl_ran_binomial_pdf (mc[i].n01, 0.5, mc[i].n01 + mc[i].n10),
		  NULL, RC_OTHER);
    }

  tab_submit (table);
}
