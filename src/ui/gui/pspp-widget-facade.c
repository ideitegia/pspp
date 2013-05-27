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

#include <config.h>

#include "pspp-widget-facade.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static void
inset_rectangle (const GdkRectangle *src,
                 const GtkBorder *inset,
                 GdkRectangle *dst)
{
  dst->x = src->x + inset->left;
  dst->y = src->y + inset->top;
  dst->width = MAX (1, src->width - inset->left - inset->right);
  dst->height = MAX (1, src->height - inset->top - inset->bottom);
}

static void
thicken_border (gint x, gint y, GtkBorder *border)
{
  border->left += x;
  border->right += x;
  border->top += y;
  border->bottom += y;
}

GtkStyle *
facade_get_style (GtkWidget *base,
                  GType type1,
                  ...)
{
  GString path, class_path;
  GType final_type;
  GtkStyle *style;
  va_list args;
  GType type;

  gtk_widget_path (base, NULL, &path.str, NULL);
  path.len = path.allocated_len = strlen (path.str);

  gtk_widget_class_path (base, NULL, &class_path.str, NULL);
  class_path.len = class_path.allocated_len = strlen (class_path.str);

  va_start (args, type1);
  for (type = final_type = type1; type != 0; type = va_arg (args, GType))
    {
      const gchar *type_name = g_type_name (type);
      g_string_append_printf (&path, ".%s", type_name);
      g_string_append_printf (&class_path, ".%s", type_name);
      final_type = type;
    }
  va_end (args);

  style = gtk_rc_get_style_by_paths (gtk_widget_get_settings (base),
                                     path.str, class_path.str, final_type);

  free (path.str);
  free (class_path.str);

  return style;
}

void
facade_hbox_get_base_size_request (gint border_width,
                                   gint spacing,
                                   gint n_children,
                                   GtkRequisition *request)
{
  request->width = border_width * 2;
  if (n_children > 1)
    request->width += spacing * (n_children - 1);

  request->height = border_width * 2;
}

void
facade_hbox_add_child_size_request (gint hbox_border_width,
                                    const GtkRequisition *child_request,
                                    gint child_padding,
                                    GtkRequisition *request)
{
  request->width += child_request->width + child_padding * 2;
  request->height = MAX (request->height,
                         hbox_border_width * 2 + child_request->height);
}

void
facade_arrow_get_size_request (gint xpad,
                               gint ypad,
                               GtkRequisition *request)
{
#define MIN_ARROW_SIZE 15
  request->width = MIN_ARROW_SIZE + xpad * 2;
  request->height = MIN_ARROW_SIZE + ypad * 2;
}

void
facade_alignment_get_size_request (gint border_width,
                                   gint padding_left,
                                   gint padding_right,
                                   gint padding_top,
                                   gint padding_bottom,
                                   const GtkRequisition *child_request,
                                   GtkRequisition *request)
{
  request->width = (border_width * 2 + padding_left + padding_right
                    + child_request->width);
  request->height = (border_width * 2 + padding_top + padding_bottom
                     + child_request->height);
}

void
facade_label_get_size_request (gint xpad,
                               gint ypad,
                               GtkWidget *base,
                               const char *text,
                               GtkRequisition *request)
{
  PangoLayout *layout;

  layout = facade_label_get_layout (base, text);
  facade_label_get_size_request_from_layout (xpad, ypad, layout, request);
  g_object_unref (layout);
}

void
facade_label_get_size_request_from_layout (gint xpad,
                                           gint ypad,
                                           PangoLayout *layout,
                                           GtkRequisition *request)
{
  PangoRectangle logical_rect;

  pango_layout_get_extents (layout, NULL, &logical_rect);
  request->width = xpad * 2 + PANGO_PIXELS (logical_rect.width);
  request->height = ypad * 2 + PANGO_PIXELS (logical_rect.height);
}

PangoLayout *
facade_label_get_layout (GtkWidget *base,
                         const char *text)
{
  PangoAlignment alignment;
  PangoLayout *layout;
  gboolean rtl;

  rtl = gtk_widget_get_direction (base) == GTK_TEXT_DIR_RTL;
  alignment = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;

  layout = gtk_widget_create_pango_layout (base, text);
  pango_layout_set_alignment (layout, alignment);

  return layout;
}

static void
facade_button_get_inner_border (GtkStyle *button_style,
                                GtkBorder *inner_border)
{
  GtkBorder *tmp_border;

  gtk_style_get (button_style, GTK_TYPE_BUTTON,
                 "inner-border", &tmp_border,
                 NULL);

  if (tmp_border)
    {
      *inner_border = *tmp_border;
      gtk_border_free (tmp_border);
    }
  else
    {
      static const GtkBorder default_inner_border = { 1, 1, 1, 1 };
      *inner_border = default_inner_border;
    }
}

void
facade_button_get_size_request (gint border_width,
                                GtkWidget *base,
                                GtkStyle *button_style,
                                const GtkRequisition *child_request,
                                GtkRequisition *request)
{
  GtkBorder inner_border;
  gint common_width;
  gint focus_width;
  gint focus_pad;

  gtk_style_get (button_style, GTK_TYPE_BUTTON,
                 "focus-line-width", &focus_width,
                 "focus-padding", &focus_pad,
                 NULL);
  facade_button_get_inner_border (button_style, &inner_border);

  common_width = 2 * (border_width + focus_width + focus_pad);
  request->width = (common_width
                    + 2 * button_style->xthickness
                    + inner_border.left + inner_border.right
                    + child_request->width);
  request->height = (common_width
                     + 2 * button_style->ythickness
                     + inner_border.top + inner_border.bottom
                     + child_request->height);
}

void
facade_button_get_focus_inset (gint border_width,
                               GtkWidget *base,
                               GtkStyle *button_style,
                               GtkBorder *focus_inset)
{
  facade_button_get_inner_border (button_style, focus_inset);
  thicken_border (border_width + button_style->xthickness,
                  border_width + button_style->ythickness,
                  focus_inset);
}

static void
facade_button_get_label_inset (gint border_width,
                               GtkWidget *base,
                               GtkStyle *button_style,
                               GtkBorder *label_inset)
{
  gint focus_width;
  gint focus_pad;

  facade_button_get_focus_inset (border_width, base, button_style,
                                 label_inset);

  gtk_style_get (button_style, GTK_TYPE_BUTTON,
                 "focus-line-width", &focus_width,
                 "focus-padding", &focus_pad,
                 NULL);
  thicken_border (focus_width + focus_pad,
                  focus_width + focus_pad,
                  label_inset);
}

static void
get_layout_location (GtkWidget *base,
                     const GdkRectangle *label_area,
                     PangoLayout *layout,
                     gint xpad,
                     gint ypad,
                     gfloat xalign,
                     gfloat yalign,
                     gint *x,
                     gint *y)
{
  PangoRectangle logical;
  GtkRequisition req;

  if (gtk_widget_get_direction (base) == GTK_TEXT_DIR_LTR)
    xalign = xalign;
  else
    xalign = 1.0 - xalign;

  pango_layout_get_pixel_extents (layout, NULL, &logical);

  facade_label_get_size_request_from_layout (xpad, ypad, layout, &req);

  *x = floor (label_area->x + xpad + xalign * (label_area->width - req.width));

  if (gtk_widget_get_direction (base) == GTK_TEXT_DIR_LTR)
    *x = MAX (*x, label_area->x + xpad);
  else
    *x = MIN (*x, label_area->x + label_area->width - xpad);
  *x -= logical.x;

  /* bgo#315462 - For single-line labels, *do* align the requisition with
   * respect to the allocation, even if we are under-allocated.  For multi-line
   * labels, always show the top of the text when they are under-allocated.
   * The rationale is this:
   *
   * - Single-line labels appear in GtkButtons, and it is very easy to get them
   *   to be smaller than their requisition.  The button may clip the label,
   *   but the label will still be able to show most of itself and the focus
   *   rectangle.  Also, it is fairly easy to read a single line of clipped
   *   text.
   *
   * - Multi-line labels should not be clipped to showing "something in the
   *   middle".  You want to read the first line, at least, to get some
   *   context.
   */
  if (pango_layout_get_line_count (layout) == 1)
    *y = floor (label_area->y + ypad
                + (label_area->height - req.height) * yalign);
  else
    *y = floor (label_area->y + ypad
                + MAX (((label_area->height - req.height) * yalign),
                       0));
}

void
facade_button_render (GtkWidget *base,
                      GdkWindow   *window,
                      GdkRectangle *expose_area,

                      GdkRectangle *button_area,
                      gint border_width,
                      GtkStyle *button_style,
                      GtkStateType state_type,

                      GtkStyle *label_style,
                      const gchar *label,
                      gint xpad,
                      gint ypad,
                      gfloat xalign,
                      gfloat yalign)
{
  GdkRectangle label_area;
  PangoLayout *layout;
  GtkBorder inset;
  gint x, y;

  /* Paint the button. */
  gtk_paint_box (button_style, window,
                 state_type,
                 GTK_SHADOW_OUT, expose_area, base, "button",
                 button_area->x + border_width,
                 button_area->y + border_width,
                 button_area->width - border_width * 2,
                 button_area->height - border_width * 2);

  /* Figure out where the label should go. */
  facade_button_get_label_inset (border_width, base, button_style, &inset);
  inset_rectangle (button_area, &inset, &label_area);

  /* Paint the label. */
  layout = facade_label_get_layout (base, label);
  get_layout_location (base, &label_area, layout, xpad, ypad, xalign, yalign,
                       &x, &y);
  gtk_paint_layout (label_style, window, state_type, FALSE, expose_area,
                    base, "label", x, y, layout);
  g_object_unref (layout);
}

