/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2007  Free Software Foundation

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


#ifndef __PSPPIRE_VAR_SELECT_H__
#define __PSPPIRE_VAR_SELECT_H__


#include <glib-object.h>
#include <glib.h>

#include <gtk/gtk.h>
#include "psppire-dict.h"

G_BEGIN_DECLS


/* --- type macros --- */
#define G_TYPE_PSPPIRE_VAR_SELECT              (psppire_var_select_get_type ())
#define PSPPIRE_VAR_SELECT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), G_TYPE_PSPPIRE_VAR_SELECT, PsppireVarSelect))
#define PSPPIRE_VAR_SELECT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_PSPPIRE_VAR_SELECT, PsppireVarSelectClass))
#define G_IS_PSPPIRE_VAR_SELECT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), G_TYPE_PSPPIRE_VAR_SELECT))
#define G_IS_PSPPIRE_VAR_SELECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_PSPPIRE_VAR_SELECT))
#define PSPPIRE_VAR_SELECT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_PSPPIRE_VAR_SELECT, PsppireVarSelectClass))



/* --- typedefs & structures --- */
typedef struct _PsppireVarSelect	   PsppireVarSelect;
typedef struct _PsppireVarSelectClass PsppireVarSelectClass;

struct _PsppireVarSelect
{
  GObject             parent;
  const PsppireDict   *dict;


  /* <private> */
  GtkSelectionMode    mode;
  GtkWidget           *source;
  GtkWidget           *dest;
  GList               *list;
};

struct _PsppireVarSelectClass
{
  GObjectClass parent_class;
};


/* -- PsppireVarSelect --- */
GType          psppire_var_select_get_type (void);


PsppireVarSelect* psppire_var_select_new    (GtkWidget *,
					     GtkWidget *,
					     const PsppireDict *);

/* Remove all variables from the selection */
void         psppire_var_select_deselect_all  (PsppireVarSelect *);

/* Return a list of all the currently selected variables */
const GList *psppire_var_select_get_variables (PsppireVarSelect *);

/* Append VAR to the list of selected variables */
void         psppire_var_select_set_variable  (PsppireVarSelect *,
					       struct variable *var);

G_END_DECLS

#endif /* __PSPPIRE_VAR_SELECT_H__ */
