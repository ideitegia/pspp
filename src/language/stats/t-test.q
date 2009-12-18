/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009 Free Software Foundation, Inc.

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

#include <gsl/gsl_cdf.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <data/case.h>
#include <data/casegrouper.h>
#include <data/casereader.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/dictionary/split-file.h>
#include <language/lexer/lexer.h>
#include <language/lexer/value-parser.h>
#include <libpspp/array.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include <libpspp/taint.h>
#include <math/group-proc.h>
#include <math/levene.h>
#include <math/correlation.h>
#include <output/manager.h>
#include <output/table.h>
#include <data/format.h>

#include "minmax.h"
#include "xalloc.h"
#include "xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* (headers) */

/* (specification)
   "T-TEST" (tts_):
   +groups=custom;
   testval=double;
   +variables=varlist("PV_NO_SCRATCH | PV_NUMERIC");
   +pairs=custom;
   missing=miss:!analysis/listwise,
   incl:include/!exclude;
   +format=fmt:!labels/nolabels;
   criteria=:cin(d:criteria,"%s > 0. && %s < 1.").
*/
/* (declarations) */
/* (functions) */

enum comparison
  {
    CMP_LE,
    CMP_EQ,
  };

/* A pair of variables to be compared. */
struct pair
  {
    const struct variable *v[2]; /* The paired variables. */
    double n;             /* The number of valid variable pairs */
    double sum[2];        /* The sum of the members */
    double ssq[2];        /* sum of squares of the members */
    double std_dev[2];    /* Std deviation of the members */
    double s_std_dev[2];  /* Sample Std deviation of the members */
    double mean[2];       /* The means of the members */
    double correlation;   /* Correlation coefficient between the variables. */
    double sum_of_diffs;  /* The sum of the differences */
    double sum_of_prod;   /* The sum of the products */
    double mean_diff;     /* The mean of the differences */
    double ssq_diffs;     /* The sum of the squares of the differences */
    double std_dev_diff;  /* The std deviation of the differences */
  };

/* Which mode was T-TEST invoked */
enum t_test_mode {
  T_1_SAMPLE,                   /* One-sample tests. */
  T_IND_SAMPLES,                /* Independent-sample tests. */
  T_PAIRED                      /* Paired-sample tests. */
};

/* Total state of a T-TEST procedure. */
struct t_test_proc
  {
    enum t_test_mode mode;      /* Mode that T-TEST was invoked in. */
    double criteria;            /* Confidence interval in (0, 1). */
    enum mv_class exclude;      /* Classes of missing values to exclude. */
    bool listwise_missing;      /* Drop whole case if one missing var? */
    struct fmt_spec weight_format; /* Format of weight variable. */

    /* Dependent variables. */
    const struct variable **vars;
    size_t n_vars;

    /* For mode == T_1_SAMPLE. */
    double testval;

    /* For mode == T_PAIRED only. */
    struct pair *pairs;
    size_t n_pairs;

    /* For mode == T_IND_SAMPLES only. */
    struct variable *indep_var; /* Independent variable. */
    enum comparison criterion;  /* Type of comparison. */
    double critical_value;      /* CMP_LE only: Grouping threshold value. */
    union value g_value[2];     /* CMP_EQ only: Per-group indep var values. */
  };

/* Statistics Summary Box */
struct ssbox
  {
    struct tab_table *t;
    void (*populate) (struct ssbox *, struct t_test_proc *);
    void (*finalize) (struct ssbox *);
  };

static void ssbox_create (struct ssbox *, struct t_test_proc *);
static void ssbox_populate (struct ssbox *, struct t_test_proc *);
static void ssbox_finalize (struct ssbox *);

/* Paired Samples Correlation box */
static void pscbox (struct t_test_proc *);


/* Test Results Box. */
struct trbox {
  struct tab_table *t;
  void (*populate) (struct trbox *, struct t_test_proc *);
  void (*finalize) (struct trbox *);
  };

static void trbox_create (struct trbox *, struct t_test_proc *);
static void trbox_populate (struct trbox *, struct t_test_proc *);
static void trbox_finalize (struct trbox *);

static void calculate (struct t_test_proc *, struct casereader *,
                       const struct dataset *);

static int compare_group_binary (const struct group_statistics *a,
                                 const struct group_statistics *b,
                                 const struct t_test_proc *);
static unsigned hash_group_binary (const struct group_statistics *g,
				   const struct t_test_proc *p);

int
cmd_t_test (struct lexer *lexer, struct dataset *ds)
{
  struct cmd_t_test cmd;
  struct t_test_proc proc;
  struct casegrouper *grouper;
  struct casereader *group;
  struct variable *wv;
  bool ok = false;

  proc.pairs = NULL;
  proc.n_pairs = 0;
  proc.vars = NULL;
  proc.indep_var = NULL;
  if (!parse_t_test (lexer, ds, &cmd, &proc))
    goto parse_failed;

  wv = dict_get_weight (dataset_dict (ds));
  proc.weight_format = wv ? *var_get_print_format (wv) : F_8_0;

  if ((cmd.sbc_testval != 0) + (cmd.sbc_groups != 0) + (cmd.sbc_pairs != 0)
      != 1)
    {
      msg (SE, _("Exactly one of TESTVAL, GROUPS and PAIRS subcommands "
                 "must be specified."));
      goto done;
    }

  proc.mode = (cmd.sbc_testval ? T_1_SAMPLE
               : cmd.sbc_groups ? T_IND_SAMPLES
               : T_PAIRED);
  proc.criteria = cmd.sbc_criteria ? cmd.criteria : 0.95;
  proc.exclude = cmd.incl != TTS_INCLUDE ? MV_ANY : MV_SYSTEM;
  proc.listwise_missing = cmd.miss == TTS_LISTWISE;

  if (proc.mode == T_1_SAMPLE)
    proc.testval = cmd.n_testval[0];

  if (proc.mode == T_PAIRED)
    {
      size_t i, j;

      if (cmd.sbc_variables)
	{
	  msg (SE, _("VARIABLES subcommand may not be used with PAIRS."));
          goto done;
	}

      /* Fill proc.vars with the unique variables from pairs. */
      proc.n_vars = proc.n_pairs * 2;
      proc.vars = xmalloc (sizeof *proc.vars * proc.n_vars);
      for (i = j = 0; i < proc.n_pairs; i++)
        {
          proc.vars[j++] = proc.pairs[i].v[0];
          proc.vars[j++] = proc.pairs[i].v[1];
        }
      proc.n_vars = sort_unique (proc.vars, proc.n_vars, sizeof *proc.vars,
                                 compare_var_ptrs_by_name, NULL);
    }
  else
    {
      if (!cmd.n_variables)
        {
          msg (SE, _("One or more VARIABLES must be specified."));
          goto done;
        }
      proc.n_vars = cmd.n_variables;
      proc.vars = cmd.v_variables;
      cmd.v_variables = NULL;
    }

  /* Data pass. */
  grouper = casegrouper_create_splits (proc_open (ds), dataset_dict (ds));
  while (casegrouper_get_next_group (grouper, &group))
    calculate (&proc, group, ds);
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  if (proc.mode == T_IND_SAMPLES)
    {
      int v;
      /* Destroy any group statistics we created */
      for (v = 0; v < proc.n_vars; v++)
	{
	  struct group_proc *grpp = group_proc_get (proc.vars[v]);
	  hsh_destroy (grpp->group_hash);
	}
    }

done:
  free_t_test (&cmd);
parse_failed:
  if (proc.indep_var != NULL)
    {
      int width = var_get_width (proc.indep_var);
      value_destroy (&proc.g_value[0], width);
      value_destroy (&proc.g_value[1], width);
    }
  free (proc.vars);
  free (proc.pairs);
  return ok ? CMD_SUCCESS : CMD_FAILURE;
}

static int
tts_custom_groups (struct lexer *lexer, struct dataset *ds,
                   struct cmd_t_test *cmd UNUSED, void *proc_)
{
  struct t_test_proc *proc = proc_;
  int n_values;
  int width;

  lex_match (lexer, '=');

  proc->indep_var = parse_variable (lexer, dataset_dict (ds));
  if (proc->indep_var == NULL)
    {
      lex_error (lexer, "expecting variable name in GROUPS subcommand");
      return 0;
    }
  width = var_get_width (proc->indep_var);
  value_init (&proc->g_value[0], width);
  value_init (&proc->g_value[1], width);

  if (!lex_match (lexer, '('))
    n_values = 0;
  else
    {
      if (!parse_value (lexer, &proc->g_value[0], width))
        return 0;
      lex_match (lexer, ',');
      if (lex_match (lexer, ')'))
        n_values = 1;
      else
        {
          if (!parse_value (lexer, &proc->g_value[1], width)
              || !lex_force_match (lexer, ')'))
            return 0;
          n_values = 2;
        }
    }

  if (var_is_numeric (proc->indep_var))
    {
      proc->criterion = n_values == 1 ? CMP_LE : CMP_EQ;
      if (n_values == 1)
        proc->critical_value = proc->g_value[0].f;
      else if (n_values == 0)
	{
	  proc->g_value[0].f = 1;
	  proc->g_value[1].f = 2;
	}
    }
  else
    {
      proc->criterion = CMP_EQ;
      if (n_values != 2)
	{
	  msg (SE, _("When applying GROUPS to a string variable, two "
                     "values must be specified."));
	  return 0;
	}
    }
  return 1;
}

static void
add_pair (struct t_test_proc *proc,
          const struct variable *v0, const struct variable *v1)
{
  struct pair *p = &proc->pairs[proc->n_pairs++];
  p->v[0] = v0;
  p->v[1] = v1;
}

static int
tts_custom_pairs (struct lexer *lexer, struct dataset *ds,
                  struct cmd_t_test *cmd UNUSED, void *proc_)
{
  struct t_test_proc *proc = proc_;

  const struct variable **vars1 = NULL;
  size_t n_vars1 = 0;

  const struct variable **vars2 = NULL;
  size_t n_vars2 = 0;

  bool paired = false;

  size_t n_total_pairs;
  size_t i, j;

  lex_match (lexer, '=');

  if (!parse_variables_const (lexer, dataset_dict (ds), &vars1, &n_vars1,
                              PV_DUPLICATE | PV_NUMERIC | PV_NO_SCRATCH))
    return 0;

  if (lex_match (lexer, T_WITH))
    {
      if (!parse_variables_const (lexer, dataset_dict (ds), &vars2, &n_vars2,
                                  PV_DUPLICATE | PV_NUMERIC | PV_NO_SCRATCH))
        {
          free (vars1);
          return 0;
        }

      if (lex_match (lexer, '(')
          && lex_match_id (lexer, "PAIRED")
          && lex_match (lexer, ')'))
        {
          paired = true;
          if (n_vars1 != n_vars2)
            {
              msg (SE, _("PAIRED was specified but the number of variables "
                         "preceding WITH (%zu) did not match the number "
                         "following (%zu)."),
                   n_vars1, n_vars2);
              free (vars1);
              free (vars2);
              return 0;
            }
        }
    }
  else
    {
      if (n_vars1 < 2)
	{
	  free (vars1);
	  msg (SE, _("At least two variables must be specified on PAIRS."));
	  return 0;
	}
    }

  /* Allocate storage for the new pairs. */
  n_total_pairs = proc->n_pairs + (paired ? n_vars1
                                   : n_vars2 > 0 ? n_vars1 * n_vars2
                                   : n_vars1 * (n_vars1 - 1) / 2);
  proc->pairs = xnrealloc (proc->pairs, n_total_pairs, sizeof *proc->pairs);

  /* Populate the pairs with the appropriate variables. */
  if (paired)
    for (i = 0; i < n_vars1; i++)
      add_pair (proc, vars1[i], vars2[i]);
  else if (n_vars2 > 0)
    for (i = 0; i < n_vars1; i++)
      for (j = 0; j < n_vars2; j++)
        add_pair (proc, vars1[i], vars2[j]);
  else
    for (i = 0; i < n_vars1; i++)
      for (j = i + 1; j < n_vars1; j++)
        add_pair (proc, vars1[i], vars1[j]);
  assert (proc->n_pairs == n_total_pairs);

  free (vars1);
  free (vars2);
  return 1;
}

/* Implementation of the SSBOX object. */

static void ssbox_base_init (struct ssbox *, int cols, int rows);
static void ssbox_base_finalize (struct ssbox *);
static void ssbox_one_sample_init (struct ssbox *, struct t_test_proc *);
static void ssbox_independent_samples_init (struct ssbox *, struct t_test_proc *);
static void ssbox_paired_init (struct ssbox *, struct t_test_proc *);

/* Factory to create an ssbox. */
static void
ssbox_create (struct ssbox *ssb, struct t_test_proc *proc)
{
  switch (proc->mode)
    {
    case T_1_SAMPLE:
      ssbox_one_sample_init (ssb, proc);
      break;
    case T_IND_SAMPLES:
      ssbox_independent_samples_init (ssb, proc);
      break;
    case T_PAIRED:
      ssbox_paired_init (ssb, proc);
      break;
    default:
      NOT_REACHED ();
    }
}

/* Despatcher for the populate method */
static void
ssbox_populate (struct ssbox *ssb, struct t_test_proc *proc)
{
  ssb->populate (ssb, proc);
}

/* Despatcher for finalize */
static void
ssbox_finalize (struct ssbox *ssb)
{
  ssb->finalize (ssb);
}

/* Submit the box and clear up */
static void
ssbox_base_finalize (struct ssbox *ssb)
{
  tab_submit (ssb->t);
}

/* Initialize a ssbox struct */
static void
ssbox_base_init (struct ssbox *this, int cols, int rows)
{
  this->finalize = ssbox_base_finalize;
  this->t = tab_create (cols, rows);

  tab_columns (this->t, SOM_COL_DOWN);
  tab_headers (this->t, 0, 0, 1, 0);
  tab_box (this->t, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, cols - 1, rows - 1);
  tab_hline (this->t, TAL_2, 0, cols- 1, 1);
  tab_dim (this->t, tab_natural_dimensions, NULL, NULL);
}

/* ssbox implementations. */

static void ssbox_one_sample_populate (struct ssbox *, struct t_test_proc *);
static void ssbox_independent_samples_populate (struct ssbox *,
                                                struct t_test_proc *);
static void ssbox_paired_populate (struct ssbox *, struct t_test_proc *);

/* Initialize the one_sample ssbox */
static void
ssbox_one_sample_init (struct ssbox *this, struct t_test_proc *proc)
{
  const int hsize = 5;
  const int vsize = proc->n_vars + 1;

  this->populate = ssbox_one_sample_populate;

  ssbox_base_init (this, hsize, vsize);
  tab_title (this->t, _("One-Sample Statistics"));
  tab_vline (this->t, TAL_2, 1, 0, vsize - 1);
  tab_text (this->t, 1, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (this->t, 2, 0, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (this->t, 3, 0, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (this->t, 4, 0, TAB_CENTER | TAT_TITLE, _("S.E. Mean"));
}

/* Initialize the independent samples ssbox */
static void
ssbox_independent_samples_init (struct ssbox *this, struct t_test_proc *proc)
{
  int hsize=6;
  int vsize = proc->n_vars * 2 + 1;

  this->populate = ssbox_independent_samples_populate;

  ssbox_base_init (this, hsize, vsize);
  tab_vline (this->t, TAL_GAP, 1, 0, vsize - 1);
  tab_title (this->t, _("Group Statistics"));
  tab_text (this->t, 1, 0, TAB_CENTER | TAT_TITLE,
            var_get_name (proc->indep_var));
  tab_text (this->t, 2, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (this->t, 3, 0, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (this->t, 4, 0, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (this->t, 5, 0, TAB_CENTER | TAT_TITLE, _("S.E. Mean"));
}

/* Populate the ssbox for independent samples */
static void
ssbox_independent_samples_populate (struct ssbox *ssb,
                                    struct t_test_proc *proc)
{
  int i;

  char *val_lab[2];
  double indep_value[2];

  char prefix[2][3];

  for (i = 0; i < 2; i++)
    {
      union value *value = &proc->g_value[i];
      int width = var_get_width (proc->indep_var);

      indep_value[i] = (proc->criterion == CMP_LE ? proc->critical_value
                        : value->f);

      if (val_type_from_width (width) == VAL_NUMERIC)
        {
          const char *s = var_lookup_value_label (proc->indep_var, value);
          val_lab[i] = s ? xstrdup (s) : xasprintf ("%g", indep_value[i]);
        }
      else
        val_lab[i] = xmemdup0 (value_str (value, width), width);
    }

  if (proc->criterion == CMP_LE)
    {
      strcpy (prefix[0], ">=");
      strcpy (prefix[1], "<");
    }
  else
    {
      strcpy (prefix[0], "");
      strcpy (prefix[1], "");
    }

  for (i = 0; i < proc->n_vars; i++)
    {
      const struct variable *var = proc->vars[i];
      struct hsh_table *grp_hash = group_proc_get (var)->group_hash;
      int count=0;

      tab_text (ssb->t, 0, i * 2 + 1, TAB_LEFT,
                var_get_name (proc->vars[i]));
      tab_text_format (ssb->t, 1, i * 2 + 1, TAB_LEFT,
                       "%s%s", prefix[0], val_lab[0]);
      tab_text_format (ssb->t, 1, i * 2 + 1+ 1, TAB_LEFT,
                       "%s%s", prefix[1], val_lab[1]);

      /* Fill in the group statistics */
      for (count = 0; count < 2; count++)
	{
	  union value search_val;
	  struct group_statistics *gs;

	  if (proc->criterion == CMP_LE)
            search_val.f = proc->critical_value + (count == 0 ? 1.0 : -1.0);
	  else
            search_val = proc->g_value[count];

	  gs = hsh_find (grp_hash, &search_val);
	  assert (gs);

	  tab_double (ssb->t, 2, i * 2 + count+ 1, TAB_RIGHT, gs->n,
                      &proc->weight_format);
	  tab_double (ssb->t, 3, i * 2 + count+ 1, TAB_RIGHT, gs->mean, NULL);
	  tab_double (ssb->t, 4, i * 2 + count+ 1, TAB_RIGHT, gs->std_dev,
                      NULL);
	  tab_double (ssb->t, 5, i * 2 + count+ 1, TAB_RIGHT, gs->se_mean,
                      NULL);
	}
    }
  free (val_lab[0]);
  free (val_lab[1]);
}

/* Initialize the paired values ssbox */
static void
ssbox_paired_init (struct ssbox *this, struct t_test_proc *proc)
{
  int hsize = 6;
  int vsize = proc->n_pairs * 2 + 1;

  this->populate = ssbox_paired_populate;

  ssbox_base_init (this, hsize, vsize);
  tab_title (this->t, _("Paired Sample Statistics"));
  tab_vline (this->t, TAL_GAP, 1, 0, vsize - 1);
  tab_vline (this->t, TAL_2, 2, 0, vsize - 1);
  tab_text (this->t, 2, 0, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (this->t, 3, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (this->t, 4, 0, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (this->t, 5, 0, TAB_CENTER | TAT_TITLE, _("S.E. Mean"));
}

/* Populate the ssbox for paired values */
static void
ssbox_paired_populate (struct ssbox *ssb, struct t_test_proc *proc)
{
  int i;

  for (i = 0; i < proc->n_pairs; i++)
    {
      struct pair *p = &proc->pairs[i];
      int j;

      tab_text_format (ssb->t, 0, i * 2 + 1, TAB_LEFT, _("Pair %d"), i);
      for (j=0; j < 2; j++)
	{
	  /* Titles */
	  tab_text (ssb->t, 1, i * 2 + j + 1, TAB_LEFT,
                    var_get_name (p->v[j]));

	  /* Values */
	  tab_double (ssb->t, 2, i * 2 + j + 1, TAB_RIGHT, p->mean[j], NULL);
	  tab_double (ssb->t, 3, i * 2 + j + 1, TAB_RIGHT, p->n,
                      &proc->weight_format);
	  tab_double (ssb->t, 4, i * 2 + j + 1, TAB_RIGHT, p->std_dev[j],
                      NULL);
	  tab_double (ssb->t, 5, i * 2 + j + 1, TAB_RIGHT,
                     p->std_dev[j] /sqrt (p->n), NULL);
	}
    }
}

/* Populate the one sample ssbox */
static void
ssbox_one_sample_populate (struct ssbox *ssb, struct t_test_proc *proc)
{
  int i;

  for (i = 0; i < proc->n_vars; i++)
    {
      struct group_statistics *gs = &group_proc_get (proc->vars[i])->ugs;

      tab_text (ssb->t, 0, i + 1, TAB_LEFT, var_get_name (proc->vars[i]));
      tab_double (ssb->t, 1, i + 1, TAB_RIGHT, gs->n, &proc->weight_format);
      tab_double (ssb->t, 2, i + 1, TAB_RIGHT, gs->mean, NULL);
      tab_double (ssb->t, 3, i + 1, TAB_RIGHT, gs->std_dev, NULL);
      tab_double (ssb->t, 4, i + 1, TAB_RIGHT, gs->se_mean, NULL);
    }
}

/* Implementation of the Test Results box struct */

static void trbox_base_init (struct trbox *, size_t n_vars, int cols);
static void trbox_base_finalize (struct trbox *);
static void trbox_independent_samples_init (struct trbox *,
                                            struct t_test_proc *);
static void trbox_independent_samples_populate (struct trbox *,
                                                struct t_test_proc *);
static void trbox_one_sample_init (struct trbox *, struct t_test_proc *);
static void trbox_one_sample_populate (struct trbox *, struct t_test_proc *);
static void trbox_paired_init (struct trbox *, struct t_test_proc *);
static void trbox_paired_populate (struct trbox *, struct t_test_proc *);

/* Create a trbox according to mode*/
static void
trbox_create (struct trbox *trb, struct t_test_proc *proc)
{
  switch (proc->mode)
    {
    case T_1_SAMPLE:
      trbox_one_sample_init (trb, proc);
      break;
    case T_IND_SAMPLES:
      trbox_independent_samples_init (trb, proc);
      break;
    case T_PAIRED:
      trbox_paired_init (trb, proc);
      break;
    default:
      NOT_REACHED ();
    }
}

/* Populate a trbox according to proc */
static void
trbox_populate (struct trbox *trb, struct t_test_proc *proc)
{
  trb->populate (trb, proc);
}

/* Submit and destroy a trbox */
static void
trbox_finalize (struct trbox *trb)
{
  trb->finalize (trb);
}

/* Initialize the independent samples trbox */
static void
trbox_independent_samples_init (struct trbox *self,
                                struct t_test_proc *proc)
{
  const int hsize = 11;
  const int vsize = proc->n_vars * 2 + 3;

  assert (self);
  self->populate = trbox_independent_samples_populate;

  trbox_base_init (self, proc->n_vars * 2, hsize);
  tab_title (self->t, _("Independent Samples Test"));
  tab_hline (self->t, TAL_1, 2, hsize - 1, 1);
  tab_vline (self->t, TAL_2, 2, 0, vsize - 1);
  tab_vline (self->t, TAL_1, 4, 0, vsize - 1);
  tab_box (self->t, -1, -1, -1, TAL_1, 2, 1, hsize - 2, vsize - 1);
  tab_hline (self->t, TAL_1, hsize - 2, hsize - 1, 2);
  tab_box (self->t, -1, -1, -1, TAL_1, hsize - 2, 2, hsize - 1, vsize - 1);
  tab_joint_text (self->t, 2, 0, 3, 0,
                  TAB_CENTER, _("Levene's Test for Equality of Variances"));
  tab_joint_text (self->t, 4, 0, hsize- 1, 0,
                  TAB_CENTER, _("t-test for Equality of Means"));

  tab_text (self->t, 2, 2, TAB_CENTER | TAT_TITLE, _("F"));
  tab_text (self->t, 3, 2, TAB_CENTER | TAT_TITLE, _("Sig."));
  tab_text (self->t, 4, 2, TAB_CENTER | TAT_TITLE, _("t"));
  tab_text (self->t, 5, 2, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (self->t, 6, 2, TAB_CENTER | TAT_TITLE, _("Sig. (2-tailed)"));
  tab_text (self->t, 7, 2, TAB_CENTER | TAT_TITLE, _("Mean Difference"));
  tab_text (self->t, 8, 2, TAB_CENTER | TAT_TITLE, _("Std. Error Difference"));
  tab_text (self->t, 9, 2, TAB_CENTER | TAT_TITLE, _("Lower"));
  tab_text (self->t, 10, 2, TAB_CENTER | TAT_TITLE, _("Upper"));

  tab_joint_text_format (self->t, 9, 1, 10, 1, TAB_CENTER,
                         _("%g%% Confidence Interval of the Difference"),
                         proc->criteria * 100.0);
}

/* Populate the independent samples trbox */
static void
trbox_independent_samples_populate (struct trbox *self,
                                    struct t_test_proc *proc)
{
  int i;

  for (i = 0; i < proc->n_vars; i++)
    {
      double p, q;

      double t;
      double df;

      double df1, df2;

      double pooled_variance;
      double std_err_diff;
      double mean_diff;

      double se2;

      const struct variable *var = proc->vars[i];
      struct group_proc *grp_data = group_proc_get (var);

      struct hsh_table *grp_hash = grp_data->group_hash;

      struct group_statistics *gs0;
      struct group_statistics *gs1;

      union value search_val;

      if (proc->criterion == CMP_LE)
	search_val.f = proc->critical_value - 1.0;
      else
	search_val = proc->g_value[0];

      gs0 = hsh_find (grp_hash, &search_val);
      assert (gs0);

      if (proc->criterion == CMP_LE)
	search_val.f = proc->critical_value + 1.0;
      else
	search_val = proc->g_value[1];

      gs1 = hsh_find (grp_hash, &search_val);
      assert (gs1);


      tab_text (self->t, 0, i * 2 + 3, TAB_LEFT, var_get_name (proc->vars[i]));
      tab_text (self->t, 1, i * 2 + 3, TAB_LEFT, _("Equal variances assumed"));
      tab_double (self->t, 2, i * 2 + 3, TAB_CENTER, grp_data->levene, NULL);

      /* Now work out the significance of the Levene test */
      df1 = 1;
      df2 = grp_data->ugs.n - 2;
      q = gsl_cdf_fdist_Q (grp_data->levene, df1, df2);
      tab_double (self->t, 3, i * 2 + 3, TAB_CENTER, q, NULL);

      df = gs0->n + gs1->n - 2.0;
      tab_double (self->t, 5, i * 2 + 3, TAB_RIGHT, df, NULL);

      pooled_variance = (gs0->n * pow2 (gs0->s_std_dev)
                         + gs1->n *pow2 (gs1->s_std_dev)) / df ;

      t = (gs0->mean - gs1->mean) / sqrt (pooled_variance);
      t /= sqrt ((gs0->n + gs1->n) / (gs0->n * gs1->n));

      tab_double (self->t, 4, i * 2 + 3, TAB_RIGHT, t, NULL);

      p = gsl_cdf_tdist_P (t, df);
      q = gsl_cdf_tdist_Q (t, df);

      tab_double (self->t, 6, i * 2 + 3, TAB_RIGHT, 2.0 * (t > 0 ? q : p),
                  NULL);

      mean_diff = gs0->mean - gs1->mean;
      tab_double (self->t, 7, i * 2 + 3, TAB_RIGHT, mean_diff, NULL);


      std_err_diff = sqrt (pow2 (gs0->se_mean) + pow2 (gs1->se_mean));
      tab_double (self->t, 8, i * 2 + 3, TAB_RIGHT, std_err_diff, NULL);

      /* Now work out the confidence interval */
      q = (1 - proc->criteria)/2.0;  /* 2-tailed test */

      t = gsl_cdf_tdist_Qinv (q, df);
      tab_double (self->t, 9, i * 2 + 3, TAB_RIGHT,
                 mean_diff - t * std_err_diff, NULL);

      tab_double (self->t, 10, i * 2 + 3, TAB_RIGHT,
                 mean_diff + t * std_err_diff, NULL);


      /* Now for the \sigma_1 != \sigma_2 case */
      tab_text (self->t, 1, i * 2 + 3 + 1,
                TAB_LEFT, _("Equal variances not assumed"));

      se2 = ((pow2 (gs0->s_std_dev) / (gs0->n - 1)) +
             (pow2 (gs1->s_std_dev) / (gs1->n - 1)));

      t = mean_diff / sqrt (se2);
      tab_double (self->t, 4, i * 2 + 3 + 1, TAB_RIGHT, t, NULL);

      df = pow2 (se2) / ((pow2 (pow2 (gs0->s_std_dev) / (gs0->n - 1))
                          / (gs0->n - 1))
                         + (pow2 (pow2 (gs1->s_std_dev) / (gs1->n - 1))
                            / (gs1->n - 1)));
      tab_double (self->t, 5, i * 2 + 3 + 1, TAB_RIGHT, df, NULL);

      p = gsl_cdf_tdist_P (t, df);
      q = gsl_cdf_tdist_Q (t, df);

      tab_double (self->t, 6, i * 2 + 3 + 1, TAB_RIGHT, 2.0 * (t > 0 ? q : p),
                  NULL);

      /* Now work out the confidence interval */
      q = (1 - proc->criteria) / 2.0;  /* 2-tailed test */

      t = gsl_cdf_tdist_Qinv (q, df);

      tab_double (self->t, 7, i * 2 + 3 + 1, TAB_RIGHT, mean_diff, NULL);
      tab_double (self->t, 8, i * 2 + 3 + 1, TAB_RIGHT, std_err_diff, NULL);
      tab_double (self->t, 9, i * 2 + 3 + 1, TAB_RIGHT,
                 mean_diff - t * std_err_diff, NULL);
      tab_double (self->t, 10, i * 2 + 3 + 1, TAB_RIGHT,
                 mean_diff + t * std_err_diff, NULL);
    }
}

/* Initialize the paired samples trbox */
static void
trbox_paired_init (struct trbox *self, struct t_test_proc *proc)
{
  const int hsize=10;
  const int vsize=proc->n_pairs+ 3;

  self->populate = trbox_paired_populate;

  trbox_base_init (self, proc->n_pairs, hsize);
  tab_title (self->t, _("Paired Samples Test"));
  tab_hline (self->t, TAL_1, 2, 6, 1);
  tab_vline (self->t, TAL_2, 2, 0, vsize - 1);
  tab_joint_text (self->t, 2, 0, 6, 0, TAB_CENTER, _("Paired Differences"));
  tab_box (self->t, -1, -1, -1, TAL_1, 2, 1, 6, vsize - 1);
  tab_box (self->t, -1, -1, -1, TAL_1, 6, 0, hsize - 1, vsize - 1);
  tab_hline (self->t, TAL_1, 5, 6, 2);
  tab_vline (self->t, TAL_GAP, 6, 0, 1);

  tab_joint_text_format (self->t, 5, 1, 6, 1, TAB_CENTER,
                         _("%g%% Confidence Interval of the Difference"),
                         proc->criteria*100.0);

  tab_text (self->t, 2, 2, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (self->t, 3, 2, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (self->t, 4, 2, TAB_CENTER | TAT_TITLE, _("Std. Error Mean"));
  tab_text (self->t, 5, 2, TAB_CENTER | TAT_TITLE, _("Lower"));
  tab_text (self->t, 6, 2, TAB_CENTER | TAT_TITLE, _("Upper"));
  tab_text (self->t, 7, 2, TAB_CENTER | TAT_TITLE, _("t"));
  tab_text (self->t, 8, 2, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (self->t, 9, 2, TAB_CENTER | TAT_TITLE, _("Sig. (2-tailed)"));
}

/* Populate the paired samples trbox */
static void
trbox_paired_populate (struct trbox *trb,
                       struct t_test_proc *proc)
{
  int i;

  for (i = 0; i < proc->n_pairs; i++)
    {
      struct pair *pair = &proc->pairs[i];
      double p, q;
      double se_mean;

      double n = pair->n;
      double t;
      double df = n - 1;

      tab_text_format (trb->t, 0, i + 3, TAB_LEFT, _("Pair %d"), i);
      tab_text_format (trb->t, 1, i + 3, TAB_LEFT, "%s - %s",
                       var_get_name (pair->v[0]),
                       var_get_name (pair->v[1]));
      tab_double (trb->t, 2, i + 3, TAB_RIGHT, pair->mean_diff, NULL);
      tab_double (trb->t, 3, i + 3, TAB_RIGHT, pair->std_dev_diff, NULL);

      /* SE Mean */
      se_mean = pair->std_dev_diff / sqrt (n);
      tab_double (trb->t, 4, i + 3, TAB_RIGHT, se_mean, NULL);

      /* Now work out the confidence interval */
      q = (1 - proc->criteria) / 2.0;  /* 2-tailed test */

      t = gsl_cdf_tdist_Qinv (q, df);

      tab_double (trb->t, 5, i + 3, TAB_RIGHT,
                 pair->mean_diff - t * se_mean, NULL);
      tab_double (trb->t, 6, i + 3, TAB_RIGHT,
                 pair->mean_diff + t * se_mean, NULL);

      t = ((pair->mean[0] - pair->mean[1])
           / sqrt ((pow2 (pair->s_std_dev[0]) + pow2 (pair->s_std_dev[1])
                    - (2 * pair->correlation
                       * pair->s_std_dev[0] * pair->s_std_dev[1]))
                   / (n - 1)));

      tab_double (trb->t, 7, i + 3, TAB_RIGHT, t, NULL);

      /* Degrees of freedom */
      tab_double (trb->t, 8, i + 3, TAB_RIGHT, df, &proc->weight_format);

      p = gsl_cdf_tdist_P (t,df);
      q = gsl_cdf_tdist_Q (t,df);

      tab_double (trb->t, 9, i + 3, TAB_RIGHT, 2.0 * (t > 0 ? q : p), NULL);
    }
}

/* Initialize the one sample trbox */
static void
trbox_one_sample_init (struct trbox *self, struct t_test_proc *proc)
{
  const int hsize = 7;
  const int vsize = proc->n_vars + 3;

  self->populate = trbox_one_sample_populate;

  trbox_base_init (self, proc->n_vars, hsize);
  tab_title (self->t, _("One-Sample Test"));
  tab_hline (self->t, TAL_1, 1, hsize - 1, 1);
  tab_vline (self->t, TAL_2, 1, 0, vsize - 1);

  tab_joint_text_format (self->t, 1, 0, hsize - 1, 0, TAB_CENTER,
                         _("Test Value = %f"), proc->testval);

  tab_box (self->t, -1, -1, -1, TAL_1, 1, 1, hsize - 1, vsize - 1);


  tab_joint_text_format (self->t, 5, 1, 6, 1, TAB_CENTER,
                         _("%g%% Confidence Interval of the Difference"),
                         proc->criteria * 100.0);

  tab_vline (self->t, TAL_GAP, 6, 1, 1);
  tab_hline (self->t, TAL_1, 5, 6, 2);
  tab_text (self->t, 1, 2, TAB_CENTER | TAT_TITLE, _("t"));
  tab_text (self->t, 2, 2, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (self->t, 3, 2, TAB_CENTER | TAT_TITLE, _("Sig. (2-tailed)"));
  tab_text (self->t, 4, 2, TAB_CENTER | TAT_TITLE, _("Mean Difference"));
  tab_text (self->t, 5, 2, TAB_CENTER | TAT_TITLE, _("Lower"));
  tab_text (self->t, 6, 2, TAB_CENTER | TAT_TITLE, _("Upper"));
}

/* Populate the one sample trbox */
static void
trbox_one_sample_populate (struct trbox *trb, struct t_test_proc *proc)
{
  int i;

  assert (trb->t);

  for (i = 0; i < proc->n_vars; i++)
    {
      double t;
      double p, q;
      double df;
      struct group_statistics *gs = &group_proc_get (proc->vars[i])->ugs;

      tab_text (trb->t, 0, i + 3, TAB_LEFT, var_get_name (proc->vars[i]));

      t = (gs->mean - proc->testval) * sqrt (gs->n) / gs->std_dev;

      tab_double (trb->t, 1, i + 3, TAB_RIGHT, t, NULL);

      /* degrees of freedom */
      df = gs->n - 1;

      tab_double (trb->t, 2, i + 3, TAB_RIGHT, df, &proc->weight_format);

      p = gsl_cdf_tdist_P (t, df);
      q = gsl_cdf_tdist_Q (t, df);

      /* Multiply by 2 to get 2-tailed significance, makeing sure we've got
	 the correct tail*/
      tab_double (trb->t, 3, i + 3, TAB_RIGHT, 2.0 * (t > 0 ? q : p), NULL);
      tab_double (trb->t, 4, i + 3, TAB_RIGHT, gs->mean_diff, NULL);


      q = (1 - proc->criteria) / 2.0;  /* 2-tailed test */
      t = gsl_cdf_tdist_Qinv (q, df);

      tab_double (trb->t, 5, i + 3, TAB_RIGHT,
		 gs->mean_diff - t * gs->se_mean, NULL);
      tab_double (trb->t, 6, i + 3, TAB_RIGHT,
		 gs->mean_diff + t * gs->se_mean, NULL);
    }
}

/* Base initializer for the generalized trbox */
static void
trbox_base_init (struct trbox *self, size_t data_rows, int cols)
{
  const size_t rows = 3 + data_rows;

  self->finalize = trbox_base_finalize;
  self->t = tab_create (cols, rows);
  tab_headers (self->t, 0, 0, 3, 0);
  tab_box (self->t, TAL_2, TAL_2, TAL_0, TAL_0, 0, 0, cols - 1, rows - 1);
  tab_hline (self->t, TAL_2, 0, cols- 1, 3);
  tab_dim (self->t, tab_natural_dimensions, NULL, NULL);
}

/* Base finalizer for the trbox */
static void
trbox_base_finalize (struct trbox *trb)
{
  tab_submit (trb->t);
}

/* Create, populate and submit the Paired Samples Correlation box */
static void
pscbox (struct t_test_proc *proc)
{
  const int rows=1+proc->n_pairs;
  const int cols=5;
  int i;

  struct tab_table *table;

  table = tab_create (cols, rows);

  tab_columns (table, SOM_COL_DOWN);
  tab_headers (table, 0, 0, 1, 0);
  tab_box (table, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, cols - 1, rows - 1);
  tab_hline (table, TAL_2, 0, cols - 1, 1);
  tab_vline (table, TAL_2, 2, 0, rows - 1);
  tab_dim (table, tab_natural_dimensions, NULL, NULL);
  tab_title (table, _("Paired Samples Correlations"));

  /* column headings */
  tab_text (table, 2, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (table, 3, 0, TAB_CENTER | TAT_TITLE, _("Correlation"));
  tab_text (table, 4, 0, TAB_CENTER | TAT_TITLE, _("Sig."));

  for (i = 0; i < proc->n_pairs; i++)
    {
      struct pair *pair = &proc->pairs[i];

      /* row headings */
      tab_text_format (table, 0, i + 1, TAB_LEFT | TAT_TITLE,
                       _("Pair %d"), i);
      tab_text_format (table, 1, i + 1, TAB_LEFT | TAT_TITLE,
                       _("%s & %s"),
                       var_get_name (pair->v[0]),
                       var_get_name (pair->v[1]));

      /* row data */
      tab_double (table, 2, i + 1, TAB_RIGHT, pair->n, &proc->weight_format);
      tab_double (table, 3, i + 1, TAB_RIGHT, pair->correlation, NULL);

      tab_double (table, 4, i + 1, TAB_RIGHT, 
		  2.0 * significance_of_correlation (pair->correlation, pair->n), NULL);
    }

  tab_submit (table);
}

/* Calculation Implementation */

/* Calculations common to all variants of the T test. */
static void
common_calc (const struct dictionary *dict,
             struct t_test_proc *proc,
             struct casereader *reader)
{
  struct ccase *c;
  int i;

  for (i = 0; i < proc->n_vars; i++)
    {
      struct group_statistics *gs = &group_proc_get (proc->vars[i])->ugs;
      gs->sum = 0;
      gs->n = 0;
      gs->ssq = 0;
      gs->sum_diff = 0;
    }

  for (; (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      double weight = dict_get_case_weight (dict, c, NULL);

      /* Listwise has to be implicit if the independent variable
         is missing ?? */
      if (proc->mode == T_IND_SAMPLES)
        {
          if (var_is_value_missing (proc->indep_var,
                                    case_data (c, proc->indep_var),
                                    proc->exclude))
            continue;
        }

      for (i = 0; i < proc->n_vars; i++)
        {
          const struct variable *v = proc->vars[i];
          const union value *val = case_data (c, v);

          if (!var_is_value_missing (v, val, proc->exclude))
            {
              struct group_statistics *gs;
              gs = &group_proc_get (v)->ugs;

              gs->n += weight;
              gs->sum += weight * val->f;
              gs->ssq += weight * pow2 (val->f);
            }
        }
    }
  casereader_destroy (reader);

  for (i = 0; i < proc->n_vars; i++)
    {
      struct group_statistics *gs = &group_proc_get (proc->vars[i])->ugs;

      gs->mean = gs->sum / gs->n;
      gs->s_std_dev = sqrt (((gs->ssq / gs->n) - pow2 (gs->mean)));
      gs->std_dev = sqrt (gs->n / (gs->n- 1)
                          * ((gs->ssq / gs->n) - pow2 (gs->mean)));
      gs->se_mean = gs->std_dev / sqrt (gs->n);
      gs->mean_diff = gs->sum_diff / gs->n;
    }
}

/* Calculations for one sample T test. */
static int
one_sample_calc (const struct dictionary *dict, struct t_test_proc *proc,
                 struct casereader *reader)
{
  struct ccase *c;
  int i;

  for (i = 0; i < proc->n_vars; i++)
    {
      struct group_statistics *gs = &group_proc_get (proc->vars[i])->ugs;
      gs->sum_diff = 0;
    }

  for (; (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      double weight = dict_get_case_weight (dict, c, NULL);
      for (i = 0; i < proc->n_vars; i++)
        {
          const struct variable *v = proc->vars[i];
          struct group_statistics *gs = &group_proc_get (v)->ugs;
          const union value *val = case_data (c, v);
          if (!var_is_value_missing (v, val, proc->exclude))
            gs->sum_diff += weight * (val->f - proc->testval);
        }
    }

  for (i = 0; i < proc->n_vars; i++)
    {
      struct group_statistics *gs = &group_proc_get (proc->vars[i])->ugs;
      gs->mean_diff = gs->sum_diff / gs->n;
    }

  casereader_destroy (reader);

  return 0;
}

static int
paired_calc (const struct dictionary *dict, struct t_test_proc *proc,
             struct casereader *reader)
{
  struct ccase *c;
  int i;

  for (i = 0; i < proc->n_pairs; i++)
    {
      struct pair *pair = &proc->pairs[i];
      pair->n = 0;
      pair->sum[0] = pair->sum[1] = 0;
      pair->ssq[0] = pair->ssq[1] = 0;
      pair->sum_of_prod = 0;
      pair->correlation = 0;
      pair->sum_of_diffs = 0;
      pair->ssq_diffs = 0;
    }

  for (; (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      double weight = dict_get_case_weight (dict, c, NULL);
      for (i = 0; i < proc->n_pairs; i++)
        {
          struct pair *pair = &proc->pairs[i];
          const struct variable *v0 = pair->v[0];
          const struct variable *v1 = pair->v[1];

          const union value *val0 = case_data (c, v0);
          const union value *val1 = case_data (c, v1);

          if (!var_is_value_missing (v0, val0, proc->exclude)
              && !var_is_value_missing (v1, val1, proc->exclude))
            {
              pair->n += weight;
              pair->sum[0] += weight * val0->f;
              pair->sum[1] += weight * val1->f;
              pair->ssq[0] += weight * pow2 (val0->f);
              pair->ssq[1] += weight * pow2 (val1->f);
              pair->sum_of_prod += weight * val0->f * val1->f;
              pair->sum_of_diffs += weight * (val0->f - val1->f);
              pair->ssq_diffs += weight * pow2 (val0->f - val1->f);
            }
        }
    }

  for (i = 0; i < proc->n_pairs; i++)
    {
      struct pair *pair = &proc->pairs[i];
      const double n = pair->n;
      int j;

      for (j=0; j < 2; j++)
	{
	  pair->mean[j] = pair->sum[j] / n;
	  pair->s_std_dev[j] = sqrt ((pair->ssq[j] / n
                                      - pow2 (pair->mean[j])));
	  pair->std_dev[j] = sqrt (n / (n- 1) * (pair->ssq[j] / n
                                                - pow2 (pair->mean[j])));
	}

      pair->correlation = (pair->sum_of_prod / pair->n
                           - pair->mean[0] * pair->mean[1]);
      /* correlation now actually contains the covariance */
      pair->correlation /= pair->std_dev[0] * pair->std_dev[1];
      pair->correlation *= pair->n / (pair->n - 1);

      pair->mean_diff = pair->sum_of_diffs / n;
      pair->std_dev_diff = sqrt (n / (n - 1) * ((pair->ssq_diffs / n)
                                                - pow2 (pair->mean_diff)));
    }

  casereader_destroy (reader);
  return 0;
}

static int
group_calc (const struct dictionary *dict, struct t_test_proc *proc,
            struct casereader *reader)
{
  struct ccase *c;
  int i;

  for (i = 0; i < proc->n_vars; i++)
    {
      struct group_proc *ttpr = group_proc_get (proc->vars[i]);
      int j;

      /* There's always 2 groups for a T - TEST */
      ttpr->n_groups = 2;
      ttpr->group_hash = hsh_create (2,
                                     (hsh_compare_func *) compare_group_binary,
                                     (hsh_hash_func *) hash_group_binary,
                                     (hsh_free_func *) free_group,
                                     proc);

      for (j = 0; j < 2; j++)
	{
	  struct group_statistics *gs = xmalloc (sizeof *gs);
	  gs->sum = 0;
	  gs->n = 0;
	  gs->ssq = 0;
	  if (proc->criterion == CMP_EQ)
            gs->id = proc->g_value[j];
	  else
	    {
	      if (j == 0)
		gs->id.f = proc->critical_value - 1.0;
	      else
		gs->id.f = proc->critical_value + 1.0;
	    }

	  hsh_insert (ttpr->group_hash, gs);
	}
    }

  for (; (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      const double weight = dict_get_case_weight (dict, c, NULL);
      const union value *gv;

      if (var_is_value_missing (proc->indep_var,
                                case_data (c, proc->indep_var), proc->exclude))
        continue;

      gv = case_data (c, proc->indep_var);
      for (i = 0; i < proc->n_vars; i++)
        {
          const struct variable *var = proc->vars[i];
          const union value *val = case_data (c, var);
          struct hsh_table *grp_hash = group_proc_get (var)->group_hash;
          struct group_statistics *gs = hsh_find (grp_hash, gv);

          /* If the independent variable doesn't match either of the values
             for this case then move on to the next case. */
          if (gs == NULL)
            break;

          if (!var_is_value_missing (var, val, proc->exclude))
            {
              gs->n += weight;
              gs->sum += weight * val->f;
              gs->ssq += weight * pow2 (val->f);
            }
        }
    }

  for (i = 0; i < proc->n_vars; i++)
    {
      const struct variable *var = proc->vars[i];
      struct hsh_table *grp_hash = group_proc_get (var)->group_hash;
      struct hsh_iterator g;
      struct group_statistics *gs;
      int count = 0;

      for (gs = hsh_first (grp_hash, &g); gs != NULL;
	   gs = hsh_next (grp_hash, &g))
	{
	  gs->mean = gs->sum / gs->n;
	  gs->s_std_dev = sqrt (((gs->ssq / gs->n) - pow2 (gs->mean)));
	  gs->std_dev = sqrt (gs->n / (gs->n- 1)
                              * ((gs->ssq / gs->n) - pow2 (gs->mean)));
	  gs->se_mean = gs->std_dev / sqrt (gs->n);
	  count++;
	}
      assert (count == 2);
    }

  casereader_destroy (reader);

  return 0;
}

static void
calculate (struct t_test_proc *proc,
           struct casereader *input, const struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);
  struct ssbox stat_summary_box;
  struct trbox test_results_box;
  struct taint *taint;
  struct ccase *c;

  c = casereader_peek (input, 0);
  if (c == NULL)
    {
      casereader_destroy (input);
      return;
    }
  output_split_file_values (ds, c);
  case_unref (c);

  if (proc->listwise_missing)
    input = casereader_create_filter_missing (input,
                                              proc->vars,
                                              proc->n_vars,
                                              proc->exclude, NULL, NULL);
  input = casereader_create_filter_weight (input, dict, NULL, NULL);
  taint = taint_clone (casereader_get_taint (input));

  common_calc (dict, proc, casereader_clone (input));
  switch (proc->mode)
    {
    case T_1_SAMPLE:
      one_sample_calc (dict, proc, input);
      break;
    case T_PAIRED:
      paired_calc (dict, proc, input);
      break;
    case T_IND_SAMPLES:
      group_calc (dict, proc, casereader_clone (input));
      levene (dict, input, proc->indep_var, proc->n_vars, proc->vars,
              proc->exclude);
      break;
    default:
      NOT_REACHED ();
    }

  if (!taint_has_tainted_successor (taint))
    {
      ssbox_create (&stat_summary_box, proc);
      ssbox_populate (&stat_summary_box, proc);
      ssbox_finalize (&stat_summary_box);

      if (proc->mode == T_PAIRED)
        pscbox (proc);

      trbox_create (&test_results_box, proc);
      trbox_populate (&test_results_box, proc);
      trbox_finalize (&test_results_box);
    }

  taint_destroy (taint);
}

/* return 0 if G belongs to group 0,
          1 if it belongs to group 1,
          2 if it belongs to neither group */
static int
which_group (const struct group_statistics *g,
             const struct t_test_proc *proc)
{
  int width = var_get_width (proc->indep_var);

  if (0 == value_compare_3way (&g->id, &proc->g_value[0], width))
    return 0;

  if (0 == value_compare_3way (&g->id, &proc->g_value[1], width))
    return 1;

  return 2;
}

/* Return -1 if the id of a is less than b; +1 if greater than and
   0 if equal */
static int
compare_group_binary (const struct group_statistics *a,
                      const struct group_statistics *b,
                      const struct t_test_proc *proc)
{
  int flag_a;
  int flag_b;

  if (proc->criterion == CMP_LE)
    {
      flag_a = (a->id.f < proc->critical_value);
      flag_b = (b->id.f < proc->critical_value);
    }
  else
    {
      flag_a = which_group (a, proc);
      flag_b = which_group (b, proc);
    }

  if (flag_a < flag_b)
    return - 1;

  return (flag_a > flag_b);
}

/* This is a degenerate case of a hash, since it can only return three possible
   values.  It's really a comparison, being used as a hash function */

static unsigned
hash_group_binary (const struct group_statistics *g,
                   const struct t_test_proc *proc)
{
  return (proc->criterion == CMP_LE
          ? g->id.f < proc->critical_value
          : which_group (g, proc));
}

/*
  Local Variables:
  mode: c
  End:
*/
