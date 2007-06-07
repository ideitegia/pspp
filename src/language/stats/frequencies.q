/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

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
#include <libpspp/alloc.h>
#include <libpspp/array.h>
#include <libpspp/bit-vector.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/magic.h>
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

static int implicit_50th ; 

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

/* Arenas used to store semi-permanent storage. */
static struct pool *int_pool;	/* Integer mode. */
static struct pool *gen_pool;	/* General mode. */

/* Frequency tables. */

/* Types of frequency tables. */
enum
  {
    FRQM_GENERAL,
    FRQM_INTEGER
  };

/* Entire frequency table. */
struct freq_tab
  {
    int mode;			/* FRQM_GENERAL or FRQM_INTEGER. */

    /* General mode. */
    struct hsh_table *data;	/* Undifferentiated data. */

    /* Integer mode. */
    double *vector;		/* Frequencies proper. */
    int min, max;		/* The boundaries of the table. */
    double out_of_range;	/* Sum of weights of out-of-range values. */
    double sysmis;		/* Sum of weights of SYSMIS values. */

    /* All modes. */
    struct freq *valid;         /* Valid freqs. */
    int n_valid;		/* Number of total freqs. */

    struct freq *missing;	/* Missing freqs. */
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

    /* Width and format for analysis and display.
       This is normally the same as "width" and "print" in struct
       variable, but in SPSS-compatible mode only the first
       MAX_SHORT_STRING bytes of long string variables are
       included. */
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
static void postcalc (void);

static void postprocess_freq_tab (const struct variable *);
static void dump_full (const struct variable *);
static void dump_condensed (const struct variable *);
static void dump_statistics (const struct variable *, int show_varname);
static void cleanup_freq_tab (const struct variable *);

static hsh_compare_func compare_value_numeric_a, compare_value_alpha_a;
static hsh_compare_func compare_value_numeric_d, compare_value_alpha_d;
static hsh_compare_func compare_freq_numeric_a, compare_freq_alpha_a;
static hsh_compare_func compare_freq_numeric_d, compare_freq_alpha_d;


static void do_piechart(const struct variable *var,
			const struct freq_tab *frq_tab);

gsl_histogram * 
freq_tab_to_hist(const struct freq_tab *ft, const struct variable *var);



/* Parser and outline. */

static int internal_cmd_frequencies (struct lexer *lexer, struct dataset *ds);

int
cmd_frequencies (struct lexer *lexer, struct dataset *ds)
{
  int result;

  int_pool = pool_create ();
  result = internal_cmd_frequencies (lexer, ds);
  pool_destroy (int_pool);
  int_pool=0;
  pool_destroy (gen_pool);
  gen_pool=0;
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

  if (cmd.onepage_limit == NOT_LONG)
    cmd.onepage_limit = 50;

  /* Figure out statistics to calculate. */
  stats = 0;
  if (cmd.a_statistics[FRQ_ST_DEFAULT] || !cmd.sbc_statistics)
    stats |= frq_default;
  if (cmd.a_statistics[FRQ_ST_ALL])
    stats |= frq_all;
  if (cmd.sort != FRQ_AVALUE && cmd.sort != FRQ_DVALUE)
    stats &= ~frq_median;
  for (i = 0; i < frq_n_stats; i++)
    if (cmd.a_statistics[st_name[i].st_indx])
      stats |= BIT_INDEX (i);
  if (stats & frq_kurt)
    stats |= frq_sekurt;
  if (stats & frq_skew)
    stats |= frq_seskew;

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
  

  /* Do it! */
  input = casereader_create_filter_weight (proc_open (ds), dataset_dict (ds),
                                           NULL, NULL);
  grouper = casegrouper_create_splits (input, dataset_dict (ds));
  for (; casegrouper_get_next_group (grouper, &group);
       casereader_destroy (group)) 
    {
      struct ccase c;
      
      precalc (group, ds);
      for (; casereader_read (group, &c); case_destroy (&c)) 
        calc (&c, ds);
      postcalc ();
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

      switch (ft->mode)
	{
	  case FRQM_GENERAL:
	    {
	      /* General mode. */
              struct freq target;
	      struct freq **fpp;

              target.value = (union value *) val;
              fpp = (struct freq **) hsh_probe (ft->data, &target);

	      if (*fpp != NULL)
		(*fpp)->count += weight;
	      else
		{
		  struct freq *fp = pool_alloc (gen_pool, sizeof *fp);
                  fp->count = weight;
                  fp->value = pool_clone (gen_pool,
                                          val,
                                          MAX (MAX_SHORT_STRING, vf->width));
                  *fpp = fp;
		}
	    }
	  break;
	case FRQM_INTEGER:
	  /* Integer mode. */
	  if (val->f == SYSMIS)
	    ft->sysmis += weight;
	  else if (val->f > INT_MIN+1 && val->f < INT_MAX-1)
	    {
	      int i = val->f;
	      if (i >= ft->min && i <= ft->max)
		ft->vector[i - ft->min] += weight;
	    }
	  else
	    ft->out_of_range += weight;
	  break;
	default:
          NOT_REACHED ();
	}
    }
}

/* Prepares each variable that is the target of FREQUENCIES by setting
   up its hash table. */
static void
precalc (struct casereader *input, struct dataset *ds)
{
  struct ccase c;
  size_t i;

  if (!casereader_peek (input, 0, &c))
    return;
  output_split_file_values (ds, &c);
  case_destroy (&c);

  pool_destroy (gen_pool);
  gen_pool = pool_create ();
  
  for (i = 0; i < n_variables; i++)
    {
      const struct variable *v = v_variables[i];
      struct freq_tab *ft = &get_var_freqs (v)->tab;

      if (ft->mode == FRQM_GENERAL)
	{
	  ft->data = hsh_create (16, compare_freq, hash_freq, NULL, v);
	}
      else
	{
	  int j;

	  for (j = (ft->max - ft->min); j >= 0; j--)
	    ft->vector[j] = 0.0;
	  ft->out_of_range = 0.0;
	  ft->sysmis = 0.0;
	}
    }
}

/* Finishes up with the variables after frequencies have been
   calculated.  Displays statistics, percentiles, ... */
static void
postcalc (void)
{
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
	    dump_condensed (v);
	    break;
	  case FRQ_STANDARD:
	    dump_full (v);
	    break;
	  case FRQ_ONEPAGE:
	    if (n_categories > cmd.onepage_limit)
	      dump_condensed (v);
	    else
	      dump_full (v);
	    break;
	  default:
            NOT_REACHED ();
	  }
      else
	dumped_freq_tab = 0;

      /* Statistics. */
      if (n_stats)
	dump_statistics (v, !dumped_freq_tab);



      if ( chart == GFT_HIST) 
	{
	  double d[frq_n_stats];
	  struct normal_curve norm;
	  gsl_histogram *hist ;


	  norm.N = vf->tab.valid_cases;

	  calc_stats (v, d);
	  norm.mean = d[frq_mean];
	  norm.stddev = d[frq_stddev];

	  hist = freq_tab_to_hist(ft,v);

	  histogram_plot(hist, var_to_string(v), &norm, normal);

	  gsl_histogram_free(hist);
	}


      if ( chart == GFT_PIE) 
	{
	  do_piechart(v_variables[i], ft);
	}



      cleanup_freq_tab (v);

    }
}

/* Returns the comparison function that should be used for
   sorting a frequency table by FRQ_SORT using VAR_TYPE
   variables. */
static hsh_compare_func *
get_freq_comparator (int frq_sort, enum var_type var_type) 
{
  bool is_numeric = var_type == VAR_NUMERIC;
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

/* Returns true iff the value in struct freq F is non-missing
   for variable V. */
static bool
not_missing (const void *f_, const void *v_) 
{
  const struct freq *f = f_;
  const struct variable *v = v_;

  return !var_is_value_missing (v, f->value, MV_ANY);
}

/* Summarizes the frequency table data for variable V. */
static void
postprocess_freq_tab (const struct variable *v)
{
  hsh_compare_func *compare;
  struct freq_tab *ft;
  size_t count;
  void *const *data;
  struct freq *freqs, *f;
  size_t i;

  ft = &get_var_freqs (v)->tab;
  assert (ft->mode == FRQM_GENERAL);
  compare = get_freq_comparator (cmd.sort, var_get_type (v));

  /* Extract data from hash table. */
  count = hsh_count (ft->data);
  data = hsh_data (ft->data);

  /* Copy dereferenced data into freqs. */
  freqs = xnmalloc (count, sizeof *freqs);
  for (i = 0; i < count; i++) 
    {
      struct freq *f = data[i];
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
  assert (ft->mode == FRQM_GENERAL);
  free (ft->valid);
  hsh_destroy (ft->data);
}

/* Parses the VARIABLES subcommand, adding to
   {n_variables,v_variables}. */
static int
frq_custom_variables (struct lexer *lexer, struct dataset *ds, struct cmd_frequencies *cmd UNUSED, void *aux UNUSED)
{
  int mode;
  int min = 0, max = 0;

  size_t old_n_variables = n_variables;
  size_t i;

  lex_match (lexer, '=');
  if (lex_token (lexer) != T_ALL && (lex_token (lexer) != T_ID
                         || dict_lookup_var (dataset_dict (ds), lex_tokid (lexer)) == NULL))
    return 2;

  if (!parse_variables_const (lexer, dataset_dict (ds), &v_variables, &n_variables,
			PV_APPEND | PV_NO_SCRATCH))
    return 0;

  if (!lex_match (lexer, '('))
    mode = FRQM_GENERAL;
  else
    {
      mode = FRQM_INTEGER;
      if (!lex_force_int (lexer))
	return 0;
      min = lex_integer (lexer);
      lex_get (lexer);
      if (!lex_force_match (lexer, ','))
	return 0;
      if (!lex_force_int (lexer))
	return 0;
      max = lex_integer (lexer);
      lex_get (lexer);
      if (!lex_force_match (lexer, ')'))
	return 0;
      if (max < min)
	{
	  msg (SE, _("Upper limit of integer mode value range must be "
		     "greater than lower limit."));
	  return 0;
	}
    }

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
      if (mode == FRQM_INTEGER && !var_is_numeric (v))
        {
          msg (SE, _("Integer mode specified, but %s is not a numeric "
                     "variable."), var_get_name (v));
          return 0;
        }

      vf = var_attach_aux (v, xmalloc (sizeof *vf), var_dtor_free);
      vf->tab.mode = mode;
      vf->tab.valid = vf->tab.missing = NULL;
      if (mode == FRQM_INTEGER)
	{
	  vf->tab.min = min;
	  vf->tab.max = max;
	  vf->tab.vector = pool_nalloc (int_pool,
                                        max - min + 1, sizeof *vf->tab.vector);
	}
      else 
        vf->tab.vector = NULL;
      vf->n_groups = 0;
      vf->groups = NULL;
      vf->width = var_get_width (v);
      vf->print = *var_get_print_format (v);
      if (vf->width > MAX_SHORT_STRING && get_algorithm () == COMPATIBLE) 
        {
          enum fmt_type type = var_get_print_format (v)->type;
          vf->width = MAX_SHORT_STRING;
          vf->print.w = MAX_SHORT_STRING * (type == FMT_AHEX ? 2 : 1);
        }
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
		    dl = pool_nrealloc (int_pool, dl, ml, sizeof *dl);
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
      percentiles = pool_nrealloc (int_pool, percentiles,
                                   n_percentiles + 1, sizeof *percentiles);

      if (i < n_percentiles)
          memmove (&percentiles[i + 1], &percentiles[i],
                   (n_percentiles - i) * sizeof (struct percentile) );

      percentiles[i].p = x;
      n_percentiles++;
    }
}

/* Comparison functions. */

/* Ascending numeric compare of values. */
static int
compare_value_numeric_a (const void *a_, const void *b_, const void *aux UNUSED)
{
  const struct freq *a = a_;
  const struct freq *b = b_;

  if (a->value[0].f > b->value[0].f)
    return 1;
  else if (a->value[0].f < b->value[0].f)
    return -1;
  else
    return 0;
}

/* Ascending string compare of values. */
static int
compare_value_alpha_a (const void *a_, const void *b_, const void *v_)
{
  const struct freq *a = a_;
  const struct freq *b = b_;
  const struct variable *v = v_;
  struct var_freqs *vf = get_var_freqs (v);

  return memcmp (a->value[0].s, b->value[0].s, vf->width);
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
  const struct freq *a = a_;
  const struct freq *b = b_;

  if (a->count > b->count)
    return 1;
  else if (a->count < b->count)
    return -1;

  if (a->value[0].f > b->value[0].f)
    return 1;
  else if (a->value[0].f < b->value[0].f)
    return -1;
  else
    return 0;
}

/* Ascending numeric compare of frequency;
   secondary key on ascending string value. */
static int
compare_freq_alpha_a (const void *a_, const void *b_, const void *v_)
{
  const struct freq *a = a_;
  const struct freq *b = b_;
  const struct variable *v = v_;
  struct var_freqs *vf = get_var_freqs (v);

  if (a->count > b->count)
    return 1;
  else if (a->count < b->count)
    return -1;
  else
    return memcmp (a->value[0].s, b->value[0].s, vf->width);
}

/* Descending numeric compare of frequency;
   secondary key on ascending numeric value. */
static int
compare_freq_numeric_d (const void *a_, const void *b_, const void *aux UNUSED)
{
  const struct freq *a = a_;
  const struct freq *b = b_;

  if (a->count > b->count)
    return -1;
  else if (a->count < b->count)
    return 1;

  if (a->value[0].f > b->value[0].f)
    return 1;
  else if (a->value[0].f < b->value[0].f)
    return -1;
  else
    return 0;
}

/* Descending numeric compare of frequency;
   secondary key on ascending string value. */
static int
compare_freq_alpha_d (const void *a_, const void *b_, const void *v_)
{
  const struct freq *a = a_;
  const struct freq *b = b_;
  const struct variable *v = v_;
  struct var_freqs *vf = get_var_freqs (v);

  if (a->count > b->count)
    return -1;
  else if (a->count < b->count)
    return 1;
  else
    return memcmp (a->value[0].s, b->value[0].s, vf->width);
}

/* Frequency table display. */

/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
full_dim (struct tab_table *t, struct outp_driver *d)
{
  int i = 0;
  int columns = 5;

  if (cmd.labels == FRQ_LABELS)
    {
    t->w[0] = MIN (tab_natural_width (t, d, 0), d->prop_em_width * 15);
      i = 1;
      columns ++;
    }

  for (;i < columns; i++)
    t->w[i] = MAX (tab_natural_width (t, d, i), d->prop_em_width * 8);

  for (i = 0; i < t->nr; i++)
    t->h[i] = d->font_height;
}

/* Displays a full frequency table for variable V. */
static void
dump_full (const struct variable *v)
{
  int n_categories;
  struct var_freqs *vf;
  struct freq_tab *ft;
  struct freq *f;
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

  vf = get_var_freqs (v);
  ft = &vf->tab;
  n_categories = ft->n_valid + ft->n_missing;
  t = tab_create (5 + lab, n_categories + 3, 0);
  tab_headers (t, 0, 0, 2, 0);
  tab_dim (t, full_dim);

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
	  const char *label = var_lookup_value_label (v, &f->value[0]);
	  if (label != NULL)
	    tab_text (t, 0, r, TAB_LEFT, label);
	}

      tab_value (t, 0 + lab, r, TAB_NONE, f->value, &vf->print);
      tab_float (t, 1 + lab, r, TAB_NONE, f->count, 8, 0);
      tab_float (t, 2 + lab, r, TAB_NONE, percent, 5, 1);
      tab_float (t, 3 + lab, r, TAB_NONE, valid_percent, 5, 1);
      tab_float (t, 4 + lab, r, TAB_NONE, cum_total, 5, 1);
      r++;
    }
  for (; f < &ft->valid[n_categories]; f++)
    {
      cum_freq += f->count;

      if (lab)
	{
	  const char *label = var_lookup_value_label (v, &f->value[0]);
	  if (label != NULL)
	    tab_text (t, 0, r, TAB_LEFT, label);
	}

      tab_value (t, 0 + lab, r, TAB_NONE, f->value, &vf->print);
      tab_float (t, 1 + lab, r, TAB_NONE, f->count, 8, 0);
      tab_float (t, 2 + lab, r, TAB_NONE,
		     f->count / ft->total_cases * 100.0, 5, 1);
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
  tab_float (t, 1 + lab, r, TAB_NONE, cum_freq, 8, 0);
  tab_float (t, 2 + lab, r, TAB_NONE, 100.0, 5, 1);
  tab_float (t, 3 + lab, r, TAB_NONE, 100.0, 5, 1);

  tab_title (t, "%s", var_to_string (v));
  tab_submit (t);
}

/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
condensed_dim (struct tab_table *t, struct outp_driver *d)
{
  int cum_w = MAX (outp_string_width (d, _("Cum"), OUTP_PROPORTIONAL),
		   MAX (outp_string_width (d, _("Cum"), OUTP_PROPORTIONAL),
			outp_string_width (d, "000", OUTP_PROPORTIONAL)));

  int i;

  for (i = 0; i < 2; i++)
    t->w[i] = MAX (tab_natural_width (t, d, i), d->prop_em_width * 8);
  for (i = 2; i < 4; i++)
    t->w[i] = cum_w;
  for (i = 0; i < t->nr; i++)
    t->h[i] = d->font_height;
}

/* Display condensed frequency table for variable V. */
static void
dump_condensed (const struct variable *v)
{
  int n_categories;
  struct var_freqs *vf;
  struct freq_tab *ft;
  struct freq *f;
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
  tab_dim (t, condensed_dim);

  r = 2;
  for (f = ft->valid; f < ft->missing; f++)
    {
      double percent;

      percent = f->count / ft->total_cases * 100.0;
      cum_total += f->count / ft->valid_cases * 100.0;

      tab_value (t, 0, r, TAB_NONE, f->value, &vf->print);
      tab_float (t, 1, r, TAB_NONE, f->count, 8, 0);
      tab_float (t, 2, r, TAB_NONE, percent, 3, 0);
      tab_float (t, 3, r, TAB_NONE, cum_total, 3, 0);
      r++;
    }
  for (; f < &ft->valid[n_categories]; f++)
    {
      tab_value (t, 0, r, TAB_NONE, f->value, &vf->print);
      tab_float (t, 1, r, TAB_NONE, f->count, 8, 0);
      tab_float (t, 2, r, TAB_NONE,
		 f->count / ft->total_cases * 100.0, 3, 0);
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
  struct freq *f=0; 
  int most_often;
  double X_mode;

  double rank;
  int i = 0;
  int idx;
  double *median_value;

  /* Calculate percentiles. */

  /* If the 50th percentile was not explicitly requested then we must 
     calculate it anyway --- it's the median */
  median_value = 0 ;
  for (i = 0; i < n_percentiles; i++) 
    {
      if (percentiles[i].p == 0.5)
	{
	  median_value = &percentiles[i].value;
	  break;
	}
    }

  if ( 0 == median_value )  
    {
      add_percentile (0.5);
      implicit_50th = 1;
    }

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

	  if ( get_algorithm() != COMPATIBLE ) 
	    tp = 
	      (ft->valid_cases - 1) *  percentiles[i].p;
	  else
	    tp = 
	      (ft->valid_cases + 1) *  percentiles[i].p - 1;

	  if ( percentiles[i].flag ) 
	    {
	      percentiles[i].x2 = f->value[0].f;
	      percentiles[i].x1 = prev_value;
	      percentiles[i].flag2 = 1;
	      continue;
	    }

          if (rank >  tp ) 
	  {
	    if ( f->count > 1 && rank - (f->count - 1) > tp ) 
	      {
		percentiles[i].x2 = percentiles[i].x1 = f->value[0].f;
		percentiles[i].flag2 = 1;
	      }
	    else
	      {
		percentiles[i].flag=1;
	      }

	    continue;
	  }
        }
      prev_value = f->value[0].f;
    }

  for (i = 0; i < n_percentiles; i++) 
    {
      /* Catches the case when p == 100% */
      if ( ! percentiles[i].flag2 ) 
	percentiles[i].x1 = percentiles[i].x2 = f->value[0].f;

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
      if ( get_algorithm() != COMPATIBLE ) 
	{
	  s = modf((ft->valid_cases - 1) * percentiles[i].p , &dummy);
	}
      else
	{
	  s = modf((ft->valid_cases + 1) * percentiles[i].p -1, &dummy);
	}

      percentiles[i].value = percentiles[i].x1 + 
	( percentiles[i].x2 - percentiles[i].x1) * s ; 

      if ( percentiles[i].p == 0.50) 
	median_value = &percentiles[i].value; 
    }


  /* Calculate the mode. */
  most_often = -1;
  X_mode = SYSMIS;
  for (f = ft->valid; f < ft->missing; f++)
    {
      if (most_often < f->count) 
        {
          most_often = f->count;
          X_mode = f->value[0].f;
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
    moments_pass_one (m, f->value[0].f, f->count);
  for (f = ft->valid; f < ft->missing; f++)
    moments_pass_two (m, f->value[0].f, f->count);
  moments_calculate (m, NULL, &d[frq_mean], &d[frq_variance],
                     &d[frq_skew], &d[frq_kurt]);
  moments_destroy (m);
                     
  /* Formulas below are taken from _SPSS Statistical Algorithms_. */
  d[frq_min] = ft->valid[0].value[0].f;
  d[frq_max] = ft->valid[ft->n_valid - 1].value[0].f;
  d[frq_mode] = X_mode;
  d[frq_range] = d[frq_max] - d[frq_min];
  d[frq_median] = *median_value;
  d[frq_sum] = d[frq_mean] * W;
  d[frq_stddev] = sqrt (d[frq_variance]);
  d[frq_semean] = d[frq_stddev] / sqrt (W);
  d[frq_seskew] = calc_seskew (W);
  d[frq_sekurt] = calc_sekurt (W);
}

/* Displays a table of all the statistics requested for variable V. */
static void
dump_statistics (const struct variable *v, int show_varname)
{
  struct freq_tab *ft;
  double stat_value[frq_n_stats];
  struct tab_table *t;
  int i, r;

  int n_explicit_percentiles = n_percentiles;

  if ( implicit_50th && n_percentiles > 0 ) 
    --n_percentiles;

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

  t = tab_create (3, n_stats + n_explicit_percentiles + 2, 0);
  tab_dim (t, tab_natural_dimensions);

  tab_box (t, TAL_1, TAL_1, -1, -1 , 0 , 0 , 2, tab_nr(t) - 1) ;


  tab_vline (t, TAL_1 , 2, 0, tab_nr(t) - 1);
  tab_vline (t, TAL_GAP , 1, 0, tab_nr(t) - 1 ) ;
  
  r=2; /* N missing and N valid are always dumped */

  for (i = 0; i < frq_n_stats; i++)
    if (stats & BIT_INDEX (i))
      {
	tab_text (t, 0, r, TAB_LEFT | TAT_TITLE,
		      gettext (st_name[i].s10));
	tab_float (t, 2, r, TAB_NONE, stat_value[i], 11, 3);
	r++;
      }

  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("N"));
  tab_text (t, 1, 0, TAB_LEFT | TAT_TITLE, _("Valid"));
  tab_text (t, 1, 1, TAB_LEFT | TAT_TITLE, _("Missing"));

  tab_float(t, 2, 0, TAB_NONE, ft->valid_cases, 11, 0);
  tab_float(t, 2, 1, TAB_NONE, ft->total_cases - ft->valid_cases, 11, 0);


  for (i = 0; i < n_explicit_percentiles; i++, r++) 
    {
      if ( i == 0 ) 
	{ 
	  tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, _("Percentiles"));
	}

      tab_float (t, 1, r, TAB_LEFT, percentiles[i].p * 100, 3, 0 );
      tab_float (t, 2, r, TAB_NONE, percentiles[i].value, 11, 3);

    }

  tab_columns (t, SOM_COL_DOWN, 1);
  if (show_varname)
    tab_title (t, "%s", var_to_string (v));
  else
    tab_flags (t, SOMF_NO_TITLE);


  tab_submit (t);
}


/* Create a gsl_histogram from a freq_tab */
gsl_histogram *
freq_tab_to_hist(const struct freq_tab *ft, const struct variable *var)
{
  int i;
  double x_min = DBL_MAX;
  double x_max = -DBL_MAX;

  gsl_histogram *hist;
  const double bins = 11;

  struct hsh_iterator hi;
  struct hsh_table *fh = ft->data;
  struct freq *frq;

  /* Find out the extremes of the x value */
  for ( frq = hsh_first(fh, &hi); frq != 0; frq = hsh_next(fh, &hi) ) 
    {
      if (var_is_value_missing(var, frq->value, MV_ANY))
	continue;

      if ( frq->value[0].f < x_min ) x_min = frq->value[0].f ;
      if ( frq->value[0].f > x_max ) x_max = frq->value[0].f ;
    }

  hist = histogram_create(bins, x_min, x_max);

  for( i = 0 ; i < ft->n_valid ; ++i ) 
    {
      frq = &ft->valid[i];
      gsl_histogram_accumulate(hist, frq->value[0].f, frq->count);
    }

  return hist;
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
      const struct freq *frq = &frq_tab->valid[i];

      slices[i].label = var_get_value_name (var, frq->value);
      slices[i].magnetude = frq->count;
    }

  return slices;
}




static void
do_piechart(const struct variable *var, const struct freq_tab *frq_tab)
{
  struct slice *slices;
  int n_slices;

  slices = freq_tab_to_slice_array(frq_tab, var, &n_slices);

  piechart_plot(var_to_string(var), slices, n_slices);

  free(slices);
}


/* 
   Local Variables:
   mode: c
   End:
*/
