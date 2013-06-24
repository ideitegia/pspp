/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2009, 2011, 2012  Free Software Foundation

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

#include "psppire-dict.h"

#define FIRST_CASE_NUMBER 1


G_BEGIN_DECLS

#define PSPPIRE_TYPE_DATA_STORE	       (psppire_data_store_get_type ())

#define PSPPIRE_DATA_STORE(obj)	\
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               PSPPIRE_TYPE_DATA_STORE, PsppireDataStore))

#define PSPPIRE_DATA_STORE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            PSPPIRE_TYPE_DATA_STORE,                    \
                            PsppireDataStoreClass))


#define PSPPIRE_IS_DATA_STORE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DATA_STORE))

#define PSPPIRE_IS_DATA_STORE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DATA_STORE))

#define PSPPIRE_DATA_STORE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              PSPPIRE_TYPE_DATA_STORE,                  \
                              PsppireDataStoreClass))

typedef struct _PsppireDataStore       PsppireDataStore;
typedef struct _PsppireDataStoreClass  PsppireDataStoreClass;

struct dictionary;


enum dict_signal_handler {
  VARIABLE_INSERTED,
  VARIABLE_CHANGED,
  VARIABLE_DELETED,
  n_dict_signals
};


struct datasheet;
struct casereader;

struct _PsppireDataStore
{
  GObject parent;

  /*< private >*/
  gboolean dispose_has_run ;
  PsppireDict *dict;
  struct datasheet *datasheet;

  gint dict_handler_id [n_dict_signals];
};

struct _PsppireDataStoreClass
{
  GObjectClass parent_class;
};


GType psppire_data_store_get_type (void) G_GNUC_CONST;
PsppireDataStore *psppire_data_store_new     (PsppireDict *dict);


void psppire_data_store_set_reader (PsppireDataStore *ds,
				    struct casereader *reader);

void psppire_data_store_set_dictionary (PsppireDataStore *data_store,
					PsppireDict *dict);

void psppire_data_store_clear (PsppireDataStore *data_store);

gboolean psppire_data_store_insert_new_case (PsppireDataStore *ds, casenumber posn);


gboolean psppire_data_store_delete_cases (PsppireDataStore *ds, casenumber first, casenumber count);


struct casereader * psppire_data_store_get_reader (PsppireDataStore *ds);

gchar *psppire_data_store_get_string (PsppireDataStore *,
                                      glong row, const struct variable *,
                                      bool use_value_label);
gboolean psppire_data_store_set_value (PsppireDataStore *,
                                       casenumber casenum,
                                       const struct variable *,
                                       const union value *);
gboolean psppire_data_store_set_string (PsppireDataStore *ds,
					const gchar *text,
					glong row, const struct variable *,
                                        gboolean use_value_label);


gboolean psppire_data_store_filtered (PsppireDataStore *ds,
				      glong row);


casenumber psppire_data_store_get_case_count (const PsppireDataStore *ds);
size_t psppire_data_store_get_value_count (const PsppireDataStore *ds);
const struct caseproto *psppire_data_store_get_proto (const PsppireDataStore *);



struct ccase *psppire_data_store_get_case (const PsppireDataStore *ds,
                                           casenumber casenum);




G_END_DECLS

#endif /* __PSPPIRE_DATA_STORE_H__ */
