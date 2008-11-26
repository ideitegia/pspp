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
#include "psppire-axis-hetero.h"
#include <gtk/gtk.h>


/* --- prototypes --- */
static void psppire_axis_hetero_class_init (PsppireAxisHeteroClass	*class);
static void psppire_axis_hetero_init	(PsppireAxisHetero		*axis);
static void psppire_axis_hetero_finalize   (GObject		*object);


/* --- variables --- */
static GObjectClass     *parent_class = NULL;


static gint
get_unit_at_pixel (const PsppireAxis *a, glong pixel)
{
  PsppireAxisHetero *ah = PSPPIRE_AXIS_HETERO (a);
  const struct tower_node *node;
  unsigned long int node_start;

  if (pixel >= tower_height (&ah->tower))
    return tower_count (&ah->tower);

  node = tower_lookup (&ah->tower, pixel, &node_start);

  return tower_node_get_index (node);
}


static gint
unit_count (const PsppireAxis *a)
{
  PsppireAxisHetero *ah = PSPPIRE_AXIS_HETERO (a);
  return tower_count (&ah->tower);
}


static glong
pixel_start (const PsppireAxis *a, gint unit)
{
  PsppireAxisHetero *ah = PSPPIRE_AXIS_HETERO (a);
  const struct tower_node *node;

  if ( unit >= tower_count (&ah->tower))
    return tower_height (&ah->tower);

  node = tower_get (&ah->tower, unit);

  return  tower_node_get_level (node);
}


static gint
unit_size (const PsppireAxis *a, gint unit)
{
  PsppireAxisHetero *ah = PSPPIRE_AXIS_HETERO (a);
  const struct tower_node *node;
  if  (unit >= tower_count (&ah->tower))
    return 0;

  node = tower_get (&ah->tower, unit);

  return tower_node_get_size (node);
}


static glong
total_size (const PsppireAxis *a)
{
  glong s;
  PsppireAxisHetero *ah = PSPPIRE_AXIS_HETERO (a);

  s =  tower_height (&ah->tower);
  return s;
}

static void
psppire_hetero_iface_init (PsppireAxisIface *iface)
{
  iface->unit_size = unit_size;
  iface->unit_count = unit_count;
  iface->pixel_start = pixel_start;
  iface->get_unit_at_pixel = get_unit_at_pixel;
  iface->total_size = total_size;
}

/* --- functions --- */
/**
 * psppire_axis_hetero_get_type:
 * @returns: the type ID for accelerator groups.
 */
GType
psppire_axis_hetero_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info = {
	sizeof (PsppireAxisHeteroClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) psppire_axis_hetero_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (PsppireAxisHetero),
	0,      /* n_preallocs */
	(GInstanceInitFunc) psppire_axis_hetero_init,
      };


      static const GInterfaceInfo interface_info =
      {
	(GInterfaceInitFunc) psppire_hetero_iface_init,
	NULL,
	NULL
      };

      object_type = g_type_register_static (G_TYPE_PSPPIRE_AXIS,
					    "PsppireAxisHetero",
					    &object_info, 0);

      g_type_add_interface_static (object_type,
				   PSPPIRE_TYPE_AXIS_IFACE,
				   &interface_info);
    }

  return object_type;
}

static void
psppire_axis_hetero_class_init (PsppireAxisHeteroClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = psppire_axis_hetero_finalize;
}


static void
psppire_axis_hetero_init (PsppireAxisHetero *axis)
{
  axis->pool = NULL;
  psppire_axis_hetero_clear (axis);
}


static void
psppire_axis_hetero_finalize (GObject *object)
{
  PsppireAxisHetero *a = PSPPIRE_AXIS_HETERO (object);
  pool_destroy (a->pool);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * psppire_axis_hetero_new:
 * @returns: a new #PsppireAxisHetero object
 *
 * Creates a new #PsppireAxisHetero.
 */
PsppireAxisHetero*
psppire_axis_hetero_new (void)
{
  return g_object_new (G_TYPE_PSPPIRE_AXIS_HETERO, NULL);
}


void
psppire_axis_hetero_append (PsppireAxisHetero *a, gint size)
{
  struct tower_node *new ;

  g_return_if_fail (PSPPIRE_IS_AXIS_HETERO (a));

  new = pool_malloc (a->pool, sizeof *new);

  tower_insert (&a->tower, size, new, NULL);
}



/* Insert a new unit of size SIZE before position POSN */
void
psppire_axis_hetero_insert (PsppireAxisHetero *a, gint size, gint posn)
{
  struct tower_node *new;
  struct tower_node *before = NULL;

  g_return_if_fail (PSPPIRE_IS_AXIS_HETERO (a));

  new = pool_malloc (a->pool, sizeof *new);

  if ( posn != tower_count (&a->tower))
    before = tower_get (&a->tower, posn);

  tower_insert (&a->tower, size, new, before);
}


void
psppire_axis_hetero_remove (PsppireAxisHetero *a, gint posn)
{
  struct tower_node *node;

  g_return_if_fail (PSPPIRE_IS_AXIS_HETERO (a));

  node = tower_get (&a->tower, posn);

  tower_delete (&a->tower, node);

  pool_free (a->pool, node);
}


void
psppire_axis_hetero_resize_unit (PsppireAxisHetero *a, gint size, gint posn)
{
  struct tower_node *node;

  g_return_if_fail (PSPPIRE_IS_AXIS_HETERO (a));

  node = tower_get (&a->tower, posn);

  tower_resize (&a->tower, node, size);
}


void
psppire_axis_hetero_clear (PsppireAxisHetero *a)
{
  g_return_if_fail (PSPPIRE_IS_AXIS_HETERO (a));

  pool_destroy (a->pool);
  a->pool = pool_create ();
  tower_init (&a->tower);
}


