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
#include "psppire-axis.h"
#include <gtk/gtk.h>


#define PSPPIRE_AXIS_GET_IFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), PSPPIRE_TYPE_AXIS_IFACE, PsppireAxisIface))

GType
psppire_axis_iface_get_type (void)
{
  static GType psppire_axis_iface_type = 0;

  if (! psppire_axis_iface_type)
    {
      static const GTypeInfo psppire_axis_iface_info =
      {
        sizeof (PsppireAxisIface), /* class_size */
	NULL,           /* base init */
	NULL,		/* base_finalize */
	NULL,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      psppire_axis_iface_type =
	g_type_register_static (G_TYPE_INTERFACE, "PsppireAxisIface",
				&psppire_axis_iface_info, 0);
    }

  return psppire_axis_iface_type;
}

G_DEFINE_ABSTRACT_TYPE(PsppireAxis, psppire_axis, G_TYPE_OBJECT);



/* --- prototypes --- */
static void psppire_axis_class_init (PsppireAxisClass	*class);
static void psppire_axis_init	(PsppireAxis		*axis);
static void psppire_axis_finalize   (GObject		*object);


/* --- variables --- */
static GObjectClass     *parent_class = NULL;



enum
  {
    PROP_0,
    PROP_MIN_EXTENT,
    PROP_DEFAULT_SIZE
  };


static void
psppire_axis_get_property (GObject         *object,
			   guint            prop_id,
			   GValue          *value,
			   GParamSpec      *pspec)
{
  PsppireAxis *axis = PSPPIRE_AXIS (object);

  switch (prop_id)
    {
    case PROP_MIN_EXTENT:
      g_value_set_long (value, axis->min_extent);
      break;
    case PROP_DEFAULT_SIZE:
      g_value_set_int (value, axis->default_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_axis_set_property (GObject         *object,
			   guint            prop_id,
			   const GValue    *value,
			   GParamSpec      *pspec)
{
  PsppireAxis *axis = PSPPIRE_AXIS (object);

  switch (prop_id)
    {
    case PROP_MIN_EXTENT:
      axis->min_extent = g_value_get_long (value);
      break;
    case PROP_DEFAULT_SIZE:
      axis->default_size = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
psppire_axis_class_init (PsppireAxisClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GParamSpec *min_extent_spec;
  GParamSpec *default_size_spec;

  object_class->set_property = psppire_axis_set_property;
  object_class->get_property = psppire_axis_get_property;

  min_extent_spec =
    g_param_spec_long ("minimum-extent",
		       "Minimum Extent",
		       "The smallest extent to which the axis will provide units (typically set to the height/width of the associated widget).",
		       0, G_MAXLONG,
		       0,
		       G_PARAM_CONSTRUCT | G_PARAM_WRITABLE | G_PARAM_READABLE );

  g_object_class_install_property (object_class,
                                   PROP_MIN_EXTENT,
                                   min_extent_spec);


  default_size_spec =
    g_param_spec_int ("default-size",
		      "Default Size",
		      "The size given to units which haven't been explicity inserted",
		      0, G_MAXINT,
		      25,
		      G_PARAM_CONSTRUCT | G_PARAM_WRITABLE | G_PARAM_READABLE );


  g_object_class_install_property (object_class,
                                   PROP_DEFAULT_SIZE,
                                   default_size_spec);

  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = psppire_axis_finalize;
}


static void
psppire_axis_init (PsppireAxis *axis)
{
}


static void
psppire_axis_finalize (GObject *object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

gint
psppire_axis_unit_size (const PsppireAxis *a, gint unit)
{
  g_return_val_if_fail (PSPPIRE_IS_AXIS (a), -1);

  g_return_val_if_fail (PSPPIRE_AXIS_GET_IFACE (a)->unit_size, -1);


  if  (unit >= PSPPIRE_AXIS_GET_IFACE (a)->unit_count(a))
    return a->default_size;

  return PSPPIRE_AXIS_GET_IFACE (a)->unit_size (a, unit);
}

gint
psppire_axis_unit_count (const PsppireAxis *a)
{
  glong padding = 0;
  glong actual_size;

  g_return_val_if_fail (PSPPIRE_IS_AXIS (a), -1);
  g_return_val_if_fail (PSPPIRE_AXIS_GET_IFACE (a)->unit_count, -1);

  actual_size = PSPPIRE_AXIS_GET_IFACE (a)->total_size (a);

  if ( actual_size < a->min_extent )
    padding = (a->min_extent - actual_size) / a->default_size;

  return PSPPIRE_AXIS_GET_IFACE (a)->unit_count (a) + padding;
}


/* Return the starting pixel of UNIT */
glong
psppire_axis_pixel_start (const PsppireAxis *a, gint unit)
{
  gint the_count, total_size ;
  g_return_val_if_fail (PSPPIRE_IS_AXIS (a), -1);

  the_count =  PSPPIRE_AXIS_GET_IFACE (a)->unit_count (a);
  total_size = PSPPIRE_AXIS_GET_IFACE (a)->total_size (a);

  if ( unit >= the_count)
    {
      return  total_size + (unit - the_count) * a->default_size;
    }

  //  g_print ("%s %d\n", __FUNCTION__, unit);

  return PSPPIRE_AXIS_GET_IFACE (a)->pixel_start (a, unit);
}


/* Return the unit covered by PIXEL */
gint
psppire_axis_get_unit_at_pixel (const PsppireAxis *a, glong pixel)
{
  glong total_size;

  g_return_val_if_fail (PSPPIRE_IS_AXIS (a), -1);

  g_return_val_if_fail (PSPPIRE_AXIS_GET_IFACE (a), -1);

  g_return_val_if_fail (PSPPIRE_AXIS_GET_IFACE (a)->get_unit_at_pixel, -1);

  total_size = PSPPIRE_AXIS_GET_IFACE (a)->total_size (a);

  if (pixel >= total_size)
    {
      gint n_items = PSPPIRE_AXIS_GET_IFACE (a)->unit_count (a);
      glong extra = pixel - total_size;

      return n_items - 1 + extra / a->default_size;
    }

  return PSPPIRE_AXIS_GET_IFACE (a)->get_unit_at_pixel (a, pixel);
}
