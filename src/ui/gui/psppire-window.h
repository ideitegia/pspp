/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2013, 2014  Free Software Foundation

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
#include <gtk/gtk.h>
#include "psppire-window-base.h"

G_BEGIN_DECLS


#define PSPPIRE_TYPE_WINDOW            (psppire_window_get_type ())

#define PSPPIRE_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
    PSPPIRE_TYPE_WINDOW, PsppireWindow))

#define PSPPIRE_WINDOW_CLASS(class)    (G_TYPE_CHECK_CLASS_CAST ((class), \
    PSPPIRE_TYPE_WINDOW, PsppireWindowClass))

#define PSPPIRE_IS_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    PSPPIRE_TYPE_WINDOW))

#define PSPPIRE_IS_WINDOW_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
    PSPPIRE_TYPE_WINDOW))



#define PSPPIRE_TYPE_WINDOW_MODEL            (psppire_window_model_get_type ())

#define PSPPIRE_IS_WINDOW_MODEL(obj)	       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_WINDOW_MODEL))

#define PSPPIRE_WINDOW_MODEL_GET_IFACE(obj) \
   (G_TYPE_INSTANCE_GET_INTERFACE ((obj), PSPPIRE_TYPE_WINDOW_MODEL, PsppireWindowIface))


typedef struct _PsppireWindow       PsppireWindow;
typedef struct _PsppireWindowClass  PsppireWindowClass;
typedef struct _PsppireWindowIface  PsppireWindowIface;


struct _PsppireWindow
{
  PsppireWindowBase parent;

  /* <private> */
  gchar *filename;             /* File name, in file name encoding, or NULL. */
  gchar *basename;             /* Last component of filename, in UTF-8 */
  gchar *id;                   /* Dataset name, or NULL.  */
  gchar *description;          /* e.g. "Data Editor" */
  gchar *list_name;            /* Name for "Windows" menu list. */

  GHashTable *menuitem_table;
  GtkMenuShell *menu;

  guint insert_handler;
  guint remove_handler;

  gboolean added_separator;
  gboolean dirty;
  GTimeVal savetime;
};


struct _PsppireWindowClass
{
  PsppireWindowBaseClass parent_class;
};


struct _PsppireWindowIface
{
  GTypeInterface g_iface;

  void (*save) (PsppireWindow *w);
  void (*pick_filename) (PsppireWindow *);
  gboolean (*load) (PsppireWindow *w, const gchar *filename,
                    const gchar *encoding, gpointer hint);
};


GType      psppire_window_get_type        (void);
GType      psppire_window_model_get_type        (void);

const gchar * psppire_window_get_filename (PsppireWindow *);

void psppire_window_set_filename (PsppireWindow *w, const gchar *filename);

void psppire_window_minimise_all (void);

void psppire_window_set_unsaved (PsppireWindow *);

gboolean psppire_window_get_unsaved (PsppireWindow *);

gint psppire_window_query_save (PsppireWindow *);

void psppire_window_save (PsppireWindow *w);
void psppire_window_save_as (PsppireWindow *w);
gboolean psppire_window_load (PsppireWindow *w, const gchar *file,
                              const gchar *encoding, gpointer hint);
void psppire_window_open (PsppireWindow *de);
GtkWidget *psppire_window_file_chooser_dialog (PsppireWindow *toplevel);

void add_most_recent (const char *file_name, const char *mime_type,
                      const char *encoding);

G_END_DECLS

#endif /* __PSPPIRE_WINDOW_H__ */
