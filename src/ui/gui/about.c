/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2007  Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */


#include <config.h>

#include <gtk/gtk.h>

#include <libpspp/copyleft.h>
#include <libpspp/version.h>
#include "about.h"
#include "helper.h"


static const gchar *artists[] = { "Patrick Brunier", "Dondi Bogusky", NULL};

void
about_new (GtkMenuItem *m, GtkWindow *parent)
{
  GtkBuilder *xml = builder_new ("psppire.ui");

  GtkWidget *about =  get_widget_assert (xml, "aboutdialog1");

  GdkPixbuf *pb =
    gdk_pixbuf_new_from_file_at_size (relocate (PKGDATADIR "/pspplogo.png"),
				      64, 64, 0);

  gtk_about_dialog_set_logo (GTK_ABOUT_DIALOG (about), pb);


  gtk_window_set_icon_name (GTK_WINDOW (about), "psppicon");

  gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (about),
				"http://www.gnu.org/software/pspp");

  gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (about),
				bare_version);

  gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (about),
				(const gchar **) authors);

  gtk_about_dialog_set_artists (GTK_ABOUT_DIALOG (about),
				artists);

  gtk_about_dialog_set_license (GTK_ABOUT_DIALOG (about),
				copyleft);


  gtk_window_set_transient_for (GTK_WINDOW (about), parent);

  gtk_window_set_modal (GTK_WINDOW (about), TRUE);

  gtk_window_set_keep_above (GTK_WINDOW (about), TRUE);

  gtk_dialog_run (GTK_DIALOG (about));

  gtk_widget_hide (about);
}

