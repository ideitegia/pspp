/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2007  Free Software Foundation

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


#include <config.h>

#include <string.h>

#include <glade/glade-build.h>
#include "psppire-dialog.h"
#include "psppire-selector.h"
#include "psppire-keypad.h"
#include "psppire-hbuttonbox.h"
#include "psppire-vbuttonbox.h"

GLADE_MODULE_CHECK_INIT

/* Glade registration functions for PSPPIRE custom widgets */

static GtkWidget *
dialog_find_internal_child (GladeXML *xml,
			    GtkWidget *parent,
			    const gchar *childname)
{
  if (!strcmp(childname, "hbox"))
    return PSPPIRE_DIALOG (parent)->box;

  return NULL;
}

void
glade_module_register_widgets (void)
{
  glade_register_widget (PSPPIRE_DIALOG_TYPE, NULL,
			 glade_standard_build_children,
			 dialog_find_internal_child);


  glade_register_widget (PSPPIRE_VBUTTON_BOX_TYPE, NULL,
			 glade_standard_build_children,
			 NULL);

  glade_register_widget (PSPPIRE_HBUTTON_BOX_TYPE, NULL,
			 glade_standard_build_children,
			 NULL);

  glade_register_widget (PSPPIRE_SELECTOR_TYPE, NULL,
			 glade_standard_build_children,
			 NULL);

  glade_register_widget (PSPPIRE_KEYPAD_TYPE, NULL,
			 glade_standard_build_children,
			 NULL);

}



