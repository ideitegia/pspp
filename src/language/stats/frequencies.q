/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009 Free Software Foundation, Inc.

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

/*
  TODO:

  * Remember that histograms, bar charts need mean, stddev.
*/

#include <config.h>

#include <math.h>
#include <stdlib.h>
#include <gsl/gsl_histogram.h>

#include <data/case.h>
#include <data/casegrouper.h>
#include <data/casereader.h>
#include <data/dictionary.h>
#include <data/format.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/dictionary/split-file.h>
#include <language/lexer/lexer.h>
#include <libpspp/array.h>
#include <libpspp/bit-vector.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>
#include <math/histogram.h>
#include <math/moments.h>
#include <output/chart.h>
#include <output/charts/piechart.h>
#include <output/charts/plot-hist.h>
#include <output/manager.h>
#include <output/output.h>
#include <output/table.h>

#include "freq.h"

#include "minmax.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* (headers) */

/* (specification)
   FREQUENCIES (frq_):
     *+variables=custom;
     +format=cond:condense/onepage(*n:onepage_limit,"%s>=0")/!standard,
	     table:limit(n:limit,"%s>0")/notable/!table,
	     labels:!labels/nolabels,
	     sort:!avalue/dvalue/afreq/dfreq,
	     spaces:!single/double,
	     paging:newpage/!oldpage;
     missing=miss:include/!exclude;
     barchart(ba_)=:minimum(d:min),
	    :maximum(d:max),
	    scale:freq(*n:freq,"%s>0")/percent(*n:pcnt,"%s>0");
     piechart(pie_)=:minimum(d:min),
	    :maximum(d:max),
	    missing:missing/!nomissing;
     histogram(hi_)=:minimum(d:min),
	    :maximum(d:max),
	    scale:freq(*n:freq,"%s>0")/percent(*n:pcnt,"%s>0"),
	    norm:!nonormal/normal,
	    incr:increment(d:inc,"%s>0");
     hbar(hb_)=:minimum(d:min),
	    :maximum(d:max),
	    scale:freq(*n:freq,"%s>0")/percent(*n:pcnt,"%s>0"),
	    norm:!nonormal/normal,
	    incr:increment(d:inc,"%s>0");
     +grouped=custom;
     +ntiles=integer;
     +percentiles = double list;
     +statistics[st_]=1|mean,2|semean,3|median,4|mode,5|stddev,6|variance,
 	    7|kurtosis,8|skewness,9|range,10|minimum,11|maximum,12|sum,
 	    13|default,14|seskewness,15|sekurtosis,all,none.
*/
/* (declarations) */
/* (functions) */

/* Statistics. */
enum
  {
    frq_mean = 0, frq_semean, frq_median, frq_mode, frq_stddev, frq_variance,
    frq_kurt, frq_sekurt, frq_skew, frq_seskew, frq_range, frq_min, frq_max,
    frq_sum, frq_n_stats
  };

/* Description of a statistic. */
struct frq_info
  {
    int st_indx;		/* Index into a_statistics[]. */
    const char *s10;		/* Identifying string. */
  };

/* Table of statistics, indexed by dsc_*. */
static const struct frq_info st_name[frq_n_stats + 1] =
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
  double x1;       /* The datum value <= the percentile */
  double x2;       /* The datum value >= the percentile */
  int flag;
  int flag2;       /* Set to 1 if this percentile value has been found */
};


static void add_percentile (double x) ;

static struct percentile *percentiles;
static int n_percentiles;

/* Groups of statistics. */
#define BI          BIT_INDEX
#define frq_default							\
	(BI (frq_mean) | BI (frq_stddev) | BI (frq_min) | BI (frq_max))
#define frq_all							\
	(BI (frq_sum) | BI(frq_min) | BI(frq_max)		\
	 | BI(frq_mean) | BI(frq_semean) | BI(frq_stddev)	\
	 | BI(frq_variance) | BI(frq_kurt) | BI(frq_sekurt)	\
	 | BI(frq_skew) | BI(frq_seskew) | BI(frq_range)	\
	 | BI(frq_range) | BI(frq_mode) | BI(frq_median))

/* Statistics; number of statistics. */
static unsigned long stats;
static int n_stats;

/* Types of graphs. */
enum
  {
    GFT_NONE,			/* Don't draw graphs. */
    GFT_BAR,			/* Draw bar charts. */
    GFT_HIST,			/* Draw histograms. */
    GFT_PIE,                    /* Draw piechart */
    GFT_HBAR			/* Draw bar charts or histograms at our discretion. */
  };

/* Parsed command. */
static struct cmd_frequencies cmd;

/* Summary of the barchart, histogram, and hbar subcommands. */
/* FIXME: These should not be mututally exclusive */
static int chart;		/* NONE/BAR/HIST/HBAR/PIE. */
static double min, max;		/* Minimum, maximum on y axis. */
static int format;		/* FREQ/PERCENT: Scaling of y axis. */
static double scale, incr;	/* FIXME */
static int normal;		/* FIXME */

/* Variables for which to calculate statistics. */
static size_t n_variables;
static const struct variable **v_variables;

/* Pools. */
static struct pool *data_pool;  	/* For per-SPLIT FILE group data. */
static struct pool *syntax_pool;        /* For syntax-related data. */

/* Frequency tables. */

/* Entire frequency table. */
struct freq_tab
  {
    struct hsh_table *data;	/* Undifferentiated data. */
    struct freq_mutable *valid; /* Valid freqs. */
    int n_valid;		/* Number of total freqs. */

    struct freq_mutable *missing; /* Missing freqs. */
    int n_missing;		/* Number of missing freqs. */

    /* Statistics. */
    double total_cases;		/* Sum of weights of all cases. */
    double valid_cases;		/* Sum of weights of valid cases. */
  };


/* Per-variable frequency data. */
struct var_freqs
  {
    /* Freqency table. */
    struct freq_tab tab;	/* Frequencies table to use. */

    /* Percentiles. */
    int n_groups;		/* Number of groups. */
    double *groups;		/* Groups. */

    /* Statistics. */
    double stat[frq_n_stats];

    /* Variable attributes. */
    int width;
    struct fmt_spec print;
  };

static inline struct var_freqs *
get_var_freqs (const struct variable *v)
{
  return var_get_aux (v);
}

static void determine_charts (void);

static void calc_stats (const struct variable *v, double d[frq_n_stats]);

static void precalc (struct casereader *, struct dataset *);
static void calc (const struct ccase *, const struct dataset *);
static void postcalc (const struct dataset *);

static void postprocess_freq_tab (const struct variable *);
static void dump_full ( const struct variable *, const struct variable *);
static void dump_condensed (const struct variable *, const struct variable *);
static void dump_statistics (const struct variable *, bool show_varname, const struct variable *);
static void cleanup_freq_tab (const struct variable *);

static hsh_compare_func compare_value_numeric_a, compare_value_alpha_a;
static hsh_compare_func compare_value_numeric_d, compare_value_alpha_d;
static hsh_compare_func compare_freq_numeric_a, compare_freq_alpha_a;
static hsh_compare_func compare_freq_numeric_d, compare_freq_alpha_d;


static void do_piechart(const struct variable *var,
			const struct freq_tab *frq_tab);

struct histogram *
freq_tab_to_hist(const struct freq_tab *ft, const struct variable *var);



/* Parser and outline. */

static int internal_cmd_frequencies (struct lexer *lexer, struct dataset *ds);

int
cmd_frequencies (struct lexer *lexer, struct dataset *ds)
{
  int result;

  syntax_pool = pool_create ();
  result = internal_cmd_frequencies (lexer, ds);
  pool_destroy (syntax_pool);
  syntax_pool=0;
  pool_destroy (data_pool);
  data_pool=0;
  free (v_variables);
  v_variables=0;
  return result;
}

static int
internal_cmd_frequencies (struct lexer *lexer, struct dataset *ds)
{
  struct casegrouper *grouper;
  struct casereader *input, *group;
  bool ok;
  int i;

  n_percentiles = 0;
  percentiles = NULL;

  n_variables = 0;
  v_variables = NULL;

  if (!parse_frequencies (lexer, ds, &cmd, NULL))
    return CMD_FAILURE;

  if (cmd.onepage_limit == LONG_MIN)
    cmd.onepage_limit = 50;

  /* Figure out statistics to calculate. */
  stats = 0;
  if (cmd.a_statistics[FRQ_ST_DEFAULT] || !cmd.sbc_statistics)
    stats |= frq_default;
  if (cmd.a_statistics[FRQ_ST_ALL])
    stats |= frq_all;
  if (cmd.sort != FRQ_AVALUE && cmd.sort != FRQ_DVALUE)
    stats &= ~BIT_INDEX (frq_median);
  for (i = 0; i < frq_n_stats; i++)
    if (cmd.a_statistics[st_name[i].st_indx])
      stats |= BIT_INDEX (i);
  if (stats & frq_kurt)
    stats |= BIT_INDEX (frq_sekurt);
  if (stats & frq_skew)
    stats |= BIT_INDEX (frq_seskew);

  /* Calculate n_stats. */
  n_stats = 0;
  for (i = 0; i < frq_n_stats; i++)
    if ((stats & BIT_INDEX (i)))
      n_stats++;

  /* Charting. */
  determine_charts ();
  if (chart != GFT_NONE || cmd.sbc_ntiles)
    cmd.sort = FRQ_AVALUE;

  /* Work out what percentiles need to be calculated */
  if ( cmd.sbc_percentiles )
    {
      for ( i = 0 ; i < MAXLISTS ; ++i )
	{
	  int pl;
	  subc_list_double *ptl_list = &cmd.dl_percentiles[i];
	  for ( pl = 0 ; pl < subc_list_double_count(ptl_list); ++pl)
	      add_percentile (subc_list_double_at(ptl_list, pl) / 100.0 );
	}
    }
  if ( cmd.sbc_ntiles )
    {
      for ( i = 0 ; i < cmd.sbc_ntiles ; ++i )
	{
	  int j;
	  for (j = 0; j <= cmd.n_ntiles[i]; ++j )
	      add_percentile (j / (double) cmd.n_ntiles[i]);
	}
    }
  if (stats & BIT_INDEX (frq_median))
    {
      /* Treat the median as the 50% percentile.
         We output it in the percentiles table as "50 (Median)." */
      add_percentile (0.5);
      stats &= ~BIT_INDEX (frq_median);
      n_stats--;
    }

  /* Do it! */
  input = casereader_create_filter_weight (proc_open (ds), dataset_dict (ds),
                                           NULL, NULL);
  grouper = casegrouper_create_splits (input, dataset_dict (ds));
  for (; casegrouper_get_next_group (grouper, &group);
       casereader_destroy (group))
    {
      struct ccase *c;

      precalc (group, ds);
      for (; (c = casereader_read (group)) != NULL; case_unref (c))
        calc (c, ds);
      postcalc (ds);
    }
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  free_frequencies(&cmd);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

/* Figure out which charts the user requested.  */
static void
determine_charts (void)
{
  int count = (!!cmd.sbc_histogram) + (!!cmd.sbc_barchart) +
    (!!cmd.sbc_hbar) + (!!cmd.sbc_piechart);

  if (!count)
    {
      chart = GFT_NONE;
      return;
    }
  else if (count > 1)
    {
      chart = GFT_HBAR;
      msg (SW, _("At most one of BARCHART, HISTOGRAM, or HBAR should be "
	   "given.  HBAR will be assumed.  Argument values will be "
	   "given precedence increasing along the order given."));
    }
  else if (cmd.sbc_histogram)
    chart = GFT_HIST;
  else if (cmd.sbc_barchart)
    chart = GFT_BAR;
  else if (cmd.sbc_piechart)
    chart = GFT_PIE;
  else
    chart = GFT_HBAR;

  min = max = SYSMIS;
  format = FRQ_FREQ;
  scale = SYSMIS;
  incr = SYSMIS;
  normal = 0;

  if (cmd.sbc_barchart)
    {
      if (cmd.ba_min != SYSMIS)
	min = cmd.ba_min;
      if (cmd.ba_max != SYSMIS)
	max = cmd.ba_max;
      if (cmd.ba_scale == FRQ_FREQ)
	{
	  format = FRQ_FREQ;
	  scale = cmd.ba_freq;
	}
      else if (cmd.ba_scale == FRQ_PERCENT)
	{
	  format = FRQ_PERCENT;
	  scale = cmd.ba_pcnt;
	}
    }

  if (cmd.sbc_histogram)
    {
      if (cmd.hi_min != SYSMIS)
	min = cmd.hi_min;
      if (cmd.hi_max != SYSMIS)
	max = cmd.hi_max;
      if (cmd.hi_scale == FRQ_FREQ)
	{
	  format = FRQ_FREQ;
	  scale = cmd.hi_freq;
	}
      else if (cmd.hi_scale == FRQ_PERCENT)
	{
	  format = FRQ_PERCENT;
	  scale = cmd.ba_pcnt;
	}
      if (cmd.hi_norm != FRQ_NONORMAL )
	normal = 1;
      if (cmd.hi_incr == FRQ_INCREMENT)
	incr = cmd.hi_inc;
    }

  if (cmd.sbc_hbar)
    {
      if (cmd.hb_min != SYSMIS)
	min = cmd.hb_min;
      if (cmd.hb_max != SYSMIS)
	max = cmd.hb_max;
      if (cmd.hb_scale == FRQ_FREQ)
	{
	  format = FRQ_FREQ;
	  scale = cmd.hb_freq;
	}
      else if (cmd.hb_scale == FRQ_PERCENT)
	{
	  format = FRQ_PERCENT;
	  scale = cmd.ba_pcnt;
	}
      if (cmd.hb_norm)
	normal = 1;
      if (cmd.hb_incr == FRQ_INCREMENT)
	incr = cmd.hb_inc;
    }

  if (min != SYSMIS && max != SYSMIS && min >= max)
    {
      msg (SE, _("MAX must be greater than or equal to MIN, if both are "
	   "specified.  However, MIN was specified as %g and MAX as %g.  "
	   "MIN and MAX will be ignored."), min, max);
      min = max = SYSMIS;
    }
}

/* Add data from case C to the frequency table. */
static void
calc (const struct ccase *c, const struct dataset *ds)
{
  double weight = dict_get_case_weight (dataset_dict (ds), c, NULL);
  size_t i;

  for (i = 0; i < n_variables; i++)
    {
      const struct variable *v = v_variables[i];
      const union value *val = case_data (c, v);
      struct var_freqs *vf = get_var_freqs (v);
      struct freq_tab *ft = &vf->tab;

      struct freq_mutable target;
      struct freq_mutable **fpp;

      target.value = *val;
      fpp = (struct freq_mutable **) hsh_probe (ft->data, &target);

      if (*fpp != NULL)
        (*fpp)->count += weight;
      else
        {
          struct freq_mutable *fp = pool_alloc (data_pool, sizeof *fp);
          fp->count = weight;
          value_init_pool (data_pool, &fp->value, vf->width);
          value_copy (&fp->value, val, vf->width);
          *fpp = fp;
        }
    }
}

/* Prepares each variable that is the target of FREQUENCIES by setting
   up its hash table. */
static void
precalc (struct casereader *input, struct dataset *ds)
{
  struct ccase *c;
  size_t i;

  c = casereader_peek (input, 0);
  if (c != NULL)
    {
      output_split_file_values (ds, c);
      case_unref (c);
    }

  pool_destroy (data_pool);
  data_pool = pool_create ();

  for (i = 0; i < n_variables; i++)
    {
      const struct variable *v = v_variables[i];
      struct freq_tab *ft = &get_var_freqs (v)->tab;

      ft->data = hsh_create (16, compare_freq, hash_freq, NULL, v);
    }
}

/* Finishes up with the variables after frequencies have been
   calculated.  Displays statistics, percentiles, ... */
static void
postcalc (const struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);
  const struct variable *wv = dict_get_weight (dict);
  size_t i;

  for (i = 0; i < n_variables; i++)
    {
      const struct variable *v = v_variables[i];
      struct var_freqs *vf = get_var_freqs (v);
      struct freq_tab *ft = &vf->tab;
      int n_categories;
      int dumped_freq_tab = 1;

      postprocess_freq_tab (v);

      /* Frequencies tables. */
      n_categories = ft->n_valid + ft->n_missing;
      if (cmd.table == FRQ_TABLE
	  || (cmd.table == FRQ_LIMIT && n_categories <= cmd.limit))
	switch (cmd.cond)
	  {
	  case FRQ_CONDENSE:
	    dump_condensed (v, wv);
	    break;
	  case FRQ_STANDARD:
	    dump_full (v, wv);
	    break;
	  case FRQ_ONEPAGE:
	    if (n_categories > cmd.onepage_limit)
	      dump_condensed (v, wv);
	    else
	      dump_full (v, wv);
	    break;
	  default:
            NOT_REACHED ();
	  }
      else
	dumped_freq_tab = 0;

      /* Statistics. */
      if (n_stats)
	dump_statistics (v, !dumped_freq_tab, wv);



      if ( chart == GFT_HIST && var_is_numeric (v) )
	{
	  double d[frq_n_stats];
	  struct histogram *hist ;

	  calc_stats (v, d);

	  hist = freq_tab_to_hist (ft,v);

	  histogram_plot (hist, var_to_string(v),
			  vf->tab.valid_cases,
			  d[frq_mean],
			  d[frq_stddev],
			  normal);

	  statistic_destroy ((struct statistic *)hist);
	}

      if ( chart == GFT_PIE)
	{
	  do_piechart(v_variables[i], ft);
	}

      cleanup_freq_tab (v);

    }
}

/* Returns the comparison function that should be used for
   sorting a frequency table by FRQ_SORT using VAL_TYPE
   values. */
static hsh_compare_func *
get_freq_comparator (int frq_sort, enum val_type val_type)
{
  bool is_numeric = val_type == VAL_NUMERIC;
  switch (frq_sort)
    {
    case FRQ_AVALUE:
      return is_numeric ? compare_value_numeric_a : compare_value_alpha_a;
    case FRQ_DVALUE:
      return is_numeric ? compare_value_numeric_d : compare_value_alpha_d;
    case FRQ_AFREQ:
      return is_numeric ? compare_freq_numeric_a : compare_freq_alpha_a;
    case FRQ_DFREQ:
      return is_numeric ? compare_freq_numeric_d : compare_freq_alpha_d;
    default:
      NOT_REACHED ();
    }
}

/* Returns true iff the value in struct freq_mutable F is non-missing
   for variable V. */
static bool
not_missing (const void *f_, const void *v_)
{
  const struct freq_mutable *f = f_;
  const struct variable *v = v_;

  return !var_is_value_missing (v, &f->value, MV_ANY);
}

/* Summarizes the frequency table data for variable V. */
static void
postprocess_freq_tab (const struct variable *v)
{
  hsh_compare_func *compare;
  struct freq_tab *ft;
  size_t count;
  void *const *data;
  struct freq_mutable *freqs, *f;
  size_t i;

  ft = &get_var_freqs (v)->tab;
  compare = get_freq_comparator (cmd.sort, var_get_type (v));

  /* Extract data from hash table. */
  count = hsh_count (ft->data);
  data = hsh_data (ft->data);

  /* Copy dereferenced data into freqs. */
  freqs = xnmalloc (count, sizeof *freqs);
  for (i = 0; i < count; i++)
    {
      struct freq_mutable *f = data[i];
      freqs[i] = *f;
    }

  /* Put data into ft. */
  ft->valid = freqs;
  ft->n_valid = partition (freqs, count, sizeof *freqs, not_missing, v);
  ft->missing = freqs + ft->n_valid;
  ft->n_missing = count - ft->n_valid;

  /* Sort data. */
  sort (ft->valid, ft->n_valid, sizeof *ft->valid, compare, v);
  sort (ft->missing, ft->n_missing, sizeof *ft->missing, compare, v);

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
cleanup_freq_tab (const struct variable *v)
{
  struct freq_tab *ft = &get_var_freqs (v)->tab;
  free (ft->valid);
  hsh_destroy (ft->data);
}

/* Parses the VARIABLES subcommand, adding to
   {n_variables,v_variables}. */
static int
frq_custom_variables (struct lexer *lexer, struct dataset *ds, struct cmd_frequencies *cmd UNUSED, void *aux UNUSED)
{
  size_t old_n_variables = n_variables;
  size_t i;

  lex_match (lexer, '=');
  if (lex_token (lexer) != T_ALL && (lex_token (lexer) != T_ID
                         || dict_lookup_var (dataset_dict (ds), lex_tokid (lexer)) == NULL))
    return 2;

  if (!parse_variables_const (lexer, dataset_dict (ds), &v_variables, &n_variables,
			PV_APPEND | PV_NO_SCRATCH))
    return 0;

  for (i = old_n_variables; i < n_variables; i++)
    {
      const struct variable *v = v_variables[i];
      struct var_freqs *vf;

      if (var_get_aux (v) != NULL)
	{
	  msg (SE, _("Variable %s specified multiple times on VARIABLES "
		     "subcommand."), var_get_name (v));
	  return 0;
	}
      vf = var_attach_aux (v, xmalloc (sizeof *vf), var_dtor_free);
      vf->tab.valid = vf->tab.missing = NULL;
      vf->n_groups = 0;
      vf->groups = NULL;
      vf->width = var_get_width (v);
      vf->print = *var_get_print_format (v);
    }
  return 1;
}

/* Parses the GROUPED subcommand, setting the n_grouped, grouped
   fields of specified variables. */
static int
frq_custom_grouped (struct lexer *lexer, struct dataset *ds, struct cmd_frequencies *cmd UNUSED, void *aux UNUSED)
{
  lex_match (lexer, '=');
  if ((lex_token (lexer) == T_ID && dict_lookup_var (dataset_dict (ds), lex_tokid (lexer)) != NULL)
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
	if (lex_match (lexer, '('))
	  {
	    nl = ml = 0;
	    dl = NULL;
	    while (lex_integer (lexer))
	      {
		if (nl >= ml)
		  {
		    ml += 16;
		    dl = pool_nrealloc (syntax_pool, dl, ml, sizeof *dl);
		  }
		dl[nl++] = lex_tokval (lexer);
		lex_get (lexer);
		lex_match (lexer, ',');
	      }
	    /* Note that nl might still be 0 and dl might still be
	       NULL.  That's okay. */
	    if (!lex_match (lexer, ')'))
	      {
		free (v);
		msg (SE, _("`)' expected after GROUPED interval list."));
		return 0;
	      }
	  }
	else
          {
            nl = 0;
            dl = NULL;
          }

	for (i = 0; i < n; i++)
          if (var_get_aux (v[i]) == NULL)
            msg (SE, _("Variables %s specified on GROUPED but not on "
                       "VARIABLES."), var_get_name (v[i]));
          else
            {
              struct var_freqs *vf = get_var_freqs (v[i]);

              if (vf->groups != NULL)
                msg (SE, _("Variables %s specified multiple times on GROUPED "
                           "subcommand."), var_get_name (v[i]));
              else
                {
                  vf->n_groups = nl;
                  vf->groups = dl;
                }
            }
	free (v);
	if (!lex_match (lexer, '/'))
	  break;
	if ((lex_token (lexer) != T_ID || dict_lookup_var (dataset_dict (ds), lex_tokid (lexer)) != NULL)
            && lex_token (lexer) != T_ALL)
	  {
	    lex_put_back (lexer, '/');
	    break;
	  }
      }

  return 1;
}

/* Adds X to the list of percentiles, keeping the list in proper
   order. */
static void
add_percentile (double x)
{
  int i;

  for (i = 0; i < n_percentiles; i++)
    {
      /* Do nothing if it's already in the list */
      if ( fabs(x - percentiles[i].p) < DBL_EPSILON )
	return;

      if (x < percentiles[i].p)
	break;
    }

  if (i >= n_percentiles || x != percentiles[i].p)
    {
      percentiles = pool_nrealloc (syntax_pool, percentiles,
                                   n_percentiles + 1, sizeof *percentiles);
      insert_element (percentiles, n_percentiles, sizeof *percentiles, i);
      percentiles[i].p = x;
      n_percentiles++;
    }
}

/* Comparison functions. */

/* Ascending numeric compare of values. */
static int
compare_value_numeric_a (const void *a_, const void *b_, const void *aux UNUSED)
{
  const struct freq_mutable *a = a_;
  const struct freq_mutable *b = b_;

  if (a->value.f > b->value.f)
    return 1;
  else if (a->value.f < b->value.f)
    return -1;
  else
    return 0;
}

/* Ascending string compare of values. */
static int
compare_value_alpha_a (const void *a_, const void *b_, const void *v_)
{
  const struct freq_mutable *a = a_;
  const struct freq_mutable *b = b_;
  const struct variable *v = v_;
  struct var_freqs *vf = get_var_freqs (v);

  return value_compare_3way (&a->value, &b->value, vf->width);
}

/* Descending numeric compare of values. */
static int
compare_value_numeric_d (const void *a, const void *b, const void *aux UNUSED)
{
  return -compare_value_numeric_a (a, b, aux);
}

/* Descending string compare of values. */
static int
compare_value_alpha_d (const void *a, const void *b, const void *v)
{
  return -compare_value_alpha_a (a, b, v);
}

/* Ascending numeric compare of frequency;
   secondary key on ascending numeric value. */
static int
compare_freq_numeric_a (const void *a_, const void *b_, const void *aux UNUSED)
{
  const struct freq_mutable *a = a_;
  const struct freq_mutable *b = b_;

  if (a->count > b->count)
    return 1;
  else if (a->count < b->count)
    return -1;

  if (a->value.f > b->value.f)
    return 1;
  else if (a->value.f < b->value.f)
    return -1;
  else
    return 0;
}

/* Ascending numeric compare of frequency;
   secondary key on ascending string value. */
static int
compare_freq_alpha_a (const void *a_, const void *b_, const void *v_)
{
  const struct freq_mutable *a = a_;
  const struct freq_mutable *b = b_;
  const struct variable *v = v_;
  struct var_freqs *vf = get_var_freqs (v);

  if (a->count > b->count)
    return 1;
  else if (a->count < b->count)
    return -1;
  else
    return value_compare_3way (&a->value, &b->value, vf->width);
}

/* Descending numeric compare of frequency;
   secondary key on ascending numeric value. */
static int
compare_freq_numeric_d (const void *a_, const void *b_, const void *aux UNUSED)
{
  const struct freq_mutable *a = a_;
  const struct freq_mutable *b = b_;

  if (a->count > b->count)
    return -1;
  else if (a->count < b->count)
    return 1;

  if (a->value.f > b->value.f)
    return 1;
  else if (a->value.f < b->value.f)
    return -1;
  else
    return 0;
}

/* Descending numeric compare of frequency;
   secondary key on ascending string value. */
static int
compare_freq_alpha_d (const void *a_, const void *b_, const void *v_)
{
  const struct freq_mutable *a = a_;
  const struct freq_mutable *b = b_;
  const struct variable *v = v_;
  struct var_freqs *vf = get_var_freqs (v);

  if (a->count > b->count)
    return -1;
  else if (a->count < b->count)
    return 1;
  else
    return value_compare_3way (&a->value, &b->value, vf->width);
}

/* Frequency table display. */

struct full_dim_aux
  {
    bool show_labels;
  };

/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
full_dim (struct tab_rendering *r, void *aux_)
{
  const struct outp_driver *d = r->driver;
  const struct tab_table *t = r->table;
  const struct full_dim_aux *aux = aux_;
  int i;

  for (i = 0; i < t->nc; i++)
    {
      r->w[i] = tab_natural_width (r, i);
      if (aux->show_labels && i == 0)
        r->w[i] = MIN (r->w[i], d->prop_em_width * 15);
      else
        r->w[i] = MAX (r->w[i], d->prop_em_width * 8);
    }

  for (i = 0; i < t->nr; i++)
    r->h[i] = d->font_height;
}

static void
full_dim_free (void *aux_)
{
  struct full_dim_aux *aux = aux_;
  free (aux);
}

/* Displays a full frequency table for variable V. */
static void
dump_full (const struct variable *v, const struct variable *wv)
{
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : &F_8_0;
  int n_categories;
  struct var_freqs *vf;
  struct freq_tab *ft;
  struct freq_mutable *f;
  struct tab_table *t;
  int r;
  double cum_total = 0.0;
  double cum_freq = 0.0;

  struct init
    {
      int c, r;
      const char *s;
    };

  const struct init *p;

  static const struct init vec[] =
  {
    {4, 0, N_("Valid")},
    {5, 0, N_("Cum")},
    {1, 1, N_("Value")},
    {2, 1, N_("Frequency")},
    {3, 1, N_("Percent")},
    {4, 1, N_("Percent")},
    {5, 1, N_("Percent")},
    {0, 0, NULL},
    {1, 0, NULL},
    {2, 0, NULL},
    {3, 0, NULL},
    {-1, -1, NULL},
  };

  const bool lab = (cmd.labels == FRQ_LABELS);

  struct full_dim_aux *aux;

  vf = get_var_freqs (v);
  ft = &vf->tab;
  n_categories = ft->n_valid + ft->n_missing;
  t = tab_create (5 + lab, n_categories + 3, 0);
  tab_headers (t, 0, 0, 2, 0);

  aux = xmalloc (sizeof *aux);
  aux->show_labels = lab;
  tab_dim (t, full_dim, full_dim_free, aux);

  if (lab)
    tab_text (t, 0, 1, TAB_CENTER | TAT_TITLE, _("Value Label"));

  for (p = vec; p->s; p++)
    tab_text (t, lab ? p->c : p->c - 1, p->r,
		  TAB_CENTER | TAT_TITLE, gettext (p->s));

  r = 2;
  for (f = ft->valid; f < ft->missing; f++)
    {
      double percent, valid_percent;

      cum_freq += f->count;

      percent = f->count / ft->total_cases * 100.0;
      valid_percent = f->count / ft->valid_cases * 100.0;
      cum_total += valid_percent;

      if (lab)
	{
	  const char *label = var_lookup_value_label (v, &f->value);
	  if (label != NULL)
	    tab_text (t, 0, r, TAB_LEFT, label);
	}

      tab_value (t, 0 + lab, r, TAB_NONE, &f->value, &vf->print);
      tab_double (t, 1 + lab, r, TAB_NONE, f->count, wfmt);
      tab_double (t, 2 + lab, r, TAB_NONE, percent, NULL);
      tab_double (t, 3 + lab, r, TAB_NONE, valid_percent, NULL);
      tab_double (t, 4 + lab, r, TAB_NONE, cum_total, NULL);
      r++;
    }
  for (; f < &ft->valid[n_categories]; f++)
    {
      cum_freq += f->count;

      if (lab)
	{
	  const char *label = var_lookup_value_label (v, &f->value);
	  if (label != NULL)
	    tab_text (t, 0, r, TAB_LEFT, label);
	}

      tab_value (t, 0 + lab, r, TAB_NONE, &f->value, &vf->print);
      tab_double (t, 1 + lab, r, TAB_NONE, f->count, wfmt);
      tab_double (t, 2 + lab, r, TAB_NONE,
		     f->count / ft->total_cases * 100.0, NULL);
      tab_text (t, 3 + lab, r, TAB_NONE, _("Missing"));
      r++;
    }

  tab_box (t, TAL_1, TAL_1,
	   cmd.spaces == FRQ_SINGLE ? -1 : TAL_GAP, TAL_1,
	   0, 0, 4 + lab, r);
  tab_hline (t, TAL_2, 0, 4 + lab, 2);
  tab_hline (t, TAL_2, 0, 4 + lab, r);
  tab_joint_text (t, 0, r, 0 + lab, r, TAB_RIGHT | TAT_TITLE, _("Total"));
  tab_vline (t, TAL_0, 1, r, r);
  tab_double (t, 1 + lab, r, TAB_NONE, cum_freq, wfmt);
  tab_fixed (t, 2 + lab, r, TAB_NONE, 100.0, 5, 1);
  tab_fixed (t, 3 + lab, r, TAB_NONE, 100.0, 5, 1);

  tab_title (t, "%s", var_to_string (v));
  tab_submit (t);
}

/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
condensed_dim (struct tab_rendering *r, void *aux UNUSED)
{
  struct outp_driver *d = r->driver;
  const struct tab_table *t = r->table;

  int cum_width = outp_string_width (d, _("Cum"), OUTP_PROPORTIONAL);
  int zeros_width = outp_string_width (d, "000", OUTP_PROPORTIONAL);
  int max_width = MAX (cum_width, zeros_width);

  int i;

  for (i = 0; i < 2; i++)
    {
      r->w[i] = tab_natural_width (r, i);
      r->w[i] = MAX (r->w[i], d->prop_em_width * 8);
    }
  for (i = 2; i < 4; i++)
    r->w[i] = max_width;
  for (i = 0; i < t->nr; i++)
    r->h[i] = d->font_height;
}

/* Display condensed frequency table for variable V. */
static void
dump_condensed (const struct variable *v, const struct variable *wv)
{
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : &F_8_0;
  int n_categories;
  struct var_freqs *vf;
  struct freq_tab *ft;
  struct freq_mutable *f;
  struct tab_table *t;
  int r;
  double cum_total = 0.0;

  vf = get_var_freqs (v);
  ft = &vf->tab;
  n_categories = ft->n_valid + ft->n_missing;
  t = tab_create (4, n_categories + 2, 0);

  tab_headers (t, 0, 0, 2, 0);
  tab_text (t, 0, 1, TAB_CENTER | TAT_TITLE, _("Value"));
  tab_text (t, 1, 1, TAB_CENTER | TAT_TITLE, _("Freq"));
  tab_text (t, 2, 1, TAB_CENTER | TAT_TITLE, _("Pct"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Cum"));
  tab_text (t, 3, 1, TAB_CENTER | TAT_TITLE, _("Pct"));
  tab_dim (t, condensed_dim, NULL, NULL);

  r = 2;
  for (f = ft->valid; f < ft->missing; f++)
    {
      double percent;

      percent = f->count / ft->total_cases * 100.0;
      cum_total += f->count / ft->valid_cases * 100.0;

      tab_value (t, 0, r, TAB_NONE, &f->value, &vf->print);
      tab_double (t, 1, r, TAB_NONE, f->count, wfmt);
      tab_double (t, 2, r, TAB_NONE, percent, NULL);
      tab_double (t, 3, r, TAB_NONE, cum_total, NULL);
      r++;
    }
  for (; f < &ft->valid[n_categories]; f++)
    {
      tab_value (t, 0, r, TAB_NONE, &f->value, &vf->print);
      tab_double (t, 1, r, TAB_NONE, f->count, wfmt);
      tab_double (t, 2, r, TAB_NONE,
		 f->count / ft->total_cases * 100.0, NULL);
      r++;
    }

  tab_box (t, TAL_1, TAL_1,
	   cmd.spaces == FRQ_SINGLE ? -1 : TAL_GAP, TAL_1,
	   0, 0, 3, r - 1);
  tab_hline (t, TAL_2, 0, 3, 2);
  tab_title (t, "%s", var_to_string (v));
  tab_columns (t, SOM_COL_DOWN, 1);
  tab_submit (t);
}

/* Statistical display. */

/* Calculates all the pertinent statistics for variable V, putting
   them in array D[].  FIXME: This could be made much more optimal. */
static void
calc_stats (const struct variable *v, double d[frq_n_stats])
{
  struct freq_tab *ft = &get_var_freqs (v)->tab;
  double W = ft->valid_cases;
  struct moments *m;
  struct freq_mutable *f=0;
  int most_often;
  double X_mode;

  double rank;
  int i = 0;
  int idx;

  /* Calculate percentiles. */

  for (i = 0; i < n_percentiles; i++)
    {
      percentiles[i].flag = 0;
      percentiles[i].flag2 = 0;
    }

  rank = 0;
  for (idx = 0; idx < ft->n_valid; ++idx)
    {
      static double prev_value = SYSMIS;
      f = &ft->valid[idx];
      rank += f->count ;
      for (i = 0; i < n_percentiles; i++)
        {
	  double tp;
	  if ( percentiles[i].flag2  ) continue ;

	  if ( settings_get_algorithm () != COMPATIBLE )
	    tp =
	      (ft->valid_cases - 1) *  percentiles[i].p;
	  else
	    tp =
	      (ft->valid_cases + 1) *  percentiles[i].p - 1;

	  if ( percentiles[i].flag )
	    {
	      percentiles[i].x2 = f->value.f;
	      percentiles[i].x1 = prev_value;
	      percentiles[i].flag2 = 1;
	      continue;
	    }

          if (rank >  tp )
	  {
	    if ( f->count > 1 && rank - (f->count - 1) > tp )
	      {
		percentiles[i].x2 = percentiles[i].x1 = f->value.f;
		percentiles[i].flag2 = 1;
	      }
	    else
	      {
		percentiles[i].flag=1;
	      }

	    continue;
	  }
        }
      prev_value = f->value.f;
    }

  for (i = 0; i < n_percentiles; i++)
    {
      /* Catches the case when p == 100% */
      if ( ! percentiles[i].flag2 )
	percentiles[i].x1 = percentiles[i].x2 = f->value.f;

      /*
      printf("percentile %d (p==%.2f); X1 = %g; X2 = %g\n",
	     i,percentiles[i].p,percentiles[i].x1,percentiles[i].x2);
      */
    }

  for (i = 0; i < n_percentiles; i++)
    {
      struct freq_tab *ft = &get_var_freqs (v)->tab;
      double s;

      double dummy;
      if ( settings_get_algorithm () != COMPATIBLE )
	{
	  s = modf((ft->valid_cases - 1) * percentiles[i].p , &dummy);
	}
      else
	{
	  s = modf((ft->valid_cases + 1) * percentiles[i].p -1, &dummy);
	}

      percentiles[i].value = percentiles[i].x1 +
	( percentiles[i].x2 - percentiles[i].x1) * s ;
    }


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
  moments_calculate (m, NULL, &d[frq_mean], &d[frq_variance],
                     &d[frq_skew], &d[frq_kurt]);
  moments_destroy (m);

  /* Formulas below are taken from _SPSS Statistical Algorithms_. */
  d[frq_min] = ft->valid[0].value.f;
  d[frq_max] = ft->valid[ft->n_valid - 1].value.f;
  d[frq_mode] = X_mode;
  d[frq_range] = d[frq_max] - d[frq_min];
  d[frq_sum] = d[frq_mean] * W;
  d[frq_stddev] = sqrt (d[frq_variance]);
  d[frq_semean] = d[frq_stddev] / sqrt (W);
  d[frq_seskew] = calc_seskew (W);
  d[frq_sekurt] = calc_sekurt (W);
}

/* Displays a table of all the statistics requested for variable V. */
static void
dump_statistics (const struct variable *v, bool show_varname,
		 const struct variable *wv)
{
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : &F_8_0;
  struct freq_tab *ft;
  double stat_value[frq_n_stats];
  struct tab_table *t;
  int i, r;

  if (var_is_alpha (v))
    return;
  ft = &get_var_freqs (v)->tab;
  if (ft->n_valid == 0)
    {
      msg (SW, _("No valid data for variable %s; statistics not displayed."),
	   var_get_name (v));
      return;
    }
  calc_stats (v, stat_value);

  t = tab_create (3, n_stats + n_percentiles + 2, 0);
  tab_dim (t, tab_natural_dimensions, NULL, NULL);

  tab_box (t, TAL_1, TAL_1, -1, -1 , 0 , 0 , 2, tab_nr(t) - 1) ;


  tab_vline (t, TAL_1 , 2, 0, tab_nr(t) - 1);
  tab_vline (t, TAL_GAP , 1, 0, tab_nr(t) - 1 ) ;

  r=2; /* N missing and N valid are always dumped */

  for (i = 0; i < frq_n_stats; i++)
    if (stats & BIT_INDEX (i))
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

  for (i = 0; i < n_percentiles; i++, r++)
    {
      if ( i == 0 )
	{
	  tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, _("Percentiles"));
	}

      if (percentiles[i].p == 0.5)
        tab_text (t, 1, r, TAB_LEFT, _("50 (Median)"));
      else
        tab_fixed (t, 1, r, TAB_LEFT, percentiles[i].p * 100, 3, 0);
      tab_double (t, 2, r, TAB_NONE, percentiles[i].value,
		  var_get_print_format (v));
    }

  tab_columns (t, SOM_COL_DOWN, 1);
  if (show_varname)
    tab_title (t, "%s", var_to_string (v));
  else
    tab_flags (t, SOMF_NO_TITLE);


  tab_submit (t);
}


/* Create a gsl_histogram from a freq_tab */
struct histogram *
freq_tab_to_hist (const struct freq_tab *ft, const struct variable *var)
{
  int i;
  double x_min = DBL_MAX;
  double x_max = -DBL_MAX;

  struct statistic *hist;
  const double bins = 11;

  struct hsh_iterator hi;
  struct hsh_table *fh = ft->data;
  struct freq_mutable *frq;

  /* Find out the extremes of the x value */
  for ( frq = hsh_first(fh, &hi); frq != 0; frq = hsh_next(fh, &hi) )
    {
      if (var_is_value_missing(var, &frq->value, MV_ANY))
	continue;

      if ( frq->value.f < x_min ) x_min = frq->value.f ;
      if ( frq->value.f > x_max ) x_max = frq->value.f ;
    }

  hist = histogram_create (bins, x_min, x_max);

  for( i = 0 ; i < ft->n_valid ; ++i )
    {
      frq = &ft->valid[i];
      histogram_add ((struct histogram *)hist, frq->value.f, frq->count);
    }

  return (struct histogram *)hist;
}


static struct slice *
freq_tab_to_slice_array(const struct freq_tab *frq_tab,
			const struct variable *var,
			int *n_slices);


/* Allocate an array of slices and fill them from the data in frq_tab
   n_slices will contain the number of slices allocated.
   The caller is responsible for freeing slices
*/
static struct slice *
freq_tab_to_slice_array(const struct freq_tab *frq_tab,
			const struct variable *var,
			int *n_slices)
{
  int i;
  struct slice *slices;

  *n_slices = frq_tab->n_valid;

  slices = xnmalloc (*n_slices, sizeof *slices);

  for (i = 0 ; i < *n_slices ; ++i )
    {
      const struct freq_mutable *frq = &frq_tab->valid[i];

      ds_init_empty (&slices[i].label);
      var_append_value_name (var, &frq->value, &slices[i].label);
      slices[i].magnetude = frq->count;
    }

  return slices;
}




static void
do_piechart(const struct variable *var, const struct freq_tab *frq_tab)
{
  struct slice *slices;
  int n_slices, i;

  slices = freq_tab_to_slice_array(frq_tab, var, &n_slices);

  piechart_plot(var_to_string(var), slices, n_slices);

  for (i = 0 ; i < n_slices ; ++i )
    {
      ds_destroy (&slices[i].label);
    }

  free(slices);
}


/*
   Local Variables:
   mode: c
   End:
*/
