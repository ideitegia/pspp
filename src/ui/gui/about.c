#include <gtk/gtk.h>
#include <glade/glade.h>

#include <libpspp/version.h>
#include "about.h"
#include "helper.h"

void
about_new (GtkMenuItem *m, GtkWindow *parent)
{
  GladeXML *xml = glade_xml_new (PKGDATADIR "/psppire.glade", NULL, NULL);

  GtkWidget *about =  get_widget_assert (xml, "aboutdialog1");

  GdkPixbuf *pb =
    gdk_pixbuf_new_from_file_at_size (PKGDATADIR "/pspplogo.png", 64, 64, 0);

  gtk_about_dialog_set_logo (GTK_ABOUT_DIALOG (about), pb);


  gtk_window_set_icon_from_file (GTK_WINDOW (about),
				 PKGDATADIR "/psppicon.png", 0);

  gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (about),
				"http://www.gnu.org/software/pspp");

  gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (about),
				bare_version);

  gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (about),
				(const gchar **) authors);

  gtk_window_set_transient_for (GTK_WINDOW (about), parent);

  gtk_window_set_modal (GTK_WINDOW (about), TRUE);

  gtk_window_set_keep_above (GTK_WINDOW (about), TRUE);


  gtk_dialog_run (about);

  gtk_widget_hide (about);
}

