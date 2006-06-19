/* 
   PSPPIRE --- A Graphical User Interface for PSPP
   Copyright (C) 2004, 2005, 2006  Free Software Foundation
   Written by John Darrington

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

#include <assert.h>
#include <libintl.h>

#include <libpspp/version.h>
#include <libpspp/copyleft.h>
#include <data/settings.h>

#include <getopt.h>
#include <gtk/gtk.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include "menu-actions.h"
#include "psppire-dict.h"
#include "psppire-var-store.h"
#include "psppire-data-store.h"
#include "helper.h"
#include "data-sheet.h"
#include "var-sheet.h"
#include "message-dialog.h"

GladeXML *xml;


PsppireDict *the_dictionary = 0;

PsppireDataStore *data_store = 0;


static bool parse_command_line (int *argc, char ***argv, 
				gchar **filename, GError **err);


#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void
give_help(void)
{
  static struct msg m = {
    MSG_GENERAL, 
    MSG_NOTE,
    {0, -1},
    0, 
  };

  if (! m.text) 
    m.text=g_strdup(_("Sorry. The help system hasn't yet been implemented."));

  popup_message(&m);
}

PsppireVarStore *var_store = 0;

int 
main(int argc, char *argv[]) 
{

  GtkWidget *data_editor ;
  GtkSheet *var_sheet ; 
  GtkSheet *data_sheet ;

  gchar *filename=0;
  GError *err = 0;

  gtk_init(&argc, &argv);

  /* gtk_init messes with the locale. 
     So unset the bits we want to control ourselves */
  setlocale (LC_NUMERIC, "C");

  bindtextdomain (PACKAGE, locale_dir);

  textdomain (PACKAGE);

  if ( ! parse_command_line(&argc, &argv, &filename, &err) ) 
    {
      g_clear_error(&err);
      return 1;
    }


  glade_init();


  settings_init();

  /* 
  set_pspp_locale("da_DK");
  */

  message_dialog_init();

  the_dictionary = psppire_dict_new();

  bind_textdomain_codeset(PACKAGE, "UTF-8");

  /* Create the model for the var_sheet */
  var_store = psppire_var_store_new(the_dictionary);

  data_store = psppire_data_store_new(the_dictionary);

  /* load the interface */
  xml = glade_xml_new(PKGDATADIR "/psppire.glade", NULL, NULL);

  if ( !xml ) return 1;

  data_editor = get_widget_assert(xml, "data_editor");
  gtk_window_set_icon_from_file(GTK_WINDOW(data_editor), 
				PKGDATADIR "/psppicon.png",0);

  /* connect the signals in the interface */
  glade_xml_signal_autoconnect(xml);

  var_sheet  = GTK_SHEET(get_widget_assert(xml, "variable_sheet"));
  data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));

  gtk_sheet_set_model(var_sheet, G_SHEET_MODEL(var_store));
  
  gtk_sheet_set_model(data_sheet, G_SHEET_MODEL(data_store));

  if (filename)
    gtk_init_add((GtkFunction)load_system_file, filename);
  else
    gtk_init_add((GtkFunction)clear_file, 0);

  var_data_selection_init();

  {
  GList *helps = glade_xml_get_widget_prefix(xml, "help_button_");

  GList *i;
  for ( i = g_list_first(helps); i ; i = g_list_next(i))
      g_signal_connect(GTK_WIDGET(i->data), "clicked", give_help, 0);
  }


  /* start the event loop */
  gtk_main();

  message_dialog_done();

  settings_done();

  return 0;
}


/* Parses the command line specified by ARGC and ARGV as received by
   main().  Returns true if normal execution should proceed,
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
	  g_printerr("Usage: psppire {|--help|--version}\n");
          return false;
	case 'V':
	  g_print(version);
	  g_print("\n");
	  g_print(legal);
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


