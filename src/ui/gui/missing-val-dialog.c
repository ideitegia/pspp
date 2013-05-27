/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2005, 2006, 2009, 2011, 2012  Free Software Foundation

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

/*  This module describes the behaviour of the Missing Values dialog box,
    used for input of the missing values in the variable sheet */

#include <config.h>

#include "ui/gui/missing-val-dialog.h"

#include "builder-wrapper.h"
#include "helper.h"
#include <data/format.h>
#include "missing-val-dialog.h"
#include <data/missing-values.h>
#include <data/variable.h>
#include <data/data-in.h>

#include <gtk/gtk.h>

#include <string.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static GObject *psppire_missing_val_dialog_constructor (
  GType type, guint, GObjectConstructParam *);
static void psppire_missing_val_dialog_finalize (GObject *);

G_DEFINE_TYPE (PsppireMissingValDialog,
               psppire_missing_val_dialog,
               PSPPIRE_TYPE_DIALOG);
enum
  {
    PROP_0,
    PROP_VARIABLE,
    PROP_MISSING_VALUES
  };

static void
psppire_missing_val_dialog_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  PsppireMissingValDialog *obj = PSPPIRE_MISSING_VAL_DIALOG (object);

  switch (prop_id)
    {
    case PROP_VARIABLE:
      psppire_missing_val_dialog_set_variable (obj,
                                               g_value_get_pointer (value));
      break;
    case PROP_MISSING_VALUES:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_missing_val_dialog_get_property (GObject      *object,
                                         guint         prop_id,
                                         GValue       *value,
                                         GParamSpec   *pspec)
{
  PsppireMissingValDialog *obj = PSPPIRE_MISSING_VAL_DIALOG (object);

  switch (prop_id)
    {
    case PROP_MISSING_VALUES:
      g_value_set_pointer (value, &obj->mvl);
      break;
    case PROP_VARIABLE:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_missing_val_dialog_class_init (PsppireMissingValDialogClass *class)
{
  GObjectClass *gobject_class;
  gobject_class = G_OBJECT_CLASS (class);

  gobject_class->constructor = psppire_missing_val_dialog_constructor;
  gobject_class->finalize = psppire_missing_val_dialog_finalize;
  gobject_class->set_property = psppire_missing_val_dialog_set_property;
  gobject_class->get_property = psppire_missing_val_dialog_get_property;

  g_object_class_install_property (
    gobject_class, PROP_VARIABLE,
    g_param_spec_pointer ("variable",
                          "Variable",
                          "Variable whose missing values are to be edited.  "
                          "The variable's print format and encoding are also "
                          "used for editing.",
                          G_PARAM_WRITABLE));

  g_object_class_install_property (
    gobject_class, PROP_MISSING_VALUES,
    g_param_spec_pointer ("missing-values",
                          "Missing Values",
                          "Edited missing values.",
                          G_PARAM_READABLE));
}

static void
psppire_missing_val_dialog_init (PsppireMissingValDialog *dialog)
{
  /* We do all of our work on widgets in the constructor function, because that
     runs after the construction properties have been set.  Otherwise
     PsppireDialog's "orientation" property hasn't been set and therefore we
     have no box to populate. */
  mv_init (&dialog->mvl, 0);
  dialog->encoding = NULL;
}

static void
psppire_missing_val_dialog_finalize (GObject *obj)
{
  PsppireMissingValDialog *dialog = PSPPIRE_MISSING_VAL_DIALOG (obj);

  mv_destroy (&dialog->mvl);
  g_free (dialog->encoding);

  G_OBJECT_CLASS (psppire_missing_val_dialog_parent_class)->finalize (obj);
}

PsppireMissingValDialog *
psppire_missing_val_dialog_new (const struct variable *var)
{
  return PSPPIRE_MISSING_VAL_DIALOG (
    g_object_new (PSPPIRE_TYPE_MISSING_VAL_DIALOG,
                  "orientation", PSPPIRE_VERTICAL,
                  "variable", var,
                  NULL));
}

void
psppire_missing_val_dialog_run (GtkWindow *parent_window,
                                const struct variable *var,
                                struct missing_values *mv)
{
  PsppireMissingValDialog *dialog;

  dialog = psppire_missing_val_dialog_new (var);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), parent_window);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_widget_show (GTK_WIDGET (dialog));

  if (psppire_dialog_run (PSPPIRE_DIALOG (dialog)) == GTK_RESPONSE_OK)
    mv_copy (mv, psppire_missing_val_dialog_get_missing_values (dialog));
  else
    mv_copy (mv, var_get_missing_values (var));

  gtk_widget_destroy (GTK_WIDGET (dialog));
}


/* A simple (sub) dialog box for displaying user input errors */
static void
err_dialog (const gchar *msg, GtkWindow *window)
{
  GtkWidget *hbox ;
  GtkWidget *label = gtk_label_new (msg);

  GtkWidget *dialog =
    gtk_dialog_new_with_buttons ("PSPP",
				 window,
				 GTK_DIALOG_MODAL |
				 GTK_DIALOG_DESTROY_WITH_PARENT, 
				 GTK_STOCK_OK,
				 GTK_RESPONSE_ACCEPT,
				 NULL);


  GtkWidget *icon = gtk_image_new_from_stock (GTK_STOCK_DIALOG_ERROR,
					     GTK_ICON_SIZE_DIALOG);

  g_signal_connect_swapped (dialog,
			    "response",
			    G_CALLBACK (gtk_widget_destroy),
			    dialog);

  hbox = gtk_hbox_new (FALSE, 10);

  gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
		     hbox);

  gtk_box_pack_start (GTK_BOX (hbox), icon, TRUE, FALSE, 10);
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 10);

  gtk_widget_show_all (dialog);
}


/* Acceptability predicate for PsppireMissingValDialog.

   This function is also the only place that dialog->mvl gets updated. */
static gboolean
missing_val_dialog_acceptable (gpointer data)
{
  PsppireMissingValDialog *dialog = data;

  if ( gtk_toggle_button_get_active (dialog->button_discrete))
    {
      gint nvals = 0;
      gint badvals = 0;
      gint i;
      mv_clear(&dialog->mvl);
      for(i = 0 ; i < 3 ; ++i )
	{
	  gchar *text =
	    g_strdup (gtk_entry_get_text (GTK_ENTRY (dialog->mv[i])));

	  union value v;
	  if ( !text || strlen (g_strstrip (text)) == 0 )
	    {
	      g_free (text);
	      continue;
	    }

	  if ( text_to_value__ (text, &dialog->format, dialog->encoding, &v))
	    {
	      nvals++;
	      mv_add_value (&dialog->mvl, &v);
	    }
	  else
	      badvals++;
	  g_free (text);
	  value_destroy (&v, fmt_var_width (&dialog->format));
	}
      if ( nvals == 0 || badvals > 0 )
	{
	  err_dialog (_("Incorrect value for variable type"),
                      GTK_WINDOW (dialog));
	  return FALSE;
	}
    }

  if (gtk_toggle_button_get_active (dialog->button_range))
    {
      gchar *discrete_text ;

      union value low_val ;
      union value high_val;
      const gchar *low_text = gtk_entry_get_text (GTK_ENTRY (dialog->low));
      const gchar *high_text = gtk_entry_get_text (GTK_ENTRY (dialog->high));
      gboolean low_ok;
      gboolean high_ok;
      gboolean ok;

      low_ok = text_to_value__ (low_text, &dialog->format, dialog->encoding,
                                &low_val) != NULL;
      high_ok = text_to_value__ (high_text, &dialog->format, dialog->encoding,
                                 &high_val) != NULL;
      ok = low_ok && high_ok && low_val.f <= high_val.f;
      if (!ok)
        {
          err_dialog (_("Incorrect range specification"), GTK_WINDOW (dialog));
          if (low_ok)
            value_destroy (&low_val, fmt_var_width (&dialog->format));
          if (high_ok)
            value_destroy (&high_val, fmt_var_width (&dialog->format));
          return FALSE;
        }

      discrete_text =
	g_strdup (gtk_entry_get_text (GTK_ENTRY (dialog->discrete)));

      mv_clear (&dialog->mvl);
      mv_add_range (&dialog->mvl, low_val.f, high_val.f);

      value_destroy (&low_val, fmt_var_width (&dialog->format));
      value_destroy (&high_val, fmt_var_width (&dialog->format));

      if ( discrete_text && strlen (g_strstrip (discrete_text)) > 0 )
	{
	  union value discrete_val;
	  if ( !text_to_value__ (discrete_text,
                                 &dialog->format,
                                 dialog->encoding,
                                 &discrete_val))
	    {
	      err_dialog (_("Incorrect value for variable type"),
			 GTK_WINDOW (dialog) );
	      g_free (discrete_text);
	      value_destroy (&discrete_val, fmt_var_width (&dialog->format));
	      return FALSE;
	    }
	  mv_add_value (&dialog->mvl, &discrete_val);
	  value_destroy (&discrete_val, fmt_var_width (&dialog->format));
	}
      g_free (discrete_text);
    }


  if (gtk_toggle_button_get_active (dialog->button_none))
    mv_clear (&dialog->mvl);

  return TRUE;
}


/* Callback which occurs when the 'discrete' radiobutton is toggled */
static void
discrete (GtkToggleButton *button, gpointer data)
{
  gint i;
  PsppireMissingValDialog *dialog = data;

  for (i = 0 ; i < 3 ; ++i )
    {
      gtk_widget_set_sensitive (dialog->mv[i],
			       gtk_toggle_button_get_active (button));
    }
}

/* Callback which occurs when the 'range' radiobutton is toggled */
static void
range (GtkToggleButton *button, gpointer data)
{
  PsppireMissingValDialog *dialog = data;

  const gboolean active = gtk_toggle_button_get_active (button);

  gtk_widget_set_sensitive (dialog->low, active);
  gtk_widget_set_sensitive (dialog->high, active);
  gtk_widget_set_sensitive (dialog->discrete, active);
}



/* Shows the dialog box and sets default values */
static GObject *
psppire_missing_val_dialog_constructor (GType                  type,
                                        guint                  n_properties,
                                        GObjectConstructParam *properties)
{
  PsppireMissingValDialog *dialog;
  GtkContainer *content_area;
  GtkBuilder *xml;
  GObject *obj;

  obj = G_OBJECT_CLASS (psppire_missing_val_dialog_parent_class)->constructor (
    type, n_properties, properties);
  dialog = PSPPIRE_MISSING_VAL_DIALOG (obj);

  content_area = GTK_CONTAINER (PSPPIRE_DIALOG (dialog)->box);
  xml = builder_new ("missing-val-dialog.ui");
  gtk_container_add (GTK_CONTAINER (content_area),
                     get_widget_assert (xml, "missing-values-dialog"));

  dialog->mv[0] = get_widget_assert (xml, "mv0");
  dialog->mv[1] = get_widget_assert (xml, "mv1");
  dialog->mv[2] = get_widget_assert (xml, "mv2");

  dialog->low = get_widget_assert (xml, "mv-low");
  dialog->high = get_widget_assert (xml, "mv-high");
  dialog->discrete = get_widget_assert (xml, "mv-discrete");


  dialog->button_none     =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "no_missing"));

  dialog->button_discrete =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "discrete_missing"));

  dialog->button_range    =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "range_missing"));

  psppire_dialog_set_accept_predicate (PSPPIRE_DIALOG (dialog),
                                       missing_val_dialog_acceptable,
                                       dialog);

  g_signal_connect (dialog->button_discrete, "toggled",
		   G_CALLBACK (discrete), dialog);

  g_signal_connect (dialog->button_range, "toggled",
		   G_CALLBACK (range), dialog);

  g_object_unref (xml);

  return obj;
}

void
psppire_missing_val_dialog_set_variable (PsppireMissingValDialog *dialog,
                                         const struct variable *var)
{
  enum val_type var_type;
  gint i;

  mv_destroy (&dialog->mvl);
  g_free (dialog->encoding);

  if (var != NULL)
    {
      mv_copy (&dialog->mvl, var_get_missing_values (var));
      dialog->encoding = g_strdup (var_get_encoding (var));
      dialog->format = *var_get_print_format (var);
    }
  else
    {
      mv_init (&dialog->mvl, 0);
      dialog->encoding = NULL;
      dialog->format = F_8_0;
    }

  /* Blank all entry boxes and make them insensitive */
  gtk_entry_set_text (GTK_ENTRY (dialog->low), "");
  gtk_entry_set_text (GTK_ENTRY (dialog->high), "");
  gtk_entry_set_text (GTK_ENTRY (dialog->discrete), "");
  gtk_widget_set_sensitive (dialog->low, FALSE);
  gtk_widget_set_sensitive (dialog->high, FALSE);
  gtk_widget_set_sensitive (dialog->discrete, FALSE);

  var_type = val_type_from_width (fmt_var_width (&dialog->format));
  gtk_widget_set_sensitive (GTK_WIDGET (dialog->button_range),
			    var_type == VAL_NUMERIC);

  if (var == NULL)
    return;

  for (i = 0 ; i < 3 ; ++i )
    {
      gtk_entry_set_text (GTK_ENTRY (dialog->mv[i]), "");
      gtk_widget_set_sensitive (dialog->mv[i], FALSE);
    }

  if ( mv_has_range (&dialog->mvl))
    {
      union value low, high;
      gchar *low_text;
      gchar *high_text;
      mv_get_range (&dialog->mvl, &low.f, &high.f);


      low_text = value_to_text__ (low, &dialog->format, dialog->encoding);
      high_text = value_to_text__ (high, &dialog->format, dialog->encoding);

      gtk_entry_set_text (GTK_ENTRY (dialog->low), low_text);
      gtk_entry_set_text (GTK_ENTRY (dialog->high), high_text);
      g_free (low_text);
      g_free (high_text);

      if ( mv_has_value (&dialog->mvl))
	{
	  gchar *text;
	  text = value_to_text__ (*mv_get_value (&dialog->mvl, 0),
                                  &dialog->format, dialog->encoding);
	  gtk_entry_set_text (GTK_ENTRY (dialog->discrete), text);
	  g_free (text);
	}

      gtk_toggle_button_set_active (dialog->button_range, TRUE);
      gtk_widget_set_sensitive (dialog->low, TRUE);
      gtk_widget_set_sensitive (dialog->high, TRUE);
      gtk_widget_set_sensitive (dialog->discrete, TRUE);

    }
  else if ( mv_has_value (&dialog->mvl))
    {
      const int n = mv_n_values (&dialog->mvl);

      for (i = 0 ; i < 3 ; ++i )
	{
	  if ( i < n)
	    {
	      gchar *text ;

	      text = value_to_text__ (*mv_get_value (&dialog->mvl, i),
                                      &dialog->format, dialog->encoding);
	      gtk_entry_set_text (GTK_ENTRY (dialog->mv[i]), text);
	      g_free (text);
	    }
	  gtk_widget_set_sensitive (dialog->mv[i], TRUE);
	}
      gtk_toggle_button_set_active (dialog->button_discrete, TRUE);
    }
  else if ( mv_is_empty (&dialog->mvl))
    {
      gtk_toggle_button_set_active (dialog->button_none, TRUE);
    }
}

const struct missing_values *
psppire_missing_val_dialog_get_missing_values (
  const PsppireMissingValDialog *dialog)
{
  return &dialog->mvl;
}
