/* Pspp - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/stats/wilcoxon.h"

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
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/sort.h"
#include "math/wilcoxon-sig.h"
#include "output/tab.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

static double
append_difference (const struct ccase *c, casenumber n UNUSED, void *aux)
{
  const variable_pair *vp = aux;

  return case_data (c, (*vp)[0])->f - case_data (c, (*vp)[1])->f;
}

static void show_ranks_box (const struct wilcoxon_state *,
			    const struct two_sample_test *,
			    const struct dictionary *);

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
  const struct two_sample_test *t2s = UP_CAST (test, const struct two_sample_test, parent);

  struct wilcoxon_state *ws = xcalloc (t2s->n_pairs, sizeof *ws);
  const struct variable *weight = dict_get_weight (dict);
  struct variable *weightx = dict_create_internal_var (WEIGHT_IDX, 0);
  struct caseproto *proto;

  input =
    casereader_create_filter_weight (input, dict, &warn, NULL);

  proto = caseproto_create ();
  proto = caseproto_add_width (proto, 0);
  proto = caseproto_add_width (proto, 0);
  if (weight != NULL)
    proto = caseproto_add_width (proto, 0);

  for (i = 0 ; i < t2s->n_pairs; ++i )
    {
      struct casereader *r = casereader_clone (input);
      struct casewriter *writer;
      struct ccase *c;
      struct subcase ordering;
      variable_pair *vp = &t2s->pairs[i];

      ws[i].sign = dict_create_internal_var (0, 0);
      ws[i].absdiff = dict_create_internal_var (1, 0);

      r = casereader_create_filter_missing (r, *vp, 2,
					    exclude,
					    NULL, NULL);

      subcase_init_var (&ordering, ws[i].absdiff, SC_ASCEND);
      writer = sort_create_writer (&ordering, proto);
      subcase_destroy (&ordering);

      for (; (c = casereader_read (r)) != NULL; case_unref (c))
	{
	  struct ccase *output = case_create (proto);
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
  caseproto_unref (proto);

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

  dict_destroy_internal_var (weightx);

  show_ranks_box (ws, t2s, dict);
  show_tests_box (ws, t2s, exact, timer);

  for (i = 0 ; i < t2s->n_pairs; ++i )
    {
      dict_destroy_internal_var (ws[i].sign);
      dict_destroy_internal_var (ws[i].absdiff);
    }

  free (ws);
}




#include "gettext.h"
#define _(msgid) gettext (msgid)

static void
show_ranks_box (const struct wilcoxon_state *ws,
		const struct two_sample_test *t2s,
		const struct dictionary *dict)
{
  size_t i;

  const struct variable *wv = dict_get_weight (dict);
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : & F_8_0;

  struct tab_table *table = tab_create (5, 1 + 4 * t2s->n_pairs);
  tab_set_format (table, RC_WEIGHT, wfmt);

  tab_title (table, _("Ranks"));

  tab_headers (table, 2, 0, 1, 0);

  /* Vertical lines inside the box */
  tab_box (table, 0, 0, -1, TAL_1,
	   1, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  /* Box around entire table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0, 0, tab_nc (table) - 1, tab_nr (table) - 1 );


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

      tab_hline (table, TAL_1, 0, tab_nc (table) - 1, 1 + i * 4);


      tab_text (table, 0, 1 + i * 4, TAB_LEFT, ds_cstr (&pair_name));
      ds_destroy (&pair_name);


      /* N */
      tab_double (table, 2, 1 + i * 4, TAB_RIGHT, ws[i].negatives.n, NULL, RC_WEIGHT);
      tab_double (table, 2, 2 + i * 4, TAB_RIGHT, ws[i].positives.n, NULL, RC_WEIGHT);
      tab_double (table, 2, 3 + i * 4, TAB_RIGHT, ws[i].n_zeros, NULL, RC_WEIGHT);

      tab_double (table, 2, 4 + i * 4, TAB_RIGHT,
		  ws[i].n_zeros + ws[i].positives.n + ws[i].negatives.n, NULL, RC_WEIGHT);

      /* Sums */
      tab_double (table, 4, 1 + i * 4, TAB_RIGHT, ws[i].negatives.sum, NULL, RC_OTHER);
      tab_double (table, 4, 2 + i * 4, TAB_RIGHT, ws[i].positives.sum, NULL, RC_OTHER);


      /* Means */
      tab_double (table, 3, 1 + i * 4, TAB_RIGHT,
		  ws[i].negatives.sum / (double) ws[i].negatives.n, NULL, RC_OTHER);

      tab_double (table, 3, 2 + i * 4, TAB_RIGHT,
		  ws[i].positives.sum / (double) ws[i].positives.n, NULL, RC_OTHER);

    }

  tab_hline (table, TAL_2, 0, tab_nc (table) - 1, 1);
  tab_vline (table, TAL_2, 2, 0, tab_nr (table) - 1);


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
  struct tab_table *table = tab_create (1 + t2s->n_pairs, exact ? 5 : 3);

  tab_title (table, _("Test Statistics"));

  tab_headers (table, 1, 0, 1, 0);

  /* Vertical lines inside the box */
  tab_box (table, 0, 0, -1, TAL_1,
	   0, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  /* Box around entire table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0, 0, tab_nc (table) - 1, tab_nr (table) - 1 );


  tab_text (table,  0, 1,  TAB_LEFT, _("Z"));
  tab_text (table,  0, 2,  TAB_LEFT, _("Asymp. Sig. (2-tailed)"));

  if ( exact )
    {
      tab_text (table,  0, 3,  TAB_LEFT, _("Exact Sig. (2-tailed)"));
      tab_text (table,  0, 4,  TAB_LEFT, _("Exact Sig. (1-tailed)"));

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

      tab_double (table, 1 + i, 1, TAB_RIGHT, z, NULL, RC_OTHER);

      tab_double (table, 1 + i, 2, TAB_RIGHT,
		 2.0 * gsl_cdf_ugaussian_P (z),
		  NULL, RC_PVALUE);

      if (exact)
	{
	  double p = LevelOfSignificanceWXMPSR (ws[i].positives.sum, n);
	  if (p < 0)
	    {
	      msg (MW, _("Too many pairs to calculate exact significance."));
	    }
	  else
	    {
	      tab_double (table, 1 + i, 3, TAB_RIGHT, p, NULL, RC_PVALUE);
	      tab_double (table, 1 + i, 4, TAB_RIGHT, p / 2.0, NULL, RC_PVALUE);
	    }
	}
    }

  tab_hline (table, TAL_2, 0, tab_nc (table) - 1, 1);
  tab_vline (table, TAL_2, 1, 0, tab_nr (table) - 1);


  tab_submit (table);
}
