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


#ifndef PSPPIRE_OUTPUT_VIEW_H
#define PSPPIRE_OUTPUT_VIEW_H

#include <gtk/gtk.h>

struct output_item;
struct string_map;

struct psppire_output_view *psppire_output_view_new (GtkLayout *output,
                                                     GtkTreeView *overview,
                                                     GtkAction *copy_action,
                                                     GtkAction *select_all_action);
void psppire_output_view_clear (struct psppire_output_view *);
void psppire_output_view_destroy (struct psppire_output_view *);

void psppire_output_view_put (struct psppire_output_view *,
                              const struct output_item *);

void psppire_output_view_export (struct psppire_output_view *,
                                 struct string_map *options);
void psppire_output_view_print (struct psppire_output_view *,
                                GtkWindow *parent_window);

void psppire_output_view_register_driver (struct psppire_output_view *);

#endif /* psppire-output-view.h */
