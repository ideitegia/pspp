/* Pspp - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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

#include "kruskal-wallis.h"

#include <gsl/gsl_cdf.h>
#include <math.h>

#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/subcase.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/hmap.h"
#include "libpspp/bt.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/sort.h"
#include "output/tab.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"


/* Returns true iff the independent variable lies in the range [nst->val1, nst->val2] */
static bool
include_func (const struct ccase *c, void *aux)
{
  const struct n_sample_test *nst = aux;

  if (0 < value_compare_3way (&nst->val1, case_data (c, nst->indep_var), var_get_width (nst->indep_var)))
    return false;

  if (0 > value_compare_3way (&nst->val2, case_data (c, nst->indep_var), var_get_width (nst->indep_var)))
    return false;

  return true;
}


struct rank_entry
{
  struct hmap_node node;
  struct bt_node btn;
  union value group;

  double sum_of_ranks;
  double n;
};


static int
compare_rank_entries_3way (const struct bt_node *a,
                           const struct bt_node *b,
                           const void *aux)
{
  const struct variable *var = aux;
  const struct rank_entry *rea = bt_data (a, struct rank_entry, btn);
  const struct rank_entry *reb = bt_data (b, struct rank_entry, btn);

  return value_compare_3way (&rea->group, &reb->group, var_get_width (var));
}


/* Return the entry with the key GROUP or null if there is no such entry */
static struct rank_entry *
find_rank_entry (const struct hmap *map, const union value *group, size_t width)
{
  struct rank_entry *re = NULL;
  size_t hash  = value_hash (group, width, 0);

  HMAP_FOR_EACH_WITH_HASH (re, struct rank_entry, node, hash, map)
    {
      if (0 == value_compare_3way (group, &re->group, width))
	return re;
    }
  
  return re;
}

/* Calculates the adjustment necessary for tie compensation */
static void
distinct_callback (double v UNUSED, casenumber t, double w UNUSED, void *aux)
{
  double *tiebreaker = aux;

  *tiebreaker += pow3 (t) - t;
}


struct kw
{
  struct hmap map;
  double h;
};

static void show_ranks_box (const struct n_sample_test *nst, const struct kw *kw, int n_groups);
static void show_sig_box (const struct n_sample_test *nst, const struct kw *kw);

void
kruskal_wallis_execute (const struct dataset *ds,
			struct casereader *input,
			enum mv_class exclude,
			const struct npar_test *test,
			bool exact UNUSED,
			double timer UNUSED)
{
  int i;
  struct ccase *c;
  bool warn = true;
  const struct dictionary *dict = dataset_dict (ds);
  const struct n_sample_test *nst = UP_CAST (test, const struct n_sample_test, parent);
  const struct caseproto *proto ;
  size_t rank_idx ;

  int total_n_groups = 0.0;

  struct kw *kw = xcalloc (nst->n_vars, sizeof *kw);

  /* If the independent variable is missing, then we ignore the case */
  input = casereader_create_filter_missing (input, 
					    &nst->indep_var, 1,
					    exclude,
					    NULL, NULL);

  input = casereader_create_filter_weight (input, dict, &warn, NULL);

  /* Remove all those cases which are outside the range (val1, val2) */
  input = casereader_create_filter_func (input, include_func, NULL, 
	CONST_CAST (struct n_sample_test *, nst), NULL);

  proto = casereader_get_proto (input);
  rank_idx = caseproto_get_n_widths (proto);

  /* Rank cases by the v value */
  for (i = 0; i < nst->n_vars; ++i)
    {
      double tiebreaker = 0.0;
      bool warn = true;
      enum rank_error rerr = 0;
      struct casereader *rr;
      struct casereader *r = casereader_clone (input);

      r = sort_execute_1var (r, nst->vars[i]);

      /* Ignore missings in the test variable */
      r = casereader_create_filter_missing (r, &nst->vars[i], 1,
					    exclude,
					    NULL, NULL);

      rr = casereader_create_append_rank (r, 
					  nst->vars[i],
					  dict_get_weight (dict),
					  &rerr,
					  distinct_callback, &tiebreaker);

      hmap_init (&kw[i].map);
      for (; (c = casereader_read (rr)); case_unref (c))
	{
	  const union value *group = case_data (c, nst->indep_var);
	  const size_t group_var_width = var_get_width (nst->indep_var);
	  struct rank_entry *rank = find_rank_entry (&kw[i].map, group, group_var_width); 

	  if ( NULL == rank)
	    {
	      rank = xzalloc (sizeof *rank);
	      value_clone (&rank->group, group, group_var_width);

	      hmap_insert (&kw[i].map, &rank->node,
			   value_hash (&rank->group, group_var_width, 0));
	    }

	  rank->sum_of_ranks += case_data_idx (c, rank_idx)->f;
	  rank->n += dict_get_case_weight (dict, c, &warn);

	  /* If this assertion fires, then either the data wasn't sorted or some other
	     problem occured */
	  assert (rerr == 0);
	}

      casereader_destroy (rr);

      /* Calculate the value of h */
      {
	struct rank_entry *mre;
	double n = 0.0;

	HMAP_FOR_EACH (mre, struct rank_entry, node, &kw[i].map)
	  {
	    kw[i].h += pow2 (mre->sum_of_ranks) / mre->n;
	    n += mre->n;

	    total_n_groups ++;
	  }
	kw[i].h *= 12 / (n * ( n + 1));
	kw[i].h -= 3 * (n + 1) ; 

	kw[i].h /= 1 - tiebreaker/ (pow3 (n) - n);
      }
    }

  casereader_destroy (input);
  
  show_ranks_box (nst, kw, total_n_groups);
  show_sig_box (nst, kw);

  /* Cleanup allocated memory */
  for (i = 0 ; i < nst->n_vars; ++i)
    {
      struct rank_entry *mre, *next;
      HMAP_FOR_EACH_SAFE (mre, next, struct rank_entry, node, &kw[i].map)
	{
	  hmap_delete (&kw[i].map, &mre->node);
	  free (mre);
	}
      hmap_destroy (&kw[i].map);
    }

  free (kw);
}


#include "gettext.h"
#define _(msgid) gettext (msgid)


static void
show_ranks_box (const struct n_sample_test *nst, const struct kw *kw, int n_groups)
{
  int row;
  int i;
  const int row_headers = 2;
  const int column_headers = 1;
  struct tab_table *table =
    tab_create (row_headers + 2, column_headers + n_groups + nst->n_vars);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Ranks"));

  /* Vertical lines inside the box */
  tab_box (table, 1, 0, -1, TAL_1,
	   row_headers, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  /* Box around the table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );

  tab_text (table, 1, 0, TAT_TITLE, 
	    var_to_string (nst->indep_var)
	    );

  tab_text (table, 3, 0, 0, _("Mean Rank"));
  tab_text (table, 2, 0, 0, _("N"));

  tab_hline (table, TAL_2, 0, tab_nc (table) -1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);


  row = column_headers;
  for (i = 0 ; i < nst->n_vars ; ++i)
    {
      int tot = 0;
      struct rank_entry *re_x;
      struct bt_node *bt_n = NULL;
      struct bt bt;

      if (i > 0)
	tab_hline (table, TAL_1, 0, tab_nc (table) -1, row);
      
      tab_text (table,  0, row,
		TAT_TITLE, var_to_string (nst->vars[i]));

      /* Sort the rank entries, by iteratin the hash and putting the entries
         into a binary tree. */
      bt_init (&bt, compare_rank_entries_3way, nst->vars[i]);
      HMAP_FOR_EACH (re_x, struct rank_entry, node, &kw[i].map)
	{
          bt_insert (&bt, &re_x->btn);
        }

      /* Report the rank entries in sorted order. */
      for (bt_n = bt_first (&bt);
           bt_n != NULL;
           bt_n = bt_next (&bt, bt_n) )
        {
          const struct rank_entry *re =
            bt_data (bt_n, const struct rank_entry, btn);

	  struct string str;
	  ds_init_empty (&str);
          
	  var_append_value_name (nst->indep_var, &re->group, &str);
          
	  tab_text   (table, 1, row, TAB_LEFT, ds_cstr (&str));
	  tab_double (table, 2, row, TAB_LEFT, re->n, NULL, RC_INTEGER);
	  tab_double (table, 3, row, TAB_LEFT, re->sum_of_ranks / re->n, NULL, RC_OTHER);
          
	  tot += re->n;
	  row++;
	  ds_destroy (&str);
	}

      tab_double (table, 2, row, TAB_LEFT,
		  tot, NULL, RC_INTEGER);
      tab_text (table, 1, row++, TAB_LEFT, _("Total"));
    }

  tab_submit (table);
}


static void
show_sig_box (const struct n_sample_test *nst, const struct kw *kw)
{
  int i;
  const int row_headers = 1;
  const int column_headers = 1;
  struct tab_table *table =
    tab_create (row_headers + nst->n_vars * 2, column_headers + 3);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Test Statistics"));

  tab_text (table,  0, column_headers,
	    TAT_TITLE | TAB_LEFT , _("Chi-Square"));

  tab_text (table,  0, 1 + column_headers,
	    TAT_TITLE | TAB_LEFT, _("df"));

  tab_text (table,  0, 2 + column_headers,
	    TAT_TITLE | TAB_LEFT, _("Asymp. Sig."));

  /* Box around the table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );


  tab_hline (table, TAL_2, 0, tab_nc (table) -1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);

  for (i = 0 ; i < nst->n_vars; ++i)
    {
      const double df = hmap_count (&kw[i].map) - 1;
      tab_text (table, column_headers + 1 + i, 0, TAT_TITLE, 
		var_to_string (nst->vars[i])
		);

      tab_double (table, column_headers + 1 + i, 1, 0,
		  kw[i].h, NULL, RC_OTHER);

      tab_double (table, column_headers + 1 + i, 2, 0,
		  df, NULL, RC_INTEGER);

      tab_double (table, column_headers + 1 + i, 3, 0,
		  gsl_cdf_chisq_Q (kw[i].h, df),
		  NULL, RC_PVALUE);
    }

  tab_submit (table);
}
