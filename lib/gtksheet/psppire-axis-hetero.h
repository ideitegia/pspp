/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

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


#ifndef PSPPIRE_AXIS_HETERO_H__
#define PSPPIRE_AXIS_HETERO_H__


#include <glib-object.h>
#include <glib.h>

#include <libpspp/tower.h>
#include "psppire-axis.h"

G_BEGIN_DECLS


/* --- type macros --- */
#define G_TYPE_PSPPIRE_AXIS_HETERO              (psppire_axis_hetero_get_type ())
#define PSPPIRE_AXIS_HETERO(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), G_TYPE_PSPPIRE_AXIS_HETERO, PsppireAxisHetero))
#define PSPPIRE_AXIS_HETERO_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_PSPPIRE_AXIS_HETERO, PsppireAxisHeteroClass))
#define PSPPIRE_IS_AXIS_HETERO(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), G_TYPE_PSPPIRE_AXIS_HETERO))
#define PSPPIRE_IS_AXIS_HETERO_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_PSPPIRE_AXIS_HETERO))
#define PSPPIRE_AXIS_HETERO_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_PSPPIRE_AXIS_HETERO, PsppireAxisHeteroClass))



/* --- typedefs & structures --- */
typedef struct _PsppireAxisHetero	   PsppireAxisHetero;
typedef struct _PsppireAxisHeteroClass PsppireAxisHeteroClass;

struct pool;

struct _PsppireAxisHetero
{
  PsppireAxis  parent;

  struct tower tower;
  struct pool *pool;
};

struct _PsppireAxisHeteroClass
{
  PsppireAxisClass parent_class;
};

GType          psppire_axis_hetero_get_type (void);

PsppireAxisHetero*   psppire_axis_hetero_new (void);


/* Interface between axis and model */

void psppire_axis_hetero_clear (PsppireAxisHetero *a);

void psppire_axis_hetero_append (PsppireAxisHetero *a, gint size);

void psppire_axis_hetero_insert (PsppireAxisHetero *a, gint size, gint posn);

void psppire_axis_hetero_remove (PsppireAxisHetero *a, gint posn);

void psppire_axis_hetero_resize_unit (PsppireAxisHetero *a, gint size, gint posn);


G_END_DECLS

#endif /* PSPPIRE_AXIS_HETERO_H__ */
