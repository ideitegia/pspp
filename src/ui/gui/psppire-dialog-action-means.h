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


#ifndef __PSPPIRE_DIALOG_ACTION_MEANS_H__
#define __PSPPIRE_DIALOG_ACTION_MEANS_H__

#include <glib-object.h>
#include <glib.h>

#include "psppire-dialog-action.h"

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_MEANS (psppire_dialog_action_means_get_type ())

#define PSPPIRE_DIALOG_ACTION_MEANS(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_MEANS, PsppireDialogActionMeans))

#define PSPPIRE_DIALOG_ACTION_MEANS_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_MEANS, \
                                 PsppireDialogActionMeansClass))


#define PSPPIRE_IS_DIALOG_ACTION_MEANS(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_MEANS))

#define PSPPIRE_IS_DIALOG_ACTION_MEANS_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_MEANS))


#define PSPPIRE_DIALOG_ACTION_MEANS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_MEANS, \
				   PsppireDialogActionMeansClass))

typedef struct _PsppireDialogActionMeans       PsppireDialogActionMeans;
typedef struct _PsppireDialogActionMeansClass  PsppireDialogActionMeansClass;


struct _PsppireDialogActionMeans
{
  PsppireDialogAction parent;

  /*< private >*/
  GtkWidget *variables;

  GtkWidget *layer;
};


struct _PsppireDialogActionMeansClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_means_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_MEANS_H__ */
