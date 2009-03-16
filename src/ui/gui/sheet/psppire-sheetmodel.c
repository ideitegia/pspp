/* PsppireSheetModel --- an abstract model for the PsppireSheet widget.
   Copyright (C) 2006, 2008 Free Software Foundation

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

#include <glib.h>
#include "psppire-sheetmodel.h"
#include <ui/gui/psppire-marshal.h>

enum {
  RANGE_CHANGED,
  ROWS_INSERTED,
  ROWS_DELETED,
  COLUMNS_INSERTED,
  COLUMNS_DELETED,
  LAST_SIGNAL
};

static guint sheet_model_signals[LAST_SIGNAL] = { 0 };


static void      psppire_sheet_model_base_init   (gpointer           g_class);


GType
psppire_sheet_model_get_type (void)
{
  static GType sheet_model_type = 0;

  if (! sheet_model_type)
    {
      static const GTypeInfo sheet_model_info =
      {
        sizeof (PsppireSheetModelIface), /* class_size */
	psppire_sheet_model_base_init,   /* base_init */
	NULL,		/* base_finalize */
	NULL,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      sheet_model_type =
	g_type_register_static (G_TYPE_INTERFACE, "PsppireSheetModel",
				&sheet_model_info, 0);

      g_type_interface_add_prerequisite (sheet_model_type, G_TYPE_OBJECT);
    }

  return sheet_model_type;
}

static void
psppire_sheet_model_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;

  if (! initialized)
    {
      sheet_model_signals[RANGE_CHANGED] =
	g_signal_new ("range_changed",
		      PSPPIRE_TYPE_SHEET_MODEL,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (PsppireSheetModelIface, range_changed),
		      NULL, NULL,
		      psppire_marshal_VOID__INT_INT_INT_INT,
		      G_TYPE_NONE, 4,
		      G_TYPE_INT,
		      G_TYPE_INT,
		      G_TYPE_INT,
		      G_TYPE_INT);



      sheet_model_signals[ROWS_INSERTED] =
	g_signal_new ("rows_inserted",
		      PSPPIRE_TYPE_SHEET_MODEL,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (PsppireSheetModelIface, rows_inserted),
		      NULL, NULL,
		      psppire_marshal_VOID__INT_INT,
		      G_TYPE_NONE, 2,
		      G_TYPE_INT,
		      G_TYPE_INT);


      sheet_model_signals[ROWS_DELETED] =
	g_signal_new ("rows_deleted",
		      PSPPIRE_TYPE_SHEET_MODEL,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (PsppireSheetModelIface, rows_deleted),
		      NULL, NULL,
		      psppire_marshal_VOID__INT_INT,
		      G_TYPE_NONE, 2,
		      G_TYPE_INT,
		      G_TYPE_INT);

      sheet_model_signals[COLUMNS_INSERTED] =
	g_signal_new ("columns_inserted",
		      PSPPIRE_TYPE_SHEET_MODEL,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (PsppireSheetModelIface, columns_inserted),
		      NULL, NULL,
		      psppire_marshal_VOID__INT_INT,
		      G_TYPE_NONE, 2,
		      G_TYPE_INT,
		      G_TYPE_INT);


      sheet_model_signals[COLUMNS_DELETED] =
	g_signal_new ("columns_deleted",
		      PSPPIRE_TYPE_SHEET_MODEL,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (PsppireSheetModelIface, columns_deleted),
		      NULL, NULL,
		      psppire_marshal_VOID__INT_INT,
		      G_TYPE_NONE, 2,
		      G_TYPE_INT,
		      G_TYPE_INT);


      initialized = TRUE;
    }
}


/**
 * psppire_sheet_model_free_strings
 * @sheet_model: A #PsppireSheetModel
 *
 * Returns: True if strings obtained with get_string should be freed by the
 * sheet when no longer required.
 **/
gboolean
psppire_sheet_model_free_strings (const PsppireSheetModel *sheet_model)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (sheet_model), FALSE);

  return PSPPIRE_SHEET_MODEL_GET_IFACE (sheet_model)->free_strings;
}


/**
 * psppire_sheet_model_get_string:
 * @sheet_model: A #PsppireSheetModel
 * @row: The row of the cell to be retrieved.
 * @column: The column of the cell to be retrieved.
 *
 * Retrieves the datum at location ROW, COLUMN in the form of a string.
 * Returns: The string representation of the datum, or NULL on error.
 **/
gchar *
psppire_sheet_model_get_string (const PsppireSheetModel *sheet_model,
			  glong row, glong column)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (sheet_model), 0);

  g_assert (PSPPIRE_SHEET_MODEL_GET_IFACE (sheet_model)->get_string);

  return (PSPPIRE_SHEET_MODEL_GET_IFACE (sheet_model)->get_string) (sheet_model, row, column);
}

/**
 * psppire_sheet_model_set_string
 * @sheet_model: A #PsppireSheetModel
 * @text: The text describing the datum to be set.
 * @row: The row of the cell to be cleared.
 * @column: The column of the cell to be cleared.
 *
 * Sets the datum at a location from a string.
 * Returns: TRUE if the datum was changed, FALSE otherwise.
 **/
gboolean
psppire_sheet_model_set_string      (PsppireSheetModel *sheet_model,
				 const gchar *text,
				 glong row, glong column)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (sheet_model), FALSE);

  g_assert (PSPPIRE_SHEET_MODEL_GET_IFACE (sheet_model)->set_string);

  return PSPPIRE_SHEET_MODEL_GET_IFACE (sheet_model)->set_string (sheet_model,
							    text, row, column);
}



/**
 * psppire_sheet_model_datum_clear:
 * @sheet_model: A #PsppireSheetModel
 * @row: The row of the cell to be cleared.
 * @column: The column of the cell to be cleared.
 *
 * Called when the datum at a location is to be cleared.
 * Returns: TRUE if the datum was cleared, FALSE otherwise.
 **/
gboolean
psppire_sheet_model_datum_clear    (PsppireSheetModel *sheet_model,
				glong row, glong column)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (sheet_model), FALSE);

  g_assert (PSPPIRE_SHEET_MODEL_GET_IFACE (sheet_model)->clear_datum);

  return PSPPIRE_SHEET_MODEL_GET_IFACE (sheet_model)->clear_datum (sheet_model,
								row, column);
}


/**
 * psppire_sheet_model_range_changed:
 * @sheet_model: A #PsppireSheetModel
 * @range: The #PsppireSheetRange range of cells which have changed.
 *
 * Emits the "range_changed" signal on @sheet_model.
 **/
void
psppire_sheet_model_range_changed (PsppireSheetModel *sheet_model,
			       glong row0, glong col0,
			       glong rowi, glong coli)
{
  g_return_if_fail (PSPPIRE_IS_SHEET_MODEL (sheet_model));

  g_signal_emit (sheet_model, sheet_model_signals[RANGE_CHANGED], 0,
		 row0, col0, rowi, coli);
}




/**
 * psppire_sheet_model_rows_inserted:
 * @sheet_model: A #PsppireSheetModel
 * @row: The row before which the new rows should be inserted.
 * @n_rows: The number of rows to insert.
 *
 * Emits the "rows_inserted" signal on @sheet_model.
 **/
void
psppire_sheet_model_rows_inserted (PsppireSheetModel *sheet_model,
			       glong row, glong n_rows)
{
  g_return_if_fail (PSPPIRE_IS_SHEET_MODEL (sheet_model));

  g_signal_emit (sheet_model, sheet_model_signals[ROWS_INSERTED], 0,
		 row, n_rows);
}


/**
 * psppire_sheet_model_columns_inserted:
 * @sheet_model: A #PsppireSheetModel
 * @column: The column before which the new columns should be inserted.
 * @n_columns: The number of columns to insert.
 *
 * Emits the "columns_inserted" signal on @sheet_model.
 **/
void
psppire_sheet_model_columns_inserted (PsppireSheetModel *sheet_model,
			       glong column, glong n_columns)
{
  g_return_if_fail (PSPPIRE_IS_SHEET_MODEL (sheet_model));

  g_signal_emit (sheet_model, sheet_model_signals[COLUMNS_INSERTED], 0,
		 column, n_columns);
}




/**
 * psppire_sheet_model_rows_deleted:
 * @sheet_model: A #PsppireSheetModel
 * @row: The first row to be deleted.
 * @n_rows: The number of rows to delete.
 *
 * Emits the "rows_deleted" signal on @sheet_model.
 **/
void
psppire_sheet_model_rows_deleted (PsppireSheetModel *sheet_model,
			       glong row, glong n_rows)
{
  g_return_if_fail (PSPPIRE_IS_SHEET_MODEL (sheet_model));

  g_signal_emit (sheet_model, sheet_model_signals[ROWS_DELETED], 0,
		 row, n_rows);
}



/**
 * psppire_sheet_model_columns_deleted:
 * @sheet_model: A #PsppireSheetModel
 * @column: The first column to be deleted.
 * @n_columns: The number of columns to delete.
 *
 * Emits the "columns_deleted" signal on @sheet_model.
 **/
void
psppire_sheet_model_columns_deleted (PsppireSheetModel *sheet_model,
			       glong column, glong n_columns)
{
  g_return_if_fail (PSPPIRE_IS_SHEET_MODEL (sheet_model));

  g_signal_emit (sheet_model, sheet_model_signals[COLUMNS_DELETED], 0,
		 column, n_columns);
}





/**
 * psppire_sheet_model_is_editable:
 * @sheet_model: A #PsppireSheetModel
 * @row: The row
 * @column: The column
 *
 * Returns: TRUE if the cell is editable, FALSE otherwise
 **/
gboolean
psppire_sheet_model_is_editable (const PsppireSheetModel *model,
			     glong row, glong column)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), TRUE);

  if ( ! PSPPIRE_SHEET_MODEL_GET_IFACE (model)->is_editable )
    return TRUE;

  return PSPPIRE_SHEET_MODEL_GET_IFACE (model)->is_editable (model,
							  row, column);
}


/**
 * psppire_sheet_model_get_foreground:
 * @sheet_model: A #PsppireSheetModel
 * @row: The row
 * @column: The column
 *
 * Returns the foreground colour of the cell at @row, @column
 * The color is unallocated.  It will be allocated by the viewing object.
 **/
GdkColor *
psppire_sheet_model_get_foreground (const PsppireSheetModel *model,
				glong row, glong column)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), NULL);

  if ( ! PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_foreground )
    return NULL;

  return PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_foreground (model,
							    row, column);
}

/**
 * psppire_sheet_model_get_background:
 * @sheet_model: A #PsppireSheetModel
 * @row: The row
 * @column: The column
 *
 * Returns the background colour of the cell at @row, @column
 * The color is unallocated.  It will be allocated by the viewing object.
 **/
GdkColor *
psppire_sheet_model_get_background (const PsppireSheetModel *model,
				glong row, glong column)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), NULL);

  if ( ! PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_background )
    return NULL;

  return PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_background (model,
							    row, column);
}

/**
 * psppire_sheet_model_get_justification:
 * @sheet_model: A #PsppireSheetModel
 * @row: The row
 * @column: The column
 *
 * Returns the justification of the cell at @row, @column
 * Returns: the justification, or NULL on error.
 **/
const GtkJustification *
psppire_sheet_model_get_justification (const PsppireSheetModel *model,
				   glong row, glong column)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), NULL);

  if ( ! PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_justification)
    return NULL;

  return PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_justification (model,
							       row, column);
}


/**
 * psppire_sheet_model_get_column_count:
 * @model: A #PsppireSheetModel
 *
 * Returns the total number of columns represented by the model
 **/
glong
psppire_sheet_model_get_column_count (const PsppireSheetModel *model)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), -1);

  return PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_column_count (model);
}

/**
 * psppire_sheet_model_get_row_count:
 * @model: A #PsppireSheetModel
 *
 * Returns the total number of rows represented by the model
 **/
gint
psppire_sheet_model_get_row_count(const PsppireSheetModel *model)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), -1);

  return PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_row_count (model);
}



/* Column related functions  */
gboolean
psppire_sheet_model_get_column_sensitivity (const PsppireSheetModel *model, gint col)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), FALSE);

  if ( NULL == PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_column_sensitivity)
    return TRUE;

  return PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_column_sensitivity (model, col);
}


gchar *
psppire_sheet_model_get_column_subtitle (const PsppireSheetModel *model,
				   gint col)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), NULL);
  g_return_val_if_fail (col >= 0, NULL);

  if ( NULL == PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_column_subtitle)
    return NULL;

  return PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_column_subtitle (model, col);
}


PsppireSheetButton *
psppire_sheet_model_get_column_button (const PsppireSheetModel *model,
				 gint col)
{
  PsppireSheetButton *button = psppire_sheet_button_new ();

  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), NULL);

  if ( PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_column_title)
    button->label = PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_column_title (model, col);

  button->overstruck = FALSE;

  return button;
}

GtkJustification
psppire_sheet_model_get_column_justification (const PsppireSheetModel *model,
					gint col)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), GTK_JUSTIFY_LEFT);

  if ( PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_column_justification)
    return PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_column_justification (model, col);

  return GTK_JUSTIFY_LEFT;
}



gboolean
psppire_sheet_model_get_row_sensitivity (const PsppireSheetModel *model, gint row)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), FALSE);

  if ( NULL == PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_row_sensitivity)
    return TRUE;

  return PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_row_sensitivity (model, row);
}



gchar *
psppire_sheet_model_get_row_subtitle (const PsppireSheetModel *model,
				gint row)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), NULL);

  if ( NULL == PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_row_subtitle)
    return NULL;

  return PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_row_subtitle (model, row);
}


PsppireSheetButton *
psppire_sheet_model_get_row_button (const PsppireSheetModel *model,
				 gint row)
{
  PsppireSheetButton *button = psppire_sheet_button_new ();

  g_return_val_if_fail (PSPPIRE_IS_SHEET_MODEL (model), NULL);

  if ( PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_row_title)
    button->label =
      PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_row_title (model, row);

  if ( PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_row_overstrike)
    button->overstruck =
      PSPPIRE_SHEET_MODEL_GET_IFACE (model)->get_row_overstrike (model, row);

  return button;
}

