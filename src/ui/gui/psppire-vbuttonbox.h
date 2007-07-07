/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007  Free Software Foundation

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


#ifndef __PSPPIRE_VBBOX_H__
#define __PSPPIRE_VBBOX_H__


#include <glib.h>
#include <glib-object.h>
#include "psppire-buttonbox.h"

G_BEGIN_DECLS

#define PSPPIRE_VBUTTON_BOX_TYPE            (psppire_vbutton_box_get_type ())
#define PSPPIRE_VBUTTON_BOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_VBUTTON_BOX_TYPE, PsppireVButtonBox))
#define PSPPIRE_VBUTTON_BOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_VBUTTON_BOX_TYPE, PsppireVButtonBoxClass))
#define PSPPIRE_IS_VBUTTON_BOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_VBUTTON_BOX_TYPE))
#define PSPPIRE_IS_VBUTTON_BOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_VBUTTON_BOX_TYPE))


typedef struct _PsppireVButtonBox       PsppireVButtonBox;
typedef struct _PsppireVButtonBoxClass  PsppireVButtonBoxClass;

struct _PsppireVButtonBox
{
  PsppireButtonBox parent;
};

struct _PsppireVButtonBoxClass
{
  PsppireButtonBoxClass parent_class;
};

GType          psppire_vbutton_box_get_type        (void);
GtkWidget*     psppire_vbutton_box_new             (void);

G_END_DECLS

#endif /* __PSPPIRE_VBBOX_H__ */

