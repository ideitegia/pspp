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
#include "var.h"
#include <ctype.h>
#include <stdlib.h>
#include "alloc.h"
#include "bitvector.h"
#include "error.h"
#include "hash.h"
#include "lexer.h"
#include "misc.h"
#include "str.h"

/* Parses a name as a variable within VS and returns the variable
   if successful.  On failure emits an error message and returns
   a null pointer. */
static struct variable *
parse_vs_variable (struct var_set *vs)
{
  struct variable *vp;

  if (token != T_ID)
    {
      lex_error ("expecting variable name");
      return NULL;
    }

  vp = var_set_lookup_var (vs, tokid);
  if (vp == NULL)
    msg (SE, _("%s is not a variable name."), tokid);
  lex_get ();

  return vp;
}

/* Parses a variable name in dictionary D and returns the
   variable if successful.  On failure emits an error message and
   returns a null pointer. */
struct variable *
parse_dict_variable (struct dictionary *d) 
{
  struct var_set *vs = var_set_create_from_dict (d);
  struct variable *var = parse_vs_variable (vs);
  var_set_destroy (vs);
  return var;
}

/* Parses a variable name in default_dict and returns the
   variable if successful.  On failure emits an error message and
   returns a null pointer. */
struct variable *
parse_variable (void)
{
  return parse_dict_variable (default_dict);
}

/* Returns the dictionary class corresponding to a variable named
   NAME. */
enum dict_class
dict_class_from_id (const char *name) 
{
  assert (name != NULL);

  switch (name[0]) 
    {
    default:
      return DC_ORDINARY;
    case '$':
      return DC_SYSTEM;
    case '#':
      return DC_SCRATCH;
    }
}

/* Returns the name of dictionary class DICT_CLASS. */
const char *
dict_class_to_name (enum dict_class dict_class) 
{
  switch (dict_class) 
    {
    case DC_ORDINARY:
      return _("ordinary");
    case DC_SYSTEM:
      return _("system");
    case DC_SCRATCH:
      return _("scratch");
    default:
      assert (0);
    }
}

/* Parses a set of variables from dictionary D given options
   OPTS.  Resulting list of variables stored in *VAR and the
   number of variables into *CNT. */
int
parse_variables (struct dictionary *d, struct variable ***var, int *cnt,
                 int opts) 
{
  struct var_set *vs;
  int success;

  assert (d != NULL);
  assert (var != NULL);
  assert (cnt != NULL);

  vs = var_set_create_from_dict (d);
  success = parse_var_set_vars (vs, var, cnt, opts);
  var_set_destroy (vs);
  return success;
}

/* Note that if parse_variables() returns 0, *v is free()'d.
   Conversely, if parse_variables() returns non-zero, then *nv is
   nonzero and *v is non-NULL. */
int
parse_var_set_vars (struct var_set *vs, 
                    struct variable ***v, int *nv,
                    int pv_opts)
{
  size_t vs_var_cnt;
  int i;
  char *included = NULL;

  struct variable *v1, *v2;
  int count, mv;
  enum dict_class dict_class;

  assert (vs != NULL);
  assert (v != NULL);
  assert (nv != NULL);

  /* At most one of PV_NUMERIC, PV_STRING, PV_SAME_TYPE may be
     specified. */
  assert ((((pv_opts & PV_NUMERIC) != 0)
           + ((pv_opts & PV_STRING) != 0)
           + ((pv_opts & PV_SAME_TYPE) != 0)) <= 1);

  /* PV_DUPLICATE and PV_NO_DUPLICATE are incompatible. */
  assert (!(pv_opts & PV_DUPLICATE) || !(pv_opts & PV_NO_DUPLICATE));

  vs_var_cnt = var_set_get_cnt (vs);

  if (!(pv_opts & PV_APPEND))
    {
      *v = NULL;
      *nv = 0;
      mv = 0;
    }
  else
    mv = *nv;

  if (!(pv_opts & PV_DUPLICATE))
    {
      included = xmalloc (vs_var_cnt);
      memset (included, 0, vs_var_cnt);
      for (i = 0; i < *nv; i++)
        included[(*v)[i]->index] = 1;
    }

  do
    {
      if (lex_match (T_ALL))
	{
	  v1 = var_set_get_var (vs, 0);
	  v2 = var_set_get_var (vs, vs_var_cnt - 1);
	  count = vs_var_cnt;
	  dict_class = DC_ORDINARY;
	}
      else
	{
	  v1 = parse_vs_variable (vs);
	  if (!v1)
	    goto fail;

	  if (lex_match (T_TO))
	    {
              enum dict_class dict_class_2;

	      v2 = parse_vs_variable (vs);
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

              dict_class = dict_class_from_id (v1->name);
              dict_class_2 = dict_class_from_id (v2->name);
	      if (dict_class != dict_class_2)
		{
		  msg (SE, _("When using the TO keyword to specify several "
                             "variables, both variables must be from "
                             "the same variable dictionaries, of either "
                             "ordinary, scratch, or system variables.  "
                             "%s is a %s variable, whereas %s is %s."),
		       v1->name, dict_class_to_name (dict_class),
                       v2->name, dict_class_to_name (dict_class_2));
		  goto fail;
		}
	    }
	  else
	    {
	      v2 = v1;
	      count = 1;
	      dict_class = dict_class_from_id (v1->name);
	    }
	  if (dict_class == DC_SCRATCH && (pv_opts & PV_NO_SCRATCH))
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

      /* Add v1...v2 to the list. */
      for (i = v1->index; i <= v2->index; i++)
	{
	  struct variable *add = var_set_get_var (vs, i);

	  /* Skip over other dictionaries. */
	  if (dict_class != dict_class_from_id (add->name))
	    continue;

          /* Different kinds of errors. */
	  if ((pv_opts & PV_NUMERIC) && add->type != NUMERIC)
            msg (SW, _("%s is not a numeric variable.  It will not be "
                       "included in the variable list."), add->name);
	  else if ((pv_opts & PV_STRING) && add->type != ALPHA)
            msg (SE, _("%s is not a string variable.  It will not be "
                       "included in the variable list."), add->name);
	  else if ((pv_opts & PV_SAME_TYPE) && *nv
                   && add->type != (*v)[0]->type)
            msg (SE, _("%s and %s are not the same type.  All variables in "
                       "this variable list must be of the same type.  %s "
                       "will be omitted from list."),
                 (*v)[0]->name, add->name, add->name);
	  else if ((pv_opts & PV_NO_DUPLICATE) && included[add->index])
            msg (SE, _("Variable %s appears twice in variable list."),
                 add->name);
	  else {
            /* Success--add the variable to the list. */
            if ((pv_opts & PV_DUPLICATE) || !included[add->index])
              {
                (*v)[(*nv)++] = var_set_get_var (vs, i);
                if (!(pv_opts & PV_DUPLICATE))
                  included[add->index] = 1;
              }

            /* Next. */
            continue;
          }

          /* Arrive here only on failure. */
          if (pv_opts & PV_SINGLE)
            goto fail;
	}

      /* We finished adding v1...v2 to the list. */
      if (pv_opts & PV_SINGLE)
        return 1;
      lex_match (',');
    }
  while ((token == T_ID && var_set_lookup_var (vs, tokid) != NULL)
         || token == T_ALL);

  if (!(pv_opts & PV_DUPLICATE))
    free (included);
  if (!*nv)
    goto fail;
  return 1;

fail:
  free (*v);
  *v = NULL;
  *nv = 0;
  if (!(pv_opts & PV_DUPLICATE))
    free (included);
  return 0;
}

/* Extracts a numeric suffix from variable name S, copying it
   into string R.  Sets *D to the length of R and *N to its
   value. */
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

  assert (names != NULL);
  assert (nnames != NULL);
  assert ((pv_opts & ~(PV_APPEND | PV_SINGLE
                       | PV_NO_SCRATCH | PV_NO_DUPLICATE)) == 0);
  /* FIXME: PV_NO_DUPLICATE is not implemented. */

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
      if (dict_class_from_id (tokid) == DC_SCRATCH
          && (pv_opts & PV_NO_SCRATCH))
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
   parse_DATA_LIST_vars(). */
int
parse_mixed_vars (char ***names, int *nnames, int pv_opts)
{
  int i;

  assert (names != NULL);
  assert (nnames != NULL);
  assert ((pv_opts & ~PV_APPEND) == 0);

  if (!(pv_opts & PV_APPEND))
    {
      *names = NULL;
      *nnames = 0;
    }
  while (token == T_ID || token == T_ALL)
    {
      if (token == T_ALL || dict_lookup_var (default_dict, tokid) != NULL)
	{
	  struct variable **v;
	  int nv;

	  if (!parse_variables (default_dict, &v, &nv, PV_NONE))
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

/* A set of variables. */
struct var_set 
  {
    size_t (*get_cnt) (struct var_set *);
    struct variable *(*get_var) (struct var_set *, size_t idx);
    struct variable *(*lookup_var) (struct var_set *, const char *);
    void (*destroy) (struct var_set *);
    void *aux;
  };

/* Returns the number of variables in VS. */
size_t
var_set_get_cnt (struct var_set *vs) 
{
  assert (vs != NULL);

  return vs->get_cnt (vs);
}

/* Return variable with index IDX in VS.
   IDX must be less than the number of variables in VS. */
struct variable *
var_set_get_var (struct var_set *vs, size_t idx) 
{
  assert (vs != NULL);
  assert (idx < var_set_get_cnt (vs));

  return vs->get_var (vs, idx);
}

/* Returns the variable in VS named NAME, or a null pointer if VS
   contains no variable with that name. */
struct variable *
var_set_lookup_var (struct var_set *vs, const char *name) 
{
  assert (vs != NULL);
  assert (name != NULL);
  assert (strlen (name) < 9);

  return vs->lookup_var (vs, name);
}

/* Destroys VS. */
void
var_set_destroy (struct var_set *vs) 
{
  if (vs != NULL)
    vs->destroy (vs);
}

/* Returns the number of variables in VS. */
static size_t
dict_var_set_get_cnt (struct var_set *vs) 
{
  struct dictionary *d = vs->aux;

  return dict_get_var_cnt (d);
}

/* Return variable with index IDX in VS.
   IDX must be less than the number of variables in VS. */
static struct variable *
dict_var_set_get_var (struct var_set *vs, size_t idx) 
{
  struct dictionary *d = vs->aux;

  return dict_get_var (d, idx);
}

/* Returns the variable in VS named NAME, or a null pointer if VS
   contains no variable with that name. */
static struct variable *
dict_var_set_lookup_var (struct var_set *vs, const char *name) 
{
  struct dictionary *d = vs->aux;

  return dict_lookup_var (d, name);
}

/* Destroys VS. */
static void
dict_var_set_destroy (struct var_set *vs) 
{
  free (vs);
}

/* Returns a variable set based on D. */
struct var_set *
var_set_create_from_dict (struct dictionary *d) 
{
  struct var_set *vs = xmalloc (sizeof *vs);
  vs->get_cnt = dict_var_set_get_cnt;
  vs->get_var = dict_var_set_get_var;
  vs->lookup_var = dict_var_set_lookup_var;
  vs->destroy = dict_var_set_destroy;
  vs->aux = d;
  return vs;
}

/* A variable set based on an array. */
struct array_var_set 
  {
    struct variable **var;      /* Array of variables. */
    size_t var_cnt;             /* Number of elements in var. */
    struct hsh_table *name_tab; /* Hash from variable names to variables. */
  };

/* Returns the number of variables in VS. */
static size_t
array_var_set_get_cnt (struct var_set *vs) 
{
  struct array_var_set *avs = vs->aux;

  return avs->var_cnt;
}

/* Return variable with index IDX in VS.
   IDX must be less than the number of variables in VS. */
static struct variable *
array_var_set_get_var (struct var_set *vs, size_t idx) 
{
  struct array_var_set *avs = vs->aux;

  return avs->var[idx];
}

/* Returns the variable in VS named NAME, or a null pointer if VS
   contains no variable with that name. */
static struct variable *
array_var_set_lookup_var (struct var_set *vs, const char *name) 
{
  struct array_var_set *avs = vs->aux;
  struct variable v;

  strcpy (v.name, name);

  return hsh_find (avs->name_tab, &v);
}

/* Destroys VS. */
static void
array_var_set_destroy (struct var_set *vs) 
{
  struct array_var_set *avs = vs->aux;

  hsh_destroy (avs->name_tab);
  free (avs);
  free (vs);
}

/* Returns a variable set based on the VAR_CNT variables in
   VAR. */
struct var_set *
var_set_create_from_array (struct variable **var, size_t var_cnt) 
{
  struct var_set *vs;
  struct array_var_set *avs;
  size_t i;

  vs = xmalloc (sizeof *vs);
  vs->get_cnt = array_var_set_get_cnt;
  vs->get_var = array_var_set_get_var;
  vs->lookup_var = array_var_set_lookup_var;
  vs->destroy = array_var_set_destroy;
  vs->aux = avs = xmalloc (sizeof *avs);
  avs->var = var;
  avs->var_cnt = var_cnt;
  avs->name_tab = hsh_create (2 * var_cnt,
                              compare_variables, hash_variable, NULL,
                              NULL);
  for (i = 0; i < var_cnt; i++)
    if (hsh_insert (avs->name_tab, var[i]) != NULL) 
      {
        var_set_destroy (vs);
        return NULL;
      }
  
  return vs;
}
