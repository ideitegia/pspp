#include <glade/glade-build.h>
#include "psppire-dialog.h"
#include "psppire-buttonbox.h"

GLADE_MODULE_CHECK_INIT

/* Glade registration functions for PSPPIRE custom widgets */

static GtkWidget *
dialog_find_internal_child (GladeXML *xml,
			    GtkWidget *parent,
			    const gchar *childname)
{
  if (!strcmp(childname, "hbox"))
    return PSPPIRE_DIALOG (parent)->box;

  return NULL;
}

void
glade_module_register_widgets (void)
{
  glade_register_widget (PSPPIRE_DIALOG_TYPE, NULL,
			 glade_standard_build_children,
			 dialog_find_internal_child);


  glade_register_widget (PSPPIRE_BUTTONBOX_TYPE, NULL,
			 glade_standard_build_children,
			 NULL);
}



