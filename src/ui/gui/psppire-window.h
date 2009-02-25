/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009  Free Software Foundation

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


#ifndef __PSPPIRE_WINDOW_H__
#define __PSPPIRE_WINDOW_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkaction.h>
#include <gtk/gtkmenushell.h>

G_BEGIN_DECLS


#define PSPPIRE_WINDOW_TYPE            (psppire_window_get_type ())
#define PSPPIRE_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_WINDOW_TYPE, PsppireWindow))
#define PSPPIRE_WINDOW_CLASS(class)    (G_TYPE_CHECK_CLASS_CAST ((class), \
    PSPPIRE_WINDOW_TYPE, PsppireWindowClass))
#define PSPPIRE_IS_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    PSPPIRE_WINDOW_TYPE))
#define PSPPIRE_IS_WINDOW_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
    PSPPIRE_WINDOW_TYPE))


typedef struct _PsppireWindow       PsppireWindow;
typedef struct _PsppireWindowClass  PsppireWindowClass;


struct _PsppireWindow
{
  GtkWindow parent;

  /* <private> */
  gchar *name;
  gchar *description;
  gchar *basename;

  GHashTable *menuitem_table;
  GtkMenuShell *menu;

  guint insert_handler;
  guint remove_handler;
  gboolean unsaved;
};

struct _PsppireWindowClass
{
  GtkWindowClass parent_class;
};

GType      psppire_window_get_type        (void);

const gchar * psppire_window_get_filename (PsppireWindow *);

void psppire_window_set_filename (PsppireWindow *w, const gchar *filename);

void psppire_window_minimise_all (void);

void psppire_window_set_unsaved (PsppireWindow *, gboolean );

gboolean psppire_window_get_unsaved (PsppireWindow *);


gboolean psppire_window_query_save (PsppireWindow *);


G_END_DECLS

#endif /* __PSPPIRE_WINDOW_H__ */
