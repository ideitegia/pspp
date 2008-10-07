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
#include <gtk/gtk.h>
#include "psppire.h"
#include "progname.h"
#include <stdlib.h>
#include <getopt.h>
#include <gl/relocatable.h>

#include <libpspp/version.h>
#include <libpspp/copyleft.h>

static gboolean parse_command_line (int *argc, char ***argv, gchar **filename,
				    gboolean *show_splash, GError **err);



static GtkWidget *
create_splash_window (void)
{
  GtkWidget *splash ;
  GtkWidget *image;

  gtk_window_set_auto_startup_notification (FALSE);

  splash = gtk_window_new (GTK_WINDOW_POPUP);

  gtk_window_set_position (GTK_WINDOW (splash),
			   GTK_WIN_POS_CENTER_ALWAYS);

  gtk_window_set_type_hint (GTK_WINDOW (splash),
			    GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);

  image = gtk_image_new_from_file (relocate (PKGDATADIR "/splash.png"));

  gtk_container_add (GTK_CONTAINER (splash), image);

  gtk_widget_show (image);

  return splash;
}

static gboolean
hide_splash_window (gpointer data)
{
  GtkWidget *splash = data;
  gtk_widget_destroy (splash);
  gtk_window_set_auto_startup_notification (TRUE);
  return FALSE;
}


static gboolean
quit_one_loop (gpointer data)
{
  gtk_main_quit ();
  return FALSE;
}


static gboolean
run_inner_loop (gpointer data)
{
  initialize ();

  g_timeout_add (500, hide_splash_window, data);

  gtk_main ();

  de_initialize ();

  return FALSE;
}



int
main (int argc, char *argv[])
{
  GtkWidget *splash_window;
  gchar *filename = 0;
  gboolean show_splash = TRUE;
  GError *err = 0;
  const gchar *vers;

  set_program_name (argv[0]);

  if ( ! gtk_parse_args (&argc, &argv) )
    {
      perror ("Error parsing arguments");
      exit (1);
    }

  if ( (vers = gtk_check_version (GTK_MAJOR_VERSION,
				 GTK_MINOR_VERSION,
				 GTK_MICRO_VERSION)) )
    {
      g_critical (vers);
    }

  /* Deal with options like --version, --help etc */
  if ( ! parse_command_line (&argc, &argv, &filename, &show_splash, &err) )
    {
      g_clear_error (&err);
      return 0;
    }

  gdk_init (&argc, &argv);

  splash_window = create_splash_window ();
  if ( show_splash )
    gtk_widget_show (splash_window);

  g_idle_add (quit_one_loop, 0);

  gtk_quit_add (0, run_inner_loop, splash_window);
  gtk_main ();


  return 0;
}


/* Parses the command line specified by ARGC and ARGV as received by
   main ().  Returns true if normal execution should proceed,
   false if the command-line indicates that PSPP should exit. */
static gboolean
parse_command_line (int *argc, char ***argv, gchar **filename,
		    gboolean *show_splash, GError **err)
{

  static struct option long_options[] =
    {
      {"help", no_argument, NULL, 'h'},
      {"version", no_argument, NULL, 'V'},
      {"no-splash", no_argument, NULL, 'q'},
      {0, 0, 0, 0},
    };

  int c;

  for (;;)
    {
      c = getopt_long (*argc, *argv, "hVq", long_options, NULL);
      if (c == -1)
	break;

      switch (c)
	{
	case 'h':
	  g_print ("Usage: psppire {|--help|--version|--no-splash}\n");
          return FALSE;
	case 'V':
	  g_print (version);
	  g_print ("\n");
	  g_print (legal);
	  return FALSE;
	case 'q':
	  *show_splash = FALSE;
	  break;
	default:
	  return FALSE;
	}
    }

  if ( optind < *argc)
    {
      *filename = (*argv)[optind];
    }

  return TRUE;
}
