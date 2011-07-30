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
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */


#ifndef __PSPPIRE_SCANF_H__
#define __PSPPIRE_SCANF_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include <gl/printf-parse.h>


G_BEGIN_DECLS

#define PSPPIRE_SCANF_TYPE            (psppire_scanf_get_type ())
#define PSPPIRE_SCANF(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_SCANF_TYPE, PsppireScanf))
#define PSPPIRE_SCANF_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_SCANF_TYPE, PsppireScanfClass))
#define PSPPIRE_IS_SCANF(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_SCANF_TYPE))
#define PSPPIRE_IS_SCANF_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_SCANF_TYPE))


typedef struct _PsppireScanf       PsppireScanf;
typedef struct _PsppireScanfClass  PsppireScanfClass;

/* All members are private. */
struct _PsppireScanf
{
  GtkHBox parent;
  const gchar *format;

  GtkWidget **widgets;
  char_directives d;

  gboolean use_underline;
  GtkWidget *mnemonic_widget;

  gboolean dispose_has_run;
};


struct _PsppireScanfClass
{
  GtkHBoxClass parent_class;
};


GType          psppire_scanf_get_type        (void);
GtkWidget*     psppire_scanf_new             (const gchar *fmt, ...);
GtkWidget *    psppire_scanf_get_child (PsppireScanf *w, gint n);


G_END_DECLS

#endif /* __PSPPIRE_SCANF_H__ */
