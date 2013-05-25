/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

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

#ifndef PSPPIRE_BUTTON_EDITABLE_H
#define PSPPIRE_BUTTON_EDITABLE_H 1

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PSPPIRE_TYPE_BUTTON_EDITABLE             (psppire_button_editable_get_type())
#define PSPPIRE_BUTTON_EDITABLE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),PSPPIRE_TYPE_BUTTON_EDITABLE,PsppireButtonEditable))
#define PSPPIRE_BUTTON_EDITABLE_CLASS(class)     (G_TYPE_CHECK_CLASS_CAST ((class),PSPPIRE_TYPE_BUTTON_EDITABLE,PsppireButtonEditableClass))
#define PSPPIRE_IS_BUTTON_EDITABLE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),PSPPIRE_TYPE_BUTTON_EDITABLE))
#define PSPPIRE_IS_BUTTON_EDITABLE_CLASS(class)  (G_TYPE_CHECK_CLASS_TYPE ((class),PSPPIRE_TYPE_BUTTON_EDITABLE))
#define PSPPIRE_BUTTON_EDITABLE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),PSPPIRE_TYPE_BUTTON_EDITABLE,PsppireButtonEditableClass))

typedef struct _PsppireButtonEditable      PsppireButtonEditable;
typedef struct _PsppireButtonEditableClass PsppireButtonEditableClass;

struct _PsppireButtonEditable {
  GtkButton parent;
  gchar *path;
};

struct _PsppireButtonEditableClass {
  GtkButtonClass parent_class;
};

GType psppire_button_editable_get_type (void) G_GNUC_CONST;
PsppireButtonEditable* psppire_button_editable_new (void);

G_END_DECLS

#endif /* PSPPIRE_BUTTON_EDITABLE_H */
