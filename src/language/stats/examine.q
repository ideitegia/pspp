/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2009 Free Software Foundation, Inc.

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

#include <gsl/gsl_cdf.h>
#include <libpspp/message.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <math/sort.h>
#include <math/order-stats.h>
#include <math/percentiles.h>
#include <math/tukey-hinges.h>
#include <math/box-whisker.h>
#include <math/trimmed-mean.h>
#include <math/extrema.h>
#include <math/np.h>
#include <data/case.h>
#include <data/casegrouper.h>
#include <data/casereader.h>
#include <data/casewriter.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/subcase.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/dictionary/split-file.h>
#include <language/lexer/lexer.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include <math/moments.h>
#include <output/chart-provider.h>
#include <output/charts/box-whisker.h>
#include <output/charts/cartesian.h>
#include <output/manager.h>
#include <output/table.h>

#include "minmax.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* (headers) */
#include <output/chart.h>
#include <output/charts/plot-hist.h>
#include <output/charts/plot-chart.h>
#include <math/histogram.h>

/* (specification)
   "EXAMINE" (xmn_):
   *^variables=custom;
   +total=custom;
   +nototal=custom;
   missing=miss:pairwise/!listwise,
   rep:report/!noreport,
   incl:include/!exclude;
   +compare=cmp:variables/!groups;
   +percentiles=custom;
   +id=var;
   +plot[plt_]=stemleaf,boxplot,npplot,:spreadlevel(*d:n),histogram,all,none;
   +cinterval=double;
   +statistics[st_]=descriptives,:extreme(*d:n),all,none.
*/

/* (declarations) */

/* (functions) */


static struct cmd_examine cmd;

static const struct variable **dependent_vars;
static size_t n_dependent_vars;

/* PERCENTILES */

static subc_list_double percentile_list;
static enum pc_alg percentile_algorithm;

struct factor_metrics
{
  struct moments1 *moments;

  struct percentile **ptl;
  size_t n_ptiles;

  struct statistic *tukey_hinges;
  struct statistic *box_whisker;
  struct statistic *trimmed_mean;
  struct statistic *histogram;
  struct order_stats *np;

  /* Three quartiles indexing into PTL */
  struct percentile **quartiles;

  /* A reader sorted in ASCENDING order */
  struct casereader *up_reader;

  /* The minimum value of all the weights */
  double cmin;

  /* Sum of all weights, including those for missing values */
  double n;

  /* Sum of weights of non_missing values */
  double n_valid;

  double mean;

  double variance;

  double skewness;

  double kurtosis;

  double se_mean;

  struct extrema *minima;
  struct extrema *maxima;
};

struct factor_result
{
  struct ll ll;

  union value value[2];

  /* An array of factor metrics, one for each variable */
  struct factor_metrics *metrics;
};

struct xfactor
{
  /* We need to make a list of this structure */
  struct ll ll;

  /* The independent variable */
  const struct variable const* indep_var[2];

  /* A list of results for this factor */
  struct ll_list result_list ;
};


static void
factor_destroy (struct xfactor *fctr)
{
  struct ll *ll = ll_head (&fctr->result_list);
  while (ll != ll_null (&fctr->result_list))
    {
      int v;
      struct factor_result *result =
	ll_data (ll, struct factor_result, ll);
      int i;

      for (v = 0; v < n_dependent_vars; ++v)
	{
	  int i;
	  moments1_destroy (result->metrics[v].moments);
	  extrema_destroy (result->metrics[v].minima);
	  extrema_destroy (result->metrics[v].maxima);
	  statistic_destroy (result->metrics[v].trimmed_mean);
	  statistic_destroy (result->metrics[v].tukey_hinges);
	  statistic_destroy (result->metrics[v].box_whisker);
	  statistic_destroy (result->metrics[v].histogram);
	  for (i = 0 ; i < result->metrics[v].n_ptiles; ++i)
	    statistic_destroy ((struct statistic *) result->metrics[v].ptl[i]);
	  free (result->metrics[v].ptl);
	  free (result->metrics[v].quartiles);
	  casereader_destroy (result->metrics[v].up_reader);
	}

      for (i = 0; i < 2; i++)
        if (fctr->indep_var[i])
          value_destroy (&result->value[i],
                         var_get_width (fctr->indep_var[i]));
      free (result->metrics);
      ll = ll_next (ll);
      free (result);
    }
}

static struct xfactor level0_factor;
static struct ll_list factor_list;

/* Parse the clause specifying the factors */
static int examine_parse_independent_vars (struct lexer *lexer,
					   const struct dictionary *dict,
					   struct cmd_examine *cmd);

/* Output functions */
static void show_summary (const struct variable **dependent_var, int n_dep_var,
			  const struct dictionary *dict,
			  const struct xfactor *f);


static void show_descriptives (const struct variable **dependent_var,
			       int n_dep_var,
			       const struct xfactor *f);


static void show_percentiles (const struct variable **dependent_var,
			       int n_dep_var,
			       const struct xfactor *f);


static void show_extremes (const struct variable **dependent_var,
			   int n_dep_var,
			   const struct xfactor *f);




/* Per Split function */
static void run_examine (struct cmd_examine *, struct casereader *,
                         struct dataset *);

static void output_examine (const struct dictionary *dict);


void factor_calc (const struct ccase *c, int case_no,
		  double weight, bool case_missing);


/* Represent a factor as a string, so it can be
   printed in a human readable fashion */
static void factor_to_string (const struct xfactor *fctr,
			      const struct factor_result *result,
			      struct string *str);

/* Represent a factor as a string, so it can be
   printed in a human readable fashion,
   but sacrificing some readablility for the sake of brevity */
static void
factor_to_string_concise (const struct xfactor *fctr,
			  const struct factor_result *result,
			  struct string *str
			  );



/* Categories of missing values to exclude. */
static enum mv_class exclude_values;

int
cmd_examine (struct lexer *lexer, struct dataset *ds)
{
  struct casegrouper *grouper;
  struct casereader *group;
  bool ok;

  subc_list_double_create (&percentile_list);
  percentile_algorithm = PC_HAVERAGE;

  ll_init (&factor_list);

  if ( !parse_examine (lexer, ds, &cmd, NULL) )
    {
      subc_list_double_destroy (&percentile_list);
      return CMD_FAILURE;
    }

  /* If /MISSING=INCLUDE is set, then user missing values are ignored */
  exclude_values = cmd.incl == XMN_INCLUDE ? MV_SYSTEM : MV_ANY;

  if ( cmd.st_n == SYSMIS )
    cmd.st_n = 5;

  if ( ! cmd.sbc_cinterval)
    cmd.n_cinterval[0] = 95.0;

  /* If descriptives have been requested, make sure the
     quartiles are calculated */
  if ( cmd.a_statistics[XMN_ST_DESCRIPTIVES] )
    {
      subc_list_double_push (&percentile_list, 25);
      subc_list_double_push (&percentile_list, 50);
      subc_list_double_push (&percentile_list, 75);
    }

  grouper = casegrouper_create_splits (proc_open (ds), dataset_dict (ds));

  while (casegrouper_get_next_group (grouper, &group))
    {
      struct casereader *reader =
     	casereader_create_arithmetic_sequence (group, 1, 1);

      run_examine (&cmd, reader, ds);
    }

  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  if ( dependent_vars )
    free (dependent_vars);

  subc_list_double_destroy (&percentile_list);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
};


struct np_plot_chart
  {
    struct chart chart;
    char *label;
    struct casereader *data;

    /* Copied directly from struct np. */
    double y_min, y_max;
    double dns_min, dns_max;

    /* Calculated. */
    double slope, intercept;
    double y_first, y_last;
    double x_lower, x_upper;
    double slack;
  };

static const struct chart_class np_plot_chart_class;
static const struct chart_class dnp_plot_chart_class;

/* Plot the normal and detrended normal plots for RESULT.
   Label the plots with LABEL */
static void
np_plot (struct np *np, const char *label)
{
  struct np_plot_chart *np_plot, *dnp_plot;

  if ( np->n < 1.0 )
    {
      msg (MW, _("Not creating plot because data set is empty."));
      return ;
    }

  np_plot = xmalloc (sizeof *np_plot);
  chart_init (&np_plot->chart, &np_plot_chart_class);
  np_plot->label = xstrdup (label);
  np_plot->data = casewriter_make_reader (np->writer);
  np_plot->y_min = np->y_min;
  np_plot->y_max = np->y_max;
  np_plot->dns_min = np->dns_min;
  np_plot->dns_max = np->dns_max;

  /* Slope and intercept of the ideal normal probability line. */
  np_plot->slope = 1.0 / np->stddev;
  np_plot->intercept = -np->mean / np->stddev;

  np_plot->y_first = gsl_cdf_ugaussian_Pinv (1 / (np->n + 1));
  np_plot->y_last = gsl_cdf_ugaussian_Pinv (np->n / (np->n + 1));

  /* Need to make sure that both the scatter plot and the ideal fit into the
     plot */
  np_plot->x_lower = MIN (
    np->y_min, (np_plot->y_first - np_plot->intercept) / np_plot->slope);
  np_plot->x_upper = MAX (
    np->y_max, (np_plot->y_last  - np_plot->intercept) / np_plot->slope) ;
  np_plot->slack = (np_plot->x_upper - np_plot->x_lower) * 0.05 ;

  dnp_plot = xmemdup (np_plot, sizeof *np_plot);
  chart_init (&dnp_plot->chart, &dnp_plot_chart_class);
  dnp_plot->label = xstrdup (dnp_plot->label);
  dnp_plot->data = casereader_clone (dnp_plot->data);

  chart_submit (&np_plot->chart);
  chart_submit (&dnp_plot->chart);
}

static void
np_plot_chart_draw (const struct chart *chart, plPlotter *lp)
{
  const struct np_plot_chart *plot = (struct np_plot_chart *) chart;
  struct chart_geometry geom;
  struct casereader *data;
  struct ccase *c;

  chart_geometry_init (lp, &geom);
  chart_write_title (lp, &geom, _("Normal Q-Q Plot of %s"), plot->label);
  chart_write_xlabel (lp, &geom, _("Observed Value"));
  chart_write_ylabel (lp, &geom, _("Expected Normal"));
  chart_write_xscale (lp, &geom,
                      plot->x_lower - plot->slack,
                      plot->x_upper + plot->slack, 5);
  chart_write_yscale (lp, &geom, plot->y_first, plot->y_last, 5);

  data = casereader_clone (plot->data);
  for (; (c = casereader_read (data)) != NULL; case_unref (c))
    chart_datum (lp, &geom, 0,
                 case_data_idx (c, NP_IDX_Y)->f,
                 case_data_idx (c, NP_IDX_NS)->f);
  casereader_destroy (data);

  chart_line (lp, &geom, plot->slope, plot->intercept,
              plot->y_first, plot->y_last, CHART_DIM_Y);

  chart_geometry_free (lp);
}

static void
dnp_plot_chart_draw (const struct chart *chart, plPlotter *lp)
{
  const struct np_plot_chart *plot = (struct np_plot_chart *) chart;
  struct chart_geometry geom;
  struct casereader *data;
  struct ccase *c;

  chart_geometry_init (lp, &geom);
  chart_write_title (lp, &geom, _("Detrended Normal Q-Q Plot of %s"),
                     plot->label);
  chart_write_xlabel (lp, &geom, _("Observed Value"));
  chart_write_ylabel (lp, &geom, _("Dev from Normal"));
  chart_write_xscale (lp, &geom, plot->y_min, plot->y_max, 5);
  chart_write_yscale (lp, &geom, plot->dns_min, plot->dns_max, 5);

  data = casereader_clone (plot->data);
  for (; (c = casereader_read (data)) != NULL; case_unref (c))
    chart_datum (lp, &geom, 0, case_data_idx (c, NP_IDX_Y)->f,
                 case_data_idx (c, NP_IDX_DNS)->f);
  casereader_destroy (data);

  chart_line (lp, &geom, 0, 0, plot->y_min, plot->y_max, CHART_DIM_X);

  chart_geometry_free (lp);
}

static void
np_plot_chart_destroy (struct chart *chart)
{
  struct np_plot_chart *plot = (struct np_plot_chart *) chart;

  casereader_destroy (plot->data);
  free (plot->label);
  free (plot);
}

static const struct chart_class np_plot_chart_class =
  {
    np_plot_chart_draw,
    np_plot_chart_destroy
  };

static const struct chart_class dnp_plot_chart_class =
  {
    dnp_plot_chart_draw,
    np_plot_chart_destroy
  };


static void
show_npplot (const struct variable **dependent_var,
	     int n_dep_var,
	     const struct xfactor *fctr)
{
  int v;

  for (v = 0; v < n_dep_var; ++v)
    {
      struct ll *ll;
      for (ll = ll_head (&fctr->result_list);
	   ll != ll_null (&fctr->result_list);
	   ll = ll_next (ll))
	{
	  struct string str;
	  const struct factor_result *result =
	    ll_data (ll, struct factor_result, ll);

	  ds_init_empty (&str);
	  ds_put_format (&str, "%s ", var_get_name (dependent_var[v]));

	  factor_to_string (fctr, result, &str);

	  np_plot ((struct np*) result->metrics[v].np, ds_cstr(&str));

	  statistic_destroy ((struct statistic *)result->metrics[v].np);

	  ds_destroy (&str);
	}
    }
}


static void
show_histogram (const struct variable **dependent_var,
		int n_dep_var,
		const struct xfactor *fctr)
{
  int v;

  for (v = 0; v < n_dep_var; ++v)
    {
      struct ll *ll;
      for (ll = ll_head (&fctr->result_list);
	   ll != ll_null (&fctr->result_list);
	   ll = ll_next (ll))
	{
	  struct string str;
	  const struct factor_result *result =
	    ll_data (ll, struct factor_result, ll);
          struct histogram *histogram;
          double mean, var, n;

          histogram = (struct histogram *) result->metrics[v].histogram;
          if (histogram == NULL)
            {
              /* Probably all values are SYSMIS. */
              continue;
            }

	  ds_init_empty (&str);
	  ds_put_format (&str, "%s ", var_get_name (dependent_var[v]));

	  factor_to_string (fctr, result, &str);

          moments1_calculate ((struct moments1 *) result->metrics[v].moments,
                              &n, &mean, &var, NULL,  NULL);
          chart_submit (histogram_chart_create (histogram, ds_cstr (&str),
                                                n, mean, sqrt (var), false));

	  ds_destroy (&str);
	}
    }
}



static void
show_boxplot_groups (const struct variable **dependent_var,
		     int n_dep_var,
		     const struct xfactor *fctr)
{
#if 0
  int v;

  for (v = 0; v < n_dep_var; ++v)
    {
      struct ll *ll;
      int f = 0;
      struct chart *ch = chart_create ();
      double y_min = DBL_MAX;
      double y_max = -DBL_MAX;

      for (ll = ll_head (&fctr->result_list);
	   ll != ll_null (&fctr->result_list);
	   ll = ll_next (ll))
	{
	  const struct extremum  *max, *min;
	  const struct factor_result *result =
	    ll_data (ll, struct factor_result, ll);

	  const struct ll_list *max_list =
	    extrema_list (result->metrics[v].maxima);

	  const struct ll_list *min_list =
	    extrema_list (result->metrics[v].minima);

	  if ( ll_is_empty (max_list))
	    {
	      msg (MW, _("Not creating plot because data set is empty."));
	      continue;
	    }

	  max = (const struct extremum *)
	    ll_data (ll_head(max_list), struct extremum, ll);

          min = (const struct extremum *)
	    ll_data (ll_head (min_list), struct extremum, ll);

	  y_max = MAX (y_max, max->value);
	  y_min = MIN (y_min, min->value);
	}

      boxplot_draw_yscale (ch, y_max, y_min);

      if ( fctr->indep_var[0])
	chart_write_title (ch, _("Boxplot of %s vs. %s"),
			   var_to_string (dependent_var[v]),
			   var_to_string (fctr->indep_var[0]) );
      else
	chart_write_title (ch, _("Boxplot of %s"),
			   var_to_string (dependent_var[v]));

      for (ll = ll_head (&fctr->result_list);
	   ll != ll_null (&fctr->result_list);
	   ll = ll_next (ll))
	{
	  const struct factor_result *result =
	    ll_data (ll, struct factor_result, ll);

	  struct string str;
	  const double box_width = (ch->data_right - ch->data_left)
	    / (ll_count (&fctr->result_list) * 2.0 ) ;

	  const double box_centre = (f++ * 2 + 1) * box_width + ch->data_left;

	  ds_init_empty (&str);
	  factor_to_string_concise (fctr, result, &str);

	  boxplot_draw_boxplot (ch,
				box_centre, box_width,
				(const struct box_whisker *)
				 result->metrics[v].box_whisker,
				ds_cstr (&str));

	  ds_destroy (&str);
	}

      chart_submit (ch);
    }
#endif
}



static void
show_boxplot_variables (const struct variable **dependent_var,
			int n_dep_var,
			const struct xfactor *fctr
			)

{
#if 0
  int v;
  struct ll *ll;
  const struct ll_list *result_list = &fctr->result_list;

  for (ll = ll_head (result_list);
       ll != ll_null (result_list);
       ll = ll_next (ll))

    {
      struct string title;
      struct chart *ch = chart_create ();
      double y_min = DBL_MAX;
      double y_max = -DBL_MAX;

      const struct factor_result *result =
	ll_data (ll, struct factor_result, ll);

      const double box_width = (ch->data_right - ch->data_left)
	/ (n_dep_var * 2.0 ) ;

      for (v = 0; v < n_dep_var; ++v)
	{
	  const struct ll *max_ll =
	    ll_head (extrema_list (result->metrics[v].maxima));
	  const struct ll *min_ll =
	    ll_head (extrema_list (result->metrics[v].minima));

	  const struct extremum  *max =
	    (const struct extremum *) ll_data (max_ll, struct extremum, ll);

          const struct extremum  *min =
	    (const struct extremum *) ll_data (min_ll, struct extremum, ll);

	  y_max = MAX (y_max, max->value);
	  y_min = MIN (y_min, min->value);
	}


      boxplot_draw_yscale (ch, y_max, y_min);

      ds_init_empty (&title);
      factor_to_string (fctr, result, &title);

#if 0
      ds_put_format (&title, "%s = ", var_get_name (fctr->indep_var[0]));
      var_append_value_name (fctr->indep_var[0], &result->value[0], &title);
#endif

      chart_write_title (ch, "%s", ds_cstr (&title));
      ds_destroy (&title);

      for (v = 0; v < n_dep_var; ++v)
	{
	  struct string str;
	  const double box_centre = (v * 2 + 1) * box_width + ch->data_left;

	  ds_init_empty (&str);
	  ds_init_cstr (&str, var_get_name (dependent_var[v]));

	  boxplot_draw_boxplot (ch,
				box_centre, box_width,
				(const struct box_whisker *) result->metrics[v].box_whisker,
				ds_cstr (&str));

	  ds_destroy (&str);
	}

      chart_submit (ch);
    }
#endif
}


/* Show all the appropriate tables */
static void
output_examine (const struct dictionary *dict)
{
  struct ll *ll;

  show_summary (dependent_vars, n_dependent_vars, dict, &level0_factor);

  if ( cmd.a_statistics[XMN_ST_EXTREME] )
    show_extremes (dependent_vars, n_dependent_vars, &level0_factor);

  if ( cmd.a_statistics[XMN_ST_DESCRIPTIVES] )
    show_descriptives (dependent_vars, n_dependent_vars, &level0_factor);

  if ( cmd.sbc_percentiles)
    show_percentiles (dependent_vars, n_dependent_vars, &level0_factor);

  if ( cmd.sbc_plot)
    {
      if (cmd.a_plot[XMN_PLT_BOXPLOT])
	show_boxplot_groups (dependent_vars, n_dependent_vars, &level0_factor);

      if (cmd.a_plot[XMN_PLT_HISTOGRAM])
	show_histogram (dependent_vars, n_dependent_vars, &level0_factor);

      if (cmd.a_plot[XMN_PLT_NPPLOT])
	show_npplot (dependent_vars, n_dependent_vars, &level0_factor);
    }

  for (ll = ll_head (&factor_list);
       ll != ll_null (&factor_list); ll = ll_next (ll))
    {
      struct xfactor *factor = ll_data (ll, struct xfactor, ll);
      show_summary (dependent_vars, n_dependent_vars, dict, factor);

      if ( cmd.a_statistics[XMN_ST_EXTREME] )
	show_extremes (dependent_vars, n_dependent_vars, factor);

      if ( cmd.a_statistics[XMN_ST_DESCRIPTIVES] )
	show_descriptives (dependent_vars, n_dependent_vars, factor);

      if ( cmd.sbc_percentiles)
	show_percentiles (dependent_vars, n_dependent_vars, factor);

      if (cmd.a_plot[XMN_PLT_BOXPLOT] &&
	  cmd.cmp == XMN_GROUPS)
	show_boxplot_groups (dependent_vars, n_dependent_vars, factor);


      if (cmd.a_plot[XMN_PLT_BOXPLOT] &&
	  cmd.cmp == XMN_VARIABLES)
	show_boxplot_variables (dependent_vars, n_dependent_vars,
				factor);

      if (cmd.a_plot[XMN_PLT_HISTOGRAM])
	show_histogram (dependent_vars, n_dependent_vars, factor);

      if (cmd.a_plot[XMN_PLT_NPPLOT])
	show_npplot (dependent_vars, n_dependent_vars, factor);
    }
}

/* Parse the PERCENTILES subcommand */
static int
xmn_custom_percentiles (struct lexer *lexer, struct dataset *ds UNUSED,
			struct cmd_examine *p UNUSED, void *aux UNUSED)
{
  lex_match (lexer, '=');

  lex_match (lexer, '(');

  while ( lex_is_number (lexer) )
    {
      subc_list_double_push (&percentile_list, lex_number (lexer));

      lex_get (lexer);

      lex_match (lexer, ',') ;
    }
  lex_match (lexer, ')');

  lex_match (lexer, '=');

  if ( lex_match_id (lexer, "HAVERAGE"))
    percentile_algorithm = PC_HAVERAGE;

  else if ( lex_match_id (lexer, "WAVERAGE"))
    percentile_algorithm = PC_WAVERAGE;

  else if ( lex_match_id (lexer, "ROUND"))
    percentile_algorithm = PC_ROUND;

  else if ( lex_match_id (lexer, "EMPIRICAL"))
    percentile_algorithm = PC_EMPIRICAL;

  else if ( lex_match_id (lexer, "AEMPIRICAL"))
    percentile_algorithm = PC_AEMPIRICAL;

  else if ( lex_match_id (lexer, "NONE"))
    percentile_algorithm = PC_NONE;


  if ( 0 == subc_list_double_count (&percentile_list))
    {
      subc_list_double_push (&percentile_list, 5);
      subc_list_double_push (&percentile_list, 10);
      subc_list_double_push (&percentile_list, 25);
      subc_list_double_push (&percentile_list, 50);
      subc_list_double_push (&percentile_list, 75);
      subc_list_double_push (&percentile_list, 90);
      subc_list_double_push (&percentile_list, 95);
    }

  return 1;
}

/* TOTAL and NOTOTAL are simple, mutually exclusive flags */
static int
xmn_custom_total (struct lexer *lexer UNUSED, struct dataset *ds UNUSED,
		  struct cmd_examine *p, void *aux UNUSED)
{
  if ( p->sbc_nototal )
    {
      msg (SE, _("%s and %s are mutually exclusive"),"TOTAL","NOTOTAL");
      return 0;
    }

  return 1;
}

static int
xmn_custom_nototal (struct lexer *lexer UNUSED, struct dataset *ds UNUSED,
		    struct cmd_examine *p, void *aux UNUSED)
{
  if ( p->sbc_total )
    {
      msg (SE, _("%s and %s are mutually exclusive"), "TOTAL", "NOTOTAL");
      return 0;
    }

  return 1;
}



/* Parser for the variables sub command
   Returns 1 on success */
static int
xmn_custom_variables (struct lexer *lexer, struct dataset *ds,
		      struct cmd_examine *cmd,
		      void *aux UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);
  lex_match (lexer, '=');

  if ( (lex_token (lexer) != T_ID || dict_lookup_var (dict, lex_tokid (lexer)) == NULL)
       && lex_token (lexer) != T_ALL)
    {
      return 2;
    }

  if (!parse_variables_const (lexer, dict, &dependent_vars, &n_dependent_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC | PV_NO_SCRATCH) )
    {
      free (dependent_vars);
      return 0;
    }

  assert (n_dependent_vars);


  if ( lex_match (lexer, T_BY))
    {
      int success ;
      success =  examine_parse_independent_vars (lexer, dict, cmd);
      if ( success != 1 )
	{
	  free (dependent_vars);
	}
      return success;
    }

  return 1;
}



/* Parse the clause specifying the factors */
static int
examine_parse_independent_vars (struct lexer *lexer,
				const struct dictionary *dict,
				struct cmd_examine *cmd)
{
  int success;
  struct xfactor *sf = xmalloc (sizeof *sf);

  ll_init (&sf->result_list);

  if ( (lex_token (lexer) != T_ID ||
	dict_lookup_var (dict, lex_tokid (lexer)) == NULL)
       && lex_token (lexer) != T_ALL)
    {
      free ( sf ) ;
      return 2;
    }

  sf->indep_var[0] = parse_variable (lexer, dict);
  sf->indep_var[1] = NULL;

  if ( lex_token (lexer) == T_BY )
    {
      lex_match (lexer, T_BY);

      if ( (lex_token (lexer) != T_ID ||
	    dict_lookup_var (dict, lex_tokid (lexer)) == NULL)
	   && lex_token (lexer) != T_ALL)
	{
	  free (sf);
	  return 2;
	}

      sf->indep_var[1] = parse_variable (lexer, dict);

      ll_push_tail (&factor_list, &sf->ll);
    }
  else
    ll_push_tail (&factor_list, &sf->ll);

  lex_match (lexer, ',');

  if ( lex_token (lexer) == '.' || lex_token (lexer) == '/' )
    return 1;

  success =  examine_parse_independent_vars (lexer, dict, cmd);

  if ( success != 1 )
    free ( sf ) ;

  return success;
}

static void
examine_group (struct cmd_examine *cmd, struct casereader *reader, int level,
	       const struct dictionary *dict, struct xfactor *factor)
{
  struct ccase *c;
  const struct variable *wv = dict_get_weight (dict);
  int v;
  int n_extrema = 1;
  struct factor_result *result = xzalloc (sizeof (*result));
  int i;

  for (i = 0; i < 2; i++)
    if (factor->indep_var[i])
      value_init (&result->value[i], var_get_width (factor->indep_var[i]));

  result->metrics = xcalloc (n_dependent_vars, sizeof (*result->metrics));

  if ( cmd->a_statistics[XMN_ST_EXTREME] )
    n_extrema = cmd->st_n;


  c = casereader_peek (reader, 0);
  if (c != NULL)
    {
      if ( level > 0)
        for (i = 0; i < 2; i++)
          if (factor->indep_var[i])
            value_copy (&result->value[i], case_data (c, factor->indep_var[i]),
                        var_get_width (factor->indep_var[i]));
      case_unref (c);
    }

  for (v = 0; v < n_dependent_vars; ++v)
    {
      struct casewriter *writer;
      struct casereader *input = casereader_clone (reader);

      result->metrics[v].moments = moments1_create (MOMENT_KURTOSIS);
      result->metrics[v].minima = extrema_create (n_extrema, EXTREME_MINIMA);
      result->metrics[v].maxima = extrema_create (n_extrema, EXTREME_MAXIMA);
      result->metrics[v].cmin = DBL_MAX;

      if (cmd->a_statistics[XMN_ST_DESCRIPTIVES] ||
	  cmd->a_plot[XMN_PLT_BOXPLOT] ||
	  cmd->a_plot[XMN_PLT_NPPLOT] ||
	  cmd->sbc_percentiles)
	{
	  /* In this case, we need to sort the data, so we create a sorting
	     casewriter */
	  struct subcase up_ordering;
          subcase_init_var (&up_ordering, dependent_vars[v], SC_ASCEND);
	  writer = sort_create_writer (&up_ordering,
				       casereader_get_proto (reader));
          subcase_destroy (&up_ordering);
	}
      else
	{
	  /* but in this case, sorting is unnecessary, so an ordinary
	     casewriter is sufficient */
	  writer =
	    autopaging_writer_create (casereader_get_proto (reader));
	}


      /* Sort or just iterate, whilst calculating moments etc */
      while ((c = casereader_read (input)) != NULL)
	{
          int n_vals = caseproto_get_n_widths (casereader_get_proto (reader));
	  const casenumber loc = case_data_idx (c, n_vals - 1)->f;

	  const double weight = wv ? case_data (c, wv)->f : 1.0;
	  const union value *value = case_data (c, dependent_vars[v]);

	  if (weight != SYSMIS)
	    minimize (&result->metrics[v].cmin, weight);

	  moments1_add (result->metrics[v].moments,
			value->f,
			weight);

	  result->metrics[v].n += weight;

	  if ( ! var_is_value_missing (dependent_vars[v], value, MV_ANY) )
	    result->metrics[v].n_valid += weight;

	  extrema_add (result->metrics[v].maxima,
		       value->f,
		       weight,
		       loc);

	  extrema_add (result->metrics[v].minima,
		       value->f,
		       weight,
		       loc);

	  casewriter_write (writer, c);
	}
      casereader_destroy (input);
      result->metrics[v].up_reader = casewriter_make_reader (writer);
    }

  /* If percentiles or descriptives have been requested, then a
     second pass through the data (which has now been sorted)
     is necessary */
  if ( cmd->a_statistics[XMN_ST_DESCRIPTIVES] ||
       cmd->a_plot[XMN_PLT_BOXPLOT] ||
       cmd->a_plot[XMN_PLT_NPPLOT] ||
       cmd->sbc_percentiles)
    {
      for (v = 0; v < n_dependent_vars; ++v)
	{
	  int i;
	  int n_os;
	  struct order_stats **os ;
	  struct factor_metrics *metric = &result->metrics[v];

	  metric->n_ptiles = percentile_list.n_data;

	  metric->ptl = xcalloc (metric->n_ptiles,
				 sizeof (struct percentile *));

	  metric->quartiles = xcalloc (3, sizeof (*metric->quartiles));

	  for (i = 0 ; i < metric->n_ptiles; ++i)
	    {
	      metric->ptl[i] = (struct percentile *)
		percentile_create (percentile_list.data[i] / 100.0, metric->n_valid);

	      if ( percentile_list.data[i] == 25)
		metric->quartiles[0] = metric->ptl[i];
	      else if ( percentile_list.data[i] == 50)
		metric->quartiles[1] = metric->ptl[i];
	      else if ( percentile_list.data[i] == 75)
		metric->quartiles[2] = metric->ptl[i];
	    }

	  metric->tukey_hinges = tukey_hinges_create (metric->n_valid, metric->cmin);
	  metric->trimmed_mean = trimmed_mean_create (metric->n_valid, 0.05);

	  n_os = metric->n_ptiles + 2;

	 if ( cmd->a_plot[XMN_PLT_NPPLOT] )
	    {
	      metric->np = np_create (metric->moments);
	      n_os ++;
	    }

	  os = xcalloc (sizeof (struct order_stats *), n_os);

	  for (i = 0 ; i < metric->n_ptiles ; ++i )
	    {
	      os[i] = (struct order_stats *) metric->ptl[i];
	    }

	  os[i] = (struct order_stats *) metric->tukey_hinges;
	  os[i+1] = (struct order_stats *) metric->trimmed_mean;

	  if (cmd->a_plot[XMN_PLT_NPPLOT])
	    os[i+2] = metric->np;

	  order_stats_accumulate (os, n_os,
				  casereader_clone (metric->up_reader),
				  wv, dependent_vars[v], MV_ANY);
	  free (os);
	}
    }

  /* FIXME: Do this in the above loop */
  if ( cmd->a_plot[XMN_PLT_HISTOGRAM] )
    {
      struct ccase *c;
      struct casereader *input = casereader_clone (reader);

      for (v = 0; v < n_dependent_vars; ++v)
	{
	  const struct extremum  *max, *min;
	  struct factor_metrics *metric = &result->metrics[v];

	  const struct ll_list *max_list =
	    extrema_list (result->metrics[v].maxima);

	  const struct ll_list *min_list =
	    extrema_list (result->metrics[v].minima);

	  if ( ll_is_empty (max_list))
	    {
	      msg (MW, _("Not creating plot because data set is empty."));
	      continue;
	    }

	  assert (! ll_is_empty (min_list));

	  max = (const struct extremum *)
	    ll_data (ll_head(max_list), struct extremum, ll);

          min = (const struct extremum *)
	    ll_data (ll_head (min_list), struct extremum, ll);

      	  metric->histogram = histogram_create (10, min->value, max->value);
	}

      while ((c = casereader_read (input)) != NULL)
	{
	  const double weight = wv ? case_data (c, wv)->f : 1.0;

	  for (v = 0; v < n_dependent_vars; ++v)
	    {
	      struct factor_metrics *metric = &result->metrics[v];
	      if ( metric->histogram)
		histogram_add ((struct histogram *) metric->histogram,
			       case_data (c, dependent_vars[v])->f, weight);
	    }
	  case_unref (c);
	}
      casereader_destroy (input);
    }

  /* In this case, a third iteration is required */
  if (cmd->a_plot[XMN_PLT_BOXPLOT])
    {
      for (v = 0; v < n_dependent_vars; ++v)
	{
	  struct factor_metrics *metric = &result->metrics[v];
          int n_vals = caseproto_get_n_widths (casereader_get_proto (
                                                 metric->up_reader));

	  metric->box_whisker =
	    box_whisker_create ((struct tukey_hinges *) metric->tukey_hinges,
				cmd->v_id, n_vals - 1);

	  order_stats_accumulate ((struct order_stats **) &metric->box_whisker,
				  1,
				  casereader_clone (metric->up_reader),
				  wv, dependent_vars[v], MV_ANY);
	}
    }

  ll_push_tail (&factor->result_list, &result->ll);
  casereader_destroy (reader);
}


static void
run_examine (struct cmd_examine *cmd, struct casereader *input,
             struct dataset *ds)
{
  struct ll *ll;
  const struct dictionary *dict = dataset_dict (ds);
  struct ccase *c;
  struct casereader *level0 = casereader_clone (input);

  c = casereader_peek (input, 0);
  if (c == NULL)
    {
      casereader_destroy (input);
      return;
    }

  output_split_file_values (ds, c);
  case_unref (c);

  ll_init (&level0_factor.result_list);

  examine_group (cmd, level0, 0, dict, &level0_factor);

  for (ll = ll_head (&factor_list);
       ll != ll_null (&factor_list);
       ll = ll_next (ll))
    {
      struct xfactor *factor = ll_data (ll, struct xfactor, ll);

      struct casereader *group = NULL;
      struct casereader *level1;
      struct casegrouper *grouper1 = NULL;

      level1 = casereader_clone (input);
      level1 = sort_execute_1var (level1, factor->indep_var[0]);
      grouper1 = casegrouper_create_vars (level1, &factor->indep_var[0], 1);

      while (casegrouper_get_next_group (grouper1, &group))
	{
	  struct casereader *group_copy = casereader_clone (group);

	  if ( !factor->indep_var[1])
	    examine_group (cmd, group_copy, 1, dict, factor);
	  else
	    {
	      int n_groups = 0;
	      struct casereader *group2 = NULL;
	      struct casegrouper *grouper2 = NULL;

	      group_copy = sort_execute_1var (group_copy,
                                              factor->indep_var[1]);

	      grouper2 = casegrouper_create_vars (group_copy,
                                                  &factor->indep_var[1], 1);

	      while (casegrouper_get_next_group (grouper2, &group2))
		{
		  examine_group (cmd, group2, 2, dict, factor);
		  n_groups++;
		}
	      casegrouper_destroy (grouper2);
	    }

	  casereader_destroy (group);
	}
      casegrouper_destroy (grouper1);
    }

  casereader_destroy (input);

  output_examine (dict);

  factor_destroy (&level0_factor);

  {
    struct ll *ll;
    for (ll = ll_head (&factor_list);
	 ll != ll_null (&factor_list);
	 ll = ll_next (ll))
      {
	struct xfactor *f = ll_data (ll, struct xfactor, ll);
	factor_destroy (f);
      }
  }

}


static void
show_summary (const struct variable **dependent_var, int n_dep_var,
	      const struct dictionary *dict,
	      const struct xfactor *fctr)
{
  const struct variable *wv = dict_get_weight (dict);
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : & F_8_0;

  static const char *subtitle[]=
    {
      N_("Valid"),
      N_("Missing"),
      N_("Total")
    };

  int v, j;
  int heading_columns = 1;
  int n_cols;
  const int heading_rows = 3;
  struct tab_table *tbl;

  int n_rows ;
  n_rows = n_dep_var;

  assert (fctr);

  if ( fctr->indep_var[0] )
    {
      heading_columns = 2;

      if ( fctr->indep_var[1] )
	{
	  heading_columns = 3;
	}
    }

  n_rows *= ll_count (&fctr->result_list);
  n_rows += heading_rows;

  n_cols = heading_columns + 6;

  tbl = tab_create (n_cols, n_rows, 0);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions, NULL, NULL);

  /* Outline the box */
  tab_box (tbl,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  /* Vertical lines for the data only */
  tab_box (tbl,
	   -1, -1,
	   -1, TAL_1,
	   heading_columns, 0,
	   n_cols - 1, n_rows - 1);


  tab_hline (tbl, TAL_2, 0, n_cols - 1, heading_rows );
  tab_hline (tbl, TAL_1, heading_columns, n_cols - 1, 1 );
  tab_hline (tbl, TAL_1, heading_columns, n_cols - 1, heading_rows -1 );

  tab_vline (tbl, TAL_2, heading_columns, 0, n_rows - 1);


  tab_title (tbl, _("Case Processing Summary"));

  tab_joint_text (tbl, heading_columns, 0,
		  n_cols -1, 0,
		  TAB_CENTER | TAT_TITLE,
		  _("Cases"));

  /* Remove lines ... */
  tab_box (tbl,
	   -1, -1,
	   TAL_0, TAL_0,
	   heading_columns, 0,
	   n_cols - 1, 0);

  for (j = 0 ; j < 3 ; ++j)
    {
      tab_text (tbl, heading_columns + j * 2 , 2, TAB_CENTER | TAT_TITLE,
		_("N"));

      tab_text (tbl, heading_columns + j * 2 + 1, 2, TAB_CENTER | TAT_TITLE,
		_("Percent"));

      tab_joint_text (tbl, heading_columns + j * 2 , 1,
		      heading_columns + j * 2 + 1, 1,
		      TAB_CENTER | TAT_TITLE,
		      subtitle[j]);

      tab_box (tbl, -1, -1,
	       TAL_0, TAL_0,
	       heading_columns + j * 2, 1,
	       heading_columns + j * 2 + 1, 1);
    }


  /* Titles for the independent variables */
  if ( fctr->indep_var[0] )
    {
      tab_text (tbl, 1, heading_rows - 1, TAB_CENTER | TAT_TITLE,
		var_to_string (fctr->indep_var[0]));

      if ( fctr->indep_var[1] )
	{
	  tab_text (tbl, 2, heading_rows - 1, TAB_CENTER | TAT_TITLE,
		    var_to_string (fctr->indep_var[1]));
	}
    }

  for (v = 0 ; v < n_dep_var ; ++v)
    {
      int j = 0;
      struct ll *ll;
      const union value *last_value = NULL;

      if ( v > 0 )
	tab_hline (tbl, TAL_1, 0, n_cols -1 ,
		   v * ll_count (&fctr->result_list)
		   + heading_rows);

      tab_text (tbl,
		0,
		v * ll_count (&fctr->result_list) + heading_rows,
		TAB_LEFT | TAT_TITLE,
		var_to_string (dependent_var[v])
		);


      for (ll = ll_head (&fctr->result_list);
	   ll != ll_null (&fctr->result_list); ll = ll_next (ll))
	{
	  double n;
	  const struct factor_result *result =
	    ll_data (ll, struct factor_result, ll);

	  if ( fctr->indep_var[0] )
	    {

	      if ( last_value == NULL ||
		   !value_equal (last_value, &result->value[0],
                                 var_get_width (fctr->indep_var[0])))
		{
		  struct string str;

		  last_value = &result->value[0];
		  ds_init_empty (&str);

		  var_append_value_name (fctr->indep_var[0], &result->value[0],
					 &str);

		  tab_text (tbl, 1,
			    heading_rows + j +
			    v * ll_count (&fctr->result_list),
			    TAB_LEFT | TAT_TITLE,
			    ds_cstr (&str));

		  ds_destroy (&str);

		  if ( fctr->indep_var[1] && j > 0)
		    tab_hline (tbl, TAL_1, 1, n_cols - 1,
			       heading_rows + j +
			       v * ll_count (&fctr->result_list));
		}

	      if ( fctr->indep_var[1])
		{
		  struct string str;

		  ds_init_empty (&str);

		  var_append_value_name (fctr->indep_var[1],
					 &result->value[1], &str);

		  tab_text (tbl, 2,
			    heading_rows + j +
			    v * ll_count (&fctr->result_list),
			    TAB_LEFT | TAT_TITLE,
			    ds_cstr (&str));

		  ds_destroy (&str);
		}
	    }


	  moments1_calculate (result->metrics[v].moments,
			      &n, &result->metrics[v].mean,
			      &result->metrics[v].variance,
			      &result->metrics[v].skewness,
			      &result->metrics[v].kurtosis);

	  result->metrics[v].se_mean = sqrt (result->metrics[v].variance / n) ;

	  /* Total Valid */
	  tab_double (tbl, heading_columns,
		     heading_rows + j + v * ll_count (&fctr->result_list),
		     TAB_LEFT,
		     n, wfmt);

	  tab_text (tbl, heading_columns + 1,
		    heading_rows + j + v * ll_count (&fctr->result_list),
		    TAB_RIGHT | TAT_PRINTF,
		    "%g%%", n * 100.0 / result->metrics[v].n);

	  /* Total Missing */
	  tab_double (tbl, heading_columns + 2,
		     heading_rows + j + v * ll_count (&fctr->result_list),
		     TAB_LEFT,
		     result->metrics[v].n - n,
		     wfmt);

	  tab_text (tbl, heading_columns + 3,
		    heading_rows + j + v * ll_count (&fctr->result_list),
		    TAB_RIGHT | TAT_PRINTF,
		    "%g%%",
		    (result->metrics[v].n - n) * 100.0 / result->metrics[v].n
		    );

	  /* Total Valid + Missing */
	  tab_double (tbl, heading_columns + 4,
		     heading_rows + j + v * ll_count (&fctr->result_list),
		     TAB_LEFT,
		     result->metrics[v].n,
		     wfmt);

	  tab_text (tbl, heading_columns + 5,
		    heading_rows + j + v * ll_count (&fctr->result_list),
		    TAB_RIGHT | TAT_PRINTF,
		    "%g%%",
		    (result->metrics[v].n) * 100.0 / result->metrics[v].n
		    );

	  ++j;
	}
    }


  tab_submit (tbl);
}

#define DESCRIPTIVE_ROWS 13

static void
show_descriptives (const struct variable **dependent_var,
		   int n_dep_var,
		   const struct xfactor *fctr)
{
  int v;
  int heading_columns = 3;
  int n_cols;
  const int heading_rows = 1;
  struct tab_table *tbl;

  int n_rows ;
  n_rows = n_dep_var;

  assert (fctr);

  if ( fctr->indep_var[0] )
    {
      heading_columns = 4;

      if ( fctr->indep_var[1] )
	{
	  heading_columns = 5;
	}
    }

  n_rows *= ll_count (&fctr->result_list) * DESCRIPTIVE_ROWS;
  n_rows += heading_rows;

  n_cols = heading_columns + 2;

  tbl = tab_create (n_cols, n_rows, 0);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions, NULL, NULL);

  /* Outline the box */
  tab_box (tbl,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);


  tab_hline (tbl, TAL_2, 0, n_cols - 1, heading_rows );
  tab_hline (tbl, TAL_2, 1, n_cols - 1, heading_rows );

  tab_vline (tbl, TAL_1, n_cols - 1, 0, n_rows - 1);


  if ( fctr->indep_var[0])
    tab_text (tbl, 1, 0, TAT_TITLE, var_to_string (fctr->indep_var[0]));

  if ( fctr->indep_var[1])
    tab_text (tbl, 2, 0, TAT_TITLE, var_to_string (fctr->indep_var[1]));

  for (v = 0 ; v < n_dep_var ; ++v )
    {
      struct ll *ll;
      int i = 0;

      const int row_var_start =
	v * DESCRIPTIVE_ROWS * ll_count(&fctr->result_list);

      tab_text (tbl,
		0,
		heading_rows + row_var_start,
		TAB_LEFT | TAT_TITLE,
		var_to_string (dependent_var[v])
		);

      for (ll = ll_head (&fctr->result_list);
	   ll != ll_null (&fctr->result_list); i++, ll = ll_next (ll))
	{
	  const struct factor_result *result =
	    ll_data (ll, struct factor_result, ll);

	  const double t =
	    gsl_cdf_tdist_Qinv ((1 - cmd.n_cinterval[0] / 100.0) / 2.0,
                                      result->metrics[v].n - 1);

	  if ( i > 0 || v > 0 )
	    {
	      const int left_col = (i == 0) ? 0 : 1;
	      tab_hline (tbl, TAL_1, left_col, n_cols - 1,
			 heading_rows + row_var_start + i * DESCRIPTIVE_ROWS);
	    }

	  if ( fctr->indep_var[0])
	    {
	      struct string vstr;
	      ds_init_empty (&vstr);
	      var_append_value_name (fctr->indep_var[0],
				     &result->value[0], &vstr);

	      tab_text (tbl, 1,
			heading_rows + row_var_start + i * DESCRIPTIVE_ROWS,
			TAB_LEFT,
			ds_cstr (&vstr)
			);

	      ds_destroy (&vstr);
	    }


	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Mean"));

	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + 1 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT | TAT_PRINTF,
		    _("%g%% Confidence Interval for Mean"),
		    cmd.n_cinterval[0]);

	  tab_text (tbl, n_cols - 3,
		    heading_rows + row_var_start + 1 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Lower Bound"));

	  tab_text (tbl, n_cols - 3,
		    heading_rows + row_var_start + 2 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Upper Bound"));

	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + 3 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT | TAT_PRINTF,
		    _("5%% Trimmed Mean"));

	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + 4 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Median"));

	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + 5 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Variance"));

	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + 6 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Std. Deviation"));

	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + 7 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Minimum"));

	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + 8 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Maximum"));

	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + 9 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Range"));

	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + 10 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Interquartile Range"));


	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + 11 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Skewness"));

	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + 12 + i * DESCRIPTIVE_ROWS,
		    TAB_LEFT,
		    _("Kurtosis"));


	  /* Now the statistics ... */

	  tab_double (tbl, n_cols - 2,
		    heading_rows + row_var_start + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     result->metrics[v].mean,
		     NULL);

	  tab_double (tbl, n_cols - 1,
		    heading_rows + row_var_start + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     result->metrics[v].se_mean,
		     NULL);


	  tab_double (tbl, n_cols - 2,
		     heading_rows + row_var_start + 1 + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     result->metrics[v].mean - t *
		      result->metrics[v].se_mean,
		     NULL);

	  tab_double (tbl, n_cols - 2,
		     heading_rows + row_var_start + 2 + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     result->metrics[v].mean + t *
		      result->metrics[v].se_mean,
		     NULL);


	  tab_double (tbl, n_cols - 2,
		     heading_rows + row_var_start + 3 + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     trimmed_mean_calculate ((struct trimmed_mean *) result->metrics[v].trimmed_mean),
		     NULL);


	  tab_double (tbl, n_cols - 2,
		     heading_rows + row_var_start + 4 + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     percentile_calculate (result->metrics[v].quartiles[1], percentile_algorithm),
		     NULL);


	  tab_double (tbl, n_cols - 2,
		     heading_rows + row_var_start + 5 + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     result->metrics[v].variance,
		     NULL);

	  tab_double (tbl, n_cols - 2,
		     heading_rows + row_var_start + 6 + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     sqrt (result->metrics[v].variance),
		     NULL);

	  tab_double (tbl, n_cols - 2,
		     heading_rows + row_var_start + 10 + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     percentile_calculate (result->metrics[v].quartiles[2],
					   percentile_algorithm) -
		     percentile_calculate (result->metrics[v].quartiles[0],
					   percentile_algorithm),
		     NULL);


	  tab_double (tbl, n_cols - 2,
		     heading_rows + row_var_start + 11 + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     result->metrics[v].skewness,
		     NULL);

	  tab_double (tbl, n_cols - 2,
		     heading_rows + row_var_start + 12 + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     result->metrics[v].kurtosis,
		     NULL);

	  tab_double (tbl, n_cols - 1,
		     heading_rows + row_var_start + 11 + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     calc_seskew (result->metrics[v].n),
		     NULL);

	  tab_double (tbl, n_cols - 1,
		     heading_rows + row_var_start + 12 + i * DESCRIPTIVE_ROWS,
		     TAB_CENTER,
		     calc_sekurt (result->metrics[v].n),
		     NULL);

	  {
	    struct extremum *minimum, *maximum ;

	    struct ll *max_ll = ll_head (extrema_list (result->metrics[v].maxima));
	    struct ll *min_ll = ll_head (extrema_list (result->metrics[v].minima));

	    maximum = ll_data (max_ll, struct extremum, ll);
	    minimum = ll_data (min_ll, struct extremum, ll);

	    tab_double (tbl, n_cols - 2,
		       heading_rows + row_var_start + 7 + i * DESCRIPTIVE_ROWS,
		       TAB_CENTER,
		       minimum->value,
		       NULL);

	    tab_double (tbl, n_cols - 2,
		       heading_rows + row_var_start + 8 + i * DESCRIPTIVE_ROWS,
		       TAB_CENTER,
		       maximum->value,
		       NULL);

	    tab_double (tbl, n_cols - 2,
		       heading_rows + row_var_start + 9 + i * DESCRIPTIVE_ROWS,
		       TAB_CENTER,
		       maximum->value - minimum->value,
		       NULL);
	  }
	}
    }

  tab_vline (tbl, TAL_2, heading_columns, 0, n_rows - 1);

  tab_title (tbl, _("Descriptives"));

  tab_text (tbl, n_cols - 2, 0, TAB_CENTER | TAT_TITLE,
	    _("Statistic"));

  tab_text (tbl, n_cols - 1, 0, TAB_CENTER | TAT_TITLE,
	    _("Std. Error"));

  tab_submit (tbl);
}



static void
show_extremes (const struct variable **dependent_var,
	       int n_dep_var,
	       const struct xfactor *fctr)
{
  int v;
  int heading_columns = 3;
  int n_cols;
  const int heading_rows = 1;
  struct tab_table *tbl;

  int n_rows ;
  n_rows = n_dep_var;

  assert (fctr);

  if ( fctr->indep_var[0] )
    {
      heading_columns = 4;

      if ( fctr->indep_var[1] )
	{
	  heading_columns = 5;
	}
    }

  n_rows *= ll_count (&fctr->result_list) * cmd.st_n * 2;
  n_rows += heading_rows;

  n_cols = heading_columns + 2;

  tbl = tab_create (n_cols, n_rows, 0);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions, NULL, NULL);

  /* Outline the box */
  tab_box (tbl,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);


  tab_hline (tbl, TAL_2, 0, n_cols - 1, heading_rows );
  tab_hline (tbl, TAL_2, 1, n_cols - 1, heading_rows );
  tab_vline (tbl, TAL_1, n_cols - 1, 0, n_rows - 1);

  if ( fctr->indep_var[0])
    tab_text (tbl, 1, 0, TAT_TITLE, var_to_string (fctr->indep_var[0]));

  if ( fctr->indep_var[1])
    tab_text (tbl, 2, 0, TAT_TITLE, var_to_string (fctr->indep_var[1]));

  for (v = 0 ; v < n_dep_var ; ++v )
    {
      struct ll *ll;
      int i = 0;
      const int row_var_start = v * cmd.st_n * 2 * ll_count(&fctr->result_list);

      tab_text (tbl,
		0,
		heading_rows + row_var_start,
		TAB_LEFT | TAT_TITLE,
		var_to_string (dependent_var[v])
		);

      for (ll = ll_head (&fctr->result_list);
	   ll != ll_null (&fctr->result_list); i++, ll = ll_next (ll))
	{
	  int e ;
	  struct ll *min_ll;
	  struct ll *max_ll;
	  const int row_result_start = i * cmd.st_n * 2;

	  const struct factor_result *result =
	    ll_data (ll, struct factor_result, ll);

	  if (i > 0 || v > 0)
	    tab_hline (tbl, TAL_1, 1, n_cols - 1,
		       heading_rows + row_var_start + row_result_start);

	  tab_hline (tbl, TAL_1, heading_columns - 2, n_cols - 1,
		     heading_rows + row_var_start + row_result_start + cmd.st_n);

	  for ( e = 1; e <= cmd.st_n; ++e )
	    {
	      tab_text (tbl, n_cols - 3,
			heading_rows + row_var_start + row_result_start + e - 1,
			TAB_RIGHT | TAT_PRINTF,
			_("%d"), e);

	      tab_text (tbl, n_cols - 3,
			heading_rows + row_var_start + row_result_start + cmd.st_n + e - 1,
			TAB_RIGHT | TAT_PRINTF,
			_("%d"), e);
	    }


	  min_ll = ll_head (extrema_list (result->metrics[v].minima));
	  for (e = 0; e < cmd.st_n;)
	    {
	      struct extremum *minimum = ll_data (min_ll, struct extremum, ll);
	      double weight = minimum->weight;

	      while (weight-- > 0 && e < cmd.st_n)
		{
		  tab_double (tbl, n_cols - 1,
			     heading_rows + row_var_start + row_result_start + cmd.st_n + e,
			     TAB_RIGHT,
			     minimum->value,
			     NULL);


		  tab_fixed (tbl, n_cols - 2,
			     heading_rows + row_var_start +
			     row_result_start + cmd.st_n + e,
			     TAB_RIGHT,
			     minimum->location,
			     10, 0);
		  ++e;
		}

	      min_ll = ll_next (min_ll);
	    }


	  max_ll = ll_head (extrema_list (result->metrics[v].maxima));
	  for (e = 0; e < cmd.st_n;)
	    {
	      struct extremum *maximum = ll_data (max_ll, struct extremum, ll);
	      double weight = maximum->weight;

	      while (weight-- > 0 && e < cmd.st_n)
		{
		  tab_double (tbl, n_cols - 1,
			     heading_rows + row_var_start +
			      row_result_start + e,
			     TAB_RIGHT,
			     maximum->value,
			     NULL);


		  tab_fixed (tbl, n_cols - 2,
			     heading_rows + row_var_start +
			     row_result_start + e,
			     TAB_RIGHT,
			     maximum->location,
			     10, 0);
		  ++e;
		}

	      max_ll = ll_next (max_ll);
	    }


	  if ( fctr->indep_var[0])
	    {
	      struct string vstr;
	      ds_init_empty (&vstr);
	      var_append_value_name (fctr->indep_var[0],
				     &result->value[0], &vstr);

	      tab_text (tbl, 1,
			heading_rows + row_var_start + row_result_start,
			TAB_LEFT,
			ds_cstr (&vstr)
			);

	      ds_destroy (&vstr);
	    }


	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + row_result_start,
		    TAB_RIGHT,
		    _("Highest"));

	  tab_text (tbl, n_cols - 4,
		    heading_rows + row_var_start + row_result_start + cmd.st_n,
		    TAB_RIGHT,
		    _("Lowest"));
	}
    }

  tab_vline (tbl, TAL_2, heading_columns, 0, n_rows - 1);


  tab_title (tbl, _("Extreme Values"));


  tab_text (tbl, n_cols - 2, 0, TAB_CENTER | TAT_TITLE,
	    _("Case Number"));


  tab_text (tbl, n_cols - 1, 0, TAB_CENTER | TAT_TITLE,
	    _("Value"));

  tab_submit (tbl);
}

#define PERCENTILE_ROWS 2

static void
show_percentiles (const struct variable **dependent_var,
		  int n_dep_var,
		  const struct xfactor *fctr)
{
  int i;
  int v;
  int heading_columns = 2;
  int n_cols;
  const int n_percentiles = subc_list_double_count (&percentile_list);
  const int heading_rows = 2;
  struct tab_table *tbl;

  int n_rows ;
  n_rows = n_dep_var;

  assert (fctr);

  if ( fctr->indep_var[0] )
    {
      heading_columns = 3;

      if ( fctr->indep_var[1] )
	{
	  heading_columns = 4;
	}
    }

  n_rows *= ll_count (&fctr->result_list) * PERCENTILE_ROWS;
  n_rows += heading_rows;

  n_cols = heading_columns + n_percentiles;

  tbl = tab_create (n_cols, n_rows, 0);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions, NULL, NULL);

  /* Outline the box */
  tab_box (tbl,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);


  tab_hline (tbl, TAL_2, 0, n_cols - 1, heading_rows );
  tab_hline (tbl, TAL_2, 1, n_cols - 1, heading_rows );

  if ( fctr->indep_var[0])
    tab_text (tbl, 1, 1, TAT_TITLE, var_to_string (fctr->indep_var[0]));

  if ( fctr->indep_var[1])
    tab_text (tbl, 2, 1, TAT_TITLE, var_to_string (fctr->indep_var[1]));

  for (v = 0 ; v < n_dep_var ; ++v )
    {
      double hinges[3];
      struct ll *ll;
      int i = 0;

      const int row_var_start =
	v * PERCENTILE_ROWS * ll_count(&fctr->result_list);

      tab_text (tbl,
		0,
		heading_rows + row_var_start,
		TAB_LEFT | TAT_TITLE,
		var_to_string (dependent_var[v])
		);

      for (ll = ll_head (&fctr->result_list);
	   ll != ll_null (&fctr->result_list); i++, ll = ll_next (ll))
	{
	  int j;
	  const struct factor_result *result =
	    ll_data (ll, struct factor_result, ll);

	  if ( i > 0 || v > 0 )
	    {
	      const int left_col = (i == 0) ? 0 : 1;
	      tab_hline (tbl, TAL_1, left_col, n_cols - 1,
			 heading_rows + row_var_start + i * PERCENTILE_ROWS);
	    }

	  if ( fctr->indep_var[0])
	    {
	      struct string vstr;
	      ds_init_empty (&vstr);
	      var_append_value_name (fctr->indep_var[0],
				     &result->value[0], &vstr);

	      tab_text (tbl, 1,
			heading_rows + row_var_start + i * PERCENTILE_ROWS,
			TAB_LEFT,
			ds_cstr (&vstr)
			);

	      ds_destroy (&vstr);
	    }


	  tab_text (tbl, n_cols - n_percentiles - 1,
		    heading_rows + row_var_start + i * PERCENTILE_ROWS,
		    TAB_LEFT,
		    ptile_alg_desc [percentile_algorithm]);


	  tab_text (tbl, n_cols - n_percentiles - 1,
		    heading_rows + row_var_start + 1 + i * PERCENTILE_ROWS,
		    TAB_LEFT,
		    _("Tukey's Hinges"));


	  tab_vline (tbl, TAL_1, n_cols - n_percentiles -1, heading_rows, n_rows - 1);

	  tukey_hinges_calculate ((struct tukey_hinges *) result->metrics[v].tukey_hinges,
				  hinges);

	  for (j = 0; j < n_percentiles; ++j)
	    {
	      double hinge = SYSMIS;
	      tab_double (tbl, n_cols - n_percentiles + j,
			 heading_rows + row_var_start + i * PERCENTILE_ROWS,
			 TAB_CENTER,
			 percentile_calculate (result->metrics[v].ptl[j],
					       percentile_algorithm),
			 NULL
			 );

	      if ( result->metrics[v].ptl[j]->ptile == 0.5)
		hinge = hinges[1];
	      else if ( result->metrics[v].ptl[j]->ptile == 0.25)
		hinge = hinges[0];
	      else if ( result->metrics[v].ptl[j]->ptile == 0.75)
		hinge = hinges[2];

	      if ( hinge != SYSMIS)
		tab_double (tbl, n_cols - n_percentiles + j,
			   heading_rows + row_var_start + 1 + i * PERCENTILE_ROWS,
			   TAB_CENTER,
			   hinge,
			   NULL
			   );

	    }
	}
    }

  tab_vline (tbl, TAL_2, heading_columns, 0, n_rows - 1);

  tab_title (tbl, _("Percentiles"));


  for (i = 0 ; i < n_percentiles; ++i )
    {
      tab_text (tbl, n_cols - n_percentiles + i, 1,
		TAB_CENTER | TAT_TITLE | TAT_PRINTF,
		_("%g"),
		subc_list_double_at (&percentile_list, i)
		);


    }

  tab_joint_text (tbl,
		  n_cols - n_percentiles, 0,
		  n_cols - 1, 0,
		  TAB_CENTER | TAT_TITLE,
		  _("Percentiles"));

  /* Vertical lines for the data only */
  tab_box (tbl,
	   -1, -1,
	   -1, TAL_1,
	   n_cols - n_percentiles, 1,
	   n_cols - 1, n_rows - 1);

  tab_hline (tbl, TAL_1, n_cols - n_percentiles, n_cols - 1, 1);


  tab_submit (tbl);
}


static void
factor_to_string_concise (const struct xfactor *fctr,
			  const struct factor_result *result,
			  struct string *str
			  )
{
  if (fctr->indep_var[0])
    {
      var_append_value_name (fctr->indep_var[0], &result->value[0], str);

      if ( fctr->indep_var[1] )
	{
	  ds_put_cstr (str, ",");

	  var_append_value_name (fctr->indep_var[1], &result->value[1], str);

	  ds_put_cstr (str, ")");
	}
    }
}


static void
factor_to_string (const struct xfactor *fctr,
		  const struct factor_result *result,
		  struct string *str
		  )
{
  if (fctr->indep_var[0])
    {
      ds_put_format (str, "(%s = ", var_get_name (fctr->indep_var[0]));

      var_append_value_name (fctr->indep_var[0], &result->value[0], str);

      if ( fctr->indep_var[1] )
	{
	  ds_put_cstr (str, ",");
	  ds_put_format (str, "%s = ", var_get_name (fctr->indep_var[1]));

	  var_append_value_name (fctr->indep_var[1], &result->value[1], str);
	}
      ds_put_cstr (str, ")");
    }
}




/*
  Local Variables:
  mode: c
  End:
*/
