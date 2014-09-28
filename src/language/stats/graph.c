/*
  PSPP - a program for statistical analysis.
  Copyright (C) 2012, 2013  Free Software Foundation, Inc.
  
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

/*
 * This module implements the graph command
 */

#include <config.h>

#include <math.h>
#include <gsl/gsl_cdf.h>

#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"


#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/caseproto.h"
#include "data/subcase.h"


#include "data/format.h"

#include "math/chart-geometry.h"
#include "math/histogram.h"
#include "math/moments.h"
#include "math/sort.h"
#include "math/order-stats.h"
#include "output/charts/plot-hist.h"
#include "output/charts/scatterplot.h"

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"

#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum chart_type
  {
    CT_NONE,
    CT_BAR,
    CT_LINE,
    CT_PIE,
    CT_ERRORBAR,
    CT_HILO,
    CT_HISTOGRAM,
    CT_SCATTERPLOT,
    CT_PARETO
  };

enum scatter_type
  {
    ST_BIVARIATE,
    ST_OVERLAY,
    ST_MATRIX,
    ST_XYZ
  };

struct exploratory_stats
{
  double missing;
  double non_missing;

  struct moments *mom;

  double minimum;
  double maximum;

  /* Total weight */
  double cc;

  /* The minimum weight */
  double cmin;
};


struct graph
{
  struct pool *pool;

  size_t n_dep_vars;
  const struct variable **dep_vars;
  struct exploratory_stats *es;

  enum mv_class dep_excl;
  enum mv_class fctr_excl;

  const struct dictionary *dict;

  bool missing_pw;

  /* ------------ Graph ---------------- */
  enum chart_type chart_type;
  enum scatter_type scatter_type;
  const struct variable *byvar;
};


static void
show_scatterplot (const struct graph *cmd, const struct casereader *input)
{
  struct string title;
  struct scatterplot_chart *scatterplot;
  bool byvar_overflow = false;

  ds_init_cstr (&title, var_to_string (cmd->dep_vars[0]));
  ds_put_cstr (&title, " vs ");              
  ds_put_cstr (&title, var_to_string (cmd->dep_vars[1]));
  if (cmd->byvar)
    {
      ds_put_cstr (&title, " by ");                
      ds_put_cstr (&title, var_to_string (cmd->byvar));
    }    

  scatterplot = scatterplot_create(input,
				   cmd->dep_vars[0], 
				   cmd->dep_vars[1],
				   cmd->byvar,
				   &byvar_overflow,
				   ds_cstr (&title),
				   cmd->es[0].minimum, cmd->es[0].maximum,
				   cmd->es[1].minimum, cmd->es[1].maximum);
  scatterplot_chart_submit(scatterplot);
  ds_destroy(&title);

  if (byvar_overflow)
    {
      msg (MW, _("Maximum number of scatterplot categories reached." 
		 "Your BY variable has too many distinct values."
		 "The colouring of the plot will not be correct"));
    }


}

static void
show_histogr (const struct graph *cmd, const struct casereader *input)
{
  struct histogram *histogram;
  struct ccase *c;
  struct casereader *reader;

  {
    /* Sturges Rule */
    double bin_width = fabs (cmd->es[0].minimum - cmd->es[0].maximum)
      / (1 + log2 (cmd->es[0].cc))
      ;

    histogram =
      histogram_create (bin_width, cmd->es[0].minimum, cmd->es[0].maximum);
  }


  for (reader=casereader_clone(input);(c = casereader_read (reader)) != NULL; case_unref (c))
    {
      const struct variable *var = cmd->dep_vars[0];
      const double x = case_data (c, var)->f;
      const double weight = dict_get_case_weight(cmd->dict,c,NULL);
      moments_pass_two (cmd->es[0].mom, x, weight);
      histogram_add (histogram, x, weight);
    }
  casereader_destroy(reader);


  {
    double n, mean, var;

    struct string label;

    ds_init_cstr (&label, 
		  var_to_string (cmd->dep_vars[0]));

    moments_calculate (cmd->es[0].mom, &n, &mean, &var, NULL, NULL);

    chart_item_submit
      ( histogram_chart_create (histogram->gsl_hist,
				ds_cstr (&label), n, mean,
				sqrt (var), false));

    statistic_destroy(&histogram->parent);      
    ds_destroy (&label);
  }
}

static void
cleanup_exploratory_stats (struct graph *cmd)
{ 
  int v;

  for (v = 0; v < cmd->n_dep_vars; ++v)
    {
      moments_destroy (cmd->es[v].mom);
    }
}


static void
run_graph (struct graph *cmd, struct casereader *input)
{
  struct ccase *c;
  struct casereader *reader;


  cmd->es = pool_calloc(cmd->pool,cmd->n_dep_vars,sizeof(struct exploratory_stats));
  for(int v=0;v<cmd->n_dep_vars;v++)
    {
      cmd->es[v].mom = moments_create (MOMENT_KURTOSIS);
      cmd->es[v].cmin = DBL_MAX;
      cmd->es[v].maximum = -DBL_MAX;
      cmd->es[v].minimum =  DBL_MAX;
    }
  /* Always remove cases listwise. This is correct for */
  /* the histogram because there is only one variable  */
  /* and a simple bivariate scatterplot                */
  /* if ( cmd->missing_pw == false)                    */
    input = casereader_create_filter_missing (input,
                                              cmd->dep_vars,
                                              cmd->n_dep_vars,
                                              cmd->dep_excl,
                                              NULL,
                                              NULL);

  for (reader = casereader_clone (input);
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      const double weight = dict_get_case_weight(cmd->dict,c,NULL);      
      for(int v=0;v<cmd->n_dep_vars;v++)
	{
	  const struct variable *var = cmd->dep_vars[v];
	  const double x = case_data (c, var)->f;

	  if (var_is_value_missing (var, case_data (c, var), cmd->dep_excl))
	    {
	      cmd->es[v].missing += weight;
	      continue;
	    }

	  if (x > cmd->es[v].maximum)
	    cmd->es[v].maximum = x;

	  if (x < cmd->es[v].minimum)
	    cmd->es[v].minimum =  x;

	  cmd->es[v].non_missing += weight;

	  moments_pass_one (cmd->es[v].mom, x, weight);

	  cmd->es[v].cc += weight;

	  if (cmd->es[v].cmin > weight)
	    cmd->es[v].cmin = weight;
	}
    }
  casereader_destroy (reader);

  switch (cmd->chart_type)
    {
    case CT_HISTOGRAM:
      reader = casereader_clone(input);
      show_histogr(cmd,reader);
      casereader_destroy(reader);
      break;
    case CT_SCATTERPLOT:
      reader = casereader_clone(input);
      show_scatterplot(cmd,reader);
      casereader_destroy(reader);
      break;
    default:
      NOT_REACHED ();
      break;
    };

  casereader_destroy(input);

  cleanup_exploratory_stats (cmd);
}


int
cmd_graph (struct lexer *lexer, struct dataset *ds)
{
  struct graph graph;

  graph.missing_pw = false;
  
  graph.pool = pool_create ();

  graph.dep_excl = MV_ANY;
  graph.fctr_excl = MV_ANY;
  
  graph.dict = dataset_dict (ds);
  

  /* ---------------- graph ------------------ */
  graph.dep_vars = NULL;
  graph.chart_type = CT_NONE;
  graph.scatter_type = ST_BIVARIATE;
  graph.byvar = NULL;

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id(lexer, "HISTOGRAM"))
	{
	  if (graph.chart_type != CT_NONE)
	    {
	      lex_error(lexer, _("Only one chart type is allowed."));
	      goto error;
	    }
	  if (!lex_force_match (lexer, T_EQUALS))
	    goto error;
	  graph.chart_type = CT_HISTOGRAM;
	  if (!parse_variables_const (lexer, graph.dict,
				      &graph.dep_vars, &graph.n_dep_vars,
				      PV_NO_DUPLICATE | PV_NUMERIC))
	    goto error;
	  if (graph.n_dep_vars > 1)
	    {
	      lex_error(lexer, _("Only one variable allowed"));
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "SCATTERPLOT"))
	{
	  if (graph.chart_type != CT_NONE)
	    {
	      lex_error(lexer, _("Only one chart type is allowed."));
	      goto error;
	    }
	  graph.chart_type = CT_SCATTERPLOT;
	  if (lex_match (lexer, T_LPAREN)) 
	    {
	      if (lex_match_id (lexer, "BIVARIATE"))
		{
		  /* This is the default anyway */
		}
	      else if (lex_match_id (lexer, "OVERLAY"))  
		{
		  lex_error(lexer, _("%s is not yet implemented."),"OVERLAY");
		  goto error;
		}
	      else if (lex_match_id (lexer, "MATRIX"))  
		{
		  lex_error(lexer, _("%s is not yet implemented."),"MATRIX");
		  goto error;
		}
	      else if (lex_match_id (lexer, "XYZ"))  
		{
		  lex_error(lexer, _("%s is not yet implemented."),"XYZ");
		  goto error;
		}
	      else
		{
		  lex_error_expecting(lexer, "BIVARIATE", NULL);
		  goto error;
		}
	      if (!lex_force_match (lexer, T_RPAREN))
		goto error;
	    }
	  if (!lex_force_match (lexer, T_EQUALS))
	    goto error;

	  if (!parse_variables_const (lexer, graph.dict,
				      &graph.dep_vars, &graph.n_dep_vars,
				      PV_NO_DUPLICATE | PV_NUMERIC))
	    goto error;
	 
	  if (graph.scatter_type == ST_BIVARIATE && graph.n_dep_vars != 1)
	    {
	      lex_error(lexer, _("Only one variable allowed"));
	      goto error;
	    }

	  if (!lex_force_match (lexer, T_WITH))
	    goto error;

	  if (!parse_variables_const (lexer, graph.dict,
				      &graph.dep_vars, &graph.n_dep_vars,
				      PV_NO_DUPLICATE | PV_NUMERIC | PV_APPEND))
	    goto error;

	  if (graph.scatter_type == ST_BIVARIATE && graph.n_dep_vars != 2)
	    {
	      lex_error(lexer, _("Only one variable allowed"));
	      goto error;
	    }
	  
	  if (lex_match(lexer, T_BY))
	    {
	      const struct variable *v = NULL;
	      if (!lex_match_variable (lexer,graph.dict,&v))
		{
		  lex_error(lexer, _("Variable expected"));
		  goto error;
		}
	      graph.byvar = v;
	    }
	}
      else if (lex_match_id (lexer, "BAR"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"BAR");
	  goto error;
	}
      else if (lex_match_id (lexer, "LINE"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"LINE");
	  goto error;
	}
      else if (lex_match_id (lexer, "PIE"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"PIE");
	  goto error;
	}
      else if (lex_match_id (lexer, "ERRORBAR"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"ERRORBAR");
	  goto error;
	}
      else if (lex_match_id (lexer, "PARETO"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"PARETO");
	  goto error;
	}
      else if (lex_match_id (lexer, "TITLE"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"TITLE");
	  goto error;
	}
      else if (lex_match_id (lexer, "SUBTITLE"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"SUBTITLE");
	  goto error;
	}
      else if (lex_match_id (lexer, "FOOTNOTE"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"FOOTNOTE");
	  lex_error (lexer, _("FOOTNOTE is not implemented yet for GRAPH"));
	  goto error;
	}
      else if (lex_match_id (lexer, "MISSING"))
        {
	  lex_match (lexer, T_EQUALS);

	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
              if (lex_match_id (lexer, "LISTWISE"))
                {
                  graph.missing_pw = false;
                }
              else if (lex_match_id (lexer, "VARIABLE"))
                {
                  graph.missing_pw = true;
                }
              else if (lex_match_id (lexer, "EXCLUDE"))
                {
                  graph.dep_excl = MV_ANY;
                }
              else if (lex_match_id (lexer, "INCLUDE"))
                {
                  graph.dep_excl = MV_SYSTEM;
                }
              else if (lex_match_id (lexer, "REPORT"))
                {
                  graph.fctr_excl = MV_NEVER;
                }
              else if (lex_match_id (lexer, "NOREPORT"))
                {
                  graph.fctr_excl = MV_ANY;
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

  if (graph.chart_type == CT_NONE)
    {
      lex_error_expecting(lexer,"HISTOGRAM","SCATTERPLOT",NULL);
      goto error;
    }


  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;
    
    grouper = casegrouper_create_splits (proc_open (ds), graph.dict);
    while (casegrouper_get_next_group (grouper, &group))
      run_graph (&graph, group);
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  free (graph.dep_vars);
  pool_destroy (graph.pool);

  return CMD_SUCCESS;

 error:
  free (graph.dep_vars);
  pool_destroy (graph.pool);

  return CMD_FAILURE;
}
