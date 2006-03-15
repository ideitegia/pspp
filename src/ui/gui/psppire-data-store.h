/* psppire-data-store.h
 
   PSPPIRE --- A Graphical User Interface for PSPP
   Copyright (C) 2006  Free Software Foundation
   Written by John Darrington

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

#ifndef __PSPPIRE_DATA_STORE_H__
#define __PSPPIRE_DATA_STORE_H__

#include <gtksheet/gsheetmodel.h>
#include "psppire-dict.h"
#include "psppire-case-array.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GTK_TYPE_DATA_STORE	       (psppire_data_store_get_type ())

#define PSPPIRE_DATA_STORE(obj)	       (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
								    GTK_TYPE_DATA_STORE, PsppireDataStore))

#define PSPPIRE_DATA_STORE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), \
								 GTK_TYPE_DATA_STORE, \
                                                                 PsppireDataStoreClass))

#define PSPPIRE_IS_DATA_STORE(obj)	       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_DATA_STORE))

#define PSPPIRE_IS_DATA_STORE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_DATA_STORE))

#define PSPPIRE_DATA_STORE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
								   GTK_TYPE_DATA_STORE, \
								   PsppireDataStoreClass))

typedef struct _PsppireDataStore       PsppireDataStore;
typedef struct _PsppireDataStoreClass  PsppireDataStoreClass;

struct dictionary;

struct _PsppireDataStore
{
  GObject parent;

  /*< private >*/
  PsppireDict *dict;
  PsppireCaseArray *cases;
  PangoFontDescription *font_desc;
  gboolean show_labels;

  /* Geometry */
  gint margin_width;
};

struct _PsppireDataStoreClass
{
  GObjectClass parent_class;

  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};


inline GType psppire_data_store_get_type (void) G_GNUC_CONST;
PsppireDataStore *psppire_data_store_new     (PsppireDict *dict, PsppireCaseArray *cases);

void psppire_data_store_set_dictionary(PsppireDataStore *data_store, PsppireDict *dict);
void psppire_data_store_set_font(PsppireDataStore *store, PangoFontDescription *fd);

void psppire_data_store_show_labels(PsppireDataStore *store, gboolean show_labels);


struct file_handle;

void psppire_data_store_create_system_file(PsppireDataStore *store,
				   struct file_handle *handle);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __PSPPIRE_DATA_STORE_H__ */
