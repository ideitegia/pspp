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


#ifndef __CASE_FILE_H__
#define __CASE_FILE_H__


#include <glib-object.h>
#include <glib.h>

#include <libpspp/str.h>
#include <data/case.h>



G_BEGIN_DECLS


/* --- type macros --- */
#define G_TYPE_PSPPIRE_CASE_FILE              (psppire_case_file_get_type ())
#define PSPPIRE_CASE_FILE(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), G_TYPE_PSPPIRE_CASE_FILE, PsppireCaseFile))
#define PSPPIRE_CASE_FILE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_PSPPIRE_CASE_FILE, PsppireCaseFileClass))
#define G_IS_PSPPIRE_CASE_FILE(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), G_TYPE_PSPPIRE_CASE_FILE))
#define G_IS_PSPPIRE_CASE_FILE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_PSPPIRE_CASE_FILE))
#define PSPPIRE_CASE_FILE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_PSPPIRE_CASE_FILE, PsppireCaseFileClass))




/* --- typedefs & structures --- */
typedef struct _PsppireCaseFile	   PsppireCaseFile;
typedef struct _PsppireCaseFileClass PsppireCaseFileClass;

struct ccase;
struct casereader;

struct _PsppireCaseFile
{
  GObject             parent;

  /* <private> */
  struct datasheet *datasheet;
  gboolean      accessible;
};


struct _PsppireCaseFileClass
{
  GObjectClass parent_class;
};


/* -- PsppireCaseFile --- */
GType          psppire_case_file_get_type (void);

PsppireCaseFile *psppire_case_file_new (const struct casereader *);

gboolean psppire_case_file_insert_case (PsppireCaseFile *cf, struct ccase *c, casenumber row);

casenumber psppire_case_file_get_case_count (const PsppireCaseFile *cf);


union value * psppire_case_file_get_value (const PsppireCaseFile *cf,
                                           casenumber, size_t idx,
                                           union value *, int width);

struct fmt_spec;

gboolean psppire_case_file_data_in (PsppireCaseFile *cf, casenumber c, gint idx,
                                   struct substring input,
                                   const struct fmt_spec *);

gboolean psppire_case_file_set_value (PsppireCaseFile *cf, casenumber casenum,
				     gint idx, union value *v, gint width);

void psppire_case_file_clear (PsppireCaseFile *cf);


gboolean psppire_case_file_delete_cases (PsppireCaseFile *cf, casenumber n_rows,
					casenumber first);

gboolean psppire_case_file_insert_values (PsppireCaseFile *cf, gint n_values, gint where);

struct case_ordering;

void psppire_case_file_sort (PsppireCaseFile *cf, struct case_ordering *);

gboolean psppire_case_file_get_case (const PsppireCaseFile *cf, 
					casenumber casenum,
					struct ccase *c);


struct casereader * psppire_case_file_make_reader (PsppireCaseFile *cf);


G_END_DECLS

#endif /* __PSPPIRE_CASE_FILE_H__ */
