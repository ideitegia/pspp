/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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
#include <stdlib.h>
#include <gsl/gsl_histogram.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/settings.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/dictionary/split-file.h"
#include "language/lexer/lexer.h"
#include "language/stats/freq.h"
#include "libpspp/array.h"
#include "libpspp/bit-vector.h"
#include "libpspp/compiler.h"
#include "libpspp/hmap.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "math/histogram.h"
#include "math/moments.h"
#include "math/chart-geometry.h"

#include "output/chart-item.h"
#include "output/charts/piechart.h"
#include "output/charts/plot-hist.h"
#include "output/tab.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* (headers) */

/* (specification)
   FREQUENCIES (frq_):
     *+variables=custom;
     +format=table:limit(n:limit,"%s>0")/notable/!table,
	     sort:!avalue/dvalue/afreq/dfreq;
     missing=miss:include/!exclude;
     barchart(ba_)=:minimum(d:min),
	    :maximum(d:max),
	    scale:freq(*n:freq,"%s>0")/percent(*n:pcnt,"%s>0");
     piechart(pie_)=:minimum(d:min),
	    :maximum(d:max),
	    missing:missing/!nomissing,
	    scale:!freq/percent;
     histogram(hi_)=:minimum(d:min),
	    :maximum(d:max),
	    scale:freq(*n:freq,"%s>0")/percent(*n:pcnt,"%s>0"),
	    norm:!nonormal/normal;
     +grouped=custom;
     +ntiles=integer;
     +percentiles = double list;
     +statistics[st_]=mean,semean,median,mode,stddev,variance,
 	    kurtosis,skewness,range,minimum,maximum,sum,
 	    default,seskewness,sekurtosis,all,none.
*/
/* (declarations) */
/* (functions) */

/* Statistics. */
enum
  {
    FRQ_MEAN, FRQ_SEMEAN, FRQ_MEDIAN, FRQ_MODE, FRQ_STDDEV, FRQ_VARIANCE,
    FRQ_KURT, FRQ_SEKURT, FRQ_SKEW, FRQ_SESKEW, FRQ_RANGE, FRQ_MIN, FRQ_MAX,
    FRQ_SUM, FRQ_N_STATS
  };

/* Description of a statistic. */
struct frq_info
  {
    int st_indx;		/* Index into a_statistics[]. */
    const char *s10;		/* Identifying string. */
  };

/* Table of statistics, indexed by dsc_*. */
static const struct frq_info st_name[FRQ_N_STATS + 1] =
{
  {FRQ_ST_MEAN, N_("Mean")},
  {FRQ_ST_SEMEAN, N_("S.E. Mean")},
  {FRQ_ST_MEDIAN, N_("Median")},
  {FRQ_ST_MODE, N_("Mode")},
  {FRQ_ST_STDDEV, N_("Std Dev")},
  {FRQ_ST_VARIANCE, N_("Variance")},
  {FRQ_ST_KURTOSIS, N_("Kurtosis")},
  {FRQ_ST_SEKURTOSIS, N_("S.E. Kurt")},
  {FRQ_ST_SKEWNESS, N_("Skewness")},
  {FRQ_ST_SESKEWNESS, N_("S.E. Skew")},
  {FRQ_ST_RANGE, N_("Range")},
  {FRQ_ST_MINIMUM, N_("Minimum")},
  {FRQ_ST_MAXIMUM, N_("Maximum")},
  {FRQ_ST_SUM, N_("Sum")},
  {-1, 0},
};

/* Percentiles to calculate. */

struct percentile
{
  double p;        /* the %ile to be calculated */
  double value;    /* the %ile's value */
  bool show;       /* True to show this percentile in the statistics box. */
};

/* Groups of statistics. */
#define BI          BIT_INDEX
#define FRQ_DEFAULT							\
	(BI (FRQ_MEAN) | BI (FRQ_STDDEV) | BI (FRQ_MIN) | BI (FRQ_MAX))
#define FRQ_ALL							\
	(BI (FRQ_SUM) | BI(FRQ_MIN) | BI(FRQ_MAX)		\
	 | BI(FRQ_MEAN) | BI(FRQ_SEMEAN) | BI(FRQ_STDDEV)	\
	 | BI(FRQ_VARIANCE) | BI(FRQ_KURT) | BI(FRQ_SEKURT)	\
	 | BI(FRQ_SKEW) | BI(FRQ_SESKEW) | BI(FRQ_RANGE)	\
	 | BI(FRQ_RANGE) | BI(FRQ_MODE) | BI(FRQ_MEDIAN))

struct frq_chart
  {
    double x_min;               /* X axis minimum value. */
    double x_max;               /* X axis maximum value. */
    int y_scale;                /* Y axis scale: FRQ_FREQ or FRQ_PERCENT. */

    /* Histograms only. */
    double y_max;               /* Y axis maximum value. */
    bool draw_normal;           /* Whether to draw normal curve. */

    /* Pie charts only. */
    bool include_missing;       /* Whether to include missing values. */
  };

/* Frequency tables. */

/* Entire frequency table. */
struct freq_tab
  {
    struct hmap data;           /* Hash table for accumulating counts. */
    struct freq *valid;         /* Valid freqs. */
    int n_valid;		/* Number of total freqs. */
    const struct dictionary *dict; /* Source of entries in the table. */

    struct freq *missing;       /* Missing freqs. */
    int n_missing;		/* Number of missing freqs. */

    /* Statistics. */
    double total_cases;		/* Sum of weights of all cases. */
    double valid_cases;		/* Sum of weights of valid cases. */
  };

/* Per-variable frequency data. */
struct var_freqs
  {
    struct variable *var;

    /* Freqency table. */
    struct freq_tab tab;	/* Frequencies table to use. */

    /* Percentiles. */
    int n_groups;		/* Number of groups. */
    double *groups;		/* Groups. */

    /* Statistics. */
    double stat[FRQ_N_STATS];

    /* Variable attributes. */
    int width;
  };

struct frq_proc
  {
    struct pool *pool;

    struct var_freqs *vars;
    size_t n_vars;

    /* Percentiles to calculate and possibly display. */
    struct percentile *percentiles;
    int n_percentiles, n_show_percentiles;

    /* Frequency table display. */
    int max_categories;         /* Maximum categories to show. */
    int sort;                   /* FRQ_AVALUE or FRQ_DVALUE
                                   or FRQ_ACOUNT or FRQ_DCOUNT. */

    /* Statistics; number of statistics. */
    unsigned long stats;
    int n_stats;

    /* Histogram and pie chart settings. */
    struct frq_chart *hist, *pie;
  };

static void determine_charts (struct frq_proc *,
                              const struct cmd_frequencies *);

static void calc_stats (const struct var_freqs *, double d[FRQ_N_STATS]);
static void calc_percentiles (const struct frq_proc *,
                              const struct var_freqs *);

static void precalc (struct frq_proc *, struct casereader *, struct dataset *);
static void calc (struct frq_proc *, const struct ccase *,
                  const struct dataset *);
static void postcalc (struct frq_proc *, const struct dataset *);

static void postprocess_freq_tab (const struct frq_proc *, struct var_freqs *);
static void dump_freq_table (const struct var_freqs *,
                             const struct variable *weight_var);
static void dump_statistics (const struct frq_proc *, const struct var_freqs *,
                             const struct variable *weight_var);
static void cleanup_freq_tab (struct var_freqs *);

static void add_percentile (struct frq_proc *, double x, bool show,
                            size_t *allocated_percentiles);

static void do_piechart(const struct frq_chart *, const struct variable *,
			const struct freq_tab *);

struct histogram *freq_tab_to_hist(const struct frq_proc *,
                                   const struct freq_tab *,
                                   const struct variable *);

/* Parser and outline. */

int
cmd_frequencies (struct lexer *lexer, struct dataset *ds)
{
  struct cmd_frequencies cmd;
  struct frq_proc frq;
  struct casegrouper *grouper;
  struct casereader *input, *group;
  size_t allocated_percentiles;
  bool ok;
  int i;

  frq.pool = pool_create ();

  frq.vars = NULL;
  frq.n_vars = 0;

  frq.percentiles = NULL;
  frq.n_percentiles = 0;
  frq.n_show_percentiles = 0;

  frq.hist = NULL;
  frq.pie = NULL;

  allocated_percentiles = 0;

  if (!parse_frequencies (lexer, ds, &cmd, &frq))
    {
      pool_destroy (frq.pool);
      return CMD_FAILURE;
    }

  /* Figure out when to show frequency tables. */
  frq.max_categories = (cmd.table == FRQ_NOTABLE ? -1
                        : cmd.table == FRQ_TABLE ? INT_MAX
                        : cmd.limit);
  frq.sort = cmd.sort;

  /* Figure out statistics to calculate. */
  frq.stats = 0;
  if (cmd.a_statistics[FRQ_ST_DEFAULT] || !cmd.sbc_statistics)
    frq.stats |= FRQ_DEFAULT;
  if (cmd.a_statistics[FRQ_ST_ALL])
    frq.stats |= FRQ_ALL;
  if (cmd.sort != FRQ_AVALUE && cmd.sort != FRQ_DVALUE)
    frq.stats &= ~BIT_INDEX (FRQ_MEDIAN);
  for (i = 0; i < FRQ_N_STATS; i++)
    if (cmd.a_statistics[st_name[i].st_indx])
      frq.stats |= BIT_INDEX (i);
  if (frq.stats & FRQ_KURT)
    frq.stats |= BIT_INDEX (FRQ_SEKURT);
  if (frq.stats & FRQ_SKEW)
    frq.stats |= BIT_INDEX (FRQ_SESKEW);

  /* Calculate n_stats. */
  frq.n_stats = 0;
  for (i = 0; i < FRQ_N_STATS; i++)
    if ((frq.stats & BIT_INDEX (i)))
      frq.n_stats++;

  /* Charting. */
  determine_charts (&frq, &cmd);
  if (cmd.sbc_histogram || cmd.sbc_piechart || cmd.sbc_ntiles)
    cmd.sort = FRQ_AVALUE;

  /* Work out what percentiles need to be calculated */
  if ( cmd.sbc_percentiles )
    {
      for ( i = 0 ; i < MAXLISTS ; ++i )
	{
	  int pl;
	  subc_list_double *ptl_list = &cmd.dl_percentiles[i];
	  for ( pl = 0 ; pl < subc_list_double_count(ptl_list); ++pl)
            add_percentile (&frq, subc_list_double_at(ptl_list, pl) / 100.0,
                            true, &allocated_percentiles);
	}
    }
  if ( cmd.sbc_ntiles )
    {
      for ( i = 0 ; i < cmd.sbc_ntiles ; ++i )
	{
	  int j;
	  for (j = 0; j <= cmd.n_ntiles[i]; ++j )
            add_percentile (&frq, j / (double) cmd.n_ntiles[i], true,
                            &allocated_percentiles);
	}
    }
  if (frq.stats & BIT_INDEX (FRQ_MEDIAN))
    {
      /* Treat the median as the 50% percentile.
         We output it in the percentiles table as "50 (Median)." */
      add_percentile (&frq, 0.5, true, &allocated_percentiles);
      frq.stats &= ~BIT_INDEX (FRQ_MEDIAN);
      frq.n_stats--;
    }
  if (cmd.sbc_histogram)
    {
      add_percentile (&frq, 0.25, false, &allocated_percentiles);
      add_percentile (&frq, 0.75, false, &allocated_percentiles);
    }

  /* Do it! */
  input = casereader_create_filter_weight (proc_open (ds), dataset_dict (ds),
                                           NULL, NULL);
  grouper = casegrouper_create_splits (input, dataset_dict (ds));
  for (; casegrouper_get_next_group (grouper, &group);
       casereader_destroy (group))
    {
      struct ccase *c;

      precalc (&frq, group, ds);
      for (; (c = casereader_read (group)) != NULL; case_unref (c))
        calc (&frq, c, ds);
      postcalc (&frq, ds);
    }
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  free_frequencies(&cmd);

  pool_destroy (frq.pool);
  free (frq.vars);
  free (frq.percentiles);
  free (frq.hist);
  free (frq.pie);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

/* Figure out which charts the user requested.  */
static void
determine_charts (struct frq_proc *frq, const struct cmd_frequencies *cmd)
{
  if (cmd->sbc_barchart)
    msg (SW, _("Bar charts are not implemented."));

  if (cmd->sbc_histogram)
    {
      struct frq_chart *hist;

      hist = frq->hist = xmalloc (sizeof *frq->hist);
      hist->x_min = cmd->hi_min;
      hist->x_max = cmd->hi_max;
      hist->y_scale = cmd->hi_scale;
      hist->y_max = cmd->hi_scale == FRQ_FREQ ? cmd->hi_freq : cmd->hi_pcnt;
      hist->draw_normal = cmd->hi_norm != FRQ_NONORMAL;
      hist->include_missing = false;

      if (hist->x_min != SYSMIS && hist->x_max != SYSMIS
          && hist->x_min >= hist->x_max)
        {
          msg (SE, _("%s for histogram must be greater than or equal to %s, "
                     "but %s was specified as %.15g and %s as %.15g.  "
                     "%s and %s will be ignored."),
	       "MAX", "MIN", 
	       "MIN", hist->x_min, 
	       "MAX", hist->x_max,
	       "MIN", "MAX");
          hist->x_min = hist->x_max = SYSMIS;
        }
    }

  if (cmd->sbc_piechart)
    {
      struct frq_chart *pie;

      pie = frq->pie = xmalloc (sizeof *frq->pie);
      pie->x_min = cmd->pie_min;
      pie->x_max = cmd->pie_max;
      pie->y_scale = cmd->pie_scale;
      pie->include_missing = cmd->pie_missing == FRQ_MISSING;

      if (pie->x_min != SYSMIS && pie->x_max != SYSMIS
          && pie->x_min >= pie->x_max)
        {
          msg (SE, _("%s for pie chart must be greater than or equal to %s, "
                     "but %s was specified as %.15g and %s as %.15g.  "
                     "%s and %s will be ignored."), 
	       "MAX", "MIN", 
	       "MIN", pie->x_min,
	       "MAX", pie->x_max,
	       "MIN", "MAX");
          pie->x_min = pie->x_max = SYSMIS;
        }
    }
}

/* Add data from case C to the frequency table. */
static void
calc (struct frq_proc *frq, const struct ccase *c, const struct dataset *ds)
{
  double weight = dict_get_case_weight (dataset_dict (ds), c, NULL);
  size_t i;

  for (i = 0; i < frq->n_vars; i++)
    {
      struct var_freqs *vf = &frq->vars[i];
      const union value *value = case_data (c, vf->var);
      size_t hash = value_hash (value, vf->width, 0);
      struct freq *f;

      f = freq_hmap_search (&vf->tab.data, value, vf->width, hash);
      if (f == NULL)
        f = freq_hmap_insert (&vf->tab.data, value, vf->width, hash);

      f->count += weight;
    }
}

/* Prepares each variable that is the target of FREQUENCIES by setting
   up its hash table. */
static void
precalc (struct frq_proc *frq, struct casereader *input, struct dataset *ds)
{
  struct ccase *c;
  size_t i;

  c = casereader_peek (input, 0);
  if (c != NULL)
    {
      output_split_file_values (ds, c);
      case_unref (c);
    }

  for (i = 0; i < frq->n_vars; i++)
    hmap_init (&frq->vars[i].tab.data);
}

/* Finishes up with the variables after frequencies have been
   calculated.  Displays statistics, percentiles, ... */
static void
postcalc (struct frq_proc *frq, const struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);
  const struct variable *wv = dict_get_weight (dict);
  size_t i;

  for (i = 0; i < frq->n_vars; i++)
    {
      struct var_freqs *vf = &frq->vars[i];

      postprocess_freq_tab (frq, vf);

      /* Frequencies tables. */
      if (vf->tab.n_valid + vf->tab.n_missing <= frq->max_categories)
        dump_freq_table (vf, wv);

      /* Statistics. */
      if (frq->n_stats)
	dump_statistics (frq, vf, wv);

      if (frq->hist && var_is_numeric (vf->var) && vf->tab.n_valid > 0)
	{
	  double d[FRQ_N_STATS];
	  struct histogram *histogram;

	  calc_stats (vf, d);

	  histogram = freq_tab_to_hist (frq, &vf->tab, vf->var);

	  if ( histogram)
	    {
	      chart_item_submit (histogram_chart_create (
                               histogram->gsl_hist, var_to_string(vf->var),
                               vf->tab.valid_cases,
                               d[FRQ_MEAN],
                               d[FRQ_STDDEV],
                               frq->hist->draw_normal));

	      statistic_destroy (&histogram->parent);
	    }
	}

      if (frq->pie)
        do_piechart(frq->pie, vf->var, &vf->tab);

      cleanup_freq_tab (vf);

    }
}

/* Returns true iff the value in struct freq F is non-missing
   for variable V. */
static bool
not_missing (const void *f_, const void *v_)
{
  const struct freq *f = f_;
  const struct variable *v = v_;

  return !var_is_value_missing (v, &f->value, MV_ANY);
}

struct freq_compare_aux
  {
    bool by_freq;
    bool ascending_freq;

    int width;
    bool ascending_value;
  };

static int
compare_freq (const void *a_, const void *b_, const void *aux_)
{
  const struct freq_compare_aux *aux = aux_;
  const struct freq *a = a_;
  const struct freq *b = b_;

  if (aux->by_freq && a->count != b->count)
    {
      int cmp = a->count > b->count ? 1 : -1;
      return aux->ascending_freq ? cmp : -cmp;
    }
  else
    {
      int cmp = value_compare_3way (&a->value, &b->value, aux->width);
      return aux->ascending_value ? cmp : -cmp;
    }
}
/* Summarizes the frequency table data for variable V. */
static void
postprocess_freq_tab (const struct frq_proc *frq, struct var_freqs *vf)
{
  struct freq_tab *ft = &vf->tab;
  struct freq_compare_aux aux;
  size_t count;
  struct freq *freqs, *f;
  size_t i;

  /* Extract data from hash table. */
  count = hmap_count (&ft->data);
  freqs = freq_hmap_extract (&ft->data);

  /* Put data into ft. */
  ft->valid = freqs;
  ft->n_valid = partition (freqs, count, sizeof *freqs, not_missing, vf->var);
  ft->missing = freqs + ft->n_valid;
  ft->n_missing = count - ft->n_valid;

  /* Sort data. */
  aux.by_freq = frq->sort == FRQ_AFREQ || frq->sort == FRQ_DFREQ;
  aux.ascending_freq = frq->sort != FRQ_DFREQ;
  aux.width = vf->width;
  aux.ascending_value = frq->sort != FRQ_DVALUE;
  sort (ft->valid, ft->n_valid, sizeof *ft->valid, compare_freq, &aux);
  sort (ft->missing, ft->n_missing, sizeof *ft->missing, compare_freq, &aux);

  /* Summary statistics. */
  ft->valid_cases = 0.0;
  for(i = 0 ;  i < ft->n_valid ; ++i )
    {
      f = &ft->valid[i];
      ft->valid_cases += f->count;

    }

  ft->total_cases = ft->valid_cases ;
  for(i = 0 ;  i < ft->n_missing ; ++i )
    {
      f = &ft->missing[i];
      ft->total_cases += f->count;
    }

}

/* Frees the frequency table for variable V. */
static void
cleanup_freq_tab (struct var_freqs *vf)
{
  free (vf->tab.valid);
  freq_hmap_destroy (&vf->tab.data, vf->width);
}

/* Parses the VARIABLES subcommand. */
static int
frq_custom_variables (struct lexer *lexer, struct dataset *ds,
                      struct cmd_frequencies *cmd UNUSED, void *frq_ UNUSED)
{
  struct frq_proc *frq = frq_;
  struct variable **vars;
  size_t n_vars;
  size_t i;

  lex_match (lexer, T_EQUALS);
  if (lex_token (lexer) != T_ALL
      && (lex_token (lexer) != T_ID
          || dict_lookup_var (dataset_dict (ds), lex_tokcstr (lexer)) == NULL))
    return 2;

  /* Get list of current variables, to avoid duplicates. */
  vars = xmalloc (frq->n_vars * sizeof *vars);
  n_vars = frq->n_vars;
  for (i = 0; i < frq->n_vars; i++)
    vars[i] = frq->vars[i].var;

  if (!parse_variables (lexer, dataset_dict (ds), &vars, &n_vars,
                        PV_APPEND | PV_NO_SCRATCH))
    return 0;

  frq->vars = xrealloc (frq->vars, n_vars * sizeof *frq->vars);
  for (i = frq->n_vars; i < n_vars; i++)
    {
      struct variable *var = vars[i];
      struct var_freqs *vf = &frq->vars[i];

      vf->var = var;
      vf->tab.valid = vf->tab.missing = NULL;
      vf->tab.dict = dataset_dict (ds);
      vf->n_groups = 0;
      vf->groups = NULL;
      vf->width = var_get_width (var);
    }
  frq->n_vars = n_vars;

  free (vars);

  return 1;
}

/* Parses the GROUPED subcommand, setting the n_grouped, grouped
   fields of specified variables. */
static int
frq_custom_grouped (struct lexer *lexer, struct dataset *ds, struct cmd_frequencies *cmd UNUSED, void *frq_ UNUSED)
{
  struct frq_proc *frq = frq_;

  lex_match (lexer, T_EQUALS);
  if ((lex_token (lexer) == T_ID
       && dict_lookup_var (dataset_dict (ds), lex_tokcstr (lexer)) != NULL)
      || lex_token (lexer) == T_ID)
    for (;;)
      {
	size_t i;

	/* Max, current size of list; list itself. */
	int nl, ml;
	double *dl;

	/* Variable list. */
	size_t n;
	const struct variable **v;

	if (!parse_variables_const (lexer, dataset_dict (ds), &v, &n,
                              PV_NO_DUPLICATE | PV_NUMERIC))
	  return 0;
	if (lex_match (lexer, T_LPAREN))
	  {
	    nl = ml = 0;
	    dl = NULL;
	    while (lex_integer (lexer))
	      {
		if (nl >= ml)
		  {
		    ml += 16;
		    dl = pool_nrealloc (frq->pool, dl, ml, sizeof *dl);
		  }
		dl[nl++] = lex_tokval (lexer);
		lex_get (lexer);
		lex_match (lexer, T_COMMA);
	      }
	    /* Note that nl might still be 0 and dl might still be
	       NULL.  That's okay. */
	    if (!lex_match (lexer, T_RPAREN))
	      {
		free (v);
                lex_error_expecting (lexer, "`)'", NULL_SENTINEL);
		return 0;
	      }
	  }
	else
          {
            nl = 0;
            dl = NULL;
          }

	for (i = 0; i < n; i++)
          {
            size_t j;

            for (j = 0; j < frq->n_vars; j++)
              {
                struct var_freqs *vf = &frq->vars[j];
                if (vf->var == v[i])
                  {
                    if (vf->groups != NULL)
                      msg (SE, _("Variables %s specified multiple times on "
                                 "%s subcommand."), var_get_name (v[i]), "GROUPED");
                    else
                      {
                        vf->n_groups = nl;
                        vf->groups = dl;
                      }
                    goto found;
                  }
              }
            msg (SE, _("Variables %s specified on %s but not on "
                       "%s."), var_get_name (v[i]), "GROUPED", "VARIABLES");

          found:;
          }

	free (v);
        if (lex_token (lexer) != T_SLASH)
          break;

        if ((lex_next_token (lexer, 1) == T_ID
             && dict_lookup_var (dataset_dict (ds),
                                 lex_next_tokcstr (lexer, 1)))
            || lex_next_token (lexer, 1) == T_ALL)
          {
            /* The token after the slash is a variable name.  Keep parsing. */
            lex_get (lexer);
          }
        else
          {
            /* The token after the slash must be the start of a new
               subcommand.  Let the caller see the slash. */
            break;
          }
      }

  return 1;
}

/* Adds X to the list of percentiles, keeping the list in proper
   order.  If SHOW is true, the percentile will be shown in the statistics
   box, otherwise it will be hidden. */
static void
add_percentile (struct frq_proc *frq, double x, bool show,
                size_t *allocated_percentiles)
{
  int i;

  /* Do nothing if it's already in the list */
  for (i = 0; i < frq->n_percentiles; i++)
    {
      struct percentile *pc = &frq->percentiles[i];

      if ( fabs(x - pc->p) < DBL_EPSILON )
        {
          if (show && !pc->show)
            {
              frq->n_show_percentiles++;
              pc->show = true;
            }
          return;
        }

      if (x < pc->p)
	break;
    }

  if (frq->n_percentiles >= *allocated_percentiles)
    frq->percentiles = x2nrealloc (frq->percentiles, allocated_percentiles,
                                   sizeof *frq->percentiles);
  insert_element (frq->percentiles, frq->n_percentiles,
                  sizeof *frq->percentiles, i);
  frq->percentiles[i].p = x;
  frq->percentiles[i].show = show;
  frq->n_percentiles++;
  if (show)
    frq->n_show_percentiles++;
}

/* Comparison functions. */


/* Frequency table display. */

/* Displays a full frequency table for variable V. */
static void
dump_freq_table (const struct var_freqs *vf, const struct variable *wv)
{
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : &F_8_0;
  const struct freq_tab *ft = &vf->tab;
  int n_categories;
  struct freq *f;
  struct tab_table *t;
  int r, x;
  double cum_total = 0.0;
  double cum_freq = 0.0;

  static const char *headings[] = {
    N_("Value Label"),
    N_("Value"),
    N_("Frequency"),
    N_("Percent"),
    N_("Valid Percent"),
    N_("Cum Percent")
  };

  n_categories = ft->n_valid + ft->n_missing;
  t = tab_create (6, n_categories + 2);
  tab_headers (t, 0, 0, 1, 0);

  for (x = 0; x < 6; x++)
    tab_text (t, x, 0, TAB_CENTER | TAT_TITLE, gettext (headings[x]));

  r = 1;
  for (f = ft->valid; f < ft->missing; f++)
    {
      const char *label;
      double percent, valid_percent;

      cum_freq += f->count;

      percent = f->count / ft->total_cases * 100.0;
      valid_percent = f->count / ft->valid_cases * 100.0;
      cum_total += valid_percent;

      label = var_lookup_value_label (vf->var, &f->value);
      if (label != NULL)
        tab_text (t, 0, r, TAB_LEFT, label);

      tab_value (t, 1, r, TAB_NONE, &f->value, vf->var, NULL);
      tab_double (t, 2, r, TAB_NONE, f->count, wfmt);
      tab_double (t, 3, r, TAB_NONE, percent, NULL);
      tab_double (t, 4, r, TAB_NONE, valid_percent, NULL);
      tab_double (t, 5, r, TAB_NONE, cum_total, NULL);
      r++;
    }
  for (; f < &ft->valid[n_categories]; f++)
    {
      const char *label;

      cum_freq += f->count;

      label = var_lookup_value_label (vf->var, &f->value);
      if (label != NULL)
        tab_text (t, 0, r, TAB_LEFT, label);

      tab_value (t, 1, r, TAB_NONE, &f->value, vf->var, NULL);
      tab_double (t, 2, r, TAB_NONE, f->count, wfmt);
      tab_double (t, 3, r, TAB_NONE,
		     f->count / ft->total_cases * 100.0, NULL);
      tab_text (t, 4, r, TAB_NONE, _("Missing"));
      r++;
    }

  tab_box (t, TAL_1, TAL_1, -1, TAL_1, 0, 0, 5, r);
  tab_hline (t, TAL_2, 0, 5, 1);
  tab_hline (t, TAL_2, 0, 5, r);
  tab_joint_text (t, 0, r, 1, r, TAB_RIGHT | TAT_TITLE, _("Total"));
  tab_vline (t, TAL_0, 1, r, r);
  tab_double (t, 2, r, TAB_NONE, cum_freq, wfmt);
  tab_fixed (t, 3, r, TAB_NONE, 100.0, 5, 1);
  tab_fixed (t, 4, r, TAB_NONE, 100.0, 5, 1);

  tab_title (t, "%s", var_to_string (vf->var));
  tab_submit (t);
}

/* Statistical display. */

static double
calc_percentile (double p, double valid_cases, double x1, double x2)
{
  double s, dummy;

  s = (settings_get_algorithm () != COMPATIBLE
       ? modf ((valid_cases - 1) * p, &dummy)
       : modf ((valid_cases + 1) * p - 1, &dummy));

  return x1 + (x2 - x1) * s;
}

/* Calculates all of the percentiles for VF within FRQ. */
static void
calc_percentiles (const struct frq_proc *frq, const struct var_freqs *vf)
{
  const struct freq_tab *ft = &vf->tab;
  double W = ft->valid_cases;
  const struct freq *f;
  int percentile_idx;
  double rank;

  assert (ft->n_valid > 0);

  rank = 0;
  percentile_idx = 0;
  for (f = ft->valid; f < ft->missing; f++)
    {
      rank += f->count;
      for (; percentile_idx < frq->n_percentiles; percentile_idx++)
        {
          struct percentile *pc = &frq->percentiles[percentile_idx];
          double tp;

          tp = (settings_get_algorithm () == ENHANCED
                ? (W - 1) * pc->p
                : (W + 1) * pc->p - 1);

          if (rank <= tp)
            break;

          if (tp + 1 < rank || f + 1 >= ft->missing)
            pc->value = f->value.f;
          else
            pc->value = calc_percentile (pc->p, W, f->value.f, f[1].value.f);
        }
    }
  for (; percentile_idx < frq->n_percentiles; percentile_idx++)
    {
      struct percentile *pc = &frq->percentiles[percentile_idx];
      pc->value = ft->valid[ft->n_valid - 1].value.f;
    }
}

/* Calculates all the pertinent statistics for VF, putting them in array
   D[]. */
static void
calc_stats (const struct var_freqs *vf, double d[FRQ_N_STATS])
{
  const struct freq_tab *ft = &vf->tab;
  double W = ft->valid_cases;
  const struct freq *f;
  struct moments *m;
  int most_often;
  double X_mode;

  assert (ft->n_valid > 0);

  /* Calculate the mode. */
  most_often = -1;
  X_mode = SYSMIS;
  for (f = ft->valid; f < ft->missing; f++)
    {
      if (most_often < f->count)
        {
          most_often = f->count;
          X_mode = f->value.f;
        }
      else if (most_often == f->count)
        {
          /* A duplicate mode is undefined.
             FIXME: keep track of *all* the modes. */
          X_mode = SYSMIS;
        }
    }

  /* Calculate moments. */
  m = moments_create (MOMENT_KURTOSIS);
  for (f = ft->valid; f < ft->missing; f++)
    moments_pass_one (m, f->value.f, f->count);
  for (f = ft->valid; f < ft->missing; f++)
    moments_pass_two (m, f->value.f, f->count);
  moments_calculate (m, NULL, &d[FRQ_MEAN], &d[FRQ_VARIANCE],
                     &d[FRQ_SKEW], &d[FRQ_KURT]);
  moments_destroy (m);

  /* Formulas below are taken from _SPSS Statistical Algorithms_. */
  d[FRQ_MIN] = ft->valid[0].value.f;
  d[FRQ_MAX] = ft->valid[ft->n_valid - 1].value.f;
  d[FRQ_MODE] = X_mode;
  d[FRQ_RANGE] = d[FRQ_MAX] - d[FRQ_MIN];
  d[FRQ_SUM] = d[FRQ_MEAN] * W;
  d[FRQ_STDDEV] = sqrt (d[FRQ_VARIANCE]);
  d[FRQ_SEMEAN] = d[FRQ_STDDEV] / sqrt (W);
  d[FRQ_SESKEW] = calc_seskew (W);
  d[FRQ_SEKURT] = calc_sekurt (W);
}

/* Displays a table of all the statistics requested for variable V. */
static void
dump_statistics (const struct frq_proc *frq, const struct var_freqs *vf,
                 const struct variable *wv)
{
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : &F_8_0;
  const struct freq_tab *ft = &vf->tab;
  double stat_value[FRQ_N_STATS];
  struct tab_table *t;
  int i, r;

  if (var_is_alpha (vf->var))
    return;

  if (ft->n_valid == 0)
    {
      msg (SW, _("No valid data for variable %s; statistics not displayed."),
	   var_get_name (vf->var));
      return;
    }
  calc_stats (vf, stat_value);
  calc_percentiles (frq, vf);

  t = tab_create (3, frq->n_stats + frq->n_show_percentiles + 2);

  tab_box (t, TAL_1, TAL_1, -1, -1 , 0 , 0 , 2, tab_nr(t) - 1) ;


  tab_vline (t, TAL_1 , 2, 0, tab_nr(t) - 1);
  tab_vline (t, TAL_GAP , 1, 0, tab_nr(t) - 1 ) ;

  r=2; /* N missing and N valid are always dumped */

  for (i = 0; i < FRQ_N_STATS; i++)
    if (frq->stats & BIT_INDEX (i))
      {
	tab_text (t, 0, r, TAB_LEFT | TAT_TITLE,
		      gettext (st_name[i].s10));
	tab_double (t, 2, r, TAB_NONE, stat_value[i], NULL);
	r++;
      }

  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("N"));
  tab_text (t, 1, 0, TAB_LEFT | TAT_TITLE, _("Valid"));
  tab_text (t, 1, 1, TAB_LEFT | TAT_TITLE, _("Missing"));

  tab_double (t, 2, 0, TAB_NONE, ft->valid_cases, wfmt);
  tab_double (t, 2, 1, TAB_NONE, ft->total_cases - ft->valid_cases, wfmt);

  for (i = 0; i < frq->n_percentiles; i++)
    {
      struct percentile *pc = &frq->percentiles[i];

      if (!pc->show)
        continue;

      if ( i == 0 )
	{
	  tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, _("Percentiles"));
	}

      if (pc->p == 0.5)
        tab_text (t, 1, r, TAB_LEFT, _("50 (Median)"));
      else
        tab_fixed (t, 1, r, TAB_LEFT, pc->p * 100, 3, 0);
      tab_double (t, 2, r, TAB_NONE, pc->value,
                  var_get_print_format (vf->var));
      r++;
    }

  tab_title (t, "%s", var_to_string (vf->var));

  tab_submit (t);
}

static double
calculate_iqr (const struct frq_proc *frq)
{
  double q1 = SYSMIS;
  double q3 = SYSMIS;
  int i;

  for (i = 0; i < frq->n_percentiles; i++)
    {
      struct percentile *pc = &frq->percentiles[i];

      if (fabs (0.25 - pc->p) < DBL_EPSILON)
        q1 = pc->value;
      else if (fabs (0.75 - pc->p) < DBL_EPSILON)
        q3 = pc->value;
    }

  return q1 == SYSMIS || q3 == SYSMIS ? SYSMIS : q3 - q1;
}

static bool
chart_includes_value (const struct frq_chart *chart,
                      const struct variable *var,
                      const union value *value)
{
  if (!chart->include_missing && var_is_value_missing (var, value, MV_ANY))
    return false;

  if (var_is_numeric (var)
      && ((chart->x_min != SYSMIS && value->f < chart->x_min)
          || (chart->x_max != SYSMIS && value->f > chart->x_max)))
    return false;

  return true;
}

/* Create a gsl_histogram from a freq_tab */
struct histogram *
freq_tab_to_hist (const struct frq_proc *frq, const struct freq_tab *ft,
                  const struct variable *var)
{
  double x_min, x_max, valid_freq;
  int i;
  double bin_width;
  struct histogram *histogram;
  double iqr;

  /* Find out the extremes of the x value, within the range to be included in
     the histogram, and sum the total frequency of those values. */
  x_min = DBL_MAX;
  x_max = -DBL_MAX;
  valid_freq = 0;
  for (i = 0; i < ft->n_valid; i++)
    {
      const struct freq *f = &ft->valid[i];
      if (chart_includes_value (frq->hist, var, &f->value))
        {
          x_min = MIN (x_min, f->value.f);
          x_max = MAX (x_max, f->value.f);
          valid_freq += f->count;
        }
    }

  /* Freedman-Diaconis' choice of bin width. */
  iqr = calculate_iqr (frq);
  bin_width = 2 * iqr / pow (valid_freq, 1.0 / 3.0);

  histogram = histogram_create (bin_width, x_min, x_max);

  if ( histogram == NULL)
    return NULL;

  for (i = 0; i < ft->n_valid; i++)
    {
      const struct freq *f = &ft->valid[i];
      if (chart_includes_value (frq->hist, var, &f->value))
        histogram_add (histogram, f->value.f, f->count);
    }

  return histogram;
}

static int
add_slice (const struct frq_chart *pie, const struct freq *freq,
           const struct variable *var, struct slice *slice)
{
  if (chart_includes_value (pie, var, &freq->value))
    {
      ds_init_empty (&slice->label);
      var_append_value_name (var, &freq->value, &slice->label);
      slice->magnitude = freq->count;
      return 1;
    }
  else
    return 0;
}

/* Allocate an array of slices and fill them from the data in frq_tab
   n_slices will contain the number of slices allocated.
   The caller is responsible for freeing slices
*/
static struct slice *
freq_tab_to_slice_array(const struct frq_chart *pie,
                        const struct freq_tab *frq_tab,
			const struct variable *var,
			int *n_slicesp)
{
  struct slice *slices;
  int n_slices;
  int i;

  slices = xnmalloc (frq_tab->n_valid + frq_tab->n_missing, sizeof *slices);
  n_slices = 0;

  for (i = 0; i < frq_tab->n_valid; i++)
    n_slices += add_slice (pie, &frq_tab->valid[i], var, &slices[n_slices]);
  for (i = 0; i < frq_tab->n_missing; i++)
    n_slices += add_slice (pie, &frq_tab->missing[i], var, &slices[n_slices]);

  *n_slicesp = n_slices;
  return slices;
}




static void
do_piechart(const struct frq_chart *pie, const struct variable *var,
            const struct freq_tab *frq_tab)
{
  struct slice *slices;
  int n_slices, i;

  slices = freq_tab_to_slice_array (pie, frq_tab, var, &n_slices);

  if (n_slices < 2)
    msg (SW, _("Omitting pie chart for %s, which has only %d unique values."),
         var_get_name (var), n_slices);
  else if (n_slices > 50)
    msg (SW, _("Omitting pie chart for %s, which has over 50 unique values."),
         var_get_name (var));
  else
    chart_item_submit (piechart_create (var_to_string(var), slices, n_slices));

  for (i = 0; i < n_slices; i++)
    ds_destroy (&slices[i].label);
  free (slices);
}


/*
   Local Variables:
   mode: c
   End:
*/
