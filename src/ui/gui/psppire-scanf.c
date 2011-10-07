/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include <gtk/gtk.h>
#include "psppire-scanf.h"

#include <gl/printf-parse.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "xalloc.h"


static void psppire_scanf_class_init          (PsppireScanfClass *class);
static void psppire_scanf_init                (PsppireScanf      *w);

G_DEFINE_TYPE (PsppireScanf, psppire_scanf, GTK_TYPE_HBOX)

/* Properties */
enum
{
  PROP_0,
  PROP_FORMAT,
  PROP_NCONV,
  PROP_USE_UNDERLINE,
  PROP_MNEMONIC_WIDGET
};

/* Create a GtkLabel and pack it into BOX.
   The label is created using part of the string at S, and the directives
   at DIRS[DIR_IDX] and subsequent.

   After this function returns, *S points to the first unused character.
*/
static void
ship_label (PsppireScanf *box, const char **s,
	    const char_directives *dirs, size_t dir_idx)
{
  GtkWidget *label ;
  GString *str = g_string_new (*s);

  if ( dirs)
    {
      char_directive dir = dirs->dir[dir_idx];
      int n = 0;

      while (dir_idx < dirs->count && dir.conversion == '%' )
	{
	  g_string_erase (str, dir.dir_start - *s, 1);
	  dir = dirs->dir[++dir_idx];
	  n++;
	}

      g_string_truncate (str, dir.dir_start - *s - n);

      if ( dir_idx >= dirs->count)
	*s = NULL;
      else
	*s = dir.dir_end;
    }

  label = gtk_label_new (str->str);

  g_string_free (str, TRUE);

  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
}

static void
guts (PsppireScanf *scanf)
{
  gint i;
  arguments a;
  const char *s = scanf->format;

  /* Get the number of args into D */
  g_return_if_fail (0 == printf_parse (scanf->format, &scanf->d, &a));

  if ( scanf->d.count > 0)
    scanf->widgets = xcalloc (scanf->d.count, sizeof (*scanf->widgets));

  /* A is not used, so get rid of it */
  if (a.arg != a.direct_alloc_arg)
    free (a.arg);

  for (i = 0 ; i < scanf->d.count ; ++i )
    {
      GtkWidget **w;
      char_directive dir = scanf->d.dir[i];
      int precision = 0;
      int width = 0;

      if ( dir.precision_start && dir.precision_end)
	precision = strtol (dir.precision_start + 1,
			    (char **) &dir.precision_end, 10);

      if ( dir.width_start && dir.width_end )
	width = strtol (dir.width_start, (char **) &dir.width_end, 10);

      if ( dir.dir_start > s )
	ship_label (scanf, &s, &scanf->d, i);

      if ( dir.conversion == '%')
	{
	  if (s) s++;
	  continue;
	}

      w = &scanf->widgets [dir.arg_index];
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
      gtk_box_pack_start (GTK_BOX (scanf), *w, FALSE, FALSE, 0);
      gtk_widget_show (*w);
    }

  if ( s && *s )
    ship_label (scanf, &s, NULL, 0);

}


static void
set_mnemonic (PsppireScanf *scanf)
{
  if (scanf->use_underline || scanf->mnemonic_widget)
    {
      GList *l = gtk_container_get_children (GTK_CONTAINER (scanf));
      while (l)
	{
	  if ( GTK_IS_LABEL (l->data))
	    {
	      const gchar *t = gtk_label_get_label (l->data);
	      if  ( g_strstr_len (t, -1,  "_"))
		{
		  g_object_set (l->data,
				"use-underline", TRUE,
				"mnemonic-widget", scanf->mnemonic_widget,
				NULL);

		  break;
		}
	    }
	  l = l->next;
	}
      g_list_free (l);
    }
}

static void
psppire_scanf_set_property (GObject         *object,
			    guint            prop_id,
			    const GValue    *value,
			    GParamSpec      *pspec)
{
  PsppireScanf *scanf = PSPPIRE_SCANF (object);

  switch (prop_id)
    {
    case PROP_FORMAT:
      scanf->format = g_value_get_string (value);
      guts (scanf);
      break;
    case PROP_MNEMONIC_WIDGET:
      scanf->mnemonic_widget = g_value_get_object (value);
      set_mnemonic (scanf);
      break;
    case PROP_USE_UNDERLINE:
      scanf->use_underline = g_value_get_boolean (value);
      set_mnemonic (scanf);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_scanf_get_property (GObject         *object,
			    guint            prop_id,
			    GValue          *value,
			    GParamSpec      *pspec)
{
  PsppireScanf *scanf = PSPPIRE_SCANF (object);

  switch (prop_id)
    {
    case PROP_FORMAT:
      g_value_set_string (value, scanf->format);
      break;
    case PROP_NCONV:
      g_value_set_int (value, scanf->d.count);
      break;
    case PROP_USE_UNDERLINE:
      g_value_set_boolean (value, scanf->use_underline);
      break;
    case PROP_MNEMONIC_WIDGET:
      g_value_set_object (value, scanf->mnemonic_widget);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static GObjectClass *parent_class = NULL;

static void
psppire_scanf_dispose (GObject *obj)
{
  PsppireScanf *w = (PsppireScanf *)obj;

  if (w->dispose_has_run)
    return;

  /* Make sure dispose does not run twice. */
  w->dispose_has_run = TRUE;


  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
psppire_scanf_finalize (GObject *obj)
{
  PsppireScanf *w = PSPPIRE_SCANF (obj);

  free (w->widgets);

  if (w->d.dir != w->d.direct_alloc_dir)
    free (w->d.dir);

   /* Chain up to the parent class */
   G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
psppire_scanf_class_init (PsppireScanfClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  GParamSpec *format_spec =
    g_param_spec_string ("format",
		       "Format",
		       "A Scanf style format string",
		       NULL,
		       G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  GParamSpec *nconv_spec =
    g_param_spec_int ("n-conv",
		       "Conversions",
		       "The number of conversions in the format string",
		      0, G_MAXINT, 0,
		       G_PARAM_READABLE);


  GParamSpec *use_underline_spec =
    g_param_spec_boolean ("use-underline",
		       "Use Underline",
		       "If set, an underline in the text indicates the next character should be used for the mnemonic accelerator key",
			  FALSE,
			  G_PARAM_READWRITE);


  GParamSpec *mnemonic_widget_spec =
    g_param_spec_object ("mnemonic-widget",
		       "Mnemonic widget",
		       "The widget which is to be activated when the Scanf's mnemonic key is pressed.  Has no effect if use-underline is false.",
			 GTK_TYPE_WIDGET,
			 G_PARAM_READWRITE);


  parent_class = g_type_class_peek_parent (class);

  object_class->dispose = psppire_scanf_dispose;
  object_class->finalize = psppire_scanf_finalize;

  object_class->set_property = psppire_scanf_set_property;
  object_class->get_property = psppire_scanf_get_property;

  g_object_class_install_property (object_class,
                                   PROP_NCONV,
                                   nconv_spec);

  g_object_class_install_property (object_class,
                                   PROP_FORMAT,
                                   format_spec);

  g_object_class_install_property (object_class,
                                   PROP_USE_UNDERLINE,
                                   use_underline_spec);

  g_object_class_install_property (object_class,
                                   PROP_MNEMONIC_WIDGET,
                                   mnemonic_widget_spec);
}



static void
psppire_scanf_init (PsppireScanf *w)
{
}

gchar
psppire_get_conversion_char (PsppireScanf *w, gint n)
{
  g_return_val_if_fail ( n < w->d.count, '\0');
  return w->d.dir[n].conversion;
}

GtkWidget *
psppire_scanf_get_child (PsppireScanf *w, gint n)
{
  g_return_val_if_fail ( n < w->d.count, NULL);
  return w->widgets[n];
}


/*
   This widget is a GtkHBox populated with GtkLabel and GtkEntry widgets.
   Each conversion in FMT will cause a GtkEntry (possibly a GtkSpinButton) to
   be created.  Any text between conversions produces a GtkLabel.
   There should be N arguments following FMT should be of type GtkEntry **,
   where N is the number of conversions.
   These arguments will be filled with a pointer to the corresponding widgets.
   Their properties may be changed, but they should not be unrefed.
 */
GtkWidget *
psppire_scanf_new (const gchar *fmt, ...)
{
  gint n, i;
  va_list ap;

  GtkWidget *w = GTK_WIDGET (g_object_new (psppire_scanf_get_type (),
				   "format", fmt, NULL));

  g_object_get (w, "n-conv", &n, NULL);

  va_start (ap, fmt);

  for (i = 0 ; i < n ; ++i )
    {
      GtkWidget **field;

      if ( psppire_get_conversion_char (PSPPIRE_SCANF (w), i) == '%')
	continue;

      field = va_arg (ap, GtkWidget **);

      *field = psppire_scanf_get_child (PSPPIRE_SCANF (w), i);
    }
  va_end (ap);

  return w;
}
