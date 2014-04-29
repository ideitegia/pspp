/*
  PSPP - a program for statistical analysis.
  Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2011, 2014 Free Software Foundation, Inc.
   
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

#include <config.h>
#include <stdlib.h>
#include <gsl/gsl_histogram.h>


#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "data/settings.h"
#include "data/value-labels.h"
#include "data/variable.h"

#include "language/dictionary/split-file.h"

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "language/stats/freq.h"

#include "libpspp/array.h"
#include "libpspp/bit-vector.h"
#include "libpspp/compiler.h"
#include "libpspp/hmap.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"

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

/* Percentiles to calculate. */

struct percentile
{
  double p;        /* the %ile to be calculated */
  double value;    /* the %ile's value */
  bool show;       /* True to show this percentile in the statistics box. */
};

static int
ptile_3way (const void *_p1, const void *_p2)
{
  const struct percentile *p1 = _p1;
  const struct percentile *p2 = _p2;

  if (p1->p < p2->p)
    return -1;

  return (p1->p > p2->p);
}


enum
  {
    FRQ_NONORMAL,
    FRQ_NORMAL
  };

enum
  {
    FRQ_FREQ,
    FRQ_PERCENT
  };

enum sortprops 
  {
    FRQ_AFREQ,
    FRQ_DFREQ,
    FRQ_AVALUE,
    FRQ_DVALUE
  };

/* Array indices for STATISTICS subcommand. */
enum
  {
    FRQ_ST_MEAN,
    FRQ_ST_SEMEAN,
    FRQ_ST_MEDIAN,
    FRQ_ST_MODE,
    FRQ_ST_STDDEV,
    FRQ_ST_VARIANCE,
    FRQ_ST_KURTOSIS,
    FRQ_ST_SEKURTOSIS,
    FRQ_ST_SKEWNESS,
    FRQ_ST_SESKEWNESS,
    FRQ_ST_RANGE,
    FRQ_ST_MINIMUM,
    FRQ_ST_MAXIMUM,
    FRQ_ST_SUM,
    FRQ_ST_count
  };

/* Description of statistics. */
static const char *st_name[FRQ_ST_count] =
{
   N_("Mean"),
   N_("S.E. Mean"),
   N_("Median"),
   N_("Mode"),
   N_("Std Dev"),
   N_("Variance"),
   N_("Kurtosis"),
   N_("S.E. Kurt"),
   N_("Skewness"),
   N_("S.E. Skew"),
   N_("Range"),
   N_("Minimum"),
   N_("Maximum"),
   N_("Sum")
};

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

/* Per-variable frequency data. */
struct var_freqs
  {
    const struct variable *var;

    /* Freqency table. */
    struct freq_tab tab;	/* Frequencies table to use. */

    /* Percentiles. */
    int n_groups;		/* Number of groups. */
    double *groups;		/* Groups. */

    /* Statistics. */
    double stat[FRQ_ST_count];

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
                                   or FRQ_AFREQ or FRQ_DFREQ. */

    /* Statistics; number of statistics. */
    unsigned long stats;
    int n_stats;

    /* Histogram and pie chart settings. */
    struct frq_chart *hist, *pie;
  };


struct freq_compare_aux
  {
    bool by_freq;
    bool ascending_freq;

    int width;
    bool ascending_value;
  };

static void calc_stats (const struct var_freqs *vf, double d[FRQ_ST_count]);

static void do_piechart(const struct frq_chart *pie, 
			const struct variable *var,
			const struct freq_tab *frq_tab);

static void dump_statistics (const struct frq_proc *frq, 
			     const struct var_freqs *vf,
			     const struct variable *wv);

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

/* Create a gsl_histogram from a freq_tab */
static struct histogram *
freq_tab_to_hist (const struct frq_proc *frq, const struct freq_tab *ft,
                  const struct variable *var);


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
  tab_set_format (t, RC_WEIGHT, wfmt);
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
      tab_double (t, 2, r, TAB_NONE, f->count, NULL, RC_WEIGHT);
      tab_double (t, 3, r, TAB_NONE, percent, NULL, RC_OTHER);
      tab_double (t, 4, r, TAB_NONE, valid_percent, NULL, RC_OTHER);
      tab_double (t, 5, r, TAB_NONE, cum_total, NULL, RC_OTHER);
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
      tab_double (t, 2, r, TAB_NONE, f->count, NULL, RC_WEIGHT);
      tab_double (t, 3, r, TAB_NONE,
		  f->count / ft->total_cases * 100.0, NULL, RC_OTHER);
      tab_text (t, 4, r, TAB_NONE, _("Missing"));
      r++;
    }

  tab_box (t, TAL_1, TAL_1, -1, TAL_1, 0, 0, 5, r);
  tab_hline (t, TAL_2, 0, 5, 1);
  tab_hline (t, TAL_2, 0, 5, r);
  tab_joint_text (t, 0, r, 1, r, TAB_RIGHT | TAT_TITLE, _("Total"));
  tab_vline (t, TAL_0, 1, r, r);
  tab_double (t, 2, r, TAB_NONE, cum_freq, NULL, RC_WEIGHT);
  tab_double (t, 3, r, TAB_NONE, 100.0, &F_5_1, RC_OTHER);
  tab_double (t, 4, r, TAB_NONE, 100.0, &F_5_1, RC_OTHER);

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

/* Returns true iff the value in struct freq F is non-missing
   for variable V. */
static bool
not_missing (const void *f_, const void *v_)
{
  const struct freq *f = f_;
  const struct variable *v = v_;

  return !var_is_value_missing (v, &f->value, MV_ANY);
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

      calc_percentiles (frq, vf);

      /* Statistics. */
      if (frq->n_stats)
	dump_statistics (frq, vf, wv);

      if (frq->hist && var_is_numeric (vf->var) && vf->tab.n_valid > 0)
	{
	  double d[FRQ_ST_count];
	  struct histogram *histogram;

	  calc_stats (vf, d);

	  histogram = freq_tab_to_hist (frq, &vf->tab, vf->var);

	  if ( histogram)
	    {
	      chart_item_submit (histogram_chart_create (
                               histogram->gsl_hist, var_to_string(vf->var),
                               vf->tab.valid_cases,
                               d[FRQ_ST_MEAN],
                               d[FRQ_ST_STDDEV],
                               frq->hist->draw_normal));

	      statistic_destroy (&histogram->parent);
	    }
	}

      if (frq->pie)
        do_piechart(frq->pie, vf->var, &vf->tab);

      cleanup_freq_tab (vf);
    }
}

int
cmd_frequencies (struct lexer *lexer, struct dataset *ds)
{
  int i;
  struct frq_proc frq;
  const struct variable **vars;

  bool sbc_barchart = false;
  bool sbc_piechart = false;
  bool sbc_histogram = false;

  double pie_min = -DBL_MAX;
  double pie_max = DBL_MAX;
  bool pie_missing = false;

  double hi_min = -DBL_MAX;
  double hi_max = DBL_MAX;
  int hi_scale = FRQ_FREQ;
  int hi_freq = INT_MIN;
  int hi_pcnt = INT_MIN;
  int hi_norm = FRQ_NONORMAL;

  frq.pool = pool_create ();
  frq.sort = FRQ_AVALUE;

  frq.vars = NULL;
  frq.n_vars = 0;
  
  frq.stats = BIT_INDEX (FRQ_ST_MEAN) 
    | BIT_INDEX (FRQ_ST_STDDEV) 
    | BIT_INDEX (FRQ_ST_MINIMUM)
    | BIT_INDEX (FRQ_ST_MAXIMUM);

  frq.n_stats = 4;

  frq.max_categories = INT_MAX;

  frq.percentiles = NULL;
  frq.n_percentiles = 0;
  frq.n_show_percentiles = 0;

  frq.hist = NULL;
  frq.pie = NULL;


  /* Accept an optional, completely pointless "/VARIABLES=" */
  lex_match (lexer, T_SLASH);
  if (lex_match_id  (lexer, "VARIABLES"))
    {
      if (! lex_force_match (lexer, T_EQUALS) )
        goto error;
    }

  if (!parse_variables_const (lexer, dataset_dict (ds),
			      &vars,
			      &frq.n_vars,
			      PV_NO_DUPLICATE))
    goto error;

  frq.vars = xzalloc (frq.n_vars * sizeof (*frq.vars));
  for (i = 0; i < frq.n_vars; ++i)
    {
      frq.vars[i].var = vars[i];
    }

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "STATISTICS"))
	{
	  lex_match (lexer, T_EQUALS);

	  frq.stats = 0;
	  frq.n_stats = 0;

	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
              if (lex_match_id (lexer, "DEFAULT"))
                {
		  frq.stats = BIT_INDEX (FRQ_ST_MEAN) 
		    | BIT_INDEX (FRQ_ST_STDDEV) 
		    | BIT_INDEX (FRQ_ST_MINIMUM)
		    | BIT_INDEX (FRQ_ST_MAXIMUM);

		  frq.n_stats = 4;
                }
              else if (lex_match_id (lexer, "MEAN"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_MEAN);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "SEMEAN"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_SEMEAN);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "MEDIAN"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_MEDIAN);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "MODE"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_MODE);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "STDDEV"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_STDDEV);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "VARIANCE"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_MEAN);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "KURTOSIS"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_KURTOSIS);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "SKEWNESS"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_SKEWNESS);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "RANGE"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_RANGE);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "MINIMUM"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_MINIMUM);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "MAXIMUM"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_MAXIMUM);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "SUM"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_SUM);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "SESKEWNESS"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_SESKEWNESS);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "SEKURTOSIS"))
                {
		  frq.stats |= BIT_INDEX (FRQ_ST_SEKURTOSIS);
		  frq.n_stats++;
                }
              else if (lex_match_id (lexer, "NONE"))
                {
		  frq.stats = 0;
		  frq.n_stats = 0;
                }
              else if (lex_match (lexer, T_ALL))
                {
		  frq.stats = ~0;
		  frq.n_stats = FRQ_ST_count;
                }
              else
                {
                  lex_error (lexer, NULL);
                  goto error;
                }
            }
        }
      else if (lex_match_id (lexer, "PERCENTILES"))
        {
	  lex_match (lexer, T_EQUALS);
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_force_num (lexer))
		{
		  frq.percentiles =
		    xrealloc (frq.percentiles, 
			      (frq.n_percentiles + 1)
			      * sizeof (*frq.percentiles));
		  frq.percentiles[frq.n_percentiles].p = lex_number (lexer)  / 100.0;
		  frq.percentiles[frq.n_percentiles].show = true;
		  lex_get (lexer);
		  frq.n_percentiles++;
		  frq.n_show_percentiles++;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "FORMAT"))
        {
	  lex_match (lexer, T_EQUALS);
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "TABLE"))
		{
		  
		}
	      else if (lex_match_id (lexer, "NOTABLE"))
		{
		  frq.max_categories = 0;
		}
	      else if (lex_match_id (lexer, "AVALUE"))
		{
		  frq.sort = FRQ_AVALUE;
		}
	      else if (lex_match_id (lexer, "DVALUE"))
		{
		  frq.sort = FRQ_DVALUE;
		}
	      else if (lex_match_id (lexer, "AFREQ"))
		{
		  frq.sort = FRQ_AFREQ;
		}
	      else if (lex_match_id (lexer, "DFREQ"))
		{
		  frq.sort = FRQ_DFREQ;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "NTILES"))
        {
	  lex_match (lexer, T_EQUALS);

	  if (lex_force_int (lexer))
	    {
	      int i;
	      int n = lex_integer (lexer);
	      lex_get (lexer);
	      for (i = 0; i < n + 1; ++i)
		{
		  frq.percentiles =
		    xrealloc (frq.percentiles, 
			      (frq.n_percentiles + 1)
			      * sizeof (*frq.percentiles));
		  frq.percentiles[frq.n_percentiles].p =
		    i / (double) n ;
		  frq.percentiles[frq.n_percentiles].show = true;

		  frq.n_percentiles++;
		  frq.n_show_percentiles++;
		}
	    }
	  else
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "ALGORITHM"))
        {
	  lex_match (lexer, T_EQUALS);

	  if (lex_match_id (lexer, "COMPATIBLE"))
	    {
              settings_set_cmd_algorithm (COMPATIBLE);
	    }
	  else if (lex_match_id (lexer, "ENHANCED"))
	    {
              settings_set_cmd_algorithm (ENHANCED);
	    }
	  else
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "HISTOGRAM"))
        {
	  lex_match (lexer, T_EQUALS);
	  sbc_histogram = true;

	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "NORMAL"))
		{
		  hi_norm = FRQ_NORMAL;
		}
	      else if (lex_match_id (lexer, "NONORMAL"))
		{
		  hi_norm = FRQ_NONORMAL;
		}
	      else if (lex_match_id (lexer, "FREQ"))
		{
                  hi_scale = FRQ_FREQ;
                  if (lex_match (lexer, T_LPAREN))
                    {
                      if (lex_force_int (lexer))
                        {
			  hi_freq = lex_integer (lexer);
			  if (hi_freq <= 0)
			    {
			      lex_error (lexer, _("Histogram frequency must be greater than zero."));
			    }
			  lex_get (lexer);
			  lex_force_match (lexer, T_RPAREN);
			}
                    }
		}
	      else if (lex_match_id (lexer, "PERCENT"))
		{
                  hi_scale = FRQ_PERCENT;
                  if (lex_match (lexer, T_LPAREN))
                    {
                      if (lex_force_int (lexer))
			{
			  hi_pcnt = lex_integer (lexer);
			  if (hi_pcnt <= 0)
			    {
			      lex_error (lexer, _("Histogram percentaage must be greater than zero."));
			    }
			  lex_get (lexer);
			  lex_force_match (lexer, T_RPAREN);
			}
                    }
		}
	      else if (lex_match_id (lexer, "MINIMUM"))
		{
		  lex_force_match (lexer, T_LPAREN);
		  if (lex_force_num (lexer))
		    {
		      hi_min = lex_number (lexer);
		      lex_get (lexer);
		    }
		  lex_force_match (lexer, T_RPAREN);
		}
	      else if (lex_match_id (lexer, "MAXIMUM"))
		{
		  lex_force_match (lexer, T_LPAREN);
		  if (lex_force_num (lexer))
		    {
		      hi_max = lex_number (lexer);
		      lex_get (lexer);
		    }
 		  lex_force_match (lexer, T_RPAREN);
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "PIECHART"))
        {
	  lex_match (lexer, T_EQUALS);
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "MINIMUM"))
		{
		  lex_force_match (lexer, T_LPAREN);
		  if (lex_force_num (lexer))
		    {
		      pie_min = lex_number (lexer);
		      lex_get (lexer);
		    }
		  lex_force_match (lexer, T_RPAREN);
		}
	      else if (lex_match_id (lexer, "MAXIMUM"))
		{
		  lex_force_match (lexer, T_LPAREN);
		  if (lex_force_num (lexer))
		    {
		      pie_max = lex_number (lexer);
		      lex_get (lexer);
		    }
 		  lex_force_match (lexer, T_RPAREN);
		}
	      else if (lex_match_id (lexer, "MISSING"))
		{
		  pie_missing = true;
		}
	      else if (lex_match_id (lexer, "NOMISSING"))
		{
		  pie_missing = false;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	  sbc_piechart = true;
	}
      else if (lex_match_id (lexer, "MISSING"))
        {
	  lex_match (lexer, T_EQUALS);

	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
              if (lex_match_id (lexer, "EXCLUDE"))
                {
                }
              else if (lex_match_id (lexer, "INCLUDE"))
                {
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

  if (frq.stats & FRQ_ST_MEDIAN)
    {
	frq.percentiles =
	  xrealloc (frq.percentiles, 
		    (frq.n_percentiles + 1)
		    * sizeof (*frq.percentiles));
	
	frq.percentiles[frq.n_percentiles].p = 0.50;
	frq.percentiles[frq.n_percentiles].show = true;

	frq.n_percentiles++;
    }


/* Figure out which charts the user requested.  */

  {
    if (sbc_barchart)
      msg (SW, _("Bar charts are not implemented."));

    if (sbc_histogram)
      {
	struct frq_chart *hist;

	hist = frq.hist = xmalloc (sizeof *frq.hist);
	hist->x_min = hi_min;
	hist->x_max = hi_max;
	hist->y_scale = hi_scale;
	hist->y_max = hi_scale == FRQ_FREQ ? hi_freq : hi_pcnt;
	hist->draw_normal = hi_norm != FRQ_NONORMAL;
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

	frq.percentiles =
	  xrealloc (frq.percentiles, 
		    (frq.n_percentiles + 2)
		    * sizeof (*frq.percentiles));
	
	frq.percentiles[frq.n_percentiles].p = 0.25;
	frq.percentiles[frq.n_percentiles].show = false;

	frq.percentiles[frq.n_percentiles + 1].p = 0.75;
	frq.percentiles[frq.n_percentiles + 1].show = false;
	
	frq.n_percentiles+=2;
      }

    if (sbc_piechart)
      {
	struct frq_chart *pie;

	pie = frq.pie = xmalloc (sizeof *frq.pie);
	pie->x_min = pie_min;
	pie->x_max = pie_max;
	pie->include_missing = pie_missing;

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

  {
    int i,o;
    double previous_p = -1;
    qsort (frq.percentiles, frq.n_percentiles,
	   sizeof (*frq.percentiles), 
	   ptile_3way);

    frq.n_show_percentiles = 0;
    for (i = o = 0; i < frq.n_percentiles; ++i)
      {
	frq.percentiles[o].p = frq.percentiles[i].p;

	if (frq.percentiles[i].show)
	  frq.percentiles[o].show = true;

	if (frq.percentiles[i].p != previous_p)
	  {
	    if (frq.percentiles[i].show)
	      frq.n_show_percentiles++;

	    o++;
	  }

	previous_p = frq.percentiles[i].p;
      }

    frq.n_percentiles = o;
  }

  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;

    grouper = casegrouper_create_splits (proc_open (ds), dataset_dict (ds));
    while (casegrouper_get_next_group (grouper, &group))
      {
	struct ccase *c;
	precalc (&frq, group, ds);
	for (; (c = casereader_read (group)) != NULL; case_unref (c))
	  calc (&frq, c, ds);
	postcalc (&frq, ds);
      }
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }


  return CMD_SUCCESS;

 error:

  return CMD_FAILURE;
}

static double
calculate_iqr (const struct frq_proc *frq)
{
  double q1 = SYSMIS;
  double q3 = SYSMIS;
  int i;

  /* This cannot work unless the 25th and 75th percentile are calculated */
  assert (frq->n_percentiles >= 2);
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
static struct histogram *
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


  iqr = calculate_iqr (frq);

  if (iqr > 0)
    /* Freedman-Diaconis' choice of bin width. */
    bin_width = 2 * iqr / pow (valid_freq, 1.0 / 3.0);

  else
    /* Sturges Rule */
    bin_width = (x_max - x_min) / (1 + log2 (valid_freq));

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

/* Calculates all the pertinent statistics for VF, putting them in array
   D[]. */
static void
calc_stats (const struct var_freqs *vf, double d[FRQ_ST_count])
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
  moments_calculate (m, NULL, &d[FRQ_ST_MEAN], &d[FRQ_ST_VARIANCE],
                     &d[FRQ_ST_SKEWNESS], &d[FRQ_ST_KURTOSIS]);
  moments_destroy (m);

  /* Formulas below are taken from _SPSS Statistical Algorithms_. */
  d[FRQ_ST_MINIMUM] = ft->valid[0].value.f;
  d[FRQ_ST_MAXIMUM] = ft->valid[ft->n_valid - 1].value.f;
  d[FRQ_ST_MODE] = X_mode;
  d[FRQ_ST_RANGE] = d[FRQ_ST_MAXIMUM] - d[FRQ_ST_MINIMUM];
  d[FRQ_ST_SUM] = d[FRQ_ST_MEAN] * W;
  d[FRQ_ST_STDDEV] = sqrt (d[FRQ_ST_VARIANCE]);
  d[FRQ_ST_SEMEAN] = d[FRQ_ST_STDDEV] / sqrt (W);
  d[FRQ_ST_SESKEWNESS] = calc_seskew (W);
  d[FRQ_ST_SEKURTOSIS] = calc_sekurt (W);
}

/* Displays a table of all the statistics requested for variable V. */
static void
dump_statistics (const struct frq_proc *frq, const struct var_freqs *vf,
                 const struct variable *wv)
{
  const struct fmt_spec *wfmt = wv ? var_get_print_format (wv) : &F_8_0;
  const struct freq_tab *ft = &vf->tab;
  double stat_value[FRQ_ST_count];
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

  t = tab_create (3, ((frq->stats & FRQ_ST_MEDIAN) ? frq->n_stats - 1 : frq->n_stats)
		  + frq->n_show_percentiles + 2);
  tab_set_format (t, RC_WEIGHT, wfmt);
  tab_box (t, TAL_1, TAL_1, -1, -1 , 0 , 0 , 2, tab_nr(t) - 1) ;


  tab_vline (t, TAL_1 , 2, 0, tab_nr(t) - 1);
  tab_vline (t, TAL_GAP , 1, 0, tab_nr(t) - 1 ) ;

  r = 2; /* N missing and N valid are always dumped */

  for (i = 0; i < FRQ_ST_count; i++)
    {
      if (FRQ_ST_MEDIAN == i)
	continue;

      if (frq->stats & BIT_INDEX (i))
      {
	tab_text (t, 0, r, TAB_LEFT | TAT_TITLE,
		      gettext (st_name[i]));
	tab_double (t, 2, r, TAB_NONE, stat_value[i], NULL, RC_OTHER);
	r++;
      }
    }

  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("N"));
  tab_text (t, 1, 0, TAB_LEFT | TAT_TITLE, _("Valid"));
  tab_text (t, 1, 1, TAB_LEFT | TAT_TITLE, _("Missing"));

  tab_double (t, 2, 0, TAB_NONE, ft->valid_cases, NULL, RC_WEIGHT);
  tab_double (t, 2, 1, TAB_NONE, ft->total_cases - ft->valid_cases, NULL, RC_WEIGHT);

  for (i = 0; i < frq->n_percentiles; i++)
    {
      const struct percentile *pc = &frq->percentiles[i];

      if (!pc->show)
        continue;

      if ( i == 0 )
	{
	  tab_text (t, 0, r, TAB_LEFT | TAT_TITLE, _("Percentiles"));
	}

      if (pc->p == 0.5)
        tab_text (t, 1, r, TAB_LEFT, _("50 (Median)"));
      else
        tab_double (t, 1, r, TAB_LEFT, pc->p * 100, NULL, RC_INTEGER);
      tab_double (t, 2, r, TAB_NONE, pc->value,
                  var_get_print_format (vf->var), RC_OTHER);
      r++;
    }

  tab_title (t, "%s", var_to_string (vf->var));

  tab_submit (t);
}

