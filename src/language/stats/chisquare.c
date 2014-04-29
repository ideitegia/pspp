/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/stats/chisquare.h"

#include <gsl/gsl_cdf.h>
#include <math.h>
#include <stdlib.h>

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
#include "libpspp/taint.h"
#include "output/tab.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Adds frequency counts of each value of VAR in INPUT between LO and HI to
   FREQ_HASH.  LO and HI and each input value is truncated to an integer.
   Returns true if successful, false on input error.  It is the caller's
   responsibility to initialize FREQ_HASH and to free it when no longer
   required, even on failure. */
static bool
create_freq_hash_with_range (const struct dictionary *dict,
			     struct casereader *input,
			     const struct variable *var,
			     double lo_, double hi_,
                             struct hmap *freq_hash)
{
  struct freq **entries;
  bool warn = true;
  struct ccase *c;
  double lo, hi;
  double i_d;

  assert (var_is_numeric (var));
  lo = trunc (lo_);
  hi = trunc (hi_);

  /* Populate the hash with zero entries */
  entries = xnmalloc (hi - lo + 1, sizeof *entries);
  for (i_d = lo; i_d <= hi; i_d += 1.0 )
    {
      size_t ofs = i_d - lo;
      union value value = { i_d };
      entries[ofs] = freq_hmap_insert (freq_hash, &value, 0,
                                       value_hash (&value, 0, 0));
    }

  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    {
      double x = trunc (case_num (c, var));
      if (x >= lo && x <= hi)
        {
          size_t ofs = x - lo;
          struct freq *fr = entries[ofs];
          fr->count += dict_get_case_weight (dict, c, &warn);
        }
    }

  free (entries);

  return casereader_destroy (input);
}

/* Adds frequency counts of each value of VAR in INPUT to FREQ_HASH.  LO and HI
   and each input value is truncated to an integer.  Returns true if
   successful, false on input error.  It is the caller's responsibility to
   initialize FREQ_HASH and to free it when no longer required, even on
   failure. */
static bool
create_freq_hash (const struct dictionary *dict,
		  struct casereader *input,
		  const struct variable *var,
                  struct hmap *freq_hash)
{
  int width = var_get_width (var);
  bool warn = true;
  struct ccase *c;

  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    {
      const union value *value = case_data (c, var);
      size_t hash = value_hash (value, width, 0);
      double weight = dict_get_case_weight (dict, c, &warn);
      struct freq *f;

      f = freq_hmap_search (freq_hash, value, width, hash);
      if (f == NULL)
        f = freq_hmap_insert (freq_hash, value, width, hash);

      f->count += weight;
    }

  return casereader_destroy (input);
}

static struct tab_table *
create_variable_frequency_table (const struct dictionary *dict,
				 struct casereader *input,
				 const struct chisquare_test *test,
				 int v, struct hmap *freq_hash)

{
  int i;
  const struct one_sample_test *ost = (const struct one_sample_test*)test;
  int n_cells;
  struct tab_table *table ;
  const struct variable *var =  ost->vars[v];

  const struct variable *wvar = dict_get_weight (dict);
  const struct fmt_spec *wfmt = wvar ? var_get_print_format (wvar) : & F_8_0;

  hmap_init (freq_hash);
  if (!create_freq_hash (dict, input, var, freq_hash))
    {
      freq_hmap_destroy (freq_hash, var_get_width (var));
      return NULL;
    }

  n_cells = hmap_count (freq_hash);

  if ( test->n_expected > 0 && n_cells != test->n_expected )
    {
      msg(ME, _("CHISQUARE test specified %d expected values, but"
		" %d distinct values were encountered in variable %s."),
	  test->n_expected, n_cells,
	  var_get_name (var)
	  );
      freq_hmap_destroy (freq_hash, var_get_width (var));
      return NULL;
    }

  table = tab_create(4, n_cells + 2);
  tab_set_format (table, RC_WEIGHT, wfmt);

  tab_title (table, "%s", var_to_string(var));
  tab_text (table, 1, 0, TAB_LEFT, _("Observed N"));
  tab_text (table, 2, 0, TAB_LEFT, _("Expected N"));
  tab_text (table, 3, 0, TAB_LEFT, _("Residual"));

  tab_headers (table, 1, 0, 1, 0);

  tab_box (table, TAL_1, TAL_1, -1, -1,
	   0, 0, tab_nc (table) - 1, tab_nr(table) - 1 );

  tab_hline (table, TAL_1, 0, tab_nc(table) - 1, 1);

  tab_vline (table, TAL_2, 1, 0, tab_nr(table) - 1);
  for ( i = 2 ; i < 4 ; ++i )
    tab_vline (table, TAL_1, i, 0, tab_nr(table) - 1);


  tab_text (table, 0, tab_nr (table) - 1, TAB_LEFT, _("Total"));

  return table;
}


static struct tab_table *
create_combo_frequency_table (const struct dictionary *dict, const struct chisquare_test *test)
{
  int i;
  const struct one_sample_test *ost = (const struct one_sample_test*)test;

  struct tab_table *table ;

  const struct variable *wvar = dict_get_weight (dict);
  const struct fmt_spec *wfmt = wvar ? var_get_print_format (wvar) : & F_8_0;

  int n_cells = test->hi - test->lo + 1;

  table = tab_create(1 + ost->n_vars * 4, n_cells + 3);
  tab_set_format (table, RC_WEIGHT, wfmt);

  tab_title (table, _("Frequencies"));
  for ( i = 0 ; i < ost->n_vars ; ++i )
    {
      const struct variable *var = ost->vars[i];
      tab_text (table, i * 4 + 1, 1, TAB_LEFT, _("Category"));
      tab_text (table, i * 4 + 2, 1, TAB_LEFT, _("Observed N"));
      tab_text (table, i * 4 + 3, 1, TAB_LEFT, _("Expected N"));
      tab_text (table, i * 4 + 4, 1, TAB_LEFT, _("Residual"));

      tab_vline (table, TAL_2, i * 4 + 1,
		 0, tab_nr (table) - 1);

      tab_vline (table, TAL_1, i * 4 + 2,
		 0, tab_nr (table) - 1);

      tab_vline (table, TAL_1, i * 4 + 3,
		 1, tab_nr (table) - 1);

      tab_vline (table, TAL_1, i * 4 + 4,
		 1, tab_nr (table) - 1);


      tab_joint_text (table,
		      i * 4 + 1, 0,
		      i * 4 + 4, 0,
		      TAB_CENTER,
		      var_to_string (var));
    }

  for ( i = test->lo ; i <= test->hi ; ++i )
    tab_double (table, 0, 2 + i - test->lo, TAB_LEFT, 1 + i - test->lo, NULL, RC_INTEGER);

  tab_headers (table, 1, 0, 2, 0);

  tab_box (table, TAL_1, TAL_1, -1, -1,
	   0, 0, tab_nc (table) - 1, tab_nr(table) - 1 );

  tab_hline (table, TAL_1, 1, tab_nc(table) - 1, 1);
  tab_hline (table, TAL_1, 0, tab_nc(table) - 1, 2);

  tab_text (table, 0, tab_nr (table) - 1, TAB_LEFT, _("Total"));

  return table;
}


static struct tab_table *
create_stats_table (const struct chisquare_test *test)
{
  const struct one_sample_test *ost = (const struct one_sample_test*) test;

  struct tab_table *table;
  table = tab_create (1 + ost->n_vars, 4);
  tab_title (table, _("Test Statistics"));
  tab_headers (table, 1, 0, 1, 0);

  tab_box (table, TAL_1, TAL_1, -1, -1,
	   0, 0, tab_nc(table) - 1, tab_nr(table) - 1 );

  tab_box (table, -1, -1, -1, TAL_1,
	   1, 0, tab_nc(table) - 1, tab_nr(table) - 1 );


  tab_vline (table, TAL_2, 1, 0, tab_nr (table) - 1);
  tab_hline (table, TAL_1, 0, tab_nc (table) - 1, 1);


  tab_text (table, 0, 1, TAB_LEFT, _("Chi-Square"));
  tab_text (table, 0, 2, TAB_LEFT, _("df"));
  tab_text (table, 0, 3, TAB_LEFT, _("Asymp. Sig."));

  return table;
}


void
chisquare_execute (const struct dataset *ds,
		   struct casereader *input,
                   enum mv_class exclude,
		   const struct npar_test *test,
		   bool exact UNUSED,
		   double timer UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);
  int v, i;
  struct chisquare_test *cst = UP_CAST (test, struct chisquare_test,
                                        parent.parent);
  struct one_sample_test *ost = &cst->parent;
  int n_cells = 0;
  double total_expected = 0.0;

  double *df = xzalloc (sizeof (*df) * ost->n_vars);
  double *xsq = xzalloc (sizeof (*df) * ost->n_vars);
  bool ok;

  for ( i = 0 ; i < cst->n_expected ; ++i )
    total_expected += cst->expected[i];

  if ( cst->ranged == false )
    {
      for ( v = 0 ; v < ost->n_vars ; ++v )
	{
          const struct variable *var = ost->vars[v];
	  double total_obs = 0.0;
	  struct hmap freq_hash;
          struct casereader *reader =
            casereader_create_filter_missing (casereader_clone (input),
                                              &var, 1, exclude,
					      NULL, NULL);
	  struct tab_table *freq_table =
            create_variable_frequency_table (dict, reader, cst, v, &freq_hash);

	  struct freq **ff;

	  if ( NULL == freq_table )
            continue;
          ff = freq_hmap_sort (&freq_hash, var_get_width (var));

	  n_cells = hmap_count (&freq_hash);

	  for ( i = 0 ; i < n_cells ; ++i )
	    total_obs += ff[i]->count;

	  xsq[v] = 0.0;
	  for ( i = 0 ; i < n_cells ; ++i )
	    {
	      struct string str;
	      double exp;
	      const union value *observed_value = &ff[i]->value;

	      ds_init_empty (&str);
	      var_append_value_name (var, observed_value, &str);

	      /* The key */
	      tab_text (freq_table, 0, i + 1, TAB_LEFT, ds_cstr (&str));
	      ds_destroy (&str);


	      /* The observed N */
	      tab_double (freq_table, 1, i + 1, TAB_NONE,
			  ff[i]->count, NULL, RC_WEIGHT);

	      if ( cst->n_expected > 0 )
		exp = cst->expected[i] * total_obs / total_expected ;
	      else
		exp = total_obs / (double) n_cells;

	      tab_double (freq_table, 2, i + 1, TAB_NONE,
			  exp, NULL, RC_OTHER);

	      /* The residual */
	      tab_double (freq_table, 3, i + 1, TAB_NONE,
			  ff[i]->count - exp, NULL, RC_OTHER);

	      xsq[v] += (ff[i]->count - exp) * (ff[i]->count - exp) / exp;
	    }

	  df[v] = n_cells - 1.0;

	  tab_double (freq_table, 1, i + 1, TAB_NONE,
		      total_obs, NULL, RC_WEIGHT);

	  tab_submit (freq_table);

          freq_hmap_destroy (&freq_hash, var_get_width (var));
          free (ff);
	}
    }
  else  /* ranged == true */
    {
      struct tab_table *freq_table = create_combo_frequency_table (dict, cst);

      n_cells = cst->hi - cst->lo + 1;

      for ( v = 0 ; v < ost->n_vars ; ++v )
	{
          const struct variable *var = ost->vars[v];
	  double total_obs = 0.0;
          struct casereader *reader =
            casereader_create_filter_missing (casereader_clone (input),
                                              &var, 1, exclude,
					      NULL, NULL);
	  struct hmap freq_hash;
	  struct freq **ff;

          hmap_init (&freq_hash);
          if (!create_freq_hash_with_range (dict, reader, var,
                                            cst->lo, cst->hi, &freq_hash))
            {
              freq_hmap_destroy (&freq_hash, var_get_width (var));
              continue;
            }

          ff = freq_hmap_sort (&freq_hash, var_get_width (var));

	  for ( i = 0 ; i < hmap_count (&freq_hash) ; ++i )
	    total_obs += ff[i]->count;

	  xsq[v] = 0.0;
	  for ( i = 0 ; i < hmap_count (&freq_hash) ; ++i )
	    {
	      struct string str;
	      double exp;

	      const union value *observed_value = &ff[i]->value;

	      ds_init_empty (&str);
	      var_append_value_name (ost->vars[v], observed_value, &str);
	      /* The key */
	      tab_text  (freq_table, v * 4 + 1, i + 2 , TAB_LEFT,
			 ds_cstr (&str));
	      ds_destroy (&str);

	      /* The observed N */
	      tab_double (freq_table, v * 4 + 2, i + 2 , TAB_NONE,
			  ff[i]->count, NULL, RC_WEIGHT);

	      if ( cst->n_expected > 0 )
		exp = cst->expected[i] * total_obs / total_expected ;
	      else
		exp = total_obs / (double) hmap_count (&freq_hash);

	      /* The expected N */
	      tab_double (freq_table, v * 4 + 3, i + 2 , TAB_NONE,
			  exp, NULL, RC_OTHER);

	      /* The residual */
	      tab_double (freq_table, v * 4 + 4, i + 2 , TAB_NONE,
			  ff[i]->count - exp, NULL, RC_OTHER);

	      xsq[v] += (ff[i]->count - exp) * (ff[i]->count - exp) / exp;
	    }


	  tab_double (freq_table, v * 4 + 2, tab_nr (freq_table) - 1, TAB_NONE,
		      total_obs, NULL, RC_WEIGHT);

	  df[v] = n_cells - 1.0;

	  freq_hmap_destroy (&freq_hash, var_get_width (var));
          free (ff);
	}

      tab_submit (freq_table);
    }
  ok = !taint_has_tainted_successor (casereader_get_taint (input));
  casereader_destroy (input);

  if (ok)
    {
      struct tab_table *stats_table = create_stats_table (cst);

      /* Populate the summary statistics table */
      for ( v = 0 ; v < ost->n_vars ; ++v )
        {
          const struct variable *var = ost->vars[v];

          tab_text (stats_table, 1 + v, 0, TAB_CENTER, var_get_name (var));

          tab_double (stats_table, 1 + v, 1, TAB_NONE, xsq[v], NULL, RC_OTHER);
          tab_double (stats_table, 1 + v, 2, TAB_NONE, df[v], NULL, RC_INTEGER);

          tab_double (stats_table, 1 + v, 3, TAB_NONE,
		      gsl_cdf_chisq_Q (xsq[v], df[v]), NULL, RC_PVALUE);
        }
      tab_submit (stats_table);
    }

  free (xsq);
  free (df);
}

