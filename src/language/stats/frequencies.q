/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010 Free Software Foundation, Inc.

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
#include <output/chart-item.h>
#include <output/charts/piechart.h>
#include <output/charts/plot-hist.h>
#include <output/tab.h>

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
  bool show;       /* True to show this percentile in the statistics box. */
};


static void add_percentile (double x, bool show);

static struct percentile *percentiles;
static int n_percentiles, n_show_percentiles;

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

/* Histogram and pie chart settings. */
static struct frq_chart hist, pie;

/* Parsed command. */
static struct cmd_frequencies cmd;

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
    const struct dictionary *dict; /* The dict from whence entries in the table
				      come */

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
static void dump_freq_table (const struct variable *, const struct variable *);
static void dump_statistics (const struct variable *, const struct variable *);
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
  n_show_percentiles = 0;
  percentiles = NULL;

  n_variables = 0;
  v_variables = NULL;

  if (!parse_frequencies (lexer, ds, &cmd, NULL))
    return CMD_FAILURE;

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
            add_percentile (subc_list_double_at(ptl_list, pl) / 100.0, true);
	}
    }
  if ( cmd.sbc_ntiles )
    {
      for ( i = 0 ; i < cmd.sbc_ntiles ; ++i )
	{
	  int j;
	  for (j = 0; j <= cmd.n_ntiles[i]; ++j )
            add_percentile (j / (double) cmd.n_ntiles[i], true);
	}
    }
  if (stats & BIT_INDEX (frq_median))
    {
      /* Treat the median as the 50% percentile.
         We output it in the percentiles table as "50 (Median)." */
      add_percentile (0.5, true);
      stats &= ~BIT_INDEX (frq_median);
      n_stats--;
    }
  if (cmd.sbc_histogram)
    {
      add_percentile (0.25, false);
      add_percentile (0.75, false);
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
  if (cmd.sbc_barchart)
    msg (SW, _("Bar charts are not implemented."));

  if (cmd.sbc_histogram)
    {
      hist.x_min = cmd.hi_min;
      hist.x_max = cmd.hi_max;
      hist.y_scale = cmd.hi_scale;
      hist.y_max = cmd.hi_scale == FRQ_FREQ ? cmd.hi_freq : cmd.hi_pcnt;
      hist.draw_normal = cmd.hi_norm != FRQ_NONORMAL;
      hist.include_missing = false;

      if (hist.x_min != SYSMIS && hist.x_max != SYSMIS
          && hist.x_min >= hist.x_max)
        {
          msg (SE, _("MAX for histogram must be greater than or equal to MIN, "
                     "but MIN was specified as %.15g and MAX as %.15g.  "
                     "MIN and MAX will be ignored."), hist.x_min, hist.x_max);
          hist.x_min = hist.x_max = SYSMIS;
        }
    }

  if (cmd.sbc_piechart)
    {
      pie.x_min = cmd.pie_min;
      pie.x_max = cmd.pie_max;
      pie.y_scale = cmd.pie_scale;
      pie.include_missing = cmd.pie_missing == FRQ_MISSING;

      if (pie.x_min != SYSMIS && pie.x_max != SYSMIS
          && pie.x_min >= pie.x_max)
        {
          msg (SE, _("MAX for pie chart must be greater than or equal to MIN, "
                     "but MIN was specified as %.15g and MAX as %.15g.  "
                     "MIN and MAX will be ignored."), pie.x_min, pie.x_max);
          pie.x_min = pie.x_max = SYSMIS;
        }
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

      postprocess_freq_tab (v);

      /* Frequencies tables. */
      n_categories = ft->n_valid + ft->n_missing;
      if  (cmd.table == FRQ_TABLE
           || (cmd.table == FRQ_LIMIT && n_categories <= cmd.limit))
        dump_freq_table (v, wv);

      /* Statistics. */
      if (n_stats)
	dump_statistics (v, wv);

      if (cmd.sbc_histogram && var_is_numeric (v) && ft->n_valid > 0)
	{
	  double d[frq_n_stats];
	  struct histogram *histogram;

	  calc_stats (v, d);

	  histogram = freq_tab_to_hist (ft, v);

          chart_item_submit (histogram_chart_create (
                               histogram->gsl_hist, var_to_string(v),
                               vf->tab.valid_cases,
                               d[frq_mean],
                               d[frq_stddev],
                               hist.draw_normal));

	  statistic_destroy (&histogram->parent);
	}

      if (cmd.sbc_piechart)
        do_piechart(v_variables[i], ft);

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
      vf->tab.dict = dataset_dict (ds);
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
   order.  If SHOW is true, the percentile will be shown in the statistics
   box, otherwise it will be hidden. */
static void
add_percentile (double x, bool show)
{
  int i;

  for (i = 0; i < n_percentiles; i++)
    {
      /* Do nothing if it's already in the list */
      if ( fabs(x - percentiles[i].p) < DBL_EPSILON )
        {
          if (show && !percentiles[i].show)
            {
              n_show_percentiles++;
              percentiles[i].show = true;
            }
          return;
        }

      if (x < percentiles[i].p)
	break;
    }

  if (i >= n_percentiles || x != percentiles[i].p)
    {
      percentiles = pool_nrealloc (syntax_pool, percentiles,
                                   n_percentiles + 1, sizeof *percentiles);
      insert_element (percentiles, n_percentiles, sizeof *percentiles, i);
      percentiles[i].p = x;
      percentiles[i].show = show;
      n_percentiles++;
      if (show)
        n_show_percentiles++;
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

/* Displays a full frequency table for variable V. */
static void
dump_freq_table (const struct variable *v, const struct variable *wv)
{
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : &F_8_0;
  int n_categories;
  struct var_freqs *vf;
  struct freq_tab *ft;
  struct freq_mutable *f;
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

  vf = get_var_freqs (v);
  ft = &vf->tab;
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

      label = var_lookup_value_label (v, &f->value);
      if (label != NULL)
        tab_text (t, 0, r, TAB_LEFT, label);

      tab_value (t, 1, r, TAB_NONE, &f->value, ft->dict, &vf->print);
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

      label = var_lookup_value_label (v, &f->value);
      if (label != NULL)
        tab_text (t, 0, r, TAB_LEFT, label);

      tab_value (t, 1, r, TAB_NONE, &f->value, ft->dict, &vf->print);
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

  tab_title (t, "%s", var_to_string (v));
  tab_submit (t);
}

/* Statistical display. */

/* Calculates all the pertinent statistics for variable V, putting them in
   array D[]. */
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

  assert (ft->n_valid > 0);

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
dump_statistics (const struct variable *v, const struct variable *wv)
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

  t = tab_create (3, n_stats + n_show_percentiles + 2);

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
      if (!percentiles[i].show)
        continue;

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

  tab_title (t, "%s", var_to_string (v));

  tab_submit (t);
}

static double
calculate_iqr (void)
{
  double q1 = SYSMIS;
  double q3 = SYSMIS;
  int i;

  for (i = 0; i < n_percentiles; i++)
    {
      if (fabs (0.25 - percentiles[i].p) < DBL_EPSILON)
        q1 = percentiles[i].value;
      else if (fabs (0.75 - percentiles[i].p) < DBL_EPSILON)
        q3 = percentiles[i].value;
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
freq_tab_to_hist (const struct freq_tab *ft, const struct variable *var)
{
  double x_min, x_max, valid_freq;
  int i;

  struct histogram *histogram;
  double iqr;
  int bins;

  /* Find out the extremes of the x value, within the range to be included in
     the histogram, and sum the total frequency of those values. */
  x_min = DBL_MAX;
  x_max = -DBL_MAX;
  valid_freq = 0;
  for (i = 0; i < ft->n_valid; i++)
    {
      const struct freq_mutable *frq = &ft->valid[i];
      if (chart_includes_value (&hist, var, &frq->value))
        {
          x_min = MIN (x_min, frq->value.f);
          x_max = MAX (x_max, frq->value.f);
          valid_freq += frq->count;
        }
    }

  /* Freedman-Diaconis' choice of bin width. */
  iqr = calculate_iqr ();
  if (iqr != SYSMIS)
    {
      double bin_width = 2 * iqr / pow (valid_freq, 1.0 / 3.0);
      bins = (x_max - x_min) / bin_width;
      if (bins < 5)
        bins = 5;
      else if (bins > 400)
        bins = 400;
    }
  else
    bins = 5;

  histogram = histogram_create (bins, x_min, x_max);
  for (i = 0; i < ft->n_valid; i++)
    {
      const struct freq_mutable *frq = &ft->valid[i];
      if (chart_includes_value (&hist, var, &frq->value))
        histogram_add (histogram, frq->value.f, frq->count);
    }

  return histogram;
}

static int
add_slice (const struct freq_mutable *freq, const struct variable *var,
           struct slice *slice)
{
  if (chart_includes_value (&pie, var, &freq->value))
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
freq_tab_to_slice_array(const struct freq_tab *frq_tab,
			const struct variable *var,
			int *n_slicesp)
{
  struct slice *slices;
  int n_slices;
  int i;

  slices = xnmalloc (frq_tab->n_valid + frq_tab->n_missing, sizeof *slices);
  n_slices = 0;

  for (i = 0; i < frq_tab->n_valid; i++)
    n_slices += add_slice (&frq_tab->valid[i], var, &slices[n_slices]);
  for (i = 0; i < frq_tab->n_missing; i++)
    n_slices += add_slice (&frq_tab->missing[i], var, &slices[n_slices]);

  *n_slicesp = n_slices;
  return slices;
}




static void
do_piechart(const struct variable *var, const struct freq_tab *frq_tab)
{
  struct slice *slices;
  int n_slices, i;

  slices = freq_tab_to_slice_array (frq_tab, var, &n_slices);

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
