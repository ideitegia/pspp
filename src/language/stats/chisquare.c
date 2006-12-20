/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.

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
#include <libpspp/assertion.h>

#include <stdlib.h>

#include <data/case.h>
#include <data/casefile.h>
#include <data/casefilter.h>
#include <data/variable.h>
#include <data/dictionary.h>
#include <data/procedure.h>

#include <libpspp/message.h>
#include <libpspp/hash.h>
#include <libpspp/alloc.h>

#include <gsl/gsl_cdf.h>

#include <output/table.h>
#include <data/value-labels.h>

#include "npar.h"
#include "chisquare.h"
#include "freq.h"

#include <math.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)




/* Return a hash table containing the frequency counts of each 
   value of VAR in CF .
   It is the caller's responsibility to free the hash table when 
   no longer required.
*/
static struct hsh_table *
create_freq_hash_with_range (const struct dictionary *dict, 
			     const struct casefile *cf, 
			     struct casefilter *filter,
			     const struct variable *var, 
			     double lo, 
			     double hi)
{
  bool warn = true;
  float i_d;
  struct ccase c;
  struct casereader *r = casefile_get_reader (cf, filter);

  struct hsh_table *freq_hash = 
    hsh_create (4, compare_freq, hash_freq, 
		free_freq_mutable_hash,
		(void *) var);

  /* Populate the hash with zero entries */
  for (i_d = trunc (lo); i_d <= trunc (hi); i_d += 1.0 ) 
    {
      union value the_value;
      struct freq_mutable *fr = xmalloc (sizeof (*fr));

      the_value.f = i_d;

      fr->value = value_dup (&the_value, 0);
      fr->count = 0;

      hsh_insert (freq_hash, fr);
    }

  while (casereader_read(r, &c))
    {
      union value obs_value;
      struct freq **existing_fr;
      struct freq *fr = xmalloc(sizeof  (*fr));
      fr->value = case_data (&c, var);

      if ( casefilter_variable_missing (filter, &c, var))
	{
	  free (fr);
	  continue;
	}

      fr->count = dict_get_case_weight (dict, &c, &warn);

      obs_value.f = trunc (fr->value->f);

      if ( obs_value.f < lo || obs_value.f > hi) 
	{
	  free (fr);
	  case_destroy (&c);
	  continue;
	}

      fr->value = &obs_value;

      existing_fr = (struct freq **) hsh_probe (freq_hash, fr);

      /* This must exist in the hash, because we previously populated it 
	 with zero counts */
      assert (*existing_fr);

      (*existing_fr)->count += fr->count;
      free (fr);

      case_destroy (&c);
    }
  casereader_destroy (r);

  return freq_hash;
}


/* Return a hash table containing the frequency counts of each 
   value of VAR in CF .
   It is the caller's responsibility to free the hash table when 
   no longer required.
*/
static struct hsh_table *
create_freq_hash (const struct dictionary *dict, 
		  const struct casefile *cf, 
		  struct casefilter *filter, 
		  const struct variable *var)
{
  bool warn = true;
  struct ccase c;
  struct casereader *r = casefile_get_reader (cf, filter);

  struct hsh_table *freq_hash = 
    hsh_create (4, compare_freq, hash_freq, 
		free_freq_hash,
		(void *) var);

  while (casereader_read(r, &c))
    {
      struct freq **existing_fr;
      struct freq *fr = xmalloc(sizeof  (*fr));
      fr->value = case_data (&c, var );

      if ( casefilter_variable_missing (filter, &c, var))
	{
	  free (fr);
	  continue;
	}

      fr->count = dict_get_case_weight (dict, &c, &warn);

      existing_fr = (struct freq **) hsh_probe (freq_hash, fr);
      if ( *existing_fr) 
	{
	  (*existing_fr)->count += fr->count;
	  free (fr);
	}
      else
	{
	  *existing_fr = fr;
	}

      case_destroy (&c);
    }
  casereader_destroy (r);

  return freq_hash;
}



static struct tab_table *
create_variable_frequency_table (const struct dictionary *dict, 
				 const struct casefile *cf, 
				 struct casefilter *filter,
				 const struct chisquare_test *test, 
				 int v, 
				 struct hsh_table **freq_hash)

{
  int i;
  const struct one_sample_test *ost = (const struct one_sample_test*)test;
  int n_cells;
  struct tab_table *table ;
  const struct variable *var =  ost->vars[v];

  *freq_hash = create_freq_hash (dict, cf, filter, var);
      
  n_cells = hsh_count (*freq_hash);

  if ( test->n_expected > 0 && n_cells != test->n_expected ) 
    {
      msg(ME, _("CHISQUARE test specified %d expected values, but"
		" %d distinct values were encountered in variable %s."), 
	  test->n_expected, n_cells, 
	  var_get_name (var)
	  );
      return NULL;
    }

  table = tab_create(4, n_cells + 2, 0);
  tab_dim (table, tab_natural_dimensions);

  tab_title (table, var_to_string(var));
  tab_text (table, 1, 0, TAB_LEFT, _("Observed N"));
  tab_text (table, 2, 0, TAB_LEFT, _("Expected N"));
  tab_text (table, 3, 0, TAB_LEFT, _("Residual"));
	
  tab_headers (table, 1, 0, 1, 0);

  tab_box (table, TAL_1, TAL_1, -1, -1, 
	   0, 0, table->nc - 1, tab_nr(table) - 1 );

  tab_hline (table, TAL_1, 0, tab_nc(table) - 1, 1);

  tab_vline (table, TAL_2, 1, 0, tab_nr(table) - 1);
  for ( i = 2 ; i < 4 ; ++i ) 
    tab_vline (table, TAL_1, i, 0, tab_nr(table) - 1);


  tab_text (table, 0, table->nr - 1, TAB_LEFT, _("Total"));

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
  tab_dim (table, tab_natural_dimensions);

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
    tab_float (table, 0, 2 + i - test->lo, 
	       TAB_LEFT, 1 + i - test->lo, 8, 0);
	
  tab_headers (table, 1, 0, 2, 0);

  tab_box (table, TAL_1, TAL_1, -1, -1, 
	   0, 0, table->nc - 1, tab_nr(table) - 1 );

  tab_hline (table, TAL_1, 1, tab_nc(table) - 1, 1);
  tab_hline (table, TAL_1, 0, tab_nc(table) - 1, 2);

  tab_text (table, 0, table->nr - 1, TAB_LEFT, _("Total"));

  return table;
}


static struct tab_table *
create_stats_table (const struct chisquare_test *test)
{
  const struct one_sample_test *ost = (const struct one_sample_test*) test;
  
  struct tab_table *table = tab_create (1 + ost->n_vars, 4, 0);
  tab_dim (table, tab_natural_dimensions);
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
		   const struct casefile *cf, 
		   struct casefilter *filter,
		   const struct npar_test *test)
{
  const struct dictionary *dict = dataset_dict (ds);
  int v, i;
  struct one_sample_test *ost = (struct one_sample_test *) test;
  struct chisquare_test *cst = (struct chisquare_test *) test;
  struct tab_table *stats_table = create_stats_table (cst);
  int n_cells = 0;
  double total_expected = 0.0;

  double *df = xzalloc (sizeof (*df) * ost->n_vars);
  double *xsq = xzalloc (sizeof (*df) * ost->n_vars);
  
  for ( i = 0 ; i < cst->n_expected ; ++i ) 
    total_expected += cst->expected[i];

  if ( cst->ranged == false ) 
    {
      for ( v = 0 ; v < ost->n_vars ; ++v ) 
	{
	  double total_obs = 0.0;
	  struct hsh_table *freq_hash = NULL;
	  struct tab_table *freq_table = 
	    create_variable_frequency_table(dict, cf, filter, cst, 
					    v, &freq_hash);

	  struct freq **ff = (struct freq **) hsh_sort (freq_hash);

	  if ( NULL == freq_table ) 
	    {
	      hsh_destroy (freq_hash);
	      continue;
	    }

	  n_cells = hsh_count (freq_hash);

	  for ( i = 0 ; i < n_cells ; ++i ) 
	    total_obs += ff[i]->count;

	  xsq[v] = 0.0;
	  for ( i = 0 ; i < n_cells ; ++i ) 
	    {
	      double exp;
	      const union value *observed_value = ff[i]->value;

	      /* The key */
	      tab_text (freq_table, 0, i + 1, TAB_LEFT, 
			var_get_value_name (ost->vars[v], observed_value));

	      /* The observed N */
	      tab_float (freq_table, 1, i + 1, TAB_NONE,
			 ff[i]->count, 8, 0);

	      if ( cst->n_expected > 0 )
		exp = cst->expected[i] * total_obs / total_expected ; 
	      else
		exp = total_obs / (double) n_cells; 

	      tab_float (freq_table, 2, i + 1, TAB_NONE,
			 exp, 8, 2);

	      /* The residual */
	      tab_float (freq_table, 3, i + 1, TAB_NONE,
			 ff[i]->count - exp, 8, 2);

	      xsq[v] += (ff[i]->count - exp) * (ff[i]->count - exp) / exp;
	    }

	  df[v] = n_cells - 1.0;

	  tab_float (freq_table, 1, i + 1, TAB_NONE,
		     total_obs, 8, 0);

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
	  struct hsh_table *freq_hash = 
	    create_freq_hash_with_range (dict, cf, filter, ost->vars[v], 
					 cst->lo, cst->hi);

	  struct freq **ff = (struct freq **) hsh_sort (freq_hash);

	  assert ( n_cells == hsh_count (freq_hash));

	  for ( i = 0 ; i < hsh_count (freq_hash) ; ++i ) 
	    total_obs += ff[i]->count;

	  xsq[v] = 0.0;
	  for ( i = 0 ; i < hsh_count (freq_hash) ; ++i ) 
	    {
	      double exp;

	      const union value *observed_value = ff[i]->value;

	      /* The key */
	      tab_text  (freq_table, v * 4 + 1, i + 2 , TAB_LEFT, 
			 var_get_value_name (ost->vars[v], observed_value));

	      /* The observed N */
	      tab_float (freq_table, v * 4 + 2, i + 2 , TAB_NONE,
			 ff[i]->count, 8, 0);

	      if ( cst->n_expected > 0 )
		exp = cst->expected[i] * total_obs / total_expected ; 
	      else
		exp = total_obs / (double) hsh_count (freq_hash); 

	      /* The expected N */
	      tab_float (freq_table, v * 4 + 3, i + 2 , TAB_NONE,
			 exp, 8, 2);

	      /* The residual */
	      tab_float (freq_table, v * 4 + 4, i + 2 , TAB_NONE,
			 ff[i]->count - exp, 8, 2);

	      xsq[v] += (ff[i]->count - exp) * (ff[i]->count - exp) / exp;
	    }

	  
	  tab_float (freq_table, v * 4 + 2, tab_nr (freq_table) - 1, TAB_NONE,
		     total_obs, 8, 0);
	  
	  df[v] = n_cells - 1.0;
	  
	  hsh_destroy (freq_hash);
	}

      tab_submit (freq_table);
    }


  /* Populate the summary statistics table */
  for ( v = 0 ; v < ost->n_vars ; ++v ) 
    {
      const struct variable *var = ost->vars[v];

      tab_text (stats_table, 1 + v, 0, TAB_CENTER, var_get_name (var));

      tab_float (stats_table, 1 + v, 1, TAB_NONE, xsq[v], 8,3);
      tab_float (stats_table, 1 + v, 2, TAB_NONE, df[v], 8,0);

      tab_float (stats_table, 1 + v, 3, TAB_NONE, 
		 gsl_cdf_chisq_Q (xsq[v], df[v]), 8,3);
    }

  free (xsq);
  free (df);

  tab_submit (stats_table);
}

