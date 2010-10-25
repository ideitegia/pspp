#include <glib.h>
#include <gtk/gtk.h>

#include "psppire-dialog.h"
#include <string.h>
#include <assert.h>
#include <gladeui/glade.h>


void
glade_psppire_dialog_post_create (GladeWidgetAdaptor *adaptor,
				  GObject            *object,
				  GladeCreateReason   reason)
{
  GladeWidget *widget ;

  GladeWidget  *box_widget;

  PsppireDialog    *dialog = PSPPIRE_DIALOG (object);

  g_return_if_fail (PSPPIRE_IS_DIALOG (dialog));

  widget = glade_widget_get_from_gobject (GTK_WIDGET (dialog));
  if (!widget)
    return;


  if (reason == GLADE_CREATE_USER)
    {
      /* HIG compliant border-width defaults on dialogs */
      glade_widget_property_set (widget, "border-width", 5);
    }

  box_widget = glade_widget_adaptor_create_internal
    (widget, G_OBJECT(dialog->box),
     "hbox", "dialog", FALSE, reason);

  /* These properties are controlled by the GtkDialog style properties:
   * "content-area-border", "button-spacing" and "action-area-border",
   * so we must disable their use.
   */
  glade_widget_remove_property (box_widget, "border-width");

  /* Only set these on the original create. */
  if (reason == GLADE_CREATE_USER)
    {

      /* HIG compliant spacing defaults on dialogs */
      glade_widget_property_set (box_widget, "spacing", 2);

      glade_widget_property_set (box_widget, "size", 2);

    }
}


GtkWidget *
glade_psppire_dialog_get_internal_child (GladeWidgetAdaptor  *adaptor,
					 PsppireDialog       *dialog,
					 const gchar         *name)
{
#if DEBUGGING
  g_print ("%s\n", __FUNCTION__);
#endif

  g_assert (0 == strcmp (name, "hbox"));

  return dialog->box;
}



void
glade_psppire_dialog_set_property (GladeWidgetAdaptor *adaptor,
				   GObject            *object,
				   const gchar        *id,
				   const GValue       *value)
{
#if DEBUGGING
  g_print ("%s(%p) Type=\"%s\" Id=\"%s\"\n", __FUNCTION__, object,
	   G_OBJECT_TYPE_NAME( object ),
	   id);
#endif

  assert (  GWA_GET_CLASS (GTK_TYPE_WINDOW)->set_property );

  GWA_GET_CLASS (GTK_TYPE_WINDOW)->set_property (adaptor, object,
						 id, value);
}



GList *
glade_psppire_dialog_get_children (GladeWidgetAdaptor  *adaptor,
				   PsppireDialog  *dialog)
{
  GList *list = NULL;

  g_return_val_if_fail (PSPPIRE_IS_DIALOG (dialog), NULL);

  list = glade_util_container_get_all_children (GTK_CONTAINER (dialog));

  return list;
}
