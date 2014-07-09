/* PSPP - a program for statistical analysis.
   Copyright (C) 2014 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <stdlib.h>

#include "data/dataset.h"
#include "data/settings.h"
#include "data/format.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/format-parser.h"
#include "libpspp/message.h"
#include "libpspp/version.h"
#include "output/tab.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct thing
{
  const char *identifier;
  enum result_class rc;
};

extern struct fmt_spec ugly [n_RC];

static const struct thing things[] = 
  {
    {"SIGNIFICANCE", RC_PVALUE},
    {"COUNT" ,RC_WEIGHT}
  };

#define N_THINGS (sizeof (things) / sizeof (struct thing))

struct output_spec
{
  /* An array of classes */
  enum result_class *rc;

  int n_rc;

  /* The format to be applied to these classes */
  struct fmt_spec fmt;
};

int
cmd_output (struct lexer *lexer, struct dataset *ds UNUSED)
{
  int j, i;
  struct output_spec *output_specs = NULL;
  int n_os = 0;
  
  if (!lex_force_match_id (lexer, "MODIFY"))
    {
      lex_error (lexer, NULL);
      goto error;
    }

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "SELECT"))
	{
	  if (!lex_match_id (lexer, "TABLES"))
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "TABLECELLS"))
	{
	  struct output_spec *os;
	  output_specs = xrealloc (output_specs, sizeof (*output_specs) * ++n_os);
	  os = &output_specs[n_os - 1];
	  os->n_rc = 0;
	  os->rc = NULL;
	  
	  while (lex_token (lexer) != T_SLASH && 
		 lex_token (lexer) != T_ENDCMD)
	    {
	      if (lex_match_id (lexer, "SELECT"))
		{
		  lex_force_match (lexer, T_EQUALS);
		  lex_force_match (lexer, T_LBRACK);
		  
		  while (lex_token (lexer) != T_RBRACK &&
			 lex_token (lexer) != T_ENDCMD)
		    {
		      int i;
		      for (i = 0 ; i < N_THINGS; ++i)
			{
			  if (lex_match_id (lexer, things[i].identifier))
			    {
			      os->rc = xrealloc (os->rc, sizeof (*os->rc) * ++os->n_rc);
			      os->rc[os->n_rc - 1] = things[i].rc;
			      break;
			    }
			}
		      if (i >= N_THINGS)
			{
			  lex_error (lexer, _("Unknown TABLECELLS class"));
			  goto error;
			}
		    }
		  lex_force_match (lexer, T_RBRACK);
		}
	      else if (lex_match_id (lexer, "FORMAT"))
		{
		  struct fmt_spec fmt;    
		  char type[FMT_TYPE_LEN_MAX + 1];
		  int width = -1;
		  int decimals = -1;

		  lex_force_match (lexer, T_EQUALS);
		  if (! parse_abstract_format_specifier (lexer, type, &width, &decimals))
		    {
		      lex_error (lexer, NULL);
		      goto error;
		    }

		  if (width <= 0)
		    {
		      const struct fmt_spec *dflt = settings_get_format ();
		      width = dflt->w;
		    }

                  if (!fmt_from_name (type, &fmt.type))
                    {
                      lex_error (lexer, _("Unknown format type `%s'."), type);
		      goto error;
                    }

		  fmt.w = width;
		  fmt.d = decimals;

		  os->fmt = fmt;
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

  /* Populate the global table, with the values we parsed */
  for (i = 0; i < n_os; ++i)
    {
      for (j = 0; j < output_specs[i].n_rc;  ++j)
	{
	  ugly [output_specs[i].rc[j]] = output_specs[i].fmt;
	}
    }
  
  for (j = 0; j < n_os;  ++j)
    free (output_specs[j].rc);
  free (output_specs);
  return CMD_SUCCESS;
 error:

  for (j = 0; j < n_os;  ++j)
    free (output_specs[j].rc);
  free (output_specs);
  return CMD_SUCCESS;
}
