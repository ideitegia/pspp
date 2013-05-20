/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "ui/gui/psppire-data-editor.h"

#include <gtk/gtk.h>
#include <gtk-contrib/gtkxpaned.h>

#include "data/datasheet.h"
#include "data/value-labels.h"
#include "libpspp/range-set.h"
#include "libpspp/str.h"
#include "ui/gui/helper.h"
#include "ui/gui/pspp-sheet-selection.h"
#include "ui/gui/psppire-data-sheet.h"
#include "ui/gui/psppire-data-store.h"
#include "ui/gui/psppire-value-entry.h"
#include "ui/gui/psppire-var-sheet.h"
#include "ui/gui/psppire.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)

#define FOR_EACH_DATA_SHEET(DATA_SHEET, IDX, DATA_EDITOR)       \
  for ((IDX) = 0;                                               \
       (IDX) < 4                                                \
         && ((DATA_SHEET) = PSPPIRE_DATA_SHEET (                \
               (DATA_EDITOR)->data_sheets[IDX])) != NULL;       \
       (IDX)++)

static void psppire_data_editor_class_init          (PsppireDataEditorClass *klass);
static void psppire_data_editor_init                (PsppireDataEditor      *de);

static void disconnect_data_sheets (PsppireDataEditor *);
static void refresh_entry (PsppireDataEditor *);
static void psppire_data_editor_update_ui_manager (PsppireDataEditor *);

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

  disconnect_data_sheets (de);

  if (de->data_store)
    {
      g_object_unref (de->data_store);
      de->data_store = NULL;
    }

  if (de->dict)
    {
      g_object_unref (de->dict);
      de->dict = NULL;
    }

  if (de->font != NULL)
    {
      pango_font_description_free (de->font);
      de->font = NULL;
    }

  if (de->ui_manager)
    {
      g_object_unref (de->ui_manager);
      de->ui_manager = NULL;
    }

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

enum
  {
    PROP_0,
    PROP_DATA_STORE,
    PROP_DICTIONARY,
    PROP_VALUE_LABELS,
    PROP_SPLIT_WINDOW,
    PROP_UI_MANAGER
  };

static void
psppire_data_editor_refresh_model (PsppireDataEditor *de)
{
  PsppireVarSheet *var_sheet = PSPPIRE_VAR_SHEET (de->var_sheet);
  PsppireDataSheet *data_sheet;
  int i;

  FOR_EACH_DATA_SHEET (data_sheet, i, de)
    psppire_data_sheet_set_data_store (data_sheet, de->data_store);
  psppire_var_sheet_set_dictionary (var_sheet, de->dict);
}

static void
psppire_data_editor_set_property (GObject         *object,
				  guint            prop_id,
				  const GValue    *value,
				  GParamSpec      *pspec)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (object);
  PsppireDataSheet *data_sheet;
  int i;

  switch (prop_id)
    {
    case PROP_SPLIT_WINDOW:
      psppire_data_editor_split_window (de, g_value_get_boolean (value));
      break;
    case PROP_DATA_STORE:
      if ( de->data_store)
        {
          g_signal_handlers_disconnect_by_func (de->data_store,
                                                G_CALLBACK (refresh_entry),
                                                de);
          g_object_unref (de->data_store);
        }

      de->data_store = g_value_get_pointer (value);
      g_object_ref (de->data_store);
      psppire_data_editor_refresh_model (de);

      g_signal_connect_swapped (de->data_store, "case-changed",
                                G_CALLBACK (refresh_entry), de);

      break;
    case PROP_DICTIONARY:
      if (de->dict)
        g_object_unref (de->dict);
      de->dict = g_value_get_pointer (value);
      g_object_ref (de->dict);

      psppire_var_sheet_set_dictionary (PSPPIRE_VAR_SHEET (de->var_sheet),
                                        de->dict);
      break;
    case PROP_VALUE_LABELS:
      FOR_EACH_DATA_SHEET (data_sheet, i, de)
        psppire_data_sheet_set_value_labels (data_sheet,
                                          g_value_get_boolean (value));
      break;
    case PROP_UI_MANAGER:
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
    case PROP_DICTIONARY:
      g_value_set_pointer (value, de->dict);
      break;
    case PROP_VALUE_LABELS:
      g_value_set_boolean (value,
                           psppire_data_sheet_get_value_labels (
                             PSPPIRE_DATA_SHEET (de->data_sheets[0])));
      break;
    case PROP_UI_MANAGER:
      g_value_set_object (value, psppire_data_editor_get_ui_manager (de));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_data_editor_switch_page (GtkNotebook     *notebook,
#if GTK_DISABLE_DEPRECATED && GTK_CHECK_VERSION(2,20,0)
                                 gpointer page,
#else
                                 GtkNotebookPage *page,
#endif
                                 guint            page_num)
{
  GTK_NOTEBOOK_CLASS (parent_class)->switch_page (notebook, page, page_num);
  psppire_data_editor_update_ui_manager (PSPPIRE_DATA_EDITOR (notebook));
}

static void
psppire_data_editor_set_focus_child (GtkContainer *container,
                                     GtkWidget    *widget)
{
  GTK_CONTAINER_CLASS (parent_class)->set_focus_child (container, widget);
  psppire_data_editor_update_ui_manager (PSPPIRE_DATA_EDITOR (container));
}

static void
psppire_data_editor_class_init (PsppireDataEditorClass *klass)
{
  GParamSpec *data_store_spec ;
  GParamSpec *dict_spec ;
  GParamSpec *value_labels_spec;
  GParamSpec *split_window_spec;
  GParamSpec *ui_manager_spec;
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);
  GtkNotebookClass *notebook_class = GTK_NOTEBOOK_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->dispose = psppire_data_editor_dispose;
  object_class->set_property = psppire_data_editor_set_property;
  object_class->get_property = psppire_data_editor_get_property;

  container_class->set_focus_child = psppire_data_editor_set_focus_child;

  notebook_class->switch_page = psppire_data_editor_switch_page;

  data_store_spec =
    g_param_spec_pointer ("data-store",
			  "Data Store",
			  "A pointer to the data store associated with this editor",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_READABLE );

  g_object_class_install_property (object_class,
                                   PROP_DATA_STORE,
                                   data_store_spec);

  dict_spec =
    g_param_spec_pointer ("dictionary",
			  "Dictionary",
			  "A pointer to the dictionary associated with this editor",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_READABLE );

  g_object_class_install_property (object_class,
                                   PROP_DICTIONARY,
                                   dict_spec);

  value_labels_spec =
    g_param_spec_boolean ("value-labels",
			 "Value Labels",
			 "Whether or not the data sheet should display labels instead of values",
			  FALSE,
			 G_PARAM_WRITABLE | G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_VALUE_LABELS,
                                   value_labels_spec);


  split_window_spec =
    g_param_spec_boolean ("split",
			  "Split Window",
			  "True iff the data sheet is split",
			  FALSE,
			  G_PARAM_READABLE | G_PARAM_WRITABLE);

  g_object_class_install_property (object_class,
                                   PROP_SPLIT_WINDOW,
                                   split_window_spec);

  ui_manager_spec =
    g_param_spec_object ("ui-manager",
                         "UI Manager",
                         "UI manager for the active notebook tab.  The client should merge this UI manager with the active UI manager to obtain menu items and tool bar items specific to the active notebook tab.",
                         GTK_TYPE_UI_MANAGER,
                         G_PARAM_READABLE);
  g_object_class_install_property (object_class,
                                   PROP_UI_MANAGER,
                                   ui_manager_spec);
}

static gboolean
on_data_sheet_var_double_clicked (PsppireDataSheet *data_sheet,
                                  gint dict_index,
                                  PsppireDataEditor *de)
{
  gtk_notebook_set_current_page (GTK_NOTEBOOK (de),
                                 PSPPIRE_DATA_EDITOR_VARIABLE_VIEW);

  psppire_var_sheet_goto_variable (PSPPIRE_VAR_SHEET (de->var_sheet),
                                   dict_index);

  return TRUE;
}

static gboolean
on_var_sheet_var_double_clicked (PsppireVarSheet *var_sheet, gint dict_index,
                                 PsppireDataEditor *de)
{
  PsppireDataSheet *data_sheet;

  gtk_notebook_set_current_page (GTK_NOTEBOOK (de),
                                 PSPPIRE_DATA_EDITOR_DATA_VIEW);

  data_sheet = psppire_data_editor_get_active_data_sheet (de);
  psppire_data_sheet_goto_variable (data_sheet, dict_index);

  return TRUE;
}

/* Refreshes 'de->cell_ref_label' and 'de->datum_entry' from the currently
   active cell or cells. */
static void
refresh_entry (PsppireDataEditor *de)
{
  PsppireDataSheet *data_sheet = psppire_data_editor_get_active_data_sheet (de);
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppSheetSelection *selection = pspp_sheet_view_get_selection (sheet_view);

  gchar *ref_cell_text;
  GList *selected_columns, *iter;
  struct variable *var;
  gint n_cases;
  gint n_vars;

  selected_columns = pspp_sheet_selection_get_selected_columns (selection);
  n_vars = 0;
  var = NULL;
  for (iter = selected_columns; iter != NULL; iter = iter->next)
    {
      PsppSheetViewColumn *column = iter->data;
      struct variable *v = g_object_get_data (G_OBJECT (column), "variable");
      if (v != NULL)
        {
          var = v;
          n_vars++;
        }
    }
  g_list_free (selected_columns);

  n_cases = pspp_sheet_selection_count_selected_rows (selection);
  if (n_cases > 0)
    {
      /* The final row is selectable but it isn't a case (it's just used to add
         more cases), so don't count it. */
      GtkTreePath *path;
      gint case_count;

      case_count = psppire_data_store_get_case_count (de->data_store);
      path = gtk_tree_path_new_from_indices (case_count, -1);
      if (pspp_sheet_selection_path_is_selected (selection, path))
        n_cases--;
      gtk_tree_path_free (path);
    }

  ref_cell_text = NULL;
  if (n_cases == 1 && n_vars == 1)
    {
      PsppireValueEntry *value_entry = PSPPIRE_VALUE_ENTRY (de->datum_entry);
      struct range_set *selected_rows;
      gboolean show_value_labels;
      union value value;
      int width;
      gint row;

      selected_rows = pspp_sheet_selection_get_range_set (selection);
      row = range_set_scan (selected_rows, 0);
      range_set_destroy (selected_rows);

      ref_cell_text = g_strdup_printf ("%d : %s", row + 1, var_get_name (var));

      show_value_labels = psppire_data_sheet_get_value_labels (data_sheet);

      psppire_value_entry_set_variable (value_entry, var);
      psppire_value_entry_set_show_value_label (value_entry,
                                                show_value_labels);

      width = var_get_width (var);
      value_init (&value, width);
      datasheet_get_value (de->data_store->datasheet,
                           row, var_get_case_index (var), &value);
      psppire_value_entry_set_value (value_entry, &value, width);
      value_destroy (&value, width);

      gtk_widget_set_sensitive (de->datum_entry, TRUE);
    }
  else
    {
      if (n_cases == 0 || n_vars == 0)
        {
          ref_cell_text = NULL;
        }
      else
        {
          struct string s;

          /* The glib string library does not understand the ' printf modifier
             on all platforms, but the "struct string" library does (because
             Gnulib fixes that problem), so use the latter.  */
          ds_init_empty (&s);
          ds_put_format (&s, ngettext ("%'d case", "%'d cases", n_cases),
                         n_cases);
          ds_put_byte (&s, ' ');
          ds_put_unichar (&s, 0xd7); /* U+00D7 MULTIPLICATION SIGN */
          ds_put_byte (&s, ' ');
          ds_put_format (&s, ngettext ("%'d variable", "%'d variables",
                                       n_vars),
                         n_vars);
          ref_cell_text = ds_steal_cstr (&s);
        }

      psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (de->datum_entry),
                                        NULL);
      gtk_entry_set_text (
        GTK_ENTRY (gtk_bin_get_child (GTK_BIN (de->datum_entry))), "");
      gtk_widget_set_sensitive (de->datum_entry, FALSE);
    }

  gtk_label_set_label (GTK_LABEL (de->cell_ref_label),
                       ref_cell_text ? ref_cell_text : "");
  g_free (ref_cell_text);
}

static void
on_datum_entry_activate (PsppireValueEntry *entry, PsppireDataEditor *de)
{
  PsppireDataSheet *data_sheet = psppire_data_editor_get_active_data_sheet (de);
  struct variable *var;
  union value value;
  int width;
  gint row;

  row = psppire_data_sheet_get_current_case (data_sheet);
  var = psppire_data_sheet_get_current_variable (data_sheet);
  if (row < 0 || !var)
    return;

  width = var_get_width (var);
  value_init (&value, width);
  if (psppire_value_entry_get_value (PSPPIRE_VALUE_ENTRY (de->datum_entry),
                                     &value, width))
    psppire_data_store_set_value (de->data_store, row, var, &value);
  value_destroy (&value, width);
}

static void
on_data_sheet_selection_changed (PsppSheetSelection *selection,
                                 PsppireDataEditor *de)
{
  /* In a split view, ensure that only a single data sheet has a nonempty
     selection.  */
  if (de->split
      && pspp_sheet_selection_count_selected_rows (selection)
      && pspp_sheet_selection_count_selected_columns (selection))
    {
      PsppireDataSheet *ds;
      int i;

      FOR_EACH_DATA_SHEET (ds, i, de)
        {
          PsppSheetSelection *s;

          s = pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (ds));
          if (s != selection)
            pspp_sheet_selection_unselect_all (s);
        }
    }

  refresh_entry (de);
}

/* Ensures that rows in the right-hand panes in the split view have the same
   row height as the left-hand panes.  Otherwise, the rows in the right-hand
   pane tend to be smaller, because the right-hand pane doesn't have buttons
   for case numbers. */
static void
on_data_sheet_fixed_height_notify (PsppireDataSheet *ds,
                                   GParamSpec *pspec,
                                   PsppireDataEditor *de)
{
  enum
    {
      TL = GTK_XPANED_TOP_LEFT,
      TR = GTK_XPANED_TOP_RIGHT,
      BL = GTK_XPANED_BOTTOM_LEFT,
      BR = GTK_XPANED_BOTTOM_RIGHT
    };

  int fixed_height = pspp_sheet_view_get_fixed_height (PSPP_SHEET_VIEW (ds));

  pspp_sheet_view_set_fixed_height (PSPP_SHEET_VIEW (de->data_sheets[TR]),
                                    fixed_height);
  pspp_sheet_view_set_fixed_height (PSPP_SHEET_VIEW (de->data_sheets[BR]),
                                    fixed_height);
}

static void
disconnect_data_sheets (PsppireDataEditor *de)
{
  PsppireDataSheet *ds;
  int i;

  FOR_EACH_DATA_SHEET (ds, i, de)
    {
      PsppSheetSelection *selection;

      if (ds == NULL)
        {
          /* This can only happen if 'dispose' runs more than once. */
          continue;
        }

      if (i == GTK_XPANED_TOP_LEFT)
        g_signal_handlers_disconnect_by_func (
          ds, G_CALLBACK (on_data_sheet_fixed_height_notify), de);

      g_signal_handlers_disconnect_by_func (
        ds, G_CALLBACK (refresh_entry), de);
      g_signal_handlers_disconnect_by_func (
        ds, G_CALLBACK (on_data_sheet_var_double_clicked), de);

      selection = pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (ds));
      g_signal_handlers_disconnect_by_func (
        selection, G_CALLBACK (on_data_sheet_selection_changed), de);

      de->data_sheets[i] = NULL;
    }
}

static GtkWidget *
make_data_sheet (PsppireDataEditor *de, GtkTreeViewGridLines grid_lines,
                 gboolean show_value_labels)
{
  PsppSheetSelection *selection;
  GtkWidget *ds;

  ds = psppire_data_sheet_new ();
  pspp_sheet_view_set_grid_lines (PSPP_SHEET_VIEW (ds), grid_lines);
  psppire_data_sheet_set_value_labels (PSPPIRE_DATA_SHEET (ds),
                                       show_value_labels);

  g_signal_connect_swapped (ds, "notify::value-labels",
                            G_CALLBACK (refresh_entry), de);
  g_signal_connect (ds, "var-double-clicked",
                    G_CALLBACK (on_data_sheet_var_double_clicked), de);

  selection = pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (ds));
  g_signal_connect (selection, "changed",
                    G_CALLBACK (on_data_sheet_selection_changed), de);

  return ds;
}

static GtkWidget *
make_single_datasheet (PsppireDataEditor *de, GtkTreeViewGridLines grid_lines,
                       gboolean show_value_labels)
{
  GtkWidget *data_sheet_scroller;

  de->data_sheets[0] = make_data_sheet (de, grid_lines, show_value_labels);
  de->data_sheets[1] = de->data_sheets[2] = de->data_sheets[3] = NULL;

  /* Put data sheet in scroller. */
  data_sheet_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (data_sheet_scroller),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (data_sheet_scroller), de->data_sheets[0]);

  return data_sheet_scroller;
}

static GtkWidget *
make_split_datasheet (PsppireDataEditor *de, GtkTreeViewGridLines grid_lines,
                      gboolean show_value_labels)
{
  /* Panes, in the order in which we want to create them. */
  enum
    {
      TL,                       /* top left */
      TR,                       /* top right */
      BL,                       /* bottom left */
      BR                        /* bottom right */
    };

  PsppSheetView *ds[4];
  GtkXPaned *xpaned;
  int i;

  xpaned = GTK_XPANED (gtk_xpaned_new ());

  for (i = 0; i < 4; i++)
    {
      GtkAdjustment *hadjust, *vadjust;
      GtkPolicyType hpolicy, vpolicy;
      GtkWidget *scroller;

      de->data_sheets[i] = make_data_sheet (de, grid_lines, show_value_labels);
      ds[i] = PSPP_SHEET_VIEW (de->data_sheets[i]);

      if (i == BL)
        hadjust = pspp_sheet_view_get_hadjustment (ds[TL]);
      else if (i == BR)
        hadjust = pspp_sheet_view_get_hadjustment (ds[TR]);
      else
        hadjust = NULL;

      if (i == TR)
        vadjust = pspp_sheet_view_get_vadjustment (ds[TL]);
      else if (i == BR)
        vadjust = pspp_sheet_view_get_vadjustment (ds[BL]);
      else
        vadjust = NULL;

      scroller = gtk_scrolled_window_new (hadjust, vadjust);
      gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroller),
                                           GTK_SHADOW_ETCHED_IN);
      hpolicy = i == TL || i == TR ? GTK_POLICY_NEVER : GTK_POLICY_ALWAYS;
      vpolicy = i == TL || i == BL ? GTK_POLICY_NEVER : GTK_POLICY_ALWAYS;
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
                                      hpolicy, vpolicy);
      gtk_container_add (GTK_CONTAINER (scroller), GTK_WIDGET (ds[i]));

      switch (i)
        {
        case TL:
          gtk_xpaned_pack_top_left (xpaned, scroller, TRUE, TRUE);
          break;

        case TR:
          gtk_xpaned_pack_top_right (xpaned, scroller, TRUE, TRUE);
          break;

        case BL:
          gtk_xpaned_pack_bottom_left (xpaned, scroller, TRUE, TRUE);
          break;

        case BR:
          gtk_xpaned_pack_bottom_right (xpaned, scroller, TRUE, TRUE);
          break;

        default:
          g_warn_if_reached ();
        }
    }

  /* Bottom sheets don't display variable names. */
  pspp_sheet_view_set_headers_visible (ds[BL], FALSE);
  pspp_sheet_view_set_headers_visible (ds[BR], FALSE);

  /* Right sheets don't display case numbers. */
  psppire_data_sheet_set_case_numbers (PSPPIRE_DATA_SHEET (ds[TR]), FALSE);
  psppire_data_sheet_set_case_numbers (PSPPIRE_DATA_SHEET (ds[BR]), FALSE);

  g_signal_connect (ds[TL], "notify::fixed-height",
                    G_CALLBACK (on_data_sheet_fixed_height_notify), de);

  return GTK_WIDGET (xpaned);
}

static void
psppire_data_editor_init (PsppireDataEditor *de)
{
  GtkWidget *var_sheet_scroller;
  GtkWidget *hbox;

  de->font = NULL;
  de->ui_manager = NULL;
  de->old_vbox_widget = NULL;

  g_object_set (de, "tab-pos", GTK_POS_BOTTOM, NULL);

  de->cell_ref_label = gtk_label_new ("");
  gtk_label_set_width_chars (GTK_LABEL (de->cell_ref_label), 25);
  gtk_misc_set_alignment (GTK_MISC (de->cell_ref_label), 0.0, 0.5);

  de->datum_entry = psppire_value_entry_new ();
  g_signal_connect (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (de->datum_entry))),
                    "activate", G_CALLBACK (on_datum_entry_activate), de);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), de->cell_ref_label, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), de->datum_entry, TRUE, TRUE, 0);

  de->split = FALSE;
  de->datasheet_vbox_widget
    = make_single_datasheet (de, GTK_TREE_VIEW_GRID_LINES_BOTH, FALSE);

  de->vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (de->vbox), hbox, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (de->vbox), de->datasheet_vbox_widget,
                      TRUE, TRUE, 0);

  gtk_notebook_append_page (GTK_NOTEBOOK (de), de->vbox,
			    gtk_label_new_with_mnemonic (_("Data View")));

  gtk_widget_show_all (de->vbox);

  de->var_sheet = GTK_WIDGET (psppire_var_sheet_new ());
  pspp_sheet_view_set_grid_lines (PSPP_SHEET_VIEW (de->var_sheet),
                                  GTK_TREE_VIEW_GRID_LINES_BOTH);
  var_sheet_scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (var_sheet_scroller),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (var_sheet_scroller), de->var_sheet);
  gtk_widget_show_all (var_sheet_scroller);
  gtk_notebook_append_page (GTK_NOTEBOOK (de), var_sheet_scroller,
			    gtk_label_new_with_mnemonic (_("Variable View")));

  g_signal_connect (de->var_sheet, "var-double-clicked",
                    G_CALLBACK (on_var_sheet_var_double_clicked), de);

  g_object_set (de, "can-focus", FALSE, NULL);

  psppire_data_editor_update_ui_manager (de);
}

GtkWidget*
psppire_data_editor_new (PsppireDict *dict,
			 PsppireDataStore *data_store)
{
  return  g_object_new (PSPPIRE_DATA_EDITOR_TYPE,
                        "dictionary",  dict,
                        "data-store",  data_store,
                        NULL);
}

/* Turns the visible grid on or off, according to GRID_VISIBLE, for DE's data
   sheet(s) and variable sheet. */
void
psppire_data_editor_show_grid (PsppireDataEditor *de, gboolean grid_visible)
{
  GtkTreeViewGridLines grid;
  PsppireDataSheet *data_sheet;
  int i;

  grid = (grid_visible
          ? GTK_TREE_VIEW_GRID_LINES_BOTH
          : GTK_TREE_VIEW_GRID_LINES_NONE);

  FOR_EACH_DATA_SHEET (data_sheet, i, de)
    pspp_sheet_view_set_grid_lines (PSPP_SHEET_VIEW (data_sheet), grid);
  pspp_sheet_view_set_grid_lines (PSPP_SHEET_VIEW (de->var_sheet), grid);
}


static void
set_font_recursively (GtkWidget *w, gpointer data)
{
  PangoFontDescription *font_desc = data;
  GtkRcStyle *style = gtk_widget_get_modifier_style (w);

  pango_font_description_free (style->font_desc);
  style->font_desc = pango_font_description_copy (font_desc);

  gtk_widget_modify_style (w, style);

  if ( GTK_IS_CONTAINER (w))
    gtk_container_foreach (GTK_CONTAINER (w), set_font_recursively, font_desc);
}

/* Sets FONT_DESC as the font used by the data sheet(s) and variable sheet. */
void
psppire_data_editor_set_font (PsppireDataEditor *de, PangoFontDescription *font_desc)
{
  set_font_recursively (GTK_WIDGET (de), font_desc);

  if (de->font)
    pango_font_description_free (de->font);
  de->font = pango_font_description_copy (font_desc);
}

/* If SPLIT is TRUE, splits DE's data sheet into four panes.
   If SPLIT is FALSE, un-splits it into a single pane. */
void
psppire_data_editor_split_window (PsppireDataEditor *de, gboolean split)
{
  GtkTreeViewGridLines grid_lines;
  gboolean labels;

  if (split == de->split)
    return;


  grid_lines = pspp_sheet_view_get_grid_lines (
    PSPP_SHEET_VIEW (de->data_sheets[0]));
  labels = psppire_data_sheet_get_value_labels (PSPPIRE_DATA_SHEET (
                                                  de->data_sheets[0]));

  disconnect_data_sheets (de);
  if (de->old_vbox_widget)
    g_object_unref (de->old_vbox_widget);
  de->old_vbox_widget = de->datasheet_vbox_widget;
  g_object_ref (de->old_vbox_widget);
  /* FIXME:  old_vbox_widget needs to be unreffed in dispose.
	(currently it seems to provoke an error if I do that.  
	I don't know why. */
  gtk_container_remove (GTK_CONTAINER (de->vbox), de->datasheet_vbox_widget);

  if (split)
    de->datasheet_vbox_widget = make_split_datasheet (de, grid_lines, labels);
  else
    de->datasheet_vbox_widget = make_single_datasheet (de, grid_lines, labels);

  psppire_data_editor_refresh_model (de);

  gtk_box_pack_start (GTK_BOX (de->vbox), de->datasheet_vbox_widget,
                      TRUE, TRUE, 0);
  gtk_widget_show_all (de->vbox);

  if (de->font)
    set_font_recursively (GTK_WIDGET (de), de->font);

  de->split = split;
  g_object_notify (G_OBJECT (de), "split");
  psppire_data_editor_update_ui_manager (de);
}

/* Makes the variable with dictionary index DICT_INDEX in DE's dictionary
   visible and selected in the active view in DE. */
void
psppire_data_editor_goto_variable (PsppireDataEditor *de, gint dict_index)
{
  PsppireDataSheet *data_sheet;

  switch (gtk_notebook_get_current_page (GTK_NOTEBOOK (de)))
    {
    case PSPPIRE_DATA_EDITOR_DATA_VIEW:
      data_sheet = psppire_data_editor_get_active_data_sheet (de);
      psppire_data_sheet_goto_variable (data_sheet, dict_index);
      break;

    case PSPPIRE_DATA_EDITOR_VARIABLE_VIEW:
      psppire_var_sheet_goto_variable (PSPPIRE_VAR_SHEET (de->var_sheet),
                                       dict_index);
      break;
    }
}

/* Returns the "active" data sheet in DE.  If DE is in single-paned mode, this
   is the only data sheet.  If DE is in split mode (showing four data sheets),
   this is the focused data sheet or, if none is focused, the data sheet with
   selected cells or, if none has selected cells, the upper-left data sheet. */
PsppireDataSheet *
psppire_data_editor_get_active_data_sheet (PsppireDataEditor *de)
{
  if (de->split)
    {
      PsppireDataSheet *data_sheet;
      GtkWidget *scroller;
      int i;

      /* If one of the datasheet's scrollers is focused, choose that one. */
      scroller = gtk_container_get_focus_child (
        GTK_CONTAINER (de->datasheet_vbox_widget));
      if (scroller != NULL)
        return PSPPIRE_DATA_SHEET (gtk_bin_get_child (GTK_BIN (scroller)));

      /* Otherwise if there's a nonempty selection in some data sheet, choose
         that one. */
      FOR_EACH_DATA_SHEET (data_sheet, i, de)
        {
          PsppSheetSelection *selection;

          selection = pspp_sheet_view_get_selection (
            PSPP_SHEET_VIEW (data_sheet));
          if (pspp_sheet_selection_count_selected_rows (selection)
              && pspp_sheet_selection_count_selected_columns (selection))
            return data_sheet;
        }
    }

  return PSPPIRE_DATA_SHEET (de->data_sheets[0]);
}

/* Returns the UI manager that should be merged into DE's toplevel widget's UI
   manager to display menu items and toolbar items specific to DE's current
   page and data sheet.

   DE's toplevel widget can watch for changes by connecting to DE's
   notify::ui-manager signal. */
GtkUIManager *
psppire_data_editor_get_ui_manager (PsppireDataEditor *de)
{
  psppire_data_editor_update_ui_manager (de);
  return de->ui_manager;
}

static void
psppire_data_editor_update_ui_manager (PsppireDataEditor *de)
{
  PsppireDataSheet *data_sheet;
  GtkUIManager *ui_manager;

  ui_manager = NULL;

  switch (gtk_notebook_get_current_page (GTK_NOTEBOOK (de)))
    {
    case PSPPIRE_DATA_EDITOR_DATA_VIEW:
      data_sheet = psppire_data_editor_get_active_data_sheet (de);
      if (data_sheet != NULL)
        ui_manager = psppire_data_sheet_get_ui_manager (data_sheet);
      else
        {
          /* This happens transiently in psppire_data_editor_split_window(). */
        }
      break;

    case PSPPIRE_DATA_EDITOR_VARIABLE_VIEW:
      ui_manager = psppire_var_sheet_get_ui_manager (
        PSPPIRE_VAR_SHEET (de->var_sheet));
      break;

    default:
      /* This happens transiently in psppire_data_editor_init(). */
      break;
    }

  if (ui_manager != de->ui_manager)
    {
      if (de->ui_manager)
        g_object_unref (de->ui_manager);
      if (ui_manager)
        g_object_ref (ui_manager);
      de->ui_manager = ui_manager;

      g_object_notify (G_OBJECT (de), "ui-manager");
    }
}
