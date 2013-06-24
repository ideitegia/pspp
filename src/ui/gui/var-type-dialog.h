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


#ifndef PSPPIRE_VAR_TYPE_DIALOG_H
#define PSPPIRE_VAR_TYPE_DIALOG_H 1

#include "data/format.h"
#include "psppire-dialog.h"

G_BEGIN_DECLS

#define PSPPIRE_TYPE_VAR_TYPE_DIALOG             (psppire_var_type_dialog_get_type())
#define PSPPIRE_VAR_TYPE_DIALOG(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),PSPPIRE_TYPE_VAR_TYPE_DIALOG,PsppireVarTypeDialog))
#define PSPPIRE_VAR_TYPE_DIALOG_CLASS(class)     (G_TYPE_CHECK_CLASS_CAST ((class),PSPPIRE_TYPE_VAR_TYPE_DIALOG,PsppireVarTypeDialogClass))
#define PSPPIRE_IS_VAR_TYPE_DIALOG(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),PSPPIRE_TYPE_VAR_TYPE_DIALOG))
#define PSPPIRE_IS_VAR_TYPE_DIALOG_CLASS(class)  (G_TYPE_CHECK_CLASS_TYPE ((class),PSPPIRE_TYPE_VAR_TYPE_DIALOG))
#define PSPPIRE_VAR_TYPE_DIALOG_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),PSPPIRE_TYPE_VAR_TYPE_DIALOG,PsppireVarTypeDialogClass))

typedef struct _PsppireVarTypeDialog      PsppireVarTypeDialog;
typedef struct _PsppireVarTypeDialogClass PsppireVarTypeDialogClass;

/*  This module describes the behaviour of the Variable Type dialog box,
    used for input of the variable type parameter in the var sheet */

enum
  {
    BUTTON_NUMERIC,
    BUTTON_COMMA,
    BUTTON_DOT,
    BUTTON_SCIENTIFIC,
    BUTTON_DATE,
    BUTTON_DOLLAR,
    BUTTON_CUSTOM,
    BUTTON_STRING,
    num_BUTTONS
  };

struct variable;

struct _PsppireVarTypeDialog {
  PsppireDialog parent;

  /* Format being edited. */
  struct fmt_spec base_format;

  /* Current version of format. */
  struct fmt_spec fmt_l;

  /* Toggle Buttons */
  GtkWidget *radioButton[num_BUTTONS];

  /* Decimals */
  GtkWidget *label_decimals;
  GtkWidget *entry_decimals;
  GtkAdjustment *adj_decimals;

  /* Width */
  GtkWidget *entry_width;
  GtkAdjustment *adj_width;

  /* Container for width/decimals entry/labels */
  GtkWidget *width_decimals;

  /* Date */
  GtkWidget *date_format_list;
  GtkTreeView *date_format_treeview;

  /* Dollar */
  GtkWidget *dollar_window;
  GtkTreeView *dollar_treeview;

  /* Custom Currency */
  GtkWidget *custom_currency_hbox;
  GtkTreeView *custom_treeview;
  GtkWidget *label_psample;
  GtkWidget *label_nsample;

  /* Actions */
  GtkWidget *ok;

  gint active_button;
};

struct _PsppireVarTypeDialogClass {
  PsppireDialogClass parent_class;
};

GType psppire_var_type_dialog_get_type (void) G_GNUC_CONST;
PsppireVarTypeDialog* psppire_var_type_dialog_new (const struct fmt_spec *);

void psppire_var_type_dialog_run (GtkWindow *parent_window,
                                  struct fmt_spec *format);

G_END_DECLS

#endif /* PSPPIRE_VAR_TYPE_DIALOG_H */
