/* PSPP - a program for statistical analysis. -*-c-*-
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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include "language/stats/friedman.h"

#include <gsl/gsl_cdf.h>
#include <math.h>

#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


struct friedman
{
  double *rank_sum;
  double cc;
  double chi_sq;
  double w;
  const struct dictionary *dict;
};

static void show_ranks_box (const struct one_sample_test *ost, 
			    const struct friedman *fr);

static void show_sig_box (const struct one_sample_test *ost,
			  const struct friedman *fr);

struct datum
{
  long posn;
  double x;
};

static int
cmp_x (const void *a_, const void *b_)
{
  const struct datum *a = a_;
  const struct datum *b = b_;

  if (a->x < b->x)
    return -1;
  
  return (a->x > b->x);
}

static int
cmp_posn (const void *a_, const void *b_)
{
  const struct datum *a = a_;
  const struct datum *b = b_;

  if (a->posn < b->posn)
    return -1;
  
  return (a->posn > b->posn);
}

void
friedman_execute (const struct dataset *ds,
		  struct casereader *input,
		  enum mv_class exclude,
		  const struct npar_test *test,
		  bool exact UNUSED,
		  double timer UNUSED)
{
  double numerator = 0.0;
  double denominator = 0.0;
  int v;
  struct ccase *c;
  const struct dictionary *dict = dataset_dict (ds);
  const struct variable *weight = dict_get_weight (dict);

  struct one_sample_test *ost = UP_CAST (test, struct one_sample_test, parent);
  struct friedman_test *ft = UP_CAST (ost, struct friedman_test, parent);
  bool warn = true;

  double sigma_t = 0.0;	
  struct datum *row = xcalloc (ost->n_vars, sizeof *row);
  double rsq;
  struct friedman fr;
  fr.rank_sum = xcalloc (ost->n_vars, sizeof *fr.rank_sum);
  fr.cc = 0.0;
  fr.dict = dict;
  for (v = 0; v < ost->n_vars; ++v)
    {
      row[v].posn = v;
      fr.rank_sum[v] = 0.0;
    }

  input = casereader_create_filter_weight (input, dict, &warn, NULL);
  input = casereader_create_filter_missing (input,
					    ost->vars, ost->n_vars,
					    exclude, 0, 0);

  for (; (c = casereader_read (input)); case_unref (c))
    {
      double prev_x = SYSMIS;
      int run_length = 0;

      const double w = weight ? case_data (c, weight)->f: 1.0;

      fr.cc += w;

      for (v = 0; v < ost->n_vars; ++v)
	{
	  const struct variable *var = ost->vars[v];
	  const union value *val = case_data (c, var);
	  row[v].x = val->f;
	}

      qsort (row, ost->n_vars, sizeof *row, cmp_x);
      for (v = 0; v < ost->n_vars; ++v)
	{
	  double x = row[v].x;
	  /* Replace value by the Rank */
	  if ( prev_x == x)
	    {
	      /* Deal with ties */
	      int i;
	      run_length++;
	      for (i = v - run_length; i < v; ++i)
		{
		  row[i].x *= run_length ;
		  row[i].x += v + 1;
		  row[i].x /= run_length + 1;
		}
	      row[v].x = row[v-1].x;
	    }
	  else
	    {
	      row[v].x = v + 1;
	      if ( run_length > 0)
		{
		  double t = run_length + 1;
		  sigma_t += w * (pow3 (t) - t);
		}
	      run_length = 0;
	    }
	  prev_x = x;
	}
      if ( run_length > 0)
	{
	  double t = run_length + 1;
	  sigma_t += w * (pow3 (t) - t );
	}

      qsort (row, ost->n_vars, sizeof *row, cmp_posn);

      for (v = 0; v < ost->n_vars; ++v)
	fr.rank_sum[v] += row[v].x * w;
    }
  casereader_destroy (input);
  free (row);


  for (v = 0; v < ost->n_vars; ++v)
    {
      numerator += pow2 (fr.rank_sum[v]);
    }

  rsq = numerator;

  numerator *= 12.0 / (fr.cc * ost->n_vars * ( ost->n_vars + 1));
  numerator -= 3 * fr.cc * ( ost->n_vars + 1);

  denominator = 1 - sigma_t / ( fr.cc * ost->n_vars * ( pow2 (ost->n_vars) - 1));

  fr.chi_sq = numerator / denominator;

  if ( ft->kendalls_w)
    {
      fr.w = 12 * rsq ;
      fr.w -= 3 * pow2 (fr.cc) *
	ost->n_vars * pow2 (ost->n_vars + 1);

      fr.w /= pow2 (fr.cc) * (pow3 (ost->n_vars) - ost->n_vars)
	- fr.cc * sigma_t;
    }
  else
    fr.w = SYSMIS;

  show_ranks_box (ost, &fr);
  show_sig_box (ost, &fr);

  free (fr.rank_sum);
}




static void
show_ranks_box (const struct one_sample_test *ost, const struct friedman *fr)
{
  int i;
  const int row_headers = 1;
  const int column_headers = 1;
  struct tab_table *table =
    tab_create (row_headers + 1, column_headers + ost->n_vars);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Ranks"));

  /* Vertical lines inside the box */
  tab_box (table, 1, 0, -1, TAL_1,
	   row_headers, 0, tab_nc (table) - 1, tab_nr (table) - 1 );

  /* Box around the table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );


  tab_text (table, 1, 0, 0, _("Mean Rank"));

  tab_hline (table, TAL_2, 0, tab_nc (table) - 1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);

  for (i = 0 ; i < ost->n_vars ; ++i)
    {
      tab_text (table, 0, row_headers + i,
		TAB_LEFT, var_to_string (ost->vars[i]));

      tab_double (table, 1, row_headers + i,
		  0, fr->rank_sum[i] / fr->cc, NULL, RC_OTHER);
    }

  tab_submit (table);
}


static void
show_sig_box (const struct one_sample_test *ost, const struct friedman *fr)
{
  const struct friedman_test *ft = UP_CAST (ost, const struct friedman_test, parent);
  
  int row = 0;
  const struct variable *weight = dict_get_weight (fr->dict);
  const struct fmt_spec *wfmt = weight ? var_get_print_format (weight) : &F_8_0;

  const int row_headers = 1;
  const int column_headers = 0;
  struct tab_table *table =
    tab_create (row_headers + 1, column_headers + (ft->kendalls_w ? 5 : 4));
  tab_set_format (table, RC_WEIGHT, wfmt);

  tab_headers (table, row_headers, 0, column_headers, 0);

  tab_title (table, _("Test Statistics"));

  tab_text (table,  0, column_headers + row++,
	    TAT_TITLE | TAB_LEFT , _("N"));

  if ( ft->kendalls_w)
    tab_text (table,  0, column_headers + row++,
	      TAT_TITLE | TAB_LEFT , _("Kendall's W"));

  tab_text (table,  0, column_headers + row++,
	    TAT_TITLE | TAB_LEFT , _("Chi-Square"));

  tab_text (table,  0, column_headers + row++,
	    TAT_TITLE | TAB_LEFT, _("df"));

  tab_text (table,  0, column_headers + row++,
	    TAT_TITLE | TAB_LEFT, _("Asymp. Sig."));

  /* Box around the table */
  tab_box (table, TAL_2, TAL_2, -1, -1,
	   0,  0, tab_nc (table) - 1, tab_nr (table) - 1 );


  tab_hline (table, TAL_2, 0, tab_nc (table) -1, column_headers);
  tab_vline (table, TAL_2, row_headers, 0, tab_nr (table) - 1);

  row = 0;
  tab_double (table, 1, column_headers + row++, 
	      0, fr->cc, NULL, RC_WEIGHT);

  if (ft->kendalls_w)
    tab_double (table, 1, column_headers + row++, 
		0, fr->w, NULL, RC_OTHER);

  tab_double (table, 1, column_headers + row++, 
	      0, fr->chi_sq, NULL, RC_OTHER);

  tab_double (table, 1, column_headers + row++, 
	      0, ost->n_vars - 1, NULL, RC_INTEGER);

  tab_double (table, 1, column_headers + row++, 
	      0, gsl_cdf_chisq_Q (fr->chi_sq, ost->n_vars - 1), 
	      NULL, RC_PVALUE);

  tab_submit (table);
}
