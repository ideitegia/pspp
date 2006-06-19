/* gsheet-hetero-column.c
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
#include "gsheet-hetero-column.h"
#include <string.h>


static void  g_sheet_hetero_column_init       (GSheetHeteroColumn      *hg);
static void  g_sheet_hetero_column_class_init (GSheetHeteroColumnClass *class);
static void  g_sheet_hetero_column_finalize   (GObject           *object);

static void g_sheet_column_init (GSheetColumnIface *iface);


static GObjectClass *parent_class = NULL;

GType
g_sheet_hetero_column_get_type (void)
{
  static GType hetero_column_type = 0;

  if (!hetero_column_type)
    {
      static const GTypeInfo hetero_column_info =
      {
	sizeof (GSheetHeteroColumnClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
        (GClassInitFunc) g_sheet_hetero_column_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
        sizeof (GSheetHeteroColumn),
	0,
        (GInstanceInitFunc) g_sheet_hetero_column_init,
      };

      static const GInterfaceInfo column_info =
      {
	(GInterfaceInitFunc) g_sheet_column_init,
	NULL,
	NULL
      };

      hetero_column_type = 
	g_type_register_static (G_TYPE_OBJECT, "g_sheet_hetero_column",
				&hetero_column_info, 0);

      g_type_add_interface_static (hetero_column_type,
				   G_TYPE_SHEET_COLUMN,
				   &column_info);
    }

  return hetero_column_type;
}


static GtkSheetButton default_button;
   


/**
 * g_sheet_hetero_column_new:
 * @width: The size of columns in this hetero column
 *
 * Return value: a new #g_sheet_hetero_column
 **/
GObject *
g_sheet_hetero_column_new (gint default_width, gint n_columns)
{
  gint i;
  GSheetHeteroColumn *hg;
  GObject *retval;

  retval = g_object_new (G_TYPE_SHEET_HETERO_COLUMN, NULL);

  hg = G_SHEET_HETERO_COLUMN(retval);
  hg->n_columns = n_columns;
  hg->default_width = default_width;
  hg->col = g_new0(struct GSheetHeteroColumnUnit, n_columns);

  for (i = 0 ; i < hg->n_columns; ++i ) 
    {
      hg->col[i].button = default_button;
    }

  return retval;
}

static gint 
g_sheet_hetero_column_get_width(const GSheetColumn *geom, gint i, gpointer data)
{
  GSheetHeteroColumn *hg = G_SHEET_HETERO_COLUMN(geom);

  g_return_val_if_fail(i < hg->n_columns, -1);
  
  return hg->col[i].width;
}

static gint 
g_sheet_hetero_column_get_sensitivity(const GSheetColumn *geom, gint u, gpointer data)
{
  return TRUE;
}


static gint 
g_sheet_hetero_column_get_visibility(const GSheetColumn *geom, gint u, gpointer data)
{
  return TRUE;
}



static gchar *
g_sheet_hetero_column_get_button_label(const GSheetColumn *geom, gint u, gpointer data)
{
  GSheetHeteroColumn *hg = G_SHEET_HETERO_COLUMN(geom);

  return g_locale_to_utf8(hg->col[u].button.label, -1, 0, 0, 0);
}


static GtkJustification
g_sheet_hetero_column_get_justification(const GSheetColumn *geom, gint u, gpointer data)
{
  return GTK_JUSTIFY_FILL;
}



static gint 
g_sheet_hetero_column_get_column_count(const GSheetColumn *geom, gpointer data)
{
  GSheetHeteroColumn *hg = G_SHEET_HETERO_COLUMN(geom);

  return hg->n_columns;
}

static void
g_sheet_hetero_column_class_init (GSheetHeteroColumnClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = g_sheet_hetero_column_finalize;

  default_button.label=NULL;
  default_button.child=NULL;
  default_button.state=GTK_STATE_NORMAL;
  default_button.justification=GTK_JUSTIFY_CENTER;
  default_button.label_visible = TRUE;
}


static void
g_sheet_hetero_column_init (GSheetHeteroColumn *o)
{
}

static void         
g_sheet_hetero_column_finalize (GObject           *object)
{
  GSheetHeteroColumn *hg = G_SHEET_HETERO_COLUMN(object);

  g_free(hg->col);
}

static void 
hetero_column_set_width(GSheetColumn *geo,
				      gint i, gint size, gpointer data)
{
  GSheetHeteroColumn *hg = G_SHEET_HETERO_COLUMN(geo);

  g_return_if_fail(i < hg->n_columns);

  hg->col[i].width = size;
}



static void
g_sheet_column_init (GSheetColumnIface *iface)
{
  iface->get_width = g_sheet_hetero_column_get_width ;
  iface->set_width = hetero_column_set_width ;
  iface->get_sensitivity = g_sheet_hetero_column_get_sensitivity ;
  iface->get_visibility = g_sheet_hetero_column_get_visibility ;
  iface->get_justification = g_sheet_hetero_column_get_justification;
  iface->get_column_count = g_sheet_hetero_column_get_column_count;

  iface->get_button_label = g_sheet_hetero_column_get_button_label;
}


void 
g_sheet_hetero_column_set_button_label(GSheetHeteroColumn *geo,
					      gint i, const gchar *label)
{
  g_return_if_fail(i < geo->n_columns);

  g_free(geo->col[i].button.label);
  geo->col[i].button.label = g_malloc(strlen(label) + 1);
  
  g_stpcpy(geo->col[i].button.label, label);
}




inline void 
g_sheet_hetero_column_set_width(GSheetHeteroColumn *geo,
					     gint i, gint size)
{
  GSheetColumn *iface = G_SHEET_COLUMN(geo);

  hetero_column_set_width(iface, i, size, 0);
}



