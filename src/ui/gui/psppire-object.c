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


#include <string.h>
#include <stdlib.h>

#include "psppire-object.h"


/* --- prototypes --- */
static void psppire_object_class_init	(PsppireObjectClass	*class);
static void psppire_object_init	(PsppireObject		*accel_group);
static void psppire_object_finalize	(GObject		*object);


/* --- variables --- */
static GObjectClass     *parent_class = NULL;


/* --- functions --- */
/**
 * psppire_object_get_type:
 * @returns: the type ID for accelerator groups.
 */
GType
psppire_object_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info = {
	sizeof (PsppireObjectClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) psppire_object_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (PsppireObject),
	0,      /* n_preallocs */
	(GInstanceInitFunc) psppire_object_init,
      };

      object_type = g_type_register_static (G_TYPE_OBJECT, "PsppireObject",
					    &object_info, G_TYPE_FLAG_ABSTRACT);
    }

  return object_type;
}

static guint signal_changed = 0 ; 

static void
psppire_object_class_init (PsppireObjectClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = psppire_object_finalize;

   signal_changed =
     g_signal_new ("changed",
		   G_OBJECT_CLASS_TYPE (class),
		   G_SIGNAL_RUN_FIRST,
		   0,
		   NULL, NULL,
		   g_cclosure_marshal_VOID__VOID,
		   G_TYPE_NONE, 0);

}

static void
psppire_object_finalize (GObject *object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
psppire_object_init (PsppireObject *psppire_object)
{

}

/**
 * psppire_object_new:
 * @returns: a new #PsppireObject object
 * 
 * Creates a new #PsppireObject. 
 */
PsppireObject*
psppire_object_new (void)
{
  return g_object_new (G_TYPE_PSPPIRE_OBJECT, NULL);
}
