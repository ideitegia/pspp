/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/lexer/variable-parser.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/variable.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmapx.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/stringi-set.h"

#include "math/interaction.h"

#include "gl/c-ctype.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static struct variable * var_set_get_var (const struct var_set *, size_t );

static struct variable *var_set_lookup_var (const struct var_set *,
					    const char *);

static bool var_set_lookup_var_idx (const struct var_set *, const char *,
				    size_t *);



/* Parses a name as a variable within VS.  Sets *IDX to the
   variable's index and returns true if successful.  On failure
   emits an error message and returns false. */
static bool
parse_vs_variable_idx (struct lexer *lexer, const struct var_set *vs,
		size_t *idx)
{
  assert (idx != NULL);

  if (lex_token (lexer) != T_ID)
    {
      lex_error (lexer, _("expecting variable name"));
      return false;
    }
  else if (var_set_lookup_var_idx (vs, lex_tokcstr (lexer), idx))
    {
      lex_get (lexer);
      return true;
    }
  else
    {
      msg (SE, _("%s is not a variable name."), lex_tokcstr (lexer));
      return false;
    }
}

/* Parses a name as a variable within VS and returns the variable
   if successful.  On failure emits an error message and returns
   a null pointer. */
static struct variable *
parse_vs_variable (struct lexer *lexer, const struct var_set *vs)
{
  size_t idx;
  return parse_vs_variable_idx (lexer, vs, &idx) ? var_set_get_var (vs, idx) : NULL;
}

/* Parses a variable name in dictionary D and returns the
   variable if successful.  On failure emits an error message and
   returns a null pointer. */
struct variable *
parse_variable (struct lexer *lexer, const struct dictionary *d)
{
  struct var_set *vs = var_set_create_from_dict (d);
  struct variable *var = parse_vs_variable (lexer, vs);
  var_set_destroy (vs);
  return var;
}

/* Parses a set of variables from dictionary D given options
   OPTS.  Resulting list of variables stored in *VAR and the
   number of variables into *CNT.  Returns true only if
   successful. */
bool
parse_variables (struct lexer *lexer, const struct dictionary *d,
			struct variable ***var,
			size_t *cnt, int opts)
{
  struct var_set *vs;
  int success;

  assert (d != NULL);
  assert (var != NULL);
  assert (cnt != NULL);

  vs = var_set_create_from_dict (d);
  success = parse_var_set_vars (lexer, vs, var, cnt, opts);
  var_set_destroy (vs);
  return success;
}

/* Parses a set of variables from dictionary D given options
   OPTS.  Resulting list of variables stored in *VARS and the
   number of variables into *VAR_CNT.  Returns true only if
   successful.  Same behavior as parse_variables, except that all
   allocations are taken from the given POOL. */
bool
parse_variables_pool (struct lexer *lexer, struct pool *pool,
		const struct dictionary *dict,
		struct variable ***vars, size_t *var_cnt, int opts)
{
  int retval;

  /* PV_APPEND is unsafe because parse_variables would free the
     existing names on failure, but those names are presumably
     already in the pool, which would attempt to re-free it
     later. */
  assert (!(opts & PV_APPEND));

  retval = parse_variables (lexer, dict, vars, var_cnt, opts);
  if (retval)
    pool_register (pool, free, *vars);
  return retval;
}

/* Parses a variable name from VS.  If successful, sets *IDX to
   the variable's index in VS, *CLASS to the variable's
   dictionary class, and returns true.  Returns false on
   failure. */
static bool
parse_var_idx_class (struct lexer *lexer, const struct var_set *vs,
			size_t *idx,
			enum dict_class *class)
{
  if (!parse_vs_variable_idx (lexer, vs, idx))
    return false;

  *class = dict_class_from_id (var_get_name (var_set_get_var (vs, *idx)));
  return true;
}

/* Add the variable from VS with index IDX to the list of
   variables V that has *NV elements and room for *MV.
   Uses and updates INCLUDED to avoid duplicates if indicated by
   PV_OPTS, which also affects what variables are allowed in
   appropriate ways. */
static void
add_variable (struct variable ***v, size_t *nv, size_t *mv,
              char *included, int pv_opts,
              const struct var_set *vs, size_t idx)
{
  struct variable *add = var_set_get_var (vs, idx);
  const char *add_name = var_get_name (add);

  if ((pv_opts & PV_NUMERIC) && !var_is_numeric (add))
    msg (SW, _("%s is not a numeric variable.  It will not be "
               "included in the variable list."), add_name);
  else if ((pv_opts & PV_STRING) && !var_is_alpha (add))
    msg (SE, _("%s is not a string variable.  It will not be "
               "included in the variable list."), add_name);
  else if ((pv_opts & PV_NO_SCRATCH)
           && dict_class_from_id (add_name) == DC_SCRATCH)
    msg (SE, _("Scratch variables (such as %s) are not allowed "
               "here."), add_name);
  else if ((pv_opts & (PV_SAME_TYPE | PV_SAME_WIDTH)) && *nv
           && var_get_type (add) != var_get_type ((*v)[0]))
    msg (SE, _("%s and %s are not the same type.  All variables in "
               "this variable list must be of the same type.  %s "
               "will be omitted from the list."),
         var_get_name ((*v)[0]), add_name, add_name);
  else if ((pv_opts & PV_SAME_WIDTH) && *nv
           && var_get_width (add) != var_get_width ((*v)[0]))
    msg (SE, _("%s and %s are string variables with different widths.  "
               "All variables in this variable list must have the "
               "same width.  %s will be omitted from the list."),
         var_get_name ((*v)[0]), add_name, add_name);
  else if ((pv_opts & PV_NO_DUPLICATE) && included[idx])
    msg (SE, _("Variable %s appears twice in variable list."), add_name);
  else if ((pv_opts & PV_DUPLICATE) || !included[idx])
    {
      if (*nv >= *mv)
        {
          *mv = 2 * (*nv + 1);
          *v = xnrealloc (*v, *mv, sizeof **v);
        }
      (*v)[(*nv)++] = add;
      if (included != NULL)
        included[idx] = 1;
    }
}

/* Adds the variables in VS with indexes FIRST_IDX through
   LAST_IDX, inclusive, to the list of variables V that has *NV
   elements and room for *MV.  Uses and updates INCLUDED to avoid
   duplicates if indicated by PV_OPTS, which also affects what
   variables are allowed in appropriate ways. */
static void
add_variables (struct variable ***v, size_t *nv, size_t *mv, char *included,
               int pv_opts,
               const struct var_set *vs, int first_idx, int last_idx,
               enum dict_class class)
{
  size_t i;

  for (i = first_idx; i <= last_idx; i++)
    if (dict_class_from_id (var_get_name (var_set_get_var (vs, i))) == class)
      add_variable (v, nv, mv, included, pv_opts, vs, i);
}

/* Note that if parse_variables() returns false, *v is free()'d.
   Conversely, if parse_variables() returns true, then *nv is
   nonzero and *v is non-NULL. */
bool
parse_var_set_vars (struct lexer *lexer, const struct var_set *vs,
                    struct variable ***v, size_t *nv,
                    int pv_opts)
{
  size_t mv;
  char *included;

  assert (vs != NULL);
  assert (v != NULL);
  assert (nv != NULL);

  /* At most one of PV_NUMERIC, PV_STRING, PV_SAME_TYPE,
     PV_SAME_WIDTH may be specified. */
  assert (((pv_opts & PV_NUMERIC) != 0)
          + ((pv_opts & PV_STRING) != 0)
          + ((pv_opts & PV_SAME_TYPE) != 0)
          + ((pv_opts & PV_SAME_WIDTH) != 0) <= 1);

  /* PV_DUPLICATE and PV_NO_DUPLICATE are incompatible. */
  assert (!(pv_opts & PV_DUPLICATE) || !(pv_opts & PV_NO_DUPLICATE));

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
      size_t i;

      included = xcalloc (var_set_get_cnt (vs), sizeof *included);
      for (i = 0; i < *nv; i++)
        {
          size_t index;
          if (!var_set_lookup_var_idx (vs, var_get_name ((*v)[i]), &index))
            NOT_REACHED ();
          included[index] = 1;
        }
    }
  else
    included = NULL;

  do
    {
      if (lex_match (lexer, T_ALL))
        add_variables (v, nv, &mv, included, pv_opts,
                       vs, 0, var_set_get_cnt (vs) - 1, DC_ORDINARY);
      else
        {
          enum dict_class class;
          size_t first_idx;

          if (!parse_var_idx_class (lexer, vs, &first_idx, &class))
            goto fail;

          if (!lex_match (lexer, T_TO))
            add_variable (v, nv, &mv, included, pv_opts, vs, first_idx);
          else
            {
              size_t last_idx;
              enum dict_class last_class;
              struct variable *first_var, *last_var;

              if (!parse_var_idx_class (lexer, vs, &last_idx, &last_class))
                goto fail;

              first_var = var_set_get_var (vs, first_idx);
              last_var = var_set_get_var (vs, last_idx);

              if (last_idx < first_idx)
                {
                  const char *first_name = var_get_name (first_var);
                  const char *last_name = var_get_name (last_var);
                  msg (SE, _("%s TO %s is not valid syntax since %s "
                             "precedes %s in the dictionary."),
                       first_name, last_name, first_name, last_name);
                  goto fail;
                }

              if (class != last_class)
                {
                  msg (SE, _("When using the TO keyword to specify several "
                             "variables, both variables must be from "
                             "the same variable dictionaries, of either "
                             "ordinary, scratch, or system variables.  "
                             "%s is a %s variable, whereas %s is %s."),
                       var_get_name (first_var), dict_class_to_name (class),
                       var_get_name (last_var),
                       dict_class_to_name (last_class));
                  goto fail;
                }

              add_variables (v, nv, &mv, included, pv_opts,
                             vs, first_idx, last_idx, class);
            }
        }

      if (pv_opts & PV_SINGLE)
        break;
      lex_match (lexer, T_COMMA);
    }
  while (lex_token (lexer) == T_ALL
         || (lex_token (lexer) == T_ID && var_set_lookup_var (vs, lex_tokcstr (lexer)) != NULL));

  if (*nv == 0)
    goto fail;

  free (included);
  return 1;

fail:
  free (included);
  free (*v);
  *v = NULL;
  *nv = 0;
  return 0;
}

/* Attempts to break UTF-8 encoded NAME into a root (whose contents are
   arbitrary except that it does not end in a digit) followed by an integer
   numeric suffix.  On success, stores the value of the suffix into *NUMBERP,
   the number of digits in the suffix into *N_DIGITSP, and returns the number
   of bytes in the root.  On failure, returns 0. */
static int
extract_numeric_suffix (const char *name,
                        unsigned long int *numberp, int *n_digitsp)
{
  size_t root_len, n_digits;
  size_t i;

  /* Count length of root. */
  root_len = 1;                 /* Valid identifier never starts with digit. */
  for (i = 1; name[i] != '\0'; i++)
    if (!c_isdigit (name[i]))
      root_len = i + 1;
  n_digits = i - root_len;

  if (n_digits == 0)
    {
      msg (SE, _("`%s' cannot be used with TO because it does not end in "
                 "a digit."), name);
      return 0;
    }

  *numberp = strtoull (name + root_len, NULL, 10);
  if (*numberp == ULONG_MAX)
    {
      msg (SE, _("Numeric suffix on `%s' is larger than supported with TO."),
           name);
      return 0;
    }
  *n_digitsp = n_digits;
  return root_len;
}

static bool
add_var_name (char *name,
              char ***names, size_t *n_vars, size_t *allocated_vars,
              struct stringi_set *set, int pv_opts)
{
  if (pv_opts & PV_NO_DUPLICATE && !stringi_set_insert (set, name))
    {
      msg (SE, _("Variable %s appears twice in variable list."),
           name);
      return false;
    }

  if (*n_vars >= *allocated_vars)
    *names = x2nrealloc (*names, allocated_vars, sizeof **names);
  (*names)[(*n_vars)++] = name;
  return true;
}

/* Parses a list of variable names according to the DATA LIST version
   of the TO convention.  */
bool
parse_DATA_LIST_vars (struct lexer *lexer, const struct dictionary *dict,
                      char ***namesp, size_t *n_varsp, int pv_opts)
{
  char **names;
  size_t n_vars;
  size_t allocated_vars;

  struct stringi_set set;

  char *name1 = NULL;
  char *name2 = NULL;
  bool ok = false;

  assert ((pv_opts & ~(PV_APPEND | PV_SINGLE
                       | PV_NO_SCRATCH | PV_NO_DUPLICATE)) == 0);
  stringi_set_init (&set);

  if (pv_opts & PV_APPEND)
    {
      n_vars = allocated_vars = *n_varsp;
      names = *namesp;

      if (pv_opts & PV_NO_DUPLICATE)
        {
          size_t i;

          for (i = 0; i < n_vars; i++)
            stringi_set_insert (&set, names[i]);
        }
    }
  else
    {
      n_vars = allocated_vars = 0;
      names = NULL;
    }

  do
    {
      if (lex_token (lexer) != T_ID
          || !dict_id_is_valid (dict, lex_tokcstr (lexer), true))
	{
	  lex_error (lexer, "expecting variable name");
	  goto exit;
	}
      if (dict_class_from_id (lex_tokcstr (lexer)) == DC_SCRATCH
          && (pv_opts & PV_NO_SCRATCH))
	{
	  msg (SE, _("Scratch variables not allowed here."));
	  goto exit;
	}
      name1 = xstrdup (lex_tokcstr (lexer));
      lex_get (lexer);
      if (lex_token (lexer) == T_TO)
	{
          unsigned long int num1, num2;
          int n_digits1, n_digits2;
          int root_len1, root_len2;
          unsigned long int number;

	  lex_get (lexer);
	  if (lex_token (lexer) != T_ID
              || !dict_id_is_valid (dict, lex_tokcstr (lexer), true))
	    {
	      lex_error (lexer, "expecting variable name");
	      goto exit;
	    }
          name2 = xstrdup (lex_tokcstr (lexer));
	  lex_get (lexer);

          root_len1 = extract_numeric_suffix (name1, &num1, &n_digits1);
          if (root_len1 == 0)
            goto exit;

          root_len2 = extract_numeric_suffix (name2, &num2, &n_digits2);
          if (root_len2 == 0)
	    goto exit;

	  if (root_len1 != root_len2 || memcasecmp (name1, name2, root_len1))
	    {
	      msg (SE, _("Prefixes don't match in use of TO convention."));
	      goto exit;
	    }
	  if (num1 > num2)
	    {
	      msg (SE, _("Bad bounds in use of TO convention."));
	      goto exit;
	    }

	  for (number = num1; number <= num2; number++)
	    {
              char *name = xasprintf ("%.*s%0*lu",
                                      root_len1, name1,
                                      n_digits1, number);
              if (!add_var_name (name, &names, &n_vars, &allocated_vars,
                                 &set, pv_opts))
                {
                  free (name);
                  goto exit;
                }
	    }

          free (name1);
          name1 = NULL;
          free (name2);
          name2 = NULL;
	}
      else
	{
          if (!add_var_name (name1, &names, &n_vars, &allocated_vars,
                             &set, pv_opts))
            goto exit;
          name1 = NULL;
	}

      lex_match (lexer, T_COMMA);

      if (pv_opts & PV_SINGLE)
	break;
    }
  while (lex_token (lexer) == T_ID);
  ok = true;

exit:
  stringi_set_destroy (&set);
  if (ok)
    {
      *namesp = names;
      *n_varsp = n_vars;
    }
  else
    {
      int i;
      for (i = 0; i < n_vars; i++)
	free (names[i]);
      free (names);
      *namesp = NULL;
      *n_varsp = 0;

      free (name1);
      free (name2);
    }
  return ok;
}

/* Registers each of the NAMES[0...NNAMES - 1] in POOL, as well
   as NAMES itself. */
static void
register_vars_pool (struct pool *pool, char **names, size_t nnames)
{
  size_t i;

  for (i = 0; i < nnames; i++)
    pool_register (pool, free, names[i]);
  pool_register (pool, free, names);
}

/* Parses a list of variable names according to the DATA LIST
   version of the TO convention.  Same args as
   parse_DATA_LIST_vars(), except that all allocations are taken
   from the given POOL. */
bool
parse_DATA_LIST_vars_pool (struct lexer *lexer, const struct dictionary *dict,
                           struct pool *pool,
                           char ***names, size_t *nnames, int pv_opts)
{
  int retval;

  /* PV_APPEND is unsafe because parse_DATA_LIST_vars would free
     the existing names on failure, but those names are
     presumably already in the pool, which would attempt to
     re-free it later. */
  assert (!(pv_opts & PV_APPEND));

  retval = parse_DATA_LIST_vars (lexer, dict, names, nnames, pv_opts);
  if (retval)
    register_vars_pool (pool, *names, *nnames);
  return retval;
}

/* Parses a list of variables where some of the variables may be
   existing and the rest are to be created.  Same args as
   parse_DATA_LIST_vars(). */
bool
parse_mixed_vars (struct lexer *lexer, const struct dictionary *dict,
		  char ***names, size_t *nnames, int pv_opts)
{
  size_t i;

  assert (names != NULL);
  assert (nnames != NULL);
  assert ((pv_opts & ~PV_APPEND) == 0);

  if (!(pv_opts & PV_APPEND))
    {
      *names = NULL;
      *nnames = 0;
    }
  while (lex_token (lexer) == T_ID || lex_token (lexer) == T_ALL)
    {
      if (lex_token (lexer) == T_ALL || dict_lookup_var (dict, lex_tokcstr (lexer)) != NULL)
	{
	  struct variable **v;
	  size_t nv;

	  if (!parse_variables (lexer, dict, &v, &nv, PV_NONE))
	    goto fail;
	  *names = xnrealloc (*names, *nnames + nv, sizeof **names);
	  for (i = 0; i < nv; i++)
	    (*names)[*nnames + i] = xstrdup (var_get_name (v[i]));
	  free (v);
	  *nnames += nv;
	}
      else if (!parse_DATA_LIST_vars (lexer, dict, names, nnames, PV_APPEND))
	goto fail;
    }
  return 1;

fail:
  for (i = 0; i < *nnames; i++)
    free ((*names)[i]);
  free (*names);
  *names = NULL;
  *nnames = 0;
  return 0;
}

/* Parses a list of variables where some of the variables may be
   existing and the rest are to be created.  Same args as
   parse_mixed_vars(), except that all allocations are taken
   from the given POOL. */
bool
parse_mixed_vars_pool (struct lexer *lexer, const struct dictionary *dict, struct pool *pool,
                       char ***names, size_t *nnames, int pv_opts)
{
  int retval;

  /* PV_APPEND is unsafe because parse_mixed_vars_pool would free
     the existing names on failure, but those names are
     presumably already in the pool, which would attempt to
     re-free it later. */
  assert (!(pv_opts & PV_APPEND));

  retval = parse_mixed_vars (lexer, dict, names, nnames, pv_opts);
  if (retval)
    register_vars_pool (pool, *names, *nnames);
  return retval;
}

/* A set of variables. */
struct var_set
  {
    size_t (*get_cnt) (const struct var_set *);
    struct variable *(*get_var) (const struct var_set *, size_t idx);
    bool (*lookup_var_idx) (const struct var_set *, const char *, size_t *);
    void (*destroy) (struct var_set *);
    void *aux;
  };

/* Returns the number of variables in VS. */
size_t
var_set_get_cnt (const struct var_set *vs)
{
  assert (vs != NULL);

  return vs->get_cnt (vs);
}

/* Return variable with index IDX in VS.
   IDX must be less than the number of variables in VS. */
static struct variable *
var_set_get_var (const struct var_set *vs, size_t idx)
{
  assert (vs != NULL);
  assert (idx < var_set_get_cnt (vs));

  return vs->get_var (vs, idx);
}

/* Returns the variable in VS named NAME, or a null pointer if VS
   contains no variable with that name. */
struct variable *
var_set_lookup_var (const struct var_set *vs, const char *name)
{
  size_t idx;
  return (var_set_lookup_var_idx (vs, name, &idx)
          ? var_set_get_var (vs, idx)
          : NULL);
}

/* If VS contains a variable named NAME, sets *IDX to its index
   and returns true.  Otherwise, returns false. */
bool
var_set_lookup_var_idx (const struct var_set *vs, const char *name,
                        size_t *idx)
{
  assert (vs != NULL);
  assert (name != NULL);

  return vs->lookup_var_idx (vs, name, idx);
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
dict_var_set_get_cnt (const struct var_set *vs)
{
  struct dictionary *d = vs->aux;

  return dict_get_var_cnt (d);
}

/* Return variable with index IDX in VS.
   IDX must be less than the number of variables in VS. */
static struct variable *
dict_var_set_get_var (const struct var_set *vs, size_t idx)
{
  struct dictionary *d = vs->aux;

  return dict_get_var (d, idx);
}

/* If VS contains a variable named NAME, sets *IDX to its index
   and returns true.  Otherwise, returns false. */
static bool
dict_var_set_lookup_var_idx (const struct var_set *vs, const char *name,
                             size_t *idx)
{
  struct dictionary *d = vs->aux;
  struct variable *v = dict_lookup_var (d, name);
  if (v != NULL)
    {
      *idx = var_get_dict_index (v);
      return true;
    }
  else
    return false;
}

/* Destroys VS. */
static void
dict_var_set_destroy (struct var_set *vs)
{
  free (vs);
}

/* Returns a variable set based on D. */
struct var_set *
var_set_create_from_dict (const struct dictionary *d)
{
  struct var_set *vs = xmalloc (sizeof *vs);
  vs->get_cnt = dict_var_set_get_cnt;
  vs->get_var = dict_var_set_get_var;
  vs->lookup_var_idx = dict_var_set_lookup_var_idx;
  vs->destroy = dict_var_set_destroy;
  vs->aux = (void *) d;
  return vs;
}

/* A variable set based on an array. */
struct array_var_set
  {
    struct variable *const *var;/* Array of variables. */
    size_t var_cnt;             /* Number of elements in var. */
    struct hmapx vars_by_name;  /* Variables hashed by name. */
  };

/* Returns the number of variables in VS. */
static size_t
array_var_set_get_cnt (const struct var_set *vs)
{
  struct array_var_set *avs = vs->aux;

  return avs->var_cnt;
}

/* Return variable with index IDX in VS.
   IDX must be less than the number of variables in VS. */
static struct variable *
array_var_set_get_var (const struct var_set *vs, size_t idx)
{
  struct array_var_set *avs = vs->aux;

  return CONST_CAST (struct variable *, avs->var[idx]);
}

/* If VS contains a variable named NAME, sets *IDX to its index
   and returns true.  Otherwise, returns false. */
static bool
array_var_set_lookup_var_idx (const struct var_set *vs, const char *name,
                              size_t *idx)
{
  struct array_var_set *avs = vs->aux;
  struct hmapx_node *node;
  struct variable **varp;

  HMAPX_FOR_EACH_WITH_HASH (varp, node, hash_case_string (name, 0),
                            &avs->vars_by_name)
    if (!strcasecmp (name, var_get_name (*varp)))
      {
        *idx = varp - avs->var;
        return true;
      }

  return false;
}

/* Destroys VS. */
static void
array_var_set_destroy (struct var_set *vs)
{
  struct array_var_set *avs = vs->aux;

  hmapx_destroy (&avs->vars_by_name);
  free (avs);
  free (vs);
}

/* Returns a variable set based on the VAR_CNT variables in VAR. */
struct var_set *
var_set_create_from_array (struct variable *const *var, size_t var_cnt)
{
  struct var_set *vs;
  struct array_var_set *avs;
  size_t i;

  vs = xmalloc (sizeof *vs);
  vs->get_cnt = array_var_set_get_cnt;
  vs->get_var = array_var_set_get_var;
  vs->lookup_var_idx = array_var_set_lookup_var_idx;
  vs->destroy = array_var_set_destroy;
  vs->aux = avs = xmalloc (sizeof *avs);
  avs->var = var;
  avs->var_cnt = var_cnt;
  hmapx_init (&avs->vars_by_name);
  for (i = 0; i < var_cnt; i++)
    {
      const char *name = var_get_name (var[i]);
      size_t idx;

      if (array_var_set_lookup_var_idx (vs, name, &idx))
        {
          var_set_destroy (vs);
          return NULL;
        }
      hmapx_insert (&avs->vars_by_name, CONST_CAST (void *, &avs->var[i]),
                    hash_case_string (name, 0));
    }

  return vs;
}


/* Match a variable.
   If the match succeeds, the variable will be placed in VAR.
   Returns true if successful */
bool
lex_match_variable (struct lexer *lexer, const struct dictionary *dict, const struct variable **var)
{
  if (lex_token (lexer) !=  T_ID)
    return false;

  *var = parse_variable_const  (lexer, dict);

  if ( *var == NULL)
    return false;
  return true;
}

/* An interaction is a variable followed by {*, BY} followed by an interaction */
bool
parse_design_interaction (struct lexer *lexer, const struct dictionary *dict, struct interaction **iact)
{
  const struct variable *v = NULL;
  assert (iact);

  switch  (lex_next_token (lexer, 1))
    {
    case T_ENDCMD:
    case T_SLASH:
    case T_COMMA:
    case T_ID:
    case T_BY:
    case T_ASTERISK:
      break;
    default:
      return false;
      break;
    }

  if (! lex_match_variable (lexer, dict, &v))
    {
      interaction_destroy (*iact);
      *iact = NULL;
      return false;
    }
  
  assert (v);

  if ( *iact == NULL)
    *iact = interaction_create (v);
  else
    interaction_add_variable (*iact, v);

  if ( lex_match (lexer, T_ASTERISK) || lex_match (lexer, T_BY))
    {
      return parse_design_interaction (lexer, dict, iact);
    }

  return true;
}

