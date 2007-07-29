/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007  Free Software Foundation

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

#include "widget-io.h"

#include <string.h>
#include <stdlib.h>
#include <gl/printf-parse.h>
#include <stdarg.h>
#include <gtk/gtk.h>

#include <gl/gettext.h>



static void
ship_label (GtkBox *box, const char **s, const char_directive *dir)
{
  GtkWidget *label ;
  gchar *text = g_strdup (*s);

  if ( dir )
    {
      text [ dir->dir_start - *s ] = '\0';
      *s = dir->dir_end;
    }

  label = gtk_label_new (text);
  g_free (text);

  gtk_box_pack_start (box, label, FALSE, FALSE, 0);
  gtk_widget_show (label);
}

/* Returns a string generated from FMT and a list of GtkEntry widgets.
   Each conversion in FMT will be replaced with the text from the
   corresponding GtkEntry.  The normal printf semantics will be ignored.
   Note that the GtkEntry widgets may be GtkSpinbuttons or any other widget
   derived from GtkEntry.
   The returned string should be freed when no longer required.
 */
gchar *
widget_printf (const gchar *fmt, ...)
{
  gint i;
  char_directives d;
  arguments a;
  GString *output;
  GtkWidget **widgets;
  gchar *text;
  va_list ap;
  const char *s = fmt;

  if ( 0 !=  printf_parse (fmt, &d, &a) )
    return NULL;

  widgets = calloc (sizeof (*widgets), d.count);
  va_start (ap, fmt);
  for (i = 0 ; i < d.count ; ++i )
    {
      if ( d.dir[i].conversion != '%')
	widgets[i] = va_arg (ap, GtkWidget *);
    }
  va_end (ap);

  g_free (a.arg);

  output = g_string_sized_new (strlen (fmt));

  for (i = 0 ; i < d.count ; ++i )
    {
      char_directive dir = d.dir[i];
      GtkWidget *w ;
      const gchar *entry_text;

      if ( dir.conversion == '%')
	{
	  s++;
	  continue;
	}

      w = widgets [dir.arg_index];
      entry_text = gtk_entry_get_text (GTK_ENTRY (w));

      if ( dir.dir_start > s )
	g_string_append_len (output, s, dir.dir_start - s);

      s = dir.dir_end;

      g_string_append (output, entry_text);
    }

  free (widgets);
  free (d.dir);

  if (*s)
    g_string_append_len (output, s, -1);

  text = output->str;
  g_string_free (output, FALSE);
  return text;
}

/*
   Returns a GtkHBox populated with an GtkLabel and GtkEntry widgets.
   Each conversion in FMT will cause a GtkEntry (possibly a GtkSpinButton) to
   be created.  Any text between conversions produces a GtkLabel.
   There should be N arguments following FMT should be of type GtkEntry **,
   where N is the number of conversions.
   These arguments will be filled with a pointer to the corresponding widgets.
   Their properties may be changed, but they should not be unrefed.
 */
GtkWidget *
widget_scanf (const gchar *fmt, ...)
{
  char_directives d;
  arguments a;
  int i;
  va_list ap;
  GtkWidget ***widgets = NULL;
  GtkWidget *hbox = NULL;
  GtkWidget **w;
  const char *s = fmt;

  if ( 0 !=  printf_parse (fmt, &d, &a) )
    return NULL;

  g_free (a.arg);

  va_start (ap, fmt);

  if ( d.count > 0 )
    {
      hbox = gtk_hbox_new (FALSE, 0);
      widgets = calloc (sizeof (*widgets), d.count);
    }

  for (i = 0 ; i < d.count ; ++i )
    {
      if ( d.dir[i].conversion != '%')
	widgets[i] = va_arg (ap, GtkWidget **);
    }
  va_end (ap);


  for (i = 0 ; i < d.count ; ++i )
    {
      char_directive dir = d.dir[i];
      int precision = 0;
      int width = 0;


      if ( dir.precision_start && dir.precision_end)
	precision = strtol (dir.precision_start + 1,
			    (char **) &dir.precision_end, 10);

      if ( dir.width_start && dir.width_end )
	width = strtol (dir.width_start, (char **) &dir.width_end, 10);

      if ( dir.dir_start > s )
	ship_label (GTK_BOX (hbox), &s, &dir);

      if ( dir.conversion == '%')
	{
	  s++;
	  continue;
	}

      w = widgets [dir.arg_index];
      switch (dir.conversion)
	{
	case 'd':
	case 'i':
	case 'f':
	  {
	    *w = gtk_spin_button_new_with_range (0, 100.0, 1.0);
	    g_object_set (*w, "digits", precision, NULL);
	  }
	  break;
	case 's':
	  *w = gtk_entry_new ();
	  break;
	};

      g_object_set (*w, "width-chars", width, NULL);
      gtk_box_pack_start (GTK_BOX (hbox), *w, FALSE, FALSE, 0);
      gtk_widget_show (*w);
    }

  if ( *s )
    ship_label (GTK_BOX (hbox), &s, NULL);


  g_free (widgets);

  free (d.dir);

  return hbox;
}
