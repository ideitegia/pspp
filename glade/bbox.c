#include <config.h>
#include <gladeui/glade.h>
#include <gtk/gtk.h>

#include "psppire-buttonbox.h"

void
glade_psppire_button_box_post_create (GladeWidgetAdaptor *adaptor,
				      GObject            *object,
				      GladeCreateReason   reason)
{
  GladeWidget  *box_widget;

  PsppireButtonBox    *bbox = PSPPIRE_BUTTONBOX (object);

  g_return_if_fail (PSPPIRE_IS_BUTTONBOX (bbox));

  box_widget = glade_widget_get_from_gobject (GTK_WIDGET (bbox));
  if (!box_widget)
    return;


  if (reason == GLADE_CREATE_USER)
    {
      /* HIG complient border-width defaults on dialogs */
      glade_widget_property_set (box_widget, "border-width", 5);
    }

}


GtkWidget *
glade_psppire_button_box_get_internal_child (GladeWidgetAdaptor  *adaptor,
					     PsppireButtonBox    *bbox,
					     const gchar         *name)
{
#if DEBUGGING
  g_print ("%s\n", __FUNCTION__);
#endif

  return GTK_WIDGET (bbox);
}




void
glade_psppire_button_box_set_property (GladeWidgetAdaptor *adaptor,
				   GObject            *object,
				   const gchar        *id,
				   const GValue       *value)
{
#if DEBUGGING
  g_print ("%s(%p) Type=\"%s\" Id=\"%s\"\n", __FUNCTION__, object,
	   G_OBJECT_TYPE_NAME( object ),
	   id);
#endif

  GWA_GET_CLASS (GTK_TYPE_WINDOW)->set_property (adaptor, object,
						 id, value);
}




GList *
glade_psppire_button_box_get_children (GladeWidgetAdaptor  *adaptor,
				       PsppireButtonBox  *bbox)
{
  GList *list = NULL;

  g_return_val_if_fail (PSPPIRE_IS_BUTTONBOX (bbox), NULL);

  list = glade_util_container_get_all_children (GTK_CONTAINER (bbox));

  return list;
}
