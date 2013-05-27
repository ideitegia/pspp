/*******************************************************************************
**3456789 123456789 123456789 123456789 123456789 123456789 123456789 123456789 
**      10        20        30        40        50        60        70        80
**
**  library for GtkXPaned-widget, a 2x2 grid-like variation of GtkPaned of gtk+
**  Copyright (C) 2005-2006 Mirco "MacSlow" Müller <macslow@bangang.de>
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2.1 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
**
**  GtkXPaned is based on GtkPaned which was done by...
**
**  "Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald"
**
**  and later modified by...
**
**  "the GTK+ Team and others 1997-2000"
**
*******************************************************************************/

#ifndef GTK_XPANED_H
#define GTK_XPANED_H

#include <gtk/gtk.h>

G_BEGIN_DECLS
#define GTK_TYPE_XPANED                  (gtk_xpaned_get_type ())
#define GTK_XPANED(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_XPANED, GtkXPaned))
#define GTK_XPANED_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_XPANED, GtkXPanedClass))
#define GTK_IS_XPANED(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_XPANED))
#define GTK_IS_XPANED_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_XPANED))
#define GTK_XPANED_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_XPANED, GtkXPanedClass))
typedef struct _GtkXPaned GtkXPaned;
typedef struct _GtkXPanedClass GtkXPanedClass;
typedef struct _GtkXPanedPrivate GtkXPanedPrivate;

typedef enum _GtkXPanedChild
{
  GTK_XPANED_TOP_LEFT = 0,
  GTK_XPANED_TOP_RIGHT,
  GTK_XPANED_BOTTOM_LEFT,
  GTK_XPANED_BOTTOM_RIGHT
} GtkXPanedChild;

struct _GtkXPaned
{
  GtkContainer container;

  GtkWidget *top_left_child;
  GtkWidget *top_right_child;
  GtkWidget *bottom_left_child;
  GtkWidget *bottom_right_child;

  GdkWindow *handle_east;
  GdkWindow *handle_west;
  GdkWindow *handle_north;
  GdkWindow *handle_south;
  GdkWindow *handle_middle;

  GdkCursorType cursor_type_east;
  GdkCursorType cursor_type_west;
  GdkCursorType cursor_type_north;
  GdkCursorType cursor_type_south;
  GdkCursorType cursor_type_middle;

  /*< private > */
  GdkRectangle handle_pos_east;
  GdkRectangle handle_pos_west;
  GdkRectangle handle_pos_north;
  GdkRectangle handle_pos_south;
  GdkRectangle handle_pos_middle;
  GtkRequisition top_left_child_size;
  GtkRequisition top_right_child_size;
  GtkRequisition bottom_left_child_size;
  GtkRequisition bottom_right_child_size;

  GtkRequisition last_allocation;
  GdkPoint min_position;
  GdkPoint max_position;
  gboolean maximized[4];

  guint position_set:1;
  guint in_drag_vert:1;
  guint in_drag_horiz:1;
  guint in_drag_vert_and_horiz:1;
  guint top_left_child_shrink:1;
  guint top_left_child_resize:1;
  guint top_right_child_shrink:1;
  guint top_right_child_resize:1;
  guint bottom_left_child_shrink:1;
  guint bottom_left_child_resize:1;
  guint bottom_right_child_shrink:1;
  guint bottom_right_child_resize:1;
  guint in_recursion:1;
  guint handle_prelit:1;

  GtkWidget *last_top_left_child_focus;
  GtkWidget *last_top_right_child_focus;
  GtkWidget *last_bottom_left_child_focus;
  GtkWidget *last_bottom_right_child_focus;
  GtkXPanedPrivate *priv;

  GdkPoint drag_pos;
  GdkPoint original_position;
  GdkPoint unmaximized_position;
};

struct _GtkXPanedClass
{
  GtkContainerClass parent_class;
    gboolean (*cycle_child_focus) (GtkXPaned * xpaned, gboolean reverse);
    gboolean (*toggle_handle_focus) (GtkXPaned * xpaned);
    gboolean (*move_handle) (GtkXPaned * xpaned, GtkScrollType scroll);
    gboolean (*cycle_handle_focus) (GtkXPaned * xpaned, gboolean reverse);
    gboolean (*accept_position) (GtkXPaned * xpaned);
    gboolean (*cancel_position) (GtkXPaned * xpaned);
};

GType
gtk_xpaned_get_type (void)
  G_GNUC_CONST;
     GtkWidget *gtk_xpaned_new (void);
     void gtk_xpaned_add_top_left (GtkXPaned * xpaned, GtkWidget * child);
     void gtk_xpaned_add_top_right (GtkXPaned * xpaned, GtkWidget * child);
     void gtk_xpaned_add_bottom_left (GtkXPaned * xpaned, GtkWidget * child);
     void gtk_xpaned_add_bottom_right (GtkXPaned * xpaned, GtkWidget * child);
     void gtk_xpaned_pack_top_left (GtkXPaned * xpaned, GtkWidget * child,
                                    gboolean resize, gboolean shrink);
     void gtk_xpaned_pack_top_right (GtkXPaned * xpaned, GtkWidget * child,
                                     gboolean resize, gboolean shrink);
     void gtk_xpaned_pack_bottom_left (GtkXPaned * xpaned, GtkWidget * child,
                                       gboolean resize, gboolean shrink);
     void gtk_xpaned_pack_bottom_right (GtkXPaned * xpaned, GtkWidget * child,
                                        gboolean resize, gboolean shrink);
     gint gtk_xpaned_get_position_x (GtkXPaned * xpaned);
     gint gtk_xpaned_get_position_y (GtkXPaned * xpaned);
     void gtk_xpaned_set_position_x (GtkXPaned * xpaned, gint xposition);
     void gtk_xpaned_set_position_y (GtkXPaned * xpaned, gint yposition);
     void gtk_xpaned_save_unmaximized_x (GtkXPaned * xpaned);
     void gtk_xpaned_save_unmaximized_y (GtkXPaned * xpaned);
     gint gtk_xpaned_fetch_unmaximized_x (GtkXPaned * xpaned);
     gint gtk_xpaned_fetch_unmaximized_y (GtkXPaned * xpaned);
     GtkWidget *gtk_xpaned_get_top_left_child (GtkXPaned * xpaned);
     GtkWidget *gtk_xpaned_get_top_right_child (GtkXPaned * xpaned);
     GtkWidget *gtk_xpaned_get_bottom_right_child (GtkXPaned * xpaned);
     GtkWidget *gtk_xpaned_get_bottom_left_child (GtkXPaned * xpaned);
     gboolean gtk_xpaned_maximize_top_left (GtkXPaned * xpaned,
                                            gboolean maximize);
     gboolean gtk_xpaned_maximize_top_right (GtkXPaned * xpaned,
                                             gboolean maximize);
     gboolean gtk_xpaned_maximize_bottom_left (GtkXPaned * xpaned,
                                               gboolean maximize);
     gboolean gtk_xpaned_maximize_bottom_right (GtkXPaned * xpaned,
                                                gboolean maximize);

/* Internal function */
#if !defined (GTK_DISABLE_DEPRECATED) || defined (GTK_COMPILATION)
     void gtk_xpaned_compute_position (GtkXPaned * xpaned,
                                       const GtkAllocation * allocation,
                                       GtkRequisition * top_left_child_req,
                                       GtkRequisition * top_right_child_req,
                                       GtkRequisition * bottom_left_child_req,
                                       GtkRequisition *
                                       bottom_right_child_req);
#endif /* !GTK_DISABLE_DEPRECATED || GTK_COMPILATION */
#ifndef GTK_DISABLE_DEPRECATED
#define	gtk_xpaned_gutter_size(p,s) (void) 0
#define	gtk_xpaned_set_gutter_size(p,s) (void) 0
#endif /* GTK_DISABLE_DEPRECATED */

G_END_DECLS
#endif /* GTK_XPANED_H */
