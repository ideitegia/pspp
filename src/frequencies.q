/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

/*
  TODO:

  * Remember that histograms, bar charts need mean, stddev.
*/

#include <config.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include "alloc.h"
#include "bitvector.h"
#include "hash.h"
#include "pool.h"
#include "command.h"
#include "lexer.h"
#include "error.h"
#include "algorithm.h"
#include "magic.h"
#include "misc.h"
#include "stats.h"
#include "output.h"
#include "som.h"
#include "str.h"
#include "tab.h"
#include "value-labels.h"
#include "var.h"
#include "vfm.h"

#include "debug-print.h"

/* (specification)
   FREQUENCIES (frq_):
     *variables=custom;
     format=cond:condense/onepage(*n:onepage_limit,"%s>=0")/!standard,
	    table:limit(n:limit,"%s>0")/notable/!table, 
	    labels:!labels/nolabels,
	    sort:!avalue/dvalue/afreq/dfreq,
	    spaces:!single/double,
	    paging:newpage/!oldpage;
     missing=miss:include/!exclude;
     barchart(ba_)=:minimum(d:min),
	    :maximum(d:max),
	    scale:freq(*n:freq,"%s>0")/percent(*n:pcnt,"%s>0");
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
     grouped=custom;
     ntiles=custom;
     percentiles=custom;
     statistics[st_]=1|mean,2|semean,3|median,4|mode,5|stddev,6|variance,
	    7|kurtosis,8|skewness,9|range,10|minimum,11|maximum,12|sum,
	    13|default,14|seskewness,15|sekurtosis,all,none.
*/
/* (declarations) */
/* (functions) */

/* Description of a statistic. */
struct frq_info
  {
    int st_indx;		/* Index into a_statistics[]. */
    const char *s10;		/* Identifying string. */
  };

/* Table of statistics, indexed by dsc_*. */
static struct frq_info st_name[frq_n_stats + 1] =
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
static double *percentiles;
static double *percentile_values;
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
    GFT_HBAR			/* Draw bar charts or histograms at our discretion. */
  };

/* Parsed command. */
static struct cmd_frequencies cmd;

/* Summary of the barchart, histogram, and hbar subcommands. */
static int chart;		/* NONE/BAR/HIST/HBAR. */
static double min, max;		/* Minimum, maximum on y axis. */
static int format;		/* FREQ/PERCENT: Scaling of y axis. */
static double scale, incr;	/* FIXME */
static int normal;		/* FIXME */

/* Variables for which to calculate statistics. */
static int n_variables;
static struct variable **v_variables;

/* Arenas used to store semi-permanent storage. */
static struct pool *int_pool;	/* Integer mode. */
static struct pool *gen_pool;	/* General mode. */

/* Easier access to a_statistics. */
#define stat cmd.a_statistics

static void determine_charts (void);

static void precalc (void *);
static int calc (struct ccase *, void *);
static void postcalc (void *);

static void postprocess_freq_tab (struct variable *);
static void dump_full (struct variable *);
static void dump_condensed (struct variable *);
static void dump_statistics (struct variable *, int show_varname);
static void cleanup_freq_tab (struct variable *);

static hsh_hash_func hash_value_numeric, hash_value_alpha;
static hsh_compare_func compare_value_numeric_a, compare_value_alpha_a;
static hsh_compare_func compare_value_numeric_d, compare_value_alpha_d;
static hsh_compare_func compare_freq_numeric_a, compare_freq_alpha_a;
static hsh_compare_func compare_freq_numeric_d, compare_freq_alpha_d;

/* Parser and outline. */

static int internal_cmd_frequencies (void);

int
cmd_frequencies (void)
{
  int result;

  int_pool = pool_create ();
  result = internal_cmd_frequencies ();
  pool_destroy (int_pool);
  int_pool=0;
  pool_destroy (gen_pool);
  gen_pool=0;
  free (v_variables);
  v_variables=0;
  return result;
}

static int
internal_cmd_frequencies (void)
{
  int i;

  n_percentiles = 0;
  percentile_values = NULL;
  percentiles = NULL;

  n_variables = 0;
  v_variables = NULL;

  for (i = 0; i < dict_get_var_cnt (default_dict); i++)
    dict_get_var(default_dict, i)->p.frq.used = 0;

  lex_match_id ("FREQUENCIES");
  if (!parse_frequencies (&cmd))
    return CMD_FAILURE;

  if (cmd.onepage_limit == NOT_LONG)
    cmd.onepage_limit = 50;

  /* Figure out statistics to calculate. */
  stats = 0;
  if (stat[FRQ_ST_DEFAULT] || !cmd.sbc_statistics)
    stats |= frq_default;
  if (stat[FRQ_ST_ALL])
    stats |= frq_all;
  if (cmd.sort != FRQ_AVALUE && cmd.sort != FRQ_DVALUE)
    stats &= ~frq_median;
  for (i = 0; i < frq_n_stats; i++)
    if (stat[st_name[i].st_indx])
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

  /* Do it! */
  procedure_with_splits (precalc, calc, postcalc, NULL);

  return CMD_SUCCESS;
}

/* Figure out which charts the user requested.  */
static void
determine_charts (void)
{
  int count = (!!cmd.sbc_histogram) + (!!cmd.sbc_barchart) + (!!cmd.sbc_hbar);

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
      if (cmd.hi_norm)
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
static int
calc (struct ccase *c, void *aux UNUSED)
{
  double weight;
  int i;

  weight = dict_get_case_weight (default_dict, c);

  for (i = 0; i < n_variables; i++)
    {
      struct variable *v = v_variables[i];
      union value *val = &c->data[v->fv];
      struct freq_tab *ft = &v->p.frq.tab;

      switch (v->p.frq.tab.mode)
	{
	  case FRQM_GENERAL:
	    {
	      /* General mode. */
	      struct freq **fpp = (struct freq **) hsh_probe (ft->data, val);
	  
	      if (*fpp != NULL)
		(*fpp)->c += weight;
	      else
		{
		  struct freq *fp = *fpp = pool_alloc (gen_pool, sizeof *fp);
		  fp->v = *val;
		  fp->c = weight;
		}
	    }
	  break;
	case FRQM_INTEGER:
	  /* Integer mode. */
	  if (val->f == SYSMIS)
	    v->p.frq.tab.sysmis += weight;
	  else if (val->f > INT_MIN+1 && val->f < INT_MAX-1)
	    {
	      int i = val->f;
	      if (i >= v->p.frq.tab.min && i <= v->p.frq.tab.max)
		v->p.frq.tab.vector[i - v->p.frq.tab.min] += weight;
	    }
	  else
	    v->p.frq.tab.out_of_range += weight;
	  break;
	default:
	  assert (0);
	}
    }
  return 1;
}

/* Prepares each variable that is the target of FREQUENCIES by setting
   up its hash table. */
static void
precalc (void *aux UNUSED)
{
  int i;

  pool_destroy (gen_pool);
  gen_pool = pool_create ();
  
  for (i = 0; i < n_variables; i++)
    {
      struct variable *v = v_variables[i];

      if (v->p.frq.tab.mode == FRQM_GENERAL)
	{
          hsh_hash_func *hash;
	  hsh_compare_func *compare;

	  if (v->type == NUMERIC) 
            {
              hash = hash_value_numeric;
              compare = compare_value_numeric_a; 
            }
	  else 
            {
              hash = hash_value_alpha;
              compare = compare_value_alpha_a;
            }
	  v->p.frq.tab.data = hsh_create (16, compare, hash, NULL, v);
	}
      else
	{
	  int j;

	  for (j = (v->p.frq.tab.max - v->p.frq.tab.min); j >= 0; j--)
	    v->p.frq.tab.vector[j] = 0.0;
	  v->p.frq.tab.out_of_range = 0.0;
	  v->p.frq.tab.sysmis = 0.0;
	}
    }
}

/* Finishes up with the variables after frequencies have been
   calculated.  Displays statistics, percentiles, ... */
static void
postcalc (void *aux UNUSED)
{
  int i;

  for (i = 0; i < n_variables; i++)
    {
      struct variable *v = v_variables[i];
      int n_categories;
      int dumped_freq_tab = 1;

      postprocess_freq_tab (v);

      /* Frequencies tables. */
      n_categories = v->p.frq.tab.n_valid + v->p.frq.tab.n_missing;
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
	    assert (0);
	  }
      else
	dumped_freq_tab = 0;

      /* Statistics. */
      if (n_stats)
	dump_statistics (v, !dumped_freq_tab);

      cleanup_freq_tab (v);
    }
}

/* Returns the comparison function that should be used for
   sorting a frequency table by FRQ_SORT using VAR_TYPE
   variables. */
static hsh_compare_func *
get_freq_comparator (int frq_sort, int var_type) 
{
  /* Note that q2c generates tags beginning with 1000. */
  switch (frq_sort | (var_type << 16))
    {
    case FRQ_AVALUE | (NUMERIC << 16):  return compare_value_numeric_a;
    case FRQ_AVALUE | (ALPHA << 16):    return compare_value_alpha_a;
    case FRQ_DVALUE | (NUMERIC << 16):  return compare_value_numeric_d;
    case FRQ_DVALUE | (ALPHA << 16):    return compare_value_alpha_d;
    case FRQ_AFREQ | (NUMERIC << 16):   return compare_freq_numeric_a;
    case FRQ_AFREQ | (ALPHA << 16):     return compare_freq_alpha_a;
    case FRQ_DFREQ | (NUMERIC << 16):   return compare_freq_numeric_d;
    case FRQ_DFREQ | (ALPHA << 16):     return compare_freq_alpha_d;
    default: assert (0);
    }

  return 0;
}

/* Returns nonzero iff the value in struct freq F is non-missing
   for variable V. */
static int
not_missing (const void *f_, void *v_) 
{
  const struct freq *f = f_;
  struct variable *v = v_;

  return !is_missing (&f->v, v);
}

/* Summarizes the frequency table data for variable V. */
static void
postprocess_freq_tab (struct variable *v)
{
  hsh_compare_func *compare;
  struct freq_tab *ft;
  size_t count;
  void **data;
  struct freq *freqs, *f;
  size_t i;

  assert (v->p.frq.tab.mode == FRQM_GENERAL);
  compare = get_freq_comparator (cmd.sort, v->type);
  ft = &v->p.frq.tab;

  /* Extract data from hash table. */
  count = hsh_count (ft->data);
  data = hsh_data (ft->data);

  /* Copy dereferenced data into freqs. */
  freqs = xmalloc (count* sizeof *freqs);
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
  ft->total_cases = ft->valid_cases = 0.0;
  for (f = ft->valid; f < ft->valid + ft->n_valid; f++) 
    {
      ft->total_cases += f->c;

      if ((v->type != NUMERIC || f->v.f != SYSMIS)
          && (cmd.miss != FRQ_EXCLUDE || !is_user_missing (&f->v, v)))
        ft->valid_cases += f->c;
    }
}

/* Frees the frequency table for variable V. */
static void
cleanup_freq_tab (struct variable *v)
{
  assert (v->p.frq.tab.mode == FRQM_GENERAL);
  free (v->p.frq.tab.valid);
  hsh_destroy (v->p.frq.tab.data);
}

/* Parses the VARIABLES subcommand, adding to
   {n_variables,v_variables}. */
static int
frq_custom_variables (struct cmd_frequencies *cmd UNUSED)
{
  int mode;
  int min = 0, max = 0;

  int old_n_variables = n_variables;
  int i;

  lex_match ('=');
  if (token != T_ALL && (token != T_ID
                         || dict_lookup_var (default_dict, tokid) == NULL))
    return 2;

  if (!parse_variables (default_dict, &v_variables, &n_variables,
			PV_APPEND | PV_NO_SCRATCH))
    return 0;

  for (i = old_n_variables; i < n_variables; i++)
    v_variables[i]->p.frq.tab.mode = FRQM_GENERAL;

  if (!lex_match ('('))
    mode = FRQM_GENERAL;
  else
    {
      mode = FRQM_INTEGER;
      if (!lex_force_int ())
	return 0;
      min = lex_integer ();
      lex_get ();
      if (!lex_force_match (','))
	return 0;
      if (!lex_force_int ())
	return 0;
      max = lex_integer ();
      lex_get ();
      if (!lex_force_match (')'))
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
      struct variable *v = v_variables[i];

      if (v->p.frq.used != 0)
	{
	  msg (SE, _("Variable %s specified multiple times on VARIABLES "
		     "subcommand."), v->name);
	  return 0;
	}
      
      v->p.frq.used = 1;		/* Used simply as a marker. */

      v->p.frq.tab.valid = v->p.frq.tab.missing = NULL;

      if (mode == FRQM_INTEGER)
	{
	  if (v->type != NUMERIC)
	    {
	      msg (SE, _("Integer mode specified, but %s is not a numeric "
			 "variable."), v->name);
	      return 0;
	    }
	  
	  v->p.frq.tab.min = min;
	  v->p.frq.tab.max = max;
	  v->p.frq.tab.vector = pool_alloc (int_pool,
					    sizeof (struct freq) * (max - min + 1));
	}
      else
	v->p.frq.tab.vector = NULL;

      v->p.frq.n_groups = 0;
      v->p.frq.groups = NULL;
    }
  return 1;
}

/* Parses the GROUPED subcommand, setting the frq.{n_grouped,grouped}
   fields of specified variables. */
static int
frq_custom_grouped (struct cmd_frequencies *cmd UNUSED)
{
  lex_match ('=');
  if ((token == T_ID && dict_lookup_var (default_dict, tokid) != NULL)
      || token == T_ID)
    for (;;)
      {
	int i;

	/* Max, current size of list; list itself. */
	int nl, ml;
	double *dl;

	/* Variable list. */
	int n;
	struct variable **v;

	if (!parse_variables (default_dict, &v, &n,
                              PV_NO_DUPLICATE | PV_NUMERIC))
	  return 0;
	if (lex_match ('('))
	  {
	    nl = ml = 0;
	    dl = NULL;
	    while (token == T_NUM)
	      {
		if (nl >= ml)
		  {
		    ml += 16;
		    dl = pool_realloc (int_pool, dl, ml * sizeof (double));
		  }
		dl[nl++] = tokval;
		lex_get ();
		lex_match (',');
	      }
	    /* Note that nl might still be 0 and dl might still be
	       NULL.  That's okay. */
	    if (!lex_match (')'))
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
	  {
	    if (v[i]->p.frq.used == 0)
	      msg (SE, _("Variables %s specified on GROUPED but not on "
		   "VARIABLES."), v[i]->name);
	    if (v[i]->p.frq.groups != NULL)
	      msg (SE, _("Variables %s specified multiple times on GROUPED "
		   "subcommand."), v[i]->name);
	    else
	      {
		v[i]->p.frq.n_groups = nl;
		v[i]->p.frq.groups = dl;
	      }
	  }
	free (v);
	if (!lex_match ('/'))
	  break;
	if ((token != T_ID || dict_lookup_var (default_dict, tokid) != NULL)
            && token != T_ALL)
	  {
	    lex_put_back ('/');
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
    if (x <= percentiles[i])
      break;
  if (i >= n_percentiles || tokval != percentiles[i])
    {
      percentiles
        = pool_realloc (int_pool, percentiles,
                        (n_percentiles + 1) * sizeof *percentiles);
      percentile_values
        = pool_realloc (int_pool, percentile_values,
                        (n_percentiles + 1) * sizeof *percentile_values);
      if (i < n_percentiles) 
        {
          memmove (&percentiles[i + 1], &percentiles[i],
                   (n_percentiles - i) * sizeof *percentiles);
          memmove (&percentile_values[i + 1], &percentile_values[i],
                   (n_percentiles - i) * sizeof *percentile_values);
        }
      percentiles[i] = x;
      n_percentiles++;
    }
}

/* Parses the PERCENTILES subcommand, adding user-specified
   percentiles to the list. */
static int
frq_custom_percentiles (struct cmd_frequencies *cmd UNUSED)
{
  lex_match ('=');
  if (token != T_NUM)
    {
      msg (SE, _("Percentile list expected after PERCENTILES."));
      return 0;
    }
  
  do
    {
      if (tokval <= 0 || tokval >= 100)
	{
	  msg (SE, _("Percentiles must be greater than "
		     "0 and less than 100."));
	  return 0;
	}
      
      add_percentile (tokval / 100.0);
      lex_get ();
      lex_match (',');
    }
  while (token == T_NUM);
  return 1;
}

/* Parses the NTILES subcommand, adding the percentiles that
   correspond to the specified evenly-distributed ntiles. */
static int
frq_custom_ntiles (struct cmd_frequencies *cmd UNUSED)
{
  int i;

  lex_match ('=');
  if (!lex_force_int ())
    return 0;
  for (i = 1; i < lex_integer (); i++)
    add_percentile (1.0 / lex_integer () * i);
  lex_get ();
  return 1;
}

/* Comparison functions. */

/* Hash of numeric values. */
static unsigned
hash_value_numeric (const void *value_, void *foo UNUSED)
{
  const struct freq *value = value_;
  return hsh_hash_double (value->v.f);
}

/* Hash of string values. */
static unsigned
hash_value_alpha (const void *value_, void *v_)
{
  const struct freq *value = value_;
  struct variable *v = v_;

  return hsh_hash_bytes (value->v.s, v->width);
}

/* Ascending numeric compare of values. */
static int
compare_value_numeric_a (const void *a_, const void *b_, void *foo UNUSED)
{
  const struct freq *a = a_;
  const struct freq *b = b_;

  if (a->v.f > b->v.f)
    return 1;
  else if (a->v.f < b->v.f)
    return -1;
  else
    return 0;
}

/* Ascending string compare of values. */
static int
compare_value_alpha_a (const void *a_, const void *b_, void *v_)
{
  const struct freq *a = a_;
  const struct freq *b = b_;
  const struct variable *v = v_;

  return memcmp (a->v.s, b->v.s, v->width);
}

/* Descending numeric compare of values. */
static int
compare_value_numeric_d (const void *a, const void *b, void *foo UNUSED)
{
  return -compare_value_numeric_a (a, b, foo);
}

/* Descending string compare of values. */
static int
compare_value_alpha_d (const void *a, const void *b, void *v)
{
  return -compare_value_alpha_a (a, b, v);
}

/* Ascending numeric compare of frequency;
   secondary key on ascending numeric value. */
static int
compare_freq_numeric_a (const void *a_, const void *b_, void *foo UNUSED)
{
  const struct freq *a = a_;
  const struct freq *b = b_;

  if (a->c > b->c)
    return 1;
  else if (a->c < b->c)
    return -1;

  if (a->v.f > b->v.f)
    return 1;
  else if (a->v.f < b->v.f)
    return -1;
  else
    return 0;
}

/* Ascending numeric compare of frequency;
   secondary key on ascending string value. */
static int
compare_freq_alpha_a (const void *a_, const void *b_, void *v_)
{
  const struct freq *a = a_;
  const struct freq *b = b_;
  const struct variable *v = v_;

  if (a->c > b->c)
    return 1;
  else if (a->c < b->c)
    return -1;
  else
    return memcmp (a->v.s, b->v.s, v->width);
}

/* Descending numeric compare of frequency;
   secondary key on ascending numeric value. */
static int
compare_freq_numeric_d (const void *a_, const void *b_, void *foo UNUSED)
{
  const struct freq *a = a_;
  const struct freq *b = b_;

  if (a->c > b->c)
    return -1;
  else if (a->c < b->c)
    return 1;

  if (a->v.f > b->v.f)
    return 1;
  else if (a->v.f < b->v.f)
    return -1;
  else
    return 0;
}

/* Descending numeric compare of frequency;
   secondary key on ascending string value. */
static int
compare_freq_alpha_d (const void *a_, const void *b_, void *v_)
{
  const struct freq *a = a_;
  const struct freq *b = b_;
  const struct variable *v = v_;

  if (a->c > b->c)
    return -1;
  else if (a->c < b->c)
    return 1;
  else
    return memcmp (a->v.s, b->v.s, v->width);
}

/* Frequency table display. */

/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
full_dim (struct tab_table *t, struct outp_driver *d)
{
  int lab = cmd.labels == FRQ_LABELS;
  int i;

  if (lab)
    t->w[0] = min (tab_natural_width (t, d, 0), d->prop_em_width * 15);
  for (i = lab; i < lab + 5; i++)
    t->w[i] = max (tab_natural_width (t, d, i), d->prop_em_width * 8);
  for (i = 0; i < t->nr; i++)
    t->h[i] = d->font_height;
}

/* Displays a full frequency table for variable V. */
static void
dump_full (struct variable * v)
{
  int n_categories;
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

  struct init *p;

  static struct init vec[] =
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

  int lab = cmd.labels == FRQ_LABELS;

  n_categories = v->p.frq.tab.n_valid + v->p.frq.tab.n_missing;
  t = tab_create (5 + lab, n_categories + 3, 0);
  tab_headers (t, 0, 0, 2, 0);
  tab_dim (t, full_dim);

  if (lab)
    tab_text (t, 0, 1, TAB_CENTER | TAT_TITLE, _("Value Label"));
  for (p = vec; p->s; p++)
    tab_text (t, p->c - (p->r ? !lab : 0), p->r,
		  TAB_CENTER | TAT_TITLE, gettext (p->s));

  r = 2;
  for (f = v->p.frq.tab.valid; f < v->p.frq.tab.missing; f++)
    {
      double percent, valid_percent;

      cum_freq += f->c;

      percent = f->c / v->p.frq.tab.total_cases * 100.0;
      valid_percent = f->c / v->p.frq.tab.valid_cases * 100.0;
      cum_total += valid_percent;

      if (lab)
	{
	  const char *label = val_labs_find (v->val_labs, f->v);
	  if (label != NULL)
	    tab_text (t, 0, r, TAB_LEFT, label);
	}

      tab_value (t, 0 + lab, r, TAB_NONE, &f->v, &v->print);
      tab_float (t, 1 + lab, r, TAB_NONE, f->c, 8, 0);
      tab_float (t, 2 + lab, r, TAB_NONE, percent, 5, 1);
      tab_float (t, 3 + lab, r, TAB_NONE, valid_percent, 5, 1);
      tab_float (t, 4 + lab, r, TAB_NONE, cum_total, 5, 1);
      r++;
    }
  for (; f < &v->p.frq.tab.valid[n_categories]; f++)
    {
      cum_freq += f->c;

      if (lab)
	{
	  const char *label = val_labs_find (v->val_labs, f->v);
	  if (label != NULL)
	    tab_text (t, 0, r, TAB_LEFT, label);
	}

      tab_value (t, 0 + lab, r, TAB_NONE, &f->v, &v->print);
      tab_float (t, 1 + lab, r, TAB_NONE, f->c, 8, 0);
      tab_float (t, 2 + lab, r, TAB_NONE,
		     f->c / v->p.frq.tab.total_cases * 100.0, 5, 1);
      tab_text (t, 3 + lab, r, TAB_NONE, _("Missing"));
      r++;
    }

  tab_box (t, TAL_1, TAL_1,
	   cmd.spaces == FRQ_SINGLE ? -1 : (TAL_1 | TAL_SPACING), TAL_1,
	   0, 0, 4 + lab, r);
  tab_hline (t, TAL_2, 0, 4 + lab, 2);
  tab_hline (t, TAL_2, 0, 4 + lab, r);
  tab_joint_text (t, 0, r, 0 + lab, r, TAB_RIGHT | TAT_TITLE, _("Total"));
  tab_vline (t, TAL_0, 1, r, r);
  tab_float (t, 1 + lab, r, TAB_NONE, cum_freq, 8, 0);
  tab_float (t, 2 + lab, r, TAB_NONE, 100.0, 5, 1);
  tab_float (t, 3 + lab, r, TAB_NONE, 100.0, 5, 1);

  tab_title (t, 1, "%s: %s", v->name, v->label ? v->label : "");
  tab_submit (t);
}

/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
condensed_dim (struct tab_table *t, struct outp_driver *d)
{
  int cum_w = max (outp_string_width (d, _("Cum")),
		   max (outp_string_width (d, _("Cum")),
			outp_string_width (d, "000")));

  int i;

  for (i = 0; i < 2; i++)
    t->w[i] = max (tab_natural_width (t, d, i), d->prop_em_width * 8);
  for (i = 2; i < 4; i++)
    t->w[i] = cum_w;
  for (i = 0; i < t->nr; i++)
    t->h[i] = d->font_height;
}

/* Display condensed frequency table for variable V. */
static void
dump_condensed (struct variable * v)
{
  int n_categories;
  struct freq *f;
  struct tab_table *t;
  int r;
  double cum_total = 0.0;

  n_categories = v->p.frq.tab.n_valid + v->p.frq.tab.n_missing;
  t = tab_create (4, n_categories + 2, 0);

  tab_headers (t, 0, 0, 2, 0);
  tab_text (t, 0, 1, TAB_CENTER | TAT_TITLE, _("Value"));
  tab_text (t, 1, 1, TAB_CENTER | TAT_TITLE, _("Freq"));
  tab_text (t, 2, 1, TAB_CENTER | TAT_TITLE, _("Pct"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Cum"));
  tab_text (t, 3, 1, TAB_CENTER | TAT_TITLE, _("Pct"));
  tab_dim (t, condensed_dim);

  r = 2;
  for (f = v->p.frq.tab.valid; f < v->p.frq.tab.missing; f++)
    {
      double percent;

      percent = f->c / v->p.frq.tab.total_cases * 100.0;
      cum_total += f->c / v->p.frq.tab.valid_cases * 100.0;

      tab_value (t, 0, r, TAB_NONE, &f->v, &v->print);
      tab_float (t, 1, r, TAB_NONE, f->c, 8, 0);
      tab_float (t, 2, r, TAB_NONE, percent, 3, 0);
      tab_float (t, 3, r, TAB_NONE, cum_total, 3, 0);
      r++;
    }
  for (; f < &v->p.frq.tab.valid[n_categories]; f++)
    {
      tab_value (t, 0, r, TAB_NONE, &f->v, &v->print);
      tab_float (t, 1, r, TAB_NONE, f->c, 8, 0);
      tab_float (t, 2, r, TAB_NONE,
		 f->c / v->p.frq.tab.total_cases * 100.0, 3, 0);
      r++;
    }

  tab_box (t, TAL_1, TAL_1,
	   cmd.spaces == FRQ_SINGLE ? -1 : (TAL_1 | TAL_SPACING), TAL_1,
	   0, 0, 3, r - 1);
  tab_hline (t, TAL_2, 0, 3, 2);
  tab_title (t, 1, "%s: %s", v->name, v->label ? v->label : "");
  tab_columns (t, SOM_COL_DOWN, 1);
  tab_submit (t);
}

/* Statistical display. */

/* Calculates all the pertinent statistics for variable V, putting
   them in array D[].  FIXME: This could be made much more optimal. */
static void
calc_stats (struct variable * v, double d[frq_n_stats])
{
  double W = v->p.frq.tab.valid_cases;
  double X_bar, X_mode, M2, M3, M4;
  struct freq *f;
  int most_often;

  double cum_total;
  int i = 0;
  double previous_value;

  /* Calculate the mean. */
  X_bar = 0.0;
  for (f = v->p.frq.tab.valid; f < v->p.frq.tab.missing; f++)
    X_bar += f->v.f * f->c;
  X_bar /= W;

  /* Calculate percentiles. */
  cum_total = 0;
  previous_value = SYSMIS;
  for (f = v->p.frq.tab.valid; f < v->p.frq.tab.missing; f++)
    {
      cum_total += f->c ;
      for (; i < n_percentiles; i++) 
        {
          if (cum_total / v->p.frq.tab.valid_cases < percentiles[i])
            break;

          percentile_values[i] = previous_value;
        }
      previous_value = f->v.f;
    }

  /* Calculate the mode. */
  most_often = -1;
  X_mode = SYSMIS;
  for (f = v->p.frq.tab.valid; f < v->p.frq.tab.missing; f++)
    {
      if (most_often < f->c) 
        {
          most_often = f->c;
          X_mode = f->v.f;
        }
      else if (most_often == f->c) 
        {
          /* A duplicate mode is undefined.
             FIXME: keep track of *all* the modes. */
          X_mode = SYSMIS;
        }
    }

  /* Calculate moments about the mean. */
  M2 = M3 = M4 = 0.0;
  for (f = v->p.frq.tab.valid; f < v->p.frq.tab.missing; f++)
    {
      double dev = f->v.f - X_bar;
      double tmp;
      tmp = dev * dev;
      M2 += f->c * tmp;
      tmp *= dev;
      M3 += f->c * tmp;
      tmp *= dev;
      M4 += f->c * tmp;
    }

  /* Formulas below are taken from _SPSS Statistical Algorithms_. */
  d[frq_min] = v->p.frq.tab.valid[0].v.f;
  d[frq_max] = v->p.frq.tab.valid[v->p.frq.tab.n_valid - 1].v.f;
  d[frq_mode] = X_mode;
  d[frq_range] = d[frq_max] - d[frq_min];
  d[frq_median] = SYSMIS;
  d[frq_mean] = X_bar;
  d[frq_sum] = X_bar * W;
  d[frq_variance] = M2 / (W - 1);
  d[frq_stddev] = sqrt (d[frq_variance]);
  d[frq_semean] = d[frq_stddev] / sqrt (W);
  if (W >= 3.0 && d[frq_variance] > 0)
    {
      double S = d[frq_stddev];
      d[frq_skew] = (W * M3 / ((W - 1.0) * (W - 2.0) * S * S * S));
      d[frq_seskew] = sqrt (6.0 * W * (W - 1.0)
			    / ((W - 2.0) * (W + 1.0) * (W + 3.0)));
    }
  else
    {
      d[frq_skew] = d[frq_seskew] = SYSMIS;
    }
  if (W >= 4.0 && d[frq_variance] > 0)
    {
      double S2 = d[frq_variance];
      double SE_g1 = d[frq_seskew];

      d[frq_kurt] = ((W * (W + 1.0) * M4 - 3.0 * M2 * M2 * (W - 1.0))
		     / ((W - 1.0) * (W - 2.0) * (W - 3.0) * S2 * S2));
      d[frq_sekurt] = sqrt ((4.0 * (W * W - 1.0) * SE_g1 * SE_g1)
			    / ((W - 3.0) * (W + 5.0)));
    }
  else
    {
      d[frq_kurt] = d[frq_sekurt] = SYSMIS;
    }
}

/* Displays a table of all the statistics requested for variable V. */
static void
dump_statistics (struct variable * v, int show_varname)
{
  double stat_value[frq_n_stats];
  struct tab_table *t;
  int i, r;

  if (v->type == ALPHA)
    return;
  if (v->p.frq.tab.n_valid == 0)
    {
      msg (SW, _("No valid data for variable %s; statistics not displayed."),
	   v->name);
      return;
    }
  calc_stats (v, stat_value);

  t = tab_create (2, n_stats + n_percentiles, 0);
  tab_dim (t, tab_natural_dimensions);
  tab_vline (t, TAL_1 | TAL_SPACING, 1, 0, n_stats - 1);
  for (i = r = 0; i < frq_n_stats; i++)
    if (stats & BIT_INDEX (i))
      {
	tab_text (t, 0, r, TAB_LEFT | TAT_TITLE,
		      gettext (st_name[i].s10));
	tab_float (t, 1, r, TAB_NONE, stat_value[i], 11, 3);
	r++;
      }

  for (i = 0; i < n_percentiles; i++, r++) 
    {
      struct string ds;

      ds_init (gen_pool, &ds, 20);
      ds_printf (&ds, "%s %d", _("Percentile"), (int) (percentiles[i] * 100));

      tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, ds.string);
      tab_float (t, 1, r, TAB_NONE, percentile_values[i], 11, 3);

      ds_destroy (&ds);
    }

  tab_columns (t, SOM_COL_DOWN, 1);
  if (show_varname)
    {
      if (v->label)
	tab_title (t, 1, "%s: %s", v->name, v->label);
      else
	tab_title (t, 0, v->name);
    }
  else
    tab_flags (t, SOMF_NO_TITLE);
  
  tab_submit (t);
}
/* 
   Local Variables:
   mode: c
   End:
*/
