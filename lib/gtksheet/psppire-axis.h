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

  glong min_extent;
  gint default_size;
};

struct _PsppireAxisClass
{
  GObjectClass parent_class;
};


GType          psppire_axis_get_type (void);




GType psppire_axis_iface_get_type (void);

#define PSPPIRE_TYPE_AXIS_IFACE (psppire_axis_iface_get_type ())

typedef struct _PsppireAxisIface PsppireAxisIface;

struct _PsppireAxisIface
{
  GTypeInterface g_iface;


  /* Virtual Table */

  gint  (*unit_size) (const PsppireAxis *a, gint unit);

  gint  (*unit_count) (const PsppireAxis *a);

  glong (*pixel_start) (const PsppireAxis *a, gint unit);

  gint  (*get_unit_at_pixel) (const PsppireAxis *a, glong pixel);

  glong (*total_size ) (const PsppireAxis *a);
};


/* Interface between sheet and axis */

gint psppire_axis_unit_size (const PsppireAxis *a, gint unit);

gint psppire_axis_unit_count (const PsppireAxis *a);

glong psppire_axis_pixel_start (const PsppireAxis *a, gint unit);

gint psppire_axis_get_unit_at_pixel (const PsppireAxis *a, glong pixel);


G_END_DECLS

#endif /* PSPPIRE_AXIS_H__ */
