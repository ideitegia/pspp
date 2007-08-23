/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006  Free Software Foundation

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

#include <assert.h>
#include <libintl.h>

#include "relocatable.h"

#include "data-editor.h"
#include "psppire.h"

#include <unistd.h>
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
#include <output/output.h>

#include <gtk/gtk.h>
#include <glade/glade.h>
#include "psppire-dict.h"
#include "psppire-var-store.h"
#include "psppire-data-store.h"
#include "helper.h"
#include "data-sheet.h"
#include "var-sheet.h"
#include "message-dialog.h"

#include "output-viewer.h"

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
  fn_init ();
  outp_init ();
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

  outp_configure_driver_line (
    ss_cstr ("gui:ascii:screen:squeeze=on headers=off top-margin=0 "
             "bottom-margin=0 paginate=off length=50 "
	     "width=" OUTPUT_LINE_WIDTH_str " emphasis=none "
             "output-file=\"" OUTPUT_FILE_NAME "\" append=yes"));

  unlink (OUTPUT_FILE_NAME);

  new_data_window (NULL, NULL);
}


void
de_initialize (void)
{
  destroy_source_stream (the_source_stream);
  message_dialog_done ();
  settings_done ();
  outp_done ();
}


struct icon_info
{
  const char *file_name;
  const gchar *id;
};


static const struct icon_info icons[] =
  {
    {PKGDATADIR "/value-labels.png",    "pspp-value-labels"},
    {PKGDATADIR "/weight-cases.png",    "pspp-weight-cases"},
    {PKGDATADIR "/goto-variable.png",   "pspp-goto-variable"},
    {PKGDATADIR "/insert-variable.png", "pspp-insert-variable"},
    {PKGDATADIR "/insert-case.png",     "pspp-insert-case"},
    {PKGDATADIR "/split-file.png",      "pspp-split-file"},
    {PKGDATADIR "/select-cases.png",    "pspp-select-cases"},
    {PKGDATADIR "/recent-dialogs.png",  "pspp-recent-dialogs"},
    {PKGDATADIR "/nominal.png",         "var-nominal"},
    {PKGDATADIR "/ordinal.png",         "var-ordinal"},
    {PKGDATADIR "/scale.png",           "var-scale"},
    {PKGDATADIR "/string.png",          "var-string"},
    {PKGDATADIR "/date-scale.png",      "var-date-scale"}
  };

static void
create_icon_factory (void)
{
  gint i;
  GtkIconFactory *factory = gtk_icon_factory_new ();

  for (i = 0 ; i < sizeof (icons) / sizeof(icons[0]); ++i)
    {
      GError *err = NULL;
      GdkPixbuf *pixbuf =
	gdk_pixbuf_new_from_file (relocate (icons[i].file_name), &err);

      if ( pixbuf )
	{
	  GtkIconSet *icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
	  g_object_unref (pixbuf);
	  gtk_icon_factory_add ( factory, icons[i].id, icon_set);
	}
      else
	{
	  g_warning ("Cannot create icon: %s", err->message);
	  g_clear_error (&err);
	}
    }

  gtk_icon_factory_add_default (factory);
}

