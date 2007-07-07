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


#ifndef __PSPPIRE_HBBOX_H__
#define __PSPPIRE_HBBOX_H__


#include <glib.h>
#include <glib-object.h>
#include "psppire-buttonbox.h"

G_BEGIN_DECLS

#define PSPPIRE_HBUTTON_BOX_TYPE            (psppire_hbutton_box_get_type ())
#define PSPPIRE_HBUTTON_BOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_HBUTTON_BOX_TYPE, PsppireHButtonBox))
#define PSPPIRE_HBUTTON_BOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_HBUTTON_BOX_TYPE, PsppireHButtonBoxClass))
#define PSPPIRE_IS_HBUTTON_BOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_HBUTTON_BOX_TYPE))
#define PSPPIRE_IS_HBUTTON_BOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_HBUTTON_BOX_TYPE))


typedef struct _PsppireHButtonBox       PsppireHButtonBox;
typedef struct _PsppireHButtonBoxClass  PsppireHButtonBoxClass;

struct _PsppireHButtonBox
{
  PsppireButtonBox parent;
};

struct _PsppireHButtonBoxClass
{
  PsppireButtonBoxClass parent_class;
};

GType          psppire_hbutton_box_get_type        (void);
GtkWidget*     psppire_hbutton_box_new             (void);

G_END_DECLS

#endif /* __PSPPIRE_HBBOX_H__ */

