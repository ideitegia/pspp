/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

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


#ifndef __PSPPIRE_DATA_WINDOW_H__
#define __PSPPIRE_DATA_WINDOW_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkaction.h>
#include "psppire-window.h"
#include "psppire-data-editor.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PSPPIRE_DATA_WINDOW_TYPE            (psppire_data_window_get_type ())
#define PSPPIRE_DATA_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_DATA_WINDOW_TYPE, PsppireDataWindow))
#define PSPPIRE_DATA_WINDOW_CLASS(class)    (G_TYPE_CHECK_CLASS_CAST ((class), \
    PSPPIRE_DATA_WINDOW_TYPE, PsppireData_WindowClass))
#define PSPPIRE_IS_DATA_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    PSPPIRE_DATA_WINDOW_TYPE))
#define PSPPIRE_IS_DATA_WINDOW_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
    PSPPIRE_DATA_WINDOW_TYPE))


typedef struct _PsppireDataWindow       PsppireDataWindow;
typedef struct _PsppireDataWindowClass  PsppireDataWindowClass;


struct _PsppireDataWindow
{
  PsppireWindow parent;

  /* <private> */
  PsppireDataEditor *data_editor;
  GtkBuilder *builder;


  GtkAction *invoke_goto_dialog;

  GtkAction *insert_variable;
  GtkAction *insert_case;
  GtkAction *delete_variables;
  GtkAction *delete_cases;


#if 0
  GtkMenu *data_sheet_variable_popup_menu;
  GtkMenu *data_sheet_cases_popup_menu;
  GtkMenu *var_sheet_variable_popup_menu;
#endif

  gboolean save_as_portable;
};

struct _PsppireDataWindowClass
{
  PsppireWindowClass parent_class;
};

GType      psppire_data_window_get_type        (void);
GtkWidget* psppire_data_window_new             (void);

G_END_DECLS

#endif /* __PSPPIRE_DATA_WINDOW_H__ */
