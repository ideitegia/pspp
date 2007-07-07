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


#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <gtk/gtk.h>

enum window_type
  {
    WINDOW_DATA,
    WINDOW_SYNTAX
  };


struct editor_window
 {
  GtkWindow *window;      /* The top level window of the editor */
  gchar *name;            /* The name of this editor (UTF-8) */
  enum window_type type;
 } ;

struct editor_window * window_create (enum window_type type,
				      const gchar *name);

const gchar * window_name (const struct editor_window *);

/* Set the name of this window based on FILENAME.
   FILENAME is in "filename encoding" */
void window_set_name_from_filename (struct editor_window *e,
				    const gchar *filename);

void default_window_name (struct editor_window *w);

void minimise_all_windows (void);


#endif
