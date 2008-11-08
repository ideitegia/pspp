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
#include <argp.h>
#include <gl/relocatable.h>
#include <ui/command-line.h>
#include <ui/source-init-opts.h>

#include <libpspp/version.h>
#include <libpspp/copyleft.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

const char *argp_program_version = version;
const char *argp_program_bug_address = PACKAGE_BUGREPORT;


/* Arguments to be interpreted before the X server gets initialised */

static const struct argp_option startup_options [] =
  {
    {"no-splash",  'q',  0,  0,  N_("Don't show the splash screen"), 0 },
    { 0, 0, 0, 0, 0, 0 }
  };

static error_t
parse_startup_opts (int key, char *arg, struct argp_state *state)
{
  gboolean *showsplash = state->input;

  switch (key)
    {
    case 'q':
      *showsplash = FALSE;
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp startup_argp = {startup_options, parse_startup_opts, 0, 0, 0, 0, 0};



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
  int argc;
  char **argv;
  GtkWidget *splash_window;
  struct command_line_processor *clp;
};


static gboolean
run_inner_loop (gpointer data)
{
  struct initialisation_parameters *ip = data;
  initialize (ip->clp, ip->argc, ip->argv);

  g_timeout_add (500, hide_splash_window, ip->splash_window);

  gtk_main ();

  de_initialize ();

  return FALSE;
}



int
main (int argc, char *argv[])
{
  struct command_line_processor *clp ;
  struct initialisation_parameters init_p;
  gboolean show_splash = TRUE;

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
      g_warning (vers);
    }

  clp = command_line_processor_create (_("PSPPIRE --- A user interface for PSPP"), "[ DATA-FILE ]", 0);

  command_line_processor_add_options (clp, &startup_argp, _("Miscellaneous options:"),  &show_splash);
  command_line_processor_add_options (clp, &post_init_argp,
				      _("Options affecting syntax and behavior:"),  NULL);
  command_line_processor_add_options (clp, &non_option_argp, NULL, NULL);

  command_line_processor_parse (clp, argc, argv);

  gdk_init (&argc, &argv);

  init_p.splash_window = create_splash_window ();
  init_p.argc = argc;
  init_p.argv = argv;
  init_p.clp = clp;

  if ( show_splash )
    gtk_widget_show (init_p.splash_window);

  g_idle_add (quit_one_loop, 0);

  gtk_quit_add (0, run_inner_loop, &init_p);
  gtk_main ();

  return 0;
}
