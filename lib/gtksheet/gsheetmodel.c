/* GSheetModel --- an abstract model for the GSheet widget.
 * Copyright (C) 2006 Free Software Foundation
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

#include <config.h>

#include <glib.h>
#include "gsheetmodel.h"
#include "gtkextra-marshal.h"

enum {
  RANGE_CHANGED,
  ROWS_INSERTED,
  ROWS_DELETED,
  COLUMNS_INSERTED,
  COLUMNS_DELETED,
  LAST_SIGNAL
};

static guint sheet_model_signals[LAST_SIGNAL] = { 0 };


static void      g_sheet_model_base_init   (gpointer           g_class);


GType
g_sheet_model_get_type (void)
{
  static GType sheet_model_type = 0;

  if (! sheet_model_type)
    {
      static const GTypeInfo sheet_model_info =
      {
        sizeof (GSheetModelIface), /* class_size */
	g_sheet_model_base_init,   /* base_init */
	NULL,		/* base_finalize */
	NULL,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      sheet_model_type =
	g_type_register_static (G_TYPE_INTERFACE, "GSheetModel",
				&sheet_model_info, 0);

      g_type_interface_add_prerequisite (sheet_model_type, G_TYPE_OBJECT);
    }

  return sheet_model_type;
}

static void
g_sheet_model_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;

  if (! initialized)
    {
      sheet_model_signals[RANGE_CHANGED] =
	g_signal_new ("range_changed",
		      G_TYPE_SHEET_MODEL,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (GSheetModelIface, range_changed),
		      NULL, NULL,
		      gtkextra_VOID__INT_INT_INT_INT,
		      G_TYPE_NONE, 4,
		      G_TYPE_INT,
		      G_TYPE_INT,
		      G_TYPE_INT,
		      G_TYPE_INT);



      sheet_model_signals[ROWS_INSERTED] =
	g_signal_new ("rows_inserted",
		      G_TYPE_SHEET_MODEL,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (GSheetModelIface, rows_inserted),
		      NULL, NULL,
		      gtkextra_VOID__INT_INT,
		      G_TYPE_NONE, 2,
		      G_TYPE_INT,
		      G_TYPE_INT);


      sheet_model_signals[ROWS_DELETED] =
	g_signal_new ("rows_deleted",
		      G_TYPE_SHEET_MODEL,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (GSheetModelIface, rows_deleted),
		      NULL, NULL,
		      gtkextra_VOID__INT_INT,
		      G_TYPE_NONE, 2,
		      G_TYPE_INT,
		      G_TYPE_INT);

      sheet_model_signals[COLUMNS_INSERTED] =
	g_signal_new ("columns_inserted",
		      G_TYPE_SHEET_MODEL,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (GSheetModelIface, columns_inserted),
		      NULL, NULL,
		      gtkextra_VOID__INT_INT,
		      G_TYPE_NONE, 2,
		      G_TYPE_INT,
		      G_TYPE_INT);


      sheet_model_signals[COLUMNS_DELETED] =
	g_signal_new ("columns_deleted",
		      G_TYPE_SHEET_MODEL,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (GSheetModelIface, columns_deleted),
		      NULL, NULL,
		      gtkextra_VOID__INT_INT,
		      G_TYPE_NONE, 2,
		      G_TYPE_INT,
		      G_TYPE_INT);


      initialized = TRUE;
    }
}


/**
 * g_sheet_model_free_strings
 * @sheet_model: A #GSheetModel
 *
 * Returns: True if strings obtained with get_string should be freed by the
 * sheet when no longer required.
 **/
gboolean
g_sheet_model_free_strings (const GSheetModel *sheet_model)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (sheet_model), FALSE);

  return G_SHEET_MODEL_GET_IFACE (sheet_model)->free_strings;
}


/**
 * g_sheet_model_get_string:
 * @sheet_model: A #GSheetModel
 * @row: The row of the cell to be retrieved.
 * @column: The column of the cell to be retrieved.
 *
 * Retrieves the datum at location ROW, COLUMN in the form of a string.
 * Returns: The string representation of the datum, or NULL on error.
 **/
gchar *
g_sheet_model_get_string (const GSheetModel *sheet_model,
			  glong row, glong column)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (sheet_model), 0);

  g_assert (G_SHEET_MODEL_GET_IFACE (sheet_model)->get_string);

  return (G_SHEET_MODEL_GET_IFACE (sheet_model)->get_string) (sheet_model, row, column);
}

/**
 * g_sheet_model_set_string
 * @sheet_model: A #GSheetModel
 * @text: The text describing the datum to be set.
 * @row: The row of the cell to be cleared.
 * @column: The column of the cell to be cleared.
 *
 * Sets the datum at a location from a string.
 * Returns: TRUE if the datum was changed, FALSE otherwise.
 **/
gboolean
g_sheet_model_set_string      (GSheetModel *sheet_model,
				 const gchar *text,
				 glong row, glong column)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (sheet_model), FALSE);

  g_assert (G_SHEET_MODEL_GET_IFACE (sheet_model)->set_string);

  return G_SHEET_MODEL_GET_IFACE (sheet_model)->set_string (sheet_model,
							    text, row, column);
}



/**
 * g_sheet_model_datum_clear:
 * @sheet_model: A #GSheetModel
 * @row: The row of the cell to be cleared.
 * @column: The column of the cell to be cleared.
 *
 * Called when the datum at a location is to be cleared.
 * Returns: TRUE if the datum was cleared, FALSE otherwise.
 **/
gboolean
g_sheet_model_datum_clear    (GSheetModel *sheet_model,
				glong row, glong column)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (sheet_model), FALSE);

  g_assert (G_SHEET_MODEL_GET_IFACE (sheet_model)->clear_datum);

  return G_SHEET_MODEL_GET_IFACE (sheet_model)->clear_datum (sheet_model,
								row, column);
}


/**
 * g_sheet_model_range_changed:
 * @sheet_model: A #GSheetModel
 * @range: The #GSheetRange range of cells which have changed.
 *
 * Emits the "range_changed" signal on @sheet_model.
 **/
void
g_sheet_model_range_changed (GSheetModel *sheet_model,
			       glong row0, glong col0,
			       glong rowi, glong coli)
{
  g_return_if_fail (G_IS_SHEET_MODEL (sheet_model));

  g_signal_emit (sheet_model, sheet_model_signals[RANGE_CHANGED], 0,
		 row0, col0, rowi, coli);
}




/**
 * g_sheet_model_rows_inserted:
 * @sheet_model: A #GSheetModel
 * @row: The row before which the new rows should be inserted.
 * @n_rows: The number of rows to insert.
 *
 * Emits the "rows_inserted" signal on @sheet_model.
 **/
void
g_sheet_model_rows_inserted (GSheetModel *sheet_model,
			       glong row, glong n_rows)
{
  g_return_if_fail (G_IS_SHEET_MODEL (sheet_model));

  g_signal_emit (sheet_model, sheet_model_signals[ROWS_INSERTED], 0,
		 row, n_rows);
}


/**
 * g_sheet_model_columns_inserted:
 * @sheet_model: A #GSheetModel
 * @column: The column before which the new columns should be inserted.
 * @n_columns: The number of columns to insert.
 *
 * Emits the "columns_inserted" signal on @sheet_model.
 **/
void
g_sheet_model_columns_inserted (GSheetModel *sheet_model,
			       glong column, glong n_columns)
{
  g_return_if_fail (G_IS_SHEET_MODEL (sheet_model));

  g_signal_emit (sheet_model, sheet_model_signals[COLUMNS_INSERTED], 0,
		 column, n_columns);
}




/**
 * g_sheet_model_rows_deleted:
 * @sheet_model: A #GSheetModel
 * @row: The first row to be deleted.
 * @n_rows: The number of rows to delete.
 *
 * Emits the "rows_deleted" signal on @sheet_model.
 **/
void
g_sheet_model_rows_deleted (GSheetModel *sheet_model,
			       glong row, glong n_rows)
{
  g_return_if_fail (G_IS_SHEET_MODEL (sheet_model));

  g_signal_emit (sheet_model, sheet_model_signals[ROWS_DELETED], 0,
		 row, n_rows);
}



/**
 * g_sheet_model_columns_deleted:
 * @sheet_model: A #GSheetModel
 * @column: The first column to be deleted.
 * @n_columns: The number of columns to delete.
 *
 * Emits the "columns_deleted" signal on @sheet_model.
 **/
void
g_sheet_model_columns_deleted (GSheetModel *sheet_model,
			       glong column, glong n_columns)
{
  g_return_if_fail (G_IS_SHEET_MODEL (sheet_model));

  g_signal_emit (sheet_model, sheet_model_signals[COLUMNS_DELETED], 0,
		 column, n_columns);
}





/**
 * g_sheet_model_is_editable:
 * @sheet_model: A #GSheetModel
 * @row: The row
 * @column: The column
 *
 * Returns: TRUE if the cell is editable, FALSE otherwise
 **/
gboolean
g_sheet_model_is_editable (const GSheetModel *model,
			     glong row, glong column)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (model), TRUE);

  if ( ! G_SHEET_MODEL_GET_IFACE (model)->is_editable )
    return TRUE;

  return G_SHEET_MODEL_GET_IFACE (model)->is_editable (model,
							  row, column);
}

/**
 * g_sheet_model_is_visible:
 * @sheet_model: A #GSheetModel
 * @row: The row
 * @column: The column
 *
 * Returns: TRUE if the cell is visible, FALSE otherwise
 **/
gboolean
g_sheet_model_is_visible (const GSheetModel *model,
			  glong row, glong column)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (model), TRUE);

  if ( ! G_SHEET_MODEL_GET_IFACE (model)->is_visible )
    return TRUE;

  return G_SHEET_MODEL_GET_IFACE (model)->is_visible (model,
							row, column);
}


/**
 * g_sheet_model_get_foreground:
 * @sheet_model: A #GSheetModel
 * @row: The row
 * @column: The column
 *
 * Returns the foreground colour of the cell at @row, @column
 * The color is unallocated.  It will be allocated by the viewing object.
 **/
GdkColor *
g_sheet_model_get_foreground (const GSheetModel *model,
				glong row, glong column)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (model), NULL);

  if ( ! G_SHEET_MODEL_GET_IFACE (model)->get_foreground )
    return NULL;

  return G_SHEET_MODEL_GET_IFACE (model)->get_foreground (model,
							    row, column);
}

/**
 * g_sheet_model_get_background:
 * @sheet_model: A #GSheetModel
 * @row: The row
 * @column: The column
 *
 * Returns the background colour of the cell at @row, @column
 * The color is unallocated.  It will be allocated by the viewing object.
 **/
GdkColor *
g_sheet_model_get_background (const GSheetModel *model,
				glong row, glong column)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (model), NULL);

  if ( ! G_SHEET_MODEL_GET_IFACE (model)->get_background )
    return NULL;

  return G_SHEET_MODEL_GET_IFACE (model)->get_background (model,
							    row, column);
}

/**
 * g_sheet_model_get_justification:
 * @sheet_model: A #GSheetModel
 * @row: The row
 * @column: The column
 *
 * Returns the justification of the cell at @row, @column
 * Returns: the justification, or NULL on error.
 **/
const GtkJustification *
g_sheet_model_get_justification (const GSheetModel *model,
				   glong row, glong column)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (model), NULL);

  if ( ! G_SHEET_MODEL_GET_IFACE (model)->get_justification)
    return NULL;

  return G_SHEET_MODEL_GET_IFACE (model)->get_justification (model,
							       row, column);
}

/**
 * g_sheet_model_get_font_desc:
 * @sheet_model: A #GSheetModel
 * @row: The row
 * @column: The column
 *
 * Returns the font description of the cell at @row, @column
 * Returns: the font description, or NULL on error.
 **/
const PangoFontDescription *
g_sheet_model_get_font_desc(const GSheetModel *model,
			      glong row, glong column)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (model), NULL);
  if ( ! G_SHEET_MODEL_GET_IFACE (model)->get_font_desc)
    return NULL;

  return G_SHEET_MODEL_GET_IFACE (model)->get_font_desc (model,
							   row, column);
}

/**
 * g_sheet_model_get_cell_border:
 * @sheet_model: A #GSheetModel
 * @row: The row
 * @column: The column
 *
 * Returns the cell border of the cell at @row, @column
 * Returns: the cell border, or NULL on error.
 **/
const GtkSheetCellBorder *
g_sheet_model_get_cell_border (const GSheetModel *model,
				 glong row, glong column)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (model), NULL);
  if ( ! G_SHEET_MODEL_GET_IFACE (model)->get_cell_border)
    return NULL;

  return G_SHEET_MODEL_GET_IFACE (model)->get_cell_border (model,
							   row, column);
}



/**
 * g_sheet_model_get_column_count:
 * @model: A #GSheetModel
 *
 * Returns the total number of columns represented by the model
 **/
glong
g_sheet_model_get_column_count (const GSheetModel *model)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (model), -1);

  return G_SHEET_MODEL_GET_IFACE (model)->get_column_count (model);
}

/**
 * g_sheet_model_get_row_count:
 * @model: A #GSheetModel
 *
 * Returns the total number of rows represented by the model
 **/
gint
g_sheet_model_get_row_count(const GSheetModel *model)
{
  g_return_val_if_fail (G_IS_SHEET_MODEL (model), -1);


  return G_SHEET_MODEL_GET_IFACE (model)->get_row_count (model);
}
