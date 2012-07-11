/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

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

#ifndef PSPPIRE_EMPTY_LIST_STORE_H
#define PSPPIRE_EMPTY_LIST_STORE_H 1

/* PsppireEmptyListStore is an GtkTreeModel implementation that has a
   client-specified number of rows and zero columns.  It is a useful model for
   GtkTreeView or PsppSheetView when the client can easily synthesize cell data
   using a callback set with gtk_tree_view_column_set_cell_data_func().  In
   that situation, GtkListStore can be wasteful (because it uses a lot of
   memory to store what does not need to be stored) and situation-specific
   custom models require additional boilerplate. */

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PSPPIRE_TYPE_EMPTY_LIST_STORE             (psppire_empty_list_store_get_type())
#define PSPPIRE_EMPTY_LIST_STORE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),PSPPIRE_TYPE_EMPTY_LIST_STORE,PsppireEmptyListStore))
#define PSPPIRE_EMPTY_LIST_STORE_CLASS(class)     (G_TYPE_CHECK_CLASS_CAST ((class),PSPPIRE_TYPE_EMPTY_LIST_STORE,PsppireEmptyListStoreClass))
#define PSPPIRE_IS_EMPTY_LIST_STORE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),PSPPIRE_TYPE_EMPTY_LIST_STORE))
#define PSPPIRE_IS_EMPTY_LIST_STORE_CLASS(class)  (G_TYPE_CHECK_CLASS_TYPE ((class),PSPPIRE_TYPE_EMPTY_LIST_STORE))
#define PSPPIRE_EMPTY_LIST_STORE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),PSPPIRE_TYPE_EMPTY_LIST_STORE,PsppireEmptyListStoreClass))

typedef struct _PsppireEmptyListStore      PsppireEmptyListStore;
typedef struct _PsppireEmptyListStoreClass PsppireEmptyListStoreClass;

struct _PsppireEmptyListStore {
  GObject parent;
  gint n_rows;
};

struct _PsppireEmptyListStoreClass {
  GObjectClass parent_class;
};

GType psppire_empty_list_store_get_type (void) G_GNUC_CONST;
PsppireEmptyListStore* psppire_empty_list_store_new (gint n_rows);

gint psppire_empty_list_store_get_n_rows (const PsppireEmptyListStore *);
void psppire_empty_list_store_set_n_rows (PsppireEmptyListStore *,
                                          gint n_rows);

void psppire_empty_list_store_row_changed (PsppireEmptyListStore *,
                                           gint row);
void psppire_empty_list_store_row_inserted (PsppireEmptyListStore *,
                                            gint row);
void psppire_empty_list_store_row_deleted (PsppireEmptyListStore *,
                                           gint row);

gint empty_list_store_iter_to_row (const GtkTreeIter *);

G_END_DECLS

#endif /* PSPPIRE_EMPTY_LIST_STORE_H */
