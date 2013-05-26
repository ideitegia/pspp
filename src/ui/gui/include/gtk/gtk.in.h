/* Wrapper for <gtk/gtk.h>.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef PSPP_GTK_GTK_H
#define PSPP_GTK_GTK_H

#if __GNUC__ >= 3
@PRAGMA_SYSTEM_HEADER@
#endif
@PRAGMA_COLUMNS@

#@INCLUDE_NEXT@ @NEXT_GTK_GTK_H@

#if !GTK_CHECK_VERSION(2,20,0)
/**
 * gtk_widget_get_realized:
 * @widget: a #GtkWidget
 *
 * Determines whether @widget is realized.
 *
 * Return value: %TRUE if @widget is realized, %FALSE otherwise
 *
 * Since: 2.20
 **/
static inline gboolean
gtk_widget_get_realized (GtkWidget *widget)
{
  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);

  return (GTK_WIDGET_FLAGS (widget) & GTK_REALIZED) != 0;
}

/**
 * gtk_widget_set_realized:
 * @widget: a #GtkWidget
 * @realized: %TRUE to mark the widget as realized
 *
 * Marks the widget as being realized.
 *
 * This function should only ever be called in a derived widget's
 * "realize" or "unrealize" implementation.
 *
 * Since: 2.20
 */
static inline void
gtk_widget_set_realized (GtkWidget *widget,
                         gboolean   realized)
{
  g_return_if_fail (GTK_IS_WIDGET (widget));

  if (realized)
    GTK_OBJECT_FLAGS (widget) |= GTK_REALIZED;
  else
    GTK_OBJECT_FLAGS (widget) &= ~(GTK_REALIZED);
}

#endif  /* gtk < 2.20 */

#if !GTK_CHECK_VERSION(2,20,0)
/**
 * gtk_widget_get_mapped:
 * @widget: a #GtkWidget
 *
 * Whether the widget is mapped.
 *
 * Return value: %TRUE if the widget is mapped, %FALSE otherwise.
 *
 * Since: 2.20
 */
static inline gboolean
gtk_widget_get_mapped (GtkWidget *widget)
{
  g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

  return (GTK_WIDGET_FLAGS (widget) & GTK_MAPPED) != 0;
}

/**
 * gtk_widget_set_mapped:
 * @widget: a #GtkWidget
 * @mapped: %TRUE to mark the widget as mapped
 *
 * Marks the widget as being realized.
 *
 * This function should only ever be called in a derived widget's
 * "map" or "unmap" implementation.
 *
 * Since: 2.20
 */
static inline void
gtk_widget_set_mapped (GtkWidget *widget,
                       gboolean   mapped)
{
  g_return_if_fail (GTK_IS_WIDGET (widget));

  if (mapped)
    GTK_OBJECT_FLAGS (widget) |= GTK_MAPPED;
  else
    GTK_OBJECT_FLAGS (widget) &= ~(GTK_MAPPED);
}

#endif  /* gtk < 2.20 */

#if !GTK_CHECK_VERSION(2,18,0)
/**
 * gtk_widget_get_visible:
 * @widget: a #GtkWidget
 *
 * Determines whether the widget is visible. Note that this doesn't
 * take into account whether the widget's parent is also visible
 * or the widget is obscured in any way.
 *
 * See gtk_widget_set_visible().
 *
 * Return value: %TRUE if the widget is visible
 *
 * Since: 2.18
 **/
static inline gboolean
gtk_widget_get_visible (GtkWidget *widget)
{
  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);

  return (GTK_WIDGET_FLAGS (widget) & GTK_VISIBLE) != 0;
}
#endif  /* gtk < 2.18 */

#if !GTK_CHECK_VERSION(2,18,0)
/**
 * gtk_widget_is_drawable:
 * @widget: a #GtkWidget
 *
 * Determines whether @widget can be drawn to. A widget can be drawn
 * to if it is mapped and visible.
 *
 * Return value: %TRUE if @widget is drawable, %FALSE otherwise
 *
 * Since: 2.18
 **/
static inline gboolean
gtk_widget_is_drawable (GtkWidget *widget)
{
  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);

  return ((GTK_WIDGET_FLAGS (wid) & GTK_VISIBLE) != 0 &&
          (GTK_WIDGET_FLAGS (wid) & GTK_MAPPED) != 0);
}
#endif  /* gtk < 2.18 */

#if !GTK_CHECK_VERSION(2,18,0)
/**
 * gtk_widget_has_focus:
 * @widget: a #GtkWidget
 *
 * Determines if the widget has the global input focus. See
 * gtk_widget_is_focus() for the difference between having the global
 * input focus, and only having the focus within a toplevel.
 *
 * Return value: %TRUE if the widget has the global input focus.
 *
 * Since: 2.18
 **/
static inline gboolean
gtk_widget_has_focus (GtkWidget *widget)
{
  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);

  return GTK_WIDGET_HAS_FOCUS (widget);
}
#endif  /* gtk < 2.18 */

#if !GTK_CHECK_VERSION(2,18,0)
/**
 * gtk_widget_set_can_focus:
 * @widget: a #GtkWidget
 * @can_focus: whether or not @widget can own the input focus.
 *
 * Specifies whether @widget can own the input focus. See
 * gtk_widget_grab_focus() for actually setting the input focus on a
 * widget.
 *
 * Since: 2.18
 **/
static inline void
gtk_widget_set_can_focus (GtkWidget *widget,
                          gboolean   can_focus)
{
  g_return_if_fail (GTK_IS_WIDGET (widget));

  if (can_focus != GTK_WIDGET_CAN_FOCUS (widget))
    {
      if (can_focus)
        GTK_WIDGET_SET_FLAGS (widget, GTK_CAN_FOCUS);
      else
        GTK_WIDGET_UNSET_FLAGS (widget, GTK_CAN_FOCUS);

      gtk_widget_queue_resize (widget);
      g_object_notify (G_OBJECT (widget), "can-focus");
    }
}
#endif  /* gtk < 2.18 */

#if !GTK_CHECK_VERSION(2,22,0)
/**
 * gtk_button_get_event_window:
 * @button: a #GtkButton
 *
 * Returns the button's event window if it is realized, %NULL otherwise.
 * This function should be rarely needed.
 *
 * Return value: (transfer none): @button's event window.
 *
 * Since: 2.22
 */
static inline GdkWindow*
gtk_button_get_event_window (GtkButton *button)
{
  g_return_val_if_fail (GTK_IS_BUTTON (button), NULL);

  return button->event_window;
}
#endif  /* gtk < 2.22 */


#ifndef G_CONST_RETURN
#define G_CONST_RETURN const
#endif


/* Like GSEAL but only used in PSPP */
#define PSEAL(X) X

#endif /* PSPP_GTK_GTK_H */
