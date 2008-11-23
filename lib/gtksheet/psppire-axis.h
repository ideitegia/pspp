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


#ifndef PSPPIRE_AXIS_H__
#define PSPPIRE_AXIS_H__


#include <glib-object.h>
#include <glib.h>

#include <libpspp/tower.h>

G_BEGIN_DECLS


/* --- type macros --- */
#define G_TYPE_PSPPIRE_AXIS              (psppire_axis_get_type ())
#define PSPPIRE_AXIS(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), G_TYPE_PSPPIRE_AXIS, PsppireAxis))
#define PSPPIRE_AXIS_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_PSPPIRE_AXIS, PsppireAxisClass))
#define PSPPIRE_IS_AXIS(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), G_TYPE_PSPPIRE_AXIS))
#define PSPPIRE_IS_AXIS_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_PSPPIRE_AXIS))
#define PSPPIRE_AXIS_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_PSPPIRE_AXIS, PsppireAxisClass))



/* --- typedefs & structures --- */
typedef struct _PsppireAxis	   PsppireAxis;
typedef struct _PsppireAxisClass PsppireAxisClass;


struct _PsppireAxis
{
  GObject             parent;
  gint width ;

  struct tower tower;
};

struct _PsppireAxisClass
{
  GObjectClass parent_class;

};

GType          psppire_axis_get_type (void);

PsppireAxis*   psppire_axis_new (gint w);


/* Interface between sheet and axis */

gint psppire_axis_unit_size (PsppireAxis *a, gint unit);

gint psppire_axis_unit_count (PsppireAxis *a);

glong psppire_axis_pixel_start (PsppireAxis *a, gint unit);

gint psppire_axis_get_unit_at_pixel (PsppireAxis *a, glong pixel);



/* Interface between axis and model */

void psppire_axis_append (PsppireAxis *a, gint width);

G_END_DECLS

#endif /* PSPPIRE_AXIS_H__ */
