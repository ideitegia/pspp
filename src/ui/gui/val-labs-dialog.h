/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2005, 2011, 2012  Free Software Foundation

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

#ifndef PSPPIRE_VAL_LABS_DIALOG_H
#define PSPPIRE_VAL_LABS_DIALOG_H


/*  This module describes the behaviour of the Value Labels dialog box,
    used for input of the value labels in the variable sheet */


#include <gtk/gtk.h>
#include "data/format.h"
#include "data/variable.h"
#include "ui/gui/psppire-dialog.h"

G_BEGIN_DECLS

#define PSPPIRE_TYPE_VAL_LABS_DIALOG             (psppire_val_labs_dialog_get_type())
#define PSPPIRE_VAL_LABS_DIALOG(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),PSPPIRE_TYPE_VAL_LABS_DIALOG,PsppireValLabsDialog))
#define PSPPIRE_VAL_LABS_DIALOG_CLASS(class)     (G_TYPE_CHECK_CLASS_CAST ((class),PSPPIRE_TYPE_VAL_LABS_DIALOG,PsppireValLabsDialogClass))
#define PSPPIRE_IS_VAL_LABS_DIALOG(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),PSPPIRE_TYPE_VAL_LABS_DIALOG))
#define PSPPIRE_IS_VAL_LABS_DIALOG_CLASS(class)  (G_TYPE_CHECK_CLASS_TYPE ((class),PSPPIRE_TYPE_VAL_LABS_DIALOG))
#define PSPPIRE_VAL_LABS_DIALOG_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),PSPPIRE_TYPE_VAL_LABS_DIALOG,PsppireValLabsDialogClass))

typedef struct _PsppireValLabsDialog      PsppireValLabsDialog;
typedef struct _PsppireValLabsDialogClass PsppireValLabsDialogClass;

struct _PsppireValLabsDialog {
  PsppireDialog parent;

  struct val_labs *labs;
  gchar *encoding;
  struct fmt_spec format;

  /* Actions */
  GtkWidget *add_button;
  GtkWidget *remove_button;
  GtkWidget *change_button;

  /* Entry Boxes */
  GtkWidget *value_entry;
  GtkWidget *label_entry;

  /* Signal handler ids */
  gint change_handler_id;
  gint value_handler_id;

  GtkWidget *treeview;
};

struct _PsppireValLabsDialogClass {
  PsppireDialogClass parent_class;
};

GType psppire_val_labs_dialog_get_type (void) G_GNUC_CONST;
PsppireValLabsDialog* psppire_val_labs_dialog_new (const struct variable *);

void psppire_val_labs_dialog_set_variable (PsppireValLabsDialog *,
                                           const struct variable *);
const struct val_labs *psppire_val_labs_dialog_get_value_labels (
  const PsppireValLabsDialog *);

struct val_labs *psppire_val_labs_dialog_run (GtkWindow *parent_window,
                                              const struct variable *);

G_END_DECLS

#endif /* psppire-val-labs-dialog.h */
