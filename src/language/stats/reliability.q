/* PSPP - a program for statistical analysis.
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

#include <math.h>

#include <data/case.h>
#include <data/casegrouper.h>
#include <data/casereader.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/variable.h>
#include <language/command.h>
#include <libpspp/misc.h>
#include <math/moments.h>
#include <output/manager.h>
#include <output/table.h>

#include "xalloc.h"
#include "xmalloca.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* (headers) */

/* (specification)
   reliability (rel_):
     *^variables=varlist("PV_NO_SCRATCH | PV_NUMERIC");
     scale=custom;
     missing=miss:!exclude/include;
     model=custom;
     method=covariance;
     +summary[sum_]=total.
*/
/* (declarations) */
/* (functions) */


static int rel_custom_scale (struct lexer *lexer, struct dataset *ds,
		      struct cmd_reliability *p, void *aux);

static int rel_custom_model (struct lexer *, struct dataset *,
			     struct cmd_reliability *, void *);

int cmd_reliability (struct lexer *lexer, struct dataset *ds);

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


struct reliability
{
  const struct dictionary *dict;
  const struct variable **variables;
  int n_variables;
  enum mv_class exclude;

  struct cronbach *sc;
  int n_sc;

  int total_start;

  struct string scale_name;

  enum model model;
  int split_point;
};


static double
alpha (int k, double sum_of_variances, double variance_of_sums)
{
  return k / ( k - 1.0) * ( 1 - sum_of_variances / variance_of_sums);
}

static void reliability_summary_total (const struct reliability *rel);

static void reliability_statistics (const struct reliability *rel);



static void
run_reliability (struct casereader *group, struct dataset *ds,
		 struct reliability *rel);


int
cmd_reliability (struct lexer *lexer, struct dataset *ds)
{
  int i;
  bool ok = false;
  struct casegrouper *grouper;
  struct casereader *group;
  struct cmd_reliability cmd;

  struct reliability rel = {NULL,
    NULL, 0, MV_ANY, NULL, 0, -1,
    DS_EMPTY_INITIALIZER,
    MODEL_ALPHA, 0};

  cmd.v_variables = NULL;

  if ( ! parse_reliability (lexer, ds, &cmd, &rel) )
    {
      goto done;
    }

  rel.dict = dataset_dict (ds);
  rel.variables = cmd.v_variables;
  rel.n_variables = cmd.n_variables;
  rel.exclude = MV_ANY;


  if (NULL == rel.sc)
    {
      struct cronbach *c;
      /* Create a default Scale */

      rel.n_sc = 1;
      rel.sc = xzalloc (sizeof (struct cronbach) * rel.n_sc);

      ds_init_cstr (&rel.scale_name, "ANY");

      c = &rel.sc[0];
      c->n_items = cmd.n_variables;
      c->items = xzalloc (sizeof (struct variable*) * c->n_items);

      for (i = 0 ; i < c->n_items ; ++i)
	c->items[i] = cmd.v_variables[i];
    }

  if ( cmd.miss == REL_INCLUDE)
    rel.exclude = MV_SYSTEM;

  if ( rel.model == MODEL_SPLIT)
    {
      int i;
      const struct cronbach *s;

      rel.n_sc += 2 ;
      rel.sc = xrealloc (rel.sc, sizeof (struct cronbach) * rel.n_sc);

      s = &rel.sc[0];

      rel.sc[1].n_items =
	(rel.split_point == -1) ? s->n_items / 2 : rel.split_point;

      rel.sc[2].n_items = s->n_items - rel.sc[1].n_items;
      rel.sc[1].items = xzalloc (sizeof (struct variable *)
				 * rel.sc[1].n_items);

      rel.sc[2].items = xzalloc (sizeof (struct variable *) *
				 rel.sc[2].n_items);

      for  (i = 0; i < rel.sc[1].n_items ; ++i)
	rel.sc[1].items[i] = s->items[i];

      while (i < s->n_items)
	{
	  rel.sc[2].items[i - rel.sc[1].n_items] = s->items[i];
	  i++;
	}
    }

  if (cmd.a_summary[REL_SUM_TOTAL])
    {
      int i;
      const int base_sc = rel.n_sc;

      rel.total_start = base_sc;

      rel.n_sc +=  rel.sc[0].n_items ;
      rel.sc = xrealloc (rel.sc, sizeof (struct cronbach) * rel.n_sc);

      for (i = 0 ; i < rel.sc[0].n_items; ++i )
	{
	  int v_src;
	  int v_dest = 0;
	  struct cronbach *s = &rel.sc[i + base_sc];

	  s->n_items = rel.sc[0].n_items - 1;
	  s->items = xzalloc (sizeof (struct variable *) * s->n_items);
	  for (v_src = 0 ; v_src < rel.sc[0].n_items ; ++v_src)
	    {
	      if ( v_src != i)
		s->items[v_dest++] = rel.sc[0].items[v_src];
	    }
	}
    }

  /* Data pass. */
  grouper = casegrouper_create_splits (proc_open (ds), dataset_dict (ds));
  while (casegrouper_get_next_group (grouper, &group))
    {
      run_reliability (group, ds, &rel);

      reliability_statistics (&rel);

      if (cmd.a_summary[REL_SUM_TOTAL])
	reliability_summary_total (&rel);
    }
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  free_reliability (&cmd);

 done:

  /* Free all the stuff */
  for (i = 0 ; i < rel.n_sc; ++i)
    {
      int x;
      struct cronbach *c = &rel.sc[i];
      free (c->items);

      moments1_destroy (c->total);

      if ( c->m)
	for (x = 0 ; x < c->n_items; ++x)
	  moments1_destroy (c->m[x]);

      free (c->m);
    }

  ds_destroy (&rel.scale_name);
  free (rel.sc);

  if (ok)
    return CMD_SUCCESS;

  return CMD_FAILURE;
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


static void case_processing_summary (casenumber n_valid, casenumber n_missing, 
				     const struct dictionary *dict);

static void
run_reliability (struct casereader *input, struct dataset *ds,
		 struct reliability *rel)
{
  int i;
  int si;
  struct ccase *c;
  casenumber n_missing ;
  casenumber n_valid = 0;


  for (si = 0 ; si < rel->n_sc; ++si)
    {
      struct cronbach *s = &rel->sc[si];

      s->m = xzalloc (sizeof (s->m) * s->n_items);
      s->total = moments1_create (MOMENT_VARIANCE);

      for (i = 0 ; i < s->n_items ; ++i )
	s->m[i] = moments1_create (MOMENT_VARIANCE);
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


  {
    struct tab_table *tab = tab_create(1, 1, 0);

    tab_dim (tab, tab_natural_dimensions, NULL);
    tab_flags (tab, SOMF_NO_TITLE );

    tab_text(tab, 0, 0, TAT_PRINTF, "Scale: %s", ds_cstr (&rel->scale_name));

    tab_submit(tab);
  }


  case_processing_summary (n_valid, n_missing, dataset_dict (ds));
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

  struct tab_table *tbl = tab_create (n_cols, n_rows, 0);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions, NULL);

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
reliability_summary_total (const struct reliability *rel)
{
  int i;
  const int n_cols = 5;
  const int heading_columns = 1;
  const int heading_rows = 1;
  const int n_rows = rel->sc[0].n_items + heading_rows ;

  struct tab_table *tbl = tab_create (n_cols, n_rows, 0);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions, NULL);

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
		 mean, NULL);

      tab_double (tbl, 2, heading_rows + i, TAB_RIGHT,
		 s->variance_of_sums, NULL);

      tab_double (tbl, 4, heading_rows + i, TAB_RIGHT,
		 s->alpha, NULL);


      moments1_calculate (rel->sc[0].m[i], &weight, &mean, &var, 0,0);
      cov = rel->sc[0].variance_of_sums + var - s->variance_of_sums;
      cov /= 2.0;

      item_to_total_r = (cov - var) / (sqrt(var) * sqrt (s->variance_of_sums));


      tab_double (tbl, 3, heading_rows + i, TAB_RIGHT,
		 item_to_total_r, NULL);
    }


  tab_submit (tbl);
}


static void
reliability_statistics_model_alpha (struct tab_table *tbl,
				    const struct reliability *rel)
{
  const struct variable *wv = dict_get_weight (rel->dict);
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : & F_8_0;

  const struct cronbach *s = &rel->sc[0];

  tab_text (tbl, 0, 0, TAB_CENTER | TAT_TITLE,
		_("Cronbach's Alpha"));

  tab_text (tbl, 1, 0, TAB_CENTER | TAT_TITLE,
		_("N of items"));

  tab_double (tbl, 0, 1, TAB_RIGHT, s->alpha, NULL);

  tab_double (tbl, 1, 1, TAB_RIGHT, s->n_items, wfmt);
}


static void
reliability_statistics_model_split (struct tab_table *tbl,
				    const struct reliability *rel)
{
  const struct variable *wv = dict_get_weight (rel->dict);
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : & F_8_0;

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



  tab_double (tbl, 3, 0, TAB_RIGHT, rel->sc[1].alpha, NULL);
  tab_double (tbl, 3, 2, TAB_RIGHT, rel->sc[2].alpha, NULL);

  tab_double (tbl, 3, 1, TAB_RIGHT, rel->sc[1].n_items, wfmt);
  tab_double (tbl, 3, 3, TAB_RIGHT, rel->sc[2].n_items, wfmt);

  tab_double (tbl, 3, 4, TAB_RIGHT,
	     rel->sc[1].n_items + rel->sc[2].n_items, wfmt);

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

    tab_double (tbl, 3, 5, TAB_RIGHT, r, NULL);

    /* Equal length Spearman-Brown Coefficient */
    tab_double (tbl, 3, 6, TAB_RIGHT, 2 * r / (1.0 + r), NULL);

    tab_double (tbl, 3, 8, TAB_RIGHT, g, NULL);

    tmp = (1.0 - r*r) * rel->sc[1].n_items * rel->sc[2].n_items /
      pow2 (rel->sc[0].n_items);

    uly = sqrt( pow4 (r) + 4 * pow2 (r) * tmp);
    uly -= pow2 (r);
    uly /= 2 * tmp;

    tab_double (tbl, 3, 7, TAB_RIGHT, uly, NULL);
  }
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
  tbl = tab_create (n_cols, n_rows, 0);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions, NULL);

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

  tab_text (tbl, heading_columns + 1, 0, TAB_CENTER | TAT_TITLE | TAT_PRINTF,
		_("%%"));

  total = n_missing + n_valid;

  tab_double (tbl, 2, heading_rows, TAB_RIGHT,
	     n_valid, wfmt);


  tab_double (tbl, 2, heading_rows + 1, TAB_RIGHT,
	     n_missing, wfmt);


  tab_double (tbl, 2, heading_rows + 2, TAB_RIGHT,
	     total, wfmt);


  tab_double (tbl, 3, heading_rows, TAB_RIGHT,
	     100 * n_valid / (double) total, NULL);


  tab_double (tbl, 3, heading_rows + 1, TAB_RIGHT,
	     100 * n_missing / (double) total, NULL);


  tab_double (tbl, 3, heading_rows + 2, TAB_RIGHT,
	     100 * total / (double) total, NULL);


  tab_submit (tbl);
}

static int
rel_custom_model (struct lexer *lexer, struct dataset *ds UNUSED,
		  struct cmd_reliability *cmd UNUSED, void *aux)
{
  struct reliability *rel = aux;

  if (lex_match_id (lexer, "ALPHA"))
    {
      rel->model = MODEL_ALPHA;
    }
  else if (lex_match_id (lexer, "SPLIT"))
    {
      rel->model = MODEL_SPLIT;
      rel->split_point = -1;
      if ( lex_match (lexer, '('))
	{
	  lex_force_num (lexer);
	  rel->split_point = lex_number (lexer);
	  lex_get (lexer);
	  lex_force_match (lexer, ')');
	}
    }
  else
    return 0;

  return 1;
}



static int
rel_custom_scale (struct lexer *lexer, struct dataset *ds UNUSED,
		  struct cmd_reliability *p, void *aux)
{
  struct const_var_set *vs;
  struct reliability *rel = aux;
  struct cronbach *scale;

  rel->n_sc = 1;
  rel->sc = xzalloc (sizeof (struct cronbach) * rel->n_sc);
  scale = &rel->sc[0];

  if ( ! lex_force_match (lexer, '(')) return 0;

  if ( ! lex_force_string (lexer) ) return 0;

  ds_init_string (&rel->scale_name, lex_tokstr (lexer));

  lex_get (lexer);

  if ( ! lex_force_match (lexer, ')')) return 0;

  lex_match (lexer, '=');

  vs = const_var_set_create_from_array (p->v_variables, p->n_variables);

  if (!parse_const_var_set_vars (lexer, vs, &scale->items, &scale->n_items, 0))
    {
      const_var_set_destroy (vs);
      return 2;
    }

  const_var_set_destroy (vs);
  return 1;
}

/*
   Local Variables:
   mode: c
   End:
*/
