/*
   PSPPIRE --- A Graphical User Interface for PSPP
   Copyright (C) 2004, 2005, 2006  Free Software Foundation

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

#include <assert.h>
#include <libintl.h>

#include "relocatable.h"

#include "data-editor.h"

#include "psppire.h"


#include <data/casereader.h>
#include <data/datasheet.h>
#include <data/file-handle-def.h>
#include <data/format.h>
#include <data/settings.h>
#include <data/file-name.h>
#include <data/procedure.h>
#include <libpspp/getl.h>
#include <language/lexer/lexer.h>
#include <libpspp/version.h>

#include <gtk/gtk.h>
#include <glade/glade.h>
#include "psppire-dict.h"
#include "psppire-var-store.h"
#include "psppire-data-store.h"
#include "helper.h"
#include "data-sheet.h"
#include "var-sheet.h"
#include "message-dialog.h"

PsppireDataStore *the_data_store = 0;
PsppireVarStore *the_var_store = 0;

static void create_icon_factory (void);

struct source_stream *the_source_stream ;
struct dataset * the_dataset = NULL;


static void
replace_casereader (struct casereader *s)
{
  PsppireCaseFile *pcf = psppire_case_file_new (s);

  psppire_data_store_set_case_file (the_data_store, pcf);
}

void
initialize (void)
{
  PsppireDict *dictionary = 0;

  /* gtk_init messes with the locale.
     So unset the bits we want to control ourselves */
  setlocale (LC_NUMERIC, "C");

  bindtextdomain (PACKAGE, locale_dir);

  textdomain (PACKAGE);

  glade_init ();

  fmt_init ();
  settings_init ();
  fh_init ();
  the_source_stream =
    create_source_stream (
			  fn_getenv_default ("STAT_INCLUDE_PATH", include_path)
			  );

  the_dataset = create_dataset (NULL, NULL);


  message_dialog_init (the_source_stream);

  dictionary = psppire_dict_new_from_dict (dataset_dict (the_dataset));


  bind_textdomain_codeset (PACKAGE, "UTF-8");


  /* Create the model for the var_sheet */
  the_var_store = psppire_var_store_new (dictionary);

  the_data_store = psppire_data_store_new (dictionary);
  replace_casereader (NULL);


  create_icon_factory ();

  new_data_window (NULL, NULL);
}


void
de_initialize (void)
{
  destroy_source_stream (the_source_stream);
  message_dialog_done ();
  settings_done ();
}


#define PIXBUF_NEW_FROM_FILE(FILE) \
  gdk_pixbuf_new_from_file (relocate (PKGDATADIR "/" FILE), 0)


static void
create_icon_factory (void)
{
  GtkIconFactory *factory = gtk_icon_factory_new ();

  GtkIconSet *icon_set;

  GdkPixbuf *pixbuf;

  pixbuf = PIXBUF_NEW_FROM_FILE ("value-labels.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-value-labels", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("weight-cases.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-weight-cases", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("goto-variable.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-goto-variable", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("insert-variable.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-insert-variable", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("insert-case.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-insert-case", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("split-file.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-split-file", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("select-cases.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-select-cases", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("recent-dialogs.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-recent-dialogs", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("nominal.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "var-nominal", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("ordinal.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "var-ordinal", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("scale.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "var-scale", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("string.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "var-string", icon_set);

  pixbuf = PIXBUF_NEW_FROM_FILE ("date-scale.png");
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "var-date-scale", icon_set);


  gtk_icon_factory_add_default (factory);
}

