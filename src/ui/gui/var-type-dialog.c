/* PSPPIRE - a graphical user interface for PSPP.
    Copyright (C) 2005, 2006  Free Software Foundation

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

#include "var-type-dialog.h"

#include "helper.h"

#include <data/variable.h>
#include <data/settings.h>
#include <libpspp/message.h>


struct tgs
{
  struct var_type_dialog *dialog;
  gint button;
};


struct format_opt {
  gchar desc[21];
  struct fmt_spec spec;
};


static const struct format_opt format_option[] =
  {
    { "dd-mmm-yyyy", {FMT_DATE,  11, 0} },
    { "dd-mmm-yy",   {FMT_DATE,   9, 0} },
    { "mm/dd/yyyy",  {FMT_ADATE, 10, 0} },
    { "mm/dd/yy",    {FMT_ADATE, 8, 0} },
    { "dd.mm.yyyy",  {FMT_EDATE, 10, 0} },
    { "dd.mm.yy",    {FMT_EDATE, 8, 0} },
    { "yyyy/mm/dd",  {FMT_SDATE, 10, 0} },
    { "yy/mm/dd",    {FMT_SDATE, 8, 0} },
    { "yyddd",       {FMT_JDATE, 5, 0} },
    { "yyyyddd",     {FMT_JDATE, 7, 0} },
    { "q Q yyyy",    {FMT_QYR, 8, 0} },
    { "q Q yy",      {FMT_QYR, 6, 0} },
    { "mmm yyyy",    {FMT_MOYR, 8, 0} },
    { "mmm yy",      {FMT_MOYR, 6, 0} },
    { "dd WK yyyy",  {FMT_WKYR, 10, 0} },
    { "dd WK yy",    {FMT_WKYR, 8, 0} },
    { "dd-mmm-yyyy HH:MM", {FMT_DATETIME, 17, 0}},
    { "dd-mmm-yyyy HH:MM:SS", {FMT_DATETIME, 20, 0}}
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


static void select_treeview_from_format
 (GtkTreeView *treeview, const struct fmt_spec *fmt);

static void select_treeview_from_format_type (GtkTreeView *treeview,
					     const int fmt_type);


/* callback for when any of the radio buttons are toggled */
static void
on_toggle_1 (GtkToggleButton *togglebutton, gpointer user_data)
{
  struct tgs *tgs = user_data;

  if ( gtk_toggle_button_get_active (togglebutton) == FALSE)
    return ;

  tgs->dialog->active_button = tgs->button;
}

static void update_width_decimals (const struct var_type_dialog *dialog);

#define force_max(x, val) if (x > val) x = val

/*
   Set the local format from the variable
   and force them to have sensible values */
static void
set_local_width_decimals (struct var_type_dialog *dialog)
{
  dialog->fmt_l = * var_get_write_format (dialog->pv);

  switch (dialog->active_button)
    {
    case BUTTON_STRING:
      force_max ( dialog->fmt_l.w, 255);
      break;
    default:
      force_max ( dialog->fmt_l.w, 40);
      force_max ( dialog->fmt_l.d, 16);
      break;
    }
}


/* callback for when any of the radio buttons are toggled */
static void
on_toggle_2 (GtkToggleButton *togglebutton, gpointer user_data)
{
  struct var_type_dialog *dialog = user_data;
  if ( gtk_toggle_button_get_active (togglebutton) == FALSE)
    {
      switch (dialog->active_button)
	{
	case BUTTON_DATE:
	  gtk_widget_hide (dialog->date_format_list);
	  break;
	case BUTTON_CUSTOM:
	  gtk_widget_hide (dialog->custom_currency_hbox);
	  break;
	case BUTTON_DOLLAR:
	  gtk_widget_hide (dialog->dollar_window);
	  break;
	case BUTTON_STRING:
	  gtk_widget_show (dialog->label_decimals);
	  gtk_widget_show (dialog->entry_decimals);
	  break;
	}
      return ;
    }

  set_local_width_decimals (dialog);
  update_width_decimals (dialog);

  switch (dialog->active_button)
    {
    case BUTTON_STRING:
      gtk_widget_show (dialog->entry_width);
      gtk_widget_show (dialog->width_decimals);
      gtk_widget_hide (dialog->label_decimals);
      gtk_widget_hide (dialog->entry_decimals);
      break;
    case BUTTON_DATE:
      select_treeview_from_format (dialog->date_format_treeview,
				  &format_option[0].spec);
      gtk_widget_hide (dialog->width_decimals);
      gtk_widget_show (dialog->date_format_list);
      break;
    case BUTTON_DOLLAR:
      select_treeview_from_format (dialog->dollar_treeview,
				  &dollar_format[0]);
      gtk_widget_show (dialog->dollar_window);
      gtk_widget_show_all (dialog->width_decimals);
      break;
    case BUTTON_CUSTOM:
      select_treeview_from_format_type (dialog->custom_treeview,
				  cc_format[0]);

      gtk_widget_show (dialog->width_decimals);
      gtk_widget_show (dialog->custom_currency_hbox);
      break;
    default:
      gtk_widget_show_all (dialog->width_decimals);
      break;
    }
}



static gint on_var_type_ok_clicked (GtkWidget *w, gpointer data);
static gint hide_dialog (GtkWidget *w,  gpointer data);


static void
add_to_group (GtkWidget *w, gpointer data)
{
  GtkSizeGroup *sg = data;

  gtk_size_group_add_widget (sg, w);
}

/* Set the local width and decimals entry boxes to reflec the local format */
static void
update_width_decimals (const struct var_type_dialog *dialog)
{
  gchar *text;
  g_assert (dialog);

  text = g_strdup_printf ("%d", dialog->fmt_l.w);
  gtk_entry_set_text (GTK_ENTRY (dialog->entry_width), text);
  g_free (text);

  text = g_strdup_printf ("%d", dialog->fmt_l.d);
  gtk_entry_set_text (GTK_ENTRY (dialog->entry_decimals), text);
  g_free (text);
}

/* Callback for when the custom treeview row is changed.
   It sets dialog box to reflect the selected format */
static void
preview_custom (GtkWidget *w, gpointer data)
{
  const gchar *text ;

  struct var_type_dialog *dialog = data;

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

      sample_text = value_to_text (v, dialog->fmt_l);
      gtk_label_set_text (GTK_LABEL (dialog->label_psample), sample_text);
      g_free (sample_text);

      v.f = -v.f;
      sample_text = value_to_text (v, dialog->fmt_l);
      gtk_label_set_text (GTK_LABEL (dialog->label_nsample), sample_text);
      g_free (sample_text);
    }
  msg_enable ();
}

/* Callback for when a treeview row is changed.
   It sets the fmt_l_spec to reflect the selected format */
static void
set_format_from_treeview (GtkTreeView *treeview, gpointer data)
{
  struct var_type_dialog *dialog = data;
  GtkTreeIter iter ;
  GValue the_value = {0};

  GtkTreeSelection* sel =  gtk_tree_view_get_selection (treeview);

  GtkTreeModel * model  = gtk_tree_view_get_model (treeview);

  gtk_tree_selection_get_selected (sel, &model, &iter);

  gtk_tree_model_get_value (model, &iter, 1, &the_value);

  dialog->fmt_l = *(struct fmt_spec *) g_value_get_pointer (&the_value);
}


/* Callback for when a treeview row is changed.
   It sets the type of the fmt_l to reflect the selected type */
static void
set_format_type_from_treeview (GtkTreeView *treeview, gpointer data)
{
  static struct fmt_spec custom_format = {0,0,0};
  struct var_type_dialog *dialog = data;
  GtkTreeIter iter ;
  GValue the_value = {0};

  GtkTreeSelection* sel =  gtk_tree_view_get_selection (treeview);

  GtkTreeModel * model  = gtk_tree_view_get_model (treeview);

  gtk_tree_selection_get_selected (sel, &model, &iter);

  gtk_tree_model_get_value (model, &iter, 1, &the_value);

  dialog->fmt_l = custom_format;
  dialog->fmt_l.type = *(int*) g_value_get_pointer (&the_value);

}




/* Create the structure from the XML definitions */
struct var_type_dialog *
var_type_dialog_create (GtkBuilder *xml)
{
  gint i;
  struct var_type_dialog *dialog = g_malloc (sizeof (struct var_type_dialog));

  g_assert (xml);

  dialog->window = get_widget_assert (xml,"var_type_dialog");
  dialog->active_button = -1;


  g_signal_connect (dialog->window, "delete-event",
		    G_CALLBACK (gtk_widget_hide_on_delete), NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog->window),
			       GTK_WINDOW (get_widget_assert (xml, "data_editor")));

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

  dialog->label_psample = get_widget_assert (xml, "psample_label");
  dialog->label_nsample = get_widget_assert (xml, "nsample_label");


  dialog->entry_width = get_widget_assert (xml,"width_entry");

  dialog->custom_currency_hbox = get_widget_assert (xml,
						   "custom_currency_hbox");

  dialog->dollar_window = get_widget_assert (xml, "dollar_window");
  dialog->dollar_treeview =
    GTK_TREE_VIEW (get_widget_assert (xml, "dollar_treeview"));

  dialog->custom_treeview =
    GTK_TREE_VIEW (get_widget_assert (xml, "custom_treeview"));


  dialog->ok = get_widget_assert (xml,"var_type_ok");


  {
  GtkTreeIter iter;
  GtkListStore *list_store ;

  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer ;

  static struct tgs tgs[num_BUTTONS];
  /* The "middle_box" is a vbox with serveral children.
     However only one child is ever shown at a time.
     We need to make sure that they all have the same width, to avoid
     upleasant resizing effects */
  GtkSizeGroup *sizeGroup = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

  gtk_container_foreach (GTK_CONTAINER (get_widget_assert (xml, "middle_box")),
			add_to_group, sizeGroup);


  for (i = 0 ; i < num_BUTTONS; ++i )
    {
      tgs[i].dialog = dialog;
      tgs[i].button = i;
      g_signal_connect (dialog->radioButton[i], "toggled",
		       G_CALLBACK (on_toggle_1), &tgs[i]);

      g_signal_connect (dialog->radioButton[i], "toggled",
		       G_CALLBACK (on_toggle_2), dialog);
    }

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


  list_store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER);

  for ( i = 0 ; i < sizeof (format_option) / sizeof (format_option[0]) ; ++i )
    {
      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter,
                          0, format_option[i].desc,
			  1, &format_option[i].spec,
			  -1);
    }

  gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->date_format_treeview),
			  GTK_TREE_MODEL (list_store));

  g_object_unref (list_store);

  g_signal_connect (dialog->date_format_treeview, "cursor-changed",
		   GTK_SIGNAL_FUNC (set_format_from_treeview), dialog);


  /* populate the dollar treeview */

  renderer = gtk_cell_renderer_text_new ();

  column = gtk_tree_view_column_new_with_attributes ("Title",
						     renderer,
						     "text",
						     0,
						     NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->dollar_treeview),
			       column);


  list_store = gtk_list_store_new (2, G_TYPE_STRING,
						 G_TYPE_POINTER);

  for ( i = 0 ; i < sizeof (dollar_format)/sizeof (dollar_format[0]) ; ++i )
    {
      char *template = settings_dollar_template (&dollar_format[i]);
      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter,
                          0, template,
			  1, &dollar_format[i],
			  -1);
      free (template);
    }

  gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->dollar_treeview),
			  GTK_TREE_MODEL (list_store));

  g_object_unref (list_store);

  g_signal_connect (dialog->dollar_treeview,
		   "cursor-changed",
		   GTK_SIGNAL_FUNC (set_format_from_treeview), dialog);

  g_signal_connect_swapped (dialog->dollar_treeview,
		   "cursor-changed",
		   GTK_SIGNAL_FUNC (update_width_decimals), dialog);


  /* populate the custom treeview */

  renderer = gtk_cell_renderer_text_new ();

  column = gtk_tree_view_column_new_with_attributes ("Title",
						     renderer,
						     "text",
						     0,
						     NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->custom_treeview),
			       column);


  list_store = gtk_list_store_new (2, G_TYPE_STRING,
						 G_TYPE_POINTER);

  for ( i = 0 ; i < 5 ; ++i )
    {
      enum fmt_type cc_fmts[5] = {FMT_CCA, FMT_CCB, FMT_CCC, FMT_CCD, FMT_CCE};
      gchar text[4];
      g_snprintf (text, 4, "%s", fmt_name (cc_fmts[i]));
      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter,
                          0, text,
			  1, &cc_format[i],
			  -1);
    }

  gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->custom_treeview),
			  GTK_TREE_MODEL (list_store));

  g_object_unref (list_store);


  g_signal_connect (dialog->custom_treeview,
		   "cursor-changed",
		   GTK_SIGNAL_FUNC (set_format_type_from_treeview), dialog);


  g_signal_connect (dialog->custom_treeview,
		   "cursor-changed",
		   GTK_SIGNAL_FUNC (preview_custom), dialog);


  g_signal_connect (dialog->entry_width,
		   "changed",
		   GTK_SIGNAL_FUNC (preview_custom), dialog);


  g_signal_connect (dialog->entry_decimals,
		   "changed",
		   GTK_SIGNAL_FUNC (preview_custom), dialog);


  /* Connect to the OK button */
  g_signal_connect (dialog->ok, "clicked", G_CALLBACK (on_var_type_ok_clicked),
		   dialog);


  /* And the cancel button */
  g_signal_connect (get_widget_assert (xml, "var_type_cancel") , "clicked",
		    G_CALLBACK (hide_dialog),
		    dialog);


  }

  return dialog;
}


/* Set a particular button to be active */
void
var_type_dialog_set_active_button (struct var_type_dialog *dialog, gint b)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->radioButton[b]),
			       TRUE);
  dialog->active_button = b;
}



/* Set the TREEVIEW list cursor to the item described by FMT */
static void
select_treeview_from_format (GtkTreeView *treeview, const struct fmt_spec *fmt)
{
  GtkTreePath *path ;

  /*
    We do this with a linear search through the model --- hardly
    efficient, but the list is short ... */
  GtkTreeIter iter;

  GtkTreeModel * model  = gtk_tree_view_get_model (treeview);

  gboolean success;
  for (success = gtk_tree_model_get_iter_first (model, &iter);
       success;
       success = gtk_tree_model_iter_next (model, &iter))
    {
      const struct fmt_spec *spec;

      GValue value = {0};

      gtk_tree_model_get_value (model, &iter, 1, &value);

      spec = g_value_get_pointer (&value);

      if ( 0 == memcmp (spec, fmt, sizeof (struct fmt_spec)))
	{
	  break;
	}
    }

  path = gtk_tree_model_get_path (model, &iter);
  if ( path )
    {
      gtk_tree_view_set_cursor (treeview, path, 0, 0);
      gtk_tree_path_free (path);
    }
  else
    {
      char str[FMT_STRING_LEN_MAX + 1];
      g_warning ("Unusual date format: %s\n", fmt_to_string (fmt, str));
    }
}


/* Set the TREEVIEW list cursor to the item described by FMT_TYPE */
static void
select_treeview_from_format_type (GtkTreeView *treeview,
				 const int fmt_type)
{
  GtkTreePath *path ;

 /*
    We do this with a linear search through the model --- hardly
    efficient, but the list is short ... */
  GtkTreeIter iter;

  GtkTreeModel * model  = gtk_tree_view_get_model (treeview);

  gboolean success;
  for (success = gtk_tree_model_get_iter_first (model, &iter);
       success;
       success = gtk_tree_model_iter_next (model, &iter))
    {
      int spec ;

      GValue value = {0};

      gtk_tree_model_get_value (model, &iter, 1, &value);

      spec = * ((int *) g_value_get_pointer (&value));

      if ( spec == fmt_type)
	break;
    }

  path = gtk_tree_model_get_path (model, &iter);
  if ( path )
    {
      gtk_tree_view_set_cursor (treeview, path, 0, 0);
      gtk_tree_path_free (path);
    }
  else
    g_warning ("Unknown custom type  %d\n", fmt_type);

}

/* Set up the state of the dialog box to match the variable VAR */
static void
var_type_dialog_set_state (struct var_type_dialog *dialog)
{
  const struct fmt_spec *write_spec ;
  GString *str = g_string_new ("");

  g_assert (dialog);
  g_assert (dialog->pv);

  /* Populate width and decimals */
  write_spec = var_get_write_format (dialog->pv);

  g_string_printf (str, "%d", write_spec->d);

  gtk_entry_set_text (GTK_ENTRY (dialog->entry_decimals),
		     str->str);

  g_string_printf (str, "%d", write_spec->w);

  gtk_entry_set_text (GTK_ENTRY (dialog->entry_width),
		     str->str);

  g_string_free (str, TRUE);

  /* Populate the radio button states */
  switch (write_spec->type)
    {
    case FMT_F:
      var_type_dialog_set_active_button (dialog, BUTTON_NUMERIC);
      gtk_widget_show_all (dialog->width_decimals);
      break;
    case FMT_A:
      var_type_dialog_set_active_button (dialog, BUTTON_STRING);
      gtk_widget_hide (dialog->label_decimals);
      gtk_widget_hide (dialog->entry_decimals);
      break;
    case FMT_COMMA:
      var_type_dialog_set_active_button (dialog, BUTTON_COMMA);
      gtk_widget_show_all (dialog->width_decimals);
      break;
    case FMT_DOT:
      var_type_dialog_set_active_button (dialog, BUTTON_DOT);
      gtk_widget_show_all (dialog->width_decimals);
      break;
    case FMT_DOLLAR:
      var_type_dialog_set_active_button (dialog, BUTTON_DOLLAR);
      gtk_widget_show_all (dialog->width_decimals);

      select_treeview_from_format (dialog->dollar_treeview, write_spec);
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
      var_type_dialog_set_active_button (dialog, BUTTON_DATE);
      gtk_widget_hide (dialog->width_decimals);
      gtk_widget_show (dialog->date_format_list);
      select_treeview_from_format (dialog->date_format_treeview, write_spec);
      break;
    case FMT_CCA:
    case FMT_CCB:
    case FMT_CCC:
    case FMT_CCD:
    case FMT_CCE:
      var_type_dialog_set_active_button (dialog, BUTTON_CUSTOM);
      select_treeview_from_format_type (dialog->custom_treeview,
				       write_spec->type);
      gtk_widget_show_all (dialog->width_decimals);
      break;
    default:
      gtk_widget_show_all (dialog->width_decimals);
      break;
    }
}


/* Popup the dialog box */
void
var_type_dialog_show (struct var_type_dialog *dialog)
{
  var_type_dialog_set_state (dialog);

  gtk_widget_show (dialog->window);
}

/* Fills F with an output format specification with type TYPE, width
   W, and D decimals. Iff it's a valid format, then return true.
*/
static bool
make_output_format_try (struct fmt_spec *f, int type, int w, int d)
{
  f->type = type;
  f->w = w;
  f->d = d;
  return fmt_check_output (f);
}




/* Callbacks for the Variable Type Dialog Box */

/* Callback for when the var type dialog is closed using the OK button.
   It sets the appropriate variable accordingly. */
static gint
on_var_type_ok_clicked (GtkWidget *w, gpointer data)
{
  struct var_type_dialog *dialog = data;

  g_assert (dialog);
  g_assert (dialog->pv);

  {
    gint width = atoi (gtk_entry_get_text
		      (GTK_ENTRY (dialog->entry_width)));

    gint decimals = atoi (gtk_entry_get_text
			 (GTK_ENTRY (dialog->entry_decimals)));

    gint new_type = VAL_NUMERIC;
    gint new_width = 0;
    bool result = false;
    struct fmt_spec spec;
    switch (dialog->active_button)
      {
      case BUTTON_STRING:
	new_type = VAL_STRING;
	new_width = width;
	result = make_output_format_try (&spec, FMT_A, width, 0);
	break;
      case BUTTON_NUMERIC:
	result = make_output_format_try (&spec, FMT_F, width, decimals);
	break;
      case BUTTON_COMMA:
	result = make_output_format_try (&spec, FMT_COMMA, width, decimals);
	break;
      case BUTTON_DOT:
	result = make_output_format_try (&spec, FMT_DOT, width, decimals);
	break;
      case BUTTON_SCIENTIFIC:
	result = make_output_format_try (&spec, FMT_E, width, decimals);
	break;
      case BUTTON_DATE:
      case BUTTON_CUSTOM:
	g_assert (fmt_check_output (&dialog->fmt_l));
	result = memcpy (&spec, &dialog->fmt_l, sizeof (struct fmt_spec));
	break;
      case BUTTON_DOLLAR:
	result = make_output_format_try (&spec, FMT_DOLLAR, width, decimals);
	break;
      default:
	g_print ("Unknown variable type: %d\n", dialog->active_button) ;
	result = false;
	break;
      }

    if ( result == true )
      {
	var_set_width (dialog->pv, new_width);
	var_set_both_formats (dialog->pv, &spec);
      }

  }
  gtk_widget_hide (dialog->window);

  return FALSE;
}



static gint
hide_dialog (GtkWidget *w,  gpointer data)
{
  struct var_type_dialog *dialog = data;

  gtk_widget_hide (dialog->window);

  return FALSE;
}

