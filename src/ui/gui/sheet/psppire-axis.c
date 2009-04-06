/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009  Free Software Foundation

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

#include <ui/gui/psppire-marshal.h>
#include <libpspp/tower.h>
#include <libpspp/pool.h>
#include "psppire-axis.h"
#include <math.h>
#include <libpspp/misc.h>


/* Signals */
enum
  {
    RESIZE_UNIT,
    n_signals
  };

static guint signals[n_signals] ;

/* --- prototypes --- */
static void psppire_axis_class_init (PsppireAxisClass	*class);
static void psppire_axis_init	(PsppireAxis		*axis);
static void psppire_axis_finalize   (GObject		*object);


/* --- variables --- */
static GObjectClass     *parent_class = NULL;


struct axis_node
{
  struct tower_node pixel_node;
  struct tower_node unit_node;
};

void
psppire_axis_dump (const PsppireAxis *a)
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

/* Return the unit covered by PIXEL */
gint
psppire_axis_unit_at_pixel (const PsppireAxis *a, glong pixel)
{
  unsigned long int start;
  struct tower_node *n;
  struct axis_node *an;
  gdouble fraction;

  glong size = tower_height (&a->pixel_tower);

  g_return_val_if_fail (pixel >= 0, -1);

  if (pixel >= size)
    {
      gint n_items = tower_height (&a->unit_tower);
      glong extra = pixel - size;

      return n_items - 1 + DIV_RND_UP (extra,  a->default_size);
    }


  n = tower_lookup (&a->pixel_tower, pixel, &start);
  an = tower_data (n, struct axis_node, pixel_node);

  fraction = (pixel - start) / (gdouble) tower_node_get_size (&an->pixel_node);

  return  tower_node_get_level (&an->unit_node)
    + fraction * tower_node_get_size (&an->unit_node);
}


gint
psppire_axis_unit_count (const PsppireAxis *a)
{
  glong padding = 0;
  glong actual_size;

  actual_size = tower_height (&a->pixel_tower);

  if ( actual_size < a->min_extent )
    padding = DIV_RND_UP (a->min_extent - actual_size, a->default_size);

  return tower_height (&a->unit_tower) + padding;
}


/* Return the starting pixel of UNIT */
glong
psppire_axis_start_pixel (const PsppireAxis *a, gint unit)
{
  gdouble fraction;
  struct tower_node *n ;
  struct axis_node *an;

  unsigned long int start;

  gint the_count, size ;

  the_count =  tower_height (&a->unit_tower);
  size = tower_height (&a->pixel_tower);

  if ( unit >= the_count)
    {
      return  size + (unit - the_count) * a->default_size;
    }

  if ( unit < 0)
    return -1;

  if ( unit >= tower_height (&a->unit_tower))
    return -1;

  n = tower_lookup (&a->unit_tower, unit, &start);

  an = tower_data (n, struct axis_node, unit_node);

  fraction = (unit - start) / (gdouble) tower_node_get_size (&an->unit_node);

  return  tower_node_get_level (&an->pixel_node) +
    nearbyint (fraction * tower_node_get_size (&an->pixel_node));
}

gint
psppire_axis_unit_size (const PsppireAxis *axis, gint unit)
{
  struct tower_node *n ;
  struct axis_node *an;

  unsigned long int start;

  if  (unit >= tower_height (&axis->unit_tower))
    return axis->default_size;

  if ( unit < 0)
    return 0;

  if ( unit >= tower_height (&axis->unit_tower))
    return 0;

  n = tower_lookup (&axis->unit_tower, unit, &start);

  an = tower_data (n, struct axis_node, unit_node);

  return nearbyint (tower_node_get_size (&an->pixel_node)
		     / (gdouble) tower_node_get_size (&an->unit_node));
}



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

  parent_class = g_type_class_peek_parent (class);

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


  signals[RESIZE_UNIT] =
    g_signal_new ("resize-unit",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  0,
		  NULL, NULL,
		  psppire_marshal_VOID__INT_LONG,
		  G_TYPE_NONE,
		  2,
		  G_TYPE_INT,
		  G_TYPE_LONG
		  );


  object_class->finalize = psppire_axis_finalize;
}


static void
psppire_axis_init (PsppireAxis *axis)
{
  tower_init (&axis->pixel_tower);
  tower_init (&axis->unit_tower);

  axis->pool = pool_create ();
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




void
psppire_axis_append (PsppireAxis *a, gint size)
{
  psppire_axis_append_n (a, 1, size);
}


void
psppire_axis_append_n (PsppireAxis *a, gint n_units, gint size)
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
split (PsppireAxis *a, gint posn)
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
psppire_axis_insert (PsppireAxis *a, gint posn, gint size)
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
make_single (PsppireAxis *a, gint posn)
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


void
psppire_axis_resize (PsppireAxis *axis, gint posn, glong size)
{
  struct axis_node *an;
  g_return_if_fail (posn >= 0);
  g_return_if_fail (size > 0);

  /* Silently ignore this request if the position is greater than the number of
     units in the axis */
  if (posn >= tower_height (&axis->unit_tower))
    return ;

  an = make_single (axis, posn);

  tower_resize (&axis->pixel_tower, &an->pixel_node, size);

  g_signal_emit (axis, signals[RESIZE_UNIT], 0, posn, size);
}






void
psppire_axis_clear (PsppireAxis *a)
{
  pool_destroy (a->pool);
  a->pool = pool_create ();

  tower_init (&a->pixel_tower);
  tower_init (&a->unit_tower);
}



void
psppire_axis_delete (PsppireAxis *a, gint first, gint n_units)
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
