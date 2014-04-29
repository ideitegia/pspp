/* PSPP - a program for statistical analysis.
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
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include "language/stats/cochran.h"

#include <float.h>
#include <gsl/gsl_cdf.h>
#include <stdbool.h>

#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/val-type.h"
#include "data/variable.h"
#include "language/stats/npar.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct cochran
{
  double success;
  double failure;

  double *hits;
  double *misses;

  const struct dictionary *dict;
  double cc;
  double df;
  double q;
};

static void show_freqs_box (const struct one_sample_test *ost, const struct cochran *ch);
static void show_sig_box (const struct cochran *ch);

void
cochran_execute (const struct dataset *ds,
	      struct casereader *input,
	      enum mv_class exclude,
	      const struct npar_test *test,
	      bool exact UNUSED, double timer UNUSED)
{
  struct one_sample_test *ct = UP_CAST (test, struct one_sample_test, parent);
  int v;
  struct cochran ch;
  const struct dictionary *dict = dataset_dict (ds);
  const struct variable *weight = dict_get_weight (dict);

  struct ccase *c;
  double rowsq = 0;
  ch.cc = 0.0;
  ch.dict = dict;
  ch.success = SYSMIS;
  ch.failure = SYSMIS;
  ch.hits = xcalloc (ct->n_vars, sizeof *ch.hits);
  ch.misses = xcalloc (ct->n_vars, sizeof *ch.misses);

  for (; (c = casereader_read (input)); case_unref (c))
    {
      double case_hits = 0.0;
      const double w = weight ? case_data (c, weight)->f: 1.0;
      for (v = 0; v < ct->n_vars; ++v)
	{
	  const struct variable *var = ct->vars[v];
	  const union value *val = case_data (c, var);

	  if ( var_is_value_missing (var, val, exclude))
	    continue;

	  if ( ch.success == SYSMIS)
	    {
	      ch.success = val->f;
	    }
	  else if (ch.failure == SYSMIS && val->f != ch.success)
	    {
	      ch.failure = val->f;
	    }
	  if ( ch.success == val->f)
	    {
	      ch.hits[v] += w;
	      case_hits += w;
	    }
	  else if ( ch.failure == val->f)
	    {
	      ch.misses[v] += w;
	    }
	  else
	    {
	      msg (MW, _("More than two values encountered.  Cochran Q test will not be run."));
	      goto finish;
	    }
	}
      ch.cc += w;
      rowsq += pow2 (case_hits);
    }
  casereader_destroy (input);
  
  {
    double c_l = 0;
    double c_l2 = 0;
    for (v = 0; v < ct->n_vars; ++v)
      {
	c_l += ch.hits[v];
	c_l2 += pow2 (ch.hits[v]);
      }

    ch.q = ct->n_vars * c_l2;
    ch.q -= pow2 (c_l);
    ch.q *= ct->n_vars - 1;

    ch.q /= ct->n_vars * c_l - rowsq;
  
    ch.df = ct->n_vars - 1;
  }

  show_freqs_box (ct, &ch);
  show_sig_box (&ch);

 finish:

  free (ch.hits);
  free (ch.misses);
}

static void
show_freqs_box (const struct one_sample_test *ost, const struct cochran *ct)
{
  int i;
  const struct variable *weight = dict_get_weight (ct->dict);
  const struct fmt_spec *wfmt = weight ? var_get_print_format (weight) : &F_8_0;

  const int row_headers = 1;
  const int column_headers = 2;
  struct tab_table *table =
    tab_create (row_headers + 2, column_headers + ost->n_vars);
  tab_set_format (table, RC_WEIGHT, wfmt);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Frequencies"));

  /* Vertical lines inside the box */
  tab_box (table, 1, 0, -1, TAL_1,
	   row_headers, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  /* Box around the table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );

  tab_joint_text (table, 1, 0, 2, 0,
		  TAT_TITLE | TAB_CENTER, _("Value"));

  tab_text_format (table, 1, 1, 0, _("Success (%.*g)"),
                   DBL_DIG + 1, ct->success);
  tab_text_format (table, 2, 1, 0, _("Failure (%.*g)"),
                   DBL_DIG + 1, ct->failure);

  tab_hline (table, TAL_2, 0, tab_nc (table) - 1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);

  for (i = 0 ; i < ost->n_vars ; ++i)
    {
      tab_text (table, 0, column_headers + i,
		TAB_LEFT, var_to_string (ost->vars[i]));

      tab_double (table, 1, column_headers + i, 0,
		  ct->hits[i], NULL, RC_WEIGHT);

      tab_double (table, 2, column_headers + i, 0,
		  ct->misses[i], NULL, RC_WEIGHT);
    }

  tab_submit (table);
}



static void
show_sig_box (const struct cochran *ch)
{
  const struct variable *weight = dict_get_weight (ch->dict);
  const struct fmt_spec *wfmt = weight ? var_get_print_format (weight) : &F_8_0;

  const int row_headers = 1;
  const int column_headers = 0;
  struct tab_table *table =
    tab_create (row_headers + 1, column_headers + 4);


  tab_set_format (table, RC_WEIGHT, wfmt);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Test Statistics"));

  tab_text (table,  0, column_headers,
	    TAT_TITLE | TAB_LEFT , _("N"));

  tab_text (table,  0, 1 + column_headers,
	    TAT_TITLE | TAB_LEFT , _("Cochran's Q"));

  tab_text (table,  0, 2 + column_headers,
	    TAT_TITLE | TAB_LEFT, _("df"));

  tab_text (table,  0, 3 + column_headers,
	    TAT_TITLE | TAB_LEFT, _("Asymp. Sig."));

  /* Box around the table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );

  tab_hline (table, TAL_2, 0, tab_nc (table) -1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);

  tab_double (table, 1, column_headers, 
	      0, ch->cc, NULL, RC_WEIGHT);

  tab_double (table, 1, column_headers + 1, 
	      0, ch->q, NULL, RC_OTHER);

  tab_double (table, 1, column_headers + 2, 
	      0, ch->df, NULL, RC_INTEGER);

  tab_double (table, 1, column_headers + 3, 
	      0, gsl_cdf_chisq_Q (ch->q, ch->df), 
	      NULL, RC_PVALUE);

  tab_submit (table);
}
