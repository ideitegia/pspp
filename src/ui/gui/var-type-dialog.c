/* PSPPIRE - a graphical user interface for PSPP.
    Copyright (C) 2005, 2006, 2010, 2011, 2012  Free Software Foundation

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


/*  This module describes the behaviour of the Variable Type dialog box used
    for inputing the variable type in the var sheet */

#include <config.h>

#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#include "data/data-out.h"
#include "data/settings.h"
#include "data/variable.h"
#include "libpspp/message.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/psppire-format.h"
#include "ui/gui/var-type-dialog.h"

static const struct fmt_spec date_format[] =
  {
    {FMT_DATE,  11, 0},
    {FMT_DATE,   9, 0},
    {FMT_ADATE, 10, 0},
    {FMT_ADATE, 8, 0},
    {FMT_EDATE, 10, 0},
    {FMT_EDATE, 8, 0},
    {FMT_SDATE, 10, 0},
    {FMT_SDATE, 8, 0},
    {FMT_JDATE, 5, 0},
    {FMT_JDATE, 7, 0},
    {FMT_QYR, 8, 0},
    {FMT_QYR, 6, 0},
    {FMT_MOYR, 8, 0},
    {FMT_MOYR, 6, 0},
    {FMT_WKYR, 10, 0},
    {FMT_WKYR, 8, 0},
    {FMT_DATETIME, 17, 0},
    {FMT_DATETIME, 20, 0}
  };


static const struct fmt_spec dollar_format[] =
  {
    {FMT_DOLLAR, 2, 0},
    {FMT_DOLLAR, 3, 0},
    {FMT_DOLLAR, 4, 0},
    {FMT_DOLLAR, 7, 2},
    {FMT_DOLLAR, 6, 0},
    {FMT_DOLLAR, 9, 2},
    {FMT_DOLLAR, 8, 0},
    {FMT_DOLLAR, 11, 2},
    {FMT_DOLLAR, 12, 0},
    {FMT_DOLLAR, 15, 2},
    {FMT_DOLLAR, 16, 0},
    {FMT_DOLLAR, 19, 2}
  };

static const int cc_format[] =
  {
    FMT_CCA,
    FMT_CCB,
    FMT_CCC,
    FMT_CCD,
    FMT_CCE,
  };

static GObject *psppire_var_type_dialog_constructor (GType type, guint,
                                                     GObjectConstructParam *);
static void psppire_var_type_dialog_set_state (PsppireVarTypeDialog *);

static void psppire_var_type_dialog_set_format (PsppireVarTypeDialog *dialog,
						const struct fmt_spec *format);

static int find_format (const struct fmt_spec *target,
                        const struct fmt_spec formats[], int n_formats);
static int find_format_type (int target, const int types[], int n_types);

static void select_treeview_at_index (GtkTreeView *, int index);

static void update_width_decimals (const PsppireVarTypeDialog *);
static void refresh_active_button (PsppireVarTypeDialog *);
static void on_active_button_change (GtkToggleButton *,
                                     PsppireVarTypeDialog *);
static void on_width_changed (GtkEntry *, PsppireVarTypeDialog *);
static void on_decimals_changed (GtkEntry *, PsppireVarTypeDialog *);

G_DEFINE_TYPE (PsppireVarTypeDialog,
               psppire_var_type_dialog,
               PSPPIRE_TYPE_DIALOG);

enum
  {
    PROP_0,
    PROP_FORMAT
  };

static void
psppire_var_type_dialog_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  PsppireVarTypeDialog *obj = PSPPIRE_VAR_TYPE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_FORMAT:
      psppire_var_type_dialog_set_format (obj, g_value_get_boxed (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_var_type_dialog_get_property (GObject      *object,
                                      guint         prop_id,
                                      GValue       *value,
                                      GParamSpec   *pspec)
{
  PsppireVarTypeDialog *obj = PSPPIRE_VAR_TYPE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_FORMAT:
      g_value_set_boxed (value, &obj->fmt_l);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_var_type_dialog_set_format (PsppireVarTypeDialog *dialog,
                                    const struct fmt_spec *format)
{
  dialog->base_format = *format;
  psppire_var_type_dialog_set_state (dialog);
}

static const struct fmt_spec *
psppire_var_type_dialog_get_format (const PsppireVarTypeDialog *dialog)
{
  return &dialog->fmt_l;
}

static void
psppire_var_type_dialog_init (PsppireVarTypeDialog *obj)
{
  /* We do all of our work on widgets in the constructor function, because that
     runs after the construction properties have been set.  Otherwise
     PsppireDialog's "orientation" property hasn't been set and therefore we
     have no box to populate. */
  obj->base_format = F_8_0;
  obj->fmt_l = F_8_0;
}

static void
psppire_var_type_dialog_class_init (PsppireVarTypeDialogClass *class)
{
  GObjectClass *gobject_class;
  gobject_class = G_OBJECT_CLASS (class);

  gobject_class->constructor = psppire_var_type_dialog_constructor;
  gobject_class->set_property = psppire_var_type_dialog_set_property;
  gobject_class->get_property = psppire_var_type_dialog_get_property;

  g_object_class_install_property (
    gobject_class, PROP_FORMAT,
    g_param_spec_boxed ("format",
                        "Format",
                        "The format being edited.",
                        PSPPIRE_TYPE_FORMAT,
                        G_PARAM_READABLE | G_PARAM_WRITABLE));
}

PsppireVarTypeDialog *
psppire_var_type_dialog_new (const struct fmt_spec *format)
{
  return PSPPIRE_VAR_TYPE_DIALOG (
    g_object_new (PSPPIRE_TYPE_VAR_TYPE_DIALOG,
                  "orientation", PSPPIRE_HORIZONTAL,
                  "format", format,
                  NULL));
}

void
psppire_var_type_dialog_run (GtkWindow *parent_window,
                             struct fmt_spec *format)
{
  PsppireVarTypeDialog *dialog;

  dialog = psppire_var_type_dialog_new (format);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), parent_window);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_widget_show (GTK_WIDGET (dialog));

  if (psppire_dialog_run (PSPPIRE_DIALOG (dialog)) == GTK_RESPONSE_OK)
    *format = *psppire_var_type_dialog_get_format (dialog);

  gtk_widget_destroy (GTK_WIDGET (dialog));
}


/* callback for when any of the radio buttons are toggled */
static void
on_toggle (GtkToggleButton *togglebutton, gpointer dialog_)
{
  PsppireVarTypeDialog *dialog = dialog_;

  if ( gtk_toggle_button_get_active (togglebutton) == TRUE)
    refresh_active_button (dialog);
}

static void
refresh_active_button (PsppireVarTypeDialog *dialog)
{
  int i;

  for (i = 0; i < num_BUTTONS; i++)
    {
      GtkToggleButton *toggle = GTK_TOGGLE_BUTTON (dialog->radioButton[i]);

      if (gtk_toggle_button_get_active (toggle))
        {
          if (dialog->active_button != i)
            {
              dialog->active_button = i;
              on_active_button_change (toggle, dialog);
            }
          return;
        }
    }

  g_return_if_reached ();
}

static void
update_adj_ranges (PsppireVarTypeDialog *dialog)
{
  enum fmt_type type = dialog->fmt_l.type;
  const enum fmt_use use = FMT_FOR_OUTPUT;
  int min_w = fmt_min_width (type, use);
  int max_w = fmt_max_width (type, use);
  int max_d = fmt_max_decimals (type, max_w, use);

  g_object_set (dialog->adj_width,
                "lower", (double) min_w,
                "upper", (double) max_w,
                NULL);

  g_object_set (dialog->adj_decimals,
                "lower", 0.0,
                "upper", (double) max_d,
                NULL);
}

/* callback for when any of the radio buttons are toggled */
static void
on_active_button_change (GtkToggleButton *togglebutton,
                         PsppireVarTypeDialog *dialog)
{
  enum widgets {
    W_WIDTH          = 1 << 0,
    W_DECIMALS       = 1 << 1,
    W_DATE_FORMATS   = 1 << 2,
    W_DOLLAR_FORMATS = 1 << 3,
    W_CC_FORMATS     = 1 << 4,
  };

  enum widgets widgets;
  int indx;

  switch (dialog->active_button)
    {
    case BUTTON_NUMERIC:
    case BUTTON_COMMA:
    case BUTTON_DOT:
    case BUTTON_SCIENTIFIC:
      widgets = W_WIDTH | W_DECIMALS;
      break;

    case BUTTON_STRING:
      widgets = W_WIDTH;
      break;

    case BUTTON_DATE:
      widgets = W_DATE_FORMATS;
      break;

    case BUTTON_DOLLAR:
      widgets = W_DOLLAR_FORMATS;
      break;

    case BUTTON_CUSTOM:
      widgets = W_CC_FORMATS | W_WIDTH | W_DECIMALS;
      break;

    default:
      /* No button active */
      return;
    }

  gtk_widget_set_visible (dialog->width_decimals, (widgets & W_WIDTH) != 0);
  gtk_widget_set_visible (dialog->entry_width, (widgets & W_WIDTH) != 0);
  gtk_widget_set_visible (dialog->entry_decimals, (widgets & W_DECIMALS) != 0);
  gtk_widget_set_visible (dialog->label_decimals, (widgets & W_DECIMALS) != 0);
  gtk_widget_set_visible (dialog->date_format_list,
                          (widgets & W_DATE_FORMATS) != 0);
  gtk_widget_set_visible (dialog->custom_currency_hbox,
                          (widgets & W_CC_FORMATS) != 0);
  gtk_widget_set_visible (dialog->dollar_window,
                          (widgets & W_DOLLAR_FORMATS) != 0);

  dialog->fmt_l = dialog->base_format;

  switch (dialog->active_button)
    {
    case BUTTON_NUMERIC:
      dialog->fmt_l.type = FMT_F;
      break;
    case BUTTON_COMMA:
      dialog->fmt_l.type = FMT_COMMA;
      break;
    case BUTTON_DOT:
      dialog->fmt_l.type = FMT_DOT;
      break;
    case BUTTON_SCIENTIFIC:
      dialog->fmt_l.type = FMT_E;
      break;
    case BUTTON_STRING:
      dialog->fmt_l.type = FMT_A;
      break;
    case BUTTON_DATE:
      indx = find_format (&dialog->fmt_l, date_format,
                          sizeof date_format / sizeof *date_format);
      select_treeview_at_index (dialog->date_format_treeview, indx);
      dialog->fmt_l = date_format[indx];
      break;
    case BUTTON_DOLLAR:
      indx = find_format (&dialog->fmt_l, dollar_format,
                          sizeof dollar_format / sizeof *dollar_format);
      select_treeview_at_index (dialog->dollar_treeview, indx);
      dialog->fmt_l = dollar_format[indx];
      break;
    case BUTTON_CUSTOM:
      indx = find_format_type (dialog->fmt_l.type, cc_format,
                               sizeof cc_format / sizeof *cc_format);
      select_treeview_at_index (dialog->custom_treeview, indx);
      dialog->fmt_l.type = cc_format[indx];
      break;
    }

  fmt_fix_output (&dialog->fmt_l);
  update_adj_ranges (dialog);
  update_width_decimals (dialog);
}

static void
add_to_group (GtkWidget *w, gpointer data)
{
  GtkSizeGroup *sg = data;

  gtk_size_group_add_widget (sg, w);
}

/* Set the local width and decimals entry boxes to reflec the local format */
static void
update_width_decimals (const PsppireVarTypeDialog *dialog)
{
  gtk_adjustment_set_value (dialog->adj_width, dialog->fmt_l.w);
  gtk_adjustment_set_value (dialog->adj_decimals, dialog->fmt_l.d);
}

static void
on_width_changed (GtkEntry *entry, PsppireVarTypeDialog *dialog)
{
  int w = atoi (gtk_entry_get_text (GTK_ENTRY (dialog->entry_width)));
  fmt_change_width (&dialog->fmt_l, w, FMT_FOR_OUTPUT);
  update_width_decimals (dialog);
}

static void
on_decimals_changed (GtkEntry *entry, PsppireVarTypeDialog *dialog)
{
  int d = atoi (gtk_entry_get_text (GTK_ENTRY (dialog->entry_decimals)));
  fmt_change_decimals (&dialog->fmt_l, d, FMT_FOR_OUTPUT);
  update_width_decimals (dialog);
}

/* Callback for when the custom treeview row is changed.
   It sets dialog box to reflect the selected format */
static void
preview_custom (GtkWidget *w, gpointer data)
{
  const gchar *text ;

  PsppireVarTypeDialog *dialog = data;

  if ( dialog->active_button != BUTTON_CUSTOM )
    return;

  text = gtk_entry_get_text (GTK_ENTRY (dialog->entry_decimals));
  dialog->fmt_l.d = atoi (text);

  text = gtk_entry_get_text (GTK_ENTRY (dialog->entry_width));
  dialog->fmt_l.w = atoi (text);

  msg_disable ();
  if ( ! fmt_check_output (&dialog->fmt_l))
    {
      gtk_label_set_text (GTK_LABEL (dialog->label_psample), "---");
      gtk_label_set_text (GTK_LABEL (dialog->label_nsample), "---");
    }
  else
    {
      gchar *sample_text;
      union value v;
      v.f = 1234.56;

      sample_text = g_strchug (data_out (&v, NULL, &dialog->fmt_l));
      gtk_label_set_text (GTK_LABEL (dialog->label_psample), sample_text);
      g_free (sample_text);

      v.f = -v.f;
      sample_text = g_strchug (data_out (&v, NULL, &dialog->fmt_l));
      gtk_label_set_text (GTK_LABEL (dialog->label_nsample), sample_text);
      g_free (sample_text);
    }
  msg_enable ();
}

static gint
get_index_from_treeview (GtkTreeView *treeview)
{
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
  GtkTreeModel *model;
  GtkTreePath *path;
  GtkTreeIter iter;
  gint index;

  gtk_tree_selection_get_selected (selection, &model, &iter);
  path = gtk_tree_model_get_path (model, &iter);
  if (!path || gtk_tree_path_get_depth (path) < 1)
    index = 0;
  else
    index = gtk_tree_path_get_indices (path)[0];
  gtk_tree_path_free (path);

  return index;
}

/* Callback for when a date treeview row is changed.
   It sets the fmt_l_spec to reflect the selected format */
static void
set_date_format_from_treeview (GtkTreeView *treeview,
                               PsppireVarTypeDialog *dialog)
{
  dialog->fmt_l = date_format[get_index_from_treeview (treeview)];
}

/* Callback for when a dollar treeview row is changed.
   It sets the fmt_l_spec to reflect the selected format */
static void
set_dollar_format_from_treeview (GtkTreeView *treeview,
                                 PsppireVarTypeDialog *dialog)
{
  dialog->fmt_l = dollar_format[get_index_from_treeview (treeview)];
}

/* Callback for when a treeview row is changed.
   It sets the type of the fmt_l to reflect the selected type */
static void
set_custom_format_from_treeview (GtkTreeView *treeview,
                                 PsppireVarTypeDialog *dialog)
{
  dialog->fmt_l.type = cc_format[get_index_from_treeview (treeview)];
  update_adj_ranges (dialog);
  fmt_fix_output (&dialog->fmt_l);
  update_width_decimals (dialog);
}

/* Create the structure */
static GObject *
psppire_var_type_dialog_constructor (GType                  type,
                                     guint                  n_properties,
                                     GObjectConstructParam *properties)
{
  PsppireVarTypeDialog *dialog;
  GtkContainer *content_area;
  GtkBuilder *xml;
  GObject *obj;
  gint i;

  obj = G_OBJECT_CLASS (psppire_var_type_dialog_parent_class)->constructor (
    type, n_properties, properties);
  dialog = PSPPIRE_VAR_TYPE_DIALOG (obj);

  xml = builder_new ("var-type-dialog.ui");

  content_area = GTK_CONTAINER (PSPPIRE_DIALOG (dialog)->box);
  gtk_container_add (GTK_CONTAINER (content_area),
                     get_widget_assert (xml, "var-type-dialog"));

  dialog->active_button = -1;

  g_signal_connect (dialog, "delete-event",
		    G_CALLBACK (gtk_widget_hide_on_delete), NULL);

  dialog->radioButton[BUTTON_NUMERIC] =
    get_widget_assert (xml,"radiobutton1");
  dialog->radioButton[BUTTON_COMMA] =
    get_widget_assert (xml,"radiobutton2");
  dialog->radioButton[BUTTON_DOT] =
    get_widget_assert (xml,"radiobutton3");
  dialog->radioButton[BUTTON_SCIENTIFIC] =
    get_widget_assert (xml,"radiobutton4");
  dialog->radioButton[BUTTON_DATE] =
    get_widget_assert (xml,"radiobutton5");
  dialog->radioButton[BUTTON_DOLLAR] =
    get_widget_assert (xml,"radiobutton6");
  dialog->radioButton[BUTTON_CUSTOM] =
    get_widget_assert (xml,"radiobutton7");
  dialog->radioButton[BUTTON_STRING] =
    get_widget_assert (xml,"radiobutton8");


  dialog->date_format_list = get_widget_assert (xml, "scrolledwindow4");
  dialog->width_decimals = get_widget_assert (xml, "width_decimals");
  dialog->label_decimals = get_widget_assert (xml, "decimals_label");
  dialog->entry_decimals = get_widget_assert (xml, "decimals_entry");
  dialog->adj_decimals = gtk_spin_button_get_adjustment (
    GTK_SPIN_BUTTON (dialog->entry_decimals));

  dialog->label_psample = get_widget_assert (xml, "psample_label");
  dialog->label_nsample = get_widget_assert (xml, "nsample_label");


  dialog->entry_width = get_widget_assert (xml,"width_entry");
  dialog->adj_width = gtk_spin_button_get_adjustment (
    GTK_SPIN_BUTTON (dialog->entry_width));
  dialog->custom_currency_hbox = get_widget_assert (xml,
						   "custom_currency_hbox");

  dialog->dollar_window = get_widget_assert (xml, "dollar_window");
  dialog->dollar_treeview =
    GTK_TREE_VIEW (get_widget_assert (xml, "dollar_treeview"));

  dialog->custom_treeview =
    GTK_TREE_VIEW (get_widget_assert (xml, "custom_treeview"));



  {
  GtkTreeIter iter;
  GtkListStore *list_store ;

  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer ;

  /* The "middle_box" is a vbox with serveral children.
     However only one child is ever shown at a time.
     We need to make sure that they all have the same width, to avoid
     upleasant resizing effects */
  GtkSizeGroup *sizeGroup = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

  gtk_container_foreach (GTK_CONTAINER (get_widget_assert (xml, "middle_box")),
			add_to_group, sizeGroup);


  for (i = 0 ; i < num_BUTTONS; ++i )
    g_signal_connect (dialog->radioButton[i], "toggled",
                      G_CALLBACK (on_toggle), dialog);

  /* Populate the date format tree view */
  dialog->date_format_treeview = GTK_TREE_VIEW (get_widget_assert (xml,
					      "date_format_list_view"));

  renderer = gtk_cell_renderer_text_new ();

  column = gtk_tree_view_column_new_with_attributes ("Title",
						     renderer,
						     "text",
						     0,
						     NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->date_format_treeview),
			       column);


  list_store = gtk_list_store_new (1, G_TYPE_STRING);

  for ( i = 0 ; i < sizeof (date_format) / sizeof (date_format[0]) ; ++i )
    {
      const struct fmt_spec *f = &date_format[i];
      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter,
                          0, fmt_date_template (f->type, f->w),
			  -1);
    }

  gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->date_format_treeview),
			  GTK_TREE_MODEL (list_store));

  g_object_unref (list_store);

  g_signal_connect (dialog->date_format_treeview, "cursor-changed",
		   G_CALLBACK (set_date_format_from_treeview), dialog);


  /* populate the dollar treeview */

  renderer = gtk_cell_renderer_text_new ();

  column = gtk_tree_view_column_new_with_attributes ("Title",
						     renderer,
						     "text",
						     0,
						     NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->dollar_treeview),
			       column);


  list_store = gtk_list_store_new (1, G_TYPE_STRING);

  for ( i = 0 ; i < sizeof (dollar_format)/sizeof (dollar_format[0]) ; ++i )
    {
      char *template = settings_dollar_template (&dollar_format[i]);
      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter,
                          0, template,
			  -1);
      free (template);
    }

  gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->dollar_treeview),
			  GTK_TREE_MODEL (list_store));

  g_object_unref (list_store);

  g_signal_connect (dialog->dollar_treeview,
		   "cursor-changed",
		   G_CALLBACK (set_dollar_format_from_treeview), dialog);

  g_signal_connect_swapped (dialog->dollar_treeview,
		   "cursor-changed",
		   G_CALLBACK (update_width_decimals), dialog);


  /* populate the custom treeview */

  renderer = gtk_cell_renderer_text_new ();

  column = gtk_tree_view_column_new_with_attributes ("Title",
						     renderer,
						     "text",
						     0,
						     NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->custom_treeview),
			       column);


  list_store = gtk_list_store_new (1, G_TYPE_STRING);

  for ( i = 0 ; i < 5 ; ++i )
    {
      enum fmt_type cc_fmts[5] = {FMT_CCA, FMT_CCB, FMT_CCC, FMT_CCD, FMT_CCE};
      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter,
                          0, fmt_name (cc_fmts[i]),
			  -1);
    }

  gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->custom_treeview),
			  GTK_TREE_MODEL (list_store));

  g_object_unref (list_store);


  g_signal_connect (dialog->custom_treeview,
		   "cursor-changed",
		   G_CALLBACK (set_custom_format_from_treeview), dialog);


  g_signal_connect (dialog->custom_treeview,
		   "cursor-changed",
		   G_CALLBACK (preview_custom), dialog);


  g_signal_connect (dialog->entry_width, "changed",
                    G_CALLBACK (on_width_changed), dialog);
  g_signal_connect (dialog->entry_decimals, "changed",
                    G_CALLBACK (on_decimals_changed), dialog);

  g_signal_connect (dialog->entry_width,
		   "changed",
		   G_CALLBACK (preview_custom), dialog);


  g_signal_connect (dialog->entry_decimals,
		   "changed",
		   G_CALLBACK (preview_custom), dialog);

  }

  g_object_unref (xml);

  psppire_var_type_dialog_set_state (dialog);

  return obj;
}


/* Set a particular button to be active */
void
var_type_dialog_set_active_button (PsppireVarTypeDialog *dialog, gint b)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->radioButton[b]),
			       TRUE);
}



static void
select_treeview_at_index (GtkTreeView *treeview, int index)
{
  GtkTreePath *path;

  path = gtk_tree_path_new_from_indices (index, -1);
  gtk_tree_view_set_cursor (treeview, path, 0, 0);
  gtk_tree_path_free (path);
}

static int
find_format (const struct fmt_spec *target,
             const struct fmt_spec formats[], int n_formats)
{
  int i;

  for (i = 0; i < n_formats; i++)
    if (fmt_equal (target, &formats[i]))
      return i;

  return 0;
}

static int
find_format_type (int target, const int types[], int n_types)
{
  int i;

  for (i = 0; i < n_types; i++)
    if (target == types[i])
      return i;

  return 0;
}

/* Set up the state of the dialog box to match the variable VAR */
static void
psppire_var_type_dialog_set_state (PsppireVarTypeDialog *dialog)
{
  int button;

  g_return_if_fail (dialog != NULL);

  /* Populate the radio button states */
  switch (dialog->base_format.type)
    {
    default:
    case FMT_F:
      button = BUTTON_NUMERIC;
      break;
    case FMT_A:
      button = BUTTON_STRING;
      break;
    case FMT_COMMA:
      button = BUTTON_COMMA;
      break;
    case FMT_DOT:
      button = BUTTON_DOT;
      break;
    case FMT_DOLLAR:
      button = BUTTON_DOLLAR;
      break;
    case FMT_DATE:
    case FMT_EDATE:
    case FMT_SDATE:
    case FMT_ADATE:
    case FMT_JDATE:
    case FMT_QYR:
    case FMT_MOYR:
    case FMT_WKYR:
    case FMT_DATETIME:
    case FMT_TIME:
    case FMT_DTIME:
    case FMT_WKDAY:
    case FMT_MONTH:
      button = BUTTON_DATE;
      break;
    case FMT_CCA:
    case FMT_CCB:
    case FMT_CCC:
    case FMT_CCD:
    case FMT_CCE:
      button = BUTTON_CUSTOM;
      break;
    }

  var_type_dialog_set_active_button (dialog, button);
  refresh_active_button (dialog);
  on_active_button_change (GTK_TOGGLE_BUTTON (dialog->radioButton[button]),
                           dialog);
}
