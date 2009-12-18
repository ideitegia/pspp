/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009  Free Software Foundation

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

#ifndef __PSPPIRE_SELECT_DEST_H__
#define __PSPPIRE_SELECT_DEST_H__

#include <glib-object.h>

GType              psppire_select_dest_widget_get_type   (void) G_GNUC_CONST;

#define PSPPIRE_TYPE_SELECT_DEST_WIDGET      (psppire_select_dest_widget_get_type ())
#define PSPPIRE_SELECT_DEST_WIDGET(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_TYPE_SELECT_DEST_WIDGET, PsppireSelectDestWidget))
#define PSPPIRE_IS_SELECT_DEST_WIDGET(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_SELECT_DEST_WIDGET))


#define PSPPIRE_SELECT_DEST_GET_IFACE(obj) \
   (G_TYPE_INSTANCE_GET_INTERFACE ((obj), PSPPIRE_TYPE_SELECT_DEST_WIDGET, PsppireSelectDestWidgetIface))

typedef struct _PsppireSelectDestWidgetIface  PsppireSelectDestWidgetIface;


typedef struct _PsppireSelectDestWidget  PsppireSelectDestWidget;  /* Dummy typedef */

struct _PsppireSelectDestWidgetIface
{
  GTypeInterface g_iface;

  /* Return TRUE iff DEST contains V */
  gboolean (*contains_var) (PsppireSelectDestWidget *dest, const GValue *v);
};


gboolean psppire_select_dest_widget_contains_var (PsppireSelectDestWidget *m, const GValue *v);

#endif
