#include <config.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "psppire-selector.h"

#include <gladeui/glade.h>


void
glade_psppire_selector_post_create (GladeWidgetAdaptor *adaptor,
				    GObject            *object,
				    GladeCreateReason   reason)
{
  GladeWidget *widget ;

  PsppireSelector *selector = PSPPIRE_SELECTOR (object);

  g_return_if_fail (PSPPIRE_IS_SELECTOR (selector));

  widget = glade_widget_get_from_gobject (GTK_WIDGET (selector));
  if (!widget)
    return;

  if (reason == GLADE_CREATE_USER)
    {
      /* HIG complient border-width defaults on selectors */
      glade_widget_property_set (widget, "border-width", 5);
    }
}


GtkWidget *
glade_psppire_selector_get_internal_child (GladeWidgetAdaptor  *adaptor,
					 PsppireSelector       *selector,
					 const gchar         *name)
{
#if DEBUGGING
  g_print ("%s\n", __FUNCTION__);
#endif
  return GTK_WIDGET (selector);
}



void
glade_psppire_selector_set_property (GladeWidgetAdaptor *adaptor,
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
glade_psppire_selector_get_children (GladeWidgetAdaptor  *adaptor,
				   PsppireSelector  *selector)
{
  GList *list = NULL;

  g_return_val_if_fail (PSPPIRE_IS_SELECTOR (selector), NULL);

  list = glade_util_container_get_all_children (GTK_CONTAINER (selector));

  return list;
}
