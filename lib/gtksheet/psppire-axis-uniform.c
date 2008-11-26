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

#include <config.h>
#include <string.h>
#include <stdlib.h>

#include <libpspp/tower.h>
#include <libpspp/pool.h>
#include "psppire-axis-uniform.h"
#include <gtk/gtk.h>


/* --- prototypes --- */
static void psppire_axis_uniform_class_init (PsppireAxisUniformClass	*class);
static void psppire_axis_uniform_init	(PsppireAxisUniform		*axis);
static void psppire_axis_uniform_finalize   (GObject		*object);


/* --- variables --- */
static GObjectClass     *parent_class = NULL;


#define UNIT_SIZE 25

static gint
get_unit_at_pixel (const PsppireAxis *a, glong pixel)
{
  gint unit_size;
  PsppireAxisUniform *au = PSPPIRE_AXIS_UNIFORM (a);

  g_object_get (au, "default-size", &unit_size, NULL);

  return pixel / unit_size;
}


static gint
unit_count (const PsppireAxis *a)
{
  PsppireAxisUniform *au = PSPPIRE_AXIS_UNIFORM (a);

  return au->n_items;
}


static glong
pixel_start (const PsppireAxis *a, gint unit)
{
  gint unit_size;
  PsppireAxisUniform *au = PSPPIRE_AXIS_UNIFORM (a);

  g_object_get (au, "default-size", &unit_size, NULL);

  return unit * unit_size;
}


static gint
unit_size (const PsppireAxis *a, gint unit)
{
  gint unit_size;
  PsppireAxisUniform *au = PSPPIRE_AXIS_UNIFORM (a);

  g_object_get (au, "default-size", &unit_size, NULL);

  return unit_size;
}


static glong
total_size (const PsppireAxis *a)
{
  gint unit_size;
  PsppireAxisUniform *au = PSPPIRE_AXIS_UNIFORM (a);

  g_object_get (au, "default-size", &unit_size, NULL);

  return unit_size * au->n_items;
}



static void
psppire_uniform_iface_init (PsppireAxisIface *iface)
{
  iface->unit_size = unit_size;
  iface->unit_count = unit_count;
  iface->pixel_start = pixel_start;
  iface->get_unit_at_pixel = get_unit_at_pixel;
  iface->total_size = total_size;
}

/* --- functions --- */
/**
 * psppire_axis_uniform_get_type:
 * @returns: the type ID for accelerator groups.
 */
GType
psppire_axis_uniform_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info = {
	sizeof (PsppireAxisUniformClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) psppire_axis_uniform_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (PsppireAxisUniform),
	0,      /* n_preallocs */
	(GInstanceInitFunc) psppire_axis_uniform_init,
      };

      static const GInterfaceInfo interface_info =
      {
	(GInterfaceInitFunc) psppire_uniform_iface_init,
	NULL,
	NULL
      };


      object_type = g_type_register_static (G_TYPE_PSPPIRE_AXIS,
					    "PsppireAxisUniform",
					    &object_info, 0);


      g_type_add_interface_static (object_type,
				   PSPPIRE_TYPE_AXIS_IFACE,
				   &interface_info);
    }

  return object_type;
}

static void
psppire_axis_uniform_class_init (PsppireAxisUniformClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = psppire_axis_uniform_finalize;
}


static void
psppire_axis_uniform_init (PsppireAxisUniform *axis)
{
  axis->n_items = 0;
}


static void
psppire_axis_uniform_finalize (GObject *object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * psppire_axis_uniform_new:
 * @returns: a new #PsppireAxisUniform object
 *
 * Creates a new #PsppireAxisUniform.
 */
PsppireAxisUniform*
psppire_axis_uniform_new (void)
{
  return g_object_new (G_TYPE_PSPPIRE_AXIS_UNIFORM, NULL);
}






void
psppire_axis_uniform_set_count (PsppireAxisUniform *axis, gint n)
{
  axis->n_items = n;
}
