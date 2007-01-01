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


#include "data-editor.h"
#include <libpspp/version.h>
#include <libpspp/copyleft.h>
#include <data/file-handle-def.h>
#include <data/format.h>
#include <data/storage-stream.h>
#include <data/settings.h>
#include <data/file-name.h>
#include <data/procedure.h>
#include <libpspp/getl.h>
#include <language/lexer/lexer.h>
#include <ui/flexifile.h>

#include <getopt.h>
#include <gtk/gtk.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include "psppire-dict.h"
#include "psppire-var-store.h"
#include "psppire-data-store.h"
#include "helper.h"
#include "data-sheet.h"
#include "var-sheet.h"
#include "message-dialog.h"
#include "flexifile-factory.h"

PsppireDataStore *the_data_store = 0;


static bool parse_command_line (int *argc, char ***argv,
				gchar **filename, GError **err);


PsppireVarStore *the_var_store = 0;

void create_icon_factory (void);

struct source_stream *the_source_stream ;
struct dataset * the_dataset = NULL;

static void
replace_dictionary (struct dictionary *d)
{
  psppire_dict_replace_dictionary (the_data_store->dict,
				   d);
}


static void
replace_flexifile (struct case_source *s)
{
  if ( NULL == s )
    psppire_case_file_replace_flexifile (the_data_store->case_file,
					 (struct flexifile *) flexifile_create (0));
  else
    psppire_case_file_replace_flexifile (the_data_store->case_file,
					 (struct flexifile *)
					 storage_source_get_casefile (s));
}


int
main (int argc, char *argv[])
{
  struct casefile_factory *factory;
  PsppireDict *dictionary = 0;


  gchar *filename=0;
  GError *err = 0;
  gchar *vers;

  gtk_init (&argc, &argv);
  if ( (vers = gtk_check_version (GTK_MAJOR_VERSION,
				 GTK_MINOR_VERSION,
				 GTK_MICRO_VERSION)) )
    {
      g_critical (vers);
    }


  /* gtk_init messes with the locale.
     So unset the bits we want to control ourselves */
  setlocale (LC_NUMERIC, "C");

  bindtextdomain (PACKAGE, locale_dir);

  textdomain (PACKAGE);

  if ( ! parse_command_line (&argc, &argv, &filename, &err) )
    {
      g_clear_error (&err);
      return 0;
    }

  glade_init ();

  fmt_init ();
  settings_init ();
  fh_init ();
  factory = flexifile_factory_create ();
  the_source_stream = create_source_stream (
			  fn_getenv_default ("STAT_INCLUDE_PATH", include_path)
			  );

  the_dataset = create_dataset (factory,
				replace_flexifile,
				replace_dictionary);

  message_dialog_init (the_source_stream);

  dictionary = psppire_dict_new_from_dict (
					   dataset_dict (the_dataset)
					   );

  bind_textdomain_codeset (PACKAGE, "UTF-8");

  /* Create the model for the var_sheet */
  the_var_store = psppire_var_store_new (dictionary);


  the_data_store = psppire_data_store_new (dictionary);

  proc_set_source (the_dataset,
		   storage_source_create (the_data_store->case_file->flexifile)
		   );

  create_icon_factory ();

  new_data_window (NULL, NULL);

  /* start the event loop */
  gtk_main ();

  destroy_source_stream (the_source_stream);
  message_dialog_done ();

  settings_done ();

  return 0;
}


/* Parses the command line specified by ARGC and ARGV as received by
   main ().  Returns true if normal execution should proceed,
   false if the command-line indicates that PSPP should exit. */
static bool
parse_command_line (int *argc, char ***argv, gchar **filename, GError **err)
{
  static struct option long_options[] =
    {
      {"help", no_argument, NULL, 'h'},
      {"version", no_argument, NULL, 'V'},
      {0, 0, 0, 0},
    };

  int c;

  for (;;)
    {
      c = getopt_long (*argc, *argv, "hV", long_options, NULL);
      if (c == -1)
	break;

      switch (c)
	{
	case 'h':
	  g_print ("Usage: psppire {|--help|--version}\n");
          return false;
	case 'V':
	  g_print (version);
	  g_print ("\n");
	  g_print (legal);
	  return false;
	default:
	  return false;
	}
    }

  if ( optind < *argc)
    {
      *filename = (*argv)[optind];
    }

  return true;
}



void
create_icon_factory (void)
{
  GtkIconFactory *factory = gtk_icon_factory_new ();

  GtkIconSet *icon_set;

  GdkPixbuf *pixbuf;

  pixbuf = gdk_pixbuf_new_from_file (PKGDATADIR "/value-labels.png", 0);
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-value-labels", icon_set);

  pixbuf = gdk_pixbuf_new_from_file (PKGDATADIR "/weight-cases.png", 0);
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-weight-cases", icon_set);

  pixbuf = gdk_pixbuf_new_from_file (PKGDATADIR "/goto-variable.png", 0);
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-goto-variable", icon_set);

  pixbuf = gdk_pixbuf_new_from_file (PKGDATADIR "/insert-variable.png", 0);
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-insert-variable", icon_set);

  pixbuf = gdk_pixbuf_new_from_file (PKGDATADIR "/insert-case.png", 0);
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-insert-case", icon_set);

  pixbuf = gdk_pixbuf_new_from_file (PKGDATADIR "/split-file.png", 0);
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-split-file", icon_set);

  pixbuf = gdk_pixbuf_new_from_file (PKGDATADIR "/select-cases.png", 0);
  icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
  g_object_unref (pixbuf);
  gtk_icon_factory_add ( factory, "pspp-select-cases", icon_set);

  gtk_icon_factory_add_default (factory);
}



