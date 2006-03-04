/* gsheet-uniform-row.c
 * 
 *  PSPPIRE --- A Graphical User Interface for PSPP
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

#include "gsheet-row-iface.h"
#include "gsheet-uniform-row.h"


static void  g_sheet_uniform_row_init       (GSheetUniformRow      *ug);
static void  g_sheet_uniform_row_class_init (GSheetUniformRowClass *class);
static void  g_sheet_uniform_row_finalize   (GObject           *object);

static void g_sheet_row_init (GSheetRowIface *iface);


static GObjectClass *parent_class = NULL;

GType
g_sheet_uniform_row_get_type (void)
{
  static GType uniform_row_type = 0;

  if (!uniform_row_type)
    {
      static const GTypeInfo uniform_row_info =
      {
	sizeof (GSheetUniformRowClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
        (GClassInitFunc) g_sheet_uniform_row_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
        sizeof (GSheetUniformRow),
	0,
        (GInstanceInitFunc) g_sheet_uniform_row_init,
      };

      static const GInterfaceInfo row_info =
      {
	(GInterfaceInitFunc) g_sheet_row_init,
	NULL,
	NULL
      };

      uniform_row_type = 
	g_type_register_static (G_TYPE_OBJECT, "g_sheet_uniform_row",
				&uniform_row_info, 0);

      g_type_add_interface_static (uniform_row_type,
				   G_TYPE_SHEET_ROW,
				   &row_info);
    }

  return uniform_row_type;
}


/**
 * g_sheet_uniform_row_new:
 * @height: The size of rows in this uniform row
 *
 * Return value: a new #g_sheet_uniform_row
 **/
GObject *
g_sheet_uniform_row_new (gint height, gint n_rows)
{
  GSheetUniformRow *ug;
  GObject *retval;

  retval = g_object_new (G_TYPE_SHEET_UNIFORM_ROW, NULL);

  ug = G_SHEET_UNIFORM_ROW(retval);
  ug->n_rows = n_rows;
  ug->height = height;
  ug->is_visible = TRUE;

  return retval;
}

static gint 
g_sheet_uniform_row_get_height(const GSheetRow *geom, gint u)
{
  GSheetUniformRow *ug = G_SHEET_UNIFORM_ROW(geom);
  
  return ug->height;
}

static gboolean
g_sheet_uniform_row_get_sensitivity(const GSheetRow *geom, gint u)
{
  GSheetUniformRow *ug = G_SHEET_UNIFORM_ROW(geom);
  
  return (u < ug->n_rows);
}


static gboolean
g_sheet_uniform_row_get_visibility(const GSheetRow *geom, gint u)
{
  GSheetUniformRow *ug = G_SHEET_UNIFORM_ROW(geom);
  
  return ug->is_visible;
}


static const gchar *
g_sheet_uniform_row_get_button_label(const GSheetRow *geom, gint u)
{
  GSheetUniformRow *ug = G_SHEET_UNIFORM_ROW(geom);
  
  static gchar *label; 
  g_free(label);
  label = g_strdup_printf("%d", u);

  return label;
}



static gint 
g_sheet_uniform_row_get_row_count(const GSheetRow *geom)
{
  GSheetUniformRow *ug = G_SHEET_UNIFORM_ROW(geom);

  return ug->n_rows;
}


static void
g_sheet_uniform_row_class_init (GSheetUniformRowClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = g_sheet_uniform_row_finalize;

}


static void
g_sheet_uniform_row_init (GSheetUniformRow *o)
{
}

static void         
g_sheet_uniform_row_finalize (GObject           *object)
{
}


static guint
g_sheet_uniform_row_top_ypixel(GSheetRow *geo, gint row, const GtkSheet *sheet)
{
  GSheetUniformRow *ug = G_SHEET_UNIFORM_ROW(geo);

  return row * ug->height;
}

static guint
g_sheet_uniform_row_pixel_to_row(GSheetRow *geo, 
				 gint pixel, const GtkSheet *sheet)
{
  GSheetUniformRow *ug = G_SHEET_UNIFORM_ROW(geo);

  gint row = pixel / ug->height;

  if (row >= g_sheet_uniform_row_get_row_count(geo))
    row = g_sheet_uniform_row_get_row_count(geo) -1;

  return row;
}



static void
g_sheet_row_init (GSheetRowIface *iface)
{
  iface->get_height = g_sheet_uniform_row_get_height;
  iface->get_sensitivity = g_sheet_uniform_row_get_sensitivity ;
  iface->get_visibility = g_sheet_uniform_row_get_visibility;
  iface->get_row_count = g_sheet_uniform_row_get_row_count;
  iface->get_button_label = g_sheet_uniform_row_get_button_label;
  iface->top_ypixel = g_sheet_uniform_row_top_ypixel;
  iface->pixel_to_row = g_sheet_uniform_row_pixel_to_row;
}

