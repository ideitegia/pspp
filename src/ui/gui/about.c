/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2006, 2007  Free Software Foundation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA. */


#include <config.h>

#include <gtk/gtk.h>
#include <glade/glade.h>

#include <libpspp/version.h>
#include "about.h"
#include "helper.h"

void
about_new (GtkMenuItem *m, GtkWindow *parent)
{
  GladeXML *xml = XML_NEW ("psppire.glade");

  GtkWidget *about =  get_widget_assert (xml, "aboutdialog1");

  GdkPixbuf *pb =
    gdk_pixbuf_new_from_file_at_size (relocate (PKGDATADIR "/pspplogo.png"),
				      64, 64, 0);

  gtk_about_dialog_set_logo (GTK_ABOUT_DIALOG (about), pb);


  gtk_window_set_icon_from_file (GTK_WINDOW (about),
				 relocate (PKGDATADIR "/psppicon.png"), 0);

  gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (about),
				"http://www.gnu.org/software/pspp");

  gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (about),
				bare_version);

  gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (about),
				(const gchar **) authors);

  gtk_window_set_transient_for (GTK_WINDOW (about), parent);

  gtk_window_set_modal (GTK_WINDOW (about), TRUE);

  gtk_window_set_keep_above (GTK_WINDOW (about), TRUE);

  gtk_dialog_run (GTK_DIALOG (about));

  gtk_widget_hide (about);
}

