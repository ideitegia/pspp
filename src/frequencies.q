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
#include "avl.h"
#include "bitvector.h"
#include "hash.h"
#include "pool.h"
#include "command.h"
#include "lexer.h"
#include "error.h"
#include "approx.h"
#include "magic.h"
#include "misc.h"
#include "stats.h"
#include "output.h"
#include "som.h"
#include "tab.h"
#include "var.h"
#include "vfm.h"

#undef DEBUGGING
/*#define DEBUGGING 1 */
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

static void precalc (void);
static int calc_weighting (struct ccase *);
static int calc_no_weight (struct ccase *);
static void postcalc (void);

static void postprocess_freq_tab (struct variable *);
static void dump_full (struct variable *);
static void dump_condensed (struct variable *);
static void dump_statistics (struct variable *, int show_varname);
static void cleanup_freq_tab (struct variable *);

static int compare_value_numeric_a (const void *, const void *, void *);
static int compare_value_alpha_a (const void *, const void *, void *);
static int compare_value_numeric_d (const void *, const void *, void *);
static int compare_value_alpha_d (const void *, const void *, void *);
static int compare_freq_numeric_a (const void *, const void *, void *);
static int compare_freq_alpha_a (const void *, const void *, void *);
static int compare_freq_numeric_d (const void *, const void *, void *);
static int compare_freq_alpha_d (const void *, const void *, void *);

/* Parser and outline. */

static int internal_cmd_frequencies (void);

int
cmd_frequencies (void)
{
  int result;

  int_pool = pool_create ();
  result = internal_cmd_frequencies ();
  pool_destroy (int_pool);
  pool_destroy (gen_pool);
  free (v_variables);
  return result;
}

static int
internal_cmd_frequencies (void)
{
  int (*calc) (struct ccase *);
  int i;

  n_percentiles = 0;
  percentiles = NULL;

  n_variables = 0;
  v_variables = NULL;

  for (i = 0; i < default_dict.nvar; i++)
    default_dict.var[i]->foo = 0;

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
  update_weighting (&default_dict);
  calc = default_dict.weight_index == -1 ? calc_no_weight : calc_weighting;
  procedure (precalc, calc, postcalc);

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

/* Generate each calc_*(). */
#define WEIGHTING 0
#include "frequencies.g"

#define WEIGHTING 1
#include "frequencies.g"

/* Prepares each variable that is the target of FREQUENCIES by setting
   up its hash table. */
static void
precalc (void)
{
  int i;

  pool_destroy (gen_pool);
  gen_pool = pool_create ();
  
  for (i = 0; i < n_variables; i++)
    {
      struct variable *v = v_variables[i];

      if (v->p.frq.tab.mode == FRQM_GENERAL)
	{
	  avl_comparison_func compare;
	  if (v->type == NUMERIC)
	    compare = compare_value_numeric_a;
	  else
	    compare = compare_value_alpha_a;
	  v->p.frq.tab.tree = avl_create (gen_pool, compare,
					  (void *) v->width);
	  v->p.frq.tab.n_missing = 0;
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
postcalc (void)
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

/* Comparison function called by comparison_helper(). */
static avl_comparison_func comparison_func;

/* Passed to comparison function by comparison_helper(). */
static void *comparison_param;

/* Used by postprocess_freq_tab to re-sort frequency tables. */
static int
comparison_helper (const void *a, const void *b)
{
  return comparison_func (&((struct freq *) a)->v,
			  &((struct freq *) b)->v, comparison_param);
}

/* Used by postprocess_freq_tab to construct the array members valid,
   missing of freq_tab. */
static void
add_freq (void *data, void *param)
{
  struct freq *f = data;
  struct variable *v = param;

  v->p.frq.tab.total_cases += f->c;

  if ((v->type == NUMERIC && f->v.f == SYSMIS)
      || (cmd.miss == FRQ_EXCLUDE && is_user_missing (&f->v, v)))
    {
      *v->p.frq.tab.missing++ = *f;
      v->p.frq.tab.valid_cases -= f->c;
    }
  else
    *v->p.frq.tab.valid++ = *f;
}

static void
postprocess_freq_tab (struct variable * v)
{
  avl_comparison_func compare;

  switch (cmd.sort | (v->type << 16))
    {
      /* Note that q2c generates tags beginning with 1000. */
    case FRQ_AVALUE | (NUMERIC << 16):
      compare = NULL;
      break;
    case FRQ_AVALUE | (ALPHA << 16):
      compare = NULL;
      break;
    case FRQ_DVALUE | (NUMERIC << 16):
      comparison_func = compare_value_numeric_d;
      break;
    case FRQ_DVALUE | (ALPHA << 16):
      compare = compare_value_alpha_d;
      break;
    case FRQ_AFREQ | (NUMERIC << 16):
      compare = compare_freq_numeric_a;
      break;
    case FRQ_AFREQ | (ALPHA << 16):
      compare = compare_freq_alpha_a;
      break;
    case FRQ_DFREQ | (NUMERIC << 16):
      compare = compare_freq_numeric_d;
      break;
    case FRQ_DFREQ | (ALPHA << 16):
      compare = compare_freq_alpha_d;
      break;
    default:
      assert (0);
    }
  comparison_func = compare;

  if (v->p.frq.tab.mode == FRQM_GENERAL)
    {
      int total;
      struct freq_tab *ft = &v->p.frq.tab;

      total = avl_count (ft->tree);
      ft->n_valid = total - ft->n_missing;
      ft->valid = xmalloc (sizeof (struct freq) * total);
      ft->missing = &ft->valid[ft->n_valid];
      ft->valid_cases = ft->total_cases = 0.0;

      avl_walk (ft->tree, add_freq, (void *) v);

      ft->valid -= ft->n_valid;
      ft->missing -= ft->n_missing;
      ft->valid_cases += ft->total_cases;

      if (compare)
	{
	  qsort (ft->valid, ft->n_valid, sizeof (struct freq), comparison_helper);
	  qsort (ft->missing, ft->n_missing, sizeof (struct freq), comparison_helper);
	}
    }
  else
    assert (0);
}

static void
cleanup_freq_tab (struct variable * v)
{
  if (v->p.frq.tab.mode == FRQM_GENERAL)
    {
      struct freq_tab *ft = &v->p.frq.tab;

      free (ft->valid);
    }
  else
    assert (0);
}

/* Parses the VARIABLES subcommand, adding to
   {n_variables,v_variables}. */
static int
frq_custom_variables (struct cmd_frequencies *cmd unused)
{
  int mode;
  int min, max;

  int old_n_variables = n_variables;
  int i;

  lex_match ('=');
  if (token != T_ALL && (token != T_ID || !is_varname (tokid)))
    return 2;

  if (!parse_variables (NULL, &v_variables, &n_variables,
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

      if (v->foo != 0)
	{
	  msg (SE, _("Variable %s specified multiple times on VARIABLES "
		     "subcommand."), v->name);
	  return 0;
	}
      
      v->foo = 1;		/* Used simply as a marker. */

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
frq_custom_grouped (struct cmd_frequencies *cmd unused)
{
  lex_match ('=');
  if ((token == T_ID && is_varname (tokid)) || token == T_ID)
    for (;;)
      {
	int i;

	/* Max, current size of list; list itself. */
	int nl, ml;
	double *dl;

	/* Variable list. */
	int n;
	struct variable **v;

	if (!parse_variables (NULL, &v, &n, PV_NO_DUPLICATE | PV_NUMERIC))
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
	  nl = 0;

	for (i = 0; i < n; i++)
	  {
	    if (v[i]->foo == 0)
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
	if ((token != T_ID || !is_varname (tokid)) && token != T_ALL)
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
      percentiles = pool_realloc (int_pool, percentiles,
				  (n_percentiles + 1) * sizeof (double));
      if (i < n_percentiles)
	memmove (&percentiles[i + 1], &percentiles[i],
		 (n_percentiles - i) * sizeof (double));
      percentiles[i] = x;
      n_percentiles++;
    }
}

/* Parses the PERCENTILES subcommand, adding user-specified
   percentiles to the list. */
static int
frq_custom_percentiles (struct cmd_frequencies *cmd unused)
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
frq_custom_ntiles (struct cmd_frequencies *cmd unused)
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

/* Ascending numeric compare of values. */
static int
compare_value_numeric_a (const void *a, const void *b, void *foo unused)
{
  return approx_compare (((struct freq *) a)->v.f, ((struct freq *) b)->v.f);
}

/* Ascending string compare of values. */
static int
compare_value_alpha_a (const void *a, const void *b, void *len)
{
  return memcmp (((struct freq *) a)->v.s, ((struct freq *) b)->v.s, (int) len);
}

/* Descending numeric compare of values. */
static int
compare_value_numeric_d (const void *a, const void *b, void *foo unused)
{
  return approx_compare (((struct freq *) b)->v.f, ((struct freq *) a)->v.f);
}

/* Descending string compare of values. */
static int
compare_value_alpha_d (const void *a, const void *b, void *len)
{
  return memcmp (((struct freq *) b)->v.s, ((struct freq *) a)->v.s, (int) len);
}

/* Ascending numeric compare of frequency;
   secondary key on ascending numeric value. */
static int
compare_freq_numeric_a (const void *a, const void *b, void *foo unused)
{
  int x = approx_compare (((struct freq *) a)->c, ((struct freq *) b)->c);
  return x ? x : approx_compare (((struct freq *) a)->v.f, ((struct freq *) b)->v.f);
}

/* Ascending numeric compare of frequency;
   secondary key on ascending string value. */
static int
compare_freq_alpha_a (const void *a, const void *b, void *len)
{
  int x = approx_compare (((struct freq *) a)->c, ((struct freq *) b)->c);
  return x ? x : memcmp (((struct freq *) a)->v.s, ((struct freq *) b)->v.s, (int) len);
}

/* Descending numeric compare of frequency;
   secondary key on ascending numeric value. */
static int
compare_freq_numeric_d (const void *a, const void *b, void *foo unused)
{
  int x = approx_compare (((struct freq *) b)->c, ((struct freq *) a)->c);
  return x ? x : approx_compare (((struct freq *) a)->v.f, ((struct freq *) b)->v.f);
}

/* Descending numeric compare of frequency;
   secondary key on ascending string value. */
static int
compare_freq_alpha_d (const void *a, const void *b, void *len)
{
  int x = approx_compare (((struct freq *) b)->c, ((struct freq *) a)->c);
  return x ? x : memcmp (((struct freq *) a)->v.s, ((struct freq *) b)->v.s, (int) len);
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
  double cum_percent = 0.0;
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
      cum_percent += valid_percent;

      if (lab)
	{
	  char *label = get_val_lab (v, f->v, 0);
	  if (label != NULL)
	    tab_text (t, 0, r, TAB_LEFT, label);
	}

      tab_value (t, 0 + lab, r, TAB_NONE, &f->v, &v->print);
      tab_float (t, 1 + lab, r, TAB_NONE, f->c, 8, 0);
      tab_float (t, 2 + lab, r, TAB_NONE, percent, 5, 1);
      tab_float (t, 3 + lab, r, TAB_NONE, valid_percent, 5, 1);
      tab_float (t, 4 + lab, r, TAB_NONE, cum_percent, 5, 1);
      r++;
    }
  for (; f < &v->p.frq.tab.valid[n_categories]; f++)
    {
      cum_freq += f->c;

      if (lab)
	{
	  char *label = get_val_lab (v, f->v, 0);
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
  double cum_percent = 0.0;

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
      cum_percent += f->c / v->p.frq.tab.valid_cases * 100.0;

      tab_value (t, 0, r, TAB_NONE, &f->v, &v->print);
      tab_float (t, 1, r, TAB_NONE, f->c, 8, 0);
      tab_float (t, 2, r, TAB_NONE, percent, 3, 0);
      tab_float (t, 3, r, TAB_NONE, cum_percent, 3, 0);
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
  double X_bar, M2, M3, M4;
  struct freq *f;

  /* Calculate the mean. */
  X_bar = 0.0;
  for (f = v->p.frq.tab.valid; f < v->p.frq.tab.missing; f++)
    X_bar += f->v.f * f->c;
  X_bar /= W;

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
  d[frq_max] = v->p.frq.tab.missing[-1].v.f;
  d[frq_mode] = 0.0;
  d[frq_range] = d[frq_max] - d[frq_min];
  d[frq_median] = 0.0;
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

  t = tab_create (2, n_stats, 0);
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

#if 0
/* Statistical calculation. */

static int degree[6];
static int maxdegree, minmax;

static void stat_func (struct freq *, VISIT, int);
static void calc_stats (int);
static void display_stats (int);

/* mapping of data[]:
 * 0=>8
 * 1=>9
 * 2=>10
 * index 3: number of modes found (detects multiple modes)
 * index 4: number of nodes processed, for calculation of median
 * 5=>11
 * 
 * mapping of dbl[]:
 * index 0-3: sum of X**i
 * index 4: minimum
 * index 5: maximum
 * index 6: mode
 * index 7: median
 * index 8: number of cases, valid and missing
 * index 9: number of valid cases
 * index 10: maximum frequency found, for calculation of mode
 * index 11: maximum frequency
 */
static void
out_stats (int i)
{
  int j;

  if (cur_var->type == ALPHA)
    return;
  for (j = 0; j < 8; j++)
    cur_var->dbl[j] = 0.;
  cur_var->dbl[10] = 0;
  cur_var->dbl[4] = DBL_MAX;
  cur_var->dbl[5] = -DBL_MAX;
  for (j = 2; j < 5; j++)
    cur_var->data[j] = 0;
  cur_var->p.frq.median_ncases = cur_var->p.frq.t.valid_cases / 2;
  avlwalk (cur_var->p.frq.t.f, stat_func, LEFT_TO_RIGHT);
  calc_stats (i);
  display_stats (i);
}

static void
calc_stats (int i)
{
  struct variable *v;
  double n;
  double *d;

  v = v_variables[i];
  n = v->p.frq.t.valid_cases;
  d = v->dbl;

  if (n < 2 || (n < 3 && stat[FRQ_ST_7]))
    {
      warn (_("only %g case%s for variable %s, statistics not "
	    "computed"), n, n == 1 ? "" : "s", v->name);
      return;
    }
  if (stat[FRQ_ST_9])
    v->res[FRQ_ST_9] = d[5] - d[4];
  if (stat[FRQ_ST_10])
    v->res[FRQ_ST_10] = d[4];
  if (stat[FRQ_ST_11])
    v->res[FRQ_ST_11] = d[5];
  if (stat[FRQ_ST_12])
    v->res[FRQ_ST_12] = d[0];
  if (stat[FRQ_ST_1] || stat[FRQ_ST_2] || stat[FRQ_ST_5] || stat[FRQ_ST_6] || stat[FRQ_ST_7])
    {
      v->res[FRQ_ST_1] = calc_mean (d, n);
      v->res[FRQ_ST_6] = calc_variance (d, n);
    }
  if (stat[FRQ_ST_2] || stat[FRQ_ST_5] || stat[FRQ_ST_7])
    v->res[FRQ_ST_5] = calc_stddev (v->res[FRQ_ST_6]);
  if (stat[FRQ_ST_2])
    v->res[FRQ_ST_2] = calc_semean (v->res[FRQ_ST_5], n);
  if (stat[FRQ_ST_7])
    {
      v->res[FRQ_ST_7] = calc_kurt (d, n, v->res[FRQ_ST_6]);
      v->res[FRQ_ST_14] = calc_sekurt (n);
    }
  if (stat[FRQ_ST_8])
    {
      v->res[FRQ_ST_8] = calc_skew (d, n, v->res[FRQ_ST_5]);
      v->res[FRQ_ST_15] = calc_seskew (n);
    }
  if (stat[FRQ_ST_MODE])
    {
      v->res[FRQ_ST_MODE] = v->dbl[6];
      if (v->data[3] > 1)
	warn (_("The variable %s has %d modes.  The lowest of these "
	      "is the one given in the table."), v->name, v->data[3]);
    }
  if (stat[FRQ_ST_MEDIAN])
    v->res[FRQ_ST_MEDIAN] = v->dbl[7];
}

static void
stat_func (struct freq * x, VISIT order, int param)
{
  double d, f;

  if (order != INORDER)
    return;
  f = d = x->v.f;
  cur_var->dbl[0] += (d * x->c);
  switch (maxdegree)
    {
    case 1:
      f *= d;
      cur_var->dbl[1] += (f * x->c);
      break;
    case 2:
      f *= d;
      cur_var->dbl[1] += (f * x->c);
      f *= d;
      cur_var->dbl[2] += (f * x->c);
      break;
    case 3:
      f *= d;
      cur_var->dbl[1] += (f * x->c);
      f *= d;
      cur_var->dbl[2] += (f * x->c);
      f *= d;
      cur_var->dbl[3] += (f * x->c);
      break;
    }
  if (minmax)
    {
      if (d < cur_var->dbl[4])
	cur_var->dbl[4] = d;
      if (d > cur_var->dbl[5])
	cur_var->dbl[5] = d;
    }
  if (x->c > cur_var->dbl[10])
    {
      cur_var->data[3] = 1;
      cur_var->dbl[10] = x->c;
      cur_var->dbl[6] = x->v.f;
    }
  else if (x->c == cur_var->dbl[10])
    cur_var->data[3]++;
  if (cur_var->data[4] < cur_var->p.frq.median_ncases
      && cur_var->data[4] + x->c >= cur_var->p.frq.median_ncases)
    cur_var->dbl[7] = x->v.f;
  cur_var->data[4] += x->c;
}

/* Statistical display. */
static int column, ncolumns;

static void outstat (char *, double);

static void
display_stats (int i)
{
  statname *sp;
  struct variable *v;
  int nlines;

  v = v_variables[i];
  ncolumns = (margin_width + 3) / 26;
  if (ncolumns < 1)
    ncolumns = 1;
  nlines = sc / ncolumns + (sc % ncolumns > 0);
  if (nlines == 2 && sc == 4)
    ncolumns = 2;
  if (nlines == 3 && sc == 9)
    ncolumns = 3;
  if (nlines == 4 && sc == 12)
    ncolumns = 3;
  column = 0;
  for (sp = st_name; sp->s != -1; sp++)
    if (stat[sp->s] == 1)
      outstat (gettext (sp->s10), v->res[sp->s]);
  if (column)
    out_eol ();
  blank_line ();
}

static void
outstat (char *label, double value)
{
  char buf[128], *cp;
  int dw, n;

  cp = &buf[0];
  if (!column)
    out_header ();
  else
    {
      memset (buf, ' ', 3);
      cp = &buf[3];
    }
  dw = 4;
  n = nsprintf (cp, "%-10s %12.4f", label, value);
  while (n > 23 && dw > 0)
    n = nsprintf (cp, "%-10s %12.*f", label, --dw, value);
  outs (buf);
  column++;
  if (column == ncolumns)
    {
      column = 0;
      out_eol ();
    }
}

/* Graphs. */

static rect pb, gb;		/* Page border, graph border. */
static int px, py;		/* Page width, height. */
static int ix, iy;		/* Inch width, height. */

static void draw_bar_chart (int);
static void draw_histogram (int);
static int scale_dep_axis (int);

static void
out_graphs (int i)
{
  struct variable *v;

  v = v_variables[i];
  if (avlcount (cur_var->p.frq.t.f) < 2
      || (chart == HIST && v_variables[i]->type == ALPHA))
    return;
  if (driver_id && set_highres == 1)
    {
      char *text;

      graf_page_size (&px, &py, &ix, &iy);
      graf_feed_page ();

      /* Calculate borders. */
      pb.x1 = ix;
      pb.y1 = iy;
      pb.x2 = px - ix;
      pb.y2 = py - iy;
      gb.x1 = pb.x1 + ix;
      gb.y1 = pb.y1 + iy;
      gb.x2 = pb.x2 - ix / 2;
      gb.y2 = pb.y2 - iy;

      /* Draw borders. */
      graf_frame_rect (COMPONENTS (pb));
      graf_frame_rect (COMPONENTS (gb));

      /* Draw axis labels. */
      graf_font_size (iy / 4);	/* 18-point text */
      text = format == PERCENT ? _("Percentage") : _("Frequency");
      graf_text (pb.x1 + max (ix, iy) / 4 + max (ix, iy) / 16, gb.y2, text,
		 SIDEWAYS);
      text = v->label ? v->label : v->name;
      graf_text (gb.x1, pb.y2 - iy / 4, text, UPRIGHT);

      /* Draw axes, chart proper. */
      if (chart == BAR ||
	  (chart == HBAR
       && (avlcount (cur_var->p.frq.t.f) || v_variables[i]->type == ALPHA)))
	draw_bar_chart (i);
      else
	draw_histogram (i);

      graf_eject_page ();
    }
  if (set_lowres == 1 || (set_lowres == 2 && (!driver_id || !set_highres)))
    {
      static warned;

      /* Do character-based graphs. */
      if (!warned)
	{
	  warn (_("low-res graphs not implemented"));
	  warned = 1;
	}
    }
}

#if __GNUC__ && !__CHECKER__
#define BIG_TYPE long long
#else /* !__GNUC__ */
#define BIG_TYPE double
#endif /* !__GNUC__ */

static void
draw_bar_chart (int i)
{
  int bar_width, bar_spacing;
  int w, max, row;
  double val;
  struct freq *f;
  rect r;
  AVLtraverser *t = NULL;

  w = (px - ix * 7 / 2) / avlcount (cur_var->p.frq.t.f);
  bar_width = w * 2 / 3;
  bar_spacing = w - bar_width;

#if !ALLOW_HUGE_BARS
  if (bar_width > ix / 2)
    bar_width = ix / 2;
#endif /* !ALLOW_HUGE_BARS */

  max = scale_dep_axis (cur_var->p.frq.t.max_freq);

  row = 0;
  r.x1 = gb.x1 + bar_spacing / 2;
  r.x2 = r.x1 + bar_width;
  r.y2 = gb.y2;
  graf_fill_color (255, 0, 0);
  for (f = avltrav (cur_var->p.frq.t.f, &t); f;
       f = avltrav (cur_var->p.frq.t.f, &t))
    {
      char buf2[64];
      char *buf;

      val = f->c;
      if (format == PERCENT)
	val = val * 100 / cur_var->p.frq.t.valid_cases;
      r.y1 = r.y2 - val * (height (gb) - 1) / max;
      graf_fill_rect (COMPONENTS (r));
      graf_frame_rect (COMPONENTS (r));
      buf = get_val_lab (cur_var, f->v, 0);
      if (!buf)
	if (cur_var->type == ALPHA)
	  buf = f->v.s;
	else
	  {
	    sprintf (buf2, "%g", f->v.f);
	    buf = buf2;
	  }
      graf_text (r.x1 + bar_width / 2,
		 gb.y2 + iy / 32 + row * iy / 9, buf, TCJUST);
      row ^= 1;
      r.x1 += bar_width + bar_spacing;
      r.x2 += bar_width + bar_spacing;
    }
  graf_fill_color (0, 0, 0);
}

#define round_down(X, V) 			\
	(floor ((X) / (V)) * (V))
#define round_up(X, V) 				\
	(ceil ((X) / (V)) * (V))

static void
draw_histogram (int i)
{
  double lower, upper, interval;
  int bars[MAX_HIST_BARS + 1], top, j;
  int err, addend, rem, nbars, row, max_freq;
  char buf[25];
  rect r;
  struct freq *f;
  AVLtraverser *t = NULL;

  lower = min == SYSMIS ? cur_var->dbl[4] : min;
  upper = max == SYSMIS ? cur_var->dbl[5] : max;
  if (upper - lower >= 10)
    {
      double l, u;

      u = round_up (upper, 5);
      l = round_down (lower, 5);
      nbars = (u - l) / 5;
      if (nbars * 2 + 1 <= MAX_HIST_BARS)
	{
	  nbars *= 2;
	  u = round_up (upper, 2.5);
	  l = round_down (lower, 2.5);
	  if (l + 1.25 <= lower && u - 1.25 >= upper)
	    nbars--, lower = l + 1.25, upper = u - 1.25;
	  else if (l + 1.25 <= lower)
	    lower = l + 1.25, upper = u + 1.25;
	  else if (u - 1.25 >= upper)
	    lower = l - 1.25, upper = u - 1.25;
	  else
	    nbars++, lower = l - 1.25, upper = u + 1.25;
	}
      else if (nbars < MAX_HIST_BARS)
	{
	  if (l + 2.5 <= lower && u - 2.5 >= upper)
	    nbars--, lower = l + 2.5, upper = u - 2.5;
	  else if (l + 2.5 <= lower)
	    lower = l + 2.5, upper = u + 2.5;
	  else if (u - 2.5 >= upper)
	    lower = l - 2.5, upper = u - 2.5;
	  else
	    nbars++, lower = l - 2.5, upper = u + 2.5;
	}
      else
	nbars = MAX_HIST_BARS;
    }
  else
    {
      nbars = avlcount (cur_var->p.frq.t.f);
      if (nbars > MAX_HIST_BARS)
	nbars = MAX_HIST_BARS;
    }
  if (nbars < MIN_HIST_BARS)
    nbars = MIN_HIST_BARS;
  interval = (upper - lower) / nbars;

  memset (bars, 0, sizeof (int[nbars + 1]));
  if (lower >= upper)
    {
      msg (SE, _("Could not make histogram for %s for specified "
	   "minimum %g and maximum %g; please discard graph."), cur_var->name,
	   lower, upper);
      return;
    }
  for (f = avltrav (cur_var->p.frq.t.f, &t); f;
       f = avltrav (cur_var->p.frq.t.f, &t))
    if (f->v.f == upper)
      bars[nbars - 1] += f->c;
    else if (f->v.f >= lower && f->v.f < upper)
      bars[(int) ((f->v.f - lower) / interval)] += f->c;
  bars[nbars - 1] += bars[nbars];
  for (j = top = 0; j < nbars; j++)
    if (bars[j] > top)
      top = bars[j];
  max_freq = top;
  top = scale_dep_axis (top);

  err = row = 0;
  addend = width (gb) / nbars;
  rem = width (gb) % nbars;
  r.x1 = gb.x1;
  r.x2 = r.x1 + addend;
  r.y2 = gb.y2;
  err += rem;
  graf_fill_color (255, 0, 0);
  for (j = 0; j < nbars; j++)
    {
      int w;

      r.y1 = r.y2 - (BIG_TYPE) bars[j] * (height (gb) - 1) / top;
      graf_fill_rect (COMPONENTS (r));
      graf_frame_rect (COMPONENTS (r));
      sprintf (buf, "%g", lower + interval / 2 + interval * j);
      graf_text (r.x1 + addend / 2,
		 gb.y2 + iy / 32 + row * iy / 9, buf, TCJUST);
      row ^= 1;
      w = addend;
      err += rem;
      while (err >= addend)
	{
	  w++;
	  err -= addend;
	}
      r.x1 = r.x2;
      r.x2 = r.x1 + w;
    }
  if (normal)
    {
      double x, y, variance, mean, step, factor;

      variance = cur_var->res[FRQ_ST_VARIANCE];
      mean = cur_var->res[FRQ_ST_MEAN];
      factor = (1. / (sqrt (2. * PI * variance))
		* cur_var->p.frq.t.valid_cases * interval);
      graf_polyline_begin ();
      for (x = lower, step = (upper - lower) / (POLYLINE_DENSITY);
	   x <= upper; x += step)
	{
	  y = factor * exp (-square (x - mean) / (2. * variance));
	  debug_printf (("(%20.10f, %20.10f)\n", x, y));
	  graf_polyline_point (gb.x1 + (x - lower) / (upper - lower) * width (gb),
			       gb.y2 - y * (height (gb) - 1) / top);
	}
      graf_polyline_end ();
    }
  graf_fill_color (0, 0, 0);
}

static int
scale_dep_axis (int max)
{
  int j, s, x, y, ty, by;
  char buf[10];

  x = 10, s = 2;
  if (scale != SYSMIS && max < scale)
    x = scale, s = scale / 5;
  else if (format == PERCENT)
    {
      max = ((BIG_TYPE) 100 * cur_var->p.frq.t.max_freq
	     / cur_var->p.frq.t.valid_cases + 1);
      if (max < 5)
	x = 5, s = 1;
      else if (max < 10)
	x = 10, s = 2;
      else if (max < 25)
	x = 25, s = 5;
      else if (max < 50)
	x = 50, s = 10;
      else
	max = 100, s = 20;
    }
  else				/* format==FREQ */
    /* Uses a progression of 10, 20, 50, 100, 200, 500, ... */
    for (;;)
      {
	if (x > max)
	  break;
	x *= 2;
	s *= 2;
	if (x > max)
	  break;
	x = x / 2 * 5;
	s = s / 2 * 5;
	if (x > max)
	  break;
	x *= 2;
	s *= 2;
      }
  graf_font_size (iy / 9);	/* 8-pt text */
  for (j = 0; j <= x; j += s)
    {
      y = gb.y2 - (BIG_TYPE) j *(height (gb) - 1) / x;
      ty = y - iy / 64;
      by = y + iy / 64;
      if (ty < gb.y1)
	ty += iy / 64, by += iy / 64;
      else if (by > gb.y2)
	ty -= iy / 64, by -= iy / 64;
      graf_fill_rect (gb.x1 - ix / 16, ty, gb.x1, by);
      sprintf (buf, "%d", j);
      graf_text (gb.x1 - ix / 8, (ty + by) / 2, buf, CRJUST);
    }
  return x;
}

/* Percentiles. */

static void ungrouped_pcnt (int i);
static int grouped_interval_pcnt (int i);
static void out_pcnt (double, double);

static void
out_percentiles (int i)
{
  if (cur_var->type == ALPHA || !n_percentiles)
    return;

  outs_line (_("Percentile    Value     "
	     "Percentile    Value     "
	     "Percentile    Value"));
  blank_line ();

  column = 0;
  if (!g_var[i])
    ungrouped_pcnt (i);
  else if (g_var[i] == 1)
    grouped_interval_pcnt (i);
#if 0
  else if (g_var[i] == -1)
    grouped_pcnt (i);
  else
    grouped_boundaries_pcnt (i);
#else /* !0 */
  else
    warn (_("this form of percentiles not supported"));
#endif
  if (column)
    out_eol ();
}

static void
out_pcnt (double pcnt, double value)
{
  if (!column)
    out_header ();
  else
    outs ("     ");
  out ("%7.2f%13.3f", pcnt * 100., value);
  column++;
  if (column == 3)
    {
      out_eol ();
      column = 0;
    }
}

static void
ungrouped_pcnt (int i)
{
  AVLtraverser *t = NULL;
  struct freq *f;
  double *p, *e;
  int sum;

  p = percentiles;
  e = &percentiles[n_percentiles];
  sum = 0;
  for (f = avltrav (cur_var->p.frq.t.f, &t);
       f && p < e; f = avltrav (cur_var->p.frq.t.f, &t))
    {
      sum += f->c;
      while (sum >= p[0] * cur_var->p.frq.t.valid_cases && p < e)
	out_pcnt (*p++, f->v.f);
    }
}


static int
grouped_interval_pcnt (int i)
{
  AVLtraverser * t = NULL;
  struct freq * f, *fp;
  double *p, *e, w;
  int sum, psum;

  p = percentiles;
  e = &percentiles[n_percentiles];
  w = gl_var[i][0];
  sum = psum = 0;
  for (fp = 0, f = avltrav (cur_var->p.frq.t.f, &t);
       f && p < e;
       fp = f, f = avltrav (cur_var->p.frq.t.f, &t))
    {
      if (fp)
	if (fabs (f->v.f - fp->v.f) < w)
	  {
	    out_eol ();
	    column = 0;
	    return msg (SE, _("Difference between %g and %g is "
			      "too small for grouping interval %g."), f->v.f,
			fp->v.f, w);
	  }
      psum = sum;
      sum += f->c;
      while (sum >= p[0] * cur_var->p.frq.t.valid_cases && p < e)
	{
	  out_pcnt (p[0], (((p[0] * cur_var->p.frq.t.valid_cases) - psum) * w / f->c
			   + (f->v.f - w / 2)));
	  p++;
	}
    }
  return 1;
}
#endif

/* 
   Local Variables:
   mode: c
   End:
*/
