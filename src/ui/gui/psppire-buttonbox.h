/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011, 2012  Free Software Foundation

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


#ifndef __PSPPIRE_BUTTONBOX_H__
#define __PSPPIRE_BUTTONBOX_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PSPPIRE_BUTTONBOX_TYPE            (psppire_button_box_get_type ())
#define PSPPIRE_BUTTONBOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_BUTTONBOX_TYPE, PsppireButtonBox))
#define PSPPIRE_BUTTONBOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_BUTTONBOX_TYPE, PsppireButtonBoxClass))
#define PSPPIRE_IS_BUTTONBOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_BUTTONBOX_TYPE))
#define PSPPIRE_IS_BUTTONBOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_BUTTONBOX_TYPE))


typedef struct _PsppireButtonBox       PsppireButtonBox;
typedef struct _PsppireButtonBoxClass  PsppireButtonBoxClass;

enum
  {
    PSPPIRE_BUTTON_OK = 0,
    PSPPIRE_BUTTON_GOTO,
    PSPPIRE_BUTTON_CONTINUE,
    PSPPIRE_BUTTON_CANCEL,
    PSPPIRE_BUTTON_CLOSE,
    PSPPIRE_BUTTON_HELP,
    PSPPIRE_BUTTON_RESET,
    PSPPIRE_BUTTON_PASTE,
    n_PsppireButtonBoxButtons
  };

struct _PsppireButtonBox
{
  GtkButtonBox parent;

  /* <private> */
  GtkWidget *button[n_PsppireButtonBoxButtons];
  guint def;
};

struct _PsppireButtonBoxClass
{
  GtkButtonBoxClass parent_class;
};

GType          psppire_button_box_get_type        (void);


/* Internal function.  Do not use */
void
_psppire_button_box_child_requisition (GtkWidget *widget,
				       int       *nvis_children,
				       int       *nvis_secondaries,
				       int       *width,
				       int       *height);

#define PSPPIRE_TYPE_BUTTON_MASK psppire_button_flags_get_type()

G_END_DECLS

#endif /* __PSPPIRE_BUTTONBOX_H__ */

