/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008 Free Software Foundation, Inc.

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
#include <gtk/gtksignal.h>
#include <gtk/gtk.h>
#include <gtksheet/gtkextra-sheet.h>
#include "psppire-data-editor.h"
#include "psppire-var-sheet.h"

#include <gtksheet/gsheet-hetero-column.h>
#include <language/syntax-string-source.h>
#include "psppire-data-store.h"
#include "helper.h"

#include <gtksheet/gtkxpaned.h>
#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void psppire_data_editor_remove_split (PsppireDataEditor *de);
static void psppire_data_editor_set_split (PsppireDataEditor *de);

enum {
  DATA_SELECTION_CHANGED,
  DATA_AVAILABLE_CHANGED,
  CASES_SELECTED,
  VARIABLES_SELECTED,
  n_SIGNALS
};


static guint data_editor_signals [n_SIGNALS] = { 0 };


static gboolean data_is_selected (PsppireDataEditor *de);

static void psppire_data_editor_class_init          (PsppireDataEditorClass *klass);
static void psppire_data_editor_init                (PsppireDataEditor      *de);

GType
psppire_data_editor_get_type (void)
{
  static GType de_type = 0;

  if (!de_type)
    {
      static const GTypeInfo de_info =
      {
	sizeof (PsppireDataEditorClass),
	NULL, /* base_init */
        NULL, /* base_finalize */
	(GClassInitFunc) psppire_data_editor_class_init,
        NULL, /* class_finalize */
	NULL, /* class_data */
        sizeof (PsppireDataEditor),
	0,
	(GInstanceInitFunc) psppire_data_editor_init,
      };

      de_type = g_type_register_static (GTK_TYPE_NOTEBOOK, "PsppireDataEditor",
					&de_info, 0);
    }

  return de_type;
}

static GObjectClass * parent_class = NULL;

static void
psppire_data_editor_dispose (GObject *obj)
{
  PsppireDataEditor *de = (PsppireDataEditor *) obj;

  if (de->dispose_has_run)
    return;

  g_object_unref (de->data_store);
  g_object_unref (de->var_store);

  /* Make sure dispose does not run twice. */
  de->dispose_has_run = TRUE;

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
psppire_data_editor_finalize (GObject *obj)
{
   /* Chain up to the parent class */
   G_OBJECT_CLASS (parent_class)->finalize (obj);
}



static void popup_variable_menu (GtkSheet *sheet, gint column,
				 GdkEventButton *event, gpointer data);

static void popup_cases_menu (GtkSheet *sheet, gint row,
			      GdkEventButton *event, gpointer data);




/* Callback which occurs when the data sheet's column title
   is double clicked */
static gboolean
on_data_column_clicked (PsppireDataEditor *de, gint col, gpointer data)
{

  gint current_row, current_column;

  gtk_notebook_set_current_page (GTK_NOTEBOOK (de), PSPPIRE_DATA_EDITOR_VARIABLE_VIEW);

  gtk_sheet_get_active_cell (GTK_SHEET (de->var_sheet),
			     &current_row, &current_column);

  gtk_sheet_set_active_cell (GTK_SHEET (de->var_sheet), col, current_column);

  return FALSE;
}





/* Callback which occurs when the var sheet's row title
   button is double clicked */
static gboolean
on_var_row_clicked (PsppireDataEditor *de, gint row, gpointer data)
{
  GtkSheetRange visible_range;

  gint current_row, current_column;

  gtk_notebook_set_current_page (GTK_NOTEBOOK(de), PSPPIRE_DATA_EDITOR_DATA_VIEW);

  gtk_sheet_get_active_cell (GTK_SHEET (de->data_sheet[0]),
			     &current_row, &current_column);

  gtk_sheet_set_active_cell (GTK_SHEET (de->data_sheet[0]), current_row, row);

  gtk_sheet_get_visible_range (GTK_SHEET (de->data_sheet[0]), &visible_range);

  if ( row < visible_range.col0 || row > visible_range.coli)
    {
      gtk_sheet_moveto (GTK_SHEET (de->data_sheet[0]),
			-1, row, 0, 0);
    }

  return FALSE;
}


/* Moves the focus to a new cell.
   Returns TRUE iff the move should be disallowed */
static gboolean
traverse_cell_callback (GtkSheet *sheet,
			GtkSheetCell *existing_cell,
			GtkSheetCell *new_cell,
			gpointer data)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (data);
  const PsppireDict *dict = de->data_store->dict;

  if ( new_cell->col >= psppire_dict_get_var_cnt (dict))
    return TRUE;

  return FALSE;
}


enum
  {
    PROP_0,
    PROP_DATA_STORE,
    PROP_VAR_STORE,
    PROP_COLUMN_MENU,
    PROP_ROW_MENU,
    PROP_VALUE_LABELS,
    PROP_CURRENT_CASE,
    PROP_CURRENT_VAR,
    PROP_DATA_SELECTED,
    PROP_SPLIT_WINDOW
  };

static void
psppire_data_editor_set_property (GObject         *object,
				  guint            prop_id,
				  const GValue    *value,
				  GParamSpec      *pspec)
{
  int i;
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (object);

  switch (prop_id)
    {
    case PROP_SPLIT_WINDOW:
      psppire_data_editor_split_window (de, g_value_get_boolean (value));
      break;
    case PROP_DATA_STORE:
      if ( de->data_store) g_object_unref (de->data_store);
      de->data_store = g_value_get_pointer (value);
      g_object_ref (de->data_store);

      for (i = 0 ; i < 4 ; ++i )
	g_object_set (de->data_sheet[i],
		      "row-geometry", de->data_store,
		      "column-geometry", de->data_store,
		      "model", de->data_store,
		      NULL);
      break;
    case PROP_VAR_STORE:
      if ( de->var_store) g_object_unref (de->var_store);
      de->var_store = g_value_get_pointer (value);
      g_object_ref (de->var_store);

      g_object_set (de->var_sheet,
		    "row-geometry", de->var_store,
		    "model", de->var_store,
		    NULL);
      break;
    case PROP_COLUMN_MENU:
      {
	GObject *menu = g_value_get_object (value);

	g_signal_connect (de->data_sheet[0], "button-event-column",
			  G_CALLBACK (popup_variable_menu), menu);
      }
      break;
    case PROP_ROW_MENU:
      {
	GObject *menu = g_value_get_object (value);

	g_signal_connect (de->data_sheet[0], "button-event-row",
			  G_CALLBACK (popup_cases_menu), menu);
      }
      break;
    case PROP_CURRENT_VAR:
      {
	gint row, col;
	gint var = g_value_get_long (value);
	switch (gtk_notebook_get_current_page (GTK_NOTEBOOK (object)))
	  {
	  case PSPPIRE_DATA_EDITOR_DATA_VIEW:
	    gtk_sheet_get_active_cell (GTK_SHEET (de->data_sheet[0]), &row, &col);
	    gtk_sheet_set_active_cell (GTK_SHEET (de->data_sheet[0]), row, var);
	    gtk_sheet_moveto (GTK_SHEET (de->data_sheet[0]), -1, var, 0.5, 0.5);
	    break;
	  case PSPPIRE_DATA_EDITOR_VARIABLE_VIEW:
	    gtk_sheet_get_active_cell (GTK_SHEET (de->var_sheet), &row, &col);
	    gtk_sheet_set_active_cell (GTK_SHEET (de->var_sheet), var, col);
	    gtk_sheet_moveto (GTK_SHEET (de->var_sheet), var, -1,  0.5, 0.5);
	    break;
	  default:
	    g_assert_not_reached ();
	    break;
	  };
      }
      break;
    case PROP_CURRENT_CASE:
      {
	gint row, col;
	gint case_num = g_value_get_long (value);
	gtk_sheet_get_active_cell (GTK_SHEET (de->data_sheet[0]), &row, &col);
	gtk_sheet_set_active_cell (GTK_SHEET (de->data_sheet[0]), case_num, col);
	gtk_sheet_moveto (GTK_SHEET (de->data_sheet[0]), case_num, -1, 0.5, 0.5);
      }
      break;
    case PROP_VALUE_LABELS:
      {
	psppire_data_store_show_labels (de->data_store,
					g_value_get_boolean (value));
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
psppire_data_editor_get_property (GObject         *object,
				  guint            prop_id,
				  GValue          *value,
				  GParamSpec      *pspec)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (object);

  switch (prop_id)
    {
    case PROP_SPLIT_WINDOW:
      g_value_set_boolean (value, de->split);
      break;
    case PROP_DATA_STORE:
      g_value_set_pointer (value, de->data_store);
      break;
    case PROP_VAR_STORE:
      g_value_set_pointer (value, de->var_store);
      break;
    case PROP_CURRENT_CASE:
      {
	gint row, column;
	gtk_sheet_get_active_cell (GTK_SHEET (de->data_sheet[0]), &row, &column);
	g_value_set_long (value, row);
      }
      break;
    case PROP_CURRENT_VAR:
      {
	gint row, column;
	gtk_sheet_get_active_cell (GTK_SHEET (de->data_sheet[0]), &row, &column);
	g_value_set_long (value, column);
      }
      break;
    case PROP_DATA_SELECTED:
      g_value_set_boolean (value, data_is_selected (de));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_data_editor_class_init (PsppireDataEditorClass *klass)
{
  GParamSpec *data_store_spec ;
  GParamSpec *var_store_spec ;
  GParamSpec *column_menu_spec;
  GParamSpec *row_menu_spec;
  GParamSpec *value_labels_spec;
  GParamSpec *current_case_spec;
  GParamSpec *current_var_spec;
  GParamSpec *data_selected_spec;
  GParamSpec *split_window_spec;
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->dispose = psppire_data_editor_dispose;
  object_class->finalize = psppire_data_editor_finalize;

  object_class->set_property = psppire_data_editor_set_property;
  object_class->get_property = psppire_data_editor_get_property;

  data_store_spec =
    g_param_spec_pointer ("data-store",
			  "Data Store",
			  "A pointer to the data store associated with this editor",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_READABLE );

  g_object_class_install_property (object_class,
                                   PROP_DATA_STORE,
                                   data_store_spec);

  var_store_spec =
    g_param_spec_pointer ("var-store",
			  "Variable Store",
			  "A pointer to the variable store associated with this editor",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_READABLE );

  g_object_class_install_property (object_class,
                                   PROP_VAR_STORE,
                                   var_store_spec);

  column_menu_spec =
    g_param_spec_object ("column-menu",
			 "Column Menu",
			 "A menu to be displayed when button 3 is pressed in the column title buttons",
			 GTK_TYPE_MENU,
			 G_PARAM_WRITABLE);

  g_object_class_install_property (object_class,
                                   PROP_COLUMN_MENU,
                                   column_menu_spec);


  row_menu_spec =
    g_param_spec_object ("row-menu",
			 "Row Menu",
			 "A menu to be displayed when button 3 is pressed in the row title buttons",
			 GTK_TYPE_MENU,
			 G_PARAM_WRITABLE);

  g_object_class_install_property (object_class,
                                   PROP_ROW_MENU,
                                   row_menu_spec);

  value_labels_spec =
    g_param_spec_boolean ("value-labels",
			 "Value Labels",
			 "Whether or not the data sheet should display labels instead of values",
			  FALSE,
			 G_PARAM_WRITABLE | G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_VALUE_LABELS,
                                   value_labels_spec);


  current_case_spec =
    g_param_spec_long ("current-case",
		       "Current Case",
		       "Zero based number of the selected case",
		       0, CASENUMBER_MAX,
		       0,
		       G_PARAM_WRITABLE | G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_CURRENT_CASE,
                                   current_case_spec);


  current_var_spec =
    g_param_spec_long ("current-variable",
		       "Current Variable",
		       "Zero based number of the selected variable",
		       0, G_MAXINT,
		       0,
		       G_PARAM_WRITABLE | G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_CURRENT_VAR,
                                   current_var_spec);


  data_selected_spec =
    g_param_spec_boolean ("data-selected",
			  "Data Selected",
			  "True iff the data view is active and  one or more cells of data have been selected.",
			  FALSE,
			  G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_DATA_SELECTED,
                                   data_selected_spec);



  split_window_spec =
    g_param_spec_boolean ("split",
			  "Split Window",
			  "True iff the data sheet is split",
			  FALSE,
			  G_PARAM_READABLE | G_PARAM_WRITABLE);

  g_object_class_install_property (object_class,
                                   PROP_SPLIT_WINDOW,
                                   split_window_spec);

  data_editor_signals [DATA_SELECTION_CHANGED] =
    g_signal_new ("data-selection-changed",
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__BOOLEAN,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_BOOLEAN);

  data_editor_signals [CASES_SELECTED] =
    g_signal_new ("cases-selected",
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  data_editor_signals [VARIABLES_SELECTED] =
    g_signal_new ("variables-selected",
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  data_editor_signals [DATA_AVAILABLE_CHANGED] =
    g_signal_new ("data-available-changed",
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__BOOLEAN,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_BOOLEAN);
}

/* Update the data_ref_entry with the reference of the active cell */
static gint
update_data_ref_entry (const GtkSheet *sheet,
		       gint row, gint col,
		       gint old_row, gint old_col,
		       gpointer data)
{
  PsppireDataEditor *de = data;

  PsppireDataStore *data_store =
    PSPPIRE_DATA_STORE (gtk_sheet_get_model (sheet));

  if (data_store)
    {
      const struct variable *var =
	psppire_dict_get_variable (data_store->dict, col);

      if ( var )
	{
	  gchar *text = g_strdup_printf ("%d: %s", row + FIRST_CASE_NUMBER,
					 var_get_name (var));

	  gchar *s = pspp_locale_to_utf8 (text, -1, 0);

	  g_free (text);

	  gtk_entry_set_text (GTK_ENTRY (de->cell_ref_entry), s);

	  g_free (s);
	}
      else
	goto blank_entry;

      if ( var )
	{
	  gchar *text =
	    psppire_data_store_get_string (data_store, row,
					   var_get_dict_index(var));

	  if ( ! text )
	    goto blank_entry;

	  g_strchug (text);

	  gtk_entry_set_text (GTK_ENTRY (de->datum_entry), text);

	  g_free (text);
	}
      else
	goto blank_entry;

    }

  return FALSE;

 blank_entry:
  gtk_entry_set_text (GTK_ENTRY (de->datum_entry), "");

  return FALSE;
}


static void
datum_entry_activate (GtkEntry *entry, gpointer data)
{
  gint row, column;
  PsppireDataEditor *de = data;

  const gchar *text = gtk_entry_get_text (entry);

  gtk_sheet_get_active_cell (GTK_SHEET (de->data_sheet[0]), &row, &column);

  if ( row == -1 || column == -1)
    return;

  psppire_data_store_set_string (de->data_store, text, row, column);
}

static void on_activate (PsppireDataEditor *de);
static gboolean on_switch_page (PsppireDataEditor *de, GtkNotebookPage *p, gint pagenum, gpointer data);
static void on_select_range (PsppireDataEditor *de);

static void on_select_row (GtkSheet *, gint, PsppireDataEditor *);
static void on_select_variable (GtkSheet *, gint, PsppireDataEditor *);


static void on_owner_change (GtkClipboard *,
			     GdkEventOwnerChange *, gpointer);

static void
on_map (GtkWidget *w)
{
  GtkClipboard *clip = gtk_widget_get_clipboard (w, GDK_SELECTION_CLIPBOARD);

  g_signal_connect (clip, "owner-change", G_CALLBACK (on_owner_change), w);
}


static void
init_sheet (PsppireDataEditor *de, int i,
	    GtkAdjustment *hadj, GtkAdjustment *vadj)
{
  de->sheet_bin[i] = gtk_scrolled_window_new (hadj, vadj);

  de->data_sheet[i] = gtk_sheet_new (NULL, NULL, NULL);

  g_object_set (de->sheet_bin[i],
		"border-width", 3,
		"shadow-type",  GTK_SHADOW_ETCHED_IN,
		NULL);

  gtk_container_add (GTK_CONTAINER (de->sheet_bin[i]), de->data_sheet[i]);

  g_signal_connect (de->data_sheet[i], "traverse",
		    G_CALLBACK (traverse_cell_callback), de);

  gtk_widget_show (de->sheet_bin[i]);
}


static void
init_data_sheet (PsppireDataEditor *de)
{
  GtkAdjustment *va0, *ha0;
  GtkAdjustment *va1, *ha1;
  GtkWidget *sheet ;

  de->split = TRUE;
  de->paned = gtk_xpaned_new ();

  init_sheet (de, 0, NULL, NULL);
  gtk_widget_show (de->sheet_bin[0]);
  va0 = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (de->sheet_bin[0]));
  ha0 = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (de->sheet_bin[0]));

  g_object_set (de->sheet_bin[0], "vscrollbar-policy", GTK_POLICY_NEVER, NULL);
  g_object_set (de->sheet_bin[0], "hscrollbar-policy", GTK_POLICY_NEVER, NULL);

  init_sheet (de, 1, NULL, va0);
  gtk_widget_show (de->sheet_bin[1]);
  sheet = gtk_bin_get_child (GTK_BIN (de->sheet_bin[1]));
  gtk_sheet_hide_row_titles (GTK_SHEET (sheet));
  ha1 = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (de->sheet_bin[1]));
  g_object_set (de->sheet_bin[1], "vscrollbar-policy", GTK_POLICY_ALWAYS, NULL);
  g_object_set (de->sheet_bin[1], "hscrollbar-policy", GTK_POLICY_NEVER, NULL);

  init_sheet (de, 2, ha0, NULL);
  gtk_widget_show (de->sheet_bin[2]);
  sheet = gtk_bin_get_child (GTK_BIN (de->sheet_bin[2]));
  gtk_sheet_hide_column_titles (GTK_SHEET (sheet));
  g_object_set (de->sheet_bin[2], "vscrollbar-policy", GTK_POLICY_NEVER, NULL);
  g_object_set (de->sheet_bin[2], "hscrollbar-policy", GTK_POLICY_ALWAYS, NULL);
  va1 = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (de->sheet_bin[2]));

  init_sheet (de, 3, ha1, va1);
  gtk_widget_show (de->sheet_bin[3]);
  sheet = gtk_bin_get_child (GTK_BIN (de->sheet_bin[3]));
  gtk_sheet_hide_column_titles (GTK_SHEET (sheet));
  gtk_sheet_hide_row_titles (GTK_SHEET (sheet));
  g_object_set (de->sheet_bin[3], "vscrollbar-policy", GTK_POLICY_ALWAYS, NULL);
  g_object_set (de->sheet_bin[3], "hscrollbar-policy", GTK_POLICY_ALWAYS, NULL);

  gtk_xpaned_pack_top_left (GTK_XPANED (de->paned), de->sheet_bin[0], TRUE, TRUE);
  gtk_xpaned_pack_top_right (GTK_XPANED (de->paned), de->sheet_bin[1], TRUE, TRUE);
  gtk_xpaned_pack_bottom_left (GTK_XPANED (de->paned), de->sheet_bin[2], TRUE, TRUE);
  gtk_xpaned_pack_bottom_right (GTK_XPANED (de->paned), de->sheet_bin[3], TRUE, TRUE);

  gtk_xpaned_set_position_y (GTK_XPANED (de->paned), 150);
  gtk_xpaned_set_position_x (GTK_XPANED (de->paned), 350);
}


static void
psppire_data_editor_init (PsppireDataEditor *de)
{
  GtkWidget *hbox = gtk_hbox_new (FALSE, 0);
  GtkWidget *sw_vs = gtk_scrolled_window_new (NULL, NULL);

  init_data_sheet (de);

  de->data_vbox = gtk_vbox_new (FALSE, 0);
  de->var_sheet = psppire_var_sheet_new ();

  g_object_set (de, "tab-pos", GTK_POS_BOTTOM, NULL);

  de->datum_entry = gtk_entry_new ();
  de->cell_ref_entry = gtk_entry_new ();

  g_object_set (de->cell_ref_entry,
		"sensitive", FALSE,
		"editable",  FALSE,
		"width_chars", 25,
		NULL);

  gtk_box_pack_start (GTK_BOX (hbox), de->cell_ref_entry, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), de->datum_entry, TRUE, TRUE, 0);


  gtk_container_add (GTK_CONTAINER (sw_vs), de->var_sheet);
  gtk_widget_show_all (sw_vs);


  gtk_box_pack_start (GTK_BOX (de->data_vbox), hbox, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (de->data_vbox), de->paned, TRUE, TRUE, 0);


  psppire_data_editor_remove_split (de);

  gtk_widget_show_all (de->data_vbox);

  gtk_notebook_append_page (GTK_NOTEBOOK (de), de->data_vbox,
			    gtk_label_new_with_mnemonic (_("Data View")));

  gtk_notebook_append_page (GTK_NOTEBOOK (de), sw_vs,
			    gtk_label_new_with_mnemonic (_("Variable View")));

  g_signal_connect (de->data_sheet[0], "activate",
		    G_CALLBACK (update_data_ref_entry),
		    de);

  g_signal_connect (de->datum_entry, "activate",
		    G_CALLBACK (datum_entry_activate),
		    de);


  g_signal_connect_swapped (de->data_sheet[0],
		    "double-click-column",
		    G_CALLBACK (on_data_column_clicked),
		    de);

  g_signal_connect_swapped (de->var_sheet,
		    "double-click-row",
		    G_CALLBACK (on_var_row_clicked),
		    de);

  g_signal_connect_swapped (de->data_sheet[0], "activate",
			    G_CALLBACK (on_activate),
			    de);

  g_signal_connect_swapped (de->data_sheet[0], "select-range",
			    G_CALLBACK (on_select_range),
			    de);

  g_signal_connect (de->data_sheet[0], "select-row",
		    G_CALLBACK (on_select_row), de);

  g_signal_connect (de->data_sheet[0], "select-column",
		    G_CALLBACK (on_select_variable), de);


  g_signal_connect (de->var_sheet, "select-row",
		    G_CALLBACK (on_select_variable), de);


  g_signal_connect_after (de, "switch-page",
		    G_CALLBACK (on_switch_page),
		    NULL);


  g_signal_connect (de, "map", G_CALLBACK (on_map), NULL);



  //     gtk_sheet_hide_column_titles (de->var_sheet);
  //  gtk_sheet_hide_row_titles (de->data_sheet);


  de->dispose_has_run = FALSE;
}


GtkWidget*
psppire_data_editor_new (PsppireVarStore *var_store,
			 PsppireDataStore *data_store)
{
  GtkWidget *widget;

  widget =  g_object_new (PSPPIRE_DATA_EDITOR_TYPE,
			  "var-store",  var_store,
			  "data-store",  data_store,
			  NULL);



  return widget;
}


static void
psppire_data_editor_remove_split (PsppireDataEditor *de)
{
  if ( !de->split ) return;
  de->split = FALSE;

  g_object_ref (de->sheet_bin[0]);
  gtk_container_remove (GTK_CONTAINER (de->paned), de->sheet_bin[0]);

  g_object_ref (de->paned);
  gtk_container_remove (GTK_CONTAINER (de->data_vbox), de->paned);

  gtk_box_pack_start (GTK_BOX (de->data_vbox), de->sheet_bin[0],
		      TRUE, TRUE, 0);

  g_object_unref (de->sheet_bin[0]);

  g_object_set (de->sheet_bin[0], "vscrollbar-policy",
		GTK_POLICY_ALWAYS, NULL);

  g_object_set (de->sheet_bin[0], "hscrollbar-policy",
		GTK_POLICY_ALWAYS, NULL);
}


static void
psppire_data_editor_set_split (PsppireDataEditor *de)
{
  if ( de->split ) return;
  de->split = TRUE;

  g_object_ref (de->sheet_bin[0]);
  gtk_container_remove (GTK_CONTAINER (de->data_vbox), de->sheet_bin[0]);

  gtk_xpaned_pack_top_left (GTK_XPANED (de->paned), de->sheet_bin [0],
			    TRUE, TRUE);

  gtk_box_pack_start (GTK_BOX (de->data_vbox), de->paned,
		      TRUE, TRUE, 0);

  g_object_unref (de->paned);

  g_object_set (de->sheet_bin[0], "vscrollbar-policy",
		GTK_POLICY_NEVER, NULL);

  g_object_set (de->sheet_bin[0], "hscrollbar-policy",
		GTK_POLICY_NEVER, NULL);
}

void
psppire_data_editor_split_window (PsppireDataEditor *de, gboolean split)
{
  if (split )
    psppire_data_editor_set_split (de);
  else
    psppire_data_editor_remove_split (de);

  gtk_widget_show_all (de->data_vbox);
}

static void data_sheet_set_clip (GtkSheet *sheet);
static void data_sheet_contents_received_callback (GtkClipboard *clipboard,
						   GtkSelectionData *sd,
						   gpointer data);


void
psppire_data_editor_clip_copy (PsppireDataEditor *de)
{
  data_sheet_set_clip (GTK_SHEET (de->data_sheet[0]));
}

void
psppire_data_editor_clip_paste (PsppireDataEditor *de)
{
  GdkDisplay *display = gtk_widget_get_display ( GTK_WIDGET (de));
  GtkClipboard *clipboard =
    gtk_clipboard_get_for_display (display, GDK_SELECTION_CLIPBOARD);

  gtk_clipboard_request_contents (clipboard,
				  gdk_atom_intern ("UTF8_STRING", TRUE),
				  data_sheet_contents_received_callback,
				  de);
}



void
psppire_data_editor_clip_cut (PsppireDataEditor *de)
{
  gint max_rows, max_columns;
  gint r;
  GtkSheetRange range;
  PsppireDataStore *ds = de->data_store;

  data_sheet_set_clip (GTK_SHEET (de->data_sheet[0]));

  /* Now blank all the cells */
  gtk_sheet_get_selected_range (GTK_SHEET (de->data_sheet[0]), &range);

   /* If nothing selected, then use active cell */
  if ( range.row0 < 0 || range.col0 < 0 )
    {
      gint row, col;
      gtk_sheet_get_active_cell (GTK_SHEET (de->data_sheet[0]), &row, &col);

      range.row0 = range.rowi = row;
      range.col0 = range.coli = col;
    }

  /* The sheet range can include cells that do not include data.
     Exclude them from the range. */
  max_rows = psppire_data_store_get_case_count (ds);
  if (range.rowi >= max_rows)
    {
      if (max_rows == 0)
        return;
      range.rowi = max_rows - 1;
    }

  max_columns = dict_get_var_cnt (ds->dict->dict);
  if (range.coli >= max_columns)
    {
      if (max_columns == 0)
        return;
      range.coli = max_columns - 1;
    }

  g_return_if_fail (range.rowi >= range.row0);
  g_return_if_fail (range.row0 >= 0);
  g_return_if_fail (range.coli >= range.col0);
  g_return_if_fail (range.col0 >= 0);


  for (r = range.row0; r <= range.rowi ; ++r )
    {
      gint c;

      for (c = range.col0 ; c <= range.coli; ++c)
	{
	  psppire_data_store_set_string (ds, "", r, c);
	}
    }

  /* and remove the selection */
  gtk_sheet_unselect_range (GTK_SHEET (de->data_sheet[0]));
}




/* Popup menu related stuff */

static void
popup_variable_menu (GtkSheet *sheet, gint column,
		     GdkEventButton *event, gpointer data)
{
  GtkMenu *menu = GTK_MENU (data);

  PsppireDataStore *data_store =
    PSPPIRE_DATA_STORE (gtk_sheet_get_model (sheet));

  const struct variable *v =
    psppire_dict_get_variable (data_store->dict, column);

  if ( v && event->button == 3)
    {
      gtk_sheet_select_column (sheet, column);

      gtk_menu_popup (menu,
		      NULL, NULL, NULL, NULL,
		      event->button, event->time);
    }
}


static void
popup_cases_menu (GtkSheet *sheet, gint row,
		  GdkEventButton *event, gpointer data)
{
  GtkMenu *menu = GTK_MENU (data);

  PsppireDataStore *data_store =
    PSPPIRE_DATA_STORE (gtk_sheet_get_model (sheet));

  if ( row <= psppire_data_store_get_case_count (data_store) &&
       event->button == 3)
    {
      gtk_sheet_select_row (sheet, row);

      gtk_menu_popup (menu,
		      NULL, NULL, NULL, NULL,
		      event->button, event->time);
    }
}



/* Sorting */

static void
do_sort (PsppireDataStore *ds, int var, gboolean descend)
{
  GString *string = g_string_new ("SORT CASES BY ");

  const struct variable *v =
    psppire_dict_get_variable (ds->dict, var);

  g_string_append_printf (string, "%s", var_get_name (v));

  if ( descend )
    g_string_append (string, " (D)");

  g_string_append (string, ".");

  execute_syntax (create_syntax_string_source (string->str));

  g_string_free (string, TRUE);
}


/* Sort the data by the the variable which the editor has currently
   selected */
void
psppire_data_editor_sort_ascending  (PsppireDataEditor *de)
{
  GtkSheetRange range;
  gtk_sheet_get_selected_range (GTK_SHEET(de->data_sheet[0]), &range);

  do_sort (de->data_store,  range.col0, FALSE);
}


/* Sort the data by the the variable which the editor has currently
   selected */
void
psppire_data_editor_sort_descending (PsppireDataEditor *de)
{
  GtkSheetRange range;
  gtk_sheet_get_selected_range (GTK_SHEET(de->data_sheet[0]), &range);

  do_sort (de->data_store,  range.col0, TRUE);
}





/* Insert a new variable  before the currently selected position */
void
psppire_data_editor_insert_variable (PsppireDataEditor *de)
{
  glong posn = -1;

  if ( de->data_sheet[0]->state == GTK_SHEET_COLUMN_SELECTED )
    posn = GTK_SHEET (de->data_sheet[0])->range.col0;
  else
    posn = GTK_SHEET (de->data_sheet[0])->active_cell.col;

  if ( posn == -1 ) posn = 0;

  psppire_dict_insert_variable (de->data_store->dict, posn, NULL);
}

/* Insert a new case before the currently selected position */
void
psppire_data_editor_insert_case (PsppireDataEditor *de)
{
  glong posn = -1;

  if ( de->data_sheet[0]->state == GTK_SHEET_ROW_SELECTED )
    posn = GTK_SHEET (de->data_sheet[0])->range.row0;
  else
    posn = GTK_SHEET (de->data_sheet[0])->active_cell.row;

  if ( posn == -1 ) posn = 0;

  psppire_data_store_insert_new_case (de->data_store, posn);
}

/* Delete the cases currently selected in the data sheet */
void
psppire_data_editor_delete_cases    (PsppireDataEditor *de)
{
  gint first = GTK_SHEET (de->data_sheet[0])->range.row0;
  gint n = GTK_SHEET (de->data_sheet[0])->range.rowi - first + 1;

  psppire_data_store_delete_cases (de->data_store, first, n);

  gtk_sheet_unselect_range (GTK_SHEET (de->data_sheet[0]));
}

/* Delete the variables currently selected in the
   datasheet or variable sheet */
void
psppire_data_editor_delete_variables (PsppireDataEditor *de)
{
  gint first, n;

  switch (gtk_notebook_get_current_page (GTK_NOTEBOOK (de)))
    {
    case PSPPIRE_DATA_EDITOR_DATA_VIEW:
      first = GTK_SHEET (de->data_sheet[0])->range.col0;
      n = GTK_SHEET (de->data_sheet[0])->range.coli - first + 1;
      break;
    case PSPPIRE_DATA_EDITOR_VARIABLE_VIEW:
      first = GTK_SHEET (de->var_sheet)->range.row0;
      n = GTK_SHEET (de->var_sheet)->range.rowi - first + 1;
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  psppire_dict_delete_variables (de->var_store->dict, first, n);

  gtk_sheet_unselect_range (GTK_SHEET (de->data_sheet[0]));
  gtk_sheet_unselect_range (GTK_SHEET (de->var_sheet));
}


void
psppire_data_editor_show_grid (PsppireDataEditor *de, gboolean grid_visible)
{
  gtk_sheet_show_grid (GTK_SHEET (de->var_sheet), grid_visible);
  gtk_sheet_show_grid (GTK_SHEET (de->data_sheet[0]), grid_visible);
}

void
psppire_data_editor_set_font (PsppireDataEditor *de, PangoFontDescription *font_desc)
{
  psppire_data_store_set_font (de->data_store, font_desc);
  psppire_var_store_set_font (de->var_store, font_desc);
}





static void
emit_selected_signal (PsppireDataEditor *de)
{
  gboolean data_selected = data_is_selected (de);

  g_signal_emit (de, data_editor_signals[DATA_SELECTION_CHANGED], 0, data_selected);
}


static void
on_activate (PsppireDataEditor *de)
{
  gint row, col;
  gtk_sheet_get_active_cell (GTK_SHEET (de->data_sheet[0]), &row, &col);


  if ( row < psppire_data_store_get_case_count (de->data_store)
       &&
       col < psppire_var_store_get_var_cnt (de->var_store))
    {
      emit_selected_signal (de);
      return ;
    }

  emit_selected_signal (de);
}


static void
on_select_range (PsppireDataEditor *de)
{
  GtkSheetRange range;

  gtk_sheet_get_selected_range (GTK_SHEET (de->data_sheet[0]), &range);

  if ( range.rowi < psppire_data_store_get_case_count (de->data_store)
       &&
       range.coli < psppire_var_store_get_var_cnt (de->var_store))
    {
      emit_selected_signal (de);
      return;
    }

  emit_selected_signal (de);
}


static gboolean
on_switch_page (PsppireDataEditor *de, GtkNotebookPage *p,
		gint pagenum, gpointer data)
{
  if ( pagenum != PSPPIRE_DATA_EDITOR_DATA_VIEW )
    {
      emit_selected_signal (de);
      return TRUE;
    }

  on_select_range (de);

  return TRUE;
}



static gboolean
data_is_selected (PsppireDataEditor *de)
{
  GtkSheetRange range;
  gint row, col;

  if ( gtk_notebook_get_current_page (GTK_NOTEBOOK (de)) != PSPPIRE_DATA_EDITOR_DATA_VIEW)
    return FALSE;

  gtk_sheet_get_active_cell (GTK_SHEET (de->data_sheet[0]), &row, &col);

  if ( row >= psppire_data_store_get_case_count (de->data_store)
       ||
       col >= psppire_var_store_get_var_cnt (de->var_store))
    {
      return FALSE;
    }

  gtk_sheet_get_selected_range (GTK_SHEET (de->data_sheet[0]), &range);

  if ( range.rowi >= psppire_data_store_get_case_count (de->data_store)
       ||
       range.coli >= psppire_var_store_get_var_cnt (de->var_store))
    {
      return FALSE;
    }

  return TRUE;
}


static void
on_select_row (GtkSheet *sheet, gint row, PsppireDataEditor *de)
{
  g_signal_emit (de, data_editor_signals[CASES_SELECTED], 0, row);
}


static void
on_select_variable (GtkSheet *sheet, gint var, PsppireDataEditor *de)
{
  g_signal_emit (de, data_editor_signals[VARIABLES_SELECTED], 0, var);
}




/* Clipboard stuff */


#include <data/casereader.h>
#include <data/case-map.h>
#include <data/casewriter.h>

#include <data/data-out.h>
#include "xalloc.h"

/* A casereader and dictionary holding the data currently in the clip */
static struct casereader *clip_datasheet = NULL;
static struct dictionary *clip_dict = NULL;


static void data_sheet_update_clipboard (GtkSheet *);

/* Set the clip according to the currently
   selected range in the data sheet */
static void
data_sheet_set_clip (GtkSheet *sheet)
{
  int i;
  struct casewriter *writer ;
  GtkSheetRange range;
  PsppireDataStore *ds;
  struct case_map *map = NULL;
  casenumber max_rows;
  size_t max_columns;

  ds = PSPPIRE_DATA_STORE (gtk_sheet_get_model (sheet));

  gtk_sheet_get_selected_range (sheet, &range);

   /* If nothing selected, then use active cell */
  if ( range.row0 < 0 || range.col0 < 0 )
    {
      gint row, col;
      gtk_sheet_get_active_cell (sheet, &row, &col);

      range.row0 = range.rowi = row;
      range.col0 = range.coli = col;
    }

  /* The sheet range can include cells that do not include data.
     Exclude them from the range. */
  max_rows = psppire_data_store_get_case_count (ds);
  if (range.rowi >= max_rows)
    {
      if (max_rows == 0)
        return;
      range.rowi = max_rows - 1;
    }
  max_columns = dict_get_var_cnt (ds->dict->dict);
  if (range.coli >= max_columns)
    {
      if (max_columns == 0)
        return;
      range.coli = max_columns - 1;
    }

  g_return_if_fail (range.rowi >= range.row0);
  g_return_if_fail (range.row0 >= 0);
  g_return_if_fail (range.coli >= range.col0);
  g_return_if_fail (range.col0 >= 0);

  /* Destroy any existing clip */
  if ( clip_datasheet )
    {
      casereader_destroy (clip_datasheet);
      clip_datasheet = NULL;
    }

  if ( clip_dict )
    {
      dict_destroy (clip_dict);
      clip_dict = NULL;
    }

  /* Construct clip dictionary. */
  clip_dict = dict_create ();
  for (i = range.col0; i <= range.coli; i++)
    {
      const struct variable *old = dict_get_var (ds->dict->dict, i);
      dict_clone_var_assert (clip_dict, old, var_get_name (old));
    }

  /* Construct clip data. */
  map = case_map_by_name (ds->dict->dict, clip_dict);
  writer = autopaging_writer_create (dict_get_next_value_idx (clip_dict));
  for (i = range.row0; i <= range.rowi ; ++i )
    {
      struct ccase old;

      if (psppire_case_file_get_case (ds->case_file, i, &old))
        {
          struct ccase new;

          case_map_execute (map, &old, &new);
          case_destroy (&old);
          casewriter_write (writer, &new);
        }
      else
        casewriter_force_error (writer);
    }
  case_map_destroy (map);

  clip_datasheet = casewriter_make_reader (writer);

  data_sheet_update_clipboard (sheet);
}

enum {
  SELECT_FMT_NULL,
  SELECT_FMT_TEXT,
  SELECT_FMT_HTML
};


/* Perform data_out for case CC, variable V, appending to STRING */
static void
data_out_g_string (GString *string, const struct variable *v,
		   const struct ccase *cc)
{
  char *buf ;

  const struct fmt_spec *fs = var_get_print_format (v);
  const union value *val = case_data (cc, v);
  buf = xzalloc (fs->w);

  data_out (val, fs, buf);

  g_string_append_len (string, buf, fs->w);

  g_free (buf);
}

static GString *
clip_to_text (void)
{
  casenumber r;
  GString *string;

  const size_t val_cnt = casereader_get_value_cnt (clip_datasheet);
  const casenumber case_cnt = casereader_get_case_cnt (clip_datasheet);
  const size_t var_cnt = dict_get_var_cnt (clip_dict);

  string = g_string_sized_new (10 * val_cnt * case_cnt);

  for (r = 0 ; r < case_cnt ; ++r )
    {
      int c;
      struct ccase cc;
      if ( !  casereader_peek (clip_datasheet, r, &cc))
	{
	  g_warning ("Clipboard seems to have inexplicably shrunk");
	  break;
	}

      for (c = 0 ; c < var_cnt ; ++c)
	{
	  const struct variable *v = dict_get_var (clip_dict, c);
	  data_out_g_string (string, v, &cc);
	  if ( c < val_cnt - 1 )
	    g_string_append (string, "\t");
	}

      if ( r < case_cnt)
	g_string_append (string, "\n");

      case_destroy (&cc);
    }

  return string;
}


static GString *
clip_to_html (void)
{
  casenumber r;
  GString *string;

  const size_t val_cnt = casereader_get_value_cnt (clip_datasheet);
  const casenumber case_cnt = casereader_get_case_cnt (clip_datasheet);
  const size_t var_cnt = dict_get_var_cnt (clip_dict);


  /* Guestimate the size needed */
  string = g_string_sized_new (20 * val_cnt * case_cnt);

  g_string_append (string, "<table>\n");
  for (r = 0 ; r < case_cnt ; ++r )
    {
      int c;
      struct ccase cc;
      if ( !  casereader_peek (clip_datasheet, r, &cc))
	{
	  g_warning ("Clipboard seems to have inexplicably shrunk");
	  break;
	}
      g_string_append (string, "<tr>\n");

      for (c = 0 ; c < var_cnt ; ++c)
	{
	  const struct variable *v = dict_get_var (clip_dict, c);
	  g_string_append (string, "<td>");
	  data_out_g_string (string, v, &cc);
	  g_string_append (string, "</td>\n");
	}

      g_string_append (string, "</tr>\n");

      case_destroy (&cc);
    }
  g_string_append (string, "</table>\n");

  return string;
}



static void
clipboard_get_cb (GtkClipboard     *clipboard,
		  GtkSelectionData *selection_data,
		  guint             info,
		  gpointer          data)
{
  GString *string = NULL;

  switch (info)
    {
    case SELECT_FMT_TEXT:
      string = clip_to_text ();
      break;
    case SELECT_FMT_HTML:
      string = clip_to_html ();
      break;
    default:
      g_assert_not_reached ();
    }

  gtk_selection_data_set (selection_data, selection_data->target,
			  8,
			  (const guchar *) string->str, string->len);

  g_string_free (string, TRUE);
}

static void
clipboard_clear_cb (GtkClipboard *clipboard,
		    gpointer data)
{
  dict_destroy (clip_dict);
  clip_dict = NULL;

  casereader_destroy (clip_datasheet);
  clip_datasheet = NULL;
}


static const GtkTargetEntry targets[] = {
  { "UTF8_STRING",   0, SELECT_FMT_TEXT },
  { "STRING",        0, SELECT_FMT_TEXT },
  { "TEXT",          0, SELECT_FMT_TEXT },
  { "COMPOUND_TEXT", 0, SELECT_FMT_TEXT },
  { "text/plain;charset=utf-8", 0, SELECT_FMT_TEXT },
  { "text/plain",    0, SELECT_FMT_TEXT },
  { "text/html",     0, SELECT_FMT_HTML }
};



static void
data_sheet_update_clipboard (GtkSheet *sheet)
{
  GtkClipboard *clipboard =
    gtk_widget_get_clipboard (GTK_WIDGET (sheet),
			      GDK_SELECTION_CLIPBOARD);

  if (!gtk_clipboard_set_with_owner (clipboard, targets,
				     G_N_ELEMENTS (targets),
				     clipboard_get_cb, clipboard_clear_cb,
				     G_OBJECT (sheet)))
    clipboard_clear_cb (clipboard, sheet);
}



/* A callback for when the clipboard contents have been received */
static void
data_sheet_contents_received_callback (GtkClipboard *clipboard,
				      GtkSelectionData *sd,
				      gpointer data)
{
  gint count = 0;
  gint row, column;
  gint next_row, next_column;
  gint first_column;
  char *c;
  PsppireDataEditor *data_editor = data;

  if ( sd->length < 0 )
    return;

  if ( sd->type != gdk_atom_intern ("UTF8_STRING", FALSE))
    return;

  c = (char *) sd->data;

  /* Paste text to selected position */
  gtk_sheet_get_active_cell (GTK_SHEET (data_editor->data_sheet[0]),
			     &row, &column);

  g_return_if_fail (row >= 0);
  g_return_if_fail (column >= 0);

  first_column = column;
  next_row = row;
  next_column = column;
  while (count < sd->length)
    {
      char *s = c;

      row = next_row;
      column = next_column;
      while (*c != '\t' && *c != '\n' && count < sd->length)
	{
	  c++;
	  count++;
	}
      if ( *c == '\t')
	{
	  next_row = row ;
	  next_column = column + 1;
	}
      else if ( *c == '\n')
	{
	  next_row = row + 1;
	  next_column = first_column;
	}
      *c++ = '\0';
      count++;


      /* Append some new cases if pasting beyond the last row */
      if ( row >= psppire_data_store_get_case_count (data_editor->data_store))
	psppire_data_store_insert_new_case (data_editor->data_store, row);

      psppire_data_store_set_string (data_editor->data_store, s, row, column);
    }
}


static void
on_owner_change (GtkClipboard *clip, GdkEventOwnerChange *event, gpointer data)
{
  gint i;
  gboolean compatible_target = FALSE;
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (data);

  for (i = 0 ; i < sizeof (targets) / sizeof(targets[0]) ; ++i )
    {
      GdkAtom atom = gdk_atom_intern (targets[i].target, TRUE);
      if ( gtk_clipboard_wait_is_target_available (clip, atom))
	{
	  compatible_target = TRUE;
	  break;
	}
    }

  g_signal_emit (de, data_editor_signals[DATA_AVAILABLE_CHANGED], 0,
		 compatible_target);
}


