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

/* 
   This is a an abstract base class, deriving from GtkAction.
   It's purpose is to abstract the way in which dialog boxes behave.
   That is, this action will fire whenever a dialog box is to be 
   popped up.
   
   Additionally, most dialog boxes generate syntax to 
   be run by the pspp back-end.  This provides an abstraction
   to do that.  The programmer needs only to provide the function
   to generate the syntax.  This base class looks after the rest.
*/

#ifndef __PSPPIRE_DIALOG_ACTION_H__
#define __PSPPIRE_DIALOG_ACTION_H__

#include <glib-object.h>
#include <glib.h>

#include "psppire-dict.h"
#include "psppire-dialog.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION (psppire_dialog_action_get_type ())

#define PSPPIRE_DIALOG_ACTION(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION, PsppireDialogAction))

#define PSPPIRE_DIALOG_ACTION_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION, \
                                 PsppireDialogActionClass))

#define PSPPIRE_IS_DIALOG_ACTION(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION))

#define PSPPIRE_IS_DIALOG_ACTION_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION))


#define PSPPIRE_DIALOG_ACTION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION, \
				   PsppireDialogActionClass))

typedef struct _PsppireDialogAction       PsppireDialogAction;
typedef struct _PsppireDialogActionClass  PsppireDialogActionClass;


struct _PsppireDialogAction
{
  GtkAction parent;

  /*< private >*/
  GtkUIManager *uim;

  GtkWidget *source;
  GtkWidget *dialog;

  GtkWidget *toplevel;
  PsppireDict *dict;
};

struct _PsppireDialogActionClass
{
  GtkActionClass parent_class;
  void   (*activate) (PsppireDialogAction *);
  char * (*generate_syntax) (PsppireDialogAction *);
};

GType psppire_dialog_action_get_type (void) ;

typedef void (*PsppireDialogActionRefresh) (PsppireDialogAction *) ;

void psppire_dialog_action_set_refresh (PsppireDialogAction *pda, 
					PsppireDialogActionRefresh refresh);

void psppire_dialog_action_set_valid_predicate (PsppireDialogAction *act, 
						ContentsAreValid dialog_state_valid);

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_H__ */
