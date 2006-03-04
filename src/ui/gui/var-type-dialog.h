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


#ifndef __PSPPIRE_VAR_TYPE_DIALOG_H
#define __PSPPIRE_VAR_TYPE_DIALOG_H


/*  This module describes the behaviour of the Variable Type dialog box,
    used for input of the variable type parameter in the var sheet */

#include "format.h"

enum 
  {
    BUTTON_NUMERIC,
    BUTTON_COMMA,
    BUTTON_DOT,
    BUTTON_SCIENTIFIC,
    BUTTON_DATE,
    BUTTON_DOLLAR,
    BUTTON_CUSTOM,
    BUTTON_STRING,
    num_BUTTONS
  };

struct variable;

typedef void (*variable_changed_func)(struct variable *var);

struct var_type_dialog
{
  GtkWidget *window;

  /* Variable to be updated */
  struct PsppireVariable *pv;
#if 0
  struct variable *var;

  /* Function to be run when the dialog changes a variable */
  variable_changed_func var_change_func;
#endif

  /* Local copy of format specifier */
  struct fmt_spec fmt_l;

  /* Toggle Buttons */
  GtkWidget *radioButton[num_BUTTONS];

  /* Decimals */
  GtkWidget *label_decimals;
  GtkWidget *entry_decimals;

  /* Width */
  GtkWidget *entry_width;

  /* Container for width/decimals entry/labels */
  GtkWidget *width_decimals;

  /* Date */
  GtkWidget *date_format_list;
  GtkTreeView *date_format_treeview;

  /* Dollar */
  GtkWidget *dollar_window;
  GtkTreeView *dollar_treeview;

  /* Custom Currency */
  GtkWidget *custom_currency_hbox;
  GtkTreeView *custom_treeview;
  GtkWidget *label_psample;
  GtkWidget *label_nsample;

  /* Actions */
  GtkWidget *ok;

  gint active_button;
};


struct var_type_dialog * var_type_dialog_create(GladeXML *xml);


void var_type_dialog_set_variable(struct var_type_dialog *dialog, 
				  variable_changed_func set_variable_changed,
				  struct variable *var);

void var_type_dialog_show(struct var_type_dialog *dialog);


#endif
