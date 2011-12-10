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
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>


#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"

#include "math/categoricals.h"
#include "math/interaction.h"

#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

struct cell_spec
{
  /* Printable title for output */
  const char *title;

  /* Keyword for syntax */
  const char *keyword;
};

/* Table of cell_specs */
static const struct cell_spec cell_spec[] =
{
  {N_("Means"),          "MEANS"},
  {N_("N"),              "COUNT"},
  {N_("Std. Deviation"), "STDDEV"},
  {N_("Median"),         "MEDIAN"},
  {N_("Group Median"),   "GMEDIAN"},
  {N_("S.E. Mean"),      "SEMEAN"},
  {N_("Sum"),            "SUM"},
  {N_("Min"),            "MIN"},
  {N_("Max"),            "MAX"},
  {N_("Range"),          "RANGE"},
  {N_("Variance"),       "VARIANCE"},
  {N_("Kurtosis"),       "KURTOSIS"},
  {N_("S.E. Kurt"),      "SEKURT"},
  {N_("Skewness"),       "SKEW"},
  {N_("S.E. Skew"),      "SESKEW"},
  {N_("First"),          "FIRST"},
  {N_("Last"),           "LAST"},
  {N_("Percent N"),      "NPCT"},
  {N_("Percent Sum"),    "SPCT"},
  {N_("Harmonic Mean"),  "HARMONIC"},
  {N_("Geom. Mean"),     "GEOMETRIC"}
};

#define n_C (sizeof (cell_spec) / sizeof (struct cell_spec))

struct means
{
  size_t n_dep_vars;
  const struct variable **dep_vars;

  size_t n_interactions;
  struct interaction **interactions;

  size_t *n_factor_vars;
  const struct variable ***factor_vars;

  int ii;

  int n_layers;

  const struct dictionary *dict;

  enum mv_class exclude;

  /* an array  indicating which statistics are to be calculated */
  int *cells;

  /* Size of cells */
  int n_cells;

  struct categoricals *cats;
};


static void
run_means (struct means *cmd, struct casereader *input,
	   const struct dataset *ds);

/* Append all the variables belonging to layer and all subsequent layers
   to iact. And then append iact to the means->interaction.
   This is a recursive function.
 */
static void
iact_append_factor (struct means *means, int layer, const struct interaction *iact)
{
  int v;
  const struct variable **fv ;

  if (layer >= means->n_layers)
    return;

  fv = means->factor_vars[layer];

  for (v = 0; v < means->n_factor_vars[layer]; ++v)
    {
      struct interaction *nexti = interaction_clone (iact);

      interaction_add_variable (nexti, fv[v]);

      iact_append_factor (means, layer + 1, nexti);

      if (layer == means->n_layers - 1)
	{
	  means->interactions[means->ii++] = nexti;
	}
    }
}

int
cmd_means (struct lexer *lexer, struct dataset *ds)
{
  int i;
  int l;
  struct means means;

  means.n_factor_vars = NULL;
  means.factor_vars = NULL;

  means.n_layers = 0;

  means.n_dep_vars = 0;
  means.dict = dataset_dict (ds);

  means.n_cells = 3;
  means.cells = xcalloc (means.n_cells, sizeof (*means.cells));
  
  /* The first three items (MEANS, COUNT, STDDEV) are the default */
  for (i = 0; i < 3 ; ++i)
    means.cells[i] = i;
  

  /*   Optional TABLES =   */
  if (lex_match_id (lexer, "TABLES"))
    {
      lex_force_match (lexer, T_EQUALS);
    }

  /* Dependent variable (s) */
  if (!parse_variables_const (lexer, means.dict,
			      &means.dep_vars, &means.n_dep_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  /* Factor variable (s) */
  while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
    {
      if (lex_match (lexer, T_BY))
	{
	  means.n_layers++;
	  means.factor_vars =
	    xrealloc (means.factor_vars,
		      sizeof (*means.factor_vars) * means.n_layers);
	  means.n_factor_vars =
	    xrealloc (means.n_factor_vars,
		      sizeof (*means.n_factor_vars) * means.n_layers);

	  if (!parse_variables_const (lexer, means.dict,
				      &means.factor_vars[means.n_layers - 1],
				      &means.n_factor_vars[means.n_layers -
							   1],
				      PV_NO_DUPLICATE | PV_NUMERIC))
	    goto error;

	}
    }

  /* /MISSING subcommand */
  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "MISSING"))
	{
	  lex_match (lexer, T_EQUALS);
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  means.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  means.exclude = MV_ANY;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "CELLS"))
	{
	  lex_match (lexer, T_EQUALS);

	  /* The default values become overwritten */
	  means.n_cells = 0;
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      int k;
	      for (k = 0; k < n_C; ++k)
		{
		  if (lex_match_id (lexer, cell_spec[k].keyword))
		    {
		      means.cells =
			xrealloc (means.cells,
				  ++means.n_cells * sizeof (*means.cells));

		      means.cells[means.n_cells - 1] = k;
		      break;
		    }
		}
	      if (k >= n_C)
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


  means.n_interactions = 1;
  for (l = 0; l < means.n_layers; ++l)
    {
      const int n_vars = means.n_factor_vars[l];
      means.n_interactions *= n_vars;
    }

  means.interactions =
    xcalloc (means.n_interactions, sizeof (*means.interactions));

  means.ii = 0;

  iact_append_factor (&means, 0, interaction_create (NULL));

  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;

    grouper = casegrouper_create_splits (proc_open (ds), means.dict);
    while (casegrouper_get_next_group (grouper, &group))
      {
	run_means (&means, group, ds);
      }
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }


  return CMD_SUCCESS;

error:

  free (means.dep_vars);

  return CMD_FAILURE;
}

static void output_case_processing_summary (const struct means *cmd);
static void output_report (const struct means *,
			  const struct interaction *);

static void
run_means (struct means *cmd, struct casereader *input,
	   const struct dataset *ds)
{
  int i;
  const struct variable *wv = dict_get_weight (cmd->dict);
  struct ccase *c;
  struct casereader *reader;

  bool warn_bad_weight = true;

  cmd->cats
    = categoricals_create (cmd->interactions,
			   cmd->n_interactions, wv, cmd->exclude, 0, 0, 0, 0);


  for (reader = casereader_clone (input);
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      double weight = dict_get_case_weight (cmd->dict, c, &warn_bad_weight);

      printf ("%g\n", case_data_idx (c, 0)->f);
      categoricals_update (cmd->cats, c);
    }
  casereader_destroy (reader);

  categoricals_done (cmd->cats);

  output_case_processing_summary (cmd);

  for (i = 0; i < cmd->n_interactions; ++i)
    {
      output_report (cmd, cmd->interactions[i]);
    }
}


static void
output_case_processing_summary (const struct means *cmd)
{
  int i;
  const int heading_columns = 1;
  const int heading_rows = 3;
  struct tab_table *t;

  const int nr = heading_rows + cmd->n_interactions;
  const int nc = 7;

  t = tab_create (nc, nr);
  tab_title (t, _("Case Processing Summary"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, nc - 1, nr - 1);

  tab_hline (t, TAL_2, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);


  tab_joint_text (t, heading_columns, 0,
		  nc - 1, 0, TAB_CENTER | TAT_TITLE, _("Cases"));

  tab_joint_text (t, 1, 1, 2, 1, TAB_CENTER | TAT_TITLE, _("Included"));
  tab_joint_text (t, 3, 1, 4, 1, TAB_CENTER | TAT_TITLE, _("Excluded"));
  tab_joint_text (t, 5, 1, 6, 1, TAB_CENTER | TAT_TITLE, _("Total"));

  tab_hline (t, TAL_1, heading_columns, nc - 1, 1);
  tab_hline (t, TAL_1, heading_columns, nc - 1, 2);


  for (i = 0; i < 3; ++i)
    {
      tab_text (t, heading_columns + i * 2, 2, TAB_CENTER | TAT_TITLE,
		_("N"));
      tab_text (t, heading_columns + i * 2 + 1, 2, TAB_CENTER | TAT_TITLE,
		_("Percent"));
    }

  for (i = 0; i < cmd->n_interactions; ++i)
    {
      const struct interaction *iact = cmd->interactions[i];

      struct string str;
      ds_init_empty (&str);
      interaction_to_string (iact, &str);

      size_t n = categoricals_n_count (cmd->cats, i);

      tab_text (t, 0, i + heading_rows, TAB_LEFT | TAT_TITLE, ds_cstr (&str));

      printf ("Count %d is %d\n", i, n);


      ds_destroy (&str);
    }

  tab_submit (t);
}



static void
output_report (const struct means *cmd, const struct interaction *iact)
{
  int i;
  const int heading_columns = 0;
  const int heading_rows = 1;
  struct tab_table *t;

  const int nr = 18;
  const int nc = heading_columns + iact->n_vars + cmd->n_cells;


  t = tab_create (nc, nr);
  tab_title (t, _("Report"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, nc - 1, nr - 1);

  tab_hline (t, TAL_2, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, iact->n_vars, 0, nr - 1);

  for (i = 0; i < iact->n_vars; ++i)
    {
      tab_text (t, heading_columns + i, 0, TAB_CENTER | TAT_TITLE,
		var_to_string (iact->vars[i]));
    }

  for (i = 0; i < cmd->n_cells; ++i)
    {
      tab_text (t, heading_columns + iact->n_vars + i, 0,
		TAB_CENTER | TAT_TITLE,
		gettext (cell_spec[cmd->cells[i]].title));
    }

  tab_text (t, heading_columns + 1, 5, TAB_CENTER | TAT_TITLE, "data");

  tab_submit (t);
}
