/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009, 2010, 2011, 2012, 2014  Free Software Foundation

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


#include <config.h>

#include "psppire-dialog-action-factor.h"

#include <float.h>

#include "psppire-var-view.h"
#include "dialog-common.h"
#include "psppire-selector.h"
#include "psppire-dict.h"
#include "psppire-dialog.h"
#include "builder-wrapper.h"
#include "psppire-scanf.h"
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dialog_action_factor_class_init      (PsppireDialogActionFactorClass *class);

G_DEFINE_TYPE (PsppireDialogActionFactor, psppire_dialog_action_factor, PSPPIRE_TYPE_DIALOG_ACTION);

static const char *rot_method_syntax[] = 
  {
    "NOROTATE",
    "VARIMAX",
    "QUARTIMAX",
    "EQUAMAX"
  };

static void
on_extract_toggle (GtkToggleButton *button, PsppireDialogActionFactor *f)
{
  gboolean active = gtk_toggle_button_get_active (button);

  gtk_widget_set_sensitive (GTK_WIDGET (f->n_factors), active);
  gtk_widget_set_sensitive (GTK_WIDGET (f->mineigen), ! active);
}

static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionFactor *rd  = PSPPIRE_DIALOG_ACTION_FACTOR (act);

  gchar *text = NULL;
  struct string str;
  ds_init_cstr (&str, "FACTOR ");

  ds_put_cstr (&str, "\n\t/VARIABLES=");

  psppire_var_view_append_names_str (PSPPIRE_VAR_VIEW (rd->variables), 0, &str);


  ds_put_cstr (&str, "\n\t/CRITERIA = ");
  if ( rd->extraction.explicit_nfactors )
    ds_put_c_format (&str, "FACTORS (%d)", rd->extraction.n_factors);
  else
    ds_put_c_format (&str, "MINEIGEN (%.*g)",
                     DBL_DIG + 1, rd->extraction.mineigen);

  /*
    The CRITERIA = ITERATE subcommand is overloaded.
     It applies to the next /ROTATION and/or EXTRACTION command whatever comes first.
  */
  ds_put_c_format (&str, " ITERATE (%d)", rd->extraction.n_iterations);


  ds_put_cstr (&str, "\n\t/EXTRACTION =");
  if ( rd->extraction.paf)
    ds_put_cstr (&str, "PAF");
  else
    ds_put_cstr (&str, "PC");


  ds_put_cstr (&str, "\n\t/METHOD = ");
  if ( rd->extraction.covariance )
    ds_put_cstr (&str, "COVARIANCE");
  else
    ds_put_cstr (&str, "CORRELATION");

  if ( rd->extraction.scree )
    {
      ds_put_cstr (&str, "\n\t/PLOT = ");
      ds_put_cstr (&str, "EIGEN");
    }

  ds_put_cstr (&str, "\n\t/PRINT = ");
  ds_put_cstr (&str, "INITIAL ");

  if ( rd->extraction.unrotated )  
    ds_put_cstr (&str, "EXTRACTION ");

  if ( rd->rotation.rotated_solution )
    ds_put_cstr (&str, "ROTATION");


  /* The CRITERIA = ITERATE subcommand is overloaded.
     It applies to the next /ROTATION and/or EXTRACTION command whatever comes first.
  */
  ds_put_c_format (&str, "\n\t/CRITERIA = ITERATE (%d)",  rd->rotation.iterations  );

  ds_put_cstr (&str, "\n\t/ROTATION = ");
  ds_put_cstr (&str, rot_method_syntax[rd->rotation.method]);

  ds_put_cstr (&str, ".");
  text = ds_steal_cstr (&str);

  ds_destroy (&str);

  return text;
}

static void
load_rotation_parameters (PsppireDialogActionFactor *fd, const struct rotation_parameters *p)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->display_rotated_solution),
				p->rotated_solution);
  
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (fd->rotate_iterations),
			     p->iterations);

  switch (p->method)
    {
    case ROT_NONE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->rotation_none), TRUE);
      break;
    case ROT_VARIMAX:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->rotation_varimax), TRUE);
      break;
    case ROT_QUARTIMAX:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->rotation_quartimax), TRUE);
      break;
    case ROT_EQUIMAX:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->rotation_equimax), TRUE);
      break;
    default:
      g_assert_not_reached ();
      break;
    }
}

static void
load_extraction_parameters (PsppireDialogActionFactor *fd, const struct extraction_parameters *p)
{
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (fd->mineigen), p->mineigen);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (fd->n_factors), p->n_factors);

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (fd->extract_iterations), p->n_iterations);


  if (p->explicit_nfactors)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->nfactors_toggle), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->mineigen_toggle), TRUE);

  if (p->covariance)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->covariance_toggle), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->correlation_toggle), TRUE);


  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->scree_button), p->scree);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->unrotated_button), p->unrotated);

  if ( p->paf )
    gtk_combo_box_set_active (GTK_COMBO_BOX (fd->extraction_combo), 1);
  else
    gtk_combo_box_set_active (GTK_COMBO_BOX (fd->extraction_combo), 0);
    
}

static gboolean
dialog_state_valid (PsppireDialogActionFactor *da)
{
  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (da->variables));

  if  (gtk_tree_model_iter_n_children (liststore, NULL) < 2)
    return FALSE;

  return TRUE;
}

static const struct extraction_parameters default_extraction_parameters =
  {1.0, 0, 25, FALSE, TRUE, FALSE, TRUE, FALSE};

static const struct rotation_parameters default_rotation_parameters =
  {TRUE, 25, ROT_VARIMAX};

static void
dialog_refresh (PsppireDialogAction *da)
{
  PsppireDialogActionFactor *fd  = PSPPIRE_DIALOG_ACTION_FACTOR (da);
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  load_extraction_parameters (fd, &default_extraction_parameters);
  load_rotation_parameters (fd, &default_rotation_parameters);
}



static void
set_rotation_parameters (PsppireDialogActionFactor *act, struct rotation_parameters *p)
{
  p->iterations = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->rotate_iterations));
  p->rotated_solution = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->display_rotated_solution));


  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->rotation_none)))
    p->method = ROT_NONE;

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->rotation_varimax)))
    p->method = ROT_VARIMAX;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->rotation_quartimax)))
    p->method = ROT_QUARTIMAX;

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->rotation_equimax)))
    p->method = ROT_EQUIMAX;
}

static void
set_extraction_parameters (PsppireDialogActionFactor *act, struct extraction_parameters *p)
{
  p->mineigen = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->mineigen));
  p->n_factors = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->n_factors));
  p->n_iterations = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->extract_iterations));

  p->explicit_nfactors = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->nfactors_toggle));
  p->covariance = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->covariance_toggle));

  p->scree = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->scree_button));
  p->unrotated = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->unrotated_button));

  p->paf = (gtk_combo_box_get_active (GTK_COMBO_BOX (act->extraction_combo)) == 1);
}


static void
run_extractions_subdialog (PsppireDialogActionFactor *act)
{
  struct extraction_parameters *ex = &act->extraction;

  gint response = psppire_dialog_run (PSPPIRE_DIALOG (act->extraction_dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      /* Set the parameters from their respective widgets */
      set_extraction_parameters (act, ex);
    }
  else
    {
      /* Cancelled.  Reset the widgets to their old state */
      load_extraction_parameters (act, ex);
    }
}

static void
run_rotations_subdialog (PsppireDialogActionFactor *act)
{
  struct rotation_parameters *rot = &act->rotation;

  gint response = psppire_dialog_run (PSPPIRE_DIALOG (act->rotation_dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      /* Set the parameters from their respective widgets */
      set_rotation_parameters (act, rot);
    }
  else
    {
      /* Cancelled.  Reset the widgets to their old state */
      load_rotation_parameters (act, rot);
    }
}

static void
psppire_dialog_action_factor_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionFactor *act = PSPPIRE_DIALOG_ACTION_FACTOR (a);
  GtkWidget *extraction_button ;
  GtkWidget *rotation_button ;

  GtkBuilder *xml = builder_new ("factor.ui");

  pda->dialog = get_widget_assert   (xml, "factor-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  extraction_button = get_widget_assert (xml, "button-extractions");
  rotation_button = get_widget_assert (xml, "button-rotations");

  act->extraction_dialog = get_widget_assert (xml, "extractions-dialog");
  act->rotation_dialog = get_widget_assert (xml, "rotations-dialog");

  act->variables = get_widget_assert   (xml, "psppire-var-view1");

  {
    GtkWidget *hbox = get_widget_assert (xml, "hbox6");
    GtkWidget *eigenvalue_extraction ;

    act->mineigen_toggle = get_widget_assert (xml, "mineigen-radiobutton");

    eigenvalue_extraction = psppire_scanf_new (_("_Eigenvalues over %4.2f times the mean eigenvalue"), &act->mineigen);

    g_object_set (eigenvalue_extraction,
		  "use-underline", TRUE,
		  "mnemonic-widget", act->mineigen_toggle,
		  NULL);

    act->nfactors_toggle = get_widget_assert (xml, "nfactors-radiobutton");
    act->n_factors = get_widget_assert (xml, "spinbutton-nfactors");
    act->extract_iterations = get_widget_assert (xml, "spinbutton-extract-iterations");
    act->covariance_toggle = get_widget_assert (xml,  "covariance-radiobutton");
    act->correlation_toggle = get_widget_assert (xml, "correlations-radiobutton");

    act->scree_button = get_widget_assert (xml, "scree-button");
    act->unrotated_button = get_widget_assert (xml, "unrotated-button");
    act->extraction_combo = get_widget_assert (xml, "combobox1");

    gtk_container_add (GTK_CONTAINER (hbox), eigenvalue_extraction);

    g_signal_connect (act->nfactors_toggle, "toggled", G_CALLBACK (on_extract_toggle), act);

    gtk_widget_show_all (eigenvalue_extraction);
  }

  {
    act->rotate_iterations = get_widget_assert (xml, "spinbutton-rot-iterations");

    act->display_rotated_solution = get_widget_assert (xml, "checkbutton-rotated-solution");

    act->rotation_none      = get_widget_assert (xml, "radiobutton-none");
    act->rotation_varimax   = get_widget_assert (xml, "radiobutton-varimax");
    act->rotation_quartimax = get_widget_assert (xml, "radiobutton-quartimax");
    act->rotation_equimax   = get_widget_assert (xml, "radiobutton-equimax");
  }

  g_signal_connect_swapped (extraction_button, "clicked",
			    G_CALLBACK (run_extractions_subdialog), act);
  g_signal_connect_swapped (rotation_button, "clicked", G_CALLBACK (run_rotations_subdialog), act);


  psppire_dialog_action_set_valid_predicate (pda, (void *) dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, dialog_refresh);

  PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_factor_parent_class)->activate (pda);
  
  g_object_unref (xml);
}

static void
psppire_dialog_action_factor_class_init (PsppireDialogActionFactorClass *class)
{
  GTK_ACTION_CLASS (class)->activate = psppire_dialog_action_factor_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}

static void
psppire_dialog_action_factor_init (PsppireDialogActionFactor *act)
{
}
