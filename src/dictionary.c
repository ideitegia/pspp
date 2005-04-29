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
#include "dictionary.h"
#include <stdlib.h>
#include <ctype.h>
#include "algorithm.h"
#include "alloc.h"
#include "case.h"
#include "error.h"
#include "hash.h"
#include "misc.h"
#include "str.h"
#include "value-labels.h"
#include "var.h"

/* A dictionary. */
struct dictionary
  {
    struct variable **var;	/* Variables. */
    size_t var_cnt, var_cap;    /* Number of variables, capacity. */
    struct hsh_table *name_tab;	/* Variable index by name. */
    struct hsh_table *long_name_tab; /* Variable indexed by long name */
    int next_value_idx;         /* Index of next `union value' to allocate. */
    struct variable **split;    /* SPLIT FILE vars. */
    size_t split_cnt;           /* SPLIT FILE count. */
    struct variable *weight;    /* WEIGHT variable. */
    struct variable *filter;    /* FILTER variable. */
    int case_limit;             /* Current case limit (N command). */
    char *label;		/* File label. */
    char *documents;		/* Documents, as a string. */
    struct vector **vector;     /* Vectors of variables. */
    size_t vector_cnt;          /* Number of vectors. */
  };





int
compare_long_names(const void *a_, const void *b_, void *aux UNUSED)
{
  const struct name_table_entry *a = a_;
  const struct name_table_entry *b = b_;

  return strcasecmp(a->longname, b->longname);
}


/* Long names use case insensitive comparison */
unsigned int
hash_long_name (const void *e_, void *aux UNUSED) 
{
  const struct name_table_entry *e = e_;
  unsigned int hash;
  int i;

  char *s = strdup(e->longname);

  for ( i = 0 ; i < strlen(s) ; ++i ) 
    s[i] = toupper(s[i]);

  hash = hsh_hash_string (s);
  
  free (s);

  return hash;
}




static char *make_short_name(struct dictionary *dict, const char *longname) ;


/* Creates and returns a new dictionary. */
struct dictionary *
dict_create (void) 
{
  struct dictionary *d = xmalloc (sizeof *d);
  
  d->var = NULL;
  d->var_cnt = d->var_cap = 0;
  d->name_tab = hsh_create (8, compare_var_names, hash_var_name, NULL, NULL);
  d->long_name_tab = hsh_create (8, compare_long_names, hash_long_name, 
				 (hsh_free_func *) free_nte, NULL);
  d->next_value_idx = 0;
  d->split = NULL;
  d->split_cnt = 0;
  d->weight = NULL;
  d->filter = NULL;
  d->case_limit = 0;
  d->label = NULL;
  d->documents = NULL;
  d->vector = NULL;
  d->vector_cnt = 0;

  return d;
}

/* Creates and returns a (deep) copy of an existing
   dictionary. */
struct dictionary *
dict_clone (const struct dictionary *s) 
{
  struct dictionary *d;
  size_t i;

  assert (s != NULL);

  d = dict_create ();

  for (i = 0; i < s->var_cnt; i++)
    dict_clone_var (d, s->var[i], s->var[i]->name, s->var[i]->longname);

  d->next_value_idx = s->next_value_idx;

  d->split_cnt = s->split_cnt;
  if (d->split_cnt > 0) 
    {
      d->split = xmalloc (d->split_cnt * sizeof *d->split);
      for (i = 0; i < d->split_cnt; i++) 
        d->split[i] = dict_lookup_var_assert (d, s->split[i]->name);
    }

  if (s->weight != NULL) 
    d->weight = dict_lookup_var_assert (d, s->weight->name);

  if (s->filter != NULL) 
    d->filter = dict_lookup_var_assert (d, s->filter->name);

  d->case_limit = s->case_limit;
  dict_set_label (d, dict_get_label (s));
  dict_set_documents (d, dict_get_documents (s));

  for (i = 0; i < s->vector_cnt; i++) 
    dict_create_vector (d, s->vector[i]->name,
                        s->vector[i]->var, s->vector[i]->cnt);

  return d;
}

/* Clears the contents from a dictionary without destroying the
   dictionary itself. */
void
dict_clear (struct dictionary *d) 
{
  /* FIXME?  Should we really clear case_limit, label, documents?
     Others are necessarily cleared by deleting all the variables.*/
  int i;

  assert (d != NULL);

  for (i = 0; i < d->var_cnt; i++) 
    {
      struct variable *v = d->var[i];
      var_clear_aux (v);
      val_labs_destroy (v->val_labs);
      free (v->label);
      free (v); 
    }
  free (d->var);
  d->var = NULL;
  d->var_cnt = d->var_cap = 0;
  hsh_clear (d->name_tab);
  if ( d->long_name_tab) 
    hsh_clear (d->long_name_tab);
  d->next_value_idx = 0;
  free (d->split);
  d->split = NULL;
  d->split_cnt = 0;
  d->weight = NULL;
  d->filter = NULL;
  d->case_limit = 0;
  free (d->label);
  d->label = NULL;
  free (d->documents);
  d->documents = NULL;
  dict_clear_vectors (d);
}

/* Allocate the pointer TEXT and fill it with text representing the
   long variable name buffer.  SIZE will contain the size of TEXT.
   TEXT must be freed by the caller when no longer required.
*/
void
dict_get_varname_block(const struct dictionary *dict, char **text, int *size)
{
  char *buf = 0;
  int bufsize = 0;
  struct hsh_iterator hi;
  struct name_table_entry *nte;
  short first = 1;

  for ( nte = hsh_first(dict->long_name_tab, &hi);
	nte; 
	nte = hsh_next(dict->long_name_tab, &hi))
    {
      bufsize += strlen(nte->name) + strlen(nte->longname) + 2;
      buf = xrealloc(buf, bufsize + 1);
      if ( first ) 
	strcpy(buf, "");
      first = 0;

      strcat(buf, nte->name);
      strcat(buf, "=");
      strcat(buf, nte->longname);
      strcat(buf, "\t");
    }

  if ( bufsize > 0 ) 
    {
      /* Loose the final delimiting TAB */
      buf[bufsize]='\0';
      bufsize--;
    }
  
  *text = buf;
  *size = bufsize;
}

/* Add a new entry into the dictionary's long name table, and update the 
   corresponding variable with the relevant long name.
*/
void
dict_add_longvar_entry(struct dictionary *d, 
		       const char *name, 
		       const char *longname)
{
  struct variable *v;
  assert ( name ) ;
  assert ( longname );
  struct name_table_entry *nte = xmalloc (sizeof (struct name_table_entry));
  nte->longname = strdup(longname);
  nte->name = strdup(name);

  /* Look up the name in name_tab */
  v = hsh_find ( d->name_tab, name);
  if ( !v ) 
    {
      msg (FE, _("The entry \"%s\" in the variable name map, has no corresponding variable"), name);
      return ;
    }
  assert ( 0 == strcmp(v->name, name) );
  v->longname = nte->longname;

  hsh_insert(d->long_name_tab, nte);
}

/* Destroy and free up an nte */
void
free_nte(struct name_table_entry *nte)
{
  assert(nte);
  free(nte->longname);
  free(nte->name);
  free(nte);
}


/* Destroys the aux data for every variable in D, by calling
   var_clear_aux() for each variable. */
void
dict_clear_aux (struct dictionary *d) 
{
  int i;
  
  assert (d != NULL);
  
  for (i = 0; i < d->var_cnt; i++)
    var_clear_aux (d->var[i]);
}

/* Clears a dictionary and destroys it. */
void
dict_destroy (struct dictionary *d)
{
  if (d != NULL) 
    {
      dict_clear (d);
      hsh_destroy (d->name_tab);
      hsh_destroy (d->long_name_tab);
      free (d);
    }
}

/* Returns the number of variables in D. */
size_t
dict_get_var_cnt (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->var_cnt;
}

/* Returns the variable in D with index IDX, which must be
   between 0 and the count returned by dict_get_var_cnt(),
   exclusive. */
struct variable *
dict_get_var (const struct dictionary *d, size_t idx) 
{
  assert (d != NULL);
  assert (idx < d->var_cnt);

  return d->var[idx];
}

/* Sets *VARS to an array of pointers to variables in D and *CNT
   to the number of variables in *D.  By default all variables
   are returned, but bits may be set in EXCLUDE_CLASSES to
   exclude ordinary, system, and/or scratch variables. */
void
dict_get_vars (const struct dictionary *d, struct variable ***vars,
               size_t *cnt, unsigned exclude_classes)
{
  size_t count;
  size_t i;
  
  assert (d != NULL);
  assert (vars != NULL);
  assert (cnt != NULL);
  assert ((exclude_classes & ~((1u << DC_ORDINARY)
                               | (1u << DC_SYSTEM)
                               | (1u << DC_SCRATCH))) == 0);
  
  count = 0;
  for (i = 0; i < d->var_cnt; i++)
    if (!(exclude_classes & (1u << dict_class_from_id (d->var[i]->name))))
      count++;

  *vars = xmalloc (count * sizeof **vars);
  *cnt = 0;
  for (i = 0; i < d->var_cnt; i++)
    if (!(exclude_classes & (1u << dict_class_from_id (d->var[i]->name))))
      (*vars)[(*cnt)++] = d->var[i];
  assert (*cnt == count);
}


static struct variable * dict_create_var_x (struct dictionary *d, 
					    const char *name, int width, 
					    short name_is_short) ;

/* Creates and returns a new variable in D with the given LONGNAME
   and WIDTH.  Returns a null pointer if the given LONGNAME would
   duplicate that of an existing variable in the dictionary.
*/
struct variable *
dict_create_var (struct dictionary *d, const char *longname, int width)
{
  return dict_create_var_x(d, longname, width, 0);
}


/* Creates and returns a new variable in D with the given SHORTNAME and 
   WIDTH.  The long name table is not updated */
struct variable *
dict_create_var_from_short (struct dictionary *d, const char *shortname, 
			    int width)
{
  return dict_create_var_x(d, shortname, width, 1);
}



/* Creates and returns a new variable in D with the given NAME
   and WIDTH.  
   If NAME_IS_SHORT, assume NAME is the short name.  Otherwise assumes
   NAME is the long name, and creates the corresponding entry in the 
   Dictionary's lookup name table .
   Returns a null pointer if the given NAME would
   duplicate that of an existing variable in the dictionary. 
   
*/
static struct variable *
dict_create_var_x (struct dictionary *d, const char *name, int width, 
		 short name_is_short) 
{
  struct variable *v;

  assert (d != NULL);
  assert (name != NULL);

  assert (strlen (name) >= 1);

  assert (width >= 0 && width < 256);
    
  if ( name_is_short ) 
    assert(strlen (name) <= SHORT_NAME_LEN);
  else
    assert(strlen (name) <= LONG_NAME_LEN);

  /* Make sure there's not already a variable by that name. */
  if (dict_lookup_var (d, name) != NULL)
    return NULL;

  /* Allocate and initialize variable. */
  v = xmalloc (sizeof *v);

  if ( name_is_short )
    {
      strncpy (v->name, name, sizeof v->name);
      v->name[SHORT_NAME_LEN] = '\0';
    }
  else
    {
      const char *sn = make_short_name(d, name);
      strncpy(v->name, sn, SHORT_NAME_LEN + 1);
      free(sn);
    }
  

  v->index = d->var_cnt;
  v->type = width == 0 ? NUMERIC : ALPHA;
  v->width = width;
  v->fv = d->next_value_idx;
  v->nv = width == 0 ? 1 : DIV_RND_UP (width, 8);
  v->init = 1;
  v->reinit = dict_class_from_id (v->name) != DC_SCRATCH;
  v->miss_type = MISSING_NONE;
  if (v->type == NUMERIC)
    {
      v->print.type = FMT_F;
      v->print.w = 8;
      v->print.d = 2;

      v->alignment = ALIGN_RIGHT;
      v->display_width = 8;
      v->measure = MEASURE_SCALE;
    }
  else
    {
      v->print.type = FMT_A;
      v->print.w = v->width;
      v->print.d = 0;

      v->alignment = ALIGN_LEFT;
      v->display_width = 8;
      v->measure = MEASURE_NOMINAL;
    }
  v->write = v->print;
  v->val_labs = val_labs_create (v->width);
  v->label = NULL;
  v->aux = NULL;
  v->aux_dtor = NULL;

  /* Update dictionary. */
  if (d->var_cnt >= d->var_cap) 
    {
      d->var_cap = 8 + 2 * d->var_cap; 
      d->var = xrealloc (d->var, d->var_cap * sizeof *d->var);
    }
  d->var[v->index] = v;
  d->var_cnt++;
  hsh_force_insert (d->name_tab, v);

  if ( ! name_is_short) 
    dict_add_longvar_entry(d, v->name, name);

  d->next_value_idx += v->nv;

  return v;
}

/* Creates and returns a new variable in D with the given NAME
   and WIDTH.  Assert-fails if the given NAME would duplicate
   that of an existing variable in the dictionary. */
struct variable *
dict_create_var_assert (struct dictionary *d, const char *longname, int width)
{
  struct variable *v = dict_create_var (d, longname, width);
  assert (v != NULL);
  return v;
}

/* Creates a new variable in D with longname LONGNAME, as a copy of
   existing  variable OV, which need not be in D or in any
   dictionary. 
   If SHORTNAME is non null, it will be used as the short name
   otherwise a new short name will be generated.
*/
struct variable *
dict_clone_var (struct dictionary *d, const struct variable *ov,
                const char *name, const char *longname)
{
  struct variable *nv;

  assert (d != NULL);
  assert (ov != NULL);
  assert (strlen (longname) <= LONG_NAME_LEN);

  struct name_table_entry *nte = xmalloc (sizeof (struct name_table_entry));

  nte->longname = strdup(longname);
  if ( name ) 
    {
      assert (strlen (name) >= 1);
      assert (strlen (name) <= SHORT_NAME_LEN);
      nte->name = strdup(name);
    }
  else 
    nte->name = make_short_name(d, longname);


  nv = dict_create_var_from_short (d, nte->name, ov->width);
  if (nv == NULL)
    return NULL;

  hsh_insert(d->long_name_tab, nte);
  nv->longname = nte->longname;

  nv->init = 1;
  nv->reinit = ov->reinit;
  nv->miss_type = ov->miss_type;
  memcpy (nv->missing, ov->missing, sizeof nv->missing);
  nv->print = ov->print;
  nv->write = ov->write;
  val_labs_destroy (nv->val_labs);
  nv->val_labs = val_labs_copy (ov->val_labs);
  if (ov->label != NULL)
    nv->label = xstrdup (ov->label);

  nv->alignment = ov->alignment;
  nv->measure = ov->measure;
  nv->display_width = ov->display_width;

  return nv;
}

/* Changes the name of V in D to name NEW_NAME.  Assert-fails if
   a variable named NEW_NAME is already in D, except that
   NEW_NAME may be the same as V's existing name. */
void 
dict_rename_var (struct dictionary *d, struct variable *v,
                 const char *new_name) 
{
  assert (d != NULL);
  assert (v != NULL);
  assert (new_name != NULL);
  assert (strlen (new_name) >= 1 && strlen (new_name) <= SHORT_NAME_LEN);
  assert (dict_contains_var (d, v));

  if (!strcmp (v->name, new_name))
    return;

  assert (dict_lookup_var (d, new_name) == NULL);

  hsh_force_delete (d->name_tab, v);
  strncpy (v->name, new_name, sizeof v->name);
  v->name[SHORT_NAME_LEN] = '\0';
  hsh_force_insert (d->name_tab, v);
  dict_add_longvar_entry (d, new_name, new_name);
}

/* Returns the variable named NAME in D, or a null pointer if no
   variable has that name. */
struct variable *
dict_lookup_var (const struct dictionary *d, const char *name)
{
  struct variable v;
  struct variable *vr;
  
  char *short_name;
  struct name_table_entry key;
  struct name_table_entry *nte;

  assert (d != NULL);
  assert (name != NULL);
  assert (strlen (name) >= 1 && strlen (name) <= LONG_NAME_LEN);

  key.longname = name;
  nte = hsh_find (d->long_name_tab, &key);
  
  if ( ! nte ) 
  {
    return 0;
  }

  short_name = nte->name ;

  strncpy (v.name, short_name, sizeof v.name);
  v.name[SHORT_NAME_LEN] = '\0';

  vr = hsh_find (d->name_tab, &v);

  return vr; 
}


/* Returns the variable named NAME in D.  Assert-fails if no
   variable has that name. */
struct variable *
dict_lookup_var_assert (const struct dictionary *d, const char *name)
{
  struct variable *v = dict_lookup_var (d, name);
  assert (v != NULL);
  return v;
}

/* Returns nonzero if variable V is in dictionary D. */
int
dict_contains_var (const struct dictionary *d, const struct variable *v)
{
  assert (d != NULL);
  assert (v != NULL);

  return v->index >= 0 && v->index < d->var_cnt && d->var[v->index] == v;
}

/* Compares two double pointers to variables, which should point
   to elements of a struct dictionary's `var' member array. */
static int
compare_var_ptrs (const void *a_, const void *b_, void *aux UNUSED) 
{
  struct variable *const *a = a_;
  struct variable *const *b = b_;

  return *a < *b ? -1 : *a > *b;
}

/* Deletes variable V from dictionary D and frees V.

   This is a very bad idea if there might be any pointers to V
   from outside D.  In general, no variable in default_dict
   should be deleted when any transformations are active, because
   those transformations might reference the deleted variable.
   The safest time to delete a variable is just after a procedure
   has been executed, as done by MODIFY VARS.

   Pointers to V within D are not a problem, because
   dict_delete_var() knows to remove V from split variables,
   weights, filters, etc. */
void
dict_delete_var (struct dictionary *d, struct variable *v) 
{
  size_t i;

  assert (d != NULL);
  assert (v != NULL);
  assert (dict_contains_var (d, v));
  assert (d->var[v->index] == v);

  /* Delete aux data. */
  var_clear_aux (v);

  /* Remove V from splits, weight, filter variables. */
  d->split_cnt = remove_equal (d->split, d->split_cnt, sizeof *d->split,
                               &v, compare_var_ptrs, NULL);
  if (d->weight == v)
    d->weight = NULL;
  if (d->filter == v)
    d->filter = NULL;
  dict_clear_vectors (d);

  /* Remove V from var array. */
  remove_element (d->var, d->var_cnt, sizeof *d->var, v->index);
  d->var_cnt--;

  /* Update index. */
  for (i = v->index; i < d->var_cnt; i++)
    d->var[i]->index = i;

  /* Update name hash. */
  hsh_force_delete (d->name_tab, v);

  /* Free memory. */
  val_labs_destroy (v->val_labs);
  free (v->label);
  free (v);
}

/* Deletes the COUNT variables listed in VARS from D.  This is
   unsafe; see the comment on dict_delete_var() for details. */
void 
dict_delete_vars (struct dictionary *d,
                  struct variable *const *vars, size_t count) 
{
  /* FIXME: this can be done in O(count) time, but this algorithm
     is O(count**2). */
  assert (d != NULL);
  assert (count == 0 || vars != NULL);

  while (count-- > 0)
    dict_delete_var (d, *vars++);
}

/* Reorders the variables in D, placing the COUNT variables
   listed in ORDER in that order at the beginning of D.  The
   other variables in D, if any, retain their relative
   positions. */
void 
dict_reorder_vars (struct dictionary *d,
                   struct variable *const *order, size_t count) 
{
  struct variable **new_var;
  size_t i;
  
  assert (d != NULL);
  assert (count == 0 || order != NULL);
  assert (count <= d->var_cnt);

  new_var = xmalloc (d->var_cnt * sizeof *new_var);
  memcpy (new_var, order, count * sizeof *new_var);
  for (i = 0; i < count; i++) 
    {
      assert (d->var[order[i]->index] != NULL);
      d->var[order[i]->index] = NULL;
      order[i]->index = i;
    }
  for (i = 0; i < d->var_cnt; i++)
    if (d->var[i] != NULL)
      {
        assert (count < d->var_cnt);
        new_var[count] = d->var[i];
        new_var[count]->index = count;
        count++;
      }
  free (d->var);
  d->var = new_var;
}

/* Renames COUNT variables specified in VARS to the names given
   in NEW_NAMES within dictionary D.  If the renaming would
   result in a duplicate variable name, returns zero and stores a
   name that would be duplicated into *ERR_NAME (if ERR_NAME is
   non-null).  Otherwise, the renaming is successful, and nonzero
   is returned. */
int
dict_rename_vars (struct dictionary *d,
                  struct variable **vars, char **new_names,
                  size_t count, char **err_name) 
{
  char **old_names;
  size_t i;
  int success = 1;


  assert (d != NULL);
  assert (count == 0 || vars != NULL);
  assert (count == 0 || new_names != NULL);


  old_names = xmalloc (count * sizeof *old_names);
  for (i = 0; i < count; i++) 
    {
      assert (d->var[vars[i]->index] == vars[i]);
      hsh_force_delete (d->name_tab, vars[i]);
      old_names[i] = xstrdup (vars[i]->name);
    }
  
  for (i = 0; i < count; i++)
    {
      char *sn;
      struct name_table_entry key;
      struct name_table_entry *nte;
      assert (new_names[i] != NULL);
      assert (*new_names[i] != '\0');
      assert (strlen (new_names[i]) <= LONG_NAME_LEN );
      
      sn = make_short_name(d, new_names[i]);
      strncpy(vars[i]->name, sn, SHORT_NAME_LEN + 1);
      free(sn);
      


      key.longname = vars[i]->longname;
      nte = hsh_find (d->long_name_tab, &key);
      
      free( nte->longname ) ;
      nte->longname = strdup ( new_names[i]);
      vars[i]->longname = nte->longname;

      if (hsh_insert (d->name_tab, vars[i]) != NULL )
        {
          size_t fail_idx = i;
          if (err_name != NULL) 
            *err_name = new_names[i];

          for (i = 0; i < fail_idx; i++)
            hsh_force_delete (d->name_tab, vars[i]);
          
          for (i = 0; i < count; i++)
            {
              strcpy (vars[i]->name, old_names[i]);
              hsh_force_insert (d->name_tab, vars[i]);
 
      key.longname = vars[i]->longname;
      nte = hsh_find (d->long_name_tab, &key);
      
      free( nte->longname ) ;
      nte->longname = strdup ( old_names[i]);
      vars[i]->longname = nte->longname;

            }

          success = 0;
          break;
        }
    }

  for (i = 0; i < count; i++)
    free (old_names[i]);
  free (old_names);

  return success;
}

/* Returns the weighting variable in dictionary D, or a null
   pointer if the dictionary is unweighted. */
struct variable *
dict_get_weight (const struct dictionary *d) 
{
  assert (d != NULL);
  assert (d->weight == NULL || dict_contains_var (d, d->weight));
  
  return d->weight;
}

/* Returns the value of D's weighting variable in case C, except that a
   negative weight is returned as 0.  Returns 1 if the dictionary is
   unweighted. Will warn about missing, negative, or zero values if
   warn_on_invalid is nonzero. The function will set warn_on_invalid to zero
   if an invalid weight is found. */
double
dict_get_case_weight (const struct dictionary *d, const struct ccase *c, 
		      int *warn_on_invalid)
{
  assert (d != NULL);
  assert (c != NULL);

  if (d->weight == NULL)
    return 1.0;
  else 
    {
      double w = case_num (c, d->weight->fv);
      if ( w < 0.0 || w == SYSMIS || is_num_user_missing(w, d->weight) )
        w = 0.0;
      if ( w == 0.0 && *warn_on_invalid ) {
	  *warn_on_invalid = 0;
	  msg (SW, _("At least one case in the data file had a weight value "
		     "that was user-missing, system-missing, zero, or "
		     "negative.  These case(s) were ignored."));
      }
      return w;
    }
}

/* Sets the weighting variable of D to V, or turning off
   weighting if V is a null pointer. */
void
dict_set_weight (struct dictionary *d, struct variable *v) 
{
  assert (d != NULL);
  assert (v == NULL || dict_contains_var (d, v));
  assert (v == NULL || v->type == NUMERIC);

  d->weight = v;
}

/* Returns the filter variable in dictionary D (see cmd_filter())
   or a null pointer if the dictionary is unfiltered. */
struct variable *
dict_get_filter (const struct dictionary *d) 
{
  assert (d != NULL);
  assert (d->filter == NULL || dict_contains_var (d, d->filter));
  
  return d->filter;
}

/* Sets V as the filter variable for dictionary D.  Passing a
   null pointer for V turn off filtering. */
void
dict_set_filter (struct dictionary *d, struct variable *v)
{
  assert (d != NULL);
  assert (v == NULL || dict_contains_var (d, v));

  d->filter = v;
}

/* Returns the case limit for dictionary D, or zero if the number
   of cases is unlimited (see cmd_n()). */
int
dict_get_case_limit (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->case_limit;
}

/* Sets CASE_LIMIT as the case limit for dictionary D.  Zero for
   CASE_LIMIT indicates no limit. */
void
dict_set_case_limit (struct dictionary *d, int case_limit) 
{
  assert (d != NULL);
  assert (case_limit >= 0);

  d->case_limit = case_limit;
}

/* Returns the index of the next value to be added to D.  This
   value is the number of `union value's that need to be
   allocated to store a case for dictionary D. */
int
dict_get_next_value_idx (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->next_value_idx;
}

/* Returns the number of bytes needed to store a case for
   dictionary D. */
size_t
dict_get_case_size (const struct dictionary *d) 
{
  assert (d != NULL);

  return sizeof (union value) * dict_get_next_value_idx (d);
}

/* Deletes scratch variables in dictionary D and reassigns values
   so that fragmentation is eliminated. */
void
dict_compact_values (struct dictionary *d) 
{
  size_t i;

  d->next_value_idx = 0;
  for (i = 0; i < d->var_cnt; )
    {
      struct variable *v = d->var[i];

      if (dict_class_from_id (v->name) != DC_SCRATCH) 
        {
          v->fv = d->next_value_idx;
          d->next_value_idx += v->nv;
          i++;
        }
      else
        dict_delete_var (default_dict, v);
    }
}

/* Copies values from SRC, which represents a case arranged
   according to dictionary D, to DST, which represents a case
   arranged according to the dictionary that will be produced by
   dict_compact_values(D). */
void
dict_compact_case (const struct dictionary *d,
                   struct ccase *dst, const struct ccase *src)
{
  size_t i;
  size_t value_idx;

  value_idx = 0;
  for (i = 0; i < d->var_cnt; i++) 
    {
      struct variable *v = d->var[i];

      if (dict_class_from_id (v->name) != DC_SCRATCH)
        {
          case_copy (dst, value_idx, src, v->fv, v->nv);
          value_idx += v->nv;
        }
    }
}

/* Returns the number of values that would be used by a case if
   dict_compact_values() were called. */
size_t
dict_get_compacted_value_cnt (const struct dictionary *d) 
{
  size_t i;
  size_t cnt;

  cnt = 0;
  for (i = 0; i < d->var_cnt; i++)
    if (dict_class_from_id (d->var[i]->name) != DC_SCRATCH) 
      cnt += d->var[i]->nv;
  return cnt;
}

/* Creates and returns an array mapping from a dictionary index
   to the `fv' that the corresponding variable will have after
   calling dict_compact_values().  Scratch variables receive -1
   for `fv' because dict_compact_values() will delete them. */
int *
dict_get_compacted_idx_to_fv (const struct dictionary *d) 
{
  size_t i;
  size_t next_value_idx;
  int *idx_to_fv;
  
  idx_to_fv = xmalloc (d->var_cnt * sizeof *idx_to_fv);
  next_value_idx = 0;
  for (i = 0; i < d->var_cnt; i++)
    {
      struct variable *v = d->var[i];

      if (dict_class_from_id (v->name) != DC_SCRATCH) 
        {
          idx_to_fv[i] = next_value_idx;
          next_value_idx += v->nv;
        }
      else 
        idx_to_fv[i] = -1;
    }
  return idx_to_fv;
}

/* Returns the SPLIT FILE vars (see cmd_split_file()).  Call
   dict_get_split_cnt() to determine how many SPLIT FILE vars
   there are.  Returns a null pointer if and only if there are no
   SPLIT FILE vars. */
struct variable *const *
dict_get_split_vars (const struct dictionary *d) 
{
  assert (d != NULL);
  
  return d->split;
}

/* Returns the number of SPLIT FILE vars. */
size_t
dict_get_split_cnt (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->split_cnt;
}

/* Sets CNT split vars SPLIT in dictionary D. */
void
dict_set_split_vars (struct dictionary *d,
                     struct variable *const *split, size_t cnt)
{
  assert (d != NULL);
  assert (cnt == 0 || split != NULL);

  d->split_cnt = cnt;
  d->split = xrealloc (d->split, cnt * sizeof *d->split);
  memcpy (d->split, split, cnt * sizeof *d->split);
}

/* Returns the file label for D, or a null pointer if D is
   unlabeled (see cmd_file_label()). */
const char *
dict_get_label (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->label;
}

/* Sets D's file label to LABEL, truncating it to a maximum of 60
   characters. */
void
dict_set_label (struct dictionary *d, const char *label) 
{
  assert (d != NULL);

  free (d->label);
  if (label == NULL)
    d->label = NULL;
  else if (strlen (label) < 60)
    d->label = xstrdup (label);
  else 
    {
      d->label = xmalloc (61);
      memcpy (d->label, label, 60);
      d->label[60] = '\0';
    }
}

/* Returns the documents for D, or a null pointer if D has no
   documents (see cmd_document()).. */
const char *
dict_get_documents (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->documents;
}

/* Sets the documents for D to DOCUMENTS, or removes D's
   documents if DOCUMENT is a null pointer. */
void
dict_set_documents (struct dictionary *d, const char *documents)
{
  assert (d != NULL);

  free (d->documents);
  if (documents == NULL)
    d->documents = NULL;
  else
    d->documents = xstrdup (documents);
}

/* Creates in D a vector named NAME that contains CNT variables
   VAR (see cmd_vector()).  Returns nonzero if successful, or
   zero if a vector named NAME already exists in D. */
int
dict_create_vector (struct dictionary *d,
                    const char *name,
                    struct variable **var, size_t cnt) 
{
  struct vector *vector;

  assert (d != NULL);
  assert (name != NULL);
  assert (strlen (name) > 0 && strlen (name) <= SHORT_NAME_LEN );
  assert (var != NULL);
  assert (cnt > 0);
  
  if (dict_lookup_vector (d, name) != NULL)
    return 0;

  d->vector = xrealloc (d->vector, (d->vector_cnt + 1) * sizeof *d->vector);
  vector = d->vector[d->vector_cnt] = xmalloc (sizeof *vector);
  vector->idx = d->vector_cnt++;
  strncpy (vector->name, name, SHORT_NAME_LEN);
  vector->name[SHORT_NAME_LEN] = '\0';
  vector->var = xmalloc (cnt * sizeof *var);
  memcpy (vector->var, var, cnt * sizeof *var);
  vector->cnt = cnt;
  
  return 1;
}

/* Returns the vector in D with index IDX, which must be less
   than dict_get_vector_cnt (D). */
const struct vector *
dict_get_vector (const struct dictionary *d, size_t idx) 
{
  assert (d != NULL);
  assert (idx < d->vector_cnt);

  return d->vector[idx];
}

/* Returns the number of vectors in D. */
size_t
dict_get_vector_cnt (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->vector_cnt;
}

/* Looks up and returns the vector within D with the given
   NAME. */
const struct vector *
dict_lookup_vector (const struct dictionary *d, const char *name) 
{
  size_t i;

  assert (d != NULL);
  assert (name != NULL);

  for (i = 0; i < d->vector_cnt; i++)
    if (!strcmp (d->vector[i]->name, name))
      return d->vector[i];
  return NULL;
}

/* Deletes all vectors from D. */
void
dict_clear_vectors (struct dictionary *d) 
{
  size_t i;
  
  assert (d != NULL);

  for (i = 0; i < d->vector_cnt; i++) 
    {
      free (d->vector[i]->var);
      free (d->vector[i]);
    }
  free (d->vector);
  d->vector = NULL;
  d->vector_cnt = 0;
}


static const char * quasi_base27(int i);


/* Convert I to quasi base 27 
   The result is a staticly allocated string.
*/
static const char *
quasi_base27(int i)
{
  static char result[SHORT_NAME_LEN + 1];
  static char reverse[SHORT_NAME_LEN + 1];

  /* FIXME: check the result cant overflow these arrays */

  char *s = result ;
  const int radix = 27;
  int units;

  /* and here's the quasi-ness of this routine */
  i = i + ( i / radix );

  strcpy(result,"");
  do {
    units = i % radix;
    *s++ = (units > 0 ) ? units + 'A' - 1 : 'A';
    i = i / radix; 
  } while (i > 0 ) ;
  *s = '\0';

  /* Reverse the result */
  i = strlen(result);
  s = reverse;
  while(i >= 0)
	*s++ = result[--i];
  *s = '\0';
	
  return reverse;
}


/* Generate a short name, given a long name.
   The return value of this function must be freed by the caller. 
*/
static char *
make_short_name(struct dictionary *dict, const char *longname)
{
  int i = 0;
  char *p;
 

  char *d = xmalloc ( SHORT_NAME_LEN + 1);

  /* Truncate the name */
  strncpy(d, longname, SHORT_NAME_LEN);
  d[SHORT_NAME_LEN] = '\0';

  /* Convert to upper case */
  for ( p = d; *p ; ++p )
	*p = toupper(*p);

  /* If a variable with that name already exists, then munge it
     until there's no conflict */
  while (0 != hsh_find (dict->name_tab, d))
  {
	const char *suffix = quasi_base27(i++);

        d[SHORT_NAME_LEN -  strlen(suffix) - 1 ] = '_';
        d[SHORT_NAME_LEN -  strlen(suffix)  ] = '\0';
	strcat(d, suffix);
  }


  return d;
}




