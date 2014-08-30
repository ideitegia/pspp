/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2014  Free Software Foundation

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


#ifndef __PSPPIRE_OUTPUT_WINDOW_H__
#define __PSPPIRE_OUTPUT_WINDOW_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include "psppire-window.h"
#include "psppire.h"
#include "libpspp/string-map.h"


G_BEGIN_DECLS

#define PSPPIRE_OUTPUT_WINDOW_TYPE            (psppire_output_window_get_type ())
#define PSPPIRE_OUTPUT_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_OUTPUT_WINDOW_TYPE, PsppireOutputWindow))
#define PSPPIRE_OUTPUT_WINDOW_CLASS(class)    (G_TYPE_CHECK_CLASS_CAST ((class), \
    PSPPIRE_OUTPUT_WINDOW_TYPE, PsppireOutput_WindowClass))
#define PSPPIRE_IS_OUTPUT_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    PSPPIRE_OUTPUT_WINDOW_TYPE))
#define PSPPIRE_IS_OUTPUT_WINDOW_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
    PSPPIRE_OUTPUT_WINDOW_TYPE))


typedef struct _PsppireOutputWindow       PsppireOutputWindow;
typedef struct _PsppireOutputWindowClass  PsppireOutputWindowClass;


struct _PsppireOutputWindow
{
  PsppireWindow parent;

  /* <private> */
  struct psppire_output_driver *driver;
  struct psppire_output_view *view;

  gboolean dispose_has_run;
};

struct _PsppireOutputWindowClass
{
  PsppireWindowClass parent_class;

};

GType      psppire_output_window_get_type        (void);
GtkWidget* psppire_output_window_new             (void);

void psppire_output_window_setup (void);

G_END_DECLS

#endif /* __PSPPIRE_OUTPUT_WINDOW_H__ */
