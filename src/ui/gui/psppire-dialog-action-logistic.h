/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012  Free Software Foundation

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

#ifndef __PSPPIRE_DIALOG_ACTION_LOGISTIC_H__
#define __PSPPIRE_DIALOG_ACTION_LOGISTIC_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_LOGISTIC (psppire_dialog_action_logistic_get_type ())

#define PSPPIRE_DIALOG_ACTION_LOGISTIC(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_LOGISTIC, PsppireDialogActionLogistic))

#define PSPPIRE_DIALOG_ACTION_LOGISTIC_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_LOGISTIC, \
                                 PsppireDialogActionLogisticClass))


#define PSPPIRE_IS_DIALOG_ACTION_LOGISTIC(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_LOGISTIC))

#define PSPPIRE_IS_DIALOG_ACTION_LOGISTIC_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_LOGISTIC))


#define PSPPIRE_DIALOG_ACTION_LOGISTIC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_LOGISTIC, \
				   PsppireDialogActionLogisticClass))

typedef struct _PsppireDialogActionLogistic       PsppireDialogActionLogistic;
typedef struct _PsppireDialogActionLogisticClass  PsppireDialogActionLogisticClass;


struct _PsppireDialogActionLogistic
{
  PsppireDialogAction parent;

  /*< private >*/
  gboolean dispose_has_run ;

  GtkWidget *dep_var;
  GtkWidget *indep_vars;

  GtkWidget *opts_dialog;
  GtkWidget *conf_checkbox;
  GtkWidget *conf_entry;
  GtkWidget *const_checkbox;
  GtkWidget *iterations_entry;
  GtkWidget *cut_point_entry;

  gdouble cut_point;
  gint max_iterations;
  gboolean constant;

  gboolean conf;
  gdouble conf_level;
};


struct _PsppireDialogActionLogisticClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_logistic_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_LOGISTIC_H__ */
