/* Pspp - a program for statistical analysis.
   Copyright (C) 2008, 2009 Free Software Foundation, Inc.

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
#include "wilcoxon.h"
#include <data/variable.h>
#include <data/casereader.h>
#include <data/casewriter.h>
#include <data/subcase.h>
#include <math/sort.h>
#include <libpspp/message.h>
#include <xalloc.h>
#include <output/table.h>
#include <data/procedure.h>
#include <data/dictionary.h>
#include <math/wilcoxon-sig.h>
#include <gsl/gsl_cdf.h>
#include <unistd.h>
#include <signal.h>
#include <libpspp/assertion.h>

static double
append_difference (const struct ccase *c, casenumber n UNUSED, void *aux)
{
  const variable_pair *vp = aux;

  return case_data (c, (*vp)[0])->f - case_data (c, (*vp)[1])->f;
}

static void show_ranks_box (const struct wilcoxon_state *,
			    const struct two_sample_test *);

static void show_tests_box (const struct wilcoxon_state *,
			    const struct two_sample_test *,
			    bool exact, double timer);



static void
distinct_callback (double v UNUSED, casenumber n, double w UNUSED, void *aux)
{
  struct wilcoxon_state *ws = aux;

  ws->tiebreaker += pow3 (n) - n;
}

#define WEIGHT_IDX 2

void
wilcoxon_execute (const struct dataset *ds,
		  struct casereader *input,
		  enum mv_class exclude,
		  const struct npar_test *test,
		  bool exact,
		  double timer)
{
  int i;
  bool warn = true;
  const struct dictionary *dict = dataset_dict (ds);
  const struct two_sample_test *t2s = (struct two_sample_test *) test;

  struct wilcoxon_state *ws = xcalloc (sizeof (*ws), t2s->n_pairs);
  const struct variable *weight = dict_get_weight (dict);
  struct variable *weightx = var_create_internal (WEIGHT_IDX);

  input =
    casereader_create_filter_weight (input, dict, &warn, NULL);

  for (i = 0 ; i < t2s->n_pairs; ++i )
    {
      struct casereader *r = casereader_clone (input);
      struct casewriter *writer;
      struct ccase *c;
      struct subcase ordering;
      variable_pair *vp = &t2s->pairs[i];

      const int reader_width = weight ? 3 : 2;

      ws[i].sign = var_create_internal (0);
      ws[i].absdiff = var_create_internal (1);

      r = casereader_create_filter_missing (r, *vp, 2,
					    exclude,
					    NULL, NULL);

      subcase_init_var (&ordering, ws[i].absdiff, SC_ASCEND);
      writer = sort_create_writer (&ordering, reader_width);
      subcase_destroy (&ordering);

      for (; (c = casereader_read (r)) != NULL; case_unref (c))
	{
	  struct ccase *output = case_create (reader_width);
	  double d = append_difference (c, 0, vp);

	  if (d > 0)
	    {
	      case_data_rw (output, ws[i].sign)->f = 1.0;

	    }
	  else if (d < 0)
	    {
	      case_data_rw (output, ws[i].sign)->f = -1.0;
	    }
	  else
	    {
	      double w = 1.0;
	      if (weight)
		w = case_data (c, weight)->f;

	      /* Central point values should be dropped */
	      ws[i].n_zeros += w;
              case_unref (output);
              continue;
	    }

	  case_data_rw (output, ws[i].absdiff)->f = fabs (d);

	  if (weight)
	   case_data_rw (output, weightx)->f = case_data (c, weight)->f;

	  casewriter_write (writer, output);
	}
      casereader_destroy (r);
      ws[i].reader = casewriter_make_reader (writer);
    }

  for (i = 0 ; i < t2s->n_pairs; ++i )
    {
      struct casereader *rr ;
      struct ccase *c;
      enum rank_error err = 0;

      rr = casereader_create_append_rank (ws[i].reader, ws[i].absdiff,
					  weight ? weightx : NULL, &err,
					  distinct_callback, &ws[i]
					  );

      for (; (c = casereader_read (rr)) != NULL; case_unref (c))
	{
	  double sign = case_data (c, ws[i].sign)->f;
	  double rank = case_data_idx (c, weight ? 3 : 2)->f;
	  double w = 1.0;
	  if (weight)
	    w = case_data (c, weightx)->f;

	  if ( sign > 0 )
	    {
	      ws[i].positives.sum += rank * w;
	      ws[i].positives.n += w;
	    }
	  else if (sign < 0)
	    {
	      ws[i].negatives.sum += rank * w;
	      ws[i].negatives.n += w;
	    }
	  else
	    NOT_REACHED ();
	}

      casereader_destroy (rr);
    }

  casereader_destroy (input);

  var_destroy (weightx);

  show_ranks_box (ws, t2s);
  show_tests_box (ws, t2s, exact, timer);

  for (i = 0 ; i < t2s->n_pairs; ++i )
    {
      var_destroy (ws[i].sign);
      var_destroy (ws[i].absdiff);
    }

  free (ws);
}




#include "gettext.h"
#define _(msgid) gettext (msgid)

static void
show_ranks_box (const struct wilcoxon_state *ws, const struct two_sample_test *t2s)
{
  size_t i;
  struct tab_table *table = tab_create (5, 1 + 4 * t2s->n_pairs, 0);

  tab_dim (table, tab_natural_dimensions);

  tab_title (table, _("Ranks"));

  tab_headers (table, 2, 0, 1, 0);

  /* Vertical lines inside the box */
  tab_box (table, 0, 0, -1, TAL_1,
	   1, 0, table->nc - 1, tab_nr (table) - 1 );

  /* Box around entire table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0, 0, table->nc - 1, tab_nr (table) - 1 );


  tab_text (table,  2, 0,  TAB_CENTER, _("N"));
  tab_text (table,  3, 0,  TAB_CENTER, _("Mean Rank"));
  tab_text (table,  4, 0,  TAB_CENTER, _("Sum of Ranks"));


  for (i = 0 ; i < t2s->n_pairs; ++i)
    {
      variable_pair *vp = &t2s->pairs[i];

      struct string pair_name;
      ds_init_cstr (&pair_name, var_to_string ((*vp)[0]));
      ds_put_cstr (&pair_name, " - ");
      ds_put_cstr (&pair_name, var_to_string ((*vp)[1]));

      tab_text (table, 1, 1 + i * 4, TAB_LEFT, _("Negative Ranks"));
      tab_text (table, 1, 2 + i * 4, TAB_LEFT, _("Positive Ranks"));
      tab_text (table, 1, 3 + i * 4, TAB_LEFT, _("Ties"));
      tab_text (table, 1, 4 + i * 4, TAB_LEFT, _("Total"));

      tab_hline (table, TAL_1, 0, table->nc - 1, 1 + i * 4);


      tab_text (table, 0, 1 + i * 4, TAB_LEFT, ds_cstr (&pair_name));
      ds_destroy (&pair_name);


      /* N */
      tab_float (table, 2, 1 + i * 4, TAB_RIGHT, ws[i].negatives.n, 8, 0);
      tab_float (table, 2, 2 + i * 4, TAB_RIGHT, ws[i].positives.n, 8, 0);
      tab_float (table, 2, 3 + i * 4, TAB_RIGHT, ws[i].n_zeros, 8, 0);

      tab_float (table, 2, 4 + i * 4, TAB_RIGHT,
		 ws[i].n_zeros + ws[i].positives.n + ws[i].negatives.n, 8, 0);

      /* Sums */
      tab_float (table, 4, 1 + i * 4, TAB_RIGHT, ws[i].negatives.sum, 8, 2);
      tab_float (table, 4, 2 + i * 4, TAB_RIGHT, ws[i].positives.sum, 8, 2);


      /* Means */
      tab_float (table, 3, 1 + i * 4, TAB_RIGHT,
		 ws[i].negatives.sum / (double) ws[i].negatives.n, 8, 2);

      tab_float (table, 3, 2 + i * 4, TAB_RIGHT,
		 ws[i].positives.sum / (double) ws[i].positives.n, 8, 2);

    }

  tab_hline (table, TAL_2, 0, table->nc - 1, 1);
  tab_vline (table, TAL_2, 2, 0, table->nr - 1);


  tab_submit (table);
}


static void
show_tests_box (const struct wilcoxon_state *ws,
		const struct two_sample_test *t2s,
		bool exact,
		double timer UNUSED
		)
{
  size_t i;
  struct tab_table *table = tab_create (1 + t2s->n_pairs, exact ? 5 : 3, 0);

  tab_dim (table, tab_natural_dimensions);

  tab_title (table, _("Test Statistics"));

  tab_headers (table, 1, 0, 1, 0);

  /* Vertical lines inside the box */
  tab_box (table, 0, 0, -1, TAL_1,
	   0, 0, table->nc - 1, tab_nr (table) - 1 );

  /* Box around entire table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0, 0, table->nc - 1, tab_nr (table) - 1 );


  tab_text (table,  0, 1,  TAB_LEFT, _("Z"));
  tab_text (table,  0, 2,  TAB_LEFT, _("Asymp. Sig (2-tailed)"));

  if ( exact )
    {
      tab_text (table,  0, 3,  TAB_LEFT, _("Exact Sig (2-tailed)"));
      tab_text (table,  0, 4,  TAB_LEFT, _("Exact Sig (1-tailed)"));

#if 0
      tab_text (table,  0, 5,  TAB_LEFT, _("Point Probability"));
#endif
    }

  for (i = 0 ; i < t2s->n_pairs; ++i)
    {
      double z;
      double n = ws[i].positives.n + ws[i].negatives.n;
      variable_pair *vp = &t2s->pairs[i];

      struct string pair_name;
      ds_init_cstr (&pair_name, var_to_string ((*vp)[0]));
      ds_put_cstr (&pair_name, " - ");
      ds_put_cstr (&pair_name, var_to_string ((*vp)[1]));


      tab_text (table, 1 + i, 0, TAB_CENTER, ds_cstr (&pair_name));
      ds_destroy (&pair_name);

      z = MIN (ws[i].positives.sum, ws[i].negatives.sum);
      z -= n * (n + 1)/ 4.0;

      z /= sqrt (n * (n + 1) * (2*n + 1)/24.0 - ws[i].tiebreaker / 48.0);

      tab_float (table, 1 + i, 1, TAB_RIGHT, z, 8, 3);

      tab_float (table, 1 + i, 2, TAB_RIGHT,
		 2.0 * gsl_cdf_ugaussian_P (z),
		 8, 3);

      if (exact)
	{
	  double p = LevelOfSignificanceWXMPSR (ws[i].positives.sum, n);
	  if (p < 0)
	    {
	      msg (MW, ("Too many pairs to calculate exact significance."));
	    }
	  else
	    {
	      tab_float (table, 1 + i, 3, TAB_RIGHT, p, 8, 3);
	      tab_float (table, 1 + i, 4, TAB_RIGHT, p / 2.0, 8, 3);
	    }
	}
    }

  tab_hline (table, TAL_2, 0, table->nc - 1, 1);
  tab_vline (table, TAL_2, 1, 0, table->nr - 1);


  tab_submit (table);
}
