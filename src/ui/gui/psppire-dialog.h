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


#ifndef __PSPPIRE_DIALOG_H__
#define __PSPPIRE_DIALOG_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkwindow.h>

#define PSPPIRE_RESPONSE_PASTE 1


G_BEGIN_DECLS

#define PSPPIRE_DIALOG_TYPE            (psppire_dialog_get_type ())
#define PSPPIRE_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_DIALOG_TYPE, PsppireDialog))
#define PSPPIRE_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_DIALOG_TYPE, PsppireDialogClass))
#define PSPPIRE_IS_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_DIALOG_TYPE))
#define PSPPIRE_IS_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_DIALOG_TYPE))


typedef struct _PsppireDialog       PsppireDialog;
typedef struct _PsppireDialogClass  PsppireDialogClass;

struct _PsppireDialog
{
  GtkWindow window;
  GtkWidget *box;

  /* Private */
  GMainLoop *loop;
  gint response;
};

struct _PsppireDialogClass
{
  GtkWindowClass parent_class;
};

GType          psppire_dialog_get_type        (void);
GtkWidget*     psppire_dialog_new             (void);
void           psppire_dialog_reload          (PsppireDialog *);
void           psppire_dialog_close           (PsppireDialog *);
gint           psppire_dialog_run             (PsppireDialog *);


G_END_DECLS

#endif /* __PSPPIRE_DIALOG_H__ */

