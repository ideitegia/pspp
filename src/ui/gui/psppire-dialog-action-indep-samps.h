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

#ifndef __PSPPIRE_DIALOG_ACTION_INDEP_SAMPS_H__
#define __PSPPIRE_DIALOG_ACTION_INDEP_SAMPS_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_INDEP_SAMPS (psppire_dialog_action_indep_samps_get_type ())

#define PSPPIRE_DIALOG_ACTION_INDEP_SAMPS(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_INDEP_SAMPS, PsppireDialogActionIndepSamps))

#define PSPPIRE_DIALOG_ACTION_INDEP_SAMPS_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_INDEP_SAMPS, \
                                 PsppireDialogActionIndepSampsClass))


#define PSPPIRE_IS_DIALOG_ACTION_INDEP_SAMPS(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_INDEP_SAMPS))

#define PSPPIRE_IS_DIALOG_ACTION_INDEP_SAMPS_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_INDEP_SAMPS))


#define PSPPIRE_DIALOG_ACTION_INDEP_SAMPS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_INDEP_SAMPS, \
				   PsppireDialogActionIndepSampsClass))

typedef struct _PsppireDialogActionIndepSamps       PsppireDialogActionIndepSamps;
typedef struct _PsppireDialogActionIndepSampsClass  PsppireDialogActionIndepSampsClass;


enum group_definition
  {
    GROUPS_UNDEF,
    GROUPS_VALUES,
    GROUPS_CUT_POINT
  };

struct tt_options_dialog;

struct _PsppireDialogActionIndepSamps
{
  PsppireDialogAction parent;

  /*< private >*/
  gboolean dispose_has_run ;

  GtkWidget *test_vars_tv;
  GtkWidget *def_grps_dialog;
  GtkWidget *define_groups_button;
  GtkWidget *options_button;

  /* The variable which determines to which group a datum belongs */
  const struct variable *grp_var;

  /* The GtkEntry which holds the reference to the above variable */
  GtkWidget *group_var_entry;

  /* The define groups subdialog */
  GtkWidget *dg_dialog;
  GtkWidget *dg_label;
  GtkWidget *dg_table1;
  GtkWidget *dg_table2;
  GtkWidget *dg_hbox1;
  GtkWidget *dg_box;

  GtkWidget *dg_values_toggle_button;
  GtkWidget *dg_cut_point_toggle_button;

  GtkWidget *dg_grp_entry[2];
  GtkWidget *dg_cut_point_entry;

  enum group_definition group_defn;

  union value grp_val[2];
  union value cut_point;

  /* The options dialog */
  struct tt_options_dialog *opts;
};


struct _PsppireDialogActionIndepSampsClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_indep_samps_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_INDEP_SAMPS_H__ */
