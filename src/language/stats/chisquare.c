/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2007, 2009 Free Software Foundation, Inc.

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

#include <language/stats/chisquare.h>

#include <stdlib.h>
#include <math.h>

#include <data/format.h>
#include <data/case.h>
#include <data/casereader.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/stats/freq.h>
#include <language/stats/npar.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/taint.h>
#include <output/table.h>

#include <gsl/gsl_cdf.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Return a hash table containing the frequency counts of each
   value of VAR in CF .
   It is the caller's responsibility to free the hash table when
   no longer required.
*/
static struct hsh_table *
create_freq_hash_with_range (const struct dictionary *dict,
			     struct casereader *input,
			     const struct variable *var,
			     double lo,
			     double hi)
{
  bool warn = true;
  float i_d;
  struct ccase *c;

  struct hsh_table *freq_hash =
    hsh_create (4, compare_freq, hash_freq,
		free_freq_mutable_hash,
		(void *) var);

  /* Populate the hash with zero entries */
  for (i_d = trunc (lo); i_d <= trunc (hi); i_d += 1.0 )
    {
      struct freq_mutable *fr = xmalloc (sizeof (*fr));
      value_init (&fr->value, 0);
      fr->value.f = i_d;
      fr->count = 0;
      hsh_insert (freq_hash, fr);
    }

  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    {
      struct freq_mutable fr;
      fr.value.f = trunc (case_num (c, var));
      if (fr.value.f >= lo && fr.value.f <= hi)
        {
          struct freq_mutable *existing_fr = hsh_force_find (freq_hash, &fr);
          existing_fr->count += dict_get_case_weight (dict, c, &warn);
        }
    }
  if (casereader_destroy (input))
    return freq_hash;
  else
    {
      hsh_destroy (freq_hash);
      return NULL;
    }
}


/* Return a hash table containing the frequency counts of each
   value of VAR in INPUT .
   It is the caller's responsibility to free the hash table when
   no longer required.
*/
static struct hsh_table *
create_freq_hash (const struct dictionary *dict,
		  struct casereader *input,
		  const struct variable *var)
{
  int width = var_get_width (var);
  bool warn = true;
  struct ccase *c;

  struct hsh_table *freq_hash =
    hsh_create (4, compare_freq, hash_freq,
		free_freq_mutable_hash,
		(void *) var);

  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    {
      struct freq_mutable fr;
      void **p;

      fr.value = *case_data (c, var);
      fr.count = dict_get_case_weight (dict, c, &warn);

      p = hsh_probe (freq_hash, &fr);
      if (*p == NULL)
        {
          struct freq_mutable *new_fr = *p = xmalloc (sizeof *new_fr);
          value_init (&new_fr->value, width);
          value_copy (&new_fr->value, &fr.value, width);
          new_fr->count = fr.count;
        }
      else
        {
          struct freq *existing_fr = *p;
          existing_fr->count += fr.count;
        }
    }
  if (casereader_destroy (input))
    return freq_hash;
  else
    {
      hsh_destroy (freq_hash);
      return NULL;
    }
}



static struct tab_table *
create_variable_frequency_table (const struct dictionary *dict,
				 struct casereader *input,
				 const struct chisquare_test *test,
				 int v,
				 struct hsh_table **freq_hash)

{
  int i;
  const struct one_sample_test *ost = (const struct one_sample_test*)test;
  int n_cells;
  struct tab_table *table ;
  const struct variable *var =  ost->vars[v];

  *freq_hash = create_freq_hash (dict, input, var);
  if (*freq_hash == NULL)
    return NULL;

  n_cells = hsh_count (*freq_hash);

  if ( test->n_expected > 0 && n_cells != test->n_expected )
    {
      msg(ME, _("CHISQUARE test specified %d expected values, but"
		" %d distinct values were encountered in variable %s."),
	  test->n_expected, n_cells,
	  var_get_name (var)
	  );
      hsh_destroy (*freq_hash);
      *freq_hash = NULL;
      return NULL;
    }

  table = tab_create(4, n_cells + 2, 0);
  tab_dim (table, tab_natural_dimensions, NULL, NULL);

  tab_title (table, var_to_string(var));
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
create_combo_frequency_table (const struct chisquare_test *test)
{
  int i;
  const struct one_sample_test *ost = (const struct one_sample_test*)test;

  struct tab_table *table ;

  int n_cells = test->hi - test->lo + 1;

  table = tab_create(1 + ost->n_vars * 4, n_cells + 3, 0);
  tab_dim (table, tab_natural_dimensions, NULL, NULL);

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
    tab_fixed (table, 0, 2 + i - test->lo,
		TAB_LEFT, 1 + i - test->lo, 8, 0);

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
  table = tab_create (1 + ost->n_vars, 4, 0);
  tab_dim (table, tab_natural_dimensions, NULL, NULL);
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
  struct one_sample_test *ost = (struct one_sample_test *) test;
  struct chisquare_test *cst = (struct chisquare_test *) test;
  int n_cells = 0;
  double total_expected = 0.0;
  const struct variable *wvar = dict_get_weight (dict);
  const struct fmt_spec *wfmt = wvar ?
    var_get_print_format (wvar) : & F_8_0;

  double *df = xzalloc (sizeof (*df) * ost->n_vars);
  double *xsq = xzalloc (sizeof (*df) * ost->n_vars);
  bool ok;

  for ( i = 0 ; i < cst->n_expected ; ++i )
    total_expected += cst->expected[i];

  if ( cst->ranged == false )
    {
      for ( v = 0 ; v < ost->n_vars ; ++v )
	{
	  double total_obs = 0.0;
	  struct hsh_table *freq_hash = NULL;
          struct casereader *reader =
            casereader_create_filter_missing (casereader_clone (input),
                                              &ost->vars[v], 1, exclude,
					      NULL, NULL);
	  struct tab_table *freq_table =
            create_variable_frequency_table(dict, reader, cst, v, &freq_hash);

	  struct freq **ff;

	  if ( NULL == freq_table )
            continue;
          ff = (struct freq **) hsh_sort (freq_hash);

	  n_cells = hsh_count (freq_hash);

	  for ( i = 0 ; i < n_cells ; ++i )
	    total_obs += ff[i]->count;

	  xsq[v] = 0.0;
	  for ( i = 0 ; i < n_cells ; ++i )
	    {
	      struct string str;
	      double exp;
	      const union value *observed_value = &ff[i]->value;

	      ds_init_empty (&str);
	      var_append_value_name (ost->vars[v], observed_value, &str);

	      /* The key */
	      tab_text (freq_table, 0, i + 1, TAB_LEFT, ds_cstr (&str));
	      ds_destroy (&str);


	      /* The observed N */
	      tab_double (freq_table, 1, i + 1, TAB_NONE,
			 ff[i]->count, wfmt);

	      if ( cst->n_expected > 0 )
		exp = cst->expected[i] * total_obs / total_expected ;
	      else
		exp = total_obs / (double) n_cells;

	      tab_double (freq_table, 2, i + 1, TAB_NONE,
			 exp, NULL);

	      /* The residual */
	      tab_double (freq_table, 3, i + 1, TAB_NONE,
			 ff[i]->count - exp, NULL);

	      xsq[v] += (ff[i]->count - exp) * (ff[i]->count - exp) / exp;
	    }

	  df[v] = n_cells - 1.0;

	  tab_double (freq_table, 1, i + 1, TAB_NONE,
		     total_obs, wfmt);

	  tab_submit (freq_table);

	  hsh_destroy (freq_hash);
	}
    }
  else  /* ranged == true */
    {
      struct tab_table *freq_table = create_combo_frequency_table (cst);

      n_cells = cst->hi - cst->lo + 1;

      for ( v = 0 ; v < ost->n_vars ; ++v )
	{
	  double total_obs = 0.0;
          struct casereader *reader =
            casereader_create_filter_missing (casereader_clone (input),
                                              &ost->vars[v], 1, exclude,
					      NULL, NULL);
	  struct hsh_table *freq_hash =
	    create_freq_hash_with_range (dict, reader,
                                         ost->vars[v], cst->lo, cst->hi);

	  struct freq **ff;

          if (freq_hash == NULL)
            continue;

          ff = (struct freq **) hsh_sort (freq_hash);
	  assert ( n_cells == hsh_count (freq_hash));

	  for ( i = 0 ; i < hsh_count (freq_hash) ; ++i )
	    total_obs += ff[i]->count;

	  xsq[v] = 0.0;
	  for ( i = 0 ; i < hsh_count (freq_hash) ; ++i )
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
			 ff[i]->count, wfmt);

	      if ( cst->n_expected > 0 )
		exp = cst->expected[i] * total_obs / total_expected ;
	      else
		exp = total_obs / (double) hsh_count (freq_hash);

	      /* The expected N */
	      tab_double (freq_table, v * 4 + 3, i + 2 , TAB_NONE,
			 exp, NULL);

	      /* The residual */
	      tab_double (freq_table, v * 4 + 4, i + 2 , TAB_NONE,
			 ff[i]->count - exp, NULL);

	      xsq[v] += (ff[i]->count - exp) * (ff[i]->count - exp) / exp;
	    }


	  tab_double (freq_table, v * 4 + 2, tab_nr (freq_table) - 1, TAB_NONE,
		     total_obs, wfmt);

	  df[v] = n_cells - 1.0;

	  hsh_destroy (freq_hash);
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

          tab_double (stats_table, 1 + v, 1, TAB_NONE, xsq[v], NULL);
          tab_fixed (stats_table, 1 + v, 2, TAB_NONE, df[v], 8, 0);

          tab_double (stats_table, 1 + v, 3, TAB_NONE,
                     gsl_cdf_chisq_Q (xsq[v], df[v]), NULL);
        }
      tab_submit (stats_table);
    }

  free (xsq);
  free (df);
}

