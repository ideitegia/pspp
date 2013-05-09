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

#ifndef PSPPIRE_CELL_RENDERER_BUTTON_H
#define PSPPIRE_CELL_RENDERER_BUTTON_H 1

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PSPPIRE_TYPE_CELL_RENDERER_BUTTON             (psppire_cell_renderer_button_get_type())
#define PSPPIRE_CELL_RENDERER_BUTTON(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),PSPPIRE_TYPE_CELL_RENDERER_BUTTON,PsppireCellRendererButton))
#define PSPPIRE_CELL_RENDERER_BUTTON_CLASS(class)     (G_TYPE_CHECK_CLASS_CAST ((class),PSPPIRE_TYPE_CELL_RENDERER_BUTTON,PsppireCellRendererButtonClass))
#define PSPPIRE_IS_CELL_RENDERER_BUTTON(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),PSPPIRE_TYPE_CELL_RENDERER_BUTTON))
#define PSPPIRE_IS_CELL_RENDERER_BUTTON_CLASS(class)  (G_TYPE_CHECK_CLASS_TYPE ((class),PSPPIRE_TYPE_CELL_RENDERER_BUTTON))
#define PSPPIRE_CELL_RENDERER_BUTTON_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),PSPPIRE_TYPE_CELL_RENDERER_BUTTON,PsppireCellRendererButtonClass))

typedef struct _PsppireCellRendererButton      PsppireCellRendererButton;
typedef struct _PsppireCellRendererButtonClass PsppireCellRendererButtonClass;

struct _PsppireCellRendererButton
{
  GtkCellRenderer parent;

  gboolean editable;
  gchar *label;
  gint border_width;
  gint xpad;
  gint ypad;

  gboolean slash;

  GtkWidget *button;
  guint32 click_time;
  gdouble click_x;
  gdouble click_y;

  /* Style caching. */
  GtkStyle *button_style;
  GtkStyle *label_style;
  GtkWidget *base;
  gulong style_set_handler;
  gboolean dispose_has_run;
};

struct _PsppireCellRendererButtonClass {
  GtkCellRendererClass parent_class;
};

GType psppire_cell_renderer_button_get_type (void) G_GNUC_CONST;
GtkCellRenderer* psppire_cell_renderer_button_new (void);

void psppire_cell_renderer_button_set_slash (PsppireCellRendererButton *,
                                             gboolean slash);
gboolean psppire_cell_renderer_button_get_slash (const PsppireCellRendererButton *);

G_END_DECLS

#endif /* PSPPIRE_CELL_RENDERER_BUTTON_H */
