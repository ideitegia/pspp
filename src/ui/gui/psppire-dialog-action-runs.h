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

#ifndef __PSPPIRE_DIALOG_ACTION_RUNS_H__
#define __PSPPIRE_DIALOG_ACTION_RUNS_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_RUNS (psppire_dialog_action_runs_get_type ())

#define PSPPIRE_DIALOG_ACTION_RUNS(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_RUNS, PsppireDialogActionRuns))

#define PSPPIRE_DIALOG_ACTION_RUNS_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_RUNS, \
                                 PsppireDialogActionRunsClass))


#define PSPPIRE_IS_DIALOG_ACTION_RUNS(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_RUNS))

#define PSPPIRE_IS_DIALOG_ACTION_RUNS_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_RUNS))


#define PSPPIRE_DIALOG_ACTION_RUNS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_RUNS, \
				   PsppireDialogActionRunsClass))

typedef struct _PsppireDialogActionRuns       PsppireDialogActionRuns;
typedef struct _PsppireDialogActionRunsClass  PsppireDialogActionRunsClass;


struct _PsppireDialogActionRuns
{
  PsppireDialogAction parent;

  /*< private >*/
  gboolean dispose_has_run ;

  GtkWidget *cb[4];
  GtkWidget *entry;
  GtkWidget *variables;
};


struct _PsppireDialogActionRunsClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_runs_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_RUNS_H__ */
