/* 
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2004  Free Software Foundation
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


#ifndef __PSPPIRE_DICT_H__
#define __PSPPIRE_DICT_H__


#include <glib-object.h>
#include <glib.h>

#include <data/dictionary.h>
#include <data/variable.h>


G_BEGIN_DECLS


/* --- type macros --- */
#define G_TYPE_PSPPIRE_DICT              (psppire_dict_get_type ())
#define PSPPIRE_DICT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), G_TYPE_PSPPIRE_DICT, PsppireDict))
#define PSPPIRE_DICT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_PSPPIRE_DICT, PsppireDictClass))
#define G_IS_PSPPIRE_DICT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), G_TYPE_PSPPIRE_DICT))
#define G_IS_PSPPIRE_DICT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_PSPPIRE_DICT))
#define PSPPIRE_DICT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_PSPPIRE_DICT, PsppireDictClass))



/* --- typedefs & structures --- */
typedef struct _PsppireDict	   PsppireDict;
typedef struct _PsppireDictClass PsppireDictClass;

struct _PsppireDict
{
  GObject             parent;
  struct dictionary *dict;

  /* Cache of variables */
  struct PsppireVariable **variables;

  gint cache_size;
};

struct _PsppireDictClass
{
  GObjectClass parent_class;

};


/* -- PsppireDict --- */
GType          psppire_dict_get_type (void);
PsppireDict*     psppire_dict_new (void);
PsppireDict*     psppire_dict_new_from_dict (struct dictionary *d);
void           psppire_dict_set_name (PsppireDict* s, gint idx, const gchar *name);
void           psppire_dict_delete_var (PsppireDict *s, gint idx);

/* Return the number of variables in the dictionary */
gint psppire_dict_get_var_cnt(const PsppireDict *d);

/* Return a variable by name.
   Return NULL if it doesn't exist
*/
struct variable * psppire_dict_lookup_var (const PsppireDict *d, const gchar *name);

/* Tell the dictionary that one of its variable has changed */
void psppire_dict_var_changed(PsppireDict *d, gint idx);


/* Clears the contents of D */
void psppire_dict_clear(PsppireDict *d);

/* Return the IDXth variable */

struct PsppireVariable * psppire_dict_get_variable(PsppireDict *d, gint idx);

/* Delete N variables beginning at FIRST */
void psppire_dict_delete_variables(PsppireDict *d, gint first, gint n);

/* Insert a new variable at posn IDX */
void psppire_dict_insert_variable(PsppireDict *d, gint idx, const gchar *name);

gboolean psppire_dict_check_name(const PsppireDict *dict, 
			      const gchar *name, gboolean report);

gint psppire_dict_get_next_value_idx (const PsppireDict *dict);


G_END_DECLS

#endif /* __PSPPIRE_DICT_H__ */
