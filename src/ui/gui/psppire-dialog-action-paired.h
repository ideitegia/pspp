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

#ifndef __PSPPIRE_DIALOG_ACTION_PAIRED_H__
#define __PSPPIRE_DIALOG_ACTION_PAIRED_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_PAIRED (psppire_dialog_action_paired_get_type ())

#define PSPPIRE_DIALOG_ACTION_PAIRED(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_PAIRED, PsppireDialogActionPaired))

#define PSPPIRE_DIALOG_ACTION_PAIRED_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_PAIRED, \
                                 PsppireDialogActionPairedClass))


#define PSPPIRE_IS_DIALOG_ACTION_PAIRED(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_PAIRED))

#define PSPPIRE_IS_DIALOG_ACTION_PAIRED_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_PAIRED))


#define PSPPIRE_DIALOG_ACTION_PAIRED_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_PAIRED, \
				   PsppireDialogActionPairedClass))

typedef struct _PsppireDialogActionPaired       PsppireDialogActionPaired;
typedef struct _PsppireDialogActionPairedClass  PsppireDialogActionPairedClass;


struct tt_options_dialog;

struct _PsppireDialogActionPaired
{
  PsppireDialogAction parent;

  /*< private >*/
  gboolean dispose_has_run;

  GtkWidget *pairs_treeview ;
  GtkListStore *list_store ;
  struct tt_options_dialog *opt;
};




struct _PsppireDialogActionPairedClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_paired_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_PAIRED_H__ */
