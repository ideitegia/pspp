/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009, 2011  Free Software Foundation

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

#include "xalloc.h"


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

  widgets = xcalloc (d.count, sizeof (*widgets));
  va_start (ap, fmt);
  for (i = 0 ; i < d.count ; ++i )
    {
      if ( d.dir[i].conversion != '%')
	widgets[i] = va_arg (ap, GtkWidget *);
    }
  va_end (ap);

  if (a.arg != a.direct_alloc_arg)
    free (a.arg);

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
  if (d.dir != d.direct_alloc_dir)
    free (d.dir);

  if (*s)
    g_string_append_len (output, s, -1);

  text = output->str;
  g_string_free (output, FALSE);
  return text;
}
