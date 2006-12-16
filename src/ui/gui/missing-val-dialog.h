/* 
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2005  Free Software Foundation

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

#ifndef __PSPPIRE_MISSING_VAL_DIALOG_H
#define __PSPPIRE_MISSING_VAL_DIALOG_H

/*  This module describes the behaviour of the Missing Values dialog box,
    used for input of the missing values in the variable sheet */


#include <gtk/gtk.h>
#include <glade/glade.h>

#include <data/missing-values.h>

struct missing_val_dialog
{
  GtkWidget *window;

  /* The variable whose missing values are to be updated */
  struct variable *pv;

  /* local copy */
  struct missing_values mvl;

  /* Radio Buttons */
  GtkToggleButton *button_none;
  GtkToggleButton *button_discrete;
  GtkToggleButton *button_range;

  /* Entry boxes */
  GtkWidget *mv[3];
  GtkWidget *low;
  GtkWidget *high;
  GtkWidget *discrete;
};

struct missing_val_dialog * missing_val_dialog_create(GladeXML *xml);

void missing_val_dialog_show(struct missing_val_dialog *dialog);

#endif
