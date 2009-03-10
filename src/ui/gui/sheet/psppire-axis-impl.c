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
#include "psppire-axis-impl.h"
#include <math.h>


/* --- prototypes --- */
static void psppire_axis_impl_class_init (PsppireAxisImplClass	*class);
static void psppire_axis_impl_init	(PsppireAxisImpl		*axis);
static void psppire_axis_impl_finalize   (GObject		*object);


/* --- variables --- */
static GObjectClass     *parent_class = NULL;


struct axis_node
{
  struct tower_node pixel_node;
  struct tower_node unit_node;
};

void
psppire_axis_impl_dump (const PsppireAxisImpl *a)
{
  struct tower_node *n = tower_first (&a->unit_tower);

  g_debug ("Axis %p", a);
  while (n)
    {
      const struct axis_node *an = tower_data (n, struct axis_node, unit_node);
      const struct tower_node *pn = &an->pixel_node;
      g_debug ("%ld units of height %g",
	       n->size, pn->size / (gdouble) n->size);

      n =  tower_next (&a->unit_tower, n);
    }
  g_debug ("\n");
}

static gint
unit_at_pixel (const PsppireAxis *axis, glong pixel)
{
  PsppireAxisImpl *a = PSPPIRE_AXIS_IMPL (axis);

  unsigned long int start;
  struct tower_node *n;
  struct axis_node *an;
  gdouble fraction;

  g_return_val_if_fail (pixel >= 0, -1);

  n = tower_lookup (&a->pixel_tower, pixel, &start);
  an = tower_data (n, struct axis_node, pixel_node);

  fraction = (pixel - start) / (gdouble) tower_node_get_size (&an->pixel_node);

  return  tower_node_get_level (&an->unit_node)
    + fraction * tower_node_get_size (&an->unit_node);
}


static gint
unit_count (const PsppireAxis *axis)
{
  PsppireAxisImpl *a = PSPPIRE_AXIS_IMPL (axis);

  return tower_height (&a->unit_tower);
}


/* Returns the pixel at the start of UNIT */
static glong
start_pixel (const PsppireAxis *axis, gint unit)
{
  gdouble fraction;
  PsppireAxisImpl *a = PSPPIRE_AXIS_IMPL (axis);
  struct tower_node *n ;
  struct axis_node *an;

  unsigned long int start;

  if ( unit < 0)
    return -1;

  if ( unit >= unit_count (axis))
    return -1;

  n = tower_lookup (&a->unit_tower, unit, &start);

  an = tower_data (n, struct axis_node, unit_node);

  fraction = (unit - start) / (gdouble) tower_node_get_size (&an->unit_node);

  return  tower_node_get_level (&an->pixel_node) +
    nearbyint (fraction * tower_node_get_size (&an->pixel_node));
}


static gint
unit_size (const PsppireAxis *axis, gint unit)
{
  PsppireAxisImpl *a = PSPPIRE_AXIS_IMPL (axis);
  struct tower_node *n ;
  struct axis_node *an;

  unsigned long int start;

  if ( unit < 0)
    return 0;

  if ( unit >= unit_count (axis))
    return 0;

  n = tower_lookup (&a->unit_tower, unit, &start);

  an = tower_data (n, struct axis_node, unit_node);

  return nearbyint (tower_node_get_size (&an->pixel_node)
		     / (gdouble) tower_node_get_size (&an->unit_node));
}


static glong
total_size (const PsppireAxis *axis)
{
  PsppireAxisImpl *a = PSPPIRE_AXIS_IMPL (axis);

  return tower_height (&a->pixel_tower);
}


static void resize (PsppireAxis *axis, gint posn, glong size);



static void
psppire_impl_iface_init (PsppireAxisIface *iface)
{
  iface->unit_size = unit_size;
  iface->unit_count = unit_count;
  iface->start_pixel = start_pixel;
  iface->unit_at_pixel = unit_at_pixel;
  iface->total_size = total_size;
  iface->resize = resize;
}

/* --- functions --- */
/**
 * psppire_axis_impl_get_type:
 * @returns: the type ID for accelerator groups.
 */
GType
psppire_axis_impl_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info = {
	sizeof (PsppireAxisImplClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) psppire_axis_impl_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (PsppireAxisImpl),
	0,      /* n_preallocs */
	(GInstanceInitFunc) psppire_axis_impl_init,
      };

      static const GInterfaceInfo interface_info =
      {
	(GInterfaceInitFunc) psppire_impl_iface_init,
	NULL,
	NULL
      };

      object_type = g_type_register_static (G_TYPE_PSPPIRE_AXIS,
					    "PsppireAxisImpl",
					    &object_info, 0);

      g_type_add_interface_static (object_type,
				   PSPPIRE_TYPE_AXIS_IFACE,
				   &interface_info);
    }

  return object_type;
}

static void
psppire_axis_impl_class_init (PsppireAxisImplClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = psppire_axis_impl_finalize;
}


static void
psppire_axis_impl_init (PsppireAxisImpl *axis)
{
  tower_init (&axis->pixel_tower);
  tower_init (&axis->unit_tower);

  axis->pool = pool_create ();
}


static void
psppire_axis_impl_finalize (GObject *object)
{
  PsppireAxisImpl *a = PSPPIRE_AXIS_IMPL (object);
  pool_destroy (a->pool);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * psppire_axis_impl_new:
 * @returns: a new #PsppireAxisImpl object
 *
 * Creates a new #PsppireAxisImpl.
 */
PsppireAxisImpl*
psppire_axis_impl_new (void)
{
  return g_object_new (G_TYPE_PSPPIRE_AXIS_IMPL, NULL);
}




void
psppire_axis_impl_append (PsppireAxisImpl *a, gint size)
{
  psppire_axis_impl_append_n (a, 1, size);
}


void
psppire_axis_impl_append_n (PsppireAxisImpl *a, gint n_units, gint size)
{
  struct axis_node *node;

  if  (n_units == 0)
    return;

  node = pool_malloc (a->pool, sizeof *node);

  tower_insert (&a->unit_tower, n_units, &node->unit_node, NULL);
  tower_insert (&a->pixel_tower, size * n_units, &node->pixel_node, NULL);
}


/* Split the node of both towers at POSN */
static void
split (PsppireAxisImpl *a, gint posn)
{
  unsigned long int existing_unit_size;
  unsigned long int existing_pixel_size;
  unsigned long int start;
  gdouble fraction;
  struct axis_node *new_node ;
  struct tower_node *n;
  struct axis_node *existing_node;

  g_return_if_fail (posn <= tower_height (&a->unit_tower));

  /* Nothing needs to be done */
  if ( posn == 0 || posn  == tower_height (&a->unit_tower))
    return;

  n = tower_lookup (&a->unit_tower, posn, &start);

  existing_node = tower_data (n, struct axis_node, unit_node);

  /* Nothing needs to be done, if the range element is already split here */
  if ( posn - start == 0)
    return;

  existing_unit_size = tower_node_get_size (&existing_node->unit_node);
  existing_pixel_size = tower_node_get_size (&existing_node->pixel_node);

  fraction = (posn - start) / (gdouble) existing_unit_size;

  new_node = pool_malloc (a->pool, sizeof (*new_node));

  tower_resize (&a->unit_tower, &existing_node->unit_node, posn - start);

  tower_resize (&a->pixel_tower, &existing_node->pixel_node,
		nearbyintf (fraction * existing_pixel_size));

  tower_insert (&a->unit_tower,
		existing_unit_size - (posn - start),
		&new_node->unit_node,
		tower_next (&a->unit_tower, &existing_node->unit_node));


  tower_insert (&a->pixel_tower,
		nearbyintf (existing_pixel_size * (1 - fraction)),
		&new_node->pixel_node,
		tower_next (&a->pixel_tower, &existing_node->pixel_node));
}


/* Insert a new unit of size SIZE before POSN */
void
psppire_axis_impl_insert (PsppireAxisImpl *a, gint posn, gint size)
{
  struct axis_node *before = NULL;
  struct axis_node *new_node;

  g_return_if_fail ( posn >= 0);
  g_return_if_fail ( posn <= tower_height (&a->unit_tower));

  if ( posn < tower_height (&a->unit_tower))
    {
      unsigned long int start = 0;
      struct tower_node *n;

      split (a, posn);

      n = tower_lookup (&a->unit_tower, posn, &start);
      g_assert (posn == start);

      before = tower_data (n, struct axis_node, unit_node);
    }

  new_node = pool_malloc (a->pool, sizeof (*new_node));

  tower_insert (&a->unit_tower,
		1,
		&new_node->unit_node,
		before ? &before->unit_node : NULL);

  tower_insert (&a->pixel_tower,
		size,
		&new_node->pixel_node,
		before ? &before->pixel_node : NULL);
}


/* Make the element at POSN singular.
   Return a pointer to the node for this element */
static struct axis_node *
make_single (PsppireAxisImpl *a, gint posn)
{
  unsigned long int start;
  struct tower_node *n;

  g_return_val_if_fail (posn < tower_height (&a->unit_tower), NULL);

  n = tower_lookup (&a->unit_tower, posn, &start);

  if ( 1 != tower_node_get_size (n))
    {
      split (a, posn + 1);
      n = tower_lookup (&a->unit_tower, posn, &start);

      if ( 1 != tower_node_get_size (n))
	{
	  split (a, posn);
	  n = tower_lookup (&a->unit_tower, posn, &start);
	}
    }

  g_assert (1 == tower_node_get_size (n));

  return tower_data (n, struct axis_node, unit_node);
}


static void
resize (PsppireAxis *axis, gint posn, glong size)
{
  PsppireAxisImpl *a = PSPPIRE_AXIS_IMPL (axis);

  struct axis_node *an;
  g_return_if_fail (posn >= 0);
  g_return_if_fail (size > 0);

  /* Silently ignore this request if the position is greater than the number of
     units in the axis */
  if (posn >= tower_height (&a->unit_tower))
    return ;

  an = make_single (a, posn);

  tower_resize (&a->pixel_tower, &an->pixel_node, size);
}


void
psppire_axis_impl_resize (PsppireAxisImpl *a, gint posn, gint size)
{
  resize (PSPPIRE_AXIS (a), posn, size);
}




void
psppire_axis_impl_clear (PsppireAxisImpl *a)
{
  pool_destroy (a->pool);
  a->pool = pool_create ();

  tower_init (&a->pixel_tower);
  tower_init (&a->unit_tower);
}



void
psppire_axis_impl_delete (PsppireAxisImpl *a, gint first, gint n_units)
{
  gint units_to_delete = n_units;
  unsigned long int start;
  struct tower_node *unit_node ;
  g_return_if_fail (first + n_units <= tower_height (&a->unit_tower));

  split (a, first);
  split (a, first + n_units);

  unit_node = tower_lookup (&a->unit_tower, first, &start);
  g_assert (start == first);

  while (units_to_delete > 0)
    {
      struct tower_node *next_unit_node;
      struct axis_node *an = tower_data (unit_node,
					 struct axis_node, unit_node);

      g_assert (unit_node == &an->unit_node);
      g_assert (unit_node->size <= n_units);

      units_to_delete -= unit_node->size;

      next_unit_node = tower_next (&a->unit_tower, unit_node);

      tower_delete (&a->unit_tower, unit_node);
      tower_delete (&a->pixel_tower, &an->pixel_node);

      pool_free (a->pool, an);

      unit_node = next_unit_node;
    }
}
