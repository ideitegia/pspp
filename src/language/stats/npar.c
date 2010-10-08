/* PSPP - a program for statistical analysis. -*-c-*-
   Copyright (C) 2006, 2008, 2009, 2010 Free Software Foundation, Inc.

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

#include <language/stats/npar.h>
#include "npar-summary.h"

#include <stdlib.h>
#include <math.h>

#include "xalloc.h"
#include <data/case.h>
#include <data/casegrouper.h>
#include <data/casereader.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <language/stats/binomial.h>
#include <language/stats/chisquare.h>
#include <language/stats/wilcoxon.h>
#include <language/stats/sign.h>
#include <libpspp/assertion.h>
#include <libpspp/cast.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>
#include <libpspp/taint.h>
#include <math/moments.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct dataset;
/* Settings for subcommand specifiers. */
enum
  {
    NPAR_ANALYSIS,
    NPAR_LISTWISE,
  };

enum
  {
    NPAR_INCLUDE,
    NPAR_EXCLUDE
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
    /* CHISQUARE subcommand. */
    int chisquare;

    /* BINOMIAL subcommand. */
    int binomial;

    /* WILCOXON subcommand. */
    int wilcoxon;

    /* SIGN subcommand. */
    int sign;

    /* MISSING subcommand. */
    int missing;
    long miss;
    long incl;

    /* METHOD subcommand. */
    int method;

    /* STATISTICS subcommand. */
    int statistics;
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
static int npar_wilcoxon (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_sign (struct lexer *, struct dataset *, struct npar_specs *);
static int npar_method (struct lexer *, struct npar_specs *);

/* Command parsing functions. */
static int parse_npar_tests (struct lexer *lexer, struct dataset *ds, struct cmd_npar_tests *p,
			     struct npar_specs *npar_specs );

static int
parse_npar_tests (struct lexer *lexer, struct dataset *ds, struct cmd_npar_tests *npt,
		  struct npar_specs *nps)
{
  npt->chisquare = 0;
  npt->binomial = 0;
  npt->wilcoxon = 0;
  npt->sign = 0;
  npt->missing = 0;
  npt->miss = NPAR_ANALYSIS;
  npt->incl = NPAR_EXCLUDE;
  npt->method = 0;
  npt->statistics = 0;
  memset (npt->a_statistics, 0, sizeof npt->a_statistics);
  for (;;)
    {
      if (lex_match_hyphenated_word (lexer, "CHISQUARE"))
        {
          lex_match (lexer, '=');
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
            default:
              NOT_REACHED ();
            }
        }
      else if (lex_match_hyphenated_word (lexer, "BINOMIAL"))
        {
          lex_match (lexer, '=');
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
      else if (lex_match_hyphenated_word (lexer, "WILCOXON"))
        {
          lex_match (lexer, '=');
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
      else if (lex_match_hyphenated_word (lexer, "SIGN"))
        {
          lex_match (lexer, '=');
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
      else if (lex_match_hyphenated_word (lexer, "MISSING"))
        {
          lex_match (lexer, '=');
          npt->missing++;
          if (npt->missing > 1)
            {
              msg (SE, _ ("MISSING subcommand may be given only once."));
              goto lossage;
            }
          while (lex_token (lexer) != '/' && lex_token (lexer) != '.')
            {
              if (lex_match_hyphenated_word (lexer, "ANALYSIS"))
                npt->miss = NPAR_ANALYSIS;
              else if (lex_match_hyphenated_word (lexer, "LISTWISE"))
                npt->miss = NPAR_LISTWISE;
              else if (lex_match_hyphenated_word (lexer, "INCLUDE"))
                npt->incl = NPAR_INCLUDE;
              else if (lex_match_hyphenated_word (lexer, "EXCLUDE"))
                npt->incl = NPAR_EXCLUDE;
              else
                {
                  lex_error (lexer, NULL);
                  goto lossage;
                }
              lex_match (lexer, ',');
            }
        }
      else if (lex_match_hyphenated_word (lexer, "METHOD"))
        {
          lex_match (lexer, '=');
          npt->method++;
          if (npt->method > 1)
            {
              msg (SE, _ ("METHOD subcommand may be given only once."));
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
      else if (lex_match_hyphenated_word (lexer, "STATISTICS"))
        {
          lex_match (lexer, '=');
          npt->statistics++;
          while (lex_token (lexer) != '/' && lex_token (lexer) != '.')
            {
              if (lex_match_hyphenated_word (lexer, "DESCRIPTIVES"))
                npt->a_statistics[NPAR_ST_DESCRIPTIVES] = 1;
              else if (lex_match_hyphenated_word (lexer, "QUARTILES"))
                npt->a_statistics[NPAR_ST_QUARTILES] = 1;
              else if (lex_match (lexer, T_ALL))
                npt->a_statistics[NPAR_ST_ALL] = 1;
              else
                {
                  lex_error (lexer, NULL);
                  goto lossage;
                }
              lex_match (lexer, ',');
            }
        }
      else if ( settings_get_syntax () != COMPATIBLE && lex_match_id (lexer, "ALGORITHM"))
        {
          lex_match (lexer, '=');
          if (lex_match_id (lexer, "COMPATIBLE"))
            settings_set_cmd_algorithm (COMPATIBLE);
          else if (lex_match_id (lexer, "ENHANCED"))
            settings_set_cmd_algorithm (ENHANCED);
          }
        if (!lex_match (lexer, '/'))
          break;
      }

    if (lex_token (lexer) != '.')
      {
        lex_error (lexer, _ ("expecting end of command"));
        goto lossage;
      }

  return true;

lossage:
  return false;
}




static void one_sample_insert_variables (const struct npar_test *test,
					 struct const_hsh_table *variables);

static void two_sample_insert_variables (const struct npar_test *test,
					 struct const_hsh_table *variables);


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
	  msg (SW, _ ("NPAR subcommand not currently implemented."));
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
  struct const_hsh_table *var_hash;
  struct casegrouper *grouper;
  struct casereader *input, *group;

  npar_specs.pool = pool_create ();

  var_hash = const_hsh_create_pool (npar_specs.pool, 0,
				    compare_vars_by_name, hash_var_by_name,
				    NULL, NULL);

  if ( ! parse_npar_tests (lexer, ds, &cmd, &npar_specs) )
    {
      pool_destroy (npar_specs.pool);
      return CMD_FAILURE;
    }

  for (i = 0; i < npar_specs.n_tests; ++i )
    {
      const struct npar_test *test = npar_specs.test[i];
      test->insert_variables (test, var_hash);
    }

  npar_specs.vv = (const struct variable **) const_hsh_sort (var_hash);
  npar_specs.n_vars = const_hsh_count (var_hash);

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

  npar_specs.filter = cmd.incl == NPAR_EXCLUDE ? MV_ANY : MV_SYSTEM;

  input = proc_open (ds);
  if ( cmd.miss == NPAR_LISTWISE )
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

  const_hsh_destroy (var_hash);

  pool_destroy (npar_specs.pool);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

static int
npar_chisquare (struct lexer *lexer, struct dataset *ds,
		struct npar_specs *specs)
{
  struct chisquare_test *cstp = pool_alloc (specs->pool, sizeof (*cstp));
  struct one_sample_test *tp = &cstp->parent;
  struct npar_test *nt = &tp->parent;

  nt->execute = chisquare_execute;
  nt->insert_variables = one_sample_insert_variables;

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
				   &tp->vars, &tp->n_vars,
				   PV_NO_SCRATCH | PV_NO_DUPLICATE))
    {
      return 2;
    }

  cstp->ranged = false;

  if ( lex_match (lexer, '('))
    {
      cstp->ranged = true;
      if ( ! lex_force_num (lexer)) return 0;
      cstp->lo = lex_integer (lexer);
      lex_get (lexer);
      lex_force_match (lexer, ',');
      if (! lex_force_num (lexer) ) return 0;
      cstp->hi = lex_integer (lexer);
      if ( cstp->lo >= cstp->hi )
	{
	  msg (ME,
	      _ ("The specified value of HI (%d) is "
		"lower than the specified value of LO (%d)"),
	      cstp->hi, cstp->lo);
	  return 0;
	}
      lex_get (lexer);
      if (! lex_force_match (lexer, ')')) return 0;
    }

  cstp->n_expected = 0;
  cstp->expected = NULL;
  if ( lex_match (lexer, '/') )
    {
      if ( lex_match_id (lexer, "EXPECTED") )
	{
	  lex_force_match (lexer, '=');
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
		  if ( lex_match (lexer, '*'))
		    {
		      n = f;
		      f = lex_number (lexer);
		      lex_get (lexer);
		    }
		  lex_match (lexer, ',');

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
      else
	lex_put_back (lexer, '/');
    }

  if ( cstp->ranged && cstp->n_expected > 0 &&
       cstp->n_expected != cstp->hi - cstp->lo + 1 )
    {
      msg (ME,
	  _ ("%d expected values were given, but the specified "
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

  return 1;
}


static int
npar_binomial (struct lexer *lexer, struct dataset *ds,
	       struct npar_specs *specs)
{
  struct binomial_test *btp = pool_alloc (specs->pool, sizeof (*btp));
  struct one_sample_test *tp = &btp->parent;
  struct npar_test *nt = &tp->parent;

  nt->execute = binomial_execute;
  nt->insert_variables = one_sample_insert_variables;

  btp->category1 = btp->category2 = btp->cutpoint = SYSMIS;

  btp->p = 0.5;

  if ( lex_match (lexer, '(') )
    {
      if ( lex_force_num (lexer) )
	{
	  btp->p = lex_number (lexer);
	  lex_get (lexer);
	  lex_force_match (lexer, ')');
	}
      else
	return 0;
    }
  else
    /* Kludge: q2c swallows the '=' so put it back here  */
     lex_put_back (lexer, '=');

  if (lex_match (lexer, '=') )
    {
      if (parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
				      &tp->vars, &tp->n_vars,
				      PV_NUMERIC | PV_NO_SCRATCH | PV_NO_DUPLICATE) )
	{
	  if (lex_match (lexer, '('))
	    {
	      lex_force_num (lexer);
	      btp->category1 = lex_number (lexer);
      	      lex_get (lexer);
	      if ( lex_match (lexer, ','))
		{
		  if ( ! lex_force_num (lexer) ) return 2;
		  btp->category2 = lex_number (lexer);
		  lex_get (lexer);
		}
	      else
		{
      		  btp->cutpoint = btp->category1;
		}

	      lex_force_match (lexer, ')');
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


static bool
parse_two_sample_related_test (struct lexer *lexer,
				    const struct dictionary *dict,
				    struct two_sample_test *test_parameters,
				    struct pool *pool
				    );


static bool
parse_two_sample_related_test (struct lexer *lexer,
			       const struct dictionary *dict,
			       struct two_sample_test *test_parameters,
			       struct pool *pool
			       )
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
				   PV_NUMERIC | PV_NO_SCRATCH | PV_NO_DUPLICATE) )
    return false;

  if ( lex_match (lexer, T_WITH))
    {
      with = true;
      if ( !parse_variables_const_pool (lexer, pool, dict,
					&vlist2, &n_vlist2,
					PV_NUMERIC | PV_NO_SCRATCH | PV_NO_DUPLICATE) )
	return false;

      paired = (lex_match (lexer, '(') &&
		lex_match_id (lexer, "PAIRED") && lex_match (lexer, ')'));
    }


  if ( with )
    {
      if (paired)
	{
	  if ( n_vlist1 != n_vlist2)
	    msg (SE, _ ("PAIRED was specified but the number of variables "
		       "preceding WITH (%zu) did not match the number "
		       "following (%zu)."), n_vlist1, n_vlist2);

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
	      test_parameters->pairs[n][1] = vlist1[i];
	      test_parameters->pairs[n][0] = vlist2[i];
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
		  test_parameters->pairs[n][1] = vlist1[i];
		  test_parameters->pairs[n][0] = vlist2[j];
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
	      test_parameters->pairs[n][1] = vlist1[i];
	      test_parameters->pairs[n][0] = vlist1[j];
	      n++;
	    }
	}
    }

  assert ( n == test_parameters->n_pairs);

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

/* Insert the variables for TEST into VAR_HASH */
static void
one_sample_insert_variables (const struct npar_test *test,
			     struct const_hsh_table *var_hash)
{
  int i;
  struct one_sample_test *ost = UP_CAST (test, struct one_sample_test, parent);

  for ( i = 0 ; i < ost->n_vars ; ++i )
    const_hsh_insert (var_hash, ost->vars[i]);
}

static void
two_sample_insert_variables (const struct npar_test *test,
			     struct const_hsh_table *var_hash)
{
  int i;

  const struct two_sample_test *tst = (const struct two_sample_test *) test;

  for ( i = 0 ; i < tst->n_pairs ; ++i )
    {
      variable_pair *pair = &tst->pairs[i];

      const_hsh_insert (var_hash, (*pair)[0]);
      const_hsh_insert (var_hash, (*pair)[1]);
    }
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

	  if ( lex_match (lexer, '('))
	    {
	      if ( lex_force_num (lexer) )
		{
		  specs->timer = lex_number (lexer);
		  lex_get (lexer);
		}
	      lex_force_match (lexer, ')');
	    }
	}
    }

  return 1;
}
