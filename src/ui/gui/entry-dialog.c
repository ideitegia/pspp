/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011 Free Software Foundation

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

#include "ui/gui/entry-dialog.h"

#include "ui/gui/builder-wrapper.h"
#include "ui/gui/psppire-dialog.h"

#include "gl/xalloc.h"

/* Creates a modal dialog with PARENT as its parent (this should be the
   application window that the dialog is associated with), with TITLE as its
   title, that prompts for a text string with PROMPT as the explanation and
   DEFAULT_VALUE as the default value.

   Returns a malloc()'d string owned by the caller if the user clicks on OK or
   otherwise accepts a value, or NULL if the user cancels. */
char *
entry_dialog_run (GtkWindow *parent,
                  const char *title,
                  const char *prompt,
                  const char *default_value)
{
  GtkBuilder *xml = builder_new ("entry-dialog.ui");
  GtkWidget *dialog;
  GtkWidget *label;
  GtkWidget *entry;
  char *result;

  dialog = get_widget_assert (xml, "entry-dialog");
  gtk_window_set_title (GTK_WINDOW (dialog), title);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

  label = get_widget_assert (xml, "label");
  gtk_label_set_text (GTK_LABEL (label), prompt);

  entry = get_widget_assert (xml, "entry");
  gtk_entry_set_text (GTK_ENTRY (entry), default_value);

  result = (psppire_dialog_run (PSPPIRE_DIALOG (dialog)) == GTK_RESPONSE_OK
            ? xstrdup (gtk_entry_get_text (GTK_ENTRY (entry)))
            : NULL);

  g_object_unref (xml);

  return result;
}
