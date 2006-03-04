/* gsheet-uniform-column.c
 * 
 * PSPPIRE --- A Graphical User Interface for PSPP
 * Copyright (C) 2006  Free Software Foundation
 * Written by John Darrington
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "gsheet-column-iface.h"
#include "gsheet-uniform-column.h"


static void  g_sheet_uniform_column_init       (GSheetUniformColumn      *ug);
static void  g_sheet_uniform_column_class_init (GSheetUniformColumnClass *class);
static void  g_sheet_uniform_column_finalize   (GObject           *object);

static void g_sheet_column_init (GSheetColumnIface *iface);


static GObjectClass *parent_class = NULL;

GType
g_sheet_uniform_column_get_type (void)
{
  static GType uniform_column_type = 0;

  if (!uniform_column_type)
    {
      static const GTypeInfo uniform_column_info =
      {
	sizeof (GSheetUniformColumnClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
        (GClassInitFunc) g_sheet_uniform_column_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
        sizeof (GSheetUniformColumn),
	0,
        (GInstanceInitFunc) g_sheet_uniform_column_init,
      };

      static const GInterfaceInfo column_info =
      {
	(GInterfaceInitFunc) g_sheet_column_init,
	NULL,
	NULL
      };

      uniform_column_type = 
	g_type_register_static (G_TYPE_OBJECT, "g_sheet_uniform_column",
				&uniform_column_info, 0);

      g_type_add_interface_static (uniform_column_type,
				   G_TYPE_SHEET_COLUMN,
				   &column_info);
    }

  return uniform_column_type;
}


/**
 * g_sheet_uniform_column_new:
 * @width: The size of columns in this uniform column
 *
 * Return value: a new #g_sheet_uniform_column
 **/
GObject *
g_sheet_uniform_column_new (gint width, gint n_columns)
{
  GSheetUniformColumn *ug;
  GObject *retval;

  retval = g_object_new (G_TYPE_SHEET_UNIFORM_COLUMN, NULL);

  ug = G_SHEET_UNIFORM_COLUMN(retval);
  ug->n_columns = n_columns;
  ug->width = width;
  ug->is_visible = TRUE;
  ug->is_sensitive = FALSE;

  return retval;
}

static gint 
g_sheet_uniform_column_get_width(const GSheetColumn *geom, gint u)
{
  GSheetUniformColumn *ug = G_SHEET_UNIFORM_COLUMN(geom);
  
  return ug->width;
}

static gint 
g_sheet_uniform_column_get_sensitivity(const GSheetColumn *geom, gint u)
{
  GSheetUniformColumn *ug = G_SHEET_UNIFORM_COLUMN(geom);
  
  return ug->is_sensitive;
}


static gint 
g_sheet_uniform_column_get_visibility(const GSheetColumn *geom, gint u)
{
  GSheetUniformColumn *ug = G_SHEET_UNIFORM_COLUMN(geom);
  
  return ug->is_visible;
}


static const gchar *
g_sheet_uniform_column_get_button_label(const GSheetColumn *geom, gint u)
{
  GSheetUniformColumn *ug = G_SHEET_UNIFORM_COLUMN(geom);
  
  static gchar *label; 

  g_free(label);
  label = g_strdup_printf("%d", u);

  return label;
}


static GtkJustification
g_sheet_uniform_column_get_justification(const GSheetColumn *geom, gint u)
{
	/* 
  GSheetUniformColumn *ug = G_SHEET_UNIFORM_COLUMN(geom);
  */
  
  return GTK_JUSTIFY_FILL;
}



static gint 
g_sheet_uniform_column_get_column_count(const GSheetColumn *geom)
{
  GSheetUniformColumn *ug = G_SHEET_UNIFORM_COLUMN(geom);

  return ug->n_columns;
}

static void
g_sheet_uniform_column_class_init (GSheetUniformColumnClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = g_sheet_uniform_column_finalize;

}


static void
g_sheet_uniform_column_init (GSheetUniformColumn *o)
{
}

static void         
g_sheet_uniform_column_finalize (GObject           *object)
{
}


static void
g_sheet_column_init (GSheetColumnIface *iface)
{
  iface->get_width = g_sheet_uniform_column_get_width ;
  iface->get_sensitivity = g_sheet_uniform_column_get_sensitivity ;
  iface->get_visibility = g_sheet_uniform_column_get_visibility ;
  iface->get_justification = g_sheet_uniform_column_get_justification;
  iface->get_column_count = g_sheet_uniform_column_get_column_count;
  iface->get_button_label = g_sheet_uniform_column_get_button_label;
}

