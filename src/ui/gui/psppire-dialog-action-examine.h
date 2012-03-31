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


#ifndef __PSPPIRE_DIALOG_ACTION_EXAMINE_H__
#define __PSPPIRE_DIALOG_ACTION_EXAMINE_H__

#include <glib-object.h>
#include <glib.h>

#include "psppire-dialog-action.h"

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE (psppire_dialog_action_examine_get_type ())

#define PSPPIRE_DIALOG_ACTION_EXAMINE(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE, PsppireDialogActionExamine))

#define PSPPIRE_DIALOG_ACTION_EXAMINE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE, \
                                 PsppireDialogActionExamineClass))


#define PSPPIRE_IS_DIALOG_ACTION_EXAMINE(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE))

#define PSPPIRE_IS_DIALOG_ACTION_EXAMINE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE))


#define PSPPIRE_DIALOG_ACTION_EXAMINE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE, \
				   PsppireDialogActionExamineClass))

typedef struct _PsppireDialogActionExamine       PsppireDialogActionExamine;
typedef struct _PsppireDialogActionExamineClass  PsppireDialogActionExamineClass;


enum PsppireDialogActionExamineOpts
  {
    OPT_LISTWISE,
    OPT_PAIRWISE,
    OPT_REPORT
  };

struct _PsppireDialogActionExamine
{
  PsppireDialogAction parent;

  /*< private >*/
  GtkWidget *variables;
  GtkWidget *factors;
  GtkWidget *id_var;

  /* The stats dialog */
  GtkWidget *stats_dialog;
  GtkWidget *descriptives_button;
  GtkWidget *extremes_button;
  GtkWidget *percentiles_button;
  guint stats;

  /* The options dialog */
  GtkWidget *opts_dialog;
  GtkWidget *listwise;
  GtkWidget *pairwise;
  GtkWidget *report;
  enum PsppireDialogActionExamineOpts opts;
};


struct _PsppireDialogActionExamineClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_examine_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_EXAMINE_H__ */
