/* 
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


#ifndef __CASE_FILE_H__
#define __CASE_FILE_H__


#include <glib-object.h>
#include <glib.h>



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
struct casefile;

struct _PsppireCaseFile
{
  GObject             parent;

  struct casefile *casefile;
};


struct _PsppireCaseFileClass
{
  GObjectClass parent_class;
};


/* -- PsppireCaseFile --- */
GType          psppire_case_file_get_type (void);

PsppireCaseFile *psppire_case_file_new (gint var_cnt);

gboolean psppire_case_file_append_case(PsppireCaseFile *cf, 
					     struct ccase *c);

gint psppire_case_file_get_case_count(const PsppireCaseFile *cf);


const union value * psppire_case_file_get_value(const PsppireCaseFile *cf, 
					      gint c, gint idx);

struct data_in;

gboolean psppire_case_file_set_value(PsppireCaseFile *cf, gint c, gint idx,
				 struct data_in *d_in);

void psppire_case_file_clear(PsppireCaseFile *cf);


G_END_DECLS

#endif /* __PSPPIRE_CASE_FILE_H__ */
