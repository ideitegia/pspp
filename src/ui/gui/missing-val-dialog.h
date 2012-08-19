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

#ifndef PSPPIRE_MISSING_VAL_DIALOG_H
#define PSPPIRE_MISSING_VAL_DIALOG_H 1

/*  This module describes the behaviour of the Missing Values dialog box,
    used for input of the missing values in the variable sheet */

#include <gtk/gtk.h>
#include "data/format.h"
#include "data/missing-values.h"
#include "ui/gui/psppire-dialog.h"

G_BEGIN_DECLS

struct variable;

#define PSPPIRE_TYPE_MISSING_VAL_DIALOG             (psppire_missing_val_dialog_get_type())
#define PSPPIRE_MISSING_VAL_DIALOG(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),PSPPIRE_TYPE_MISSING_VAL_DIALOG,PsppireMissingValDialog))
#define PSPPIRE_MISSING_VAL_DIALOG_CLASS(class)     (G_TYPE_CHECK_CLASS_CAST ((class),PSPPIRE_TYPE_MISSING_VAL_DIALOG,PsppireMissingValDialogClass))
#define PSPPIRE_IS_MISSING_VAL_DIALOG(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),PSPPIRE_TYPE_MISSING_VAL_DIALOG))
#define PSPPIRE_IS_MISSING_VAL_DIALOG_CLASS(class)  (G_TYPE_CHECK_CLASS_TYPE ((class),PSPPIRE_TYPE_MISSING_VAL_DIALOG))
#define PSPPIRE_MISSING_VAL_DIALOG_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),PSPPIRE_TYPE_MISSING_VAL_DIALOG,PsppireMissingValDialogClass))

typedef struct _PsppireMissingValDialog      PsppireMissingValDialog;
typedef struct _PsppireMissingValDialogClass PsppireMissingValDialogClass;

struct _PsppireMissingValDialog {
  PsppireDialog parent;

  struct missing_values mvl;
  gchar *encoding;
  struct fmt_spec format;

  /* Radio Buttons */
  GtkToggleButton *button_none;
  GtkToggleButton *button_discrete;
  GtkToggleButton *button_range;

  /* Entry boxes */
  GtkWidget *mv[3];
  GtkWidget *low;
  GtkWidget *high;
  GtkWidget *discrete;
};

struct _PsppireMissingValDialogClass {
  PsppireDialogClass parent_class;
};

GType psppire_missing_val_dialog_get_type (void) G_GNUC_CONST;
PsppireMissingValDialog* psppire_missing_val_dialog_new (
  const struct variable *);

void psppire_missing_val_dialog_set_variable (PsppireMissingValDialog *,
                                              const struct variable *);
const struct missing_values *psppire_missing_val_dialog_get_missing_values (
  const PsppireMissingValDialog *);

void psppire_missing_val_dialog_run (GtkWindow *parent_window,
                                     const struct variable *,
                                     struct missing_values *);


G_END_DECLS

#endif /* PSPPIRE_MISSING_VAL_DIALOG_H */
