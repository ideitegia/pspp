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

#include <config.h>

#include <stdlib.h>

#include <data/procedure.h>
#include <data/dictionary.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_vector (struct dataset *ds)
{
  /* Just to be different, points to a set of null terminated strings
     containing the names of the vectors to be created.  The list
     itself is terminated by a empty string.  So a list of three
     elements, A B C, would look like this: "A\0B\0C\0\0". */
  char *vecnames;

  /* vecnames iterators. */
  char *cp, *cp2;

  /* Maximum allocated position for vecnames, plus one position. */
  char *endp = NULL;

  struct dictionary *dict = dataset_dict (ds);

  cp = vecnames = xmalloc (256);
  endp = &vecnames[256];
  do
    {
      /* Get the name(s) of the new vector(s). */
      if (!lex_force_id ())
	return CMD_CASCADING_FAILURE;
      while (token == T_ID)
	{
	  if (cp + 16 > endp)
	    {
	      char *old_vecnames = vecnames;
	      vecnames = xrealloc (vecnames, endp - vecnames + 256);
	      cp = (cp - old_vecnames) + vecnames;
	      endp = (endp - old_vecnames) + vecnames + 256;
	    }

	  for (cp2 = cp; cp2 < cp; cp2 += strlen (cp))
	    if (!strcasecmp (cp2, tokid))
	      {
		msg (SE, _("Vector name %s is given twice."), tokid);
		goto fail;
	      }

	  if (dict_lookup_vector (dict, tokid))
	    {
	      msg (SE, _("There is already a vector with name %s."), tokid);
	      goto fail;
	    }

	  cp = stpcpy (cp, tokid) + 1;
	  lex_get ();
	  lex_match (',');
	}
      *cp++ = 0;

      /* Now that we have the names it's time to check for the short
         or long forms. */
      if (lex_match ('='))
	{
	  /* Long form. */
          struct variable **v;
          size_t nv;

	  if (strchr (vecnames, '\0')[1])
	    {
	      /* There's more than one vector name. */
	      msg (SE, _("A slash must be used to separate each vector "
                         "specification when using the long form.  Commands "
                         "such as VECTOR A,B=Q1 TO Q20 are not supported."));
	      goto fail;
	    }

	  if (!parse_variables (dict, &v, &nv,
                                PV_SAME_TYPE | PV_DUPLICATE))
	    goto fail;

          dict_create_vector (dict, vecnames, v, nv);
          free (v);
	}
      else if (lex_match ('('))
	{
	  int i;

	  /* Maximum number of digits in a number to add to the base
	     vecname. */
	  int ndig;

	  /* Name of an individual variable to be created. */
	  char name[SHORT_NAME_LEN + 1];

          /* Vector variables. */
          struct variable **v;
          int nv;

	  if (!lex_force_int ())
	    return CMD_CASCADING_FAILURE;
	  nv = lex_integer ();
	  lex_get ();
	  if (nv <= 0)
	    {
	      msg (SE, _("Vectors must have at least one element."));
	      goto fail;
	    }
	  if (!lex_force_match (')'))
	    goto fail;

	  /* First check that all the generated variable names
	     are LONG_NAME_LEN characters or shorter. */
	  ndig = intlog10 (nv);
	  for (cp = vecnames; *cp;)
	    {
	      int len = strlen (cp);
	      if (len + ndig > LONG_NAME_LEN)
		{
		  msg (SE, _("%s%d is too long for a variable name."), cp, nv);
		  goto fail;
		}
	      cp += len + 1;
	    }

	  /* Next check that none of the variables exist. */
	  for (cp = vecnames; *cp;)
	    {
	      for (i = 0; i < nv; i++)
		{
		  sprintf (name, "%s%d", cp, i + 1);
		  if (dict_lookup_var (dict, name))
		    {
		      msg (SE, _("There is already a variable named %s."),
                           name);
		      goto fail;
		    }
		}
	      cp += strlen (cp) + 1;
	    }

	  /* Finally create the variables and vectors. */
          v = xmalloc (nv * sizeof *v);
	  for (cp = vecnames; *cp;)
	    {
	      for (i = 0; i < nv; i++)
		{
		  sprintf (name, "%s%d", cp, i + 1);
		  v[i] = dict_create_var_assert (dict, name, 0);
		}
              if (!dict_create_vector (dict, cp, v, nv))
                NOT_REACHED ();
	      cp += strlen (cp) + 1;
	    }
          free (v);
	}
      else
	{
	  msg (SE, _("The syntax for this command does not match "
	       "the expected syntax for either the long form "
	       "or the short form of VECTOR."));
	  goto fail;
	}

      free (vecnames);
      vecnames = NULL;
    }
  while (lex_match ('/'));

  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      goto fail;
    }
  return CMD_SUCCESS;

fail:
  free (vecnames);
  return CMD_FAILURE;
}
