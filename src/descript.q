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

/* FIXME: Many possible optimizations. */

#include <config.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include "algorithm.h"
#include "alloc.h"
#include "bitvector.h"
#include "command.h"
#include "lexer.h"
#include "error.h"
#include "magic.h"
#include "stats.h"
#include "som.h"
#include "tab.h"
#include "var.h"
#include "vfm.h"

/* (specification)
   DESCRIPTIVES (dsc_):
     *variables=custom;
     +missing=miss:!variable/listwise,incl:!noinclude/include;
     +format=labeled:!labels/nolabels,indexed:!noindex/index,lined:!line/serial;
     +save=;
     +options[op_]=1,2,3,4,5,6,7,8;
     +statistics[st_]=all,1|mean,2|semean,5|stddev,6|variance,7|kurtosis,
		      8|skewness,9|range,10|minimum,11|maximum,12|sum,
		      13|default,14|seskewness,15|sekurtosis;
     +sort=sortby:mean/semean/stddev/variance/kurtosis/skewness/range/
	   range/minimum/maximum/sum/name/seskewness/sekurtosis/!none, 
	   order:!a/d.
*/
/* (declarations) */
/* (functions) */

/* DESCRIPTIVES private data. */

/* Describes properties of a distribution for the purpose of
   calculating a Z-score. */
struct dsc_z_score
  {
    struct variable *s, *d;	/* Source, destination variable. */
    double mean;		/* Distribution mean. */
    double std_dev;		/* Distribution standard deviation. */
  };

/* DESCRIPTIVES transformation (for calculating Z-scores). */
struct descriptives_trns
  {
    struct trns_header h;
    int n;			/* Number of Z-scores. */
    struct dsc_z_score *z;	/* Array of Z-scores. */
  };

/* These next three vars, see comment at top of display(). */
/* Number of cases missing listwise, even if option 5 not selected. */
static double d_glob_miss_list;

/* Number of total *cases* valid or missing, as a double.  Unless
   option 5 is selected, d_glob_missing is 0. */
static double d_glob_valid, d_glob_missing;

/* Set when a weighting variable is missing or <=0. */
static int bad_weight;

/* Number of generic zvarnames we've generated in this execution. */
static int z_count;

/* Variables specified on command. */
static struct variable **v_variables;
static int n_variables;

/* Command specifications. */
static struct cmd_descriptives cmd;

/* Whether z-scores are computed. */
static int z_scores;

/* Statistic to sort by. */
static int sortby_stat;

/* Statistics to display. */
static unsigned long stats;

/* Easier access to long-named arrays. */
#define stat cmd.a_statistics
#define opt  cmd.a_options

/* Groups of statistics. */
#define BI          BIT_INDEX

#define dsc_default 							\
	(BI (dsc_mean) | BI (dsc_stddev) | BI (dsc_min) | BI (dsc_max))
     
#define dsc_all							\
	(BI (dsc_sum) | BI (dsc_min) | BI (dsc_max)		\
	 | BI (dsc_mean) | BI (dsc_semean) | BI (dsc_stddev)	\
	 | BI (dsc_variance) | BI (dsc_kurt) | BI (dsc_sekurt)	\
	 | BI (dsc_skew) | BI (dsc_seskew) | BI (dsc_range)	\
	 | BI (dsc_range))

/* Table of options. */
#define op_incl_miss	DSC_OP_1	/* Honored. */
#define op_no_varlabs	DSC_OP_2	/* Ignored. */
#define op_zscores	DSC_OP_3	/* Honored. */
#define op_index	DSC_OP_4	/* FIXME. */
#define op_excl_miss	DSC_OP_5	/* Honored. */
#define op_serial	DSC_OP_6	/* Honored. */
#define op_narrow	DSC_OP_7	/* Ignored. */
#define op_no_varnames	DSC_OP_8	/* Honored. */

/* Describes one statistic that can be calculated. */
/* FIXME: Currently sm,col_width are not used. */
struct dsc_info
  {
    int st_indx;		/* Index into st_a_statistics[]. */
    int sb_indx;		/* Sort-by index. */
    const char *s10;		/* Name, stuffed into 10 columns. */
    const char *s8;		/* Name, stuffed into 8 columns. */
    const char *sm;		/* Name, stuffed minimally. */
    const char *s;		/* Full name. */
    int max_degree;		/* Highest degree necessary to calculate this
				   statistic. */
    int col_width;		/* Column width (not incl. spacing between columns) */
  };

/* Table of statistics, indexed by dsc_*. */
static struct dsc_info dsc_info[dsc_n_stats] =
{
  {DSC_ST_MEAN, DSC_MEAN, N_("Mean"), N_("Mean"), N_("Mean"), N_("mean"), 1, 10},
  {DSC_ST_SEMEAN, DSC_SEMEAN, N_("S.E. Mean"), N_("S E Mean"), N_("SE"),
   N_("standard error of mean"), 2, 10},
  {DSC_ST_STDDEV, DSC_STDDEV, N_("Std Dev"), N_("Std Dev"), N_("SD"),
   N_("standard deviation"), 2, 11},
  {DSC_ST_VARIANCE, DSC_VARIANCE, N_("Variance"), N_("Variance"),
   N_("Var"), N_("variance"), 2, 12},
  {DSC_ST_KURTOSIS, DSC_KURTOSIS, N_("Kurtosis"), N_("Kurtosis"),
   N_("Kurt"), N_("kurtosis"), 4, 9},
  {DSC_ST_SEKURTOSIS, DSC_SEKURTOSIS, N_("S.E. Kurt"), N_("S E Kurt"), N_("SEKurt"),
   N_("standard error of kurtosis"), 0, 9},
  {DSC_ST_SKEWNESS, DSC_SKEWNESS, N_("Skewness"), N_("Skewness"), N_("Skew"),
   N_("skewness"), 3, 9},
  {DSC_ST_SESKEWNESS, DSC_SESKEWNESS, N_("S.E. Skew"), N_("S E Skew"), N_("SESkew"),
   N_("standard error of skewness"), 0, 9},
  {DSC_ST_RANGE, DSC_RANGE, N_("Range"), N_("Range"), N_("Rng"), N_("range"), 0, 10},
  {DSC_ST_MINIMUM, DSC_MINIMUM, N_("Minimum"), N_("Minimum"), N_("Min"),
   N_("minimum"), 0, 10},
  {DSC_ST_MAXIMUM, DSC_MAXIMUM, N_("Maximum"), N_("Maximum"), N_("Max"),
   N_("maximum"), 0, 10},
  {DSC_ST_SUM, DSC_SUM, N_("Sum"), N_("Sum"), N_("Sum"), N_("sum"), 1, 13},
};

/* Z-score functions. */
static int generate_z_varname (struct variable * v);
static void dump_z_table (void);
static void run_z_pass (void);

/* Procedure execution functions. */
static int calc (struct ccase *, void *);
static void precalc (void *);
static void postcalc (void *);
static void display (void);

/* Parser and outline. */

int
cmd_descriptives (void)
{
  struct variable *v;
  int i;

  v_variables = NULL;
  n_variables = 0;

  lex_match_id ("DESCRIPTIVES");
  lex_match_id ("CONDESCRIPTIVES");
  if (!parse_descriptives (&cmd))
    goto lossage;

  if (n_variables == 0)
    goto lossage;
  for (i = 0; i < n_variables; i++)
    {
      v = v_variables[i];
      v->p.dsc.dup = 0;
      v->p.dsc.zname[0] = 0;
    }

  if (n_variables < 0)
    {
      msg (SE, _("No variables specified."));
      goto lossage;
    }

  if (cmd.sbc_options && (cmd.sbc_save || cmd.sbc_format || cmd.sbc_missing))
    {
      msg (SE, _("OPTIONS may not be used with SAVE, FORMAT, or MISSING."));
      goto lossage;
    }
  
  if (!cmd.sbc_options)
    {
      if (cmd.incl == DSC_INCLUDE)
	opt[op_incl_miss] = 1;
      if (cmd.labeled == DSC_NOLABELS)
	opt[op_no_varlabs] = 1;
      if (cmd.sbc_save)
	opt[op_zscores] = 1;
      if (cmd.miss == DSC_LISTWISE)
	opt[op_excl_miss] = 1;
      if (cmd.lined == DSC_SERIAL)
	opt[op_serial] = 1;
    }

  /* Construct z-score varnames, show translation table. */
  if (opt[op_zscores])
    {
      z_count = 0;
      for (i = 0; i < n_variables; i++)
	{
	  v = v_variables[i];
	  if (v->p.dsc.dup++)
	    continue;

	  if (v->p.dsc.zname[0] == 0)
	    if (!generate_z_varname (v))
	      goto lossage;
	}
      dump_z_table ();
      z_scores = 1;
    }

  /* Figure out statistics to calculate. */
  stats = 0;
  if (stat[DSC_ST_DEFAULT] || !cmd.sbc_statistics)
    stats |= dsc_default;
  if (stat[DSC_ST_ALL])
    stats |= dsc_all;
  for (i = 0; i < dsc_n_stats; i++)
    if (stat[dsc_info[i].st_indx])
      stats |= BIT_INDEX (i);
  if (stats & dsc_kurt)
    stats |= dsc_sekurt;
  if (stats & dsc_skew)
    stats |= dsc_seskew;

  /* Check the sort order. */
  sortby_stat = -1;
  if (cmd.sortby == DSC_NONE)
    sortby_stat = -2;
  else if (cmd.sortby != DSC_NAME)
    {
      for (i = 0; i < n_variables; i++)
	if (dsc_info[i].sb_indx == cmd.sortby)
	  {
	    sortby_stat = i;
	    if (!(stats & BIT_INDEX (i)))
	      {
		msg (SE, _("It's not possible to sort on `%s' without "
			   "displaying `%s'."),
		     gettext (dsc_info[i].s), gettext (dsc_info[i].s));
		goto lossage;
	      }
	  }
      assert (sortby_stat != -1);
    }

  /* Data pass! */
  bad_weight = 0;
  procedure (precalc, calc, postcalc, NULL);

  if (bad_weight)
    msg (SW, _("At least one case in the data file had a weight value "
	 "that was system-missing, zero, or negative.  These case(s) "
	 "were ignored."));

  /* Z-scoring! */
  if (z_scores)
    run_z_pass ();

  if (v_variables)
    free (v_variables);
  return CMD_SUCCESS;

 lossage:
  if (v_variables)
    free (v_variables);
  return CMD_FAILURE;
}

/* Parses the VARIABLES subcommand. */
static int
dsc_custom_variables (struct cmd_descriptives *cmd UNUSED)
{
  if (!lex_match_id ("VARIABLES")
      && (token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
      && token != T_ALL)
    return 2;
  lex_match ('=');

  while (token == T_ID || token == T_ALL)
    {
      int i, n;

      n = n_variables;
      if (!parse_variables (default_dict, &v_variables, &n_variables,
			    PV_DUPLICATE | PV_SINGLE | PV_APPEND | PV_NUMERIC
			    | PV_NO_SCRATCH))
	return 0;
      if (lex_match ('('))
	{
	  if (n_variables - n > 1)
	    {
	      msg (SE, _("Names for z-score variables must be given for "
			 "individual variables, not for groups of "
			 "variables."));
	      return 0;
	    }
	  assert (n_variables - n <= 0);
	  if (token != T_ID)
	    {
	      msg (SE, _("Name for z-score variable expected."));
	      return 0;
	    }
	  if (dict_lookup_var (default_dict, tokid))
	    {
	      msg (SE, _("Z-score variable name `%s' is a "
			 "duplicate variable name with a current variable."),
		   tokid);
	      return 0;
	    }
	  for (i = 0; i < n_variables; i++)
	    if (v_variables[i]->p.dsc.zname[0]
		&& !strcmp (v_variables[i]->p.dsc.zname, tokid))
	      {
		msg (SE, _("Z-score variable name `%s' is "
			   "used multiple times."), tokid);
		return 0;
	      }
	  strcpy (v_variables[n_variables - 1]->p.dsc.zname, tokid);
	  lex_get ();
	  if (token != ')')
	    {
	      msg (SE, _("`)' expected after z-score variable name."));
	      return 0;
	    }

	  z_scores = 1;
	}
      lex_match (',');
    }
  return 1;
}

/* Z scores. */

/* Returns 0 if NAME is a duplicate of any existing variable name or
   of any previously-declared z-var name; otherwise returns 1. */
static int
try_name (char *name)
{
  int i;

  if (dict_lookup_var (default_dict, name) != NULL)
    return 0;
  for (i = 0; i < n_variables; i++)
    {
      struct variable *v = v_variables[i];
      if (!strcmp (v->p.dsc.zname, name))
	return 0;
    }
  return 1;
}

static int
generate_z_varname (struct variable * v)
{
  char zname[10];

  strcpy (&zname[1], v->name);
  zname[0] = 'Z';
  zname[8] = '\0';
  if (try_name (zname))
    {
      strcpy (v->p.dsc.zname, zname);
      return 1;
    }

  for (;;)
    {
      /* Generate variable name. */
      z_count++;

      if (z_count <= 99)
	sprintf (zname, "ZSC%03d", z_count);
      else if (z_count <= 108)
	sprintf (zname, "STDZ%02d", z_count - 99);
      else if (z_count <= 117)
	sprintf (zname, "ZZZZ%02d", z_count - 108);
      else if (z_count <= 126)
	sprintf (zname, "ZQZQ%02d", z_count - 117);
      else
	{
	  msg (SE, _("Ran out of generic names for Z-score variables.  "
		     "There are only 126 generic names: ZSC001-ZSC0999, "
		     "STDZ01-STDZ09, ZZZZ01-ZZZZ09, ZQZQ01-ZQZQ09."));
	  return 0;
	}
      
      if (try_name (zname))
	{
	  strcpy (v->p.dsc.zname, zname);
	  return 1;
	}
    }
}

static void
dump_z_table (void)
{
  int count;
  struct tab_table *t;
  
  {
    int i;
    
    for (count = i = 0; i < n_variables; i++)
      if (v_variables[i]->p.dsc.zname)
	count++;
  }
  
  t = tab_create (2, count + 1, 0);
  tab_title (t, 0, _("Mapping of variables to corresponding Z-scores."));
  tab_columns (t, SOM_COL_DOWN, 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, TAL_0, TAL_1, 0, 0, 1, count);
  tab_hline (t, TAL_2, 0, 1, 1);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Source"));
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Target"));
  tab_dim (t, tab_natural_dimensions);

  {
    int i, y;
    
    for (i = 0, y = 1; i < n_variables; i++)
      if (v_variables[i]->p.dsc.zname)
	{
	  tab_text (t, 0, y, TAB_LEFT, v_variables[i]->name);
	  tab_text (t, 1, y++, TAB_LEFT, v_variables[i]->p.dsc.zname);
	}
  }
  
  tab_submit (t);
}

/* Transformation function to calculate Z-scores. */
static int
descriptives_trns_proc (struct trns_header * trns, struct ccase * c)
{
  struct descriptives_trns *t = (struct descriptives_trns *) trns;
  struct dsc_z_score *z;
  int i;

  for (i = t->n, z = t->z; i--; z++)
    {
      double score = c->data[z->s->fv].f;

      if (z->mean == SYSMIS || score == SYSMIS)
	c->data[z->d->fv].f = SYSMIS;
      else
	c->data[z->d->fv].f = (score - z->mean) / z->std_dev;
    }
  return -1;
}

/* Frees a descriptives_trns struct. */
static void
descriptives_trns_free (struct trns_header * trns)
{
  struct descriptives_trns *t = (struct descriptives_trns *) trns;

  free (t->z);
}

/* The name is a misnomer: actually this function sets up a
   transformation by which scores can be converted into Z-scores. */
static void
run_z_pass (void)
{
  struct descriptives_trns *t;
  int count, i;

  for (i = 0; i < n_variables; i++)
    v_variables[i]->p.dsc.dup = 0;
  for (count = i = 0; i < n_variables; i++)
    {
      if (v_variables[i]->p.dsc.dup++)
	continue;
      if (v_variables[i]->p.dsc.zname)
	count++;
    }

  t = xmalloc (sizeof *t);
  t->h.proc = descriptives_trns_proc;
  t->h.free = descriptives_trns_free;
  t->n = count;
  t->z = xmalloc (count * sizeof *t->z);

  for (i = 0; i < n_variables; i++)
    v_variables[i]->p.dsc.dup = 0;
  for (count = i = 0; i < n_variables; i++)
    {
      struct variable *v = v_variables[i];
      if (v->p.dsc.dup++ == 0 && v->p.dsc.zname[0])
	{
	  char *cp;
	  struct variable *d;

	  t->z[count].s = v;
	  t->z[count].d = d = dict_create_var_assert (default_dict,
                                                      v->p.dsc.zname, 0);
          d->init = 0;
	  if (v->label)
	    {
	      d->label = xmalloc (strlen (v->label) + 12);
	      cp = stpcpy (d->label, _("Z-score of "));
	      strcpy (cp, v->label);
	    }
	  else
	    {
	      d->label = xmalloc (strlen (v->name) + 12);
	      cp = stpcpy (d->label, _("Z-score of "));
	      strcpy (cp, v->name);
	    }
	  t->z[count].mean = v->p.dsc.stats[dsc_mean];
	  t->z[count].std_dev = v->p.dsc.stats[dsc_stddev];
	  if (t->z[count].std_dev == SYSMIS || t->z[count].std_dev == 0.0)
	    t->z[count].mean = SYSMIS;
	  count++;
	}
    }

  add_transformation ((struct trns_header *) t);
}

/* Statistical calculation. */

static void
precalc (void *aux UNUSED)
{
  int i;

  for (i = 0; i < n_variables; i++)
    v_variables[i]->p.dsc.dup = -2;
  for (i = 0; i < n_variables; i++)
    {
      struct descriptives_proc *dsc = &v_variables[i]->p.dsc;

      /* Don't need to initialize more than once. */
      if (dsc->dup == -1)
	continue;
      dsc->dup = -1;

      dsc->valid = dsc->miss = 0.0;
      dsc->X_bar = dsc->M2 = dsc->M3 = dsc->M4 = 0.0;
      dsc->min = DBL_MAX;
      dsc->max = -DBL_MAX;
    }
  d_glob_valid = d_glob_missing = 0.0;
}

static int
calc (struct ccase * c, void *aux UNUSED)
{
  int i;

  /* Unique case identifier. */
  static int case_id;

  /* Get the weight for this case. */
  double weight = dict_get_case_weight (default_dict, c);

  if (weight <= 0.0)
    {
      weight = 0.0;
      bad_weight = 1;
    }
  case_id++;

  /* Handle missing values. */
  for (i = 0; i < n_variables; i++)
    {
      struct variable *v = v_variables[i];
      double X = c->data[v->fv].f;

      if (X == SYSMIS || (!opt[op_incl_miss] && is_num_user_missing (X, v)))
	{
	  if (opt[op_excl_miss])
	    {
	      d_glob_missing += weight;
	      return 1;
	    }
	  else
	    {
	      d_glob_miss_list += weight;
	      goto iterate;
	    }
	}
    }
  d_glob_valid += weight;

iterate:
  for (i = 0; i < n_variables; i++)
    {
      struct descriptives_proc *inf = &v_variables[i]->p.dsc;

      double X, v;
      double W_old, W_new;
      double v2, v3, v4;
      double w2, w3, w4;

      if (inf->dup == case_id)
	continue;
      inf->dup = case_id;

      X = c->data[v_variables[i]->fv].f;
      if (X == SYSMIS
	  || (!opt[op_incl_miss] && is_num_user_missing (X, v_variables[i])))
	{
	  inf->miss += weight;
	  continue;
	}

      /* These formulas taken from _SPSS Statistical Algorithms_.  The
         names W_old, and W_new are used for W_j-1 and W_j,
         respectively, and other variables simply have the subscripts
         trimmed off, except for X_bar.

         I am happy that mathematical formulas may not be
         copyrighted. */
      W_old = inf->valid;
      W_new = inf->valid += weight;
      v = (weight / W_new) * (X - inf->X_bar);
      v2 = v * v;
      v3 = v2 * v;
      v4 = v3 * v;
      w2 = weight * weight;
      w3 = w2 * weight;
      w4 = w3 * weight;
      inf->M4 += (-4.0 * v * inf->M3 + 6.0 * v2 * inf->M2
	       + (W_new * W_new - 3 * weight * W_old / w3) * v4 * W_old * W_new);
      inf->M3 += (-3.0 * v * inf->M2 + W_new * W_old / w2
		  * (W_new - 2 * weight) * v3);
      inf->M2 += W_new * W_old / weight * v2;
      inf->X_bar += v;
      if (X < inf->min)
	inf->min = X;
      if (X > inf->max)
	inf->max = X;
    }
  return 1;
}

static void
postcalc (void *aux UNUSED)
{
  int i;

  if (opt[op_excl_miss])
    d_glob_miss_list = d_glob_missing;

  for (i = 0; i < n_variables; i++)
    {
      struct descriptives_proc *dsc = &v_variables[i]->p.dsc;
      double W;

      /* Don't duplicate our efforts. */
      if (dsc->dup == -2)
	continue;
      dsc->dup = -2;

      W = dsc->valid;

      dsc->stats[dsc_mean] = dsc->X_bar;
      dsc->stats[dsc_variance] = dsc->M2 / (W - 1);
      dsc->stats[dsc_stddev] = sqrt (dsc->stats[dsc_variance]);
      dsc->stats[dsc_semean] = dsc->stats[dsc_stddev] / sqrt (W);
      dsc->stats[dsc_min] = dsc->min == DBL_MAX ? SYSMIS : dsc->min;
      dsc->stats[dsc_max] = dsc->max == -DBL_MAX ? SYSMIS : dsc->max;
      dsc->stats[dsc_range] = ((dsc->min == DBL_MAX || dsc->max == -DBL_MAX)
			       ? SYSMIS : dsc->max - dsc->min);
      dsc->stats[dsc_sum] = W * dsc->X_bar;
      if (W > 2.0 && dsc->stats[dsc_variance] >= 1e-20)
	{
	  double S = dsc->stats[dsc_stddev];
	  dsc->stats[dsc_skew] = (W * dsc->M3 / ((W - 1.0) * (W - 2.0) * S * S * S));
	  dsc->stats[dsc_seskew] =
	    sqrt (6.0 * W * (W - 1.0) / ((W - 2.0) * (W + 1.0) * (W + 3.0)));
	}
      else
	{
	  dsc->stats[dsc_skew] = dsc->stats[dsc_seskew] = SYSMIS;
	}
      if (W > 3.0 && dsc->stats[dsc_variance] >= 1e-20)
	{
	  double S2 = dsc->stats[dsc_variance];
	  double SE_g1 = dsc->stats[dsc_seskew];

	  dsc->stats[dsc_kurt] =
	    (W * (W + 1.0) * dsc->M4 - 3.0 * dsc->M2 * dsc->M2 * (W - 1.0))
	    / ((W - 1.0) * (W - 2.0) * (W - 3.0) * S2 * S2);

	  /* Note that in _SPSS Statistical Algorithms_, the square
	     root symbol is missing from this formula. */
	  dsc->stats[dsc_sekurt] =
	    sqrt ((4.0 * (W * W - 1.0) * SE_g1 * SE_g1) / ((W - 3.0) * (W + 5.0)));
	}
      else
	{
	  dsc->stats[dsc_kurt] = dsc->stats[dsc_sekurt] = SYSMIS;
	}
    }

  display ();
}

/* Statistical display. */

static algo_compare_func descriptives_compare_variables;

static void
display (void)
{
  int i, j;

  int nc, n_stats;
  struct tab_table *t;

  /* If op_excl_miss is on, d_glob_valid and (potentially)
     d_glob_missing are nonzero, and d_glob_missing equals
     d_glob_miss_list.

     If op_excl_miss is off, d_glob_valid is nonzero.  d_glob_missing
     is zero.  d_glob_miss_list is (potentially) nonzero.  */

  if (sortby_stat != -2)
    sort (v_variables, n_variables, sizeof *v_variables,
	   descriptives_compare_variables, &cmd);

  for (nc = i = 0; i < dsc_n_stats; i++)
    if (stats & BIT_INDEX (i))
      nc++;
  n_stats = nc;
  if (!opt[op_no_varnames])
    nc++;
  nc += opt[op_serial] ? 2 : 1;

  t = tab_create (nc, n_variables + 1, 0);
  tab_headers (t, 1, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, -1, -1, 0, 0, nc - 1, n_variables);
  tab_box (t, -1, -1, -1, TAL_1, 1, 0, nc - 1, n_variables);
  tab_hline (t, TAL_2, 0, nc - 1, 1);
  tab_vline (t, TAL_2, 1, 0, n_variables);
  tab_dim (t, tab_natural_dimensions);

  nc = 0;
  if (!opt[op_no_varnames])
    {
      tab_text (t, nc++, 0, TAB_LEFT | TAT_TITLE, _("Variable"));
    }
  if (opt[op_serial])
    {
      tab_text (t, nc++, 0, TAB_CENTER | TAT_TITLE, _("Valid N"));
      tab_text (t, nc++, 0, TAB_CENTER | TAT_TITLE, _("Missing N"));
    } else {
      tab_text (t, nc++, 0, TAB_CENTER | TAT_TITLE, "N");
    }

  for (i = 0; i < dsc_n_stats; i++)
    if (stats & BIT_INDEX (i))
      {
	const char *title = gettext (dsc_info[i].s8);
	tab_text (t, nc++, 0, TAB_CENTER | TAT_TITLE, title);
      }

  for (i = 0; i < n_variables; i++)
    {
      struct variable *v = v_variables[i];

      nc = 0;
      if (!opt[op_no_varnames])
	tab_text (t, nc++, i + 1, TAB_LEFT, v->name);
      tab_text (t, nc++, i + 1, TAT_PRINTF, "%g", v->p.dsc.valid);
      if (opt[op_serial])
	tab_text (t, nc++, i + 1, TAT_PRINTF, "%g", v->p.dsc.miss);
      for (j = 0; j < dsc_n_stats; j++)
	if (stats & BIT_INDEX (j))
	  tab_float (t, nc++, i + 1, TAB_NONE, v->p.dsc.stats[j], 10, 3);
    }

  tab_title (t, 1, _("Valid cases = %g; cases with missing value(s) = %g."),
	     d_glob_valid, d_glob_miss_list);

  tab_submit (t);
}

/* Compares variables A and B according to the ordering specified
   by CMD. */
static int
descriptives_compare_variables (const void *a_, const void *b_, void *cmd_)
{
  struct variable *const *ap = a_;
  struct variable *const *bp = b_;
  struct variable *a = *ap;
  struct variable *b = *bp;
  struct cmd_descriptives *cmd = cmd_;

  int result;

  if (cmd->sortby == DSC_NAME)
    result = strcmp (a->name, b->name);
  else 
    {
      double as = a->p.dsc.stats[sortby_stat];
      double bs = b->p.dsc.stats[sortby_stat];

      result = as < bs ? -1 : as > bs;
    }

  if (cmd->order == DSC_D)
    result = -result;

  return result;
}

/*
   Local variables:
   mode: c
   End:
*/
