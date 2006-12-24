/*
   PSPPIRE --- A Graphical User Interface for PSPP
   Copyright (C) 2005  Free Software Foundation

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
   02110-1301, USA.
*/

/*
   This widget is a subclass of GtkEntry.  It's an entry widget with a
   button on the right hand side.

   This code is heavily based upon the GtkSpinButton widget.  Therefore
   the copyright notice of that code is pasted below.

   Please note however,  this code is covered by the GPL, not the LGPL.
*/

/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * GtkSpinButton widget for GTK+
 * Copyright (C) 1998 Lars Hamann and Stefan Jeske
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */


#ifndef __PSPPIRE_CUSTOM_ENTRY_H__
#define __PSPPIRE_CUSTOM_ENTRY_H__


#include <glib.h>
#include <glib-object.h>


GType psppire_custom_entry_get_type (void);

G_BEGIN_DECLS

#define PSPPIRE_CUSTOM_ENTRY_TYPE (psppire_custom_entry_get_type ())

#define PSPPIRE_CUSTOM_ENTRY(obj)            \
     (G_TYPE_CHECK_INSTANCE_CAST ((obj),PSPPIRE_CUSTOM_ENTRY_TYPE, PsppireCustomEntry))

#define PSPPIRE_CUSTOM_ENTRY_CLASS(klass)    \
     (G_TYPE_CHECK_CLASS_CAST ((klass),PSPPIRE_CUSTOM_ENTRY_TYPE, PsppireCustomEntryClass))

#define PSPPIRE_IS_CUSTOM_ENTRY(obj)         \
     (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_CUSTOM_ENTRY_TYPE))

#define IS_PSPPIRE_CUSTOM_ENTRY_CLASS(klass) \
     (G_TYPE_CHECK_CLASS_TYPE ((klass),  PSPPIRE_CUSTOM_ENTRY_TYPE))


typedef struct _PsppireCustomEntry       PsppireCustomEntry;
typedef struct _PsppireCustomEntryClass  PsppireCustomEntryClass;

struct _PsppireCustomEntry
{
  GtkEntry entry;

  GdkWindow *panel;
};

struct _PsppireCustomEntryClass
{
  GtkEntryClass parent_class;

  void (*clicked)  (PsppireCustomEntry *spin_button);

};

GType          custom_entry_get_type        (void);
GtkWidget*     custom_entry_new             (void);

G_END_DECLS

#endif /* __PSPPIRE_CUSTOM_ENTRY_H__ */
