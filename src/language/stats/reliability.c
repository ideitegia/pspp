/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2013 Free Software Foundation, Inc.

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

#include <math.h>

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "math/moments.h"
#include "output/tab.h"
#include "output/text-item.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct cronbach
{
  const struct variable **items;
  size_t n_items;
  double alpha;
  double sum_of_variances;
  double variance_of_sums;
  int totals_idx;          /* Casereader index into the totals */

  struct moments1 **m ;    /* Moments of the items */
  struct moments1 *total ; /* Moments of the totals */
};

#if 0
static void
dump_cronbach (const struct cronbach *s)
{
  int i;
  printf ("N items %d\n", s->n_items);
  for (i = 0 ; i < s->n_items; ++i)
    {
      printf ("%s\n", var_get_name (s->items[i]));
    }

  printf ("Totals idx %d\n", s->totals_idx);

  printf ("scale variance %g\n", s->variance_of_sums);
  printf ("alpha %g\n", s->alpha);
  putchar ('\n');
}
#endif

enum model
  {
    MODEL_ALPHA,
    MODEL_SPLIT
  };


enum summary_opts
  {
    SUMMARY_TOTAL = 0x0001,
  };


struct reliability
{
  const struct variable **variables;
  size_t n_variables;
  enum mv_class exclude;

  struct cronbach *sc;
  int n_sc;

  int total_start;

  struct string scale_name;

  enum model model;
  int split_point;


  enum summary_opts summary;

  const struct variable *wv;
};


static bool run_reliability (struct dataset *ds, const struct reliability *reliability);

static void
reliability_destroy (struct reliability *rel)
{
  int j;
  ds_destroy (&rel->scale_name);
  if (rel->sc)
    for (j = 0; j < rel->n_sc ; ++j)
      {
	int x;
	free (rel->sc[j].items);
        moments1_destroy (rel->sc[j].total);
        if (rel->sc[j].m)
          for (x = 0; x < rel->sc[j].n_items; ++x)
            free (rel->sc[j].m[x]);
	free (rel->sc[j].m);
      }

  free (rel->sc);
  free (rel->variables);
}

int
cmd_reliability (struct lexer *lexer, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);

  struct reliability reliability;
  reliability.n_variables = 0;
  reliability.variables = NULL;
  reliability.model = MODEL_ALPHA;
  reliability.exclude = MV_ANY;
  reliability.summary = 0;
  reliability.n_sc = 0;
  reliability.sc = NULL;
  reliability.wv = dict_get_weight (dict);
  reliability.total_start = 0;
  ds_init_empty (&reliability.scale_name);


  lex_match (lexer, T_SLASH);

  if (!lex_force_match_id (lexer, "VARIABLES"))
    {
      goto error;
    }

  lex_match (lexer, T_EQUALS);

  if (!parse_variables_const (lexer, dict, &reliability.variables, &reliability.n_variables,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  if (reliability.n_variables < 2)
    msg (MW, _("Reliability on a single variable is not useful."));


    {
      int i;
      struct cronbach *c;
      /* Create a default Scale */

      reliability.n_sc = 1;
      reliability.sc = xzalloc (sizeof (struct cronbach) * reliability.n_sc);

      ds_assign_cstr (&reliability.scale_name, "ANY");

      c = &reliability.sc[0];
      c->n_items = reliability.n_variables;
      c->items = xzalloc (sizeof (struct variable*) * c->n_items);

      for (i = 0 ; i < c->n_items ; ++i)
	c->items[i] = reliability.variables[i];
    }



  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "SCALE"))
	{
	  struct const_var_set *vs;
	  if ( ! lex_force_match (lexer, T_LPAREN))
	    goto error;

	  if ( ! lex_force_string (lexer) ) 
	    goto error;

	  ds_assign_substring (&reliability.scale_name, lex_tokss (lexer));

	  lex_get (lexer);

	  if ( ! lex_force_match (lexer, T_RPAREN))
	    goto error;

          lex_match (lexer, T_EQUALS);

	  vs = const_var_set_create_from_array (reliability.variables, reliability.n_variables);

	  free (reliability.sc->items);
	  if (!parse_const_var_set_vars (lexer, vs, &reliability.sc->items, &reliability.sc->n_items, 0))
	    {
	      const_var_set_destroy (vs);
	      goto error;
	    }

	  const_var_set_destroy (vs);
	}
      else if (lex_match_id (lexer, "MODEL"))
	{
          lex_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "ALPHA"))
	    {
	      reliability.model = MODEL_ALPHA;
	    }
	  else if (lex_match_id (lexer, "SPLIT"))
	    {
	      reliability.model = MODEL_SPLIT;
	      reliability.split_point = -1;

	      if ( lex_match (lexer, T_LPAREN))
		{
		  lex_force_num (lexer);
		  reliability.split_point = lex_number (lexer);
		  lex_get (lexer);
		  lex_force_match (lexer, T_RPAREN);
		}
	    }
	  else
	    goto error;
	}
      else if (lex_match_id (lexer, "SUMMARY"))
        {
          lex_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "TOTAL"))
	    {
	      reliability.summary |= SUMMARY_TOTAL;
	    }
	  else if (lex_match (lexer, T_ALL))
	    {
	      reliability.summary = 0xFFFF;
	    }
	  else
	    goto error;
	}
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  reliability.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  reliability.exclude = MV_ANY;
		}
	      else
		{
                  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }

  if ( reliability.model == MODEL_SPLIT)
    {
      int i;
      const struct cronbach *s;

      if ( reliability.split_point >= reliability.n_variables)
        {
          msg (ME, _("The split point must be less than the number of variables"));
          goto error;
        }

      reliability.n_sc += 2 ;
      reliability.sc = xrealloc (reliability.sc, sizeof (struct cronbach) * reliability.n_sc);

      s = &reliability.sc[0];

      reliability.sc[1].n_items =
	(reliability.split_point == -1) ? s->n_items / 2 : reliability.split_point;

      reliability.sc[2].n_items = s->n_items - reliability.sc[1].n_items;
      reliability.sc[1].items = xzalloc (sizeof (struct variable *)
				 * reliability.sc[1].n_items);

      reliability.sc[2].items = xzalloc (sizeof (struct variable *) *
				 reliability.sc[2].n_items);

      for  (i = 0; i < reliability.sc[1].n_items ; ++i)
	reliability.sc[1].items[i] = s->items[i];

      while (i < s->n_items)
	{
	  reliability.sc[2].items[i - reliability.sc[1].n_items] = s->items[i];
	  i++;
	}
    }

  if ( reliability.summary & SUMMARY_TOTAL)
    {
      int i;
      const int base_sc = reliability.n_sc;

      reliability.total_start = base_sc;

      reliability.n_sc +=  reliability.sc[0].n_items ;
      reliability.sc = xrealloc (reliability.sc, sizeof (struct cronbach) * reliability.n_sc);


      for (i = 0 ; i < reliability.sc[0].n_items; ++i )
	{
	  int v_src;
	  int v_dest = 0;
	  struct cronbach *s = &reliability.sc[i + base_sc];

	  s->n_items = reliability.sc[0].n_items - 1;
	  s->items = xzalloc (sizeof (struct variable *) * s->n_items);
	  for (v_src = 0 ; v_src < reliability.sc[0].n_items ; ++v_src)
	    {
	      if ( v_src != i)
		s->items[v_dest++] = reliability.sc[0].items[v_src];
	    }
	}
    }


  if ( ! run_reliability (ds, &reliability)) 
    goto error;

  reliability_destroy (&reliability);
  return CMD_SUCCESS;

 error:
  reliability_destroy (&reliability);
  return CMD_FAILURE;
}


static void
do_reliability (struct casereader *group, struct dataset *ds,
		const struct reliability *rel);


static void reliability_summary_total (const struct reliability *rel);

static void reliability_statistics (const struct reliability *rel);


static bool
run_reliability (struct dataset *ds, const struct reliability *reliability)
{
  struct dictionary *dict = dataset_dict (ds);
  bool ok;
  struct casereader *group;

  struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), dict);
  int si;

  for (si = 0 ; si < reliability->n_sc; ++si)
    {
      struct cronbach *s = &reliability->sc[si];
      int i;

      s->m = xzalloc (sizeof *s->m * s->n_items);
      s->total = moments1_create (MOMENT_VARIANCE);

      for (i = 0 ; i < s->n_items ; ++i )
	s->m[i] = moments1_create (MOMENT_VARIANCE);
    }


  while (casegrouper_get_next_group (grouper, &group))
    {
      do_reliability (group, ds, reliability);

      reliability_statistics (reliability);

      if (reliability->summary & SUMMARY_TOTAL )
	reliability_summary_total (reliability);
    }

  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  return ok;
}





/* Return the sum of all the item variables in S */
static  double
append_sum (const struct ccase *c, casenumber n UNUSED, void *aux)
{
  double sum = 0;
  const struct cronbach *s = aux;

  int v;
  for (v = 0 ; v < s->n_items; ++v)
    {
      sum += case_data (c, s->items[v])->f;
    }

  return sum;
};

static void
case_processing_summary (casenumber n_valid, casenumber n_missing,
			 const struct dictionary *dict);


static double
alpha (int k, double sum_of_variances, double variance_of_sums)
{
  return k / ( k - 1.0) * ( 1 - sum_of_variances / variance_of_sums);
}

static void
do_reliability (struct casereader *input, struct dataset *ds,
		const struct reliability *rel)
{
  int i;
  int si;
  struct ccase *c;
  casenumber n_missing ;
  casenumber n_valid = 0;


  for (si = 0 ; si < rel->n_sc; ++si)
    {
      struct cronbach *s = &rel->sc[si];

      moments1_clear (s->total);

      for (i = 0 ; i < s->n_items ; ++i )
        moments1_clear (s->m[i]);
    }

  input = casereader_create_filter_missing (input,
					    rel->variables,
					    rel->n_variables,
					    rel->exclude,
					    &n_missing,
					    NULL);

  for (si = 0 ; si < rel->n_sc; ++si)
    {
      struct cronbach *s = &rel->sc[si];


      s->totals_idx = caseproto_get_n_widths (casereader_get_proto (input));
      input =
	casereader_create_append_numeric (input, append_sum,
					  s, NULL);
    }

  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    {
      double weight = 1.0;
      n_valid ++;

      for (si = 0; si < rel->n_sc; ++si)
	{
	  struct cronbach *s = &rel->sc[si];

	  for (i = 0 ; i < s->n_items ; ++i )
	    moments1_add (s->m[i], case_data (c, s->items[i])->f, weight);

	  moments1_add (s->total, case_data_idx (c, s->totals_idx)->f, weight);
	}
    }
  casereader_destroy (input);

  for (si = 0; si < rel->n_sc; ++si)
    {
      struct cronbach *s = &rel->sc[si];

      s->sum_of_variances = 0;
      for (i = 0 ; i < s->n_items ; ++i )
	{
	  double weight, mean, variance;
	  moments1_calculate (s->m[i], &weight, &mean, &variance, NULL, NULL);

	  s->sum_of_variances += variance;
	}

      moments1_calculate (s->total, NULL, NULL, &s->variance_of_sums,
			  NULL, NULL);

      s->alpha =
	alpha (s->n_items, s->sum_of_variances, s->variance_of_sums);
    }

  text_item_submit (text_item_create_format (TEXT_ITEM_PARAGRAPH, _("Scale: %s"),
                                             ds_cstr (&rel->scale_name)));

  case_processing_summary (n_valid, n_missing, dataset_dict (ds));
}





static void
case_processing_summary (casenumber n_valid, casenumber n_missing,
			 const struct dictionary *dict)
{
  const struct variable *wv = dict_get_weight (dict);
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : & F_8_0;

  casenumber total;
  int n_cols = 4;
  int n_rows = 4;
  int heading_columns = 2;
  int heading_rows = 1;
  struct tab_table *tbl;
  tbl = tab_create (n_cols, n_rows);
  tab_set_format (tbl, RC_WEIGHT, wfmt);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_title (tbl, _("Case Processing Summary"));

  /* Vertical lines for the data only */
  tab_box (tbl,
	   -1, -1,
	   -1, TAL_1,
	   heading_columns, 0,
	   n_cols - 1, n_rows - 1);

  /* Box around table */
  tab_box (tbl,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);


  tab_hline (tbl, TAL_2, 0, n_cols - 1, heading_rows);

  tab_vline (tbl, TAL_2, heading_columns, 0, n_rows - 1);


  tab_text (tbl, 0, heading_rows, TAB_LEFT | TAT_TITLE,
		_("Cases"));

  tab_text (tbl, 1, heading_rows, TAB_LEFT | TAT_TITLE,
		_("Valid"));

  tab_text (tbl, 1, heading_rows + 1, TAB_LEFT | TAT_TITLE,
		_("Excluded"));

  tab_text (tbl, 1, heading_rows + 2, TAB_LEFT | TAT_TITLE,
		_("Total"));

  tab_text (tbl, heading_columns, 0, TAB_CENTER | TAT_TITLE,
		_("N"));

  tab_text (tbl, heading_columns + 1, 0, TAB_CENTER | TAT_TITLE, _("%"));

  total = n_missing + n_valid;

  tab_double (tbl, 2, heading_rows, TAB_RIGHT,
	     n_valid, NULL, RC_WEIGHT);


  tab_double (tbl, 2, heading_rows + 1, TAB_RIGHT,
	     n_missing, NULL, RC_WEIGHT);


  tab_double (tbl, 2, heading_rows + 2, TAB_RIGHT,
	     total, NULL, RC_WEIGHT);


  tab_double (tbl, 3, heading_rows, TAB_RIGHT,
	      100 * n_valid / (double) total, NULL, RC_OTHER);


  tab_double (tbl, 3, heading_rows + 1, TAB_RIGHT,
	      100 * n_missing / (double) total, NULL, RC_OTHER);


  tab_double (tbl, 3, heading_rows + 2, TAB_RIGHT,
	      100 * total / (double) total, NULL, RC_OTHER);


  tab_submit (tbl);
}



static void
reliability_summary_total (const struct reliability *rel)
{
  int i;
  const int n_cols = 5;
  const int heading_columns = 1;
  const int heading_rows = 1;
  const int n_rows = rel->sc[0].n_items + heading_rows ;
  const struct variable *wv = rel->wv;
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : & F_8_0;
  struct tab_table *tbl = tab_create (n_cols, n_rows);
  tab_set_format (tbl, RC_WEIGHT, wfmt);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_title (tbl, _("Item-Total Statistics"));

  /* Vertical lines for the data only */
  tab_box (tbl,
	   -1, -1,
	   -1, TAL_1,
	   heading_columns, 0,
	   n_cols - 1, n_rows - 1);

  /* Box around table */
  tab_box (tbl,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);


  tab_hline (tbl, TAL_2, 0, n_cols - 1, heading_rows);

  tab_vline (tbl, TAL_2, heading_columns, 0, n_rows - 1);

  tab_text (tbl, 1, 0, TAB_CENTER | TAT_TITLE,
	    _("Scale Mean if Item Deleted"));

  tab_text (tbl, 2, 0, TAB_CENTER | TAT_TITLE,
	    _("Scale Variance if Item Deleted"));

  tab_text (tbl, 3, 0, TAB_CENTER | TAT_TITLE,
	    _("Corrected Item-Total Correlation"));

  tab_text (tbl, 4, 0, TAB_CENTER | TAT_TITLE,
	    _("Cronbach's Alpha if Item Deleted"));


  for (i = 0 ; i < rel->sc[0].n_items; ++i)
    {
      double cov, item_to_total_r;
      double mean, weight, var;

      const struct cronbach *s = &rel->sc[rel->total_start + i];
      tab_text (tbl, 0, heading_rows + i, TAB_LEFT| TAT_TITLE,
		var_to_string (rel->sc[0].items[i]));

      moments1_calculate (s->total, &weight, &mean, &var, 0, 0);

      tab_double (tbl, 1, heading_rows + i, TAB_RIGHT,
		  mean, NULL, RC_OTHER);

      tab_double (tbl, 2, heading_rows + i, TAB_RIGHT,
		  s->variance_of_sums, NULL, RC_OTHER);

      tab_double (tbl, 4, heading_rows + i, TAB_RIGHT,
		  s->alpha, NULL, RC_OTHER);


      moments1_calculate (rel->sc[0].m[i], &weight, &mean, &var, 0,0);
      cov = rel->sc[0].variance_of_sums + var - s->variance_of_sums;
      cov /= 2.0;

      item_to_total_r = (cov - var) / (sqrt(var) * sqrt (s->variance_of_sums));


      tab_double (tbl, 3, heading_rows + i, TAB_RIGHT,
		  item_to_total_r, NULL, RC_OTHER);
    }


  tab_submit (tbl);
}


static void reliability_statistics_model_alpha (struct tab_table *tbl,
						const struct reliability *rel);

static void reliability_statistics_model_split (struct tab_table *tbl,
						const struct reliability *rel);


struct reliability_output_table
{
  int n_cols;
  int n_rows;
  int heading_cols;
  int heading_rows;
  void (*populate) (struct tab_table *, const struct reliability *);
};


static struct reliability_output_table rol[2] =
  {
    { 2, 2, 1, 1, reliability_statistics_model_alpha},
    { 4, 9, 3, 0, reliability_statistics_model_split}
  };

static void
reliability_statistics (const struct reliability *rel)
{
  int n_cols = rol[rel->model].n_cols;
  int n_rows = rol[rel->model].n_rows;
  int heading_columns = rol[rel->model].heading_cols;
  int heading_rows = rol[rel->model].heading_rows;
  const struct variable *wv = rel->wv;
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : & F_8_0;
  struct tab_table *tbl = tab_create (n_cols, n_rows);
  tab_set_format (tbl, RC_WEIGHT, wfmt);

  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_title (tbl, _("Reliability Statistics"));

  /* Vertical lines for the data only */
  tab_box (tbl,
	   -1, -1,
	   -1, TAL_1,
	   heading_columns, 0,
	   n_cols - 1, n_rows - 1);

  /* Box around table */
  tab_box (tbl,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);


  tab_hline (tbl, TAL_2, 0, n_cols - 1, heading_rows);

  tab_vline (tbl, TAL_2, heading_columns, 0, n_rows - 1);

  if ( rel->model == MODEL_ALPHA )
    reliability_statistics_model_alpha (tbl, rel);
  else if (rel->model == MODEL_SPLIT )
    reliability_statistics_model_split (tbl, rel);

  tab_submit (tbl);
}


static void
reliability_statistics_model_alpha (struct tab_table *tbl,
				    const struct reliability *rel)
{
  const struct cronbach *s = &rel->sc[0];

  tab_text (tbl, 0, 0, TAB_CENTER | TAT_TITLE,
		_("Cronbach's Alpha"));

  tab_text (tbl, 1, 0, TAB_CENTER | TAT_TITLE,
		_("N of Items"));

  tab_double (tbl, 0, 1, TAB_RIGHT, s->alpha, NULL, RC_OTHER);

  tab_double (tbl, 1, 1, TAB_RIGHT, s->n_items, NULL, RC_WEIGHT);
}


static void
reliability_statistics_model_split (struct tab_table *tbl,
				    const struct reliability *rel)
{
  tab_text (tbl, 0, 0, TAB_LEFT,
	    _("Cronbach's Alpha"));

  tab_text (tbl, 1, 0, TAB_LEFT,
	    _("Part 1"));

  tab_text (tbl, 2, 0, TAB_LEFT,
	    _("Value"));

  tab_text (tbl, 2, 1, TAB_LEFT,
	    _("N of Items"));

  tab_text (tbl, 1, 2, TAB_LEFT,
	    _("Part 2"));

  tab_text (tbl, 2, 2, TAB_LEFT,
	    _("Value"));

  tab_text (tbl, 2, 3, TAB_LEFT,
	    _("N of Items"));

  tab_text (tbl, 1, 4, TAB_LEFT,
	    _("Total N of Items"));

  tab_text (tbl, 0, 5, TAB_LEFT,
	    _("Correlation Between Forms"));

  tab_text (tbl, 0, 6, TAB_LEFT,
	    _("Spearman-Brown Coefficient"));

  tab_text (tbl, 1, 6, TAB_LEFT,
	    _("Equal Length"));

  tab_text (tbl, 1, 7, TAB_LEFT,
	    _("Unequal Length"));


  tab_text (tbl, 0, 8, TAB_LEFT,
	    _("Guttman Split-Half Coefficient"));



  tab_double (tbl, 3, 0, TAB_RIGHT, rel->sc[1].alpha, NULL, RC_OTHER);
  tab_double (tbl, 3, 2, TAB_RIGHT, rel->sc[2].alpha, NULL, RC_OTHER);

  tab_double (tbl, 3, 1, TAB_RIGHT, rel->sc[1].n_items, NULL, RC_WEIGHT);
  tab_double (tbl, 3, 3, TAB_RIGHT, rel->sc[2].n_items, NULL, RC_WEIGHT);

  tab_double (tbl, 3, 4, TAB_RIGHT,
	     rel->sc[1].n_items + rel->sc[2].n_items, NULL, RC_WEIGHT);

  {
    /* R is the correlation between the two parts */
    double r = rel->sc[0].variance_of_sums -
      rel->sc[1].variance_of_sums -
      rel->sc[2].variance_of_sums ;

    /* Guttman Split Half Coefficient */
    double g = 2 * r / rel->sc[0].variance_of_sums;

    /* Unequal Length Spearman Brown Coefficient, and
     intermediate value used in the computation thereof */
    double uly, tmp;

    r /= sqrt (rel->sc[1].variance_of_sums);
    r /= sqrt (rel->sc[2].variance_of_sums);
    r /= 2.0;

    tab_double (tbl, 3, 5, TAB_RIGHT, r, NULL, RC_OTHER);

    /* Equal length Spearman-Brown Coefficient */
    tab_double (tbl, 3, 6, TAB_RIGHT, 2 * r / (1.0 + r), NULL, RC_OTHER);

    tab_double (tbl, 3, 8, TAB_RIGHT, g, NULL, RC_OTHER);

    tmp = (1.0 - r*r) * rel->sc[1].n_items * rel->sc[2].n_items /
      pow2 (rel->sc[0].n_items);

    uly = sqrt( pow4 (r) + 4 * pow2 (r) * tmp);
    uly -= pow2 (r);
    uly /= 2 * tmp;

    tab_double (tbl, 3, 7, TAB_RIGHT, uly, NULL, RC_OTHER);
  }
}

