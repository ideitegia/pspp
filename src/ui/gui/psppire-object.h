/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2004  Free Software Foundation

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


#ifndef __PSPPIRE_OBJECT_H__
#define __PSPPIRE_OBJECT_H__

#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS


/* --- type macros --- */
#define G_TYPE_PSPPIRE_OBJECT              (psppire_object_get_type ())
#define PSPPIRE_OBJECT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), G_TYPE_PSPPIRE_OBJECT, PsppireObject))
#define PSPPIRE_OBJECT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_PSPPIRE_OBJECT, PsppireObjectClass))
#define G_IS_PSPPIRE_OBJECT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), G_TYPE_PSPPIRE_OBJECT))
#define G_IS_PSPPIRE_OBJECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_PSPPIRE_OBJECT))
#define PSPPIRE_OBJECT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_PSPPIRE_OBJECT, PsppireObjectClass))



/* --- typedefs & structures --- */
typedef struct _PsppireObject	   PsppireObject;
typedef struct _PsppireObjectClass PsppireObjectClass;


struct _PsppireObject
{
  GObject             parent;
};

struct _PsppireObjectClass
{
  GObjectClass parent_class;

};


/* -- PsppireObject --- */
GType          psppire_object_get_type(void);
PsppireObject*      psppire_object_new(void);

G_END_DECLS

#endif /* __PSPPIRE_OBJECT_H__ */
