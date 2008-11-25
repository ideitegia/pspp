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


/* --- prototypes --- */
static void psppire_axis_class_init (PsppireAxisClass	*class);
static void psppire_axis_init	(PsppireAxis		*axis);
static void psppire_axis_finalize   (GObject		*object);


/* --- variables --- */
static GObjectClass     *parent_class = NULL;

/* --- functions --- */
/**
 * psppire_axis_get_type:
 * @returns: the type ID for accelerator groups.
 */
GType
psppire_axis_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info = {
	sizeof (PsppireAxisClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) psppire_axis_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (PsppireAxis),
	0,      /* n_preallocs */
	(GInstanceInitFunc) psppire_axis_init,
      };

      object_type = g_type_register_static (G_TYPE_OBJECT,
					    "PsppireAxis",
					    &object_info, 0);
    }

  return object_type;
}

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
    g_param_spec_pointer ("minimum-extent",
			  "Minimum Extent",
			  "The smallest extent to which the axis will provide units (typically set to the height/width of the associated widget)",
			  G_PARAM_WRITABLE | G_PARAM_READABLE );

  g_object_class_install_property (object_class,
                                   PROP_MIN_EXTENT,
                                   min_extent_spec);


  default_size_spec =
    g_param_spec_pointer ("default-size",
			  "Default Size",
			  "The size given to units which haven't been explicity inserted",
			  G_PARAM_WRITABLE | G_PARAM_READABLE );


  g_object_class_install_property (object_class,
                                   PROP_DEFAULT_SIZE,
                                   default_size_spec);

  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = psppire_axis_finalize;
}


static void
psppire_axis_init (PsppireAxis *axis)
{
  axis->min_extent = 800;
  axis->default_size = 75;
  axis->pool = NULL;
  psppire_axis_clear (axis);
}


static void
psppire_axis_finalize (GObject *object)
{
  PsppireAxis *a = PSPPIRE_AXIS (object);
  pool_destroy (a->pool);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * psppire_axis_new:
 * @returns: a new #PsppireAxis object
 *
 * Creates a new #PsppireAxis.
 */
PsppireAxis*
psppire_axis_new (void)
{
  return g_object_new (G_TYPE_PSPPIRE_AXIS, NULL);
}


gint
psppire_axis_unit_size (PsppireAxis *a, gint unit)
{
  const struct tower_node *node;
  if  (unit >= tower_count (&a->tower))
    return a->default_size;

  node = tower_get (&a->tower, unit);

  return tower_node_get_size (node);
}

gint
psppire_axis_unit_count (PsppireAxis *a)
{
  glong padding = 0;

  if ( tower_height (&a->tower) < a->min_extent )
    padding = (a->min_extent - tower_height (&a->tower))
      / a->default_size;

  return tower_count (&a->tower) + padding;
}


/* Return the starting pixel of UNIT */
glong
psppire_axis_pixel_start (PsppireAxis *a, gint unit)
{
  const struct tower_node *node;

  if ( unit >= tower_count (&a->tower))
    {
      return  tower_height (&a->tower) +
	(unit - tower_count (&a->tower)) * a->default_size;
    }

  node = tower_get (&a->tower, unit);
  return  tower_node_get_level (node);
}


/* Return the unit covered by PIXEL */
gint
psppire_axis_get_unit_at_pixel (PsppireAxis *a, glong pixel)
{
  const struct tower_node *node;
  unsigned long int node_start;

  if (pixel >= tower_height (&a->tower))
    {
      glong extra = pixel - tower_height (&a->tower);

      if ( extra > a->min_extent - tower_height (&a->tower))
	extra = a->min_extent - tower_height (&a->tower);

      return tower_count (&a->tower) - 1 + extra / a->default_size;
    }

  node = tower_lookup (&a->tower, pixel, &node_start);

  return  tower_node_get_index (node);
}

void
psppire_axis_append (PsppireAxis *a, gint size)
{
  struct tower_node *new = pool_malloc (a->pool, sizeof *new);

  tower_insert (&a->tower, size, new, NULL);
}



/* Insert a new unit of size SIZE before position POSN */
void
psppire_axis_insert (PsppireAxis *a, gint size, gint posn)
{
  struct tower_node *new = pool_malloc (a->pool, sizeof *new);

  struct tower_node *before = NULL;

  if ( posn != tower_count (&a->tower))
    before = tower_get (&a->tower, posn);

  tower_insert (&a->tower, size, new, before);
}


void
psppire_axis_remove (PsppireAxis *a, gint posn)
{
  struct tower_node *node = tower_get (&a->tower, posn);

  tower_delete (&a->tower, node);

  pool_free (a->pool, node);
}


void
psppire_axis_resize_unit (PsppireAxis *a, gint size, gint posn)
{
  struct tower_node *node = tower_get (&a->tower, posn);

  tower_resize (&a->tower, node, size);
}


void
psppire_axis_clear (PsppireAxis *a)
{
  pool_destroy (a->pool);
  a->pool = pool_create ();
  tower_init (&a->tower);
}


