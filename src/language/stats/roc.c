/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

#include "roc.h"
#include <data/procedure.h>
#include <language/lexer/variable-parser.h>
#include <language/lexer/value-parser.h>
#include <language/lexer/lexer.h>

#include <data/casegrouper.h>
#include <data/casereader.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct cmd_roc
{
  size_t n_vars;
  const struct variable **vars;

  struct variable *state_var ;
  union value state_value;

  /* Plot the roc curve */
  bool curve;
  /* Plot the reference line */
  bool reference;

  double ci;

  bool print_coords;
  bool print_se;
  bool bi_neg_exp; /* True iff the bi-negative exponential critieria
		      should be used */
  enum mv_class exclude;

  bool invert ; /* True iff a smaller test result variable indicates
		   a positive result */

};

static int run_roc (struct dataset *ds, struct cmd_roc *roc);

int
cmd_roc (struct lexer *lexer, struct dataset *ds)
{
  struct cmd_roc roc ;
  const struct dictionary *dict = dataset_dict (ds);

  roc.vars = NULL;
  roc.n_vars = 0;
  roc.print_se = false;
  roc.print_coords = false;
  roc.exclude = MV_ANY;
  roc.curve = true;
  roc.reference = false;
  roc.ci = 95;
  roc.bi_neg_exp = false;
  roc.invert = false;

  if (!parse_variables_const (lexer, dict, &roc.vars, &roc.n_vars,
			      PV_APPEND | PV_NO_DUPLICATE | PV_NUMERIC))
    return 2;

  if ( ! lex_force_match (lexer, T_BY))
    {
      return 2;
    }

  roc.state_var = parse_variable (lexer, dict);

  if ( !lex_force_match (lexer, '('))
    {
      return 2;
    }

  parse_value (lexer, &roc.state_value, var_get_width (roc.state_var));


  if ( !lex_force_match (lexer, ')'))
    {
      return 2;
    }


  while (lex_token (lexer) != '.')
    {
      lex_match (lexer, '/');
      if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, '=');
          while (lex_token (lexer) != '.' && lex_token (lexer) != '/')
            {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  roc.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  roc.exclude = MV_ANY;
		}
	      else
		{
                  lex_error (lexer, NULL);
		  return 2;
		}
	    }
	}
      else if (lex_match_id (lexer, "PLOT"))
	{
	  lex_match (lexer, '=');
	  if (lex_match_id (lexer, "CURVE"))
	    {
	      roc.curve = true;
	      if (lex_match (lexer, '('))
		{
		  roc.reference = true;
		  lex_force_match_id (lexer, "REFERENCE");
		  lex_force_match (lexer, ')');
		}
	    }
	  else if (lex_match_id (lexer, "NONE"))
	    {
	      roc.curve = false;
	    }
	  else
	    {
	      lex_error (lexer, NULL);
	      return 2;
	    }
	}
      else if (lex_match_id (lexer, "PRINT"))
	{
	  lex_match (lexer, '=');
          while (lex_token (lexer) != '.' && lex_token (lexer) != '/')
	    {
	      if (lex_match_id (lexer, "SE"))
		{
		  roc.print_se = true;
		}
	      else if (lex_match_id (lexer, "COORDINATES"))
		{
		  roc.print_coords = true;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  return 2;
		}
	    }
	}
      else if (lex_match_id (lexer, "CRITERIA"))
	{
	  lex_match (lexer, '=');
          while (lex_token (lexer) != '.' && lex_token (lexer) != '/')
	    {
	      if (lex_match_id (lexer, "CUTOFF"))
		{
		  lex_force_match (lexer, '(');
		  if (lex_match_id (lexer, "INCLUDE"))
		    {
		      roc.exclude = MV_SYSTEM;
		    }
		  else if (lex_match_id (lexer, "EXCLUDE"))
		    {
		      roc.exclude = MV_USER | MV_SYSTEM;
		    }
		  else
		    {
		      lex_error (lexer, NULL);
		      return 2;
		    }
		  lex_force_match (lexer, ')');
		}
	      else if (lex_match_id (lexer, "TESTPOS"))
		{
		  lex_force_match (lexer, '(');
		  if (lex_match_id (lexer, "LARGE"))
		    {
		      roc.invert = false;
		    }
		  else if (lex_match_id (lexer, "SMALL"))
		    {
		      roc.invert = true;
		    }
		  else
		    {
		      lex_error (lexer, NULL);
		      return 2;
		    }
		  lex_force_match (lexer, ')');
		}
	      else if (lex_match_id (lexer, "CI"))
		{
		  lex_force_match (lexer, '(');
		  lex_force_num (lexer);
		  roc.ci = lex_number (lexer);
		  lex_get (lexer);
		  lex_force_match (lexer, ')');
		}
	      else if (lex_match_id (lexer, "DISTRIBUTION"))
		{
		  lex_force_match (lexer, '(');
		  if (lex_match_id (lexer, "FREE"))
		    {
		      roc.bi_neg_exp = false;
		    }
		  else if (lex_match_id (lexer, "NEGEXPO"))
		    {
		      roc.bi_neg_exp = true;
		    }
		  else
		    {
		      lex_error (lexer, NULL);
		      return 2;
		    }
		  lex_force_match (lexer, ')');
		}
	      else
		{
		  lex_error (lexer, NULL);
		  return 2;
		}
	    }
	}
      else
	{
	  lex_error (lexer, NULL);
	  break;
	}
    }

  run_roc (ds, &roc);

  return 1;
}




static void
do_roc (struct cmd_roc *roc, struct casereader *group, struct dictionary *dict);


static int
run_roc (struct dataset *ds, struct cmd_roc *roc)
{
  struct dictionary *dict = dataset_dict (ds);
  bool ok;
  struct casereader *group;

  struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), dict);
  while (casegrouper_get_next_group (grouper, &group))
    {
      do_roc (roc, group, dataset_dict (ds));
      casereader_destroy (group);
    }
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  return ok;
}


static void
do_roc (struct cmd_roc *roc, struct casereader *group, struct dictionary *dict)
{
}

