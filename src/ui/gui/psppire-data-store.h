/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006  Free Software Foundation

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

#ifndef __PSPPIRE_DATA_STORE_H__
#define __PSPPIRE_DATA_STORE_H__

#include <gtksheet/gsheetmodel.h>
#include "psppire-dict.h"
#include "psppire-case-file.h"

#define FIRST_CASE_NUMBER 1


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
  PsppireCaseFile *case_file;
  const PangoFontDescription *font_desc;

  /* The width of an upper case 'M' rendered in the current font */
  gint width_of_m ;

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
PsppireDataStore *psppire_data_store_new     (PsppireDict *dict);

void psppire_data_store_set_case_file (PsppireDataStore *data_store,
				       PsppireCaseFile *cf);

void psppire_data_store_set_dictionary (PsppireDataStore *data_store,
					PsppireDict *dict);

void psppire_data_store_set_font (PsppireDataStore *store,
				 const PangoFontDescription *fd);

void psppire_data_store_show_labels (PsppireDataStore *store,
				    gboolean show_labels);

void psppire_data_store_clear (PsppireDataStore *data_store);

gboolean psppire_data_store_insert_new_case (PsppireDataStore *ds, gint posn);

struct casereader * psppire_data_store_get_reader (PsppireDataStore *ds);

gchar * psppire_data_store_get_string (PsppireDataStore *ds,
				       gint row, gint column);

gboolean psppire_data_store_set_string (PsppireDataStore *ds,
					const gchar *text,
					gint row, gint column);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __PSPPIRE_DATA_STORE_H__ */
