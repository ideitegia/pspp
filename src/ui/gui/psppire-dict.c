/* 
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2004, 2006  Free Software Foundation
    Written by John Darrington

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA. */


#include <string.h>
#include <stdlib.h>

#include <gtksheet/gtkextra-marshal.c>

#include "psppire-object.h"
#include "psppire-dict.h"
#include <data/format.h>
#include <data/dictionary.h>
#include <data/missing-values.h>
#include <data/value-labels.h>


#include "message-dialog.h"
#include "psppire-variable.h"

/* --- prototypes --- */
static void psppire_dict_class_init	(PsppireDictClass	*class);
static void psppire_dict_init	(PsppireDict		*dict);
static void psppire_dict_finalize	(GObject		*object);


/* --- variables --- */
static GObjectClass     *parent_class = NULL;

enum  {VARIABLE_CHANGED, 
       VARIABLE_INSERTED,
       VARIABLES_DELETED, 
       n_SIGNALS};

static guint signal[n_SIGNALS];

#define CACHE_CHUNK 5

/* --- functions --- */
/**
 * psppire_dict_get_type:
 * @returns: the type ID for accelerator groups.
 */
GType
psppire_dict_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info = {
	sizeof (PsppireDictClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) psppire_dict_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (PsppireDict),
	0,      /* n_preallocs */
	(GInstanceInitFunc) psppire_dict_init,
      };

      object_type = g_type_register_static (G_TYPE_PSPPIRE_OBJECT, "PsppireDict",
					    &object_info, 0);
    }

  return object_type;
}


static void
psppire_dict_class_init (PsppireDictClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = psppire_dict_finalize;

  signal[VARIABLE_CHANGED] =
    g_signal_new ("variable_changed",
		  G_TYPE_FROM_CLASS(class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE, 
		  1,
		  G_TYPE_INT);



  signal[VARIABLE_INSERTED] =
    g_signal_new ("variable_inserted",
		  G_TYPE_FROM_CLASS(class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE, 
		  1,
		  G_TYPE_INT);


  signal[VARIABLES_DELETED] =
    g_signal_new ("variables_deleted",
		  G_TYPE_FROM_CLASS(class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  gtkextra_VOID__INT_INT,
		  G_TYPE_NONE, 
		  2,
		  G_TYPE_INT,
		  G_TYPE_INT);

}

static void
psppire_dict_finalize (GObject *object)
{
  gint v;
  PsppireDict *d = PSPPIRE_DICT (object);
  
  for (v = 0 ; v < psppire_dict_get_var_cnt(d) ; ++v ) 
    g_free(d->variables[v]);

  g_free(d->variables);
  d->cache_size = 0;

  dict_destroy(d->dict);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
psppire_dict_init (PsppireDict *psppire_dict)
{
  psppire_dict->dict = dict_create();

  psppire_dict->variables = 0; 
  psppire_dict->cache_size = 0;
}

/**
 * psppire_dict_new:
 * @returns: a new #PsppireDict object
 * 
 * Creates a new #PsppireDict. 
 */
PsppireDict*
psppire_dict_new (void)
{
  return g_object_new (G_TYPE_PSPPIRE_DICT, NULL);
}


/**
 * psppire_dict_new_from_dict:
 * @returns: a new #PsppireDict object
 * 
 * Creates a new #PsppireDict. 
 */
PsppireDict*
psppire_dict_new_from_dict (struct dictionary *d)
{
  PsppireDict *new_dict = g_object_new (G_TYPE_PSPPIRE_DICT, NULL);
  new_dict->dict = d;
  new_dict->cache_size = dict_get_var_cnt(d);
  new_dict->variables = g_malloc0(sizeof(struct PsppireVariable *) * 
				  new_dict->cache_size);


  return new_dict;
}


/* Returns a valid name for a new variable in DICT.
   The return value is statically allocated */
static gchar * 
auto_generate_var_name(PsppireDict *dict)
{
  gint d = 0;
  static gchar name[10];


  while (g_snprintf(name, 10, "VAR%05d",d++),
	 psppire_dict_lookup_var(dict, name))
    ;

  return name;
}

/* Insert a new variable at posn IDX, with the name NAME.
   If NAME is null, then a name will be automatically assigned.
 */
void
psppire_dict_insert_variable(PsppireDict *d, gint idx, const gchar *name)
{
  struct variable *var ;
  gint i;
  g_return_if_fail(d);
  g_return_if_fail(G_IS_PSPPIRE_DICT(d));


  /* Invalidate the cache from IDX onwards */
  for ( i = idx ; i < d->cache_size ; ++i ) 
    {
      g_free(d->variables[i]);
      d->variables[i] = 0;
    }

  /* Ensure that the cache is large enough */
  if ( dict_get_var_cnt(d->dict) >= d->cache_size ) 
    {
      d->variables = g_realloc(d->variables, sizeof(struct PsppireVariable*) * 
			       (d->cache_size + CACHE_CHUNK));
      d->cache_size += CACHE_CHUNK;
    }

  /* Zero the new pointers */
  for ( ; i < d->cache_size ; ++i ) 
    {
      d->variables[i] = 0;
    }

  if ( ! name ) 
    name = auto_generate_var_name(d);
  
  var = dict_create_var(d->dict, name, 0);

  dict_reorder_var(d->dict, var, idx);

  d->variables[idx] = g_malloc(sizeof (struct PsppireVariable));
  d->variables[idx]->v = var;
  d->variables[idx]->dict = d;

  g_signal_emit(d, signal[VARIABLE_INSERTED], 0, idx );  
}

/* Delete N variables beginning at FIRST */
void
psppire_dict_delete_variables(PsppireDict *d, gint first, gint n)
{
  gint idx;
  g_return_if_fail(d);
  g_return_if_fail(d->dict);
  g_return_if_fail(G_IS_PSPPIRE_DICT(d));

  /* Invalidate all pvs from FIRST onwards */
  for ( idx = first ; idx < d->cache_size ; ++idx ) 
    {
      g_free(d->variables[idx]);
      d->variables[idx] = 0;
    }

  for (idx = 0 ; idx < n ; ++idx ) 
    {
      struct variable *var;

      /* Do nothing if it's out of bounds */
      if ( first >= dict_get_var_cnt (d->dict))
	break; 

      var = dict_get_var(d->dict, first);
      dict_delete_var (d->dict, var);
    }

  g_signal_emit(d, signal[VARIABLES_DELETED], 0, first, idx );  
}


void
psppire_dict_set_name(PsppireDict* d, gint idx, const gchar *name)
{
  struct variable *var;
  g_assert(d);
  g_assert(G_IS_PSPPIRE_DICT(d));


  if ( idx < dict_get_var_cnt(d->dict))
    {
      /* This is an existing variable? */
      var = dict_get_var(d->dict, idx);
      dict_rename_var(d->dict, var, name);
      g_signal_emit(d, signal[VARIABLE_CHANGED], 0, idx);
    }
  else
    {
      /* new variable */
      dict_create_var(d->dict, name, 0);
      g_signal_emit(d, signal[VARIABLE_INSERTED], 0, idx);
    }
}



/* Return the IDXth variable */
struct PsppireVariable *
psppire_dict_get_variable(PsppireDict *d, gint idx)
{
  struct PsppireVariable *var ;
  g_return_val_if_fail(d, NULL);
  g_return_val_if_fail(d->dict, NULL);

  if ( ! d->variables) 
    return NULL;
  
  if (idx < 0 || idx >= psppire_dict_get_var_cnt(d))
    return NULL;

  var = d->variables[idx] ; 

  if (! var ) 
    {
      var = g_malloc(sizeof (*var));
      var->dict = d;
      var->v = dict_get_var(d->dict, idx);
      d->variables[idx] = var;
    }
    
  return var;
}


/* Return the number of variables in the dictionary */
gint 
psppire_dict_get_var_cnt(const PsppireDict *d)
{
  g_return_val_if_fail(d, -1);
  g_return_val_if_fail(d->dict, -1);
  

  return dict_get_var_cnt(d->dict);
}


/* Return a variable by name.
   Return NULL if it doesn't exist
*/
struct variable *
psppire_dict_lookup_var (const PsppireDict *d, const gchar *name)
{
  g_return_val_if_fail(d, NULL);
  g_return_val_if_fail(d->dict, NULL);

  return dict_lookup_var(d->dict, name);
}


void
psppire_dict_var_changed(PsppireDict *d, gint idx)
{
  g_return_if_fail(d);

  g_signal_emit(d, signal[VARIABLE_CHANGED], 0, idx);
}


/* Clears the contents of D */
void 
psppire_dict_clear(PsppireDict *d)
{
  g_return_if_fail(d);
  g_return_if_fail(d->dict);

  {
    const gint n_vars = dict_get_var_cnt(d->dict);
    gint i;
  
    dict_clear(d->dict);

    /* Invalidate the entire cache */
    for ( i = 0 ; i < d->cache_size ; ++i ) 
      {
	g_free(d->variables[i]);
	d->variables[i] = 0;
      }

    g_signal_emit(d, signal[VARIABLES_DELETED], 0, 0, n_vars );  
  }
}



/* Return true is NAME would be a valid name of a variable to add to the 
   dictionary.  False otherwise. 
   If REPORT is true, then invalid names will be reported as such as errors
*/
gboolean
psppire_dict_check_name(const PsppireDict *dict, 
		     const gchar *name, gboolean report)
{
  if ( ! var_is_valid_name(name, report ) )
      return FALSE;

  if (psppire_dict_lookup_var(dict, name))
    {
      if ( report ) 
	msg(ME,"Duplicate variable name.");
      return FALSE;
    }

  return TRUE;
}


inline gint 
psppire_dict_get_next_value_idx (const PsppireDict *dict)
{
  return dict_get_next_value_idx(dict->dict);
}
