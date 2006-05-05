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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

/* FIXME: Many possible optimizations. */

#include <config.h>

#include <limits.h>
#include <math.h>
#include <stdlib.h>

#include <data/case.h>
#include <data/casefile.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/transformations.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/array.h>
#include <libpspp/compiler.h>
#include <libpspp/magic.h>
#include <libpspp/message.h>
#include <math/moments.h>
#include <output/manager.h>
#include <output/table.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* DESCRIPTIVES private data. */

struct dsc_proc;

/* Handling of missing values. */
enum dsc_missing_type
  {
    DSC_VARIABLE,       /* Handle missing values on a per-variable basis. */
    DSC_LISTWISE        /* Discard entire case if any variable is missing. */
  };

/* Describes properties of a distribution for the purpose of
   calculating a Z-score. */
struct dsc_z_score
  {
    int src_idx;                /* Source index into case data. */
    int dst_idx;                /* Destination index into case data. */
    double mean;		/* Distribution mean. */
    double std_dev;		/* Distribution standard deviation. */
    struct variable *v;         /* Variable on which z-score is based. */
  };

/* DESCRIPTIVES transformation (for calculating Z-scores). */
struct dsc_trns
  {
    struct dsc_z_score *z_scores; /* Array of Z-scores. */
    int z_score_cnt;            /* Number of Z-scores. */
    struct variable **vars;     /* Variables for listwise missing checks. */
    size_t var_cnt;             /* Number of variables. */
    enum dsc_missing_type missing_type; /* Treatment of missing values. */
    int include_user_missing;   /* Nonzero to include user-missing values. */
  };

/* Statistics.  Used as bit indexes, so must be 32 or fewer. */
enum dsc_statistic
  {
    DSC_MEAN = 0, DSC_SEMEAN, DSC_STDDEV, DSC_VARIANCE, DSC_KURTOSIS,
    DSC_SEKURT, DSC_SKEWNESS, DSC_SESKEW, DSC_RANGE, DSC_MIN,
    DSC_MAX, DSC_SUM, DSC_N_STATS,

    /* Only valid as sort criteria. */
    DSC_NAME = -2,              /* Sort by name. */
    DSC_NONE = -1               /* Unsorted. */
  };

/* Describes one statistic. */
struct dsc_statistic_info
  {
    const char *identifier;     /* Identifier. */
    const char *name;		/* Full name. */
    enum moment moment;		/* Highest moment needed to calculate. */
  };

/* Table of statistics, indexed by DSC_*. */
static const struct dsc_statistic_info dsc_info[DSC_N_STATS] =
  {
    {"MEAN", N_("Mean"), MOMENT_MEAN},
    {"SEMEAN", N_("S E Mean"), MOMENT_VARIANCE},
    {"STDDEV", N_("Std Dev"), MOMENT_VARIANCE},
    {"VARIANCE", N_("Variance"), MOMENT_VARIANCE},
    {"KURTOSIS", N_("Kurtosis"), MOMENT_KURTOSIS},
    {"SEKURTOSIS", N_("S E Kurt"), MOMENT_NONE},
    {"SKEWNESS", N_("Skewness"), MOMENT_SKEWNESS},
    {"SESKEWNESS", N_("S E Skew"), MOMENT_NONE},
    {"RANGE", N_("Range"), MOMENT_NONE},
    {"MINIMUM", N_("Minimum"), MOMENT_NONE},
    {"MAXIMUM", N_("Maximum"), MOMENT_NONE},
    {"SUM", N_("Sum"), MOMENT_MEAN},
  };

/* Statistics calculated by default if none are explicitly
   requested. */
#define DEFAULT_STATS                                                   \
	((1ul << DSC_MEAN) | (1ul << DSC_STDDEV) | (1ul << DSC_MIN)     \
         | (1ul << DSC_MAX))
     
/* A variable specified on DESCRIPTIVES. */
struct dsc_var
  {
    struct variable *v;         /* Variable to calculate on. */
    char z_name[LONG_NAME_LEN + 1]; /* Name for z-score variable. */
    double valid, missing;	/* Valid, missing counts. */
    struct moments *moments;    /* Moments. */
    double min, max;            /* Maximum and mimimum values. */
    double stats[DSC_N_STATS];	/* All the stats' values. */
  };

/* Output format. */
enum dsc_format 
  {
    DSC_LINE,           /* Abbreviated format. */
    DSC_SERIAL          /* Long format. */
  };

/* A DESCRIPTIVES procedure. */
struct dsc_proc 
  {
    /* Per-variable info. */
    struct dsc_var *vars;       /* Variables. */
    size_t var_cnt;             /* Number of variables. */

    /* User options. */
    enum dsc_missing_type missing_type; /* Treatment of missing values. */
    int include_user_missing;   /* Nonzero to include user-missing values. */
    int show_var_labels;        /* Nonzero to show variable labels. */
    int show_index;             /* Nonzero to show variable index. */
    enum dsc_format format;     /* Output format. */

    /* Accumulated results. */
    double missing_listwise;    /* Sum of weights of cases missing listwise. */
    double valid;               /* Sum of weights of valid cases. */
    int bad_warn;               /* Warn if bad weight found. */
    enum dsc_statistic sort_by_stat; /* Statistic to sort by; -1: name. */
    int sort_ascending;         /* !0: ascending order; 0: descending. */
    unsigned long show_stats;   /* Statistics to display. */
    unsigned long calc_stats;   /* Statistics to calculate. */
    enum moment max_moment;     /* Highest moment needed for stats. */
  };

/* Parsing. */
static enum dsc_statistic match_statistic (void);
static void free_dsc_proc (struct dsc_proc *);

/* Z-score functions. */
static int try_name (struct dsc_proc *dsc, char *name);
static int generate_z_varname (struct dsc_proc *dsc, char *z_name,
                               const char *name, size_t *z_cnt);
static void dump_z_table (struct dsc_proc *);
static void setup_z_trns (struct dsc_proc *);

/* Procedure execution functions. */
static bool calc_descriptives (const struct casefile *, void *dsc_);
static void display (struct dsc_proc *dsc);

/* Parser and outline. */

/* Handles DESCRIPTIVES. */
int
cmd_descriptives (void)
{
  struct dsc_proc *dsc;
  struct variable **vars = NULL;
  size_t var_cnt = 0;
  int save_z_scores = 0;
  size_t z_cnt = 0;
  size_t i;
  bool ok;

  /* Create and initialize dsc. */
  dsc = xmalloc (sizeof *dsc);
  dsc->vars = NULL;
  dsc->var_cnt = 0;
  dsc->missing_type = DSC_VARIABLE;
  dsc->include_user_missing = 0;
  dsc->show_var_labels = 1;
  dsc->show_index = 0;
  dsc->format = DSC_LINE;
  dsc->missing_listwise = 0.;
  dsc->valid = 0.;
  dsc->bad_warn = 1;
  dsc->sort_by_stat = DSC_NONE;
  dsc->sort_ascending = 1;
  dsc->show_stats = dsc->calc_stats = DEFAULT_STATS;

  /* Parse DESCRIPTIVES. */
  while (token != '.') 
    {
      if (lex_match_id ("MISSING"))
        {
          lex_match ('=');
          while (token != '.' && token != '/') 
            {
              if (lex_match_id ("VARIABLE"))
                dsc->missing_type = DSC_VARIABLE;
              else if (lex_match_id ("LISTWISE"))
                dsc->missing_type = DSC_LISTWISE;
              else if (lex_match_id ("INCLUDE"))
                dsc->include_user_missing = 1;
              else
                {
                  lex_error (NULL);
                  goto error;
                }
              lex_match (',');
            }
        }
      else if (lex_match_id ("SAVE"))
        save_z_scores = 1;
      else if (lex_match_id ("FORMAT")) 
        {
          lex_match ('=');
          while (token != '.' && token != '/') 
            {
              if (lex_match_id ("LABELS"))
                dsc->show_var_labels = 1;
              else if (lex_match_id ("NOLABELS"))
                dsc->show_var_labels = 0;
              else if (lex_match_id ("INDEX"))
                dsc->show_index = 1;
              else if (lex_match_id ("NOINDEX"))
                dsc->show_index = 0;
              else if (lex_match_id ("LINE"))
                dsc->format = DSC_LINE;
              else if (lex_match_id ("SERIAL"))
                dsc->format = DSC_SERIAL;
              else
                {
                  lex_error (NULL);
                  goto error;
                }
              lex_match (',');
            }
        }
      else if (lex_match_id ("STATISTICS")) 
        {
          lex_match ('=');
          dsc->show_stats = 0;
          while (token != '.' && token != '/') 
            {
              if (lex_match (T_ALL)) 
                dsc->show_stats |= (1ul << DSC_N_STATS) - 1;
              else if (lex_match_id ("DEFAULT"))
                dsc->show_stats |= DEFAULT_STATS;
              else
		dsc->show_stats |= 1ul << (match_statistic ());
              lex_match (',');
            }
          if (dsc->show_stats == 0)
            dsc->show_stats = DEFAULT_STATS;
        }
      else if (lex_match_id ("SORT")) 
        {
          lex_match ('=');
          if (lex_match_id ("NAME"))
            dsc->sort_by_stat = DSC_NAME;
          else 
	    {
	      dsc->sort_by_stat = match_statistic ();
	      if (dsc->sort_by_stat == DSC_NONE )
		dsc->sort_by_stat = DSC_MEAN;
	    }
          if (lex_match ('(')) 
            {
              if (lex_match_id ("A"))
                dsc->sort_ascending = 1;
              else if (lex_match_id ("D"))
                dsc->sort_ascending = 0;
              else
                lex_error (NULL);
              lex_force_match (')');
            }
        }
      else if (var_cnt == 0)
        {
          if (lex_look_ahead () == '=') 
            {
              lex_match_id ("VARIABLES");
              lex_match ('=');
            }

          while (token != '.' && token != '/') 
            {
              int i;
              
              if (!parse_variables (default_dict, &vars, &var_cnt,
                                    PV_APPEND | PV_NO_DUPLICATE | PV_NUMERIC))
		goto error;

              dsc->vars = xnrealloc (dsc->vars, var_cnt, sizeof *dsc->vars);
              for (i = dsc->var_cnt; i < var_cnt; i++)
                {
                  struct dsc_var *dv = &dsc->vars[i];
                  dv->v = vars[i];
                  dv->z_name[0] = '\0';
                  dv->moments = NULL;
                }
              dsc->var_cnt = var_cnt;

              if (lex_match ('(')) 
                {
                  if (token != T_ID) 
                    {
                      lex_error (NULL);
                      goto error;
                    }
                  if (try_name (dsc, tokid)) 
                    {
                      strcpy (dsc->vars[dsc->var_cnt - 1].z_name, tokid);
                      z_cnt++;
                    }
                  else
                    msg (SE, _("Z-score variable name %s would be"
                               " a duplicate variable name."), tokid);
                  lex_get ();
                  if (!lex_force_match (')'))
		    goto error;
                }
            }
        }
      else 
        {
          lex_error (NULL);
          goto error; 
        }

      lex_match ('/');
    }
  if (var_cnt == 0)
    {
      msg (SE, _("No variables specified."));
      goto error;
    }

  /* Construct z-score varnames, show translation table. */
  if (z_cnt || save_z_scores)
    {
      if (save_z_scores) 
        {
          size_t gen_cnt = 0;

          for (i = 0; i < dsc->var_cnt; i++)
            if (dsc->vars[i].z_name[0] == 0) 
              {
                if (!generate_z_varname (dsc, dsc->vars[i].z_name,
                                         dsc->vars[i].v->name, &gen_cnt))
                  goto error;
                z_cnt++;
              } 
        }
      dump_z_table (dsc);
    }

  /* Figure out statistics to display. */
  if (dsc->show_stats & (1ul << DSC_SKEWNESS))
    dsc->show_stats |= 1ul << DSC_SESKEW;
  if (dsc->show_stats & (1ul << DSC_KURTOSIS))
    dsc->show_stats |= 1ul << DSC_SEKURT;

  /* Figure out which statistics to calculate. */
  dsc->calc_stats = dsc->show_stats;
  if (z_cnt > 0)
    dsc->calc_stats |= (1ul << DSC_MEAN) | (1ul << DSC_STDDEV);
  if (dsc->sort_by_stat >= 0)
    dsc->calc_stats |= 1ul << dsc->sort_by_stat;
  if (dsc->show_stats & (1ul << DSC_SESKEW))
    dsc->calc_stats |= 1ul << DSC_SKEWNESS;
  if (dsc->show_stats & (1ul << DSC_SEKURT))
    dsc->calc_stats |= 1ul << DSC_KURTOSIS;

  /* Figure out maximum moment needed and allocate moments for
     the variables. */
  dsc->max_moment = MOMENT_NONE;
  for (i = 0; i < DSC_N_STATS; i++) 
    if (dsc->calc_stats & (1ul << i) && dsc_info[i].moment > dsc->max_moment)
      dsc->max_moment = dsc_info[i].moment;
  if (dsc->max_moment != MOMENT_NONE)
    for (i = 0; i < dsc->var_cnt; i++)
      dsc->vars[i].moments = moments_create (dsc->max_moment);

  /* Data pass. */
  ok = multipass_procedure_with_splits (calc_descriptives, dsc);

  /* Z-scoring! */
  if (ok && z_cnt)
    setup_z_trns (dsc);

  /* Done. */
  free (vars);
  free_dsc_proc (dsc);
  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

 error:
  free (vars);
  free_dsc_proc (dsc);
  return CMD_FAILURE;
}

/* Returns the statistic named by the current token and skips past the token.
   Returns DSC_NONE if no statistic is given (e.g., subcommand with no
   specifiers). Emits an error if the current token ID does not name a
   statistic. */
static enum dsc_statistic
match_statistic (void) 
{
  if (token == T_ID) 
    {
      enum dsc_statistic stat;

      for (stat = 0; stat < DSC_N_STATS; stat++)
        if (lex_match_id (dsc_info[stat].identifier)) 
	  return stat;

      lex_get();
      lex_error (_("expecting statistic name: reverting to default"));
    }

  return DSC_NONE;
}

/* Frees DSC. */
static void
free_dsc_proc (struct dsc_proc *dsc)
{
  size_t i;

  if (dsc == NULL)
    return;
  
  for (i = 0; i < dsc->var_cnt; i++)
    moments_destroy (dsc->vars[i].moments);
  free (dsc->vars);
  free (dsc);
}

/* Z scores. */

/* Returns 0 if NAME is a duplicate of any existing variable name or
   of any previously-declared z-var name; otherwise returns 1. */
static int
try_name (struct dsc_proc *dsc, char *name)
{
  size_t i;

  if (dict_lookup_var (default_dict, name) != NULL)
    return 0;
  for (i = 0; i < dsc->var_cnt; i++)
    if (!strcasecmp (dsc->vars[i].z_name, name))
      return 0;
  return 1;
}

/* Generates a name for a Z-score variable based on a variable
   named VAR_NAME, given that *Z_CNT generated variable names are
   known to already exist.  If successful, returns nonzero and
   copies the new name into Z_NAME.  On failure, returns zero. */
static int
generate_z_varname (struct dsc_proc *dsc, char *z_name,
                    const char *var_name, size_t *z_cnt)
{
  char name[LONG_NAME_LEN + 1];

  /* Try a name based on the original variable name. */
  name[0] = 'Z';
  str_copy_trunc (name + 1, sizeof name - 1, var_name);
  if (try_name (dsc, name))
    {
      strcpy (z_name, name);
      return 1;
    }

  /* Generate a synthetic name. */
  for (;;)
    {
      (*z_cnt)++;

      if (*z_cnt <= 99)
	sprintf (name, "ZSC%03d", *z_cnt);
      else if (*z_cnt <= 108)
	sprintf (name, "STDZ%02d", *z_cnt - 99);
      else if (*z_cnt <= 117)
	sprintf (name, "ZZZZ%02d", *z_cnt - 108);
      else if (*z_cnt <= 126)
	sprintf (name, "ZQZQ%02d", *z_cnt - 117);
      else
	{
	  msg (SE, _("Ran out of generic names for Z-score variables.  "
		     "There are only 126 generic names: ZSC001-ZSC0999, "
		     "STDZ01-STDZ09, ZZZZ01-ZZZZ09, ZQZQ01-ZQZQ09."));
	  return 0;
	}
      
      if (try_name (dsc, name))
	{
	  strcpy (z_name, name);
	  return 1;
	}
    }
}

/* Outputs a table describing the mapping between source
   variables and Z-score variables. */
static void
dump_z_table (struct dsc_proc *dsc)
{
  size_t cnt = 0;
  struct tab_table *t;
  
  {
    size_t i;
    
    for (i = 0; i < dsc->var_cnt; i++)
      if (dsc->vars[i].z_name[0] != '\0')
	cnt++;
  }
  
  t = tab_create (2, cnt + 1, 0);
  tab_title (t, _("Mapping of variables to corresponding Z-scores."));
  tab_columns (t, SOM_COL_DOWN, 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, TAL_0, TAL_1, 0, 0, 1, cnt);
  tab_hline (t, TAL_2, 0, 1, 1);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Source"));
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Target"));
  tab_dim (t, tab_natural_dimensions);

  {
    size_t i, y;
    
    for (i = 0, y = 1; i < dsc->var_cnt; i++)
      if (dsc->vars[i].z_name[0] != '\0')
	{
	  tab_text (t, 0, y, TAB_LEFT, dsc->vars[i].v->name);
	  tab_text (t, 1, y++, TAB_LEFT, dsc->vars[i].z_name);
	}
  }
  
  tab_submit (t);
}

/* Transformation function to calculate Z-scores. Will return SYSMIS if any of
   the following are true: 1) mean or standard deviation is SYSMIS 2) score is
   SYSMIS 3) score is user missing and they were not included in the original
   analyis. 4) any of the variables in the original analysis were missing
   (either system or user-missing values that weren't included).
*/
static int
descriptives_trns_proc (void *trns_, struct ccase * c,
                        int case_idx UNUSED)
{
  struct dsc_trns *t = trns_;
  struct dsc_z_score *z;
  struct variable **vars;
  int all_sysmis = 0;

  if (t->missing_type == DSC_LISTWISE)
    {
      assert(t->vars);
      for (vars = t->vars; vars < t->vars + t->var_cnt; vars++)
	{
	  double score = case_num (c, (*vars)->fv);
	  if ( score == SYSMIS
               || (!t->include_user_missing 
                   && mv_is_num_user_missing (&(*vars)->miss, score)))
	    {
	      all_sysmis = 1;
	      break;
	    }
	}
    }
      
  for (z = t->z_scores; z < t->z_scores + t->z_score_cnt; z++)
    {
      double input = case_num (c, z->src_idx);
      double *output = &case_data_rw (c, z->dst_idx)->f;

      if (z->mean == SYSMIS || z->std_dev == SYSMIS 
	  || all_sysmis || input == SYSMIS 
	  || (!t->include_user_missing
              && mv_is_num_user_missing (&z->v->miss, input)))
	*output = SYSMIS;
      else
	*output = (input - z->mean) / z->std_dev;
    }
  return TRNS_CONTINUE;
}

/* Frees a descriptives_trns struct. */
static bool
descriptives_trns_free (void *trns_)
{
  struct dsc_trns *t = trns_;

  free (t->z_scores);
  assert((t->missing_type != DSC_LISTWISE) ^ (t->vars != NULL));
  free (t->vars);
  return true;
}

/* Sets up a transformation to calculate Z scores. */
static void
setup_z_trns (struct dsc_proc *dsc)
{
  struct dsc_trns *t;
  size_t cnt, i;

  for (cnt = i = 0; i < dsc->var_cnt; i++)
    if (dsc->vars[i].z_name[0] != '\0')
      cnt++;

  t = xmalloc (sizeof *t);
  t->z_scores = xnmalloc (cnt, sizeof *t->z_scores);
  t->z_score_cnt = cnt;
  t->missing_type = dsc->missing_type;
  t->include_user_missing = dsc->include_user_missing;
  if ( t->missing_type == DSC_LISTWISE )
    {
      t->var_cnt = dsc->var_cnt;
      t->vars = xnmalloc (t->var_cnt, sizeof *t->vars);
      for (i = 0; i < t->var_cnt; i++)
	t->vars[i] = dsc->vars[i].v;
    }
  else
    {
      t->var_cnt = 0;
      t->vars = NULL;
    }

  for (cnt = i = 0; i < dsc->var_cnt; i++)
    {
      struct dsc_var *dv = &dsc->vars[i];
      if (dv->z_name[0] != '\0')
	{
          struct dsc_z_score *z;
	  char *cp;
	  struct variable *dst_var;

	  dst_var = dict_create_var_assert (default_dict, dv->z_name, 0);
	  if (dv->v->label)
	    {
	      dst_var->label = xmalloc (strlen (dv->v->label) + 12);
	      cp = stpcpy (dst_var->label, _("Z-score of "));
	      strcpy (cp, dv->v->label);
	    }
	  else
	    {
	      dst_var->label = xmalloc (strlen (dv->v->name) + 12);
	      cp = stpcpy (dst_var->label, _("Z-score of "));
	      strcpy (cp, dv->v->name);
	    }

          z = &t->z_scores[cnt++];
          z->src_idx = dv->v->fv;
          z->dst_idx = dst_var->fv;
          z->mean = dv->stats[DSC_MEAN];
          z->std_dev = dv->stats[DSC_STDDEV];
	  z->v = dv->v;
	}
    }

  add_transformation (descriptives_trns_proc, descriptives_trns_free, t);
}

/* Statistical calculation. */

static int listwise_missing (struct dsc_proc *dsc, const struct ccase *c);

/* Calculates and displays descriptive statistics for the cases
   in CF. */
static bool
calc_descriptives (const struct casefile *cf, void *dsc_) 
{
  struct dsc_proc *dsc = dsc_;
  struct casereader *reader;
  struct ccase c;
  size_t i;

  for (i = 0; i < dsc->var_cnt; i++)
    {
      struct dsc_var *dv = &dsc->vars[i];
      
      dv->valid = dv->missing = 0.0;
      if (dv->moments != NULL)
        moments_clear (dv->moments);
      dv->min = DBL_MAX;
      dv->max = -DBL_MAX;
    }
  dsc->missing_listwise = 0.;
  dsc->valid = 0.;

  /* First pass to handle most of the work. */
  for (reader = casefile_get_reader (cf);
       casereader_read (reader, &c);
       case_destroy (&c))
    {
      double weight = dict_get_case_weight (default_dict, &c, &dsc->bad_warn);
      if (weight <= 0.0) 
        continue;
       
      /* Check for missing values. */
      if (listwise_missing (dsc, &c)) 
        {
          dsc->missing_listwise += weight;
          if (dsc->missing_type == DSC_LISTWISE)
            continue; 
        }
      dsc->valid += weight;

      for (i = 0; i < dsc->var_cnt; i++) 
        {
          struct dsc_var *dv = &dsc->vars[i];
          double x = case_num (&c, dv->v->fv);
          
          if (dsc->missing_type != DSC_LISTWISE
              && (x == SYSMIS
                  || (!dsc->include_user_missing
                      && mv_is_num_user_missing (&dv->v->miss, x))))
            {
              dv->missing += weight;
              continue;
            }

          if (dv->moments != NULL) 
            moments_pass_one (dv->moments, x, weight);

          if (x < dv->min)
            dv->min = x;
          if (x > dv->max)
            dv->max = x;
        }
    }
  casereader_destroy (reader);

  /* Second pass for higher-order moments. */
  if (dsc->max_moment > MOMENT_MEAN) 
    {
      for (reader = casefile_get_reader (cf);
           casereader_read (reader, &c);
           case_destroy (&c))
        {
          double weight = dict_get_case_weight (default_dict, &c, 
						&dsc->bad_warn);
          if (weight <= 0.0)
            continue;
      
          /* Check for missing values. */
          if (listwise_missing (dsc, &c) 
              && dsc->missing_type == DSC_LISTWISE)
            continue; 

          for (i = 0; i < dsc->var_cnt; i++) 
            {
              struct dsc_var *dv = &dsc->vars[i];
              double x = case_num (&c, dv->v->fv);
          
              if (dsc->missing_type != DSC_LISTWISE
                  && (x == SYSMIS
                      || (!dsc->include_user_missing
                          && mv_is_num_user_missing (&dv->v->miss, x))))
                continue;

              if (dv->moments != NULL)
                moments_pass_two (dv->moments, x, weight);
            }
        }
      casereader_destroy (reader);
    }
  
  /* Calculate results. */
  for (i = 0; i < dsc->var_cnt; i++)
    {
      struct dsc_var *dv = &dsc->vars[i];
      double W;
      int j;

      for (j = 0; j < DSC_N_STATS; j++)
        dv->stats[j] = SYSMIS;

      dv->valid = W = dsc->valid - dv->missing;

      if (dv->moments != NULL)
        moments_calculate (dv->moments, NULL,
                           &dv->stats[DSC_MEAN], &dv->stats[DSC_VARIANCE],
                           &dv->stats[DSC_SKEWNESS], &dv->stats[DSC_KURTOSIS]);
      if (dsc->calc_stats & (1ul << DSC_SEMEAN)
          && dv->stats[DSC_VARIANCE] != SYSMIS && W > 0.)
        dv->stats[DSC_SEMEAN] = sqrt (dv->stats[DSC_VARIANCE]) / sqrt (W);
      if (dsc->calc_stats & (1ul << DSC_STDDEV)
          && dv->stats[DSC_VARIANCE] != SYSMIS)
        dv->stats[DSC_STDDEV] = sqrt (dv->stats[DSC_VARIANCE]);
      if (dsc->calc_stats & (1ul << DSC_SEKURT)) 
        if (dv->stats[DSC_KURTOSIS] != SYSMIS)
            dv->stats[DSC_SEKURT] = calc_sekurt (W);
      if (dsc->calc_stats & (1ul << DSC_SESKEW)
          && dv->stats[DSC_SKEWNESS] != SYSMIS)
        dv->stats[DSC_SESKEW] = calc_seskew (W);
      dv->stats[DSC_RANGE] = ((dv->min == DBL_MAX || dv->max == -DBL_MAX)
                              ? SYSMIS : dv->max - dv->min);
      dv->stats[DSC_MIN] = dv->min == DBL_MAX ? SYSMIS : dv->min;
      dv->stats[DSC_MAX] = dv->max == -DBL_MAX ? SYSMIS : dv->max;
      if (dsc->calc_stats & (1ul << DSC_SUM))
        dv->stats[DSC_SUM] = W * dv->stats[DSC_MEAN];
    }

  /* Output results. */
  display (dsc);

  return true;
}

/* Returns nonzero if any of the descriptives variables in DSC's
   variable list have missing values in case C, zero otherwise. */
static int
listwise_missing (struct dsc_proc *dsc, const struct ccase *c) 
{
  size_t i;

  for (i = 0; i < dsc->var_cnt; i++)
    {
      struct dsc_var *dv = &dsc->vars[i];
      double x = case_num (c, dv->v->fv);

      if (x == SYSMIS
          || (!dsc->include_user_missing
              && mv_is_num_user_missing (&dv->v->miss, x)))
        return 1;
    }
  return 0;
}

/* Statistical display. */

static algo_compare_func descriptives_compare_dsc_vars;

/* Displays a table of descriptive statistics for DSC. */
static void
display (struct dsc_proc *dsc)
{
  size_t i;
  int nc;
  struct tab_table *t;

  nc = 1 + (dsc->format == DSC_SERIAL ? 2 : 1);
  for (i = 0; i < DSC_N_STATS; i++)
    if (dsc->show_stats & (1ul << i))
      nc++;

  if (dsc->sort_by_stat != DSC_NONE)
    sort (dsc->vars, dsc->var_cnt, sizeof *dsc->vars,
          descriptives_compare_dsc_vars, dsc);

  t = tab_create (nc, dsc->var_cnt + 1, 0);
  tab_headers (t, 1, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, -1, -1, 0, 0, nc - 1, dsc->var_cnt);
  tab_box (t, -1, -1, -1, TAL_1, 1, 0, nc - 1, dsc->var_cnt);
  tab_hline (t, TAL_2, 0, nc - 1, 1);
  tab_vline (t, TAL_2, 1, 0, dsc->var_cnt);
  tab_dim (t, tab_natural_dimensions);

  nc = 0;
  tab_text (t, nc++, 0, TAB_LEFT | TAT_TITLE, _("Variable"));
  if (dsc->format == DSC_SERIAL)
    {
      tab_text (t, nc++, 0, TAB_CENTER | TAT_TITLE, _("Valid N"));
      tab_text (t, nc++, 0, TAB_CENTER | TAT_TITLE, _("Missing N"));
    }
  else
    tab_text (t, nc++, 0, TAB_CENTER | TAT_TITLE, "N");

  for (i = 0; i < DSC_N_STATS; i++)
    if (dsc->show_stats & (1ul << i))
      {
	const char *title = gettext (dsc_info[i].name);
	tab_text (t, nc++, 0, TAB_CENTER | TAT_TITLE, title);
      }

  for (i = 0; i < dsc->var_cnt; i++)
    {
      struct dsc_var *dv = &dsc->vars[i];
      size_t j;

      nc = 0;
      tab_text (t, nc++, i + 1, TAB_LEFT, dv->v->name);
      tab_text (t, nc++, i + 1, TAT_PRINTF, "%g", dv->valid);
      if (dsc->format == DSC_SERIAL)
	tab_text (t, nc++, i + 1, TAT_PRINTF, "%g", dv->missing);
      for (j = 0; j < DSC_N_STATS; j++)
	if (dsc->show_stats & (1ul << j))
	  tab_float (t, nc++, i + 1, TAB_NONE, dv->stats[j], 10, 3);
    }

  tab_title (t, _("Valid cases = %g; cases with missing value(s) = %g."),
	     dsc->valid, dsc->missing_listwise);

  tab_submit (t);
}

/* Compares `struct dsc_var's A and B according to the ordering
   specified by CMD. */
static int
descriptives_compare_dsc_vars (const void *a_, const void *b_, void *dsc_)
{
  const struct dsc_var *a = a_;
  const struct dsc_var *b = b_;
  struct dsc_proc *dsc = dsc_;

  int result;

  if (dsc->sort_by_stat == DSC_NAME)
    result = strcasecmp (a->v->name, b->v->name);
  else 
    {
      double as = a->stats[dsc->sort_by_stat];
      double bs = b->stats[dsc->sort_by_stat];

      result = as < bs ? -1 : as > bs;
    }

  if (!dsc->sort_ascending)
    result = -result;

  return result;
}
