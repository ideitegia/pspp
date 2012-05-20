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

#ifndef __PSPPIRE_DIALOG_ACTION_REGRESSION_H__
#define __PSPPIRE_DIALOG_ACTION_REGRESSION_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_REGRESSION (psppire_dialog_action_regression_get_type ())

#define PSPPIRE_DIALOG_ACTION_REGRESSION(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_REGRESSION, PsppireDialogActionRegression))

#define PSPPIRE_DIALOG_ACTION_REGRESSION_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_REGRESSION, \
                                 PsppireDialogActionRegressionClass))


#define PSPPIRE_IS_DIALOG_ACTION_REGRESSION(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_REGRESSION))

#define PSPPIRE_IS_DIALOG_ACTION_REGRESSION_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_REGRESSION))


#define PSPPIRE_DIALOG_ACTION_REGRESSION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_REGRESSION, \
				   PsppireDialogActionRegressionClass))

typedef struct _PsppireDialogActionRegression       PsppireDialogActionRegression;
typedef struct _PsppireDialogActionRegressionClass  PsppireDialogActionRegressionClass;


struct _PsppireDialogActionRegression
{
  PsppireDialogAction parent;

  /*< private >*/
  gboolean dispose_has_run ;

  GtkWidget *dep_vars;
  GtkWidget *indep_vars;

  GtkWidget *resid_button;
  GtkWidget *pred_button;

  GtkWidget *stat_dialog;
  GtkWidget *save_dialog;

  GtkWidget *stat_view;


  /* Save Options */
  gboolean pred;
  gboolean resid;
};


struct _PsppireDialogActionRegressionClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_regression_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_REGRESSION_H__ */
