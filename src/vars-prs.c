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

/* AIX requires this to be the first thing in the file.  */
#include <config.h>
#if __GNUC__
#define alloca __builtin_alloca
#else
#if HAVE_ALLOCA_H
#include <alloca.h>
#else
#ifdef _AIX
#pragma alloca
#else
#ifndef alloca			/* predefined by HP cc +Olibcalls */
char *alloca ();
#endif
#endif
#endif
#endif

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include "alloc.h"
#include "bitvector.h"
#include "error.h"
#include "hash.h"
#include "lexer.h"
#include "misc.h"
#include "str.h"
#include "var.h"

/* Allocates an array at *V to contain all the variables in
   default_dict.  If FV_NO_SYSTEM is set in FLAGS then system
   variables will not be included.  If FV_NO_SCRATCH is set in FLAGS
   then scratch variables will not be included.  *C is set to the
   number of variables in *V. */
void
fill_all_vars (struct variable ***varlist, int *c, int flags)
{
  int i;

  *varlist = xmalloc (default_dict.nvar * sizeof **varlist);
  if (flags == FV_NONE)
    {
      *c = default_dict.nvar;
      for (i = 0; i < default_dict.nvar; i++)
	(*varlist)[i] = default_dict.var[i];
    }
  else
    {
      *c = 0;
      
      for (i = 0; i < default_dict.nvar; i++)
	{
	  struct variable *v = default_dict.var[i];

	  if ((flags & FV_NO_SYSTEM) && v->name[0] == '$')
	    continue;
	  if ((flags & FV_NO_SCRATCH) && v->name[0] == '#')
	    continue;
	  
	  (*varlist)[*c] = v;
	  (*c)++;
	}
      
      if (*c != default_dict.nvar)
	*varlist = xrealloc (*varlist, *c * sizeof **varlist);
    }
}

int
is_varname (const char *s)
{
  return hsh_find (default_dict.name_tab, s) != NULL;
}

int
is_dict_varname (const struct dictionary *dict, const char *s)
{
  return hsh_find (dict->name_tab, s) != NULL;
}

struct variable *
parse_variable (void)
{
  struct variable *vp;

  if (token != T_ID)
    {
      lex_error ("expecting variable name");
      return NULL;
    }
  vp = find_variable (tokid);
  if (!vp)
    msg (SE, _("%s is not declared as a variable."), tokid);
  lex_get ();
  return vp;
}

struct variable *
parse_dict_variable (struct dictionary * dict)
{
  struct variable *vp;

  if (token != T_ID)
    {
      lex_error ("expecting variable name");
      return NULL;
    }

  vp = hsh_find (dict->name_tab, tokid);
  if (!vp)
    msg (SE, _("%s is not a variable name."), tokid);
  lex_get ();

  return vp;
}

/* Returns the dictionary class of an identifier based on its
   first letter: `X' if is an ordinary identifier, `$' if it
   designates a system variable, `#' if it designates a scratch
   variable. */
#define id_dict(C) 					\
	((C) == '$' ? '$' : ((C) == '#' ? '#' : 'X'))

/* FIXME: One interesting variation in the case of PV_APPEND would be
   to keep the bitmap, reducing time required to an actual O(n log n)
   instead of having to reproduce the bitmap *every* *single* *time*.
   Later though.  (Another idea would be to keep a marker bit in each
   variable.) */
/* Note that if parse_variables() returns 0, *v is free()'d.
   Conversely, if parse_variables() returns non-zero, then *nv is
   nonzero and *v is non-NULL. */
int
parse_variables (struct dictionary * dict, struct variable *** v, int *nv, int pv_opts)
{
  int i;
  int nbytes;
  unsigned char *bits;

  struct variable *v1, *v2;
  int count, mv;
  int scratch;			/* Dictionary we're reading from. */
  int delayed_fail = 0;

  if (dict == NULL)
    dict = &default_dict;

  if (!(pv_opts & PV_APPEND))
    {
      *v = NULL;
      *nv = 0;
      mv = 0;
    }
  else
    mv = *nv;

#if GLOBAL_DEBUGGING
  {
    int corrupt = 0;
    int i;

    for (i = 0; i < dict->nvar; i++)
      if (dict->var[i]->index != i)
	{
	  printf ("%s index corruption: variable %s\n",
		  dict == &default_dict ? "default_dict" : "aux dict",
		  dict->var[i]->name);
	  corrupt = 1;
	}
    
    assert (!corrupt);
  }
#endif

  nbytes = DIV_RND_UP (dict->nvar, 8);
  if (!(pv_opts & PV_DUPLICATE))
    {
      bits = local_alloc (nbytes);
      memset (bits, 0, nbytes);
      for (i = 0; i < *nv; i++)
	SET_BIT (bits, (*v)[i]->index);
    }

  do
    {
      if (lex_match (T_ALL))
	{
	  v1 = dict->var[0];
	  v2 = dict->var[dict->nvar - 1];
	  count = dict->nvar;
	  scratch = id_dict ('X');
	}
      else
	{
	  v1 = parse_dict_variable (dict);
	  if (!v1)
	    goto fail;

	  if (lex_match (T_TO))
	    {
	      v2 = parse_dict_variable (dict);
	      if (!v2)
		{
		  lex_error ("expecting variable name");
		  goto fail;
		}

	      count = v2->index - v1->index + 1;
	      if (count < 1)
		{
		  msg (SE, _("%s TO %s is not valid syntax since %s "
		       "precedes %s in the dictionary."),
		       v1->name, v2->name, v2->name, v1->name);
		  goto fail;
		}

	      scratch = id_dict (v1->name[0]);
	      if (scratch != id_dict (v2->name[0]))
		{
		  msg (SE, _("When using the TO keyword to specify several "
		       "variables, both variables must be from "
		       "the same variable dictionaries, of either "
		       "ordinary, scratch, or system variables.  "
		       "%s and %s are from different dictionaries."),
		       v1->name, v2->name);
		  goto fail;
		}
	    }
	  else
	    {
	      v2 = v1;
	      count = 1;
	      scratch = id_dict (v1->name[0]);
	    }
	  if (scratch == id_dict ('#') && (pv_opts & PV_NO_SCRATCH))
	    {
	      msg (SE, _("Scratch variables (such as %s) are not allowed "
			 "here."), v1->name);
	      goto fail;
	    }
	}

      if (*nv + count > mv)
	{
	  mv += ROUND_UP (count, 16);
	  *v = xrealloc (*v, mv * sizeof **v);
	}

      for (i = v1->index; i <= v2->index; i++)
	{
	  struct variable *add = dict->var[i];

	  /* Skip over other dictionaries. */
	  if (scratch != id_dict (add->name[0]))
	    continue;

	  if ((pv_opts & PV_NUMERIC) && add->type != NUMERIC)
	    {
	      delayed_fail = 1;
	      msg (SW, _("%s is not a numeric variable.  It will not be "
			 "included in the variable list."), add->name);
	    }
	  else if ((pv_opts & PV_STRING) && add->type != ALPHA)
	    {
	      delayed_fail = 1;
	      msg (SE, _("%s is not a string variable.  It will not be "
			 "included in the variable list."), add->name);
	    }
	  else if ((pv_opts & PV_SAME_TYPE) && *nv && add->type != (*v)[0]->type)
	    {
	      delayed_fail = 1;
	      msg (SE, _("%s and %s are not the same type.  All variables in "
			 "this variable list must be of the same type.  %s "
			 "will be omitted from list."),
		   (*v)[0]->name, add->name, add->name);
	    }
	  else if ((pv_opts & PV_NO_DUPLICATE) && TEST_BIT (bits, add->index))
	    {
	      delayed_fail = 1;
	      msg (SE, _("Variable %s appears twice in variable list."),
		   add->name);
	    }
	  else if ((pv_opts & PV_DUPLICATE) || !TEST_BIT (bits, add->index))
	    {
	      (*v)[(*nv)++] = dict->var[i];
	      if (!(pv_opts & PV_DUPLICATE))
		SET_BIT (bits, add->index);
	    }
	}

      if (pv_opts & PV_SINGLE)
	{
	  if (delayed_fail)
	    goto fail;
	  else
	    return 1;
	}
      lex_match (',');
    }
  while ((token == T_ID && is_dict_varname (dict, tokid)) || token == T_ALL);

  if (!(pv_opts & PV_DUPLICATE))
    local_free (bits);
  if (!nv)
    goto fail;
  return 1;

fail:
  free (*v);
  *v = NULL;
  *nv = 0;
  if (!(pv_opts & PV_DUPLICATE))
    local_free (bits);
  return 0;
}

static int
extract_num (char *s, char *r, int *n, int *d)
{
  char *cp;

  /* Find first digit. */
  cp = s + strlen (s) - 1;
  while (isdigit ((unsigned char) *cp) && cp > s)
    cp--;
  cp++;

  /* Extract root. */
  strncpy (r, s, cp - s);
  r[cp - s] = 0;

  /* Count initial zeros. */
  *n = *d = 0;
  while (*cp == '0')
    {
      (*d)++;
      cp++;
    }

  /* Extract value. */
  while (isdigit ((unsigned char) *cp))
    {
      (*d)++;
      *n = (*n * 10) + (*cp - '0');
      cp++;
    }

  /* Sanity check. */
  if (*n == 0 && *d == 0)
    {
      msg (SE, _("incorrect use of TO convention"));
      return 0;
    }
  return 1;
}

/* Parses a list of variable names according to the DATA LIST version
   of the TO convention.  */
int
parse_DATA_LIST_vars (char ***names, int *nnames, int pv_opts)
{
  int n1, n2;
  int d1, d2;
  int n;
  int nvar, mvar;
  char *name1, *name2;
  char *root1, *root2;
  int success = 0;

  if (pv_opts & PV_APPEND)
    nvar = mvar = *nnames;
  else
    {
      nvar = mvar = 0;
      *names = NULL;
    }

  name1 = xmalloc (36);
  name2 = &name1[1 * 9];
  root1 = &name1[2 * 9];
  root2 = &name1[3 * 9];
  do
    {
      if (token != T_ID)
	{
	  lex_error ("expecting variable name");
	  goto fail;
	}
      if (tokid[0] == '#' && (pv_opts & PV_NO_SCRATCH))
	{
	  msg (SE, _("Scratch variables not allowed here."));
	  goto fail;
	}
      strcpy (name1, tokid);
      lex_get ();
      if (token == T_TO)
	{
	  lex_get ();
	  if (token != T_ID)
	    {
	      lex_error ("expecting variable name");
	      goto fail;
	    }
	  strcpy (name2, tokid);
	  lex_get ();

	  if (!extract_num (name1, root1, &n1, &d1)
	      || !extract_num (name2, root2, &n2, &d2))
	    goto fail;

	  if (strcmp (root1, root2))
	    {
	      msg (SE, _("Prefixes don't match in use of TO convention."));
	      goto fail;
	    }
	  if (n1 > n2)
	    {
	      msg (SE, _("Bad bounds in use of TO convention."));
	      goto fail;
	    }
	  if (d2 > d1)
	    d2 = d1;

	  if (mvar < nvar + (n2 - n1 + 1))
	    {
	      mvar += ROUND_UP (n2 - n1 + 1, 16);
	      *names = xrealloc (*names, mvar * sizeof **names);
	    }

	  for (n = n1; n <= n2; n++)
	    {
	      (*names)[nvar] = xmalloc (9);
	      sprintf ((*names)[nvar], "%s%0*d", root1, d1, n);
	      nvar++;
	    }
	}
      else
	{
	  if (nvar >= mvar)
	    {
	      mvar += 16;
	      *names = xrealloc (*names, mvar * sizeof **names);
	    }
	  (*names)[nvar++] = xstrdup (name1);
	}

      lex_match (',');

      if (pv_opts & PV_SINGLE)
	break;
    }
  while (token == T_ID);
  success = 1;

fail:
  *nnames = nvar;
  free (name1);
  if (!success)
    {
      int i;
      for (i = 0; i < nvar; i++)
	free ((*names)[i]);
      free (*names);
      *names = NULL;
      *nnames = 0;
    }
  return success;
}

/* Parses a list of variables where some of the variables may be
   existing and the rest are to be created.  Same args as
   parse_variables(). */
int
parse_mixed_vars (char ***names, int *nnames, int pv_opts)
{
  int i;

  if (!(pv_opts & PV_APPEND))
    {
      *names = NULL;
      *nnames = 0;
    }
  while (token == T_ID || token == T_ALL)
    {
      if (token == T_ALL || is_varname (tokid))
	{
	  struct variable **v;
	  int nv;

	  if (!parse_variables (NULL, &v, &nv, PV_NONE))
	    goto fail;
	  *names = xrealloc (*names, (*nnames + nv) * sizeof **names);
	  for (i = 0; i < nv; i++)
	    (*names)[*nnames + i] = xstrdup (v[i]->name);
	  free (v);
	  *nnames += nv;
	}
      else if (!parse_DATA_LIST_vars (names, nnames, PV_APPEND))
	goto fail;
    }
  return 1;

fail:
  for (i = 0; i < *nnames; i++)
    free ((*names)[*nnames]);
  free (names);
  *names = NULL;
  *nnames = 0;
  return 0;
}
