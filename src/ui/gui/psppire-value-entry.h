/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012 Free Software Foundation, Inc.

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

#ifndef PSPPIRE_VALUE_ENTRY_H
#define PSPPIRE_VALUE_ENTRY_H 1

#include <gtk/gtk.h>
#include "data/format.h"

/* PsppireValueEntry is a subclass of GtkComboBox that is specialized for
   displaying and entering "union value"s.  Its main advantage over a plain
   GtkEntry is that, when value labels are supplied, it (optionally) displays
   the value label instead of the value.  It also allows the user to choose a
   new value by label from the drop-down list.

   The easiest way to use a PsppireValueEntry is to hand it a particular
   variable whose values are to be displayed, using
   psppire_value_entry_set_variable().  If you do that, you don't need any of
   the other functions to set value labels, format, encoding, width, etc.,
   because all of those are determined from the variable.  The other functions
   are useful if no variable is available. */

G_BEGIN_DECLS

union value;
struct fmt_spec;
struct val_labs;
struct variable;

#define PSPPIRE_TYPE_VALUE_ENTRY             (psppire_value_entry_get_type())
#define PSPPIRE_VALUE_ENTRY(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),PSPPIRE_TYPE_VALUE_ENTRY,PsppireValueEntry))
#define PSPPIRE_VALUE_ENTRY_CLASS(class)     (G_TYPE_CHECK_CLASS_CAST ((class),PSPPIRE_TYPE_VALUE_ENTRY,PsppireValueEntryClass))
#define PSPPIRE_IS_VALUE_ENTRY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),PSPPIRE_TYPE_VALUE_ENTRY))
#define PSPPIRE_IS_VALUE_ENTRY_CLASS(class)  (G_TYPE_CHECK_CLASS_TYPE ((class),PSPPIRE_TYPE_VALUE_ENTRY))
#define PSPPIRE_VALUE_ENTRY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),PSPPIRE_TYPE_VALUE_ENTRY,PsppireValueEntryClass))

typedef struct _PsppireValueEntry      PsppireValueEntry;
typedef struct _PsppireValueEntryClass PsppireValueEntryClass;

struct _PsppireValueEntry 
{
  GtkComboBox parent;

  gboolean show_value_label;

  struct val_labs *val_labs;
  struct fmt_spec format;
  gchar *encoding;

  const union value *cur_value;
};

struct _PsppireValueEntryClass 
{
  GtkComboBoxClass parent_class;
};

GType psppire_value_entry_get_type (void);
GtkWidget *psppire_value_entry_new (void);

void psppire_value_entry_set_show_value_label (PsppireValueEntry *,
                                               gboolean show_value_label);
gboolean psppire_value_entry_get_show_value_label (const PsppireValueEntry *);

void psppire_value_entry_set_variable (PsppireValueEntry *,
                                       const struct variable *);

void psppire_value_entry_set_value_labels (PsppireValueEntry *,
                                           const struct val_labs *);
const struct val_labs *
psppire_value_entry_get_value_labels (const PsppireValueEntry *);

void psppire_value_entry_set_format (PsppireValueEntry *,
                                     const struct fmt_spec *);
const struct fmt_spec *
psppire_value_entry_get_format (const PsppireValueEntry *);

void psppire_value_entry_set_encoding (PsppireValueEntry *, const gchar *);
const gchar *psppire_value_entry_get_encoding (const PsppireValueEntry *);

void psppire_value_entry_set_width (PsppireValueEntry *, int width);
int psppire_value_entry_get_width (const PsppireValueEntry *);

void psppire_value_entry_set_value (PsppireValueEntry *,
                                    const union value *,
                                    int width);
gboolean psppire_value_entry_get_value (PsppireValueEntry *,
                                        union value *,
                                        int width);

G_END_DECLS

#endif /* PSPPIRE_VALUE_ENTRY_H */
