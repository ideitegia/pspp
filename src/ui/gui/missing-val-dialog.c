/* 
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2005, 2006  Free Software Foundation
    Written by John Darrington

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA. */

/*  This module describes the behaviour of the Missing Values dialog box,
    used for input of the missing values in the variable sheet */

#include "helper.h"
#include "missing-val-dialog.h"
#include "missing-values.h"
#include "variable.h"
#include "data-in.h"
#include "psppire-variable.h"

#include <gtk/gtk.h>
#include <glade/glade.h>

#include <string.h>

#define _(A) A

/* A simple (sub) dialog box for displaying user input errors */
static void
err_dialog(const gchar *msg, GtkWindow *window)
{
  GtkWidget *label = gtk_label_new (msg);

  GtkWidget *dialog = 
    gtk_dialog_new_with_buttons ("PSPP",
				 window,
				 GTK_DIALOG_MODAL | 
				 GTK_DIALOG_DESTROY_WITH_PARENT | 
				 GTK_DIALOG_NO_SEPARATOR,
				 GTK_STOCK_OK,
				 GTK_RESPONSE_ACCEPT,
				 NULL);


  GtkWidget *icon = gtk_image_new_from_stock(GTK_STOCK_DIALOG_ERROR,
					     GTK_ICON_SIZE_DIALOG);
   
  g_signal_connect_swapped (dialog,
			    "response", 
			    G_CALLBACK (gtk_widget_destroy),
			    dialog);

  GtkWidget *hbox = gtk_hbox_new(FALSE, 10);

  gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->vbox),
		     hbox);

  gtk_box_pack_start(GTK_BOX(hbox), icon, TRUE, FALSE, 10);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 10);

  gtk_widget_show_all (dialog);
}


/* Callback which occurs when the OK button is clicked */
static void 
missing_val_dialog_accept(GtkWidget *w, gpointer data)
{
  struct missing_val_dialog *dialog = data;

  const struct fmt_spec *write_spec = psppire_variable_get_write_spec(dialog->pv);
  
  if ( gtk_toggle_button_get_active(dialog->button_discrete))
    {
      gint nvals = 0;
      gint badvals = 0;
      gint i;
      mv_set_type(&dialog->mvl, MV_NONE);
      for(i = 0 ; i < 3 ; ++i ) 
	{
	  gchar *text = 
	    g_strdup(gtk_entry_get_text(GTK_ENTRY(dialog->mv[i])));

	  union value v;
	  if ( !text || strlen(g_strstrip(text)) == 0 )
	    {
	      g_free(text);
	      continue;
	    }

	  if ( text_to_value(text, &v, *write_spec))
	    {
	      nvals++;
	      mv_add_value (&dialog->mvl, &v);
	    }
	  else 
	      badvals++;
	  g_free(text);
	}
      if ( nvals == 0 || badvals > 0 ) 
	{
	  err_dialog(_("Incorrect value for variable type"), 
		     GTK_WINDOW(dialog->window));
	  return ;
	}
    }
  
  if (gtk_toggle_button_get_active(dialog->button_range))
    {
      
      union value low_val ; 
      union value high_val;
      const gchar *low_text = gtk_entry_get_text(GTK_ENTRY(dialog->low));
      const gchar *high_text = gtk_entry_get_text(GTK_ENTRY(dialog->high));

      if ( text_to_value(low_text, &low_val, *write_spec)
	   && 
	   text_to_value(high_text, &high_val, *write_spec) ) 
	{
	  if ( low_val.f > high_val.f ) 
	    {
	      err_dialog(_("Incorrect range specification"),
			  GTK_WINDOW(dialog->window));
	      return ;
	    }
	}
      else
	{
	  err_dialog(_("Incorrect range specification"),
		      GTK_WINDOW(dialog->window));
	  return;
	}

      gchar *discrete_text = 
	g_strdup(gtk_entry_get_text(GTK_ENTRY(dialog->discrete)));


      mv_set_type(&dialog->mvl, MV_NONE);
      mv_add_num_range(&dialog->mvl, low_val.f, high_val.f);
      
      if ( discrete_text && strlen(g_strstrip(discrete_text)) > 0 )
	{
	  union value discrete_val;
	  if ( !text_to_value(discrete_text, &discrete_val, 
			      *write_spec))
	    {
	      err_dialog(_("Incorrect value for variable type"),
			 GTK_WINDOW(dialog->window) );
	      g_free(discrete_text);
	      return;
	    }
	  mv_add_value(&dialog->mvl, &discrete_val);
	}
      g_free(discrete_text);
    }

  
  if (gtk_toggle_button_get_active(dialog->button_none))
    mv_set_type(&dialog->mvl, MV_NONE);

  psppire_variable_set_missing(dialog->pv, &dialog->mvl);

  gtk_widget_hide(dialog->window);
}


/* Callback which occurs when the 'discrete' radiobutton is toggled */
static void 
discrete(GtkToggleButton *button, gpointer data)
{
  gint i;
  struct missing_val_dialog *dialog = data;

  for(i = 0 ; i < 3 ; ++i ) 
    {
      gtk_widget_set_sensitive(dialog->mv[i], 
			       gtk_toggle_button_get_active(button));
    }
}

/* Callback which occurs when the 'range' radiobutton is toggled */
static void 
range(GtkToggleButton *button, gpointer data)
{
  struct missing_val_dialog *dialog = data;
  
  const gboolean active = gtk_toggle_button_get_active (button);

  gtk_widget_set_sensitive(dialog->low, active);      
  gtk_widget_set_sensitive(dialog->high, active);      
  gtk_widget_set_sensitive(dialog->discrete, active);   
}


/* Creates the dialog structure from the xml */
struct missing_val_dialog * 
missing_val_dialog_create(GladeXML *xml)
{
  struct missing_val_dialog *dialog = g_malloc(sizeof(*dialog));

  dialog->window = get_widget_assert(xml, "missing_values_dialog");

  gtk_window_set_transient_for
    (GTK_WINDOW(dialog->window), 
     GTK_WINDOW(get_widget_assert(xml, "data_editor")));


  g_signal_connect_swapped(get_widget_assert(xml, "missing_val_cancel"),
		   "clicked", G_CALLBACK(gtk_widget_hide), dialog->window);

  g_signal_connect(get_widget_assert(xml, "missing_val_ok"),
		   "clicked", G_CALLBACK(missing_val_dialog_accept), dialog);


  dialog->mv[0] = get_widget_assert(xml, "mv0");
  dialog->mv[1] = get_widget_assert(xml, "mv1");
  dialog->mv[2] = get_widget_assert(xml, "mv2");

  dialog->low = get_widget_assert(xml, "mv-low");
  dialog->high = get_widget_assert(xml, "mv-high");
  dialog->discrete = get_widget_assert(xml, "mv-discrete");
  

  dialog->button_none     =  
    GTK_TOGGLE_BUTTON(get_widget_assert(xml, "no_missing"));

  dialog->button_discrete =  
    GTK_TOGGLE_BUTTON(get_widget_assert(xml, "discrete_missing"));

  dialog->button_range    =  
    GTK_TOGGLE_BUTTON(get_widget_assert(xml, "range_missing"));


  g_signal_connect(G_OBJECT(dialog->button_discrete), "toggled", 
		   G_CALLBACK(discrete), dialog);

  g_signal_connect(G_OBJECT(dialog->button_range), "toggled", 
		   G_CALLBACK(range), dialog);

  return dialog;
}

/* Shows the dialog box and sets default values */
void 
missing_val_dialog_show(struct missing_val_dialog *dialog)
{
  gint i;
  g_return_if_fail(dialog);
  g_return_if_fail(dialog->pv);

  mv_copy (&dialog->mvl, psppire_variable_get_missing(dialog->pv));

  const struct fmt_spec *write_spec = psppire_variable_get_write_spec(dialog->pv);

  /* Blank all entry boxes and make them insensitive */
  gtk_entry_set_text(GTK_ENTRY(dialog->low), "");
  gtk_entry_set_text(GTK_ENTRY(dialog->high), "");
  gtk_entry_set_text(GTK_ENTRY(dialog->discrete), "");   
  gtk_widget_set_sensitive(dialog->low, FALSE);      
  gtk_widget_set_sensitive(dialog->high, FALSE);      
  gtk_widget_set_sensitive(dialog->discrete, FALSE);   

  gtk_widget_set_sensitive(GTK_WIDGET(dialog->button_range), 
			   psppire_variable_get_type(dialog->pv) == NUMERIC);

  for(i = 0 ; i < 3 ; ++i ) 
    {
      gtk_entry_set_text(GTK_ENTRY(dialog->mv[i]), "");	  
      gtk_widget_set_sensitive(dialog->mv[i], FALSE);
    }

  if ( mv_has_range (&dialog->mvl))
    {
      union value low, high;
      gchar *low_text;
      gchar *high_text;
      mv_peek_range(&dialog->mvl, &low.f, &high.f);

      low_text = value_to_text(low, *write_spec);
      high_text = value_to_text(high, *write_spec);
      
      gtk_entry_set_text(GTK_ENTRY(dialog->low), low_text);
      gtk_entry_set_text(GTK_ENTRY(dialog->high), high_text);
      g_free(low_text);
      g_free(high_text);

      if ( mv_has_value(&dialog->mvl))
	{
	  gchar *text;
	  union value value;
	  mv_peek_value(&dialog->mvl, &value, 0);
	  text = value_to_text(value, *write_spec);
	  gtk_entry_set_text(GTK_ENTRY(dialog->discrete), text);
	  g_free(text);
	}
      
      gtk_toggle_button_set_active(dialog->button_range, TRUE);
      gtk_widget_set_sensitive(dialog->low, TRUE);      
      gtk_widget_set_sensitive(dialog->high, TRUE);      
      gtk_widget_set_sensitive(dialog->discrete, TRUE);   

    }
  else if ( mv_has_value (&dialog->mvl))
    {
      const int n = mv_n_values (&dialog->mvl);

      for(i = 0 ; i < 3 ; ++i ) 
	{
	  if ( i < n)
	    {
	      union value value;

	      mv_peek_value(&dialog->mvl, &value, i);
	      gchar *text = value_to_text(value, *write_spec);
	      gtk_entry_set_text(GTK_ENTRY(dialog->mv[i]), text);
	      g_free(text);
	    }
	  gtk_widget_set_sensitive(dialog->mv[i], TRUE);
	}
      gtk_toggle_button_set_active(dialog->button_discrete, TRUE);
    }
  else if ( mv_is_empty (&dialog->mvl))
    {
      gtk_toggle_button_set_active(dialog->button_none, TRUE);
    }

  gtk_widget_show(dialog->window);
}
