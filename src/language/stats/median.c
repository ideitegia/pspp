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
   along with this program.  If not, see <http://www.gnu.org/licenses/>. 
*/

#include <config.h>
#include "median.h"

#include <gsl/gsl_cdf.h>

#include "data/format.h"


#include "data/variable.h"
#include "data/case.h"
#include "data/dictionary.h"
#include "data/dataset.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/subcase.h"
#include "data/value.h"

#include "math/percentiles.h"
#include "math/sort.h"

#include "libpspp/cast.h"
#include "libpspp/hmap.h"
#include "libpspp/array.h"
#include "libpspp/str.h"
#include "libpspp/misc.h"

#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


struct val_node
{
  struct hmap_node node;
  union value val;
  casenumber le;
  casenumber gt;
};

struct results
{
  const struct variable *var;
  struct val_node **sorted_array;
  double n;
  double median;
  double chisq;  
};



static int 
val_node_cmp_3way (const void *a_, const void *b_, const void *aux)
{
  const struct variable *indep_var = aux;
  const struct val_node *const *a = a_;
  const struct val_node *const *b = b_;

  return value_compare_3way (&(*a)->val, &(*b)->val, var_get_width (indep_var));
}

static void 
show_frequencies (const struct n_sample_test *nst, const struct results *results,  int n_vals, const struct dictionary *);

static void 
show_test_statistics (const struct n_sample_test *nst, const struct results *results, int, const struct dictionary *);


static struct val_node *
find_value (const struct hmap *map, const union value *val, 
	    const struct variable *var)
{
  struct val_node *foo = NULL;
  size_t hash = value_hash (val, var_get_width (var), 0);
  HMAP_FOR_EACH_WITH_HASH (foo, struct val_node, node, hash, map)
    if (value_equal (val, &foo->val, var_get_width (var)))
      break;

  return foo;
}

void
median_execute (const struct dataset *ds,
		struct casereader *input,
		enum mv_class exclude,
		const struct npar_test *test,
		bool exact UNUSED,
		double timer UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);
  const struct variable *wvar = dict_get_weight (dict);
  bool warn = true;
  int v;
  const struct median_test *mt = UP_CAST (test, const struct median_test,
					  parent.parent);

  const struct n_sample_test *nst = UP_CAST (test, const struct n_sample_test,
					  parent);

  const bool n_sample_test = (value_compare_3way (&nst->val2, &nst->val1,
				       var_get_width (nst->indep_var)) > 0);

  struct results *results = xcalloc (nst->n_vars, sizeof (*results));
  int n_vals = 0;
  for (v = 0; v < nst->n_vars; ++v)
    {
      double count = 0;
      double cc = 0;
      double median = mt->median;
      const struct variable *var = nst->vars[v];
      struct ccase *c;
      struct hmap map = HMAP_INITIALIZER (map);
      struct casereader *r = casereader_clone (input);



      if (n_sample_test == false)
	{
	  struct val_node *vn = xzalloc (sizeof *vn);
	  value_clone (&vn->val,  &nst->val1, var_get_width (nst->indep_var));
	  hmap_insert (&map, &vn->node, value_hash (&nst->val1,
					    var_get_width (nst->indep_var), 0));

	  vn = xzalloc (sizeof *vn);
	  value_clone (&vn->val,  &nst->val2, var_get_width (nst->indep_var));
	  hmap_insert (&map, &vn->node, value_hash (&nst->val2,
					    var_get_width (nst->indep_var), 0));
	}

      if ( median == SYSMIS)
	{
	  struct percentile *ptl;
	  struct order_stats *os;

	  struct casereader *rr;
	  struct subcase sc;
	  struct casewriter *writer;
	  subcase_init_var (&sc, var, SC_ASCEND);
	  rr = casereader_clone (r);
	  writer = sort_create_writer (&sc, casereader_get_proto (rr));

	  for (; (c = casereader_read (rr)) != NULL; )
	    {
	      if ( var_is_value_missing (var, case_data (c, var), exclude))
		{
		  case_unref (c);
		  continue;
		}

	      cc += dict_get_case_weight (dict, c, &warn);
	      casewriter_write (writer, c);
	    }
	  subcase_destroy (&sc);
	  casereader_destroy (rr);

	  rr = casewriter_make_reader (writer);

	  ptl = percentile_create (0.5, cc);
	  os = &ptl->parent;
	    
	  order_stats_accumulate (&os, 1,
				  rr,
				  wvar,
				  var,
				  exclude);

	  median = percentile_calculate (ptl, PC_HAVERAGE);
	  statistic_destroy (&ptl->parent.parent);
	}

      results[v].median = median;
      

      for (; (c = casereader_read (r)) != NULL; case_unref (c))
	{
	  struct val_node *vn ;
	  const double weight = dict_get_case_weight (dict, c, &warn);
	  const union value *val = case_data (c, var);
	  const union value *indep_val = case_data (c, nst->indep_var);

	  if ( var_is_value_missing (var, case_data (c, var), exclude))
	    {
	      continue;
	    }

	  if (n_sample_test)
	    {
	      int width = var_get_width (nst->indep_var);
	      /* Ignore out of range values */
	      if (
		  value_compare_3way (indep_val, &nst->val1, width) < 0
		||
		  value_compare_3way (indep_val, &nst->val2, width) > 0
		   )
		{
		  continue;
		}
	    }

	  vn = find_value (&map, indep_val, nst->indep_var);
	  if ( vn == NULL)
	    {
	      if ( n_sample_test == true)
		{
		  int width = var_get_width (nst->indep_var);
		  vn = xzalloc (sizeof *vn);
		  value_clone (&vn->val,  indep_val, width);
		  
		  hmap_insert (&map, &vn->node, value_hash (indep_val, width, 0));
		}
	      else
		{
		  continue;
		}
	    }

	  if (val->f <= median)
	    vn->le += weight;
	  else
	    vn->gt += weight;

	  count += weight;
	}
      casereader_destroy (r);

      {
	int x = 0;
	struct val_node *vn = NULL;
	double r_0 = 0;
	double r_1 = 0;
	HMAP_FOR_EACH (vn, struct val_node, node, &map)
	  {
	    r_0 += vn->le;
	    r_1 += vn->gt;
	  }

	results[v].n = count;
	results[v].sorted_array = xcalloc (hmap_count (&map), sizeof (void*));
	results[v].var = var;

	HMAP_FOR_EACH (vn, struct val_node, node, &map)
	  {
	    double e_0j = r_0 * (vn->le + vn->gt) / count;
	    double e_1j = r_1 * (vn->le + vn->gt) / count;

	    results[v].chisq += pow2 (vn->le - e_0j) / e_0j;
	    results[v].chisq += pow2 (vn->gt - e_1j) / e_1j;

	    results[v].sorted_array[x++] = vn;
	  }

	n_vals = x;
	hmap_destroy (&map);

	sort (results[v].sorted_array, x, sizeof (*results[v].sorted_array), 
	      val_node_cmp_3way, nst->indep_var);

      }
    }

  casereader_destroy (input);

  show_frequencies (nst, results,  n_vals, dict);
  show_test_statistics (nst, results, n_vals, dict);

  for (v = 0; v < nst->n_vars; ++v)
    {
      int i;
      const struct results *rs = results + v;

      for (i = 0; i < n_vals; ++i)
	{
	  struct val_node *vn = rs->sorted_array[i];
	  value_destroy (&vn->val, var_get_width (nst->indep_var));
	  free (vn);
	}
      free (rs->sorted_array);
    }
  free (results);
}



static void 
show_frequencies (const struct n_sample_test *nst, const struct results *results,  int n_vals, const struct dictionary *dict)
{
  const struct variable *weight = dict_get_weight (dict);
  const struct fmt_spec *wfmt = weight ? var_get_print_format (weight) : &F_8_0;

  int i;
  int v;

  const int row_headers = 2;
  const int column_headers = 2;
  const int nc = row_headers + n_vals;
  const int nr = column_headers + nst->n_vars * 2;
    
  struct tab_table *table = tab_create (nc, nr);
  tab_set_format (table, RC_WEIGHT, wfmt);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Frequencies"));

  /* Box around the table and vertical lines inside*/
  tab_box (table, TAL_2, TAL_2, -1, TAL_1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );

  tab_hline (table, TAL_2, 0, tab_nc (table) -1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);

  tab_joint_text (table,
		  row_headers, 0, row_headers + n_vals - 1, 0,
		  TAT_TITLE | TAB_CENTER, var_to_string (nst->indep_var));


  tab_hline (table, TAL_1, row_headers, tab_nc (table) - 1, 1);


  for (i = 0; i < n_vals; ++i)
    {
      const struct results *rs = results + 0;
      struct string label;
      ds_init_empty (&label);

      var_append_value_name (nst->indep_var, &rs->sorted_array[i]->val,
			    &label);

      tab_text (table, row_headers + i, 1,
		TAT_TITLE | TAB_LEFT, ds_cstr (&label));
  
      ds_destroy (&label);
    }

  for (v = 0; v < nst->n_vars; ++v)
    {
      const struct results *rs = &results[v];
      tab_text (table,  0, column_headers + v * 2,
		TAT_TITLE | TAB_LEFT, var_to_string (rs->var) );

      tab_text (table,  1, column_headers + v * 2,
		TAT_TITLE | TAB_LEFT, _("> Median") );

      tab_text (table,  1, column_headers + v * 2 + 1,
		TAT_TITLE | TAB_LEFT, _("≤ Median") );

      if ( v > 0)
	tab_hline (table, TAL_1, 0, tab_nc (table) - 1, column_headers + v * 2);
    }

  for (v = 0; v < nst->n_vars; ++v)
    {
      int i;
      const struct results *rs = &results[v];

      for (i = 0; i < n_vals; ++i)
	{
	  const struct val_node *vn = rs->sorted_array[i];
	  tab_double (table, row_headers + i, column_headers + v * 2,
		      0, vn->gt, NULL, RC_WEIGHT);

	  tab_double (table, row_headers + i, column_headers + v * 2 + 1,
		      0, vn->le, NULL, RC_WEIGHT);
	}
    }

  tab_submit (table);
}


static void 
show_test_statistics (const struct n_sample_test *nst,
		      const struct results *results,
		      int n_vals,
		      const struct dictionary *dict)
{
  const struct variable *weight = dict_get_weight (dict);
  const struct fmt_spec *wfmt = weight ? var_get_print_format (weight) : &F_8_0;

  int v;

  const int row_headers = 1;
  const int column_headers = 1;
  const int nc = row_headers + 5;
  const int nr = column_headers + nst->n_vars;
    
  struct tab_table *table = tab_create (nc, nr);
  tab_set_format (table, RC_WEIGHT, wfmt);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Test Statistics"));


  tab_box (table, TAL_2, TAL_2, -1, TAL_1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );

  tab_hline (table, TAL_2, 0, tab_nc (table) -1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);

  tab_text (table, row_headers + 0, 0,
	    TAT_TITLE | TAB_CENTER, _("N"));

  tab_text (table, row_headers + 1, 0,
	    TAT_TITLE | TAB_CENTER, _("Median"));

  tab_text (table, row_headers + 2, 0,
	    TAT_TITLE | TAB_CENTER, _("Chi-Square"));

  tab_text (table, row_headers + 3, 0,
	    TAT_TITLE | TAB_CENTER, _("df"));

  tab_text (table, row_headers + 4, 0,
	    TAT_TITLE | TAB_CENTER, _("Asymp. Sig."));


  for (v = 0; v < nst->n_vars; ++v)
    {
      double df = n_vals - 1;
      const struct results *rs = &results[v];
      tab_text (table,  0, column_headers + v,
		TAT_TITLE | TAB_LEFT, var_to_string (rs->var));


      tab_double (table, row_headers + 0, column_headers + v,
		  0, rs->n, NULL, RC_WEIGHT);

      tab_double (table, row_headers + 1, column_headers + v,
		  0, rs->median, NULL, RC_OTHER);

      tab_double (table, row_headers + 2, column_headers + v,
		  0, rs->chisq, NULL, RC_OTHER);

      tab_double (table, row_headers + 3, column_headers + v,
		  0, df, NULL, RC_WEIGHT);

      tab_double (table, row_headers + 4, column_headers + v,
		  0, gsl_cdf_chisq_Q (rs->chisq, df), NULL, RC_PVALUE);
    }
  
  tab_submit (table);
}
