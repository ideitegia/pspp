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


#ifndef __PSPPIRE_DIALOG_ACTION_FACTOR_H__
#define __PSPPIRE_DIALOG_ACTION_FACTOR_H__

#include <glib-object.h>
#include <glib.h>

#include "psppire-dialog-action.h"

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_FACTOR (psppire_dialog_action_factor_get_type ())

#define PSPPIRE_DIALOG_ACTION_FACTOR(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_FACTOR, PsppireDialogActionFactor))

#define PSPPIRE_DIALOG_ACTION_FACTOR_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_FACTOR, \
                                 PsppireDialogActionFactorClass))


#define PSPPIRE_IS_DIALOG_ACTION_FACTOR(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_FACTOR))

#define PSPPIRE_IS_DIALOG_ACTION_FACTOR_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_FACTOR))


#define PSPPIRE_DIALOG_ACTION_FACTOR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_FACTOR, \
				   PsppireDialogActionFactorClass))

typedef struct _PsppireDialogActionFactor       PsppireDialogActionFactor;
typedef struct _PsppireDialogActionFactorClass  PsppireDialogActionFactorClass;


enum rotation_type {
  ROT_NONE,
  ROT_VARIMAX,
  ROT_QUARTIMAX,
  ROT_EQUIMAX
};

struct rotation_parameters
{
  gboolean rotated_solution;
  gint iterations;

  enum rotation_type method;
};

struct extraction_parameters
{
  gdouble mineigen;
  gint n_factors;
  gint n_iterations;

  gboolean explicit_nfactors;  
  gboolean covariance;

  gboolean scree;
  gboolean unrotated;

  gboolean paf;
};


struct _PsppireDialogActionFactor
{
  PsppireDialogAction parent;

  /*< private >*/
  GtkWidget *variables ;

  /* The Extraction subdialog */
  GtkWidget *extraction_dialog;
  GtkWidget *rotation_dialog;

  GtkWidget *n_factors;
  GtkWidget *mineigen;
  GtkWidget *extract_iterations;

  GtkWidget *nfactors_toggle;
  GtkWidget *mineigen_toggle;

  GtkWidget *covariance_toggle;
  GtkWidget *correlation_toggle;

  GtkWidget *scree_button;
  GtkWidget *unrotated_button;

  GtkWidget *extraction_combo;


  /* Rotation Widgets */
  GtkWidget *rotate_iterations;
  GtkWidget *display_rotated_solution;
  GtkWidget *rotation_none;
  GtkWidget *rotation_varimax;
  GtkWidget *rotation_quartimax;
  GtkWidget *rotation_equimax;


  struct extraction_parameters extraction;
  struct rotation_parameters rotation;
};


struct _PsppireDialogActionFactorClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_factor_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_FACTOR_H__ */
