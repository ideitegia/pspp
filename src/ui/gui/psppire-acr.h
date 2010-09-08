/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010 Free Software Foundation, Inc.

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


#ifndef __PSPPIRE_ACR_H__
#define __PSPPIRE_ACR_H__

/*
  This widget is a GtkBox which looks roughly like:

  +-----------------------------+
  |+------------+  +----------+	|
  ||   Add      |  |	      |	|
  |+------------+  |	      |	|
  |                |	      |	|
  |+------------+  |	      |	|
  ||   Edit     |  |	      |	|
  |+------------+  |	      |	|
  |      	   |	      |	|
  |+------------+  |	      |	|
  ||  Remove    |  |	      |	|
  |+------------+  +----------+	|
  +-----------------------------+

  It interacts with an external widget, such as a GtkEntry.
  It maintains a list of items controlled by the three buttons.
  This implementation deals only with g_double values.
 */


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>


G_BEGIN_DECLS

#define PSPPIRE_ACR_TYPE            (psppire_acr_get_type ())
#define PSPPIRE_ACR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_ACR_TYPE, PsppireAcr))
#define PSPPIRE_ACR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_ACR_TYPE, PsppireAcrClass))
#define PSPPIRE_IS_ACR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_ACR_TYPE))
#define PSPPIRE_IS_ACR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_ACR_TYPE))


typedef struct _PsppireAcr       PsppireAcr;
typedef struct _PsppireAcrClass  PsppireAcrClass;

typedef gboolean (*GetValueFunc) (gint, GValue *, gpointer);
typedef gboolean (*EnabledFunc) (gpointer);
typedef void (*UpdateCallbackFunc) (gpointer);

struct _PsppireAcr
{
  GtkHBox parent;


  GtkListStore *list_store;

  GtkTreeView *tv;
  GtkTreeSelection *selection;

  /* The buttons */
  GtkWidget *add_button;
  GtkWidget *change_button;
  GtkWidget *remove_button;

  GetValueFunc get_value;
  gpointer get_value_data;

  EnabledFunc enabled;
  gpointer enabled_data;

  UpdateCallbackFunc update;
  gpointer update_data;
};


struct _PsppireAcrClass
{
  GtkHBoxClass parent_class;
};



GType       psppire_acr_get_type        (void);
GtkWidget*  psppire_acr_new             (void);

void        psppire_acr_set_model       (PsppireAcr *, GtkListStore *);
void        psppire_acr_set_get_value_func (PsppireAcr *, GetValueFunc,
					    gpointer);

void        psppire_acr_set_enable_func (PsppireAcr *, EnabledFunc, gpointer);

void        psppire_acr_set_enabled (PsppireAcr *, gboolean);
void        psppire_acr_set_entry  (PsppireAcr *, GtkEntry *);



G_END_DECLS

#endif /* __PSPPIRE_ACR_H__ */
