/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include <data/format.h>
#include <data/procedure.h>
#include <data/dictionary.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/format-parser.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>

#include "intprops.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_vector (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct pool *pool = pool_create ();

  do
    {
      char **vectors;
      size_t vector_cnt, vector_cap;

      /* Get the name(s) of the new vector(s). */
      if (!lex_force_id (lexer))
	return CMD_CASCADING_FAILURE;

      vectors = NULL;
      vector_cnt = vector_cap = 0;
      while (lex_token (lexer) == T_ID)
	{
          size_t i;

	  if (dict_lookup_vector (dict, lex_tokid (lexer)))
	    {
	      msg (SE, _("A vector named %s already exists."),
                   lex_tokid (lexer));
	      goto fail;
	    }

          for (i = 0; i < vector_cnt; i++)
            if (!strcasecmp (vectors[i], lex_tokid (lexer)))
	      {
		msg (SE, _("Vector name %s is given twice."),
                     lex_tokid (lexer));
		goto fail;
	      }

          if (vector_cnt == vector_cap)
            vectors = pool_2nrealloc (pool,
                                       vectors, &vector_cap, sizeof *vectors);
          vectors[vector_cnt++] = pool_strdup (pool, lex_tokid (lexer));

	  lex_get (lexer);
	  lex_match (lexer, ',');
	}

      /* Now that we have the names it's time to check for the short
         or long forms. */
      if (lex_match (lexer, '='))
	{
	  /* Long form. */
          struct variable **v;
          size_t nv;

	  if (vector_cnt > 1)
	    {
	      msg (SE, _("A slash must separate each vector "
                         "specification in VECTOR's long form."));
	      goto fail;
	    }

	  if (!parse_variables_pool (lexer, pool, dict, &v, &nv,
                                     PV_SAME_WIDTH | PV_DUPLICATE))
	    goto fail;

          dict_create_vector (dict, vectors[0], v, nv);
	}
      else if (lex_match (lexer, '('))
	{
          /* Short form. */
          struct fmt_spec format;
          bool seen_format = false;

          struct variable **vars;
          int var_cnt;

          size_t i;

          var_cnt = 0;
          format = fmt_for_output (FMT_F, 8, 2);
          seen_format = false;
          while (!lex_match (lexer, ')'))
            {
              if (lex_is_integer (lexer) && var_cnt == 0)
                {
                  var_cnt = lex_integer (lexer);
                  lex_get (lexer);
                  if (var_cnt <= 0)
                    {
                      msg (SE, _("Vectors must have at least one element."));
                      goto fail;
                    }
                }
              else if (lex_token (lexer) == T_ID && !seen_format)
                {
                  seen_format = true;
                  if (!parse_format_specifier (lexer, &format)
                      || !fmt_check_output (&format)
                      || !fmt_check_type_compat (&format, VAR_NUMERIC))
                    goto fail;
                }
              else
                {
                  lex_error (lexer, NULL);
                  goto fail;
                }
              lex_match (lexer, ',');
            }
          if (var_cnt == 0)
            {
              lex_error (lexer, _("expecting vector length"));
              goto fail;
            }

	  /* Check that none of the variables exist and that
             their names are no more than LONG_NAME_LEN bytes
             long. */
          for (i = 0; i < vector_cnt; i++)
	    {
              int j;
	      for (j = 0; j < var_cnt; j++)
		{
                  char name[LONG_NAME_LEN + INT_STRLEN_BOUND (int) + 1];
		  sprintf (name, "%s%d", vectors[i], j + 1);
                  if (strlen (name) > LONG_NAME_LEN)
                    {
                      msg (SE, _("%s is too long for a variable name."), name);
                      goto fail;
                    }
                  if (dict_lookup_var (dict, name))
		    {
		      msg (SE, _("%s is an existing variable name."), name);
		      goto fail;
		    }
		}
	    }

	  /* Finally create the variables and vectors. */
          vars = pool_nmalloc (pool, var_cnt, sizeof *vars);
          for (i = 0; i < vector_cnt; i++)
	    {
              int j;
	      for (j = 0; j < var_cnt; j++)
		{
                  char name[LONG_NAME_LEN + 1];
		  sprintf (name, "%s%d", vectors[i], j + 1);
		  vars[j] = dict_create_var_assert (dict, name, 0);
                  var_set_both_formats (vars[j], &format);
		}
              dict_create_vector_assert (dict, vectors[i], vars, var_cnt);
	    }
	}
      else
	{
          lex_error (lexer, NULL);
	  goto fail;
	}
    }
  while (lex_match (lexer, '/'));

  pool_destroy (pool);
  return lex_end_of_command (lexer);

fail:
  pool_destroy (pool);
  return CMD_FAILURE;
}
