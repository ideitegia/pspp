/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012  Free Software Foundation

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

/*
 This is an abstract base class upon which all (well almost all) windows in
 psppire are based.   The exceptions are transient windows such as the 
 splash screen and popups.
*/

#ifndef __PSPPIRE_WINDOW_BASE_H__
#define __PSPPIRE_WINDOW_BASE_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS


#define PSPPIRE_TYPE_WINDOW_BASE            (psppire_window_base_get_type ())

#define PSPPIRE_WINDOW_BASE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
    PSPPIRE_TYPE_WINDOW_BASE, PsppireWindowBase))

#define PSPPIRE_WINDOW_BASE_CLASS(class)    (G_TYPE_CHECK_CLASS_CAST ((class), \
    PSPPIRE_TYPE_WINDOW_BASE, PsppireWindowBaseClass))

#define PSPPIRE_IS_WINDOW_BASE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    PSPPIRE_TYPE_WINDOW_BASE))

#define PSPPIRE_IS_WINDOW_BASE_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
    PSPPIRE_TYPE_WINDOW_BASE))


typedef struct _PsppireWindowBase       PsppireWindowBase;
typedef struct _PsppireWindowBaseClass  PsppireWindowBaseClass;


struct _PsppireWindowBase
{
  GtkWindow parent;

  /* <private> */
};


struct _PsppireWindowBaseClass
{
  GtkWindowClass parent_class;
};



GType      psppire_window_base_get_type        (void);
GType      psppire_window_base_model_get_type        (void);

G_END_DECLS

#endif /* __PSPPIRE_WINDOW_BASE_H__ */
