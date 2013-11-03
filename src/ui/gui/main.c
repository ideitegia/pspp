/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006, 2010, 2011, 2012, 2013  Free Software Foundation

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

#include "language/lexer/include-path.h"
#include "libpspp/argv-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/copyleft.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"
#include "libpspp/version.h"
#include "ui/source-init-opts.h"

#include "gl/configmake.h"
#include "gl/progname.h"
#include "gl/relocatable.h"
#include "gl/version-etc.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


/* Arguments to be interpreted before the X server gets initialised */

enum
  {
    OPT_HELP,
    OPT_VERSION,
    OPT_NO_SPLASH,
    OPT_MEASURE_STARTUP,
    N_STARTUP_OPTIONS
  };

static const struct argv_option startup_options[N_STARTUP_OPTIONS] =
  {
    {"help",      'h', no_argument, OPT_HELP},
    {"version",   'V', no_argument, OPT_VERSION},
    {"no-splash", 'q', no_argument, OPT_NO_SPLASH},
    {"measure-startup", 0, no_argument, OPT_MEASURE_STARTUP},
  };

/* --measure-startup: Prints the elapsed time to start up and load any file
   specified on the command line. */
static gboolean measure_startup;
static GTimer *startup;

static void
usage (void)
{
  char *inc_path = string_array_join (include_path_default (), " ");
  GOptionGroup *gtk_options;
  GOptionContext *ctx;
  gchar *gtk_help_base, *gtk_help;

  /* Get help text for GTK+ options.  */
  ctx = g_option_context_new ("psppire");
  gtk_options = gtk_get_option_group (FALSE);
  gtk_help_base = g_option_context_get_help (ctx, FALSE, gtk_options);
  g_option_context_free (ctx);

  /* The GTK+ help text starts with usage instructions that we don't want,
     followed by a blank line.  Trim off everything up to and including the
     first blank line. */
  gtk_help = strstr (gtk_help_base, "\n\n");
  gtk_help = gtk_help != NULL ? gtk_help + 2 : gtk_help_base;

  printf (_("\
PSPPIRE, a GUI for PSPP, a program for statistical analysis of sampled data.\n\
Usage: %s [OPTION]... FILE\n\
\n\
Arguments to long options also apply to equivalent short options.\n\
\n\
GUI options:\n\
  -q, --no-splash           don't show splash screen during startup\n\
\n\
%s\
Language options:\n\
  -I, --include=DIR         append DIR to search path\n\
  -I-, --no-include         clear search path\n\
  -a, --algorithm={compatible|enhanced}\n\
                            set to `compatible' if you want output\n\
                            calculated from broken algorithms\n\
  -x, --syntax={compatible|enhanced}\n\
                            set to `compatible' to disable PSPP extensions\n\
  -i, --interactive         interpret syntax in interactive mode\n\
  -s, --safer               don't allow some unsafe operations\n\
Default search path: %s\n\
\n\
Informative output:\n\
  -h, --help                display this help and exit\n\
  -V, --version             output version information and exit\n\
\n\
A non-option argument is interpreted as a data file in .sav or .zsav or .por\n\
format or a syntax file to load.\n"),
          program_name, gtk_help, inc_path);

  free (inc_path);
  g_free (gtk_help_base);

  emit_bug_reporting_address ();
  exit (EXIT_SUCCESS);
}

static void
startup_option_callback (int id, void *show_splash_)
{
  gboolean *show_splash = show_splash_;

  switch (id)
    {
    case OPT_HELP:
      usage ();
      break;

    case OPT_VERSION:
      version_etc (stdout, "psppire", PACKAGE_NAME, PACKAGE_VERSION,
                   "Ben Pfaff", "John Darrington", "Jason Stover",
                   NULL_SENTINEL);
      exit (EXIT_SUCCESS);

    case OPT_NO_SPLASH:
      *show_splash = FALSE;
      break;

    case OPT_MEASURE_STARTUP:
      measure_startup = TRUE;
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
print_startup_time (gpointer data)
{
  g_timer_stop (startup);
  printf ("%.3f seconds elapsed\n", g_timer_elapsed (startup, NULL));
  g_timer_destroy (startup);
  startup = NULL;

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
  const char *data_file;
  GtkWidget *splash_window;
};


static gboolean
run_inner_loop (gpointer data)
{
  struct initialisation_parameters *ip = data;
  initialize (ip->data_file);

  g_timeout_add (500, hide_splash_window, ip->splash_window);

  if (measure_startup)
    {
      GSource *source = g_idle_source_new ();
      g_source_set_priority (source, G_PRIORITY_LOW);
      g_source_set_callback (source, print_startup_time, NULL, NULL);
      g_source_attach (source, NULL);
      g_source_unref (source);
    }

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

#ifdef __APPLE__
static const bool apple = true;
#else
static const bool apple = false;
#endif

/* Searches ARGV for the -psn_xxxx option that the desktop application
   launcher passes in, and removes it if it finds it.  Returns the new value
   of ARGC. */
static inline int
remove_psn (int argc, char **argv)
{
  if (apple)
    {
      int i;

      for (i = 0; i < argc; i++)
	{
	  if (!strncmp (argv[i], "-psn", 4))
	    {
	      remove_element (argv, argc + 1, sizeof *argv, i);
	      return argc - 1;
	    }
	}
    }
  return argc;
}

int
main (int argc, char *argv[])
{
  struct initialisation_parameters init_p;
  gboolean show_splash = TRUE;
  struct argv_parser *parser;
  const gchar *vers;

  set_program_name (argv[0]);

  g_mem_set_vtable (&vtable);
  g_thread_init (NULL);

  gtk_disable_setlocale ();

  startup = g_timer_new ();
  g_timer_start (startup);

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

  argc = remove_psn (argc, argv);

  /* Parse our own options. 
     This must come BEFORE gdk_init otherwise options such as 
     --help --version which ought to work without an X server, won't.
  */
  parser = argv_parser_create ();
  argv_parser_add_options (parser, startup_options, N_STARTUP_OPTIONS,
                           startup_option_callback, &show_splash);
  source_init_register_argv_parser (parser);
  if (!argv_parser_run (parser, argc, argv))
    exit (EXIT_FAILURE);
  argv_parser_destroy (parser);

  /* Initialise GDK.  Theoretically this call can remove options from argc,argv if
     it thinks they are gdk options.
     However there shouldn't be any here because of the gtk_parse_args call above. */
  gdk_init (&argc, &argv);

  init_p.splash_window = create_splash_window ();
  init_p.data_file = optind < argc ? argv[optind] : NULL;

  if ( show_splash )
    gtk_widget_show (init_p.splash_window);

  g_idle_add (quit_one_loop, 0);

  gtk_quit_add (0, run_inner_loop, &init_p);
  gtk_main ();

  return 0;
}
