/* PSPP - a program for statistical analysis. -*-c-*-
   Copyright (C) 2006, 2008, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/stats/npar.h"

#include <stdlib.h>
#include <math.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/settings.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "language/stats/binomial.h"
#include "language/stats/chisquare.h"
#include "language/stats/ks-one-sample.h"
#include "language/stats/cochran.h"
#include "language/stats/friedman.h"
#include "language/stats/jonckheere-terpstra.h"
#include "language/stats/kruskal-wallis.h"
#include "language/stats/mann-whitney.h"
#include "language/stats/mcnemar.h"
#include "language/stats/median.h"
#include "language/stats/npar-summary.h"
#include "language/stats/runs.h"
#include "language/stats/sign.h"
#include "language/stats/wilcoxon.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmapx.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/taint.h"
#include "math/moments.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Settings for subcommand specifiers. */
enum missing_type
  {
    MISS_ANALYSIS,
    MISS_LISTWISE,
  };

/* Array indices for STATISTICS subcommand. */
enum
  {
    NPAR_ST_DESCRIPTIVES = 0,
    NPAR_ST_QUARTILES = 1,
    NPAR_ST_ALL = 2,
    NPAR_ST_count
  };

/* NPAR TESTS structure. */
struct cmd_npar_tests
  {
    /* Count variables indicating how many
       of the subcommands have been given. */
    int chisquare;
    int cochran;
    int binomial;
    int ks_one_sample;
    int wilcoxon;
    int sign;
    int runs;
    int friedman;
    int kendall;
    int kruskal_wallis;
    int mann_whitney;
    int mcnemar;
    int median;
    int jonckheere_terpstra;
    int missing;
    int method;
    int statistics;

    /* How missing values should be treated */
    long miss;

    /* Which statistics have been requested */
    int a_statistics[NPAR_ST_count];
  };


struct npar_specs
{
  struct pool *pool;
  struct npar_test **test;
  size_t n_tests;

  const struct variable **vv; /* Compendium of all variables
				  (those mentioned on ANY subcommand */
  int n_vars; /* Number of variables in vv */

  enum mv_class filter;    /* Missing values to filter. */

  bool descriptives;       /* Descriptive statistics should be calculated */
  bool quartiles;          /* Quartiles should be calculated */

  bool exact;  /* Whether exact calculations have been requested */
  double timer;   /* Maximum time (in minutes) to wait for exact calculations */
};


/* Prototype for custom subcommands of NPAR TESTS. */
static int npar_chisquare (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_binomial (struct lexer *, struct dataset *,  struct npar_specs *);
static int npar_ks_one_sample (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_runs (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_friedman (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_kendall (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_cochran (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_wilcoxon (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_sign (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_kruskal_wallis (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_jonckheere_terpstra (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_mann_whitney (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_mcnemar (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_median (struct lexer *, struct dataset *, struct npar_specs *);

static int npar_method (struct lexer *, struct npar_specs *);

/* Command parsing functions. */
static int parse_npar_tests (struct lexer *lexer, struct dataset *ds, struct cmd_npar_tests *p,
			     struct npar_specs *npar_specs );

static int
parse_npar_tests (struct lexer *lexer, struct dataset *ds, struct cmd_npar_tests *npt,
		  struct npar_specs *nps)
{
  npt->chisquare = 0;
  npt->cochran = 0;
  npt->binomial = 0;
  npt->ks_one_sample = 0;
  npt->wilcoxon = 0;
  npt->sign = 0;
  npt->runs = 0;
  npt->friedman = 0;
  npt->kendall = 0;
  npt->kruskal_wallis = 0;
  npt->mann_whitney = 0;
  npt->mcnemar = 0;
  npt->median = 0;
  npt->jonckheere_terpstra = 0;

  npt->miss = MISS_ANALYSIS;
  npt->missing = 0;
  npt->method = 0;
  npt->statistics = 0;

  memset (npt->a_statistics, 0, sizeof npt->a_statistics);
  for (;;)
    {
      if (lex_match_id (lexer, "COCHRAN"))
	{
          npt->cochran++;
          switch (npar_cochran (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
	}
      else if (lex_match_id (lexer, "FRIEDMAN"))
	{
          npt->friedman++;
          switch (npar_friedman (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
	}
      else if (lex_match_id (lexer, "KENDALL"))
	{
          npt->kendall++;
          switch (npar_kendall (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
	}
      else if (lex_match_id (lexer, "RUNS"))
	{
          npt->runs++;
          switch (npar_runs (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
	}
      else if (lex_match_id (lexer, "CHISQUARE"))
        {
          lex_match (lexer, T_EQUALS);
          npt->chisquare++;
          switch (npar_chisquare (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            case 3:
              continue;
            default:
              NOT_REACHED ();
            }
        }
      else if (lex_match_id (lexer, "BINOMIAL"))
        {
          lex_match (lexer, T_EQUALS);
          npt->binomial++;
          switch (npar_binomial (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
        }
      else if (lex_match_phrase (lexer, "K-S") ||
	       lex_match_phrase (lexer, "KOLMOGOROV-SMIRNOV"))
        {
          lex_match (lexer, T_EQUALS);
          npt->ks_one_sample++;
          switch (npar_ks_one_sample (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
        }
      else if (lex_match_phrase (lexer, "J-T") ||
	       lex_match_phrase (lexer, "JONCKHEERE-TERPSTRA"))
        {
          lex_match (lexer, T_EQUALS);
          npt->jonckheere_terpstra++;
          switch (npar_jonckheere_terpstra (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
        }
      else if (lex_match_phrase (lexer, "K-W") ||
	       lex_match_phrase (lexer, "KRUSKAL-WALLIS"))
        {
          lex_match (lexer, T_EQUALS);
          npt->kruskal_wallis++;
          switch (npar_kruskal_wallis (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
        }
      else if (lex_match_phrase (lexer, "MCNEMAR"))
        {
          lex_match (lexer, T_EQUALS);
          npt->mcnemar++;
          switch (npar_mcnemar (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
        }
      else if (lex_match_phrase (lexer, "M-W") ||
	       lex_match_phrase (lexer, "MANN-WHITNEY"))
        {
          lex_match (lexer, T_EQUALS);
          npt->mann_whitney++;
          switch (npar_mann_whitney (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
	}
      else if (lex_match_phrase (lexer, "MEDIAN"))
        {
          npt->median++;

          switch (npar_median (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
        }
      else if (lex_match_id (lexer, "WILCOXON"))
        {
          lex_match (lexer, T_EQUALS);
          npt->wilcoxon++;
          switch (npar_wilcoxon (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
        }
      else if (lex_match_id (lexer, "SIGN"))
        {
          lex_match (lexer, T_EQUALS);
          npt->sign++;
          switch (npar_sign (lexer, ds, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
        }
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          npt->missing++;
          if (npt->missing > 1)
            {
              lex_sbc_only_once ("MISSING");
              goto lossage;
            }
          while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
            {
              if (lex_match_id (lexer, "ANALYSIS"))
                npt->miss = MISS_ANALYSIS;
              else if (lex_match_id (lexer, "LISTWISE"))
                npt->miss = MISS_LISTWISE;
              else if (lex_match_id (lexer, "INCLUDE"))
                nps->filter = MV_SYSTEM;
              else if (lex_match_id (lexer, "EXCLUDE"))
                nps->filter = MV_ANY;
              else
                {
                  lex_error (lexer, NULL);
                  goto lossage;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "METHOD"))
        {
          lex_match (lexer, T_EQUALS);
          npt->method++;
          if (npt->method > 1)
            {
              lex_sbc_only_once ("METHOD");
              goto lossage;
            }
          switch (npar_method (lexer, nps))
            {
            case 0:
              goto lossage;
            case 1:
              break;
            case 2:
              lex_error (lexer, NULL);
              goto lossage;
            default:
              NOT_REACHED ();
            }
        }
      else if (lex_match_id (lexer, "STATISTICS"))
        {
          lex_match (lexer, T_EQUALS);
          npt->statistics++;
          while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
            {
              if (lex_match_id (lexer, "DESCRIPTIVES"))
                npt->a_statistics[NPAR_ST_DESCRIPTIVES] = 1;
              else if (lex_match_id (lexer, "QUARTILES"))
                npt->a_statistics[NPAR_ST_QUARTILES] = 1;
              else if (lex_match (lexer, T_ALL))
                npt->a_statistics[NPAR_ST_ALL] = 1;
              else
                {
                  lex_error (lexer, NULL);
                  goto lossage;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if ( settings_get_syntax () != COMPATIBLE && lex_match_id (lexer, "ALGORITHM"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "COMPATIBLE"))
            settings_set_cmd_algorithm (COMPATIBLE);
          else if (lex_match_id (lexer, "ENHANCED"))
            settings_set_cmd_algorithm (ENHANCED);
          }
        if (!lex_match (lexer, T_SLASH))
          break;
      }

    if (lex_token (lexer) != T_ENDCMD)
      {
        lex_error (lexer, _("expecting end of command"));
        goto lossage;
      }

  return true;

lossage:
  return false;
}


static void one_sample_insert_variables (const struct npar_test *test,
					 struct hmapx *);

static void two_sample_insert_variables (const struct npar_test *test,
					 struct hmapx *);

static void n_sample_insert_variables (const struct npar_test *test,
				       struct hmapx *);

static void
npar_execute (struct casereader *input,
             const struct npar_specs *specs,
	     const struct dataset *ds)
{
  int t;
  struct descriptives *summary_descriptives = NULL;

  for ( t = 0 ; t < specs->n_tests; ++t )
    {
      const struct npar_test *test = specs->test[t];
      if ( NULL == test->execute )
	{
	  msg (SW, _("%s subcommand not currently implemented."), "NPAR");
	  continue;
	}
      test->execute (ds, casereader_clone (input), specs->filter, test, specs->exact, specs->timer);
    }

  if ( specs->descriptives )
    {
      summary_descriptives = xnmalloc (sizeof (*summary_descriptives),
				       specs->n_vars);

      npar_summary_calc_descriptives (summary_descriptives,
                                      casereader_clone (input),
				      dataset_dict (ds),
				      specs->vv, specs->n_vars,
                                      specs->filter);
    }

  if ( (specs->descriptives || specs->quartiles)
       && !taint_has_tainted_successor (casereader_get_taint (input)) )
    do_summary_box (summary_descriptives, specs->vv, specs->n_vars );

  free (summary_descriptives);
  casereader_destroy (input);
}

int
cmd_npar_tests (struct lexer *lexer, struct dataset *ds)
{
  struct cmd_npar_tests cmd;
  bool ok;
  int i;
  struct npar_specs npar_specs = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  struct casegrouper *grouper;
  struct casereader *input, *group;
  struct hmapx var_map = HMAPX_INITIALIZER (var_map);


  npar_specs.pool = pool_create ();
  npar_specs.filter = MV_ANY;
  npar_specs.n_vars = -1;
  npar_specs.vv = NULL;

  if ( ! parse_npar_tests (lexer, ds, &cmd, &npar_specs) )
    {
      pool_destroy (npar_specs.pool);
      return CMD_FAILURE;
    }

  for (i = 0; i < npar_specs.n_tests; ++i )
    {
      const struct npar_test *test = npar_specs.test[i];
      test->insert_variables (test, &var_map);
    }

  {
    struct hmapx_node *node;
    struct variable *var;
    npar_specs.n_vars = 0;

    HMAPX_FOR_EACH (var, node, &var_map)
      {
	npar_specs.n_vars ++;
	npar_specs.vv = pool_nrealloc (npar_specs.pool, npar_specs.vv, npar_specs.n_vars, sizeof (*npar_specs.vv));
	npar_specs.vv[npar_specs.n_vars - 1] = var;
      }
  }

  sort (npar_specs.vv, npar_specs.n_vars, sizeof (*npar_specs.vv), 
	 compare_var_ptrs_by_name, NULL);

  if ( cmd.statistics )
    {
      int i;

      for ( i = 0 ; i < NPAR_ST_count; ++i )
	{
	  if ( cmd.a_statistics[i] )
	    {
	      switch ( i )
		{
		case NPAR_ST_DESCRIPTIVES:
		  npar_specs.descriptives = true;
		  break;
		case NPAR_ST_QUARTILES:
		  npar_specs.quartiles = true;
		  break;
		case NPAR_ST_ALL:
		  npar_specs.quartiles = true;
		  npar_specs.descriptives = true;
		  break;
		default:
		  NOT_REACHED ();
		};
	    }
	}
    }

  input = proc_open (ds);
  if ( cmd.miss == MISS_LISTWISE )
    {
      input = casereader_create_filter_missing (input,
						npar_specs.vv,
						npar_specs.n_vars,
						npar_specs.filter,
						NULL, NULL);
    }


  grouper = casegrouper_create_splits (input, dataset_dict (ds));
  while (casegrouper_get_next_group (grouper, &group))
    npar_execute (group, &npar_specs, ds);
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  pool_destroy (npar_specs.pool);
  hmapx_destroy (&var_map);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

static int
npar_runs (struct lexer *lexer, struct dataset *ds,
	   struct npar_specs *specs)
{
  struct runs_test *rt = pool_alloc (specs->pool, sizeof (*rt));
  struct one_sample_test *tp = &rt->parent;
  struct npar_test *nt = &tp->parent;

  nt->execute = runs_execute;
  nt->insert_variables = one_sample_insert_variables;

  if ( lex_force_match (lexer, T_LPAREN) )
    {
      if ( lex_match_id (lexer, "MEAN"))
	{
	  rt->cp_mode = CP_MEAN;
	}
      else if (lex_match_id (lexer, "MEDIAN"))
	{
	  rt->cp_mode = CP_MEDIAN;
	}
      else if (lex_match_id (lexer, "MODE"))
	{
	  rt->cp_mode = CP_MODE;
	}
      else if (lex_is_number (lexer))
	{
	  rt->cutpoint = lex_number (lexer);
	  rt->cp_mode = CP_CUSTOM;
	  lex_get (lexer);
	}
      else
	{
	  lex_error (lexer, _("Expecting %s, %s, %s or a number."), "MEAN", "MEDIAN", "MODE");
	  return 0;
	}
		  
      lex_force_match (lexer, T_RPAREN);
      lex_force_match (lexer, T_EQUALS);
      if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
				  &tp->vars, &tp->n_vars,
				  PV_NO_SCRATCH | PV_NO_DUPLICATE | PV_NUMERIC))
	{
	  return 2;
	}
    }

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);

  specs->test[specs->n_tests - 1] = nt;

  return 1;
}

static int
npar_friedman (struct lexer *lexer, struct dataset *ds,
	       struct npar_specs *specs)
{
  struct friedman_test *ft = pool_alloc (specs->pool, sizeof (*ft)); 
  struct one_sample_test *ost = &ft->parent;
  struct npar_test *nt = &ost->parent;

  ft->kendalls_w = false;
  nt->execute = friedman_execute;
  nt->insert_variables = one_sample_insert_variables;

  lex_match (lexer, T_EQUALS);

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
				   &ost->vars, &ost->n_vars,
				   PV_NO_SCRATCH | PV_NO_DUPLICATE | PV_NUMERIC))
    {
      return 2;
    }

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);

  specs->test[specs->n_tests - 1] = nt;

  return 1;
}

static int
npar_kendall (struct lexer *lexer, struct dataset *ds,
	       struct npar_specs *specs)
{
  struct friedman_test *kt = pool_alloc (specs->pool, sizeof (*kt)); 
  struct one_sample_test *ost = &kt->parent;
  struct npar_test *nt = &ost->parent;

  kt->kendalls_w = true;
  nt->execute = friedman_execute;
  nt->insert_variables = one_sample_insert_variables;

  lex_match (lexer, T_EQUALS);

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
				   &ost->vars, &ost->n_vars,
				   PV_NO_SCRATCH | PV_NO_DUPLICATE | PV_NUMERIC))
    {
      return 2;
    }

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);

  specs->test[specs->n_tests - 1] = nt;

  return 1;
}


static int
npar_cochran (struct lexer *lexer, struct dataset *ds,
	       struct npar_specs *specs)
{
  struct one_sample_test *ft = pool_alloc (specs->pool, sizeof (*ft)); 
  struct npar_test *nt = &ft->parent;

  nt->execute = cochran_execute;
  nt->insert_variables = one_sample_insert_variables;

  lex_match (lexer, T_EQUALS);

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
				   &ft->vars, &ft->n_vars,
				   PV_NO_SCRATCH | PV_NO_DUPLICATE | PV_NUMERIC))
    {
      return 2;
    }

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);

  specs->test[specs->n_tests - 1] = nt;

  return 1;
}


static int
npar_chisquare (struct lexer *lexer, struct dataset *ds,
		struct npar_specs *specs)
{
  struct chisquare_test *cstp = pool_alloc (specs->pool, sizeof (*cstp));
  struct one_sample_test *tp = &cstp->parent;
  struct npar_test *nt = &tp->parent;
  int retval = 1;

  nt->execute = chisquare_execute;
  nt->insert_variables = one_sample_insert_variables;

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
				   &tp->vars, &tp->n_vars,
				   PV_NO_SCRATCH | PV_NO_DUPLICATE))
    {
      return 2;
    }

  cstp->ranged = false;

  if ( lex_match (lexer, T_LPAREN))
    {
      cstp->ranged = true;
      if ( ! lex_force_num (lexer)) return 0;
      cstp->lo = lex_number (lexer);
      lex_get (lexer);
      lex_force_match (lexer, T_COMMA);
      if (! lex_force_num (lexer) ) return 0;
      cstp->hi = lex_number (lexer);
      if ( cstp->lo >= cstp->hi )
	{
	  msg (ME,
	      _("The specified value of HI (%d) is "
		"lower than the specified value of LO (%d)"),
	      cstp->hi, cstp->lo);
	  return 0;
	}
      lex_get (lexer);
      if (! lex_force_match (lexer, T_RPAREN)) return 0;
    }

  cstp->n_expected = 0;
  cstp->expected = NULL;
  if (lex_match_phrase (lexer, "/EXPECTED"))
    {
      lex_force_match (lexer, T_EQUALS);
      if ( ! lex_match_id (lexer, "EQUAL") )
        {
          double f;
          int n;
          while ( lex_is_number (lexer) )
            {
              int i;
              n = 1;
              f = lex_number (lexer);
              lex_get (lexer);
              if ( lex_match (lexer, T_ASTERISK))
                {
                  n = f;
                  f = lex_number (lexer);
                  lex_get (lexer);
                }
              lex_match (lexer, T_COMMA);

              cstp->n_expected += n;
              cstp->expected = pool_realloc (specs->pool,
                                             cstp->expected,
                                             sizeof (double) *
                                             cstp->n_expected);
              for ( i = cstp->n_expected - n ;
                    i < cstp->n_expected;
                    ++i )
                cstp->expected[i] = f;

            }
        }
    }

  if ( cstp->ranged && cstp->n_expected > 0 &&
       cstp->n_expected != cstp->hi - cstp->lo + 1 )
    {
      msg (ME,
	  _("%d expected values were given, but the specified "
	    "range (%d-%d) requires exactly %d values."),
	  cstp->n_expected, cstp->lo, cstp->hi,
	  cstp->hi - cstp->lo +1);
      return 0;
    }

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);

  specs->test[specs->n_tests - 1] = nt;

  return retval;
}


static int
npar_binomial (struct lexer *lexer, struct dataset *ds,
	       struct npar_specs *specs)
{
  struct binomial_test *btp = pool_alloc (specs->pool, sizeof (*btp));
  struct one_sample_test *tp = &btp->parent;
  struct npar_test *nt = &tp->parent;
  bool equals = false;

  nt->execute = binomial_execute;
  nt->insert_variables = one_sample_insert_variables;

  btp->category1 = btp->category2 = btp->cutpoint = SYSMIS;

  btp->p = 0.5;

  if ( lex_match (lexer, T_LPAREN) )
    {
      equals = false;
      if ( lex_force_num (lexer) )
	{
	  btp->p = lex_number (lexer);
	  lex_get (lexer);
	  lex_force_match (lexer, T_RPAREN);
	}
      else
	return 0;
    }
  else
    equals = true;

  if (equals || lex_match (lexer, T_EQUALS) )
    {
      if (parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
				      &tp->vars, &tp->n_vars,
				      PV_NUMERIC | PV_NO_SCRATCH | PV_NO_DUPLICATE) )
	{
	  if (lex_match (lexer, T_LPAREN))
	    {
	      if (! lex_force_num (lexer))
		return 2;
	      btp->category1 = lex_number (lexer);
      	      lex_get (lexer);
	      if ( lex_match (lexer, T_COMMA))
		{
		  if ( ! lex_force_num (lexer) ) return 2;
		  btp->category2 = lex_number (lexer);
		  lex_get (lexer);
		}
	      else
		{
      		  btp->cutpoint = btp->category1;
		}

	      lex_force_match (lexer, T_RPAREN);
	    }
	}
      else
	return 2;

    }

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);

  specs->test[specs->n_tests - 1] = nt;

  return 1;
}



static void
ks_one_sample_parse_params (struct lexer *lexer, struct ks_one_sample_test *kst, int params)
{
  assert (params == 1 || params == 2);

  if (lex_is_number (lexer))
    {
      kst->p[0] = lex_number (lexer);

      lex_get (lexer);
      if ( params == 2)
	{
	  lex_match (lexer, T_COMMA);
	  if (lex_force_num (lexer))
	    {
	      kst->p[1] = lex_number (lexer);
	      lex_get (lexer);
	    }
	}
    }
}

static int
npar_ks_one_sample (struct lexer *lexer, struct dataset *ds, struct npar_specs *specs)
{
  struct ks_one_sample_test *kst = pool_alloc (specs->pool, sizeof (*kst));
  struct one_sample_test *tp = &kst->parent;
  struct npar_test *nt = &tp->parent;

  nt->execute = ks_one_sample_execute;
  nt->insert_variables = one_sample_insert_variables;

  kst->p[0] = kst->p[1] = SYSMIS;

  if (! lex_force_match (lexer, T_LPAREN))
    return 2;

  if (lex_match_id (lexer, "NORMAL"))
    {
      kst->dist = KS_NORMAL;
      ks_one_sample_parse_params (lexer, kst, 2);
    }
  else if (lex_match_id (lexer, "POISSON"))
    {
      kst->dist = KS_POISSON;
      ks_one_sample_parse_params (lexer, kst, 1);
    }
  else if (lex_match_id (lexer, "UNIFORM"))
    {
      kst->dist = KS_UNIFORM;
      ks_one_sample_parse_params (lexer, kst, 2);
    }
  else if (lex_match_id (lexer, "EXPONENTIAL"))
    {
      kst->dist = KS_EXPONENTIAL;
      ks_one_sample_parse_params (lexer, kst, 1);
    }
  else
    return 2;

  if (! lex_force_match (lexer, T_RPAREN))
    return 2;

  lex_match (lexer, T_EQUALS);

  if (! parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
				  &tp->vars, &tp->n_vars,
				  PV_NUMERIC | PV_NO_SCRATCH | PV_NO_DUPLICATE) )
    return 2;

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);

  specs->test[specs->n_tests - 1] = nt;

  return 1;
}


static bool
parse_two_sample_related_test (struct lexer *lexer,
			       const struct dictionary *dict,
			       struct two_sample_test *test_parameters,
			       struct pool *pool)
{
  int n = 0;
  bool paired = false;
  bool with = false;
  const struct variable **vlist1;
  size_t n_vlist1;

  const struct variable **vlist2;
  size_t n_vlist2;

  test_parameters->parent.insert_variables = two_sample_insert_variables;

  if (!parse_variables_const_pool (lexer, pool,
				   dict,
				   &vlist1, &n_vlist1,
				   PV_NUMERIC | PV_NO_SCRATCH | PV_DUPLICATE) )
    return false;

  if ( lex_match (lexer, T_WITH))
    {
      with = true;
      if ( !parse_variables_const_pool (lexer, pool, dict,
					&vlist2, &n_vlist2,
					PV_NUMERIC | PV_NO_SCRATCH | PV_DUPLICATE) )
	return false;

      paired = (lex_match (lexer, T_LPAREN) &&
		lex_match_id (lexer, "PAIRED") && lex_match (lexer, T_RPAREN));
    }


  if ( with )
    {
      if (paired)
	{
	  if ( n_vlist1 != n_vlist2)
            {
	      msg (SE, _("PAIRED was specified but the number of variables "
		       "preceding WITH (%zu) did not match the number "
		       "following (%zu)."), n_vlist1, n_vlist2);
              return false;
            }

	  test_parameters->n_pairs = n_vlist1 ;
	}
      else
	{
	  test_parameters->n_pairs = n_vlist1 * n_vlist2;
	}
    }
  else
    {
      test_parameters->n_pairs = (n_vlist1 * (n_vlist1 - 1)) / 2 ;
    }

  test_parameters->pairs =
    pool_alloc (pool, sizeof ( variable_pair) * test_parameters->n_pairs);

  if ( with )
    {
      if (paired)
	{
	  int i;
	  assert (n_vlist1 == n_vlist2);
	  for ( i = 0 ; i < n_vlist1; ++i )
	    {
	      test_parameters->pairs[n][0] = vlist1[i];
	      test_parameters->pairs[n][1] = vlist2[i];
	      n++;
	    }
	}
      else
	{
	  int i,j;
	  for ( i = 0 ; i < n_vlist1; ++i )
	    {
	      for ( j = 0 ; j < n_vlist2; ++j )
		{
		  test_parameters->pairs[n][0] = vlist1[i];
		  test_parameters->pairs[n][1] = vlist2[j];
		  n++;
		}
	    }
	}
    }
  else
    {
      int i,j;
      for ( i = 0 ; i < n_vlist1 - 1; ++i )
	{
	  for ( j = i + 1 ; j < n_vlist1; ++j )
	    {
	      assert ( n < test_parameters->n_pairs);
	      test_parameters->pairs[n][0] = vlist1[i];
	      test_parameters->pairs[n][1] = vlist1[j];
	      n++;
	    }
	}
    }

  assert ( n == test_parameters->n_pairs);

  return true;
}


static bool
parse_n_sample_related_test (struct lexer *lexer,
			     const struct dictionary *dict,
			     struct n_sample_test *nst,
			     struct pool *pool
			     )
{
  if (!parse_variables_const_pool (lexer, pool,
				   dict,
				   &nst->vars, &nst->n_vars,
				   PV_NUMERIC | PV_NO_SCRATCH | PV_NO_DUPLICATE) )
    return false;

  if ( ! lex_force_match (lexer, T_BY))
    return false;

  nst->indep_var = parse_variable_const (lexer, dict);

  if ( ! lex_force_match (lexer, T_LPAREN))
    return false;

  value_init (&nst->val1, var_get_width (nst->indep_var));
  if ( ! parse_value (lexer, &nst->val1, nst->indep_var))
    {
      value_destroy (&nst->val1, var_get_width (nst->indep_var));
      return false;
    }

  lex_match (lexer, T_COMMA);

  value_init (&nst->val2, var_get_width (nst->indep_var));
  if ( ! parse_value (lexer, &nst->val2, nst->indep_var))
    {
      value_destroy (&nst->val2, var_get_width (nst->indep_var));
      return false;
    }

  if ( ! lex_force_match (lexer, T_RPAREN))
    return false;

  return true;
}

static int
npar_wilcoxon (struct lexer *lexer,
	       struct dataset *ds,
	       struct npar_specs *specs )
{
  struct two_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;
  nt->execute = wilcoxon_execute;

  if (!parse_two_sample_related_test (lexer, dataset_dict (ds),
				      tp, specs->pool) )
    return 0;

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);
  specs->test[specs->n_tests - 1] = nt;

  return 1;
}


static int
npar_mann_whitney (struct lexer *lexer,
	       struct dataset *ds,
	       struct npar_specs *specs )
{
  struct n_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;

  nt->insert_variables = n_sample_insert_variables;
  nt->execute = mann_whitney_execute;

  if (!parse_n_sample_related_test (lexer, dataset_dict (ds),
				    tp, specs->pool) )
    return 0;

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);
  specs->test[specs->n_tests - 1] = nt;

  return 1;
}


static int
npar_median (struct lexer *lexer,
	     struct dataset *ds,
	     struct npar_specs *specs)
{
  struct median_test *mt = pool_alloc (specs->pool, sizeof (*mt));
  struct n_sample_test *tp = &mt->parent;
  struct npar_test *nt = &tp->parent;

  mt->median = SYSMIS;

  if ( lex_match (lexer, T_LPAREN))
    {
      lex_force_num (lexer);
      mt->median = lex_number (lexer);
      lex_get (lexer);
      lex_force_match (lexer, T_RPAREN);
    }

  lex_match (lexer, T_EQUALS);

  nt->insert_variables = n_sample_insert_variables;
  nt->execute = median_execute;

  if (!parse_n_sample_related_test (lexer, dataset_dict (ds),
				    tp, specs->pool) )
    return 0;

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);
  specs->test[specs->n_tests - 1] = nt;

  return 1;
}


static int
npar_sign (struct lexer *lexer, struct dataset *ds,
	   struct npar_specs *specs)
{
  struct two_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;

  nt->execute = sign_execute;

  if (!parse_two_sample_related_test (lexer, dataset_dict (ds),
				      tp, specs->pool) )
    return 0;

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);
  specs->test[specs->n_tests - 1] = nt;

  return 1;
}


static int
npar_mcnemar (struct lexer *lexer, struct dataset *ds,
	   struct npar_specs *specs)
{
  struct two_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;

  nt->execute = mcnemar_execute;

  if (!parse_two_sample_related_test (lexer, dataset_dict (ds),
				      tp, specs->pool) )
    return 0;

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);
  specs->test[specs->n_tests - 1] = nt;

  return 1;
}


static int
npar_jonckheere_terpstra (struct lexer *lexer, struct dataset *ds,
		      struct npar_specs *specs)
{
  struct n_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;

  nt->insert_variables = n_sample_insert_variables;

  nt->execute = jonckheere_terpstra_execute;

  if (!parse_n_sample_related_test (lexer, dataset_dict (ds),
				      tp, specs->pool) )
    return 0;

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);
  specs->test[specs->n_tests - 1] = nt;

  return 1;
}

static int
npar_kruskal_wallis (struct lexer *lexer, struct dataset *ds,
		      struct npar_specs *specs)
{
  struct n_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;

  nt->insert_variables = n_sample_insert_variables;

  nt->execute = kruskal_wallis_execute;

  if (!parse_n_sample_related_test (lexer, dataset_dict (ds),
				      tp, specs->pool) )
    return 0;

  specs->n_tests++;
  specs->test = pool_realloc (specs->pool,
			      specs->test,
			      sizeof (*specs->test) * specs->n_tests);
  specs->test[specs->n_tests - 1] = nt;

  return 1;
}

static void
insert_variable_into_map (struct hmapx *var_map, const struct variable *var)
{
  size_t hash = hash_pointer (var, 0);
  struct hmapx_node *node;
  const struct variable *v = NULL;
      
  HMAPX_FOR_EACH_WITH_HASH (v, node, hash, var_map)
    {
      if ( v == var)
	return ;
    }

  hmapx_insert (var_map, CONST_CAST (struct variable *, var), hash);
}

/* Insert the variables for TEST into VAR_MAP */
static void
one_sample_insert_variables (const struct npar_test *test,
			     struct hmapx *var_map)
{
  int i;
  const struct one_sample_test *ost = UP_CAST (test, const struct one_sample_test, parent);

  for ( i = 0 ; i < ost->n_vars ; ++i )
    insert_variable_into_map (var_map, ost->vars[i]);
}


static void
two_sample_insert_variables (const struct npar_test *test,
			     struct hmapx *var_map)
{
  int i;
  const struct two_sample_test *tst = UP_CAST (test, const struct two_sample_test, parent);

  for ( i = 0 ; i < tst->n_pairs ; ++i )
    {
      variable_pair *pair = &tst->pairs[i];

      insert_variable_into_map (var_map, (*pair)[0]);
      insert_variable_into_map (var_map, (*pair)[1]);
    }
}

static void 
n_sample_insert_variables (const struct npar_test *test,
			   struct hmapx *var_map)
{
  int i;
  const struct n_sample_test *tst = UP_CAST (test, const struct n_sample_test, parent);

  for ( i = 0 ; i < tst->n_vars ; ++i )
    insert_variable_into_map (var_map, tst->vars[i]);

  insert_variable_into_map (var_map, tst->indep_var);
}


static int
npar_method (struct lexer *lexer,  struct npar_specs *specs)
{
  if ( lex_match_id (lexer, "EXACT") )
    {
      specs->exact = true;
      specs->timer = 0.0;
      if (lex_match_id (lexer, "TIMER"))
	{
	  specs->timer = 5.0;

	  if ( lex_match (lexer, T_LPAREN))
	    {
	      if ( lex_force_num (lexer) )
		{
		  specs->timer = lex_number (lexer);
		  lex_get (lexer);
		}
	      lex_force_match (lexer, T_RPAREN);
	    }
	}
    }

  return 1;
}
