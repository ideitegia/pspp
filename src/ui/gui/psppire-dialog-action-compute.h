/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2013  Free Software Foundation

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


#include <glib-object.h>
#include <glib.h>

#include "psppire-dialog-action.h"

#ifndef __PSPPIRE_DIALOG_ACTION_COMPUTE_H__
#define __PSPPIRE_DIALOG_ACTION_COMPUTE_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_COMPUTE (psppire_dialog_action_compute_get_type ())

#define PSPPIRE_DIALOG_ACTION_COMPUTE(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_COMPUTE, PsppireDialogActionCompute))

#define PSPPIRE_DIALOG_ACTION_COMPUTE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_COMPUTE, \
                                 PsppireDialogActionComputeClass))


#define PSPPIRE_IS_DIALOG_ACTION_COMPUTE(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_COMPUTE))

#define PSPPIRE_IS_DIALOG_ACTION_COMPUTE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_COMPUTE))


#define PSPPIRE_DIALOG_ACTION_COMPUTE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_COMPUTE, \
				   PsppireDialogActionComputeClass))

typedef struct _PsppireDialogActionCompute       PsppireDialogActionCompute;
typedef struct _PsppireDialogActionComputeClass  PsppireDialogActionComputeClass;


struct _PsppireDialogActionCompute
{
  PsppireDialogAction parent;

  /*< private >*/
  gboolean dispose_has_run ;

  gboolean use_type;


  GtkWidget *subdialog;

  GtkWidget *entry;
  GtkWidget *width_entry;
  GtkWidget *user_label;
  GtkWidget *numeric_target;
  GtkWidget *textview;

  GtkWidget *functions;
  GtkWidget *keypad;
  GtkWidget *target;
  GtkWidget *var_selector;
  GtkWidget *func_selector;
  GtkWidget *type_and_label;
  GtkWidget *expression;
  GtkWidget *str_btn;
};


struct _PsppireDialogActionComputeClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_compute_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_COMPUTE_H__ */
