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


#ifndef __PSPPIRE_DIALOG_ACTION_RANK_H__
#define __PSPPIRE_DIALOG_ACTION_RANK_H__

#include <glib-object.h>
#include <glib.h>

#include "psppire-dialog-action.h"

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_RANK (psppire_dialog_action_rank_get_type ())

#define PSPPIRE_DIALOG_ACTION_RANK(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_RANK, PsppireDialogActionRank))

#define PSPPIRE_DIALOG_ACTION_RANK_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_RANK, \
                                 PsppireDialogActionRankClass))


#define PSPPIRE_IS_DIALOG_ACTION_RANK(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_RANK))

#define PSPPIRE_IS_DIALOG_ACTION_RANK_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_RANK))


#define PSPPIRE_DIALOG_ACTION_RANK_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_RANK, \
				   PsppireDialogActionRankClass))

typedef struct _PsppireDialogActionRank       PsppireDialogActionRank;
typedef struct _PsppireDialogActionRankClass  PsppireDialogActionRankClass;


enum RANK_FUNC
  {
    RANK,
    NORMAL,
    PERCENT,
    RFRACTION,
    PROPORTION,
    N,
    NTILES,
    SAVAGE,
    n_RANK_FUNCS
  };

struct _PsppireDialogActionRank
{
  PsppireDialogAction parent;

  /*< private >*/
  GtkWidget *rank_vars;
  GtkWidget *group_vars;

  GtkToggleButton *ascending_togglebutton;
  GtkToggleButton *summary_togglebutton;


  /* Types subdialog widgets */

  GtkWidget *types_dialog;
  GtkWidget *ntiles_entry;

  GtkToggleButton *func_button[n_RANK_FUNCS];
  GtkWidget *formula_box;

  GtkToggleButton *blom;
  GtkToggleButton *tukey;
  GtkToggleButton *rankit;
  GtkToggleButton *vw;

  /* Ties subdialog widgets */

  PsppireDialog *ties_dialog;
  GtkToggleButton *mean;
  GtkToggleButton *low;
  GtkToggleButton *high;
  GtkToggleButton *condense;
};


struct _PsppireDialogActionRankClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_rank_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_RANK_H__ */
