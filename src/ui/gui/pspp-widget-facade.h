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

#ifndef FACADE_WIDGET_FACADE_H
#define FACADE_WIDGET_FACADE_H 1

#include <gtk/gtk.h>

G_BEGIN_DECLS

GtkStyle *facade_get_style (GtkWidget *base, GType, ...);

void facade_hbox_get_base_size_request (gint border_width,
                                        gint spacing,
                                        gint n_children,
                                        GtkRequisition *);
void facade_hbox_add_child_size_request (gint hbox_border_width,
                                         const GtkRequisition *child_request,
                                         gint child_padding,
                                         GtkRequisition *);

void facade_arrow_get_size_request (gint xpad,
                                    gint ypad,
                                    GtkRequisition *);


void facade_alignment_get_size_request (gint border_width,
                                        gint padding_left,
                                        gint padding_right,
                                        gint padding_top,
                                        gint padding_bottom,
                                        const GtkRequisition *child_request,
                                        GtkRequisition *);

void facade_label_get_size_request (gint xpad,
                                    gint ypad,
                                    GtkWidget *base,
                                    const char *text,
                                    GtkRequisition *);
void facade_label_get_size_request_from_layout (gint xpad,
                                                gint ypad,
                                                PangoLayout *,
                                                GtkRequisition *);
PangoLayout *facade_label_get_layout (GtkWidget *base,
                                      const char *text);

void facade_button_get_size_request (gint border_width,
                                     GtkWidget *base,
                                     GtkStyle *button_style,
                                     const GtkRequisition *child_request,
                                     GtkRequisition *);
void facade_button_render (GtkWidget *base,
                           cairo_t *cr,
                           const GdkRectangle *button_area,
                           gint border_width,
                           GtkStyle *button_style,
                           GtkStateType state_type,

                           GtkStyle *label_style,
                           const gchar *label,
                           gint xpad,
                           gint ypad,
                           gfloat xalign,
                           gfloat yalign);
void facade_button_get_focus_inset (gint border_width,
                                    GtkWidget *base,
                                    GtkStyle *button_style,
                                    GtkBorder *focus_inset);

G_END_DECLS

#endif /* FACADE_WIDGET_FACADE_H */
