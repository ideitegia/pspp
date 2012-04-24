/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2005, 2011  Free Software Foundation

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

#ifndef __PSPPIRE_VAL_LABS_DIALOG_H
#define __PSPPIRE_VAL_LABS_DIALOG_H


/*  This module describes the behaviour of the Value Labels dialog box,
    used for input of the value labels in the variable sheet */


#include <gtk/gtk.h>
#include <data/variable.h>
#include "psppire-var-store.h"

struct val_labs;


struct val_labs_dialog * val_labs_dialog_create (GtkWindow *);

void val_labs_dialog_show (struct val_labs_dialog *);

void val_labs_dialog_set_target_variable (struct val_labs_dialog *,
					  struct variable *);

#endif
