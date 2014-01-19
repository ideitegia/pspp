/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2013  Free Software Foundation

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


#ifndef __PSPPIRE_CHECKBOX_TREEVIEW_H__
#define __PSPPIRE_CHECKBOX_TREEVIEW_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS


#define PSPPIRE_TYPE_CHECKBOX_TREEVIEW            (psppire_checkbox_treeview_get_type ())

#define PSPPIRE_CHECKBOX_TREEVIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
    PSPPIRE_TYPE_CHECKBOX_TREEVIEW, PsppireCheckboxTreeview))

#define PSPPIRE_CHECKBOX_TREEVIEW_CLASS(class)    (G_TYPE_CHECK_CLASS_CAST ((class), \
    PSPPIRE_TYPE_CHECKBOX_TREEVIEW, PsppireCheckboxTreeviewClass))

#define PSPPIRE_IS_CHECKBOX_TREEVIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    PSPPIRE_TYPE_CHECKBOX_TREEVIEW))

#define PSPPIRE_IS_CHECKBOX_TREEVIEW_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
    PSPPIRE_TYPE_CHECKBOX_TREEVIEW))


typedef struct _PsppireCheckboxTreeview       PsppireCheckboxTreeview;
typedef struct _PsppireCheckboxTreeviewClass  PsppireCheckboxTreeviewClass;


struct _PsppireCheckboxTreeview
{
  GtkTreeView parent;

  /* <private> */
  GtkTreeModel *list;
};


struct _PsppireCheckboxTreeviewClass
{
  GtkTreeViewClass parent_class;
};



GType      psppire_checkbox_treeview_get_type        (void);
GType      psppire_checkbox_treeview_model_get_type        (void);


struct checkbox_entry_item
  {
    const char *name;
    const char *label;
    const char *tooltip;
  };

enum
  {
    CHECKBOX_COLUMN_LABEL,
    CHECKBOX_COLUMN_SELECTED,
    CHECKBOX_COLUMN_TOOLTIP,
    N_CHECKBOX_COLUMNS
  };

void psppire_checkbox_treeview_populate (PsppireCheckboxTreeview *pctv,
					 guint default_items,
					 gint n_items,
					 const struct checkbox_entry_item *items);

G_END_DECLS

#endif /* __PSPPIRE_CHECKBOX_TREEVIEW_H__ */
