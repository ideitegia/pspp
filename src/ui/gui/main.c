/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006, 2010  Free Software Foundation

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

#include "ui/gui/psppire.h"

#include <gtk/gtk.h>
#include <stdlib.h>

#include "libpspp/argv-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/getl.h"
#include "libpspp/version.h"
#include "libpspp/copyleft.h"
#include "ui/source-init-opts.h"

#include "gl/progname.h"
#include "gl/relocatable.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


/* Arguments to be interpreted before the X server gets initialised */

enum
  {
    OPT_NO_SPLASH,
    N_STARTUP_OPTIONS
  };

static const struct argv_option startup_options[N_STARTUP_OPTIONS] =
  {
    {"no-splash", 'q', no_argument, OPT_NO_SPLASH}
  };

static void
startup_option_callback (int id, void *show_splash_)
{
  gboolean *show_splash = show_splash_;

  switch (id)
    {
    case OPT_NO_SPLASH:
      *show_splash = FALSE;
      break;

    default:
      NOT_REACHED ();
    }
}

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

struct initialisation_parameters
{
  struct source_stream *ss;
  const char *data_file;
  GtkWidget *splash_window;
};


static gboolean
run_inner_loop (gpointer data)
{
  struct initialisation_parameters *ip = data;
  initialize (ip->ss, ip->data_file);

  g_timeout_add (500, hide_splash_window, ip->splash_window);

  gtk_main ();

  de_initialize ();

  return FALSE;
}


static GMemVTable vtable =
  {
    xmalloc,
    xrealloc,
    free,
    xcalloc,
    malloc,
    realloc
  };

int
main (int argc, char *argv[])
{
  struct initialisation_parameters init_p;
  gboolean show_splash = TRUE;
  struct argv_parser *parser;
  struct source_stream *ss;
  const gchar *vers;

  set_program_name (argv[0]);

  g_mem_set_vtable (&vtable);

  gtk_disable_setlocale ();


  if ( ! gtk_parse_args (&argc, &argv) )
    {
      perror ("Error parsing arguments");
      exit (1);
    }

  if ( (vers = gtk_check_version (GTK_MAJOR_VERSION,
				 GTK_MINOR_VERSION,
				 GTK_MICRO_VERSION)) )
    {
      g_warning ("%s", vers);
    }

  /* Let GDK remove any options that it owns. */
  gdk_init (&argc, &argv);

  /* Parse our own options. */
  ss = create_source_stream ();
  parser = argv_parser_create ();
  argv_parser_add_options (parser, startup_options, N_STARTUP_OPTIONS,
                           startup_option_callback, &show_splash);
  source_init_register_argv_parser (parser, ss);
  if (!argv_parser_run (parser, argc, argv))
    exit (EXIT_FAILURE);
  argv_parser_destroy (parser);

  init_p.splash_window = create_splash_window ();
  init_p.ss = ss;
  init_p.data_file = optind < argc ? argv[optind] : NULL;

  if ( show_splash )
    gtk_widget_show (init_p.splash_window);

  g_idle_add (quit_one_loop, 0);

  gtk_quit_add (0, run_inner_loop, &init_p);
  gtk_main ();

  return 0;
}
