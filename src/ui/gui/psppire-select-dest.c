/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009, 2010  Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include "psppire-select-dest.h"
#include <gtk/gtk.h>

GType
psppire_select_dest_widget_get_type (void)
{
  static GType dest_widget_type = 0;

  if (! dest_widget_type)
    {
      const GTypeInfo dest_widget_info =
      {
        sizeof (PsppireSelectDestWidgetIface), /* class_size */
	NULL,           /* base_init */
	NULL,		/* base_finalize */
	NULL,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      dest_widget_type =
	g_type_register_static (G_TYPE_INTERFACE, "PsppireSelectDestWidget",
				&dest_widget_info, 0);

      g_type_interface_add_prerequisite (dest_widget_type, GTK_TYPE_WIDGET);
    }

  return dest_widget_type;
}


gboolean
psppire_select_dest_widget_contains_var (PsppireSelectDestWidget *sdm, const GValue *value)
{
  return PSPPIRE_SELECT_DEST_GET_IFACE (sdm)->contains_var (sdm, value);
}
