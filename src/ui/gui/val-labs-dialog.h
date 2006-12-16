/* 
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2005  Free Software Foundation
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



#ifndef __PSPPIRE_VAL_LABS_DIALOG_H
#define __PSPPIRE_VAL_LABS_DIALOG_H


/*  This module describes the behaviour of the Value Labels dialog box,
    used for input of the value labels in the variable sheet */


#include <gtk/gtk.h>
#include <glade/glade.h>


struct val_labs;

struct val_labs_dialog
{
  GtkWidget *window;


  /* The variable to be updated */
  struct variable *pv;

  /* Local copy of labels */
  struct val_labs *labs;

  /* Actions */
  GtkWidget *ok;
  GtkWidget *add_button;
  GtkWidget *remove_button;
  GtkWidget *change_button;

  /* Entry Boxes */
  GtkWidget *value_entry;
  GtkWidget *label_entry;

  /* Signal handler ids */
  gint change_handler_id;
  gint value_handler_id;

  GtkWidget *treeview;
};




struct val_labs_dialog * val_labs_dialog_create(GladeXML *xml);

void val_labs_dialog_show(struct val_labs_dialog *dialog);


#endif
