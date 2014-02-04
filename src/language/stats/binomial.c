/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2010, 2011, 2014 Free Software Foundation, Inc.

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

#include "language/stats/binomial.h"

#include <float.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_randist.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value-labels.h"
#include "data/value.h"
#include "data/variable.h"
#include "language/stats/freq.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "output/tab.h"

#include "gl/xalloc.h"
#include "gl/minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static double calculate_binomial_internal (double n1, double n2,
					   double p);


static void
swap (double *i1, double *i2)
{
  double temp = *i1;
  *i1 = *i2;
  *i2 = temp;
}

static double
calculate_binomial (double n1, double n2, double p)
{
  const double n = n1 + n2;
  const bool test_reversed = (n1 / n > p ) ;
  if ( test_reversed )
    {
      p = 1 - p ;
      swap (&n1, &n2);
    }

  return calculate_binomial_internal (n1, n2, p);
}

static double
calculate_binomial_internal (double n1, double n2, double p)
{
  /* SPSS Statistical Algorithms has completely different and WRONG
     advice here. */

  double sig1tailed = gsl_cdf_binomial_P (n1, p, n1 + n2);

  if ( p == 0.5 )
    return sig1tailed > 0.5 ? 1.0 :sig1tailed * 2.0;

  return sig1tailed ;
}

static bool
do_binomial (const struct dictionary *dict,
	     struct casereader *input,
	     const struct one_sample_test *ost,
	     struct freq *cat1,
	     struct freq *cat2,
             enum mv_class exclude
	     )
{
  const struct binomial_test *bst = UP_CAST (ost, const struct binomial_test, parent);
  bool warn = true;

  struct ccase *c;

  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    {
      int v;
      double w = dict_get_case_weight (dict, c, &warn);

      for (v = 0 ; v < ost->n_vars ; ++v )
	{
	  const struct variable *var = ost->vars[v];
	  double value = case_num (c, var);

	  if (var_is_num_missing (var, value, exclude))
	    continue;

	  if (bst->cutpoint != SYSMIS)
	    {
	      if ( cat1[v].value.f >= value )
		  cat1[v].count  += w;
	      else
		  cat2[v].count += w;
	    }
	  else
	    {
	      if ( SYSMIS == cat1[v].value.f )
		{
		  cat1[v].value.f = value;
		  cat1[v].count = w;
		}
	      else if ( cat1[v].value.f == value )
		cat1[v].count += w;
	      else if ( SYSMIS == cat2[v].value.f )
		{
		  cat2[v].value.f = value;
		  cat2[v].count = w;
		}
	      else if ( cat2[v].value.f == value )
		cat2[v].count += w;
	      else if ( bst->category1 == SYSMIS)
		msg (ME, _("Variable %s is not dichotomous"), var_get_name (var));
	    }
	}
    }
  return casereader_destroy (input);
}



void
binomial_execute (const struct dataset *ds,
		  struct casereader *input,
                  enum mv_class exclude,
		  const struct npar_test *test,
		  bool exact UNUSED,
		  double timer UNUSED)
{
  int v;
  const struct dictionary *dict = dataset_dict (ds);
  const struct one_sample_test *ost = UP_CAST (test, const struct one_sample_test, parent);
  const struct binomial_test *bst = UP_CAST (ost, const struct binomial_test, parent);

  struct freq *cat[2];
  int i;

  assert ((bst->category1 == SYSMIS) == (bst->category2 == SYSMIS) || bst->cutpoint != SYSMIS);

  for (i = 0; i < 2; i++)
    {
      double value;
      if (i == 0)
        value = bst->cutpoint != SYSMIS ? bst->cutpoint : bst->category1;
      else
        value = bst->category2;

      cat[i] = xnmalloc (ost->n_vars, sizeof *cat[i]);
      for (v = 0; v < ost->n_vars; v++)
        {
          cat[i][v].value.f = value;
          cat[i][v].count = 0;
        }
    }

  if (do_binomial (dataset_dict (ds), input, ost, cat[0], cat[1], exclude))
    {
      const struct variable *wvar = dict_get_weight (dict);
      const struct fmt_spec *wfmt = wvar ?
	var_get_print_format (wvar) : & F_8_0;

      struct tab_table *table = tab_create (7, ost->n_vars * 3 + 1);

      tab_title (table, _("Binomial Test"));

      tab_headers (table, 2, 0, 1, 0);

      tab_box (table, TAL_1, TAL_1, -1, TAL_1,
               0, 0, tab_nc (table) - 1, tab_nr(table) - 1 );

      for (v = 0 ; v < ost->n_vars; ++v)
        {
          double n_total, sig;
	  struct string catstr[2];
          const struct variable *var = ost->vars[v];

	  ds_init_empty (&catstr[0]);
	  ds_init_empty (&catstr[1]);

	  if ( bst->cutpoint != SYSMIS)
	    {
	      ds_put_format (&catstr[0], "<= %.*g",
                             DBL_DIG + 1, bst->cutpoint);
	    }
          else
            {
              var_append_value_name (var, &cat[0][v].value, &catstr[0]);
              var_append_value_name (var, &cat[1][v].value, &catstr[1]);
            }

          tab_hline (table, TAL_1, 0, tab_nc (table) -1, 1 + v * 3);

          /* Titles */
          tab_text (table, 0, 1 + v * 3, TAB_LEFT, var_to_string (var));
          tab_text (table, 1, 1 + v * 3, TAB_LEFT, _("Group1"));
          tab_text (table, 1, 2 + v * 3, TAB_LEFT, _("Group2"));
          tab_text (table, 1, 3 + v * 3, TAB_LEFT, _("Total"));

          /* Test Prop */
          tab_double (table, 5, 1 + v * 3, TAB_NONE, bst->p, NULL);

          /* Category labels */
          tab_text (table, 2, 1 + v * 3, TAB_NONE, ds_cstr (&catstr[0]));
	  tab_text (table, 2, 2 + v * 3, TAB_NONE, ds_cstr (&catstr[1]));

          /* Observed N */
          tab_double (table, 3, 1 + v * 3, TAB_NONE, cat[0][v].count, wfmt);
          tab_double (table, 3, 2 + v * 3, TAB_NONE, cat[1][v].count, wfmt);

          n_total = cat[0][v].count + cat[1][v].count;
          tab_double (table, 3, 3 + v * 3, TAB_NONE, n_total, wfmt);

          /* Observed Proportions */
          tab_double (table, 4, 1 + v * 3, TAB_NONE,
                     cat[0][v].count / n_total, NULL);
          tab_double (table, 4, 2 + v * 3, TAB_NONE,
                     cat[1][v].count / n_total, NULL);

          tab_double (table, 4, 3 + v * 3, TAB_NONE,
                     (cat[0][v].count + cat[1][v].count) / n_total, NULL);

          /* Significance */
          sig = calculate_binomial (cat[0][v].count, cat[1][v].count, bst->p);
          tab_double (table, 6, 1 + v * 3, TAB_NONE, sig, NULL);

	  ds_destroy (&catstr[0]);
	  ds_destroy (&catstr[1]);
        }

      tab_text (table,  2, 0,  TAB_CENTER, _("Category"));
      tab_text (table,  3, 0,  TAB_CENTER, _("N"));
      tab_text (table,  4, 0,  TAB_CENTER, _("Observed Prop."));
      tab_text (table,  5, 0,  TAB_CENTER, _("Test Prop."));

      tab_text_format (table,  6, 0,  TAB_CENTER,
                       _("Exact Sig. (%d-tailed)"),
                       bst->p == 0.5 ? 2 : 1);

      tab_vline (table, TAL_2, 2, 0, tab_nr (table) -1);
      tab_submit (table);
    }

  for (i = 0; i < 2; i++)
    free (cat[i]);
}
