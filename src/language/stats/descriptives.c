/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-2000, 2009-2014 Free Software Foundation, Inc.

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

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/dictionary/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "math/moments.h"
#include "output/tab.h"

#include "gl/xalloc.h"

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
    const struct variable *src_var;   /* Variable on which z-score is based. */
    struct variable *z_var;     /* New z-score variable. */
    double mean;		/* Distribution mean. */
    double std_dev;		/* Distribution standard deviation. */
  };

/* DESCRIPTIVES transformation (for calculating Z-scores). */
struct dsc_trns
  {
    struct dsc_z_score *z_scores; /* Array of Z-scores. */
    int z_score_cnt;            /* Number of Z-scores. */
    const struct variable **vars;     /* Variables for listwise missing checks. */
    size_t var_cnt;             /* Number of variables. */
    enum dsc_missing_type missing_type; /* Treatment of missing values. */
    enum mv_class exclude;      /* Classes of missing values to exclude. */
    struct casereader *z_reader; /* Reader for count, mean, stddev. */
    casenumber count;            /* Number left in this SPLIT FILE group.*/
    bool ok;
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
    {"SEMEAN", N_("S.E. Mean"), MOMENT_VARIANCE},
    {"STDDEV", N_("Std Dev"), MOMENT_VARIANCE},
    {"VARIANCE", N_("Variance"), MOMENT_VARIANCE},
    {"KURTOSIS", N_("Kurtosis"), MOMENT_KURTOSIS},
    {"SEKURTOSIS", N_("S.E. Kurt"), MOMENT_NONE},
    {"SKEWNESS", N_("Skewness"), MOMENT_SKEWNESS},
    {"SESKEWNESS", N_("S.E. Skew"), MOMENT_NONE},
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
    const struct variable *v;         /* Variable to calculate on. */
    char *z_name;                     /* Name for z-score variable. */
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
    enum mv_class exclude;      /* Classes of missing values to exclude. */
    int show_var_labels;        /* Nonzero to show variable labels. */
    int show_index;             /* Nonzero to show variable index. */
    enum dsc_format format;     /* Output format. */

    /* Accumulated results. */
    double missing_listwise;    /* Sum of weights of cases missing listwise. */
    double valid;               /* Sum of weights of valid cases. */
    bool bad_warn;               /* Warn if bad weight found. */
    enum dsc_statistic sort_by_stat; /* Statistic to sort by; -1: name. */
    int sort_ascending;         /* !0: ascending order; 0: descending. */
    unsigned long show_stats;   /* Statistics to display. */
    unsigned long calc_stats;   /* Statistics to calculate. */
    enum moment max_moment;     /* Highest moment needed for stats. */

    /* Z scores. */
    struct casewriter *z_writer; /* Mean and stddev per SPLIT FILE group. */
  };

/* Parsing. */
static enum dsc_statistic match_statistic (struct lexer *);
static void free_dsc_proc (struct dsc_proc *);

/* Z-score functions. */
static bool try_name (const struct dictionary *dict,
		      struct dsc_proc *dsc, const char *name);
static char *generate_z_varname (const struct dictionary *dict,
                                 struct dsc_proc *dsc,
                                 const char *name, int *z_cnt);
static void dump_z_table (struct dsc_proc *);
static void setup_z_trns (struct dsc_proc *, struct dataset *);

/* Procedure execution functions. */
static void calc_descriptives (struct dsc_proc *, struct casereader *,
                               struct dataset *);
static void display (struct dsc_proc *dsc);

/* Parser and outline. */

/* Handles DESCRIPTIVES. */
int
cmd_descriptives (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct dsc_proc *dsc;
  const struct variable **vars = NULL;
  size_t var_cnt = 0;
  int save_z_scores = 0;
  int z_cnt = 0;
  size_t i;
  bool ok;

  struct casegrouper *grouper;
  struct casereader *group;

  /* Create and initialize dsc. */
  dsc = xmalloc (sizeof *dsc);
  dsc->vars = NULL;
  dsc->var_cnt = 0;
  dsc->missing_type = DSC_VARIABLE;
  dsc->exclude = MV_ANY;
  dsc->show_var_labels = 1;
  dsc->show_index = 0;
  dsc->format = DSC_LINE;
  dsc->missing_listwise = 0.;
  dsc->valid = 0.;
  dsc->bad_warn = 1;
  dsc->sort_by_stat = DSC_NONE;
  dsc->sort_ascending = 1;
  dsc->show_stats = dsc->calc_stats = DEFAULT_STATS;
  dsc->z_writer = NULL;

  /* Parse DESCRIPTIVES. */
  while (lex_token (lexer) != T_ENDCMD)
    {
      if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "VARIABLE"))
                dsc->missing_type = DSC_VARIABLE;
              else if (lex_match_id (lexer, "LISTWISE"))
                dsc->missing_type = DSC_LISTWISE;
              else if (lex_match_id (lexer, "INCLUDE"))
                dsc->exclude = MV_SYSTEM;
              else
                {
                  lex_error (lexer, NULL);
                  goto error;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "SAVE"))
        save_z_scores = 1;
      else if (lex_match_id (lexer, "FORMAT"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "LABELS"))
                dsc->show_var_labels = 1;
              else if (lex_match_id (lexer, "NOLABELS"))
                dsc->show_var_labels = 0;
              else if (lex_match_id (lexer, "INDEX"))
                dsc->show_index = 1;
              else if (lex_match_id (lexer, "NOINDEX"))
                dsc->show_index = 0;
              else if (lex_match_id (lexer, "LINE"))
                dsc->format = DSC_LINE;
              else if (lex_match_id (lexer, "SERIAL"))
                dsc->format = DSC_SERIAL;
              else
                {
                  lex_error (lexer, NULL);
                  goto error;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "STATISTICS"))
        {
          lex_match (lexer, T_EQUALS);
          dsc->show_stats = 0;
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match (lexer, T_ALL))
                dsc->show_stats |= (1ul << DSC_N_STATS) - 1;
              else if (lex_match_id (lexer, "DEFAULT"))
                dsc->show_stats |= DEFAULT_STATS;
              else
		dsc->show_stats |= 1ul << (match_statistic (lexer));
              lex_match (lexer, T_COMMA);
            }
          if (dsc->show_stats == 0)
            dsc->show_stats = DEFAULT_STATS;
        }
      else if (lex_match_id (lexer, "SORT"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "NAME"))
            dsc->sort_by_stat = DSC_NAME;
          else
	    {
	      dsc->sort_by_stat = match_statistic (lexer);
	      if (dsc->sort_by_stat == DSC_NONE )
		dsc->sort_by_stat = DSC_MEAN;
	    }
          if (lex_match (lexer, T_LPAREN))
            {
              if (lex_match_id (lexer, "A"))
                dsc->sort_ascending = 1;
              else if (lex_match_id (lexer, "D"))
                dsc->sort_ascending = 0;
              else
                lex_error (lexer, NULL);
              lex_force_match (lexer, T_RPAREN);
            }
        }
      else if (var_cnt == 0)
        {
          if (lex_next_token (lexer, 1) == T_EQUALS)
            {
              lex_match_id (lexer, "VARIABLES");
              lex_match (lexer, T_EQUALS);
            }

          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              int i;

              if (!parse_variables_const (lexer, dict, &vars, &var_cnt,
                                    PV_APPEND | PV_NO_DUPLICATE | PV_NUMERIC))
		goto error;

              dsc->vars = xnrealloc ((void *)dsc->vars, var_cnt, sizeof *dsc->vars);
              for (i = dsc->var_cnt; i < var_cnt; i++)
                {
                  struct dsc_var *dv = &dsc->vars[i];
                  dv->v = vars[i];
                  dv->z_name = NULL;
                  dv->moments = NULL;
                }
              dsc->var_cnt = var_cnt;

              if (lex_match (lexer, T_LPAREN))
                {
                  if (lex_token (lexer) != T_ID)
                    {
                      lex_error (lexer, NULL);
                      goto error;
                    }
                  if (try_name (dict, dsc, lex_tokcstr (lexer)))
                    {
                      struct dsc_var *dsc_var = &dsc->vars[dsc->var_cnt - 1];
                      dsc_var->z_name = xstrdup (lex_tokcstr (lexer));
                      z_cnt++;
                    }
                  else
                    msg (SE, _("Z-score variable name %s would be"
                               " a duplicate variable name."), lex_tokcstr (lexer));
                  lex_get (lexer);
                  if (!lex_force_match (lexer, T_RPAREN))
		    goto error;
                }
            }
        }
      else
        {
          lex_error (lexer, NULL);
          goto error;
        }

      lex_match (lexer, T_SLASH);
    }
  if (var_cnt == 0)
    {
      msg (SE, _("No variables specified."));
      goto error;
    }

  /* Construct z-score varnames, show translation table. */
  if (z_cnt || save_z_scores)
    {
      struct caseproto *proto;

      if (save_z_scores)
        {
          int gen_cnt = 0;

          for (i = 0; i < dsc->var_cnt; i++)
            {
              struct dsc_var *dsc_var = &dsc->vars[i];
              if (dsc_var->z_name == NULL)
                {
                  const char *name = var_get_name (dsc_var->v);
                  dsc_var->z_name = generate_z_varname (dict, dsc, name,
                                                        &gen_cnt);
                  if (dsc_var->z_name == NULL)
                    goto error;

                  z_cnt++;
                }
            }
        }

      /* It would be better to handle Z scores correctly (however we define
         that) when TEMPORARY is in effect, but in the meantime this at least
         prevents a use-after-free error.  See bug #38786.  */
      if (proc_make_temporary_transformations_permanent (ds))
        msg (SW, _("DESCRIPTIVES with Z scores ignores TEMPORARY.  "
                   "Temporary transformations will be made permanent."));

      proto = caseproto_create ();
      for (i = 0; i < 1 + 2 * z_cnt; i++)
        proto = caseproto_add_width (proto, 0);
      dsc->z_writer = autopaging_writer_create (proto);
      caseproto_unref (proto);

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
  grouper = casegrouper_create_splits (proc_open_filtering (ds, z_cnt == 0),
                                       dict);
  while (casegrouper_get_next_group (grouper, &group))
    calc_descriptives (dsc, group, ds);
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  /* Z-scoring! */
  if (ok && z_cnt)
    setup_z_trns (dsc, ds);

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
match_statistic (struct lexer *lexer)
{
  if (lex_token (lexer) == T_ID)
    {
      enum dsc_statistic stat;

      for (stat = 0; stat < DSC_N_STATS; stat++)
        if (lex_match_id (lexer, dsc_info[stat].identifier))
	  return stat;

      lex_get (lexer);
      lex_error (lexer, _("expecting statistic name: reverting to default"));
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
    {
      struct dsc_var *dsc_var = &dsc->vars[i];
      free (dsc_var->z_name);
      moments_destroy (dsc_var->moments);
    }
  casewriter_destroy (dsc->z_writer);
  free (dsc->vars);
  free (dsc);
}

/* Z scores. */

/* Returns false if NAME is a duplicate of any existing variable name or
   of any previously-declared z-var name; otherwise returns true. */
static bool
try_name (const struct dictionary *dict, struct dsc_proc *dsc,
	  const char *name)
{
  size_t i;

  if (dict_lookup_var (dict, name) != NULL)
    return false;
  for (i = 0; i < dsc->var_cnt; i++)
    {
      struct dsc_var *dsc_var = &dsc->vars[i];
      if (dsc_var->z_name != NULL && !utf8_strcasecmp (dsc_var->z_name, name))
        return false;
    }
  return true;
}

/* Generates a name for a Z-score variable based on a variable
   named VAR_NAME, given that *Z_CNT generated variable names are
   known to already exist.  If successful, returns the new name
   as a dynamically allocated string.  On failure, returns NULL. */
static char *
generate_z_varname (const struct dictionary *dict, struct dsc_proc *dsc,
                    const char *var_name, int *z_cnt)
{
  char *z_name, *trunc_name;

  /* Try a name based on the original variable name. */
  z_name = xasprintf ("Z%s", var_name);
  trunc_name = utf8_encoding_trunc (z_name, dict_get_encoding (dict),
                                    ID_MAX_LEN);
  free (z_name);
  if (try_name (dict, dsc, trunc_name))
    return trunc_name;
  free (trunc_name);

  /* Generate a synthetic name. */
  for (;;)
    {
      char name[8];

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
	  return NULL;
	}

      if (try_name (dict, dsc, name))
        return xstrdup (name);
    }
  NOT_REACHED();
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
      if (dsc->vars[i].z_name != NULL)
	cnt++;
  }

  t = tab_create (2, cnt + 1);
  tab_title (t, _("Mapping of variables to corresponding Z-scores."));
  tab_headers (t, 0, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, TAL_0, TAL_1, 0, 0, 1, cnt);
  tab_hline (t, TAL_2, 0, 1, 1);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Source"));
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Target"));

  {
    size_t i, y;

    for (i = 0, y = 1; i < dsc->var_cnt; i++)
      if (dsc->vars[i].z_name != NULL)
	{
	  tab_text (t, 0, y, TAB_LEFT, var_to_string (dsc->vars[i].v));
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
descriptives_trns_proc (void *trns_, struct ccase **c,
                        casenumber case_idx UNUSED)
{
  struct dsc_trns *t = trns_;
  struct dsc_z_score *z;
  const struct variable **vars;
  int all_sysmis = 0;

  if (t->count <= 0)
    {
      struct ccase *z_case;

      z_case = casereader_read (t->z_reader);
      if (z_case)
        {
          size_t z_idx = 0;

          t->count = case_num_idx (z_case, z_idx++);
          for (z = t->z_scores; z < t->z_scores + t->z_score_cnt; z++)
            {
              z->mean = case_num_idx (z_case, z_idx++);
              z->std_dev = case_num_idx (z_case, z_idx++);
            }
          case_unref (z_case);
        }
      else
        {
          if (t->ok)
            {
              msg (SE, _("Internal error processing Z scores"));
              t->ok = false;
            }
          for (z = t->z_scores; z < t->z_scores + t->z_score_cnt; z++)
            z->mean = z->std_dev = SYSMIS;
        }
    }
  t->count--;

  if (t->missing_type == DSC_LISTWISE)
    {
      assert(t->vars);
      for (vars = t->vars; vars < t->vars + t->var_cnt; vars++)
	{
	  double score = case_num (*c, *vars);
	  if (var_is_num_missing (*vars, score, t->exclude))
	    {
	      all_sysmis = 1;
	      break;
	    }
	}
    }

  *c = case_unshare (*c);
  for (z = t->z_scores; z < t->z_scores + t->z_score_cnt; z++)
    {
      double input = case_num (*c, z->src_var);
      double *output = &case_data_rw (*c, z->z_var)->f;

      if (z->mean == SYSMIS || z->std_dev == SYSMIS || all_sysmis
          || var_is_num_missing (z->src_var, input, t->exclude))
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
  bool ok = t->ok && !casereader_error (t->z_reader);

  free (t->z_scores);
  casereader_destroy (t->z_reader);
  assert((t->missing_type != DSC_LISTWISE) ^ (t->vars != NULL));
  free (t->vars);
  free (t);

  return ok;
}

/* Sets up a transformation to calculate Z scores. */
static void
setup_z_trns (struct dsc_proc *dsc, struct dataset *ds)
{
  struct dsc_trns *t;
  size_t cnt, i;

  for (cnt = i = 0; i < dsc->var_cnt; i++)
    if (dsc->vars[i].z_name != NULL)
      cnt++;

  t = xmalloc (sizeof *t);
  t->z_scores = xnmalloc (cnt, sizeof *t->z_scores);
  t->z_score_cnt = cnt;
  t->missing_type = dsc->missing_type;
  t->exclude = dsc->exclude;
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
  t->z_reader = casewriter_make_reader (dsc->z_writer);
  t->count = 0;
  t->ok = true;
  dsc->z_writer = NULL;

  for (cnt = i = 0; i < dsc->var_cnt; i++)
    {
      struct dsc_var *dv = &dsc->vars[i];
      if (dv->z_name != NULL)
	{
          struct dsc_z_score *z;
	  struct variable *dst_var;
          char *label;

	  dst_var = dict_create_var_assert (dataset_dict (ds), dv->z_name, 0);

          label = xasprintf (_("Z-score of %s"),var_to_string (dv->v));
          var_set_label (dst_var, label);
          free (label);

          z = &t->z_scores[cnt++];
          z->src_var = dv->v;
          z->z_var = dst_var;
	}
    }

  add_transformation (ds,
		      descriptives_trns_proc, descriptives_trns_free, t);
}

/* Statistical calculation. */

static bool listwise_missing (struct dsc_proc *dsc, const struct ccase *c);

/* Calculates and displays descriptive statistics for the cases
   in CF. */
static void
calc_descriptives (struct dsc_proc *dsc, struct casereader *group,
                   struct dataset *ds)
{
  struct casereader *pass1, *pass2;
  casenumber count;
  struct ccase *c;
  size_t z_idx;
  size_t i;

  c = casereader_peek (group, 0);
  if (c == NULL)
    {
      casereader_destroy (group);
      return;
    }
  output_split_file_values (ds, c);
  case_unref (c);

  group = casereader_create_filter_weight (group, dataset_dict (ds),
                                           NULL, NULL);

  pass1 = group;
  pass2 = dsc->max_moment <= MOMENT_MEAN ? NULL : casereader_clone (pass1);

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
  count = 0;
  for (; (c = casereader_read (pass1)) != NULL; case_unref (c))
    {
      double weight = dict_get_case_weight (dataset_dict (ds), c, NULL);

      /* Check for missing values. */
      if (listwise_missing (dsc, c))
        {
          dsc->missing_listwise += weight;
          if (dsc->missing_type == DSC_LISTWISE)
            continue;
        }
      dsc->valid += weight;

      for (i = 0; i < dsc->var_cnt; i++)
        {
          struct dsc_var *dv = &dsc->vars[i];
          double x = case_num (c, dv->v);

          if (var_is_num_missing (dv->v, x, dsc->exclude))
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

      count++;
    }
  if (!casereader_destroy (pass1))
    {
      casereader_destroy (pass2);
      return;
    }

  /* Second pass for higher-order moments. */
  if (dsc->max_moment > MOMENT_MEAN)
    {
      for (; (c = casereader_read (pass2)) != NULL; case_unref (c))
        {
          double weight = dict_get_case_weight (dataset_dict (ds), c, NULL);

          /* Check for missing values. */
          if (dsc->missing_type == DSC_LISTWISE && listwise_missing (dsc, c))
            continue;

          for (i = 0; i < dsc->var_cnt; i++)
            {
              struct dsc_var *dv = &dsc->vars[i];
              double x = case_num (c, dv->v);

              if (var_is_num_missing (dv->v, x, dsc->exclude))
                continue;

              if (dv->moments != NULL)
                moments_pass_two (dv->moments, x, weight);
            }
        }
      if (!casereader_destroy (pass2))
        return;
    }

  /* Calculate results. */
  if (dsc->z_writer)
    {
      c = case_create (casewriter_get_proto (dsc->z_writer));
      z_idx = 0;
      case_data_rw_idx (c, z_idx++)->f = count;
    }
  else
    c = NULL;

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

      if (dv->z_name)
        {
          case_data_rw_idx (c, z_idx++)->f = dv->stats[DSC_MEAN];
          case_data_rw_idx (c, z_idx++)->f = dv->stats[DSC_STDDEV];
        }
    }

  if (c != NULL)
    casewriter_write (dsc->z_writer, c);

  /* Output results. */
  display (dsc);
}

/* Returns true if any of the descriptives variables in DSC's
   variable list have missing values in case C, false otherwise. */
static bool
listwise_missing (struct dsc_proc *dsc, const struct ccase *c)
{
  size_t i;

  for (i = 0; i < dsc->var_cnt; i++)
    {
      struct dsc_var *dv = &dsc->vars[i];
      double x = case_num (c, dv->v);

      if (var_is_num_missing (dv->v, x, dsc->exclude))
        return true;
    }
  return false;
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

  t = tab_create (nc, dsc->var_cnt + 1);
  tab_headers (t, 1, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, -1, -1, 0, 0, nc - 1, dsc->var_cnt);
  tab_box (t, -1, -1, -1, TAL_1, 1, 0, nc - 1, dsc->var_cnt);
  tab_hline (t, TAL_2, 0, nc - 1, 1);
  tab_vline (t, TAL_2, 1, 0, dsc->var_cnt);

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
      tab_text (t, nc++, i + 1, TAB_LEFT, var_to_string (dv->v));
      tab_text_format (t, nc++, i + 1, 0, "%.*g", DBL_DIG + 1, dv->valid);
      if (dsc->format == DSC_SERIAL)
	tab_text_format (t, nc++, i + 1, 0, "%.*g", DBL_DIG + 1, dv->missing);

      for (j = 0; j < DSC_N_STATS; j++)
	if (dsc->show_stats & (1ul << j))
	  tab_double (t, nc++, i + 1, TAB_NONE, dv->stats[j], NULL);
    }

  tab_title (t, _("Valid cases = %.*g; cases with missing value(s) = %.*g."),
	     DBL_DIG + 1, dsc->valid,
             DBL_DIG + 1, dsc->missing_listwise);

  tab_submit (t);
}

/* Compares `struct dsc_var's A and B according to the ordering
   specified by CMD. */
static int
descriptives_compare_dsc_vars (const void *a_, const void *b_, const void *dsc_)
{
  const struct dsc_var *a = a_;
  const struct dsc_var *b = b_;
  const struct dsc_proc *dsc = dsc_;

  int result;

  if (dsc->sort_by_stat == DSC_NAME)
    result = utf8_strcasecmp (var_get_name (a->v), var_get_name (b->v));
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
