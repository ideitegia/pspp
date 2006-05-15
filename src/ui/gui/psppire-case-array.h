/* 
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2004  Free Software Foundation
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


#ifndef __CASE_ARRAY_H__
#define __CASE_ARRAY_H__


#include <glib-object.h>
#include <glib.h>



G_BEGIN_DECLS


/* --- type macros --- */
#define G_TYPE_PSPPIRE_CASE_ARRAY              (psppire_case_array_get_type ())
#define PSPPIRE_CASE_ARRAY(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), G_TYPE_PSPPIRE_CASE_ARRAY, PsppireCaseArray))
#define PSPPIRE_CASE_ARRAY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_PSPPIRE_CASE_ARRAY, PsppireCaseArrayClass))
#define G_IS_PSPPIRE_CASE_ARRAY(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), G_TYPE_PSPPIRE_CASE_ARRAY))
#define G_IS_PSPPIRE_CASE_ARRAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_PSPPIRE_CASE_ARRAY))
#define PSPPIRE_CASE_ARRAY_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_PSPPIRE_CASE_ARRAY, PsppireCaseArrayClass))




/* --- typedefs & structures --- */
typedef struct _PsppireCaseArray	   PsppireCaseArray;
typedef struct _PsppireCaseArrayClass PsppireCaseArrayClass;

struct ccase;

struct _PsppireCaseArray
{
  GObject             parent;

  struct ccase *cases;
  gint capacity;
  gint width;
  gint size;
};


struct _PsppireCaseArrayClass
{
  GObjectClass parent_class;
};


/* -- PsppireCaseArray --- */
GType          psppire_case_array_get_type (void);

PsppireCaseArray *psppire_case_array_new (gint capacity, gint width);

void psppire_case_array_resize(PsppireCaseArray *ca,  gint new_size);


void psppire_case_array_delete_cases(PsppireCaseArray *ca, gint first, gint n_cases);


typedef gboolean psppire_case_array_fill_case_func(struct ccase *, gpointer aux);

typedef gboolean psppire_case_array_use_case_func(const struct ccase *, gpointer aux);

gboolean psppire_case_array_insert_case(PsppireCaseArray *ca, gint posn,
					psppire_case_array_fill_case_func fill_case_func,
					gpointer aux);

inline gboolean psppire_case_array_append_case(PsppireCaseArray *ca, 
					psppire_case_array_fill_case_func fill_case_func,
					gpointer aux);


gboolean psppire_case_array_iterate_case(PsppireCaseArray *ca, 
				  psppire_case_array_use_case_func fill_case_func,
				  gpointer aux);



gint psppire_case_array_get_n_cases(const PsppireCaseArray *ca);


/* Clears the contents of CA */
void psppire_case_array_clear(PsppireCaseArray *ca);


const union value * psppire_case_array_get_value(const PsppireCaseArray *ca, 
					      gint c, gint idx);


typedef gboolean value_fill_func_t(union value *v, gpointer data);

void psppire_case_array_set_value(PsppireCaseArray *ca, gint c, gint idx,
			       value_fill_func_t ff,
			       gpointer data);

G_END_DECLS

#endif /* __PSPPIRE_CASE_ARRAY_H__ */
