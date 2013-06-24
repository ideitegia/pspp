/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2009, 2011  Free Software Foundation

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


#ifndef __PSPPIRE_DICT_H__
#define __PSPPIRE_DICT_H__


#include <glib-object.h>
#include <glib.h>

#include <data/dictionary.h>
#include <data/variable.h>


G_BEGIN_DECLS


/* --- type macros --- */
#define PSPPIRE_TYPE_DICT              (psppire_dict_get_type ())
#define PSPPIRE_DICT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), PSPPIRE_TYPE_DICT, PsppireDict))
#define PSPPIRE_DICT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_TYPE_DICT, PsppireDictClass))
#define PSPPIRE_IS_DICT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), PSPPIRE_TYPE_DICT))
#define PSPPIRE_IS_DICT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DICT))
#define PSPPIRE_DICT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), PSPPIRE_TYPE_DICT, PsppireDictClass))


/* --- typedefs & structures --- */
typedef struct _PsppireDict	   PsppireDict;
typedef struct _PsppireDictClass PsppireDictClass;

enum {DICT_TVM_COL_NAME=0, DICT_TVM_COL_VAR, DICT_TVM_COL_LABEL, n_DICT_COLS} ;

struct _PsppireDict
{
  GObject             parent;
  struct dictionary *dict;

  gboolean disable_insert_signal;
  /* For GtkTreeModelIface */
  gint stamp;
};

struct _PsppireDictClass
{
  GObjectClass parent_class;
};


/* -- PsppireDict --- */
GType          psppire_dict_get_type (void);
PsppireDict*   psppire_dict_new_from_dict (struct dictionary *d);
gboolean       psppire_dict_set_name (PsppireDict* s, gint idx, const gchar *name);
void           psppire_dict_delete_var (PsppireDict *s, gint idx);

/* Return the number of variables in the dictionary */
gint psppire_dict_get_var_cnt (const PsppireDict *d);

/* Return the number of `union value's in the dictionary */
size_t psppire_dict_get_value_cnt (const PsppireDict *d);

/* Returns the prototype for the cases that match the dictionary */
const struct caseproto *psppire_dict_get_proto (const PsppireDict *d);

/* Return a variable by name.
   Return NULL if it doesn't exist
*/
struct variable * psppire_dict_lookup_var (const PsppireDict *d, const gchar *name);

/* Clears the contents of D */
void psppire_dict_clear (PsppireDict *d);

/* Return the IDXth variable */
struct variable * psppire_dict_get_variable (const PsppireDict *d, gint idx);

/* Delete N variables beginning at FIRST */
void psppire_dict_delete_variables (PsppireDict *d, gint first, gint n);

/* Insert a new variable at posn IDX */
struct variable *psppire_dict_insert_variable (PsppireDict *d, gint idx,
                                               const gchar *name);

gboolean psppire_dict_check_name (const PsppireDict *dict,
			      const gchar *name, gboolean report);

bool psppire_dict_generate_name (const PsppireDict *, char *name, size_t size);

gint psppire_dict_get_next_value_idx (const PsppireDict *dict);

gboolean psppire_dict_rename_var (PsppireDict *dict, struct variable *v,
			      const gchar *text);

void psppire_dict_replace_dictionary (PsppireDict *, struct dictionary *);

struct variable * psppire_dict_get_weight_variable (const PsppireDict *);

#if DEBUGGING
void psppire_dict_dump (const PsppireDict *);
#endif

const gchar *psppire_dict_encoding (const PsppireDict *);

G_END_DECLS

#endif /* __PSPPIRE_DICT_H__ */
