#include <glib.h>
#include <gtk/gtk.h>

#include "psppire-acr.h"

#include <gladeui/glade.h>


void
glade_psppire_acr_post_create (GladeWidgetAdaptor *adaptor,
				    GObject            *object,
				    GladeCreateReason   reason)
{
  GladeWidget *widget ;

  PsppireAcr *acr = PSPPIRE_ACR (object);

  g_return_if_fail (PSPPIRE_IS_ACR (acr));

  widget = glade_widget_get_from_gobject (GTK_WIDGET (acr));
  if (!widget)
    return;

  if (reason == GLADE_CREATE_USER)
    {
      /* HIG complient border-width defaults on acrs */
      glade_widget_property_set (widget, "border-width", 5);
    }
}


GtkWidget *
glade_psppire_acr_get_internal_child (GladeWidgetAdaptor  *adaptor,
					 PsppireAcr       *acr,
					 const gchar         *name)
{
#if DEBUGGING
  g_print ("%s\n", __FUNCTION__);
#endif
  return GTK_WIDGET (acr);
}



void
glade_psppire_acr_set_property (GladeWidgetAdaptor *adaptor,
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
glade_psppire_acr_get_children (GladeWidgetAdaptor  *adaptor,
				   PsppireAcr  *acr)
{
  GList *list = NULL;

  g_return_val_if_fail (PSPPIRE_IS_ACR (acr), NULL);

  list = glade_util_container_get_all_children (GTK_CONTAINER (acr));

  return list;
}
