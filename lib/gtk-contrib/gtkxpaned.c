/*******************************************************************************
 **3456789 123456789 123456789 123456789 123456789 123456789 123456789 123456789 
 **      10        20        30        40        50        60        70        80
 **
 **  library for GtkXPaned-widget, a 2x2 grid-like variation of GtkPaned of gtk+
 **  Copyright (C) 2012, 2013 Free Software Foundation, Inc.
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

#include <config.h>
#include "gtkxpaned.h"

#include <gtk/gtk.h>
#include <ui/gui/psppire-marshal.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkkeysyms-compat.h>

enum WidgetProperties
  {
    PROP_0,
    PROP_X_POSITION,
    PROP_Y_POSITION,
    PROP_POSITION_SET,
    PROP_MIN_X_POSITION,
    PROP_MIN_Y_POSITION,
    PROP_MAX_X_POSITION,
    PROP_MAX_Y_POSITION
  };

enum ChildProperties
  {
    CHILD_PROP_0,
    CHILD_PROP_RESIZE,
    CHILD_PROP_SHRINK
  };

enum WidgetSignals
  {
    CYCLE_CHILD_FOCUS,
    TOGGLE_HANDLE_FOCUS,
    MOVE_HANDLE,
    CYCLE_HANDLE_FOCUS,
    ACCEPT_POSITION,
    CANCEL_POSITION,
    LAST_SIGNAL
  };

static void gtk_xpaned_class_init (GtkXPanedClass * klass);

static void gtk_xpaned_init (GtkXPaned * xpaned);

static void
gtk_xpaned_get_preferred_width (GtkWidget *widget,
                                gint      *minimal_width,
                                gint      *natural_width)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);
  gint tl[2], tr[2], bl[2], br[2];
  gint overhead;
  gint w[2];
  int i;

  if (xpaned->top_left_child
      && gtk_widget_get_visible (xpaned->top_left_child))
    gtk_widget_get_preferred_width (xpaned->top_left_child, &tl[0], &tl[1]);
  else
    tl[0] = tl[1] = 0;

  if (xpaned->top_right_child
      && gtk_widget_get_visible (xpaned->top_right_child))
    gtk_widget_get_preferred_width (xpaned->top_right_child, &tr[0], &tr[1]);
  else
    tr[0] = tr[1] = 0;

  if (xpaned->bottom_left_child
      && gtk_widget_get_visible (xpaned->bottom_left_child))
    gtk_widget_get_preferred_width (xpaned->bottom_left_child, &bl[0], &bl[1]);
  else
    bl[0] = bl[1] = 0;

  if (xpaned->bottom_right_child
      && gtk_widget_get_visible (xpaned->bottom_right_child))
    gtk_widget_get_preferred_width (xpaned->bottom_right_child,
                                    &br[0], &br[1]);
  else
    br[0] = br[1] = 0;

  /* add 2 times the set border-width to the GtkXPaneds requisition */
  overhead = gtk_container_get_border_width (GTK_CONTAINER (xpaned)) * 2;

  /* also add the handle "thickness" to GtkXPaned's width requisition */
  if (xpaned->top_left_child
      && gtk_widget_get_visible (xpaned->top_left_child)
      && xpaned->top_right_child
      && gtk_widget_get_visible (xpaned->top_right_child)
      && xpaned->bottom_left_child
      && gtk_widget_get_visible (xpaned->bottom_left_child)
      && xpaned->bottom_right_child
      && gtk_widget_get_visible (xpaned->bottom_right_child))
    {
      gint handle_size;

      gtk_widget_style_get (widget, "handle-size", &handle_size, NULL);
      overhead += handle_size;
    }

  for (i = 0; i < 2; i++)
    w[i] = (br[i] ? br[i] : MAX (tl[i] + tr[i], bl[i])) + overhead;

  *minimal_width = w[0];
  *natural_width = w[1];
}

static void
gtk_xpaned_get_preferred_height (GtkWidget *widget,
                                gint      *minimal_height,
                                gint      *natural_height)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);
  gint tl[2], tr[2], bl[2], br[2];
  gint overhead;
  gint h[2];
  int i;

  if (xpaned->top_left_child
      && gtk_widget_get_visible (xpaned->top_left_child))
    gtk_widget_get_preferred_height (xpaned->top_left_child, &tl[0], &tl[1]);
  else
    tl[0] = tl[1] = 0;

  if (xpaned->top_right_child
      && gtk_widget_get_visible (xpaned->top_right_child))
    gtk_widget_get_preferred_height (xpaned->top_right_child, &tr[0], &tr[1]);
  else
    tr[0] = tr[1] = 0;

  if (xpaned->bottom_left_child
      && gtk_widget_get_visible (xpaned->bottom_left_child))
    gtk_widget_get_preferred_height (xpaned->bottom_left_child,
                                     &bl[0], &bl[1]);
  else
    bl[0] = bl[1] = 0;

  if (xpaned->bottom_right_child
      && gtk_widget_get_visible (xpaned->bottom_right_child))
    gtk_widget_get_preferred_height (xpaned->bottom_right_child,
                                    &br[0], &br[1]);
  else
    br[0] = br[1] = 0;

  /* add 2 times the set border-width to the GtkXPaneds requisition */
  overhead = gtk_container_get_border_width (GTK_CONTAINER (xpaned)) * 2;

  /* also add the handle "thickness" to GtkXPaned's height-requisition */
  if (xpaned->top_left_child
      && gtk_widget_get_visible (xpaned->top_left_child)
      && xpaned->top_right_child
      && gtk_widget_get_visible (xpaned->top_right_child)
      && xpaned->bottom_left_child
      && gtk_widget_get_visible (xpaned->bottom_left_child)
      && xpaned->bottom_right_child
      && gtk_widget_get_visible (xpaned->bottom_right_child))
    {
      gint handle_size;

      gtk_widget_style_get (widget, "handle-size", &handle_size, NULL);
      overhead += handle_size;
    }

  for (i = 0; i < 2; i++)
    h[i] = (br[i] ? br[i] : bl[i] + MAX (tl[i], tr[i])) + overhead;

  *minimal_height = h[0];
  *natural_height = h[1];
}

static void gtk_xpaned_size_allocate (GtkWidget * widget,
                                      GtkAllocation * allocation);

static void gtk_xpaned_set_property (GObject * object,
                                     guint prop_id,
                                     const GValue * value,
                                     GParamSpec * pspec);

static void gtk_xpaned_get_property (GObject * object,
                                     guint prop_id,
                                     GValue * value, GParamSpec * pspec);

static void gtk_xpaned_set_child_property (GtkContainer * container,
                                           GtkWidget * child,
                                           guint property_id,
                                           const GValue * value,
                                           GParamSpec * pspec);

static void gtk_xpaned_get_child_property (GtkContainer * container,
                                           GtkWidget * child,
                                           guint property_id,
                                           GValue * value,
                                           GParamSpec * pspec);

static void gtk_xpaned_finalize (GObject * object);

static void gtk_xpaned_realize (GtkWidget * widget);

static void gtk_xpaned_unrealize (GtkWidget * widget);

static void gtk_xpaned_map (GtkWidget * widget);

static void gtk_xpaned_unmap (GtkWidget * widget);

static gboolean gtk_xpaned_draw (GtkWidget * widget,
                                   cairo_t *ct);

static gboolean gtk_xpaned_enter (GtkWidget * widget,
                                  GdkEventCrossing * event);

static gboolean gtk_xpaned_leave (GtkWidget * widget,
                                  GdkEventCrossing * event);

static gboolean gtk_xpaned_button_press (GtkWidget * widget,
                                         GdkEventButton * event);

static gboolean gtk_xpaned_button_release (GtkWidget * widget,
                                           GdkEventButton * event);

static gboolean gtk_xpaned_motion (GtkWidget * widget,
                                   GdkEventMotion * event);

static gboolean gtk_xpaned_focus (GtkWidget * widget,
                                  GtkDirectionType direction);

static void gtk_xpaned_add (GtkContainer * container, GtkWidget * widget);

static void gtk_xpaned_remove (GtkContainer * container, GtkWidget * widget);

static void gtk_xpaned_forall (GtkContainer * container,
                               gboolean include_internals,
                               GtkCallback callback, gpointer callback_data);

static void gtk_xpaned_set_focus_child (GtkContainer * container,
                                        GtkWidget * child);

static void gtk_xpaned_set_saved_focus (GtkXPaned * xpaned,
                                        GtkWidget * widget);

static void gtk_xpaned_set_first_xpaned (GtkXPaned * xpaned,
                                         GtkXPaned * first_xpaned);

static void gtk_xpaned_set_last_top_left_child_focus (GtkXPaned * xpaned,
                                                      GtkWidget * widget);

static void gtk_xpaned_set_last_top_right_child_focus (GtkXPaned * xpaned,
                                                       GtkWidget * widget);

static void gtk_xpaned_set_last_bottom_left_child_focus (GtkXPaned * xpaned,
                                                         GtkWidget * widget);

static void gtk_xpaned_set_last_bottom_right_child_focus (GtkXPaned * xpaned,
                                                          GtkWidget * widget);

static gboolean gtk_xpaned_cycle_child_focus (GtkXPaned * xpaned,
                                              gboolean reverse);

static gboolean gtk_xpaned_cycle_handle_focus (GtkXPaned * xpaned,
                                               gboolean reverse);

static gboolean gtk_xpaned_move_handle (GtkXPaned * xpaned,
                                        GtkScrollType scroll);

static gboolean gtk_xpaned_accept_position (GtkXPaned * xpaned);

static gboolean gtk_xpaned_cancel_position (GtkXPaned * xpaned);

static gboolean gtk_xpaned_toggle_handle_focus (GtkXPaned * xpaned);

static GType gtk_xpaned_child_type (GtkContainer * container);

static GtkContainerClass *parent_class = NULL;

struct _GtkXPanedPrivate
{
  GtkWidget *saved_focus;
  GtkXPaned *first_xpaned;
};

GType
gtk_xpaned_get_type (void)
{
  static GType xpaned_type = 0;

  if (!xpaned_type)
    {
      static const GTypeInfo xpaned_info = {
        sizeof (GtkXPanedClass),
        NULL,                   /* base_init */
        NULL,                   /* base_finalize */
        (GClassInitFunc) gtk_xpaned_class_init,
        NULL,                   /* class_finalize */
        NULL,                   /* class_data */
        sizeof (GtkXPaned),
        0,                      /* n_preallocs */
        (GInstanceInitFunc) gtk_xpaned_init
      };

      xpaned_type = g_type_register_static (GTK_TYPE_CONTAINER,
                                            "GtkXPaned", &xpaned_info, 0);
    }

  return xpaned_type;
}

GtkWidget *
gtk_xpaned_new (void)
{
  GtkXPaned *xpaned;

  xpaned = g_object_new (GTK_TYPE_XPANED, NULL);

  return GTK_WIDGET (xpaned);
}

static guint signals[LAST_SIGNAL] = { 0 };

static void
add_tab_bindings (GtkBindingSet * binding_set, GdkModifierType modifiers)
{
  gtk_binding_entry_add_signal (binding_set,
                                GDK_Tab, modifiers, "toggle_handle_focus", 0);

  gtk_binding_entry_add_signal (binding_set,
                                GDK_KP_Tab,
                                modifiers, "toggle_handle_focus", 0);
}

static void
add_move_binding (GtkBindingSet * binding_set,
                  guint keyval, GdkModifierType mask, GtkScrollType scroll)
{
  gtk_binding_entry_add_signal (binding_set,
                                keyval,
                                mask,
                                "move_handle",
                                1, GTK_TYPE_SCROLL_TYPE, scroll);
}

static void
gtk_xpaned_class_init (GtkXPanedClass * class)
{
  GObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;
  GtkXPanedClass *xpaned_class;
  GtkBindingSet *binding_set;

  object_class = (GObjectClass *) class;
  widget_class = (GtkWidgetClass *) class;
  container_class = (GtkContainerClass *) class;
  xpaned_class = (GtkXPanedClass *) class;

  parent_class = g_type_class_peek_parent (class);

  object_class->set_property = gtk_xpaned_set_property;
  object_class->get_property = gtk_xpaned_get_property;
  object_class->finalize = gtk_xpaned_finalize;

  widget_class->realize = gtk_xpaned_realize;
  widget_class->unrealize = gtk_xpaned_unrealize;
  widget_class->map = gtk_xpaned_map;
  widget_class->unmap = gtk_xpaned_unmap;
  widget_class->draw = gtk_xpaned_draw;
  widget_class->focus = gtk_xpaned_focus;
  widget_class->enter_notify_event = gtk_xpaned_enter;
  widget_class->leave_notify_event = gtk_xpaned_leave;
  widget_class->button_press_event = gtk_xpaned_button_press;
  widget_class->button_release_event = gtk_xpaned_button_release;
  widget_class->motion_notify_event = gtk_xpaned_motion;
  widget_class->get_preferred_width  = gtk_xpaned_get_preferred_width;
  widget_class->get_preferred_height = gtk_xpaned_get_preferred_height;

  widget_class->size_allocate = gtk_xpaned_size_allocate;

  container_class->add = gtk_xpaned_add;
  container_class->remove = gtk_xpaned_remove;
  container_class->forall = gtk_xpaned_forall;
  container_class->child_type = gtk_xpaned_child_type;
  container_class->set_focus_child = gtk_xpaned_set_focus_child;
  container_class->set_child_property = gtk_xpaned_set_child_property;
  container_class->get_child_property = gtk_xpaned_get_child_property;

  xpaned_class->cycle_child_focus = gtk_xpaned_cycle_child_focus;
  xpaned_class->toggle_handle_focus = gtk_xpaned_toggle_handle_focus;
  xpaned_class->move_handle = gtk_xpaned_move_handle;
  xpaned_class->cycle_handle_focus = gtk_xpaned_cycle_handle_focus;
  xpaned_class->accept_position = gtk_xpaned_accept_position;
  xpaned_class->cancel_position = gtk_xpaned_cancel_position;

  g_object_class_install_property (object_class,
                                   PROP_X_POSITION,
                                   g_param_spec_int ("x-position",
                                                     ("x-Position"),
                                                     ("x-Position of paned separator in pixels (0 means all the way to the left)"),
                                                     0,
                                                     G_MAXINT,
                                                     0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));

  g_object_class_install_property (object_class,
                                   PROP_Y_POSITION,
                                   g_param_spec_int ("y-position",
                                                     "y-Position",
                                                     "y-Position of paned separator in pixels (0 means all the way to the top)",
                                                     0,
                                                     G_MAXINT,
                                                     0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE));

  g_object_class_install_property (object_class,
                                   PROP_POSITION_SET,
                                   g_param_spec_boolean ("position-set",
                                                         "Position Set",
                                                         "TRUE if the Position property should be used",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE));

  gtk_widget_class_install_style_property (widget_class,
                                           g_param_spec_int ("handle-size",
                                                             "Handle Size",
                                                             "Width of handle",
                                                             0,
                                                             G_MAXINT,
                                                             3,
                                                             G_PARAM_READABLE));
  /**
   * GtkXPaned:min-x-position:
   *
   * The smallest possible value for the x-position property. This property is derived from the
   * size and shrinkability of the widget's children.
   *
   * Since: 2.4
   */
  g_object_class_install_property (object_class,
                                   PROP_MIN_X_POSITION,
                                   g_param_spec_int ("min-x-position",
                                                     "Minimal x-Position",
                                                     "Smallest possible value for the \"x-position\" property",
                                                     0,
                                                     G_MAXINT,
                                                     0, G_PARAM_READABLE));

  /**
   * GtkXPaned:min-y-position:
   *
   * The smallest possible value for the y-position property. This property is derived from the
   * size and shrinkability of the widget's children.
   *
   * Since: 2.4
   */
  g_object_class_install_property (object_class,
                                   PROP_MIN_Y_POSITION,
                                   g_param_spec_int ("min-y-position",
                                                     "Minimal y-Position",
                                                     "Smallest possible value for the \"y-position\" property",
                                                     0,
                                                     G_MAXINT,
                                                     0, G_PARAM_READABLE));

  /**
   * GtkPaned:max-x-position:
   *
   * The largest possible value for the x-position property. This property is derived from the
   * size and shrinkability of the widget's children.
   *
   * Since: 2.4
   */
  g_object_class_install_property (object_class,
                                   PROP_MAX_X_POSITION,
                                   g_param_spec_int ("max-x-position",
                                                     "Maximal x-Position",
                                                     "Largest possible value for the \"x-position\" property",
                                                     0,
                                                     G_MAXINT,
                                                     G_MAXINT,
                                                     G_PARAM_READABLE));

  /**
   * GtkPaned:max-y-position:
   *
   * The largest possible value for the y-position property. This property is derived from the
   * size and shrinkability of the widget's children.
   *
   * Since: 2.4
   */
  g_object_class_install_property (object_class,
                                   PROP_MAX_Y_POSITION,
                                   g_param_spec_int ("max-y-position",
                                                     "Maximal y-Position",
                                                     "Largest possible value for the \"y-position\" property",
                                                     0,
                                                     G_MAXINT,
                                                     G_MAXINT,
                                                     G_PARAM_READABLE));

  /**
   * GtkPaned:resize:
   *
   * The "resize" child property determines whether the child expands and 
   * shrinks along with the paned widget.
   * 
   * Since: 2.4 
   */
  gtk_container_class_install_child_property (container_class,
                                              CHILD_PROP_RESIZE,
                                              g_param_spec_boolean ("resize",
                                                                    "Resize",
                                                                    "If TRUE, the child expands and shrinks along with the paned widget",
                                                                    TRUE,
                                                                    G_PARAM_READWRITE));

  /**
   * GtkPaned:shrink:
   *
   * The "shrink" child property determines whether the child can be made 
   * smaller than its requisition.
   * 
   * Since: 2.4 
   */
  gtk_container_class_install_child_property (container_class,
                                              CHILD_PROP_SHRINK,
                                              g_param_spec_boolean ("shrink",
                                                                    "Shrink",
                                                                    "If TRUE, the child can be made smaller than its requisition",
                                                                    TRUE,
                                                                    G_PARAM_READWRITE));

  signals[CYCLE_CHILD_FOCUS] = g_signal_new ("cycle-child-focus",
                                             G_TYPE_FROM_CLASS (object_class),
                                             G_SIGNAL_RUN_LAST |
                                             G_SIGNAL_ACTION,
                                             G_STRUCT_OFFSET (GtkXPanedClass,
                                                              cycle_child_focus),
                                             NULL, NULL,
                                             psppire_marshal_BOOLEAN__BOOLEAN,
                                             G_TYPE_BOOLEAN, 1,
                                             G_TYPE_BOOLEAN);

  signals[TOGGLE_HANDLE_FOCUS] = g_signal_new ("toggle-handle-focus",
                                               G_TYPE_FROM_CLASS
                                               (object_class),
                                               G_SIGNAL_RUN_LAST |
                                               G_SIGNAL_ACTION,
                                               G_STRUCT_OFFSET
                                               (GtkXPanedClass,
                                                toggle_handle_focus), NULL,
                                               NULL,
                                               psppire_marshal_BOOLEAN__VOID,
                                               G_TYPE_BOOLEAN, 0);

  signals[MOVE_HANDLE] = g_signal_new ("move-handle",
                                       G_TYPE_FROM_CLASS (object_class),
                                       G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                       G_STRUCT_OFFSET (GtkXPanedClass,
                                                        move_handle), NULL,
                                       NULL, psppire_marshal_BOOLEAN__ENUM,
                                       G_TYPE_BOOLEAN, 1,
                                       GTK_TYPE_SCROLL_TYPE);

  signals[CYCLE_HANDLE_FOCUS] = g_signal_new ("cycle-handle-focus",
                                              G_TYPE_FROM_CLASS
                                              (object_class),
                                              G_SIGNAL_RUN_LAST |
                                              G_SIGNAL_ACTION,
                                              G_STRUCT_OFFSET (GtkXPanedClass,
                                                               cycle_handle_focus),
                                              NULL, NULL,
                                              psppire_marshal_BOOLEAN__BOOLEAN,
                                              G_TYPE_BOOLEAN, 1,
                                              G_TYPE_BOOLEAN);

  signals[ACCEPT_POSITION] = g_signal_new ("accept-position",
                                           G_TYPE_FROM_CLASS (object_class),
                                           G_SIGNAL_RUN_LAST |
                                           G_SIGNAL_ACTION,
                                           G_STRUCT_OFFSET (GtkXPanedClass,
                                                            accept_position),
                                           NULL, NULL,
                                           psppire_marshal_BOOLEAN__VOID,
                                           G_TYPE_BOOLEAN, 0);

  signals[CANCEL_POSITION] = g_signal_new ("cancel-position",
                                           G_TYPE_FROM_CLASS (object_class),
                                           G_SIGNAL_RUN_LAST |
                                           G_SIGNAL_ACTION,
                                           G_STRUCT_OFFSET (GtkXPanedClass,
                                                            cancel_position),
                                           NULL, NULL,
                                           psppire_marshal_BOOLEAN__VOID,
                                           G_TYPE_BOOLEAN, 0);

  binding_set = gtk_binding_set_by_class (class);

  /* F6 and friends */
  gtk_binding_entry_add_signal (binding_set,
                                GDK_F6, 0,
                                "cycle-child-focus", 1,
                                G_TYPE_BOOLEAN, FALSE);

  gtk_binding_entry_add_signal (binding_set,
                                GDK_F6, GDK_SHIFT_MASK,
                                "cycle-child-focus", 1, G_TYPE_BOOLEAN, TRUE);

  /* F8 and friends */
  gtk_binding_entry_add_signal (binding_set,
                                GDK_F8, 0,
                                "cycle-handle-focus", 1,
                                G_TYPE_BOOLEAN, FALSE);

  gtk_binding_entry_add_signal (binding_set,
                                GDK_F8, GDK_SHIFT_MASK,
                                "cycle-handle-focus", 1,
                                G_TYPE_BOOLEAN, TRUE);

  add_tab_bindings (binding_set, 0);
  add_tab_bindings (binding_set, GDK_CONTROL_MASK);
  add_tab_bindings (binding_set, GDK_SHIFT_MASK);
  add_tab_bindings (binding_set, GDK_CONTROL_MASK | GDK_SHIFT_MASK);

  /* accept and cancel positions */
  gtk_binding_entry_add_signal (binding_set,
                                GDK_Escape, 0, "cancel-position", 0);

  gtk_binding_entry_add_signal (binding_set,
                                GDK_Return, 0, "accept-position", 0);

  gtk_binding_entry_add_signal (binding_set,
                                GDK_KP_Enter, 0, "accept-position", 0);

  gtk_binding_entry_add_signal (binding_set,
                                GDK_space, 0, "accept-position", 0);

  gtk_binding_entry_add_signal (binding_set,
                                GDK_KP_Space, 0, "accept-position", 0);

  /* move handle */
  add_move_binding (binding_set, GDK_Left, 0, GTK_SCROLL_STEP_LEFT);
  add_move_binding (binding_set, GDK_KP_Left, 0, GTK_SCROLL_STEP_LEFT);
  add_move_binding (binding_set, GDK_Left, GDK_CONTROL_MASK,
                    GTK_SCROLL_PAGE_LEFT);
  add_move_binding (binding_set, GDK_KP_Left, GDK_CONTROL_MASK,
                    GTK_SCROLL_PAGE_LEFT);

  add_move_binding (binding_set, GDK_Right, 0, GTK_SCROLL_STEP_RIGHT);
  add_move_binding (binding_set, GDK_Right, GDK_CONTROL_MASK,
                    GTK_SCROLL_PAGE_RIGHT);
  add_move_binding (binding_set, GDK_KP_Right, 0, GTK_SCROLL_STEP_RIGHT);
  add_move_binding (binding_set, GDK_KP_Right, GDK_CONTROL_MASK,
                    GTK_SCROLL_PAGE_RIGHT);

  add_move_binding (binding_set, GDK_Up, 0, GTK_SCROLL_STEP_UP);
  add_move_binding (binding_set, GDK_Up, GDK_CONTROL_MASK,
                    GTK_SCROLL_PAGE_UP);
  add_move_binding (binding_set, GDK_KP_Up, 0, GTK_SCROLL_STEP_UP);
  add_move_binding (binding_set, GDK_KP_Up, GDK_CONTROL_MASK,
                    GTK_SCROLL_PAGE_UP);
  add_move_binding (binding_set, GDK_Page_Up, 0, GTK_SCROLL_PAGE_UP);
  add_move_binding (binding_set, GDK_KP_Page_Up, 0, GTK_SCROLL_PAGE_UP);

  add_move_binding (binding_set, GDK_Down, 0, GTK_SCROLL_STEP_DOWN);
  add_move_binding (binding_set, GDK_Down, GDK_CONTROL_MASK,
                    GTK_SCROLL_PAGE_DOWN);
  add_move_binding (binding_set, GDK_KP_Down, 0, GTK_SCROLL_STEP_DOWN);
  add_move_binding (binding_set, GDK_KP_Down, GDK_CONTROL_MASK,
                    GTK_SCROLL_PAGE_DOWN);
  add_move_binding (binding_set, GDK_Page_Down, 0, GTK_SCROLL_PAGE_RIGHT);
  add_move_binding (binding_set, GDK_KP_Page_Down, 0, GTK_SCROLL_PAGE_RIGHT);

  add_move_binding (binding_set, GDK_Home, 0, GTK_SCROLL_START);
  add_move_binding (binding_set, GDK_KP_Home, 0, GTK_SCROLL_START);
  add_move_binding (binding_set, GDK_End, 0, GTK_SCROLL_END);
  add_move_binding (binding_set, GDK_KP_End, 0, GTK_SCROLL_END);
}

static GType
gtk_xpaned_child_type (GtkContainer * container)
{
  if (!GTK_XPANED (container)->top_left_child ||
      !GTK_XPANED (container)->top_right_child ||
      !GTK_XPANED (container)->bottom_left_child ||
      !GTK_XPANED (container)->bottom_right_child)
    return GTK_TYPE_WIDGET;
  else
    return G_TYPE_NONE;
}

static void
gtk_xpaned_init (GtkXPaned * xpaned)
{
  gtk_widget_set_can_focus (GTK_WIDGET (xpaned), TRUE);
  gtk_widget_set_has_window (GTK_WIDGET (xpaned), FALSE);

  xpaned->top_left_child = NULL;
  xpaned->top_right_child = NULL;
  xpaned->bottom_left_child = NULL;
  xpaned->bottom_right_child = NULL;
  xpaned->handle_east = NULL;
  xpaned->handle_west = NULL;
  xpaned->handle_north = NULL;
  xpaned->handle_south = NULL;
  xpaned->handle_middle = NULL;
  xpaned->cursor_type_east = GDK_SB_V_DOUBLE_ARROW;
  xpaned->cursor_type_west = GDK_SB_V_DOUBLE_ARROW;
  xpaned->cursor_type_north = GDK_SB_H_DOUBLE_ARROW;
  xpaned->cursor_type_south = GDK_SB_H_DOUBLE_ARROW;
  xpaned->cursor_type_middle = GDK_FLEUR;

  xpaned->handle_pos_east.width = 5;
  xpaned->handle_pos_east.height = 5;
  xpaned->handle_pos_west.width = 5;
  xpaned->handle_pos_west.height = 5;
  xpaned->handle_pos_north.width = 5;
  xpaned->handle_pos_north.height = 5;
  xpaned->handle_pos_south.width = 5;
  xpaned->handle_pos_south.height = 5;
  xpaned->handle_pos_middle.width = 5;
  xpaned->handle_pos_middle.height = 5;

  xpaned->position_set = FALSE;
  xpaned->last_allocation.width = -1;
  xpaned->last_allocation.height = -1;
  xpaned->in_drag_vert = FALSE;
  xpaned->in_drag_horiz = FALSE;
  xpaned->in_drag_vert_and_horiz = FALSE;

  xpaned->maximized[GTK_XPANED_TOP_LEFT] = FALSE;
  xpaned->maximized[GTK_XPANED_TOP_RIGHT] = FALSE;
  xpaned->maximized[GTK_XPANED_BOTTOM_LEFT] = FALSE;
  xpaned->maximized[GTK_XPANED_BOTTOM_RIGHT] = FALSE;

  xpaned->priv = g_new0 (GtkXPanedPrivate, 1);
  xpaned->last_top_left_child_focus = NULL;
  xpaned->last_top_right_child_focus = NULL;
  xpaned->last_bottom_left_child_focus = NULL;
  xpaned->last_bottom_right_child_focus = NULL;
  xpaned->in_recursion = FALSE;
  xpaned->handle_prelit = FALSE;
  xpaned->original_position.x = -1;
  xpaned->original_position.y = -1;
  xpaned->unmaximized_position.x = -1;
  xpaned->unmaximized_position.y = -1;

  xpaned->handle_pos_east.x = -1;
  xpaned->handle_pos_east.y = -1;
  xpaned->handle_pos_west.x = -1;
  xpaned->handle_pos_west.y = -1;
  xpaned->handle_pos_north.x = -1;
  xpaned->handle_pos_north.y = -1;
  xpaned->handle_pos_south.x = -1;
  xpaned->handle_pos_south.y = -1;
  xpaned->handle_pos_middle.x = -1;
  xpaned->handle_pos_middle.y = -1;

  xpaned->drag_pos.x = -1;
  xpaned->drag_pos.y = -1;
}

void
gtk_xpaned_compute_position (GtkXPaned * xpaned,
                             const GtkAllocation * allocation,
                             GtkRequisition * top_left_child_req,
                             GtkRequisition * top_right_child_req,
                             GtkRequisition * bottom_left_child_req,
                             GtkRequisition * bottom_right_child_req);


static void
gtk_xpaned_size_allocate (GtkWidget * widget, GtkAllocation * allocation)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);
  gint border_width = gtk_container_get_border_width (GTK_CONTAINER (xpaned));
  GtkAllocation top_left_child_allocation;
  GtkAllocation top_right_child_allocation;
  GtkAllocation bottom_left_child_allocation;
  GtkAllocation bottom_right_child_allocation;
  GtkRequisition top_left_child_requisition;
  GtkRequisition top_right_child_requisition;
  GtkRequisition bottom_left_child_requisition;
  GtkRequisition bottom_right_child_requisition;
  gint handle_size;

  /* determine size of handle(s) */
  gtk_widget_style_get (widget, "handle-size", &handle_size, NULL);

  gtk_widget_set_allocation (widget, allocation);

  if (xpaned->top_left_child
      && gtk_widget_get_visible (xpaned->top_left_child)
      && xpaned->top_right_child
      && gtk_widget_get_visible (xpaned->top_right_child)
      && xpaned->bottom_left_child
      && gtk_widget_get_visible (xpaned->bottom_left_child)
      && xpaned->bottom_right_child
      && gtk_widget_get_visible (xpaned->bottom_right_child))
    {
      /* what sizes do the children want to be at least at */
      gtk_widget_get_preferred_size (xpaned->top_left_child,
                                     &top_left_child_requisition, NULL);
      gtk_widget_get_preferred_size (xpaned->top_right_child,
                                     &top_right_child_requisition, NULL);
      gtk_widget_get_preferred_size (xpaned->bottom_left_child,
                                     &bottom_left_child_requisition, NULL);
      gtk_widget_get_preferred_size (xpaned->bottom_right_child,
                                     &bottom_right_child_requisition, NULL);

      /* determine the total requisition-sum of all requisitions of borders,
       * handles, children etc. */
      gtk_xpaned_compute_position (xpaned,
                                   allocation,
                                   &top_left_child_requisition,
                                   &top_right_child_requisition,
                                   &bottom_left_child_requisition,
                                   &bottom_right_child_requisition);

      /* calculate the current positions and sizes of the handles */
      xpaned->handle_pos_east.x =
        allocation->x + border_width +
        xpaned->top_left_child_size.width + handle_size;
      xpaned->handle_pos_east.y =
        allocation->y + border_width +
        xpaned->top_left_child_size.height;
      xpaned->handle_pos_east.width =
        allocation->width - xpaned->top_left_child_size.width -
        2 * border_width - handle_size;
      xpaned->handle_pos_east.height = handle_size;

      xpaned->handle_pos_west.x = allocation->x + border_width;
      xpaned->handle_pos_west.y = xpaned->handle_pos_east.y;
      xpaned->handle_pos_west.width =
        allocation->width - xpaned->handle_pos_east.width -
        2 * border_width - handle_size;
      xpaned->handle_pos_west.height = handle_size;

      xpaned->handle_pos_north.x = xpaned->handle_pos_east.x - handle_size;
      xpaned->handle_pos_north.y = allocation->y + border_width;
      xpaned->handle_pos_north.width = handle_size;
      xpaned->handle_pos_north.height =
        xpaned->handle_pos_east.y - allocation->y - border_width;

      xpaned->handle_pos_south.x = xpaned->handle_pos_north.x;
      xpaned->handle_pos_south.y = xpaned->handle_pos_east.y + handle_size;
      xpaned->handle_pos_south.width = handle_size;
      xpaned->handle_pos_south.height =
        allocation->height - xpaned->handle_pos_north.height -
        2 * border_width - handle_size;


#define CENTRUM 20
      xpaned->handle_pos_middle.x = xpaned->handle_pos_north.x;
      xpaned->handle_pos_middle.y = xpaned->handle_pos_east.y;
      xpaned->handle_pos_middle.width = handle_size + CENTRUM;
      xpaned->handle_pos_middle.height = handle_size + CENTRUM;

      /* set allocation for top-left child */
      top_left_child_allocation.x = allocation->x + border_width;
      top_left_child_allocation.y = allocation->y + border_width;
      top_left_child_allocation.width = xpaned->handle_pos_west.width;
      top_left_child_allocation.height = xpaned->handle_pos_north.height;

      /* set allocation for top-right child */
      top_right_child_allocation.x =
        allocation->x + border_width + handle_size +
        top_left_child_allocation.width;
      top_right_child_allocation.y = allocation->y + border_width;
      top_right_child_allocation.width = xpaned->handle_pos_east.width;
      top_right_child_allocation.height = xpaned->handle_pos_north.height;

      /* set allocation for bottom-left child */
      bottom_left_child_allocation.x = xpaned->handle_pos_west.x;
      bottom_left_child_allocation.y = xpaned->handle_pos_south.y;
      bottom_left_child_allocation.width = xpaned->handle_pos_west.width;
      bottom_left_child_allocation.height = xpaned->handle_pos_south.height;

      /* set allocation for bottom-right child */
      bottom_right_child_allocation.x = top_right_child_allocation.x;
      bottom_right_child_allocation.y = bottom_left_child_allocation.y;
      bottom_right_child_allocation.width = xpaned->handle_pos_east.width;
      bottom_right_child_allocation.height = xpaned->handle_pos_south.height;

      if (gtk_widget_get_realized (widget))
        {
          if (gtk_widget_get_mapped (widget))
            {
              gdk_window_show (xpaned->handle_east);
              gdk_window_show (xpaned->handle_west);
              gdk_window_show (xpaned->handle_north);
              gdk_window_show (xpaned->handle_south);
              gdk_window_show (xpaned->handle_middle);
            }

          gdk_window_move_resize (xpaned->handle_east,
                                  xpaned->handle_pos_east.x,
                                  xpaned->handle_pos_east.y,
                                  xpaned->handle_pos_east.width,
                                  xpaned->handle_pos_east.height);

          gdk_window_move_resize (xpaned->handle_west,
                                  xpaned->handle_pos_west.x,
                                  xpaned->handle_pos_west.y,
                                  xpaned->handle_pos_west.width,
                                  xpaned->handle_pos_west.height);

          gdk_window_move_resize (xpaned->handle_north,
                                  xpaned->handle_pos_north.x,
                                  xpaned->handle_pos_north.y,
                                  xpaned->handle_pos_north.width,
                                  xpaned->handle_pos_north.height);

          gdk_window_move_resize (xpaned->handle_south,
                                  xpaned->handle_pos_south.x,
                                  xpaned->handle_pos_south.y,
                                  xpaned->handle_pos_south.width,
                                  xpaned->handle_pos_south.height);

          gdk_window_move_resize (xpaned->handle_middle,
                                  xpaned->handle_pos_middle.x,
                                  xpaned->handle_pos_middle.y,
                                  xpaned->handle_pos_middle.width,
                                  xpaned->handle_pos_middle.height);
        }

      /* Now allocate the childen, making sure, when resizing not to
       * overlap the windows
       */
      if (gtk_widget_get_mapped (widget))
        {
          gtk_widget_size_allocate (xpaned->top_right_child,
                                    &top_right_child_allocation);
          gtk_widget_size_allocate (xpaned->top_left_child,
                                    &top_left_child_allocation);
          gtk_widget_size_allocate (xpaned->bottom_left_child,
                                    &bottom_left_child_allocation);
          gtk_widget_size_allocate (xpaned->bottom_right_child,
                                    &bottom_right_child_allocation);
        }
    }
}

static void
gtk_xpaned_set_property (GObject * object,
                         guint prop_id,
                         const GValue * value, GParamSpec * pspec)
{
  GtkXPaned *xpaned = GTK_XPANED (object);

  switch (prop_id)
    {
    case PROP_X_POSITION:
      gtk_xpaned_set_position_x (xpaned, g_value_get_int (value));
      break;

    case PROP_Y_POSITION:
      gtk_xpaned_set_position_y (xpaned, g_value_get_int (value));
      break;

    case PROP_POSITION_SET:
      xpaned->position_set = g_value_get_boolean (value);
      gtk_widget_queue_resize (GTK_WIDGET (xpaned));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_xpaned_get_property (GObject * object,
                         guint prop_id, GValue * value, GParamSpec * pspec)
{
  GtkXPaned *xpaned = GTK_XPANED (object);

  switch (prop_id)
    {
    case PROP_X_POSITION:
      g_value_set_int (value, xpaned->top_left_child_size.width);
      break;

    case PROP_Y_POSITION:
      g_value_set_int (value, xpaned->top_left_child_size.height);
      break;

    case PROP_POSITION_SET:
      g_value_set_boolean (value, xpaned->position_set);
      break;

    case PROP_MIN_X_POSITION:
      g_value_set_int (value, xpaned->min_position.x);
      break;

    case PROP_MIN_Y_POSITION:
      g_value_set_int (value, xpaned->min_position.y);
      break;

    case PROP_MAX_X_POSITION:
      g_value_set_int (value, xpaned->max_position.x);
      break;

    case PROP_MAX_Y_POSITION:
      g_value_set_int (value, xpaned->max_position.y);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_xpaned_set_child_property (GtkContainer * container,
                               GtkWidget * child,
                               guint property_id,
                               const GValue * value, GParamSpec * pspec)
{
  GtkXPaned *xpaned = GTK_XPANED (container);
  gboolean old_value = FALSE;
  gboolean new_value = FALSE;

  g_assert (child == xpaned->top_left_child ||
            child == xpaned->top_right_child ||
            child == xpaned->bottom_left_child ||
            child == xpaned->bottom_right_child);

  new_value = g_value_get_boolean (value);

  switch (property_id)
    {
    case CHILD_PROP_RESIZE:
      if (child == xpaned->top_left_child)
        {
          old_value = xpaned->top_left_child_resize;
          xpaned->top_left_child_resize = new_value;
        }
      else if (child == xpaned->top_right_child)
        {
          old_value = xpaned->top_right_child_resize;
          xpaned->top_right_child_resize = new_value;
        }
      else if (child == xpaned->bottom_left_child)
        {
          old_value = xpaned->bottom_left_child_resize;
          xpaned->bottom_left_child_resize = new_value;
        }
      else if (child == xpaned->bottom_right_child)
        {
          old_value = xpaned->bottom_right_child_resize;
          xpaned->bottom_right_child_resize = new_value;
        }
      break;

    case CHILD_PROP_SHRINK:
      if (child == xpaned->top_left_child)
        {
          old_value = xpaned->top_left_child_shrink;
          xpaned->top_left_child_shrink = new_value;
        }
      else if (child == xpaned->top_right_child)
        {
          old_value = xpaned->top_right_child_shrink;
          xpaned->top_right_child_shrink = new_value;
        }
      else if (child == xpaned->bottom_left_child)
        {
          old_value = xpaned->bottom_left_child_shrink;
          xpaned->bottom_left_child_shrink = new_value;
        }
      else if (child == xpaned->bottom_right_child)
        {
          old_value = xpaned->bottom_right_child_shrink;
          xpaned->bottom_right_child_shrink = new_value;
        }
      break;

    default:
      GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID (container,
                                                    property_id, pspec);
      old_value = -1;           /* quiet gcc */
      break;
    }

  if (old_value != new_value)
    gtk_widget_queue_resize (GTK_WIDGET (container));
}

static void
gtk_xpaned_get_child_property (GtkContainer * container,
                               GtkWidget * child,
                               guint property_id,
                               GValue * value, GParamSpec * pspec)
{
  GtkXPaned *xpaned = GTK_XPANED (container);

  g_assert (child == xpaned->top_left_child ||
            child == xpaned->top_right_child ||
            child == xpaned->bottom_left_child ||
            child == xpaned->bottom_right_child);

  switch (property_id)
    {
    case CHILD_PROP_RESIZE:
      if (child == xpaned->top_left_child)
        g_value_set_boolean (value, xpaned->top_left_child_resize);
      else if (child == xpaned->top_right_child)
        g_value_set_boolean (value, xpaned->top_right_child_resize);
      else if (child == xpaned->bottom_left_child)
        g_value_set_boolean (value, xpaned->bottom_left_child_resize);
      else if (child == xpaned->bottom_right_child)
        g_value_set_boolean (value, xpaned->bottom_right_child_resize);
      break;

    case CHILD_PROP_SHRINK:
      if (child == xpaned->top_left_child)
        g_value_set_boolean (value, xpaned->top_left_child_shrink);
      else if (child == xpaned->top_right_child)
        g_value_set_boolean (value, xpaned->top_right_child_shrink);
      else if (child == xpaned->bottom_left_child)
        g_value_set_boolean (value, xpaned->bottom_left_child_shrink);
      else if (child == xpaned->bottom_right_child)
        g_value_set_boolean (value, xpaned->bottom_right_child_shrink);
      break;

    default:
      GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID (container,
                                                    property_id, pspec);
      break;
    }
}

static void
gtk_xpaned_finalize (GObject * object)
{
  GtkXPaned *xpaned = GTK_XPANED (object);

  gtk_xpaned_set_saved_focus (xpaned, NULL);
  gtk_xpaned_set_first_xpaned (xpaned, NULL);

  g_free (xpaned->priv);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gtk_xpaned_realize (GtkWidget * widget)
{
  GtkXPaned *xpaned;
  GdkWindowAttr attributes_east;
  GdkWindowAttr attributes_west;
  GdkWindowAttr attributes_north;
  GdkWindowAttr attributes_south;
  GdkWindowAttr attributes_middle;
  gint attributes_mask_east;
  gint attributes_mask_west;
  gint attributes_mask_north;
  gint attributes_mask_south;
  gint attributes_mask_middle;

  gtk_widget_set_realized (widget, TRUE);
  xpaned = GTK_XPANED (widget);

  gtk_widget_set_window (widget, gtk_widget_get_parent_window (widget));
  //  g_object_ref (widget->window);

  attributes_east.window_type = GDK_WINDOW_CHILD;
  attributes_west.window_type = GDK_WINDOW_CHILD;
  attributes_north.window_type = GDK_WINDOW_CHILD;
  attributes_south.window_type = GDK_WINDOW_CHILD;
  attributes_middle.window_type = GDK_WINDOW_CHILD;

  attributes_east.wclass = GDK_INPUT_ONLY;
  attributes_west.wclass = GDK_INPUT_ONLY;
  attributes_north.wclass = GDK_INPUT_ONLY;
  attributes_south.wclass = GDK_INPUT_ONLY;
  attributes_middle.wclass = GDK_INPUT_ONLY;

  attributes_east.x = xpaned->handle_pos_east.x;
  attributes_east.y = xpaned->handle_pos_east.y;
  attributes_east.width = xpaned->handle_pos_east.width;
  attributes_east.height = xpaned->handle_pos_east.height;

  attributes_west.x = xpaned->handle_pos_west.x;
  attributes_west.y = xpaned->handle_pos_west.y;
  attributes_west.width = xpaned->handle_pos_west.width;
  attributes_west.height = xpaned->handle_pos_west.height;

  attributes_north.x = xpaned->handle_pos_north.x;
  attributes_north.y = xpaned->handle_pos_north.y;
  attributes_north.width = xpaned->handle_pos_north.width;
  attributes_north.height = xpaned->handle_pos_north.height;

  attributes_south.x = xpaned->handle_pos_south.x;
  attributes_south.y = xpaned->handle_pos_south.y;
  attributes_south.width = xpaned->handle_pos_south.width;
  attributes_south.height = xpaned->handle_pos_south.height;

  attributes_middle.x = xpaned->handle_pos_middle.x;
  attributes_middle.y = xpaned->handle_pos_middle.y;
  attributes_middle.width = xpaned->handle_pos_middle.width;
  attributes_middle.height = xpaned->handle_pos_middle.height;

  attributes_east.cursor =
    gdk_cursor_new_for_display (gtk_widget_get_display (widget),
                                xpaned->cursor_type_east);
  attributes_west.cursor =
    gdk_cursor_new_for_display (gtk_widget_get_display (widget),
                                xpaned->cursor_type_west);
  attributes_north.cursor =
    gdk_cursor_new_for_display (gtk_widget_get_display (widget),
                                xpaned->cursor_type_north);
  attributes_south.cursor =
    gdk_cursor_new_for_display (gtk_widget_get_display (widget),
                                xpaned->cursor_type_south);
  attributes_middle.cursor =
    gdk_cursor_new_for_display (gtk_widget_get_display (widget),
                                xpaned->cursor_type_middle);

  attributes_east.event_mask = gtk_widget_get_events (widget);
  attributes_west.event_mask = gtk_widget_get_events (widget);
  attributes_north.event_mask = gtk_widget_get_events (widget);
  attributes_south.event_mask = gtk_widget_get_events (widget);
  attributes_middle.event_mask = gtk_widget_get_events (widget);

  attributes_east.event_mask |= (GDK_BUTTON_PRESS_MASK |
                                 GDK_BUTTON_RELEASE_MASK |
                                 GDK_ENTER_NOTIFY_MASK |
                                 GDK_LEAVE_NOTIFY_MASK |
                                 GDK_POINTER_MOTION_MASK |
                                 GDK_POINTER_MOTION_HINT_MASK);
  attributes_west.event_mask |= (GDK_BUTTON_PRESS_MASK |
                                 GDK_BUTTON_RELEASE_MASK |
                                 GDK_ENTER_NOTIFY_MASK |
                                 GDK_LEAVE_NOTIFY_MASK |
                                 GDK_POINTER_MOTION_MASK |
                                 GDK_POINTER_MOTION_HINT_MASK);
  attributes_north.event_mask |= (GDK_BUTTON_PRESS_MASK |
                                  GDK_BUTTON_RELEASE_MASK |
                                  GDK_ENTER_NOTIFY_MASK |
                                  GDK_LEAVE_NOTIFY_MASK |
                                  GDK_POINTER_MOTION_MASK |
                                  GDK_POINTER_MOTION_HINT_MASK);
  attributes_south.event_mask |= (GDK_BUTTON_PRESS_MASK |
                                  GDK_BUTTON_RELEASE_MASK |
                                  GDK_ENTER_NOTIFY_MASK |
                                  GDK_LEAVE_NOTIFY_MASK |
                                  GDK_POINTER_MOTION_MASK |
                                  GDK_POINTER_MOTION_HINT_MASK);
  attributes_middle.event_mask |= (GDK_BUTTON_PRESS_MASK |
                                   GDK_BUTTON_RELEASE_MASK |
                                   GDK_ENTER_NOTIFY_MASK |
                                   GDK_LEAVE_NOTIFY_MASK |
                                   GDK_POINTER_MOTION_MASK |
                                   GDK_POINTER_MOTION_HINT_MASK);

  attributes_mask_east = GDK_WA_X | GDK_WA_Y | GDK_WA_CURSOR;
  attributes_mask_west = GDK_WA_X | GDK_WA_Y | GDK_WA_CURSOR;
  attributes_mask_north = GDK_WA_X | GDK_WA_Y | GDK_WA_CURSOR;
  attributes_mask_south = GDK_WA_X | GDK_WA_Y | GDK_WA_CURSOR;
  attributes_mask_middle = GDK_WA_X | GDK_WA_Y | GDK_WA_CURSOR;

  xpaned->handle_east = gdk_window_new (gtk_widget_get_window (widget),
                                        &attributes_east,
                                        attributes_mask_east);
  xpaned->handle_west = gdk_window_new (gtk_widget_get_window (widget),
                                        &attributes_west,
                                        attributes_mask_west);
  xpaned->handle_north = gdk_window_new (gtk_widget_get_window (widget),
                                         &attributes_north,
                                         attributes_mask_north);
  xpaned->handle_south = gdk_window_new (gtk_widget_get_window (widget),
                                         &attributes_south,
                                         attributes_mask_south);
  xpaned->handle_middle = gdk_window_new (gtk_widget_get_window (widget),
                                          &attributes_middle,
                                          attributes_mask_middle);

  gdk_window_set_user_data (xpaned->handle_east, xpaned);
  gdk_window_set_user_data (xpaned->handle_west, xpaned);
  gdk_window_set_user_data (xpaned->handle_north, xpaned);
  gdk_window_set_user_data (xpaned->handle_south, xpaned);
  gdk_window_set_user_data (xpaned->handle_middle, xpaned);

  g_object_unref (attributes_east.cursor);
  g_object_unref (attributes_west.cursor);
  g_object_unref (attributes_north.cursor);
  g_object_unref (attributes_south.cursor);
  g_object_unref (attributes_middle.cursor);

  if (xpaned->top_left_child
      && gtk_widget_get_visible (xpaned->top_left_child)
      && xpaned->top_right_child
      && gtk_widget_get_visible (xpaned->top_right_child)
      && xpaned->bottom_left_child
      && gtk_widget_get_visible (xpaned->bottom_left_child)
      && xpaned->bottom_right_child
      && gtk_widget_get_visible (xpaned->bottom_right_child))
    {
      gdk_window_show (xpaned->handle_east);
      gdk_window_show (xpaned->handle_west);
      gdk_window_show (xpaned->handle_north);
      gdk_window_show (xpaned->handle_south);
      gdk_window_show (xpaned->handle_middle);
    }
}

static void
gtk_xpaned_unrealize (GtkWidget * widget)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);

  if (xpaned->handle_east)
    {
      gdk_window_set_user_data (xpaned->handle_east, NULL);
      gdk_window_destroy (xpaned->handle_east);
      xpaned->handle_east = NULL;
    }

  if (xpaned->handle_west)
    {
      gdk_window_set_user_data (xpaned->handle_west, NULL);
      gdk_window_destroy (xpaned->handle_west);
      xpaned->handle_west = NULL;
    }

  if (xpaned->handle_north)
    {
      gdk_window_set_user_data (xpaned->handle_north, NULL);
      gdk_window_destroy (xpaned->handle_north);
      xpaned->handle_north = NULL;
    }

  if (xpaned->handle_south)
    {
      gdk_window_set_user_data (xpaned->handle_south, NULL);
      gdk_window_destroy (xpaned->handle_south);
      xpaned->handle_south = NULL;
    }

  if (xpaned->handle_middle)
    {
      gdk_window_set_user_data (xpaned->handle_middle, NULL);
      gdk_window_destroy (xpaned->handle_middle);
      xpaned->handle_middle = NULL;
    }

  gtk_xpaned_set_last_top_left_child_focus (xpaned, NULL);
  gtk_xpaned_set_last_top_right_child_focus (xpaned, NULL);
  gtk_xpaned_set_last_bottom_left_child_focus (xpaned, NULL);
  gtk_xpaned_set_last_bottom_right_child_focus (xpaned, NULL);
  gtk_xpaned_set_saved_focus (xpaned, NULL);
  gtk_xpaned_set_first_xpaned (xpaned, NULL);

  if (GTK_WIDGET_CLASS (parent_class)->unrealize)
    (*GTK_WIDGET_CLASS (parent_class)->unrealize) (widget);
}

static void
gtk_xpaned_map (GtkWidget * widget)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);

  gdk_window_show (xpaned->handle_east);
  gdk_window_show (xpaned->handle_west);
  gdk_window_show (xpaned->handle_north);
  gdk_window_show (xpaned->handle_south);
  gdk_window_show (xpaned->handle_middle);

  GTK_WIDGET_CLASS (parent_class)->map (widget);
}

static void
gtk_xpaned_unmap (GtkWidget * widget)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);

  gdk_window_hide (xpaned->handle_east);
  gdk_window_hide (xpaned->handle_west);
  gdk_window_hide (xpaned->handle_north);
  gdk_window_hide (xpaned->handle_south);
  gdk_window_hide (xpaned->handle_middle);

  GTK_WIDGET_CLASS (parent_class)->unmap (widget);
}

static gboolean
gtk_xpaned_draw (GtkWidget * widget, cairo_t *cr)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);
  gint handle_size;

  /* determine size of handle(s) */
  gtk_widget_style_get (widget, "handle-size", &handle_size, NULL);

  /* I want the handle-"thickness" to be at least 3 */
  g_assert (handle_size >= 3);

  if (gtk_widget_get_visible (widget) && gtk_widget_get_mapped (widget) &&
      xpaned->top_left_child
      && gtk_widget_get_visible (xpaned->top_left_child)
      && xpaned->top_right_child
      && gtk_widget_get_visible (xpaned->top_right_child)
      && xpaned->bottom_left_child
      && gtk_widget_get_visible (xpaned->bottom_left_child)
      && xpaned->bottom_right_child
      && gtk_widget_get_visible (xpaned->bottom_right_child))
    {
      GtkStyleContext *context;

      context = gtk_widget_get_style_context (widget);
      gtk_render_handle (context, cr,
                         xpaned->handle_pos_east.x - handle_size - 256 / 2,
                         xpaned->handle_pos_west.y + 1,
                         256 + handle_size, handle_size - 2);

      gtk_render_handle (context, cr,
                         xpaned->handle_pos_north.x + 1,
                         xpaned->handle_pos_south.y - handle_size - 256 / 2,
                         handle_size - 2, 256 + handle_size);
    }

  /* Chain up to draw children */
  GTK_WIDGET_CLASS (parent_class)->draw (widget, cr);

  return FALSE;
}

static gboolean
is_rtl (GtkXPaned * xpaned)
{
  if (gtk_widget_get_direction (GTK_WIDGET (xpaned)) == GTK_TEXT_DIR_RTL)
    return TRUE;

  return FALSE;
}

static void
update_drag (GtkXPaned * xpaned)
{
  GdkPoint pos;
  GtkWidget *widget = GTK_WIDGET (xpaned);
  gint handle_size;
  GtkRequisition size;
  GtkAllocation allocation;

  gtk_widget_get_allocation (widget, &allocation);

  gdk_window_get_device_position (gtk_widget_get_window (widget),
                                  gdk_device_manager_get_client_pointer (
                                    gdk_display_get_device_manager (
                                      gtk_widget_get_display (widget))),
                                  &pos.x, &pos.y, NULL);
  if (!gtk_widget_get_has_window (widget))
    {
      pos.x -= allocation.x;
      pos.y -= allocation.y;
    }

  if (xpaned->in_drag_vert)
    {
      pos.y -= xpaned->drag_pos.y;

      if (is_rtl (xpaned))
        {
          gtk_widget_style_get (widget, "handle-size", &handle_size, NULL);

          size.height = allocation.height - pos.y - handle_size;
        }
      else
        {
          size.height = pos.y;
        }

      size.height -= gtk_container_get_border_width (GTK_CONTAINER (xpaned));

      size.height =
        CLAMP (size.height, xpaned->min_position.y, xpaned->max_position.y);

      if (size.height != xpaned->top_left_child_size.height)
        gtk_xpaned_set_position_y (xpaned, size.height);
    }

  if (xpaned->in_drag_horiz)
    {
      pos.x -= xpaned->drag_pos.x;

      if (is_rtl (xpaned))
        {
          gtk_widget_style_get (widget, "handle-size", &handle_size, NULL);

          size.width = allocation.width - pos.x - handle_size;
        }
      else
        {
          size.width = pos.x;
        }

      size.width -= gtk_container_get_border_width (GTK_CONTAINER (xpaned));

      size.width =
        CLAMP (size.width, xpaned->min_position.x, xpaned->max_position.x);

      if (size.width != xpaned->top_left_child_size.width)
        gtk_xpaned_set_position_x (xpaned, size.width);
    }

  if (xpaned->in_drag_vert_and_horiz)
    {
      pos.x -= xpaned->drag_pos.x;
      pos.y -= xpaned->drag_pos.y;

      if (is_rtl (xpaned))
        {
          gtk_widget_style_get (widget, "handle-size", &handle_size, NULL);

          size.width = allocation.width - pos.x - handle_size;
          size.height = allocation.height - pos.y - handle_size;
        }
      else
        {
          size.width = pos.x;
          size.height = pos.y;
        }

      size.width -= gtk_container_get_border_width (GTK_CONTAINER (xpaned));
      size.height -= gtk_container_get_border_width (GTK_CONTAINER (xpaned));

      size.width =
        CLAMP (size.width, xpaned->min_position.x, xpaned->max_position.x);
      size.height =
        CLAMP (size.height, xpaned->min_position.y, xpaned->max_position.y);

      if (size.width != xpaned->top_left_child_size.width)
        gtk_xpaned_set_position_x (xpaned, size.width);

      if (size.height != xpaned->top_left_child_size.height)
        gtk_xpaned_set_position_y (xpaned, size.height);
    }
}

static gboolean
gtk_xpaned_enter (GtkWidget * widget, GdkEventCrossing * event)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);

  if (xpaned->in_drag_vert ||
      xpaned->in_drag_horiz || xpaned->in_drag_vert_and_horiz)
    update_drag (xpaned);
  else
    {
      xpaned->handle_prelit = TRUE;

      gtk_widget_queue_draw_area (widget,
                                  xpaned->handle_pos_east.x,
                                  xpaned->handle_pos_east.y,
                                  xpaned->handle_pos_east.width,
                                  xpaned->handle_pos_east.height);

      gtk_widget_queue_draw_area (widget,
                                  xpaned->handle_pos_west.x,
                                  xpaned->handle_pos_west.y,
                                  xpaned->handle_pos_west.width,
                                  xpaned->handle_pos_west.height);

      gtk_widget_queue_draw_area (widget,
                                  xpaned->handle_pos_north.x,
                                  xpaned->handle_pos_north.y,
                                  xpaned->handle_pos_north.width,
                                  xpaned->handle_pos_north.height);

      gtk_widget_queue_draw_area (widget,
                                  xpaned->handle_pos_south.x,
                                  xpaned->handle_pos_south.y,
                                  xpaned->handle_pos_south.width,
                                  xpaned->handle_pos_south.height);

      gtk_widget_queue_draw_area (widget,
                                  xpaned->handle_pos_middle.x,
                                  xpaned->handle_pos_middle.y,
                                  xpaned->handle_pos_middle.width,
                                  xpaned->handle_pos_middle.height);
    }

  return TRUE;
}

static gboolean
gtk_xpaned_leave (GtkWidget * widget, GdkEventCrossing * event)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);

  if (xpaned->in_drag_vert ||
      xpaned->in_drag_horiz || xpaned->in_drag_vert_and_horiz)
    update_drag (xpaned);
  else
    {
      xpaned->handle_prelit = FALSE;

      gtk_widget_queue_draw_area (widget,
                                  xpaned->handle_pos_east.x,
                                  xpaned->handle_pos_east.y,
                                  xpaned->handle_pos_east.width,
                                  xpaned->handle_pos_east.height);

      gtk_widget_queue_draw_area (widget,
                                  xpaned->handle_pos_west.x,
                                  xpaned->handle_pos_west.y,
                                  xpaned->handle_pos_west.width,
                                  xpaned->handle_pos_west.height);

      gtk_widget_queue_draw_area (widget,
                                  xpaned->handle_pos_north.x,
                                  xpaned->handle_pos_north.y,
                                  xpaned->handle_pos_north.width,
                                  xpaned->handle_pos_north.height);

      gtk_widget_queue_draw_area (widget,
                                  xpaned->handle_pos_south.x,
                                  xpaned->handle_pos_south.y,
                                  xpaned->handle_pos_south.width,
                                  xpaned->handle_pos_south.height);

      gtk_widget_queue_draw_area (widget,
                                  xpaned->handle_pos_middle.x,
                                  xpaned->handle_pos_middle.y,
                                  xpaned->handle_pos_middle.width,
                                  xpaned->handle_pos_middle.height);
    }

  return TRUE;
}

static gboolean
gtk_xpaned_focus (GtkWidget * widget, GtkDirectionType direction)
{
  gboolean retval;

  /* This is a hack, but how can this be done without
   * excessive cut-and-paste from gtkcontainer.c?
   */

  gtk_widget_set_can_focus (GTK_WIDGET (widget), FALSE);
  retval = (*GTK_WIDGET_CLASS (parent_class)->focus) (widget, direction);
  gtk_widget_set_can_focus (GTK_WIDGET (widget), TRUE);

  return retval;
}

static void
gtk_xpaned_button_press_grab (GdkWindow *handle, GdkEventButton *event)
{
  /* We need a server grab here, not gtk_grab_add(), since
   * we don't want to pass events on to the widget's children */
  gdk_device_grab (event->device, handle,
                   GDK_OWNERSHIP_NONE,
                   FALSE,
                   (GDK_POINTER_MOTION_HINT_MASK
                    | GDK_BUTTON1_MOTION_MASK
                    | GDK_BUTTON_RELEASE_MASK
                    | GDK_ENTER_NOTIFY_MASK
                    | GDK_LEAVE_NOTIFY_MASK),
                   NULL, event->time);
}

static gboolean
gtk_xpaned_button_press (GtkWidget * widget, GdkEventButton * event)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);

  /* if any child is currently maximized, jump right back */
  if (xpaned->maximized[GTK_XPANED_TOP_LEFT] ||
      xpaned->maximized[GTK_XPANED_TOP_RIGHT] ||
      xpaned->maximized[GTK_XPANED_BOTTOM_LEFT] ||
      xpaned->maximized[GTK_XPANED_BOTTOM_RIGHT])
    return FALSE;

  /* if user is dragging the handles around */
  if (!xpaned->in_drag_vert_and_horiz &&
      event->window != xpaned->handle_east &&
      event->window != xpaned->handle_west &&
      event->window != xpaned->handle_north &&
      event->window != xpaned->handle_south &&
      event->window == xpaned->handle_middle && event->button == 1)
    {
      xpaned->in_drag_vert_and_horiz = TRUE;
      gtk_xpaned_button_press_grab (xpaned->handle_middle, event);
      xpaned->drag_pos.x = event->x;
      xpaned->drag_pos.y = event->y;

      return TRUE;
    }
  else if (!xpaned->in_drag_vert &&
           event->window == xpaned->handle_east &&
           event->window != xpaned->handle_west &&
           event->window != xpaned->handle_north &&
           event->window != xpaned->handle_south &&
           event->window != xpaned->handle_middle && event->button == 1)
    {
      xpaned->in_drag_vert = TRUE;
      gtk_xpaned_button_press_grab (xpaned->handle_east, event);
      xpaned->drag_pos.y = event->y;

      return TRUE;
    }
  else if (!xpaned->in_drag_vert &&
           event->window != xpaned->handle_east &&
           event->window == xpaned->handle_west &&
           event->window != xpaned->handle_north &&
           event->window != xpaned->handle_south &&
           event->window != xpaned->handle_middle && event->button == 1)
    {
      xpaned->in_drag_vert = TRUE;
      gtk_xpaned_button_press_grab (xpaned->handle_west, event);
      xpaned->drag_pos.y = event->y;

      return TRUE;
    }
  else if (!xpaned->in_drag_horiz &&
           event->window != xpaned->handle_east &&
           event->window != xpaned->handle_west &&
           event->window == xpaned->handle_north &&
           event->window != xpaned->handle_south &&
           event->window != xpaned->handle_middle && event->button == 1)
    {
      xpaned->in_drag_horiz = TRUE;
      gtk_xpaned_button_press_grab (xpaned->handle_north, event);
      xpaned->drag_pos.x = event->x;

      return TRUE;
    }
  else if (!xpaned->in_drag_horiz &&
           event->window != xpaned->handle_east &&
           event->window != xpaned->handle_west &&
           event->window != xpaned->handle_north &&
           event->window == xpaned->handle_south &&
           event->window != xpaned->handle_middle && event->button == 1)
    {
      xpaned->in_drag_horiz = TRUE;
      gtk_xpaned_button_press_grab (xpaned->handle_south, event);
      xpaned->drag_pos.x = event->x;

      return TRUE;
    }
  return FALSE;
}

static gboolean
gtk_xpaned_button_release (GtkWidget * widget, GdkEventButton * event)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);

  if (xpaned->in_drag_vert && (event->button == 1))
    {
      xpaned->in_drag_vert = FALSE;
      xpaned->drag_pos.y = -1;
      xpaned->position_set = TRUE;
      gdk_device_ungrab (event->device, event->time);
      return TRUE;
    }
  else if (xpaned->in_drag_horiz && (event->button == 1))
    {
      xpaned->in_drag_horiz = FALSE;
      xpaned->drag_pos.x = -1;
      xpaned->position_set = TRUE;
      gdk_device_ungrab (event->device, event->time);
      return TRUE;
    }
  else if (xpaned->in_drag_vert_and_horiz && (event->button == 1))
    {
      xpaned->in_drag_vert_and_horiz = FALSE;
      xpaned->drag_pos.x = -1;
      xpaned->drag_pos.y = -1;
      xpaned->position_set = TRUE;
      gdk_device_ungrab (event->device, event->time);
      return TRUE;
    }

  return FALSE;
}

static gboolean
gtk_xpaned_motion (GtkWidget * widget, GdkEventMotion * event)
{
  GtkXPaned *xpaned = GTK_XPANED (widget);

  if (xpaned->in_drag_vert ||
      xpaned->in_drag_horiz || xpaned->in_drag_vert_and_horiz)

    {
      update_drag (xpaned);
      return TRUE;
    }

  return FALSE;
}

void
gtk_xpaned_add_top_left (GtkXPaned * xpaned, GtkWidget * widget)
{
  gtk_xpaned_pack_top_left (xpaned, widget, FALSE, TRUE);
}

void
gtk_xpaned_add_top_right (GtkXPaned * xpaned, GtkWidget * widget)
{
  gtk_xpaned_pack_top_right (xpaned, widget, FALSE, TRUE);
}

void
gtk_xpaned_add_bottom_left (GtkXPaned * xpaned, GtkWidget * widget)
{
  gtk_xpaned_pack_bottom_left (xpaned, widget, FALSE, TRUE);
}

void
gtk_xpaned_add_bottom_right (GtkXPaned * xpaned, GtkWidget * widget)
{
  gtk_xpaned_pack_bottom_right (xpaned, widget, FALSE, TRUE);
}

void
gtk_xpaned_pack_top_left (GtkXPaned * xpaned,
                          GtkWidget * child, gboolean resize, gboolean shrink)
{
  g_return_if_fail (GTK_IS_XPANED (xpaned));
  g_return_if_fail (GTK_IS_WIDGET (child));

  if (!xpaned->top_left_child)
    {
      xpaned->top_left_child = child;
      xpaned->top_left_child_resize = resize;
      xpaned->top_left_child_shrink = shrink;

      gtk_widget_set_parent (child, GTK_WIDGET (xpaned));
    }
}

void
gtk_xpaned_pack_top_right (GtkXPaned * xpaned,
                           GtkWidget * child,
                           gboolean resize, gboolean shrink)
{
  g_return_if_fail (GTK_IS_XPANED (xpaned));
  g_return_if_fail (GTK_IS_WIDGET (child));

  if (!xpaned->top_right_child)
    {
      xpaned->top_right_child = child;
      xpaned->top_right_child_resize = resize;
      xpaned->top_right_child_shrink = shrink;

      gtk_widget_set_parent (child, GTK_WIDGET (xpaned));
    }
}

void
gtk_xpaned_pack_bottom_left (GtkXPaned * xpaned,
                             GtkWidget * child,
                             gboolean resize, gboolean shrink)
{
  g_return_if_fail (GTK_IS_XPANED (xpaned));
  g_return_if_fail (GTK_IS_WIDGET (child));

  if (!xpaned->bottom_left_child)
    {
      xpaned->bottom_left_child = child;
      xpaned->bottom_left_child_resize = resize;
      xpaned->bottom_left_child_shrink = shrink;

      gtk_widget_set_parent (child, GTK_WIDGET (xpaned));
    }
}

void
gtk_xpaned_pack_bottom_right (GtkXPaned * xpaned,
                              GtkWidget * child,
                              gboolean resize, gboolean shrink)
{
  g_return_if_fail (GTK_IS_XPANED (xpaned));
  g_return_if_fail (GTK_IS_WIDGET (child));

  if (!xpaned->bottom_right_child)
    {
      xpaned->bottom_right_child = child;
      xpaned->bottom_right_child_resize = resize;
      xpaned->bottom_right_child_shrink = shrink;

      gtk_widget_set_parent (child, GTK_WIDGET (xpaned));
    }
}

static void
gtk_xpaned_add (GtkContainer * container, GtkWidget * widget)
{
  GtkXPaned *xpaned;

  g_return_if_fail (GTK_IS_XPANED (container));

  xpaned = GTK_XPANED (container);

  if (!xpaned->top_left_child)
    gtk_xpaned_add_top_left (xpaned, widget);
  else if (!xpaned->top_right_child)
    gtk_xpaned_add_top_right (xpaned, widget);
  else if (!xpaned->bottom_left_child)
    gtk_xpaned_add_bottom_left (xpaned, widget);
  else if (!xpaned->bottom_right_child)
    gtk_xpaned_add_bottom_right (xpaned, widget);
  else
    g_warning ("GtkXPaned cannot have more than 4 children\n");
}

static void
gtk_xpaned_remove (GtkContainer * container, GtkWidget * widget)
{
  GtkXPaned *xpaned;
  gboolean was_visible;

  xpaned = GTK_XPANED (container);
  was_visible = gtk_widget_get_visible (widget);

  if (xpaned->top_left_child == widget)
    {
      gtk_widget_unparent (widget);

      xpaned->top_left_child = NULL;

      if (was_visible && gtk_widget_get_visible (GTK_WIDGET (container)))
        gtk_widget_queue_resize (GTK_WIDGET (container));
    }
  else if (xpaned->top_right_child == widget)
    {
      gtk_widget_unparent (widget);

      xpaned->top_right_child = NULL;

      if (was_visible && gtk_widget_get_visible (GTK_WIDGET (container)))
        gtk_widget_queue_resize (GTK_WIDGET (container));
    }
  else if (xpaned->bottom_left_child == widget)
    {
      gtk_widget_unparent (widget);

      xpaned->bottom_left_child = NULL;

      if (was_visible && gtk_widget_get_visible (GTK_WIDGET (container)))
        gtk_widget_queue_resize (GTK_WIDGET (container));
    }
  else if (xpaned->bottom_right_child == widget)
    {
      gtk_widget_unparent (widget);

      xpaned->bottom_right_child = NULL;

      if (was_visible && gtk_widget_get_visible (GTK_WIDGET (container)))
        gtk_widget_queue_resize (GTK_WIDGET (container));
    }
  else
    g_warning ("GtkXPaned has no more children attached\n");

}

static void
gtk_xpaned_forall (GtkContainer * container,
                   gboolean include_internals,
                   GtkCallback callback, gpointer callback_data)
{
  GtkXPaned *xpaned;

  g_return_if_fail (callback != NULL);

  xpaned = GTK_XPANED (container);

  if (xpaned->top_left_child)
    (*callback) (xpaned->top_left_child, callback_data);
  if (xpaned->top_right_child)
    (*callback) (xpaned->top_right_child, callback_data);
  if (xpaned->bottom_left_child)
    (*callback) (xpaned->bottom_left_child, callback_data);
  if (xpaned->bottom_right_child)
    (*callback) (xpaned->bottom_right_child, callback_data);
}

/**
 * gtk_xpaned_get_position_x:
 * @paned: a #GtkXPaned widget
 * 
 * Obtains the x-position of the divider.
 * 
 * Return value: x-position of the divider
 **/
gint
gtk_xpaned_get_position_x (GtkXPaned * xpaned)
{
  g_return_val_if_fail (GTK_IS_XPANED (xpaned), 0);

  return xpaned->top_left_child_size.width;
}

/**
 * gtk_xpaned_get_position_y:
 * @paned: a #GtkXPaned widget
 * 
 * Obtains the y-position of the divider.
 * 
 * Return value: y-position of the divider
 **/
gint
gtk_xpaned_get_position_y (GtkXPaned * xpaned)
{
  g_return_val_if_fail (GTK_IS_XPANED (xpaned), 0);

  return xpaned->top_left_child_size.height;
}

/**
 * gtk_xpaned_set_position_x:
 * @paned: a #GtkXPaned widget
 * @xposition: pixel x-position of divider, a negative values
 * 			   of a component mean that the position is unset.
 * 
 * Sets the x-position of the divider between the four panes.
 **/
void
gtk_xpaned_set_position_x (GtkXPaned * xpaned, gint xposition)
{
  GObject *object;

  g_return_if_fail (GTK_IS_XPANED (xpaned));

  /* if any child is currently maximized, jump right back */
  if (xpaned->maximized[GTK_XPANED_TOP_LEFT] ||
      xpaned->maximized[GTK_XPANED_TOP_RIGHT] ||
      xpaned->maximized[GTK_XPANED_BOTTOM_LEFT] ||
      xpaned->maximized[GTK_XPANED_BOTTOM_RIGHT])
    return;

  object = G_OBJECT (xpaned);

  if (xposition >= 0)
    {
      /* We don't clamp here - the assumption is that
       * if the total allocation changes at the same time
       * as the position, the position set is with reference
       * to the new total size. If only the position changes,
       * then clamping will occur in gtk_paned_compute_position()
       */

      xpaned->top_left_child_size.width = xposition;
      xpaned->position_set = TRUE;
    }
  else
    {
      xpaned->position_set = FALSE;
    }

  g_object_freeze_notify (object);
  g_object_notify (object, "x-position");
  g_object_notify (object, "position-set");
  g_object_thaw_notify (object);

  gtk_widget_queue_resize (GTK_WIDGET (xpaned));
}

/**
 * gtk_xpaned_set_position_y:
 * @paned: a #GtkXPaned widget
 * @yposition: pixel y-position of divider, a negative values
 * 			   of a component mean that the position is unset.
 * 
 * Sets the y-position of the divider between the four panes.
 **/
void
gtk_xpaned_set_position_y (GtkXPaned * xpaned, gint yposition)
{
  GObject *object;

  g_return_if_fail (GTK_IS_XPANED (xpaned));

  /* if any child is currently maximized, jump right back */
  if (xpaned->maximized[GTK_XPANED_TOP_LEFT] ||
      xpaned->maximized[GTK_XPANED_TOP_RIGHT] ||
      xpaned->maximized[GTK_XPANED_BOTTOM_LEFT] ||
      xpaned->maximized[GTK_XPANED_BOTTOM_RIGHT])
    return;

  object = G_OBJECT (xpaned);

  if (yposition >= 0)
    {
      /* We don't clamp here - the assumption is that
       * if the total allocation changes at the same time
       * as the position, the position set is with reference
       * to the new total size. If only the position changes,
       * then clamping will occur in gtk_paned_compute_position()
       */

      xpaned->top_left_child_size.height = yposition;
      xpaned->position_set = TRUE;
    }
  else
    {
      xpaned->position_set = FALSE;
    }

  g_object_freeze_notify (object);
  g_object_notify (object, "y-position");
  g_object_notify (object, "position-set");
  g_object_thaw_notify (object);

  gtk_widget_queue_resize (GTK_WIDGET (xpaned));
}

/* this call is private and only intended for internal use! */
void
gtk_xpaned_save_unmaximized_x (GtkXPaned * xpaned)
{
  xpaned->unmaximized_position.x = gtk_xpaned_get_position_x (xpaned);
}

/* this call is private and only intended for internal use! */
void
gtk_xpaned_save_unmaximized_y (GtkXPaned * xpaned)
{
  xpaned->unmaximized_position.y = gtk_xpaned_get_position_y (xpaned);
}

/* this call is private and only intended for internal use! */
gint
gtk_xpaned_fetch_unmaximized_x (GtkXPaned * xpaned)
{
  return xpaned->unmaximized_position.x;
}

/* this call is private and only intended for internal use! */
gint
gtk_xpaned_fetch_unmaximized_y (GtkXPaned * xpaned)
{
  return xpaned->unmaximized_position.y;
}

/**
 * gtk_xpaned_get_top_left_child:
 * @xpaned: a #GtkXPaned widget
 * 
 * Obtains the top-left child of the xpaned widget.
 * 
 * Return value: top-left child, or %NULL if it is not set.
 *
 * Since: 2.4
 **/
GtkWidget *
gtk_xpaned_get_top_left_child (GtkXPaned * xpaned)
{
  g_return_val_if_fail (GTK_IS_XPANED (xpaned), NULL);

  return xpaned->top_left_child;
}

/**
 * gtk_xpaned_get_top_right_child:
 * @xpaned: a #GtkXPaned widget
 * 
 * Obtains the top-right child of the xpaned widget.
 * 
 * Return value: top-right child, or %NULL if it is not set.
 *
 * Since: 2.4
 **/
GtkWidget *
gtk_xpaned_get_top_right_child (GtkXPaned * xpaned)
{
  g_return_val_if_fail (GTK_IS_XPANED (xpaned), NULL);

  return xpaned->top_right_child;
}

/**
 * gtk_xpaned_get_bottom_left_child:
 * @xpaned: a #GtkXPaned widget
 * 
 * Obtains the bottom-left child of the xpaned widget.
 * 
 * Return value: bottom-left child, or %NULL if it is not set.
 *
 * Since: 2.4
 **/
GtkWidget *
gtk_xpaned_get_bottom_left_child (GtkXPaned * xpaned)
{
  g_return_val_if_fail (GTK_IS_XPANED (xpaned), NULL);

  return xpaned->bottom_left_child;
}

/**
 * gtk_xpaned_get_bottom_right_child:
 * @xpaned: a #GtkXPaned widget
 * 
 * Obtains the bottom-right child of the xpaned widget.
 * 
 * Return value: bottom-right child, or %NULL if it is not set.
 *
 * Since: 2.4
 **/
GtkWidget *
gtk_xpaned_get_bottom_right_child (GtkXPaned * xpaned)
{
  g_return_val_if_fail (GTK_IS_XPANED (xpaned), NULL);

  return xpaned->bottom_right_child;
}

gboolean
gtk_xpaned_maximize_top_left (GtkXPaned * xpaned, gboolean maximize)
{
  if (maximize)
    {
      /* see if any child is already maximized */
      if (!xpaned->maximized[GTK_XPANED_TOP_LEFT] &&
          !xpaned->maximized[GTK_XPANED_TOP_RIGHT] &&
          !xpaned->maximized[GTK_XPANED_BOTTOM_LEFT] &&
          !xpaned->maximized[GTK_XPANED_BOTTOM_RIGHT])
        {
          /* save current position */
          gtk_xpaned_save_unmaximized_x (xpaned);
          gtk_xpaned_save_unmaximized_y (xpaned);

          /* set new maximized position */
          gtk_xpaned_set_position_x (xpaned, xpaned->max_position.x);
          gtk_xpaned_set_position_y (xpaned, xpaned->max_position.y);

          /* mark maximized flag for top-left child */
          xpaned->maximized[GTK_XPANED_TOP_LEFT] = TRUE;

          return TRUE;
        }
      /* already one child maximized, report error */
      else
        return FALSE;
    }
  else
    {
      /* verify that top-left child is really currently maximized */
      if (xpaned->maximized[GTK_XPANED_TOP_LEFT])
        {
          /* clear maximized flat for top-left child */
          xpaned->maximized[GTK_XPANED_TOP_LEFT] = FALSE;

          /* restore unmaximized position */
          gtk_xpaned_set_position_x (xpaned,
                                     gtk_xpaned_fetch_unmaximized_x (xpaned));
          gtk_xpaned_set_position_y (xpaned,
                                     gtk_xpaned_fetch_unmaximized_y (xpaned));

          return TRUE;
        }
      /* top-left child is currently not maximized, report error */
      else
        return FALSE;
    }
}

gboolean
gtk_xpaned_maximize_top_right (GtkXPaned * xpaned, gboolean maximize)
{
  if (maximize)
    {
      /* see if any child is already maximized */
      if (!xpaned->maximized[GTK_XPANED_TOP_LEFT] &&
          !xpaned->maximized[GTK_XPANED_TOP_RIGHT] &&
          !xpaned->maximized[GTK_XPANED_BOTTOM_LEFT] &&
          !xpaned->maximized[GTK_XPANED_BOTTOM_RIGHT])
        {
          /* save current position */
          gtk_xpaned_save_unmaximized_x (xpaned);
          gtk_xpaned_save_unmaximized_y (xpaned);

          /* set new maximized position */
          gtk_xpaned_set_position_x (xpaned, xpaned->min_position.x);
          gtk_xpaned_set_position_y (xpaned, xpaned->max_position.y);

          /* mark maximized flag for top-right child */
          xpaned->maximized[GTK_XPANED_TOP_RIGHT] = TRUE;

          return TRUE;
        }
      /* already one child maximized, report error */
      else
        return FALSE;
    }
  else
    {
      /* verify that top-right child is really currently maximized */
      if (xpaned->maximized[GTK_XPANED_TOP_RIGHT])
        {
          /* clear maximized flat for top-right child */
          xpaned->maximized[GTK_XPANED_TOP_RIGHT] = FALSE;

          /* restore unmaximized position */
          gtk_xpaned_set_position_x (xpaned,
                                     gtk_xpaned_fetch_unmaximized_x (xpaned));
          gtk_xpaned_set_position_y (xpaned,
                                     gtk_xpaned_fetch_unmaximized_y (xpaned));

          return TRUE;
        }
      /* top-right child is currently not maximized, report error */
      else
        return FALSE;
    }
}

gboolean
gtk_xpaned_maximize_bottom_left (GtkXPaned * xpaned, gboolean maximize)
{
  if (maximize)
    {
      /* see if any child is already maximized */
      if (!xpaned->maximized[GTK_XPANED_TOP_LEFT] &&
          !xpaned->maximized[GTK_XPANED_TOP_RIGHT] &&
          !xpaned->maximized[GTK_XPANED_BOTTOM_LEFT] &&
          !xpaned->maximized[GTK_XPANED_BOTTOM_RIGHT])
        {
          /* save current position */
          gtk_xpaned_save_unmaximized_x (xpaned);
          gtk_xpaned_save_unmaximized_y (xpaned);

          /* set new maximized position */
          gtk_xpaned_set_position_x (xpaned, xpaned->max_position.x);
          gtk_xpaned_set_position_y (xpaned, xpaned->min_position.y);

          /* mark maximized flag for bottom-left child */
          xpaned->maximized[GTK_XPANED_BOTTOM_LEFT] = TRUE;

          return TRUE;
        }
      /* already one child maximized, report error */
      else
        return FALSE;
    }
  else
    {
      /* verify that bottom-left child is really currently maximized */
      if (xpaned->maximized[GTK_XPANED_BOTTOM_LEFT])
        {
          /* clear maximized flat for bottom-left child */
          xpaned->maximized[GTK_XPANED_BOTTOM_LEFT] = FALSE;

          /* restore unmaximized position */
          gtk_xpaned_set_position_x (xpaned,
                                     gtk_xpaned_fetch_unmaximized_x (xpaned));
          gtk_xpaned_set_position_y (xpaned,
                                     gtk_xpaned_fetch_unmaximized_y (xpaned));

          return TRUE;
        }
      /* bottom-left child is currently not maximized, report error */
      else
        return FALSE;
    }
}

gboolean
gtk_xpaned_maximize_bottom_right (GtkXPaned * xpaned, gboolean maximize)
{
  if (maximize)
    {
      /* see if any child is already maximized */
      if (!xpaned->maximized[GTK_XPANED_TOP_LEFT] &&
          !xpaned->maximized[GTK_XPANED_TOP_RIGHT] &&
          !xpaned->maximized[GTK_XPANED_BOTTOM_LEFT] &&
          !xpaned->maximized[GTK_XPANED_BOTTOM_RIGHT])
        {
          /* save current position */
          gtk_xpaned_save_unmaximized_x (xpaned);
          gtk_xpaned_save_unmaximized_y (xpaned);

          /* set new maximized position */
          gtk_xpaned_set_position_x (xpaned, xpaned->min_position.x);
          gtk_xpaned_set_position_y (xpaned, xpaned->min_position.y);

          /* mark maximized flag for bottom-right child */
          xpaned->maximized[GTK_XPANED_BOTTOM_RIGHT] = TRUE;

          return TRUE;
        }
      /* already one child maximized, report error */
      else
        return FALSE;
    }
  else
    {
      /* verify that bottom-right child is really currently maximized */
      if (xpaned->maximized[GTK_XPANED_BOTTOM_RIGHT])
        {
          /* clear maximized flat for bottom-right child */
          xpaned->maximized[GTK_XPANED_BOTTOM_RIGHT] = FALSE;

          /* restore unmaximized position */
          gtk_xpaned_set_position_x (xpaned,
                                     gtk_xpaned_fetch_unmaximized_x (xpaned));
          gtk_xpaned_set_position_y (xpaned,
                                     gtk_xpaned_fetch_unmaximized_y (xpaned));

          return TRUE;
        }
      /* bottom-right child is currently not maximized, report error */
      else
        return FALSE;
    }
}

void
gtk_xpaned_compute_position (GtkXPaned * xpaned,
                             const GtkAllocation * allocation,
                             GtkRequisition * top_left_child_req,
                             GtkRequisition * top_right_child_req,
                             GtkRequisition * bottom_left_child_req,
                             GtkRequisition * bottom_right_child_req)
{
  GdkPoint old_position;
  GdkPoint old_min_position;
  GdkPoint old_max_position;
  gint handle_size;
  gint border_width = gtk_container_get_border_width (GTK_CONTAINER (xpaned));

  g_return_if_fail (GTK_IS_XPANED (xpaned));

  old_position.x = xpaned->top_left_child_size.width;
  old_position.y = xpaned->top_left_child_size.height;
  old_min_position.x = xpaned->min_position.x;
  old_min_position.y = xpaned->min_position.y;
  old_max_position.x = xpaned->max_position.x;
  old_max_position.y = xpaned->max_position.y;

  xpaned->min_position.x =
    xpaned->top_left_child_shrink ? 0 : top_left_child_req->width;
  xpaned->min_position.y =
    xpaned->top_left_child_shrink ? 0 : top_left_child_req->height;

  gtk_widget_style_get (GTK_WIDGET (xpaned), "handle-size", &handle_size,
                        NULL);

  xpaned->max_position.x = allocation->width - 2 * border_width - handle_size;
  xpaned->max_position.y =
    allocation->height - 2 * border_width - handle_size;
  if (!xpaned->top_left_child_shrink)
    xpaned->max_position.x =
      MAX (1, xpaned->max_position.x - top_left_child_req->width);
  xpaned->max_position.x =
    MAX (xpaned->min_position.x, xpaned->max_position.x);

  if (!xpaned->position_set)
    {
      if (xpaned->top_left_child_resize && !xpaned->top_right_child_resize)
        {
          xpaned->top_left_child_size.width =
            MAX (0, allocation->width - top_right_child_req->width);
          xpaned->top_left_child_size.height =
            MAX (0, allocation->height - top_right_child_req->height);
        }
      else if (!xpaned->top_left_child_resize
               && xpaned->top_right_child_resize)
        {
          xpaned->top_left_child_size.width = top_left_child_req->width;
          xpaned->top_left_child_size.height = top_left_child_req->height;
        }
      else
        {
          xpaned->top_left_child_size.width = allocation->width * 0.5 + 0.5;
          xpaned->top_left_child_size.height = allocation->height * 0.5 + 0.5;
        }
    }
  else
    {
      /* If the position was set before the initial allocation.
      ** (paned->last_allocation <= 0) just clamp it and leave it. */
      if (xpaned->last_allocation.width > 0)
        {
          if (xpaned->top_left_child_resize
              && !xpaned->top_right_child_resize)
            {
              xpaned->top_left_child_size.width += allocation->width
                - xpaned->last_allocation.width;

              xpaned->top_left_child_size.height += allocation->height
                - xpaned->last_allocation.height;
            }
          else
            if (!
                (!xpaned->top_left_child_resize
                 && xpaned->top_right_child_resize))
              {
                xpaned->top_left_child_size.width = allocation->width
                  * ((gdouble) xpaned->top_left_child_size.width /
                     (xpaned->last_allocation.width)) + 0.5;

                xpaned->top_left_child_size.height = allocation->height
                  * ((gdouble) xpaned->top_left_child_size.height /
                     (xpaned->last_allocation.height)) + 0.5;
              }
        }
      if (xpaned->last_allocation.height > 0)
        {
          if (xpaned->top_left_child_resize
              && !xpaned->top_right_child_resize)
            {
              xpaned->top_left_child_size.width +=
                allocation->width - xpaned->last_allocation.width;
              xpaned->top_left_child_size.height +=
                allocation->height - xpaned->last_allocation.height;
            }
          else
            if (!
                (!xpaned->top_left_child_resize
                 && xpaned->top_right_child_resize))
              {
                xpaned->top_left_child_size.width =
                  allocation->width *
                  ((gdouble) xpaned->top_left_child_size.width /
                   (xpaned->last_allocation.width)) + 0.5;
                xpaned->top_left_child_size.height =
                  allocation->height *
                  ((gdouble) xpaned->top_left_child_size.height /
                   (xpaned->last_allocation.height)) + 0.5;
              }
        }

    }

  xpaned->top_left_child_size.width =
    CLAMP (xpaned->top_left_child_size.width, xpaned->min_position.x,
           xpaned->max_position.x);
  xpaned->top_left_child_size.height =
    CLAMP (xpaned->top_left_child_size.height, xpaned->min_position.y,
           xpaned->max_position.y);

  xpaned->top_right_child_size.width =
    CLAMP (xpaned->top_right_child_size.width, xpaned->min_position.x,
           xpaned->max_position.x);
  xpaned->top_right_child_size.height =
    CLAMP (xpaned->top_right_child_size.height, xpaned->min_position.y,
           xpaned->max_position.y);

  xpaned->bottom_left_child_size.width =
    CLAMP (xpaned->bottom_left_child_size.width, xpaned->min_position.x,
           xpaned->max_position.x);
  xpaned->bottom_left_child_size.height =
    CLAMP (xpaned->bottom_left_child_size.height, xpaned->min_position.y,
           xpaned->max_position.y);

  xpaned->bottom_right_child_size.width =
    CLAMP (xpaned->bottom_right_child_size.width, xpaned->min_position.x,
           xpaned->max_position.x);
  xpaned->bottom_right_child_size.height =
    CLAMP (xpaned->bottom_right_child_size.height, xpaned->min_position.y,
           xpaned->max_position.y);

  gtk_widget_set_child_visible (xpaned->top_left_child, TRUE);
  gtk_widget_set_child_visible (xpaned->top_right_child, TRUE);
  gtk_widget_set_child_visible (xpaned->bottom_left_child, TRUE);
  gtk_widget_set_child_visible (xpaned->bottom_right_child, TRUE);

  g_object_freeze_notify (G_OBJECT (xpaned));

  if (xpaned->top_left_child_size.width != old_position.x)
    g_object_notify (G_OBJECT (xpaned), "x-position");
  if (xpaned->top_left_child_size.height != old_position.y)
    g_object_notify (G_OBJECT (xpaned), "y-position");

  if (xpaned->top_right_child_size.width != old_position.x)
    g_object_notify (G_OBJECT (xpaned), "x-position");
  if (xpaned->top_right_child_size.height != old_position.y)
    g_object_notify (G_OBJECT (xpaned), "y-position");

  if (xpaned->bottom_left_child_size.width != old_position.x)
    g_object_notify (G_OBJECT (xpaned), "x-position");
  if (xpaned->bottom_left_child_size.height != old_position.y)
    g_object_notify (G_OBJECT (xpaned), "y-position");

  if (xpaned->bottom_right_child_size.width != old_position.x)
    g_object_notify (G_OBJECT (xpaned), "x-position");
  if (xpaned->bottom_right_child_size.height != old_position.y)
    g_object_notify (G_OBJECT (xpaned), "y-position");

  if (xpaned->min_position.x != old_min_position.x)
    g_object_notify (G_OBJECT (xpaned), "min-x-position");
  if (xpaned->min_position.y != old_min_position.y)
    g_object_notify (G_OBJECT (xpaned), "min-y-position");

  if (xpaned->max_position.x != old_max_position.x)
    g_object_notify (G_OBJECT (xpaned), "max-y-position");
  if (xpaned->max_position.y != old_max_position.y)
    g_object_notify (G_OBJECT (xpaned), "max-y-position");

  g_object_thaw_notify (G_OBJECT (xpaned));

  xpaned->last_allocation.width = allocation->width;
  xpaned->last_allocation.height = allocation->height;
}

static void
gtk_xpaned_set_saved_focus (GtkXPaned * xpaned, GtkWidget * widget)
{
  if (xpaned->priv->saved_focus)
    g_object_remove_weak_pointer (G_OBJECT (xpaned->priv->saved_focus),
                                  (gpointer *) & (xpaned->priv->saved_focus));

  xpaned->priv->saved_focus = widget;

  if (xpaned->priv->saved_focus)
    g_object_add_weak_pointer (G_OBJECT (xpaned->priv->saved_focus),
                               (gpointer *) & (xpaned->priv->saved_focus));
}

static void
gtk_xpaned_set_first_xpaned (GtkXPaned * xpaned, GtkXPaned * first_xpaned)
{
  if (xpaned->priv->first_xpaned)
    g_object_remove_weak_pointer (G_OBJECT (xpaned->priv->first_xpaned),
                                  (gpointer *) & (xpaned->priv->
                                                  first_xpaned));

  xpaned->priv->first_xpaned = first_xpaned;

  if (xpaned->priv->first_xpaned)
    g_object_add_weak_pointer (G_OBJECT (xpaned->priv->first_xpaned),
                               (gpointer *) & (xpaned->priv->first_xpaned));
}

static void
gtk_xpaned_set_last_top_left_child_focus (GtkXPaned * xpaned,
                                          GtkWidget * widget)
{
  if (xpaned->last_top_left_child_focus)
    g_object_remove_weak_pointer (G_OBJECT
                                  (xpaned->last_top_left_child_focus),
                                  (gpointer *) & (xpaned->
                                                  last_top_left_child_focus));

  xpaned->last_top_left_child_focus = widget;

  if (xpaned->last_top_left_child_focus)
    g_object_add_weak_pointer (G_OBJECT (xpaned->last_top_left_child_focus),
                               (gpointer *) & (xpaned->
                                               last_top_left_child_focus));
}

static void
gtk_xpaned_set_last_top_right_child_focus (GtkXPaned * xpaned,
                                           GtkWidget * widget)
{
  if (xpaned->last_top_right_child_focus)
    g_object_remove_weak_pointer (G_OBJECT
                                  (xpaned->last_top_right_child_focus),
                                  (gpointer *) & (xpaned->
                                                  last_top_right_child_focus));

  xpaned->last_top_right_child_focus = widget;

  if (xpaned->last_top_right_child_focus)
    g_object_add_weak_pointer (G_OBJECT (xpaned->last_top_right_child_focus),
                               (gpointer *) & (xpaned->
                                               last_top_right_child_focus));
}

static void
gtk_xpaned_set_last_bottom_left_child_focus (GtkXPaned * xpaned,
                                             GtkWidget * widget)
{
  if (xpaned->last_bottom_left_child_focus)
    g_object_remove_weak_pointer (G_OBJECT
                                  (xpaned->last_bottom_left_child_focus),
                                  (gpointer *) & (xpaned->
                                                  last_bottom_left_child_focus));

  xpaned->last_bottom_left_child_focus = widget;

  if (xpaned->last_bottom_left_child_focus)
    g_object_add_weak_pointer (G_OBJECT
                               (xpaned->last_bottom_left_child_focus),
                               (gpointer *) & (xpaned->
                                               last_bottom_left_child_focus));
}

static void
gtk_xpaned_set_last_bottom_right_child_focus (GtkXPaned * xpaned,
                                              GtkWidget * widget)
{
  if (xpaned->last_bottom_right_child_focus)
    g_object_remove_weak_pointer (G_OBJECT
                                  (xpaned->last_bottom_right_child_focus),
                                  (gpointer *) & (xpaned->
                                                  last_bottom_right_child_focus));

  xpaned->last_bottom_right_child_focus = widget;

  if (xpaned->last_bottom_right_child_focus)
    g_object_add_weak_pointer (G_OBJECT
                               (xpaned->last_bottom_right_child_focus),
                               (gpointer *) & (xpaned->
                                               last_bottom_right_child_focus));
}

static GtkWidget *
xpaned_get_focus_widget (GtkXPaned * xpaned)
{
  GtkWidget *toplevel;

  toplevel = gtk_widget_get_toplevel (GTK_WIDGET (xpaned));
  if (gtk_widget_is_toplevel (toplevel))
    return gtk_window_get_focus (GTK_WINDOW (toplevel));

  return NULL;
}

static void
gtk_xpaned_set_focus_child (GtkContainer * container, GtkWidget * focus_child)
{
  GtkXPaned *xpaned;

  g_return_if_fail (GTK_IS_XPANED (container));

  xpaned = GTK_XPANED (container);

  if (focus_child == NULL)
    {
      GtkWidget *last_focus;
      GtkWidget *w;

      last_focus = xpaned_get_focus_widget (xpaned);

      if (last_focus)
        {
          /* If there is one or more paned widgets between us and the
           * focus widget, we want the topmost of those as last_focus
           */
          for (w = last_focus; w != GTK_WIDGET (xpaned); w = gtk_widget_get_parent (w))
            if (GTK_IS_XPANED (w))
              last_focus = w;

          if (gtk_container_get_focus_child (container) == xpaned->top_left_child)
            gtk_xpaned_set_last_top_left_child_focus (xpaned, last_focus);
          else if (gtk_container_get_focus_child (container) == xpaned->top_right_child)
            gtk_xpaned_set_last_top_right_child_focus (xpaned, last_focus);
          else if (gtk_container_get_focus_child (container) == xpaned->bottom_left_child)
            gtk_xpaned_set_last_bottom_left_child_focus (xpaned, last_focus);
          else if (gtk_container_get_focus_child (container) == xpaned->bottom_right_child)
            gtk_xpaned_set_last_bottom_right_child_focus (xpaned, last_focus);
        }
    }

  if (parent_class->set_focus_child)
    (*parent_class->set_focus_child) (container, focus_child);
}

static void
gtk_xpaned_get_cycle_chain (GtkXPaned * xpaned,
                            GtkDirectionType direction, GList ** widgets)
{
  GtkContainer *container = GTK_CONTAINER (xpaned);
  GtkWidget *ancestor = NULL;
  GList *temp_list = NULL;
  GList *list;

  if (xpaned->in_recursion)
    return;

  g_assert (widgets != NULL);

  if (xpaned->last_top_left_child_focus &&
      !gtk_widget_is_ancestor (xpaned->last_top_left_child_focus,
                               GTK_WIDGET (xpaned)))
    {
      gtk_xpaned_set_last_top_left_child_focus (xpaned, NULL);
    }

  if (xpaned->last_top_right_child_focus &&
      !gtk_widget_is_ancestor (xpaned->last_top_right_child_focus,
                               GTK_WIDGET (xpaned)))
    {
      gtk_xpaned_set_last_top_right_child_focus (xpaned, NULL);
    }

  if (xpaned->last_bottom_left_child_focus &&
      !gtk_widget_is_ancestor (xpaned->last_bottom_left_child_focus,
                               GTK_WIDGET (xpaned)))
    {
      gtk_xpaned_set_last_bottom_left_child_focus (xpaned, NULL);
    }

  if (xpaned->last_bottom_right_child_focus &&
      !gtk_widget_is_ancestor (xpaned->last_bottom_right_child_focus,
                               GTK_WIDGET (xpaned)))
    {
      gtk_xpaned_set_last_bottom_right_child_focus (xpaned, NULL);
    }

  if (gtk_widget_get_parent (GTK_WIDGET (xpaned)))
    ancestor = gtk_widget_get_ancestor (gtk_widget_get_parent (GTK_WIDGET (xpaned)),
                                        GTK_TYPE_XPANED);

  /* The idea here is that temp_list is a list of widgets we want to cycle
   * to. The list is prioritized so that the first element is our first
   * choice, the next our second, and so on.
   *
   * We can't just use g_list_reverse(), because we want to try
   * paned->last_child?_focus before paned->child?, both when we
   * are going forward and backward.
   */
  if (direction == GTK_DIR_TAB_FORWARD)
    {
      if (gtk_container_get_focus_child (container) == xpaned->top_left_child)
        {
          temp_list =
            g_list_append (temp_list, xpaned->last_top_right_child_focus);
          temp_list = g_list_append (temp_list, xpaned->top_right_child);
          temp_list = g_list_append (temp_list, ancestor);
        }
      else if (gtk_container_get_focus_child (container) == xpaned->top_right_child)
        {
          temp_list = g_list_append (temp_list, ancestor);
          temp_list =
            g_list_append (temp_list, xpaned->last_bottom_left_child_focus);
          temp_list = g_list_append (temp_list, xpaned->bottom_left_child);
        }
      else if (gtk_container_get_focus_child (container) == xpaned->bottom_left_child)
        {
          temp_list = g_list_append (temp_list, ancestor);
          temp_list =
            g_list_append (temp_list, xpaned->last_bottom_right_child_focus);
          temp_list = g_list_append (temp_list, xpaned->bottom_right_child);
        }
      else if (gtk_container_get_focus_child (container) == xpaned->bottom_right_child)
        {
          temp_list = g_list_append (temp_list, ancestor);
          temp_list =
            g_list_append (temp_list, xpaned->last_top_left_child_focus);
          temp_list = g_list_append (temp_list, xpaned->top_left_child);
        }
      else
        {
          temp_list =
            g_list_append (temp_list, xpaned->last_top_left_child_focus);
          temp_list = g_list_append (temp_list, xpaned->top_left_child);
          temp_list =
            g_list_append (temp_list, xpaned->last_top_right_child_focus);
          temp_list = g_list_append (temp_list, xpaned->top_right_child);
          temp_list =
            g_list_append (temp_list, xpaned->last_bottom_left_child_focus);
          temp_list = g_list_append (temp_list, xpaned->bottom_left_child);
          temp_list =
            g_list_append (temp_list, xpaned->last_bottom_right_child_focus);
          temp_list = g_list_append (temp_list, xpaned->bottom_right_child);
          temp_list = g_list_append (temp_list, ancestor);
        }
    }
  else
    {
      if (gtk_container_get_focus_child (container) == xpaned->top_left_child)
        {
          temp_list = g_list_append (temp_list, ancestor);
          temp_list =
            g_list_append (temp_list, xpaned->last_top_right_child_focus);
          temp_list = g_list_append (temp_list, xpaned->top_right_child);
        }
      else if (gtk_container_get_focus_child (container) == xpaned->top_right_child)
        {
          temp_list =
            g_list_append (temp_list, xpaned->last_bottom_left_child_focus);
          temp_list = g_list_append (temp_list, xpaned->bottom_left_child);
          temp_list = g_list_append (temp_list, ancestor);
        }
      else if (gtk_container_get_focus_child (container) == xpaned->bottom_right_child)
        {
          temp_list =
            g_list_append (temp_list, xpaned->last_bottom_left_child_focus);
          temp_list = g_list_append (temp_list, xpaned->bottom_left_child);
          temp_list = g_list_append (temp_list, ancestor);
        }
      else if (gtk_container_get_focus_child (container) == xpaned->top_right_child)
        {
          temp_list =
            g_list_append (temp_list, xpaned->last_bottom_left_child_focus);
          temp_list = g_list_append (temp_list, xpaned->bottom_left_child);
          temp_list = g_list_append (temp_list, ancestor);
        }
      else
        {
          temp_list =
            g_list_append (temp_list, xpaned->last_bottom_right_child_focus);
          temp_list = g_list_append (temp_list, xpaned->bottom_right_child);
          temp_list =
            g_list_append (temp_list, xpaned->last_bottom_left_child_focus);
          temp_list = g_list_append (temp_list, xpaned->bottom_left_child);
          temp_list =
            g_list_append (temp_list, xpaned->last_top_right_child_focus);
          temp_list = g_list_append (temp_list, xpaned->top_right_child);
          temp_list =
            g_list_append (temp_list, xpaned->last_top_left_child_focus);
          temp_list = g_list_append (temp_list, xpaned->top_left_child);
          temp_list = g_list_append (temp_list, ancestor);
        }
    }

  /* Walk the list and expand all the paned widgets. */
  for (list = temp_list; list != NULL; list = list->next)
    {
      GtkWidget *widget = list->data;

      if (widget)
        {
          if (GTK_IS_XPANED (widget))
            {
              xpaned->in_recursion = TRUE;
              gtk_xpaned_get_cycle_chain (GTK_XPANED (widget),
                                          direction, widgets);
              xpaned->in_recursion = FALSE;
            }
          else
            {
              *widgets = g_list_append (*widgets, widget);
            }
        }
    }

  g_list_free (temp_list);
}

static gboolean
gtk_xpaned_cycle_child_focus (GtkXPaned * xpaned, gboolean reversed)
{
  GList *cycle_chain = NULL;
  GList *list;

  GtkDirectionType direction =
    reversed ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD;

  /* ignore f6 if the handle is focused */
  if (gtk_widget_is_focus (GTK_WIDGET (xpaned)))
    return TRUE;

  /* we can't just let the event propagate up the hierarchy,
   * because the paned will want to cycle focus _unless_ an
   * ancestor paned handles the event
   */
  gtk_xpaned_get_cycle_chain (xpaned, direction, &cycle_chain);

  for (list = cycle_chain; list != NULL; list = list->next)
    if (gtk_widget_child_focus (GTK_WIDGET (list->data), direction))
      break;

  g_list_free (cycle_chain);

  return TRUE;
}

static void
get_child_xpanes (GtkWidget * widget, GList ** xpanes)
{
  if (GTK_IS_XPANED (widget))
    {
      GtkXPaned *xpaned = GTK_XPANED (widget);

      get_child_xpanes (xpaned->top_left_child, xpanes);
      *xpanes = g_list_prepend (*xpanes, widget);
      get_child_xpanes (xpaned->top_right_child, xpanes);
      *xpanes = g_list_prepend (*xpanes, widget);
      get_child_xpanes (xpaned->bottom_left_child, xpanes);
      *xpanes = g_list_prepend (*xpanes, widget);
      get_child_xpanes (xpaned->bottom_right_child, xpanes);
    }
  else if (GTK_IS_CONTAINER (widget))
    {
      gtk_container_foreach (GTK_CONTAINER (widget),
                             (GtkCallback) get_child_xpanes, xpanes);
    }
}

static GList *
get_all_xpanes (GtkXPaned * xpaned)
{
  GtkXPaned *topmost = NULL;
  GList *result = NULL;
  GtkWidget *w;

  for (w = GTK_WIDGET (xpaned); w != NULL; w = gtk_widget_get_parent (w))
    {
      if (GTK_IS_XPANED (w))
        topmost = GTK_XPANED (w);
    }

  g_assert (topmost);

  get_child_xpanes (GTK_WIDGET (topmost), &result);

  return g_list_reverse (result);
}

static void
gtk_xpaned_find_neighbours (GtkXPaned * xpaned,
                            GtkXPaned ** next, GtkXPaned ** prev)
{
  GList *all_xpanes;
  GList *this_link;

  all_xpanes = get_all_xpanes (xpaned);
  g_assert (all_xpanes);

  this_link = g_list_find (all_xpanes, xpaned);

  g_assert (this_link);

  if (this_link->next)
    *next = this_link->next->data;
  else
    *next = all_xpanes->data;

  if (this_link->prev)
    *prev = this_link->prev->data;
  else
    *prev = g_list_last (all_xpanes)->data;

  g_list_free (all_xpanes);
}

static gboolean
gtk_xpaned_move_handle (GtkXPaned * xpaned, GtkScrollType scroll)
{
  if (gtk_widget_is_focus (GTK_WIDGET (xpaned)))
    {
      GdkPoint old_position;
      GdkPoint new_position;
      gint increment;

      enum
      {
        SINGLE_STEP_SIZE = 1,
        PAGE_STEP_SIZE = 75
      };

      new_position.x = old_position.x = gtk_xpaned_get_position_x (xpaned);
      new_position.y = old_position.y = gtk_xpaned_get_position_y (xpaned);
      increment = 0;

      switch (scroll)
        {
        case GTK_SCROLL_STEP_LEFT:
        case GTK_SCROLL_STEP_UP:
        case GTK_SCROLL_STEP_BACKWARD:
          increment = -SINGLE_STEP_SIZE;
          break;

        case GTK_SCROLL_STEP_RIGHT:
        case GTK_SCROLL_STEP_DOWN:
        case GTK_SCROLL_STEP_FORWARD:
          increment = SINGLE_STEP_SIZE;
          break;

        case GTK_SCROLL_PAGE_LEFT:
        case GTK_SCROLL_PAGE_UP:
        case GTK_SCROLL_PAGE_BACKWARD:
          increment = -PAGE_STEP_SIZE;
          break;

        case GTK_SCROLL_PAGE_RIGHT:
        case GTK_SCROLL_PAGE_DOWN:
        case GTK_SCROLL_PAGE_FORWARD:
          increment = PAGE_STEP_SIZE;
          break;

        case GTK_SCROLL_START:
          new_position.x = xpaned->min_position.x;
          new_position.y = xpaned->min_position.y;
          break;

        case GTK_SCROLL_END:
          new_position.x = xpaned->max_position.x;
          new_position.y = xpaned->max_position.y;
          break;

        default:
          break;
        }

      if (increment)
        {
          if (is_rtl (xpaned))
            increment = -increment;

          new_position.x = old_position.x + increment;
          new_position.y = old_position.y + increment;
        }

      new_position.x = CLAMP (new_position.x,
                              xpaned->min_position.x, xpaned->max_position.x);

      new_position.y = CLAMP (new_position.y,
                              xpaned->min_position.y, xpaned->max_position.y);

      if (old_position.x != new_position.x)
        gtk_xpaned_set_position_x (xpaned, new_position.x);

      if (old_position.y != new_position.y)
        gtk_xpaned_set_position_y (xpaned, new_position.y);

      return TRUE;
    }

  return FALSE;
}

static void
gtk_xpaned_restore_focus (GtkXPaned * xpaned)
{
  if (gtk_widget_is_focus (GTK_WIDGET (xpaned)))
    {
      if (xpaned->priv->saved_focus &&
          gtk_widget_get_sensitive (xpaned->priv->saved_focus))
        {
          gtk_widget_grab_focus (xpaned->priv->saved_focus);
        }
      else
        {
          /* the saved focus is somehow not available for focusing,
           * try
           *   1) tabbing into the paned widget
           * if that didn't work,
           *   2) unset focus for the window if there is one
           */

          if (!gtk_widget_child_focus
              (GTK_WIDGET (xpaned), GTK_DIR_TAB_FORWARD))
            {
              GtkWidget *toplevel =
                gtk_widget_get_toplevel (GTK_WIDGET (xpaned));

              if (GTK_IS_WINDOW (toplevel))
                gtk_window_set_focus (GTK_WINDOW (toplevel), NULL);
            }
        }

      gtk_xpaned_set_saved_focus (xpaned, NULL);
      gtk_xpaned_set_first_xpaned (xpaned, NULL);
    }
}

static gboolean
gtk_xpaned_accept_position (GtkXPaned * xpaned)
{
  if (gtk_widget_is_focus (GTK_WIDGET (xpaned)))
    {
      xpaned->original_position.x = -1;
      xpaned->original_position.y = -1;
      gtk_xpaned_restore_focus (xpaned);

      return TRUE;
    }

  return FALSE;
}

static gboolean
gtk_xpaned_cancel_position (GtkXPaned * xpaned)
{
  if (gtk_widget_is_focus (GTK_WIDGET (xpaned)))
    {
      if (xpaned->original_position.x != -1)
        {
          gtk_xpaned_set_position_x (xpaned, xpaned->original_position.x);
          xpaned->original_position.x = -1;
        }

      if (xpaned->original_position.y != -1)
        {
          gtk_xpaned_set_position_y (xpaned, xpaned->original_position.y);
          xpaned->original_position.y = -1;
        }

      gtk_xpaned_restore_focus (xpaned);
      return TRUE;
    }

  return FALSE;
}

static gboolean
gtk_xpaned_cycle_handle_focus (GtkXPaned * xpaned, gboolean reversed)
{
  GtkXPaned *next;
  GtkXPaned *prev;

  if (gtk_widget_is_focus (GTK_WIDGET (xpaned)))
    {
      GtkXPaned *focus = NULL;

      if (!xpaned->priv->first_xpaned)
        {
          /* The first_pane has disappeared. As an ad-hoc solution,
           * we make the currently focused paned the first_paned. To the
           * user this will seem like the paned cycling has been reset.
           */
          gtk_xpaned_set_first_xpaned (xpaned, xpaned);
        }

      gtk_xpaned_find_neighbours (xpaned, &next, &prev);

      if (reversed && prev &&
          prev != xpaned && xpaned != xpaned->priv->first_xpaned)
        {
          focus = prev;
        }
      else if (!reversed &&
               next && next != xpaned && next != xpaned->priv->first_xpaned)
        {
          focus = next;
        }
      else
        {
          gtk_xpaned_accept_position (xpaned);
          return TRUE;
        }

      g_assert (focus);

      gtk_xpaned_set_saved_focus (focus, xpaned->priv->saved_focus);
      gtk_xpaned_set_first_xpaned (focus, xpaned->priv->first_xpaned);

      gtk_xpaned_set_saved_focus (xpaned, NULL);
      gtk_xpaned_set_first_xpaned (xpaned, NULL);

      gtk_widget_grab_focus (GTK_WIDGET (focus));

      if (!gtk_widget_is_focus (GTK_WIDGET (xpaned)))
        {
          xpaned->original_position.x = -1;
          xpaned->original_position.y = -1;
          focus->original_position.x = gtk_xpaned_get_position_x (focus);
          focus->original_position.y = gtk_xpaned_get_position_y (focus);
        }
    }
  else
    {
      GtkContainer *container = GTK_CONTAINER (xpaned);
      GtkXPaned *focus;
      GtkXPaned *first;
      GtkXPaned *prev;
      GtkXPaned *next;
      GtkWidget *toplevel;

      gtk_xpaned_find_neighbours (xpaned, &next, &prev);

      if (gtk_container_get_focus_child (container) == xpaned->top_left_child)
        {
          if (reversed)
            {
              focus = prev;
              first = xpaned;
            }
          else
            {
              focus = xpaned;
              first = xpaned;
            }
        }
      else if (gtk_container_get_focus_child (container) == xpaned->top_right_child)
        {
          if (reversed)
            {
              focus = xpaned;
              first = next;
            }
          else
            {
              focus = next;
              first = next;
            }
        }
      else
        {
          /* Focus is not inside this xpaned, and we don't have focus.
           * Presumably this happened because the application wants us
           * to start keyboard navigating.
           */
          focus = xpaned;

          if (reversed)
            first = xpaned;
          else
            first = next;
        }

      toplevel = gtk_widget_get_toplevel (GTK_WIDGET (xpaned));

      if (GTK_IS_WINDOW (toplevel))
        gtk_xpaned_set_saved_focus (focus,
                                    gtk_window_get_focus (GTK_WINDOW (toplevel)));
      gtk_xpaned_set_first_xpaned (focus, first);
      focus->original_position.x = gtk_xpaned_get_position_x (focus);
      focus->original_position.y = gtk_xpaned_get_position_y (focus);

      gtk_widget_grab_focus (GTK_WIDGET (focus));
    }

  return TRUE;
}

static gboolean
gtk_xpaned_toggle_handle_focus (GtkXPaned * xpaned)
{
  /* This function/signal has the wrong name. It is called when you
   * press Tab or Shift-Tab and what we do is act as if
   * the user pressed Return and then Tab or Shift-Tab
   */
  if (gtk_widget_is_focus (GTK_WIDGET (xpaned)))
    gtk_xpaned_accept_position (xpaned);

  return FALSE;
}

/*#define __GTK_XPANED_C__*/
/*#include "gtkaliasdef.c"*/
