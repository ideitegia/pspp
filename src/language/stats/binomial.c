/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include <libpspp/compiler.h>
#include <output/table.h>
#include <libpspp/alloc.h>

#include <data/case.h>
#include <data/casefile.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/variable.h>
#include <data/value.h>
#include <data/value-labels.h>
#include <data/casefilter.h>

#include <libpspp/message.h>
#include <libpspp/assertion.h>

#include "binomial.h"
#include "freq.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include <libpspp/misc.h>

#include <gsl/gsl_cdf.h>
#include <gsl/gsl_randist.h>
#include <gsl-extras/gsl-extras.h>

#include <minmax.h>

#include <libpspp/hash.h>

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

  double sig1tailed = gslextras_cdf_binomial_P (n1, n1 + n2, p);

  if ( p == 0.5 )
    return sig1tailed > 0.5 ? 1.0 :sig1tailed * 2.0;

  return sig1tailed ;
}

static void
do_binomial (const struct dictionary *dict,
	     const struct casefile *cf,
	     const struct binomial_test *bst,
	     struct freq *cat1,
	     struct freq *cat2,
	     const struct casefilter *filter
	     )
{
  bool warn = true;

  const struct one_sample_test *ost = (const struct one_sample_test *) bst;
  struct ccase c;
  struct casereader *r = casefile_get_reader (cf, NULL);

  while (casereader_read(r, &c))
    {
      int v;
      double w =
	dict_get_case_weight (dict, &c, &warn);

      for (v = 0 ; v < ost->n_vars ; ++v )
	{
	  const struct variable *var = ost->vars[v];
	  const union value *value = case_data (&c, var);

	  if ( casefilter_variable_missing (filter, &c, var))
	    break;

	  if ( NULL == cat1[v].value )
	    {
	      cat1[v].value = value_dup (value, var_get_width (var));
	      cat1[v].count = w;
	    }
	  else if ( 0 == compare_values (cat1[v].value, value,
					 var_get_width (var)))
	    cat1[v].count += w;
	  else if ( NULL == cat2[v].value )
	    {
	      cat2[v].value = value_dup (value, var_get_width (var));
	      cat2[v].count = w;
	    }
	  else if ( 0 == compare_values (cat2[v].value, value,
					 var_get_width (var)))
	    cat2[v].count += w;
	  else if ( bst->category1 == SYSMIS)
	    msg (ME, _("Variable %s is not dichotomous"), var_get_name (var));
	}

      case_destroy (&c);
    }
  casereader_destroy (r);
}



void
binomial_execute (const struct dataset *ds,
		  const struct casefile *cf,
		  struct casefilter *filter,
		  const struct npar_test *test)
{
  int v;
  const struct binomial_test *bst = (const struct binomial_test *) test;
  const struct one_sample_test *ost = (const struct one_sample_test*) test;

  struct freq *cat1 = xzalloc (sizeof (*cat1) * ost->n_vars);
  struct freq *cat2 = xzalloc (sizeof (*cat1) * ost->n_vars);
  struct tab_table *table ;

  assert ((bst->category1 == SYSMIS) == (bst->category2 == SYSMIS) );

  if ( bst->category1 != SYSMIS )
    {
      union value v;
      v.f = bst->category1;
      cat1->value = value_dup (&v, 0);
    }

  if ( bst->category2 != SYSMIS )
    {
      union value v;
      v.f = bst->category2;
      cat2->value = value_dup (&v, 0);
    }

  do_binomial (dataset_dict(ds), cf, bst, cat1, cat2, filter);

  table = tab_create (7, ost->n_vars * 3 + 1, 0);

  tab_dim (table, tab_natural_dimensions);

  tab_title (table, _("Binomial Test"));

  tab_headers (table, 2, 0, 1, 0);

  tab_box (table, TAL_1, TAL_1, -1, TAL_1,
	   0, 0, table->nc - 1, tab_nr(table) - 1 );

  for (v = 0 ; v < ost->n_vars; ++v)
    {
      double n_total, sig;
      const struct variable *var = ost->vars[v];
      tab_hline (table, TAL_1, 0, tab_nc (table) -1, 1 + v * 3);

      /* Titles */
      tab_text (table, 0, 1 + v * 3, TAB_LEFT,
		var_to_string (var));

      tab_text (table, 1, 1 + v * 3, TAB_LEFT,
		_("Group1"));

      tab_text (table, 1, 2 + v * 3, TAB_LEFT,
		_("Group2"));

      tab_text (table, 1, 3 + v * 3, TAB_LEFT,
		_("Total"));

      /* Test Prop */
      tab_float (table, 5, 1 + v * 3, TAB_NONE, bst->p, 8, 3);

      /* Category labels */
      tab_text (table, 2, 1 + v * 3, TAB_NONE,
		var_get_value_name (var, cat1[v].value));

      tab_text (table, 2, 2 + v * 3, TAB_NONE,
		var_get_value_name (var, cat2[v].value));

      /* Observed N */
      tab_float (table, 3, 1 + v * 3, TAB_NONE,
		 cat1[v].count, 8, 0);

      tab_float (table, 3, 2 + v * 3, TAB_NONE,
		 cat2[v].count, 8, 0);

      n_total = cat1[v].count + cat2[v].count;


      tab_float (table, 3, 3 + v * 3, TAB_NONE,
		 n_total, 8, 0);

      /* Observed Proportions */

      tab_float (table, 4, 1 + v * 3, TAB_NONE,
		 cat1[v].count / n_total, 8, 3);

      tab_float (table, 4, 2 + v * 3, TAB_NONE,
		 cat2[v].count / n_total, 8, 3);

      tab_float (table, 4, 3 + v * 3, TAB_NONE,
		 (cat1[v].count + cat2[v].count) / n_total, 8, 2);


      /* Significance */
      sig = calculate_binomial (cat1[v].count, cat2[v].count,
				       bst->p);

      tab_float (table, 6, 1 + v * 3, TAB_NONE,
		 sig, 8, 3);
    }

  tab_text (table,  2, 0,  TAB_CENTER, _("Category"));
  tab_text (table,  3, 0,  TAB_CENTER, _("N"));
  tab_text (table,  4, 0,  TAB_CENTER, _("Observed Prop."));
  tab_text (table,  5, 0,  TAB_CENTER, _("Test Prop."));

  tab_text (table,  6, 0,  TAB_CENTER | TAT_PRINTF,
	    _("Exact Sig. (%d-tailed)"),
	    bst->p == 0.5 ? 2: 1);

  tab_vline (table, TAL_2, 2, 0, tab_nr (table) -1);

  free (cat1);
  free (cat2);

  tab_submit (table);

}
