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

#include <config.h>
#include <assert.h>
#include <stdlib.h>
#include "alloc.h"
#include "cases.h"
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "misc.h"
#include "str.h"
#include "var.h"
#include "vector.h"

/* Vectors created on VECTOR. */
struct vector *vec;

/* Number of vectors in vec. */
int nvec;

int
cmd_vector (void)
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

  /* Variables on list (long form only). */
  struct variable **v = NULL;
  int nv;

  lex_match_id ("VECTOR");

  cp = vecnames = xmalloc (256);
  endp = &vecnames[256];
  do
    {
      /* Get the name(s) of the new vector(s). */
      if (!lex_force_id ())
	return CMD_FAILURE;
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
	    if (!strcmp (cp2, tokid))
	      {
		msg (SE, _("Vector name %s is given twice."), tokid);
		goto fail;
	      }

	  if (find_vector (tokid))
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

	  if (strchr (vecnames, '\0')[1])
	    {
	      /* There's more than one vector name. */
	      msg (SE, _("A slash must be used to separate each vector "
		   "specification when using the long form.  Commands "
		   "such as VECTOR A,B=Q1 TO Q20 are not supported."));
	      goto fail;
	    }

	  if (!parse_variables (NULL, &v, &nv, PV_SAME_TYPE | PV_DUPLICATE))
	    goto fail;

	  vec = xrealloc (vec, sizeof *vec * (nvec + 1));
	  vec[nvec].index = nvec;
	  strcpy (vec[nvec].name, vecnames);
	  vec[nvec].v = v;
	  vec[nvec].nv = nv;
	  nvec++;
	  v = NULL;		/* prevent block from being freed on error */
	}
      else if (lex_match ('('))
	{
	  int i;

	  /* Maximum number of digits in a number to add to the base
	     vecname. */
	  int ndig;

	  /* Name of an individual variable to be created. */
	  char name[9];

	  if (!lex_force_int ())
	    return CMD_FAILURE;
	  nv = lex_integer ();
	  lex_get ();
	  if (nv <= 0)
	    {
	      msg (SE, _("Vectors must have at least one element."));
	      goto fail;
	    }
	  if (!lex_force_match (')'))
	    goto fail;

	  /* First check that all the generated variable names are 8
	     characters or shorter. */
	  ndig = intlog10 (nv);
	  for (cp = vecnames; *cp;)
	    {
	      int len = strlen (cp);
	      if (len + ndig > 8)
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
		  if (is_varname (name))
		    {
		      msg (SE, _("There is already a variable named %s."), name);
		      goto fail;
		    }
		}
	      cp += strlen (cp) + 1;
	    }

	  /* Finally create the variables and vectors. */
	  vec = xrealloc (vec, sizeof *vec * (nvec + nv));
	  for (cp = vecnames; *cp;)
	    {
	      vec[nvec].index = nvec;
	      strcpy (vec[nvec].name, cp);
	      vec[nvec].v = xmalloc (sizeof *vec[nvec].v * nv);
	      vec[nvec].nv = nv;
	      for (i = 0; i < nv; i++)
		{
		  sprintf (name, "%s%d", cp, i + 1);
		  vec[nvec].v[i] = force_create_variable (&default_dict, name,
							  NUMERIC, 0);
		  envector (vec[nvec].v[i]);
		}
	      nvec++;
	      cp += strlen (cp) + 1;
	    }
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
  free (v);
  return CMD_PART_SUCCESS_MAYBE;
}

/* Returns a pointer to the vector with name NAME, or NULL on
   failure. */
struct vector *
find_vector (const char *name)
{
  int i;

  for (i = 0; i < nvec; i++)
    if (!strcmp (vec[i].name, name))
      return &vec[i];
  return NULL;
}
