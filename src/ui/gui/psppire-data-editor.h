/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008 Free Software Foundation, Inc.

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

#ifndef __PSPPIRE_DATA_EDITOR_H__
#define __PSPPIRE_DATA_EDITOR_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtknotebook.h>

#include <lib/gtksheet/psppire-axis-impl.h>
#include "psppire-var-store.h"
#include "psppire-data-store.h"

G_BEGIN_DECLS

#define PSPPIRE_DATA_EDITOR_TYPE            (psppire_data_editor_get_type ())
#define PSPPIRE_DATA_EDITOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_DATA_EDITOR_TYPE, PsppireDataEditor))
#define PSPPIRE_DATA_EDITOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_DATA_EDITOR_TYPE, PsppireDataEditorClass))
#define PSPPIRE_IS_DATA_EDITOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_DATA_EDITOR_TYPE))
#define PSPPIRE_IS_DATA_EDITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_DATA_EDITOR_TYPE))


typedef struct _PsppireDataEditor       PsppireDataEditor;
typedef struct _PsppireDataEditorClass  PsppireDataEditorClass;

/* All members are private. */
struct _PsppireDataEditor
{
  GtkNotebook parent;

  /* <private> */
  gboolean dispose_has_run;
  GtkWidget *cell_ref_entry;
  GtkWidget *datum_entry;
  GtkWidget *var_sheet;
  PsppireDataStore *data_store;
  PsppireVarStore *var_store;

  GtkWidget *sheet_bin[4];
  GtkWidget *data_sheet[4];

  GtkWidget *data_vbox;

  GtkWidget *paned;
  gboolean split;

  PsppireAxisImpl *vaxis[2];

  /* There's only one horizontal axis, since the
     column widths are parameters of the variables */
  PsppireAxisImpl *haxis;
};


struct _PsppireDataEditorClass
{
  GtkNotebookClass parent_class;
};


GType          psppire_data_editor_get_type        (void);
GtkWidget*     psppire_data_editor_new             (PsppireVarStore *, PsppireDataStore *);
void           psppire_data_editor_clip_copy       (PsppireDataEditor *);
void           psppire_data_editor_clip_paste      (PsppireDataEditor *);
void           psppire_data_editor_clip_cut        (PsppireDataEditor *);
void           psppire_data_editor_sort_ascending  (PsppireDataEditor *);
void           psppire_data_editor_sort_descending (PsppireDataEditor *);
void           psppire_data_editor_insert_variable (PsppireDataEditor *);
void           psppire_data_editor_delete_variables (PsppireDataEditor *);
void           psppire_data_editor_show_grid       (PsppireDataEditor *, gboolean);
void           psppire_data_editor_insert_case     (PsppireDataEditor *);
void           psppire_data_editor_delete_cases    (PsppireDataEditor *);
void           psppire_data_editor_set_font        (PsppireDataEditor *, PangoFontDescription *);
void           psppire_data_editor_delete_cases    (PsppireDataEditor *);
void           psppire_data_editor_split_window    (PsppireDataEditor *, gboolean );


G_END_DECLS

enum {PSPPIRE_DATA_EDITOR_DATA_VIEW = 0, PSPPIRE_DATA_EDITOR_VARIABLE_VIEW};


#endif /* __PSPPIRE_DATA_EDITOR_H__ */
