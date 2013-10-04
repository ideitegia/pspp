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

#ifndef __PSPPIRE_DIALOG_ACTION_CHISQUARE_H__
#define __PSPPIRE_DIALOG_ACTION_CHISQUARE_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_CHISQUARE (psppire_dialog_action_chisquare_get_type ())

#define PSPPIRE_DIALOG_ACTION_CHISQUARE(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_CHISQUARE, PsppireDialogActionChisquare))

#define PSPPIRE_DIALOG_ACTION_CHISQUARE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_CHISQUARE, \
                                 PsppireDialogActionChisquareClass))


#define PSPPIRE_IS_DIALOG_ACTION_CHISQUARE(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_CHISQUARE))

#define PSPPIRE_IS_DIALOG_ACTION_CHISQUARE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_CHISQUARE))


#define PSPPIRE_DIALOG_ACTION_CHISQUARE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_CHISQUARE, \
				   PsppireDialogActionChisquareClass))

typedef struct _PsppireDialogActionChisquare       PsppireDialogActionChisquare;
typedef struct _PsppireDialogActionChisquareClass  PsppireDialogActionChisquareClass;


struct _PsppireDialogActionChisquare
{
  PsppireDialogAction parent;

  /*< private >*/
  gboolean dispose_has_run ;

  GtkWidget *var_view;

  GtkWidget *button1;
  GtkWidget *button2;

  GtkWidget *range_button;
  GtkWidget *value_lower;
  GtkWidget *value_upper;

  GtkWidget *values_button;

  GtkListStore *expected_list;
};


struct _PsppireDialogActionChisquareClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_chisquare_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_CHISQUARE_H__ */
