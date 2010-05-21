/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009  Free Software Foundation

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

#include "dialog-common.h"
#include <language/syntax-string-source.h>
#include <ui/syntax-gen.h>
#include <libpspp/str.h>

#include "factor-dialog.h"
#include "psppire-selector.h"
#include "psppire-dictview.h"
#include "psppire-dialog.h"

#include "psppire-data-window.h"
#include "psppire-var-view.h"

#include "widget-io.h"

#include "executor.h"
#include "helper.h"

#include <gtk/gtk.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


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

enum rotation_type {
  ROT_NONE,
  ROT_VARIMAX,
  ROT_QUARTIMAX,
  ROT_EQUIMAX
};

static const char *rot_method_syntax[] = 
  {
    "NOROTATE",
    "VARIMAX",
    "QUARTIMAX",
    "EQUAMAX"
  };

struct rotation_parameters
{
  gboolean rotated_solution;
  gint iterations;

  enum rotation_type method;
};



static const struct extraction_parameters default_extraction_parameters = {1.0, 0, 25, FALSE, TRUE, FALSE, TRUE, FALSE};

static const struct rotation_parameters default_rotation_parameters = {TRUE, 25, ROT_VARIMAX};

struct factor
{
  GtkBuilder *xml;
  PsppireDict *dict;

  GtkWidget *variables;
  PsppireDataWindow *de ;

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

static void
load_rotation_parameters (struct factor *fd, const struct rotation_parameters *p)
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
load_extraction_parameters (struct factor *fd, const struct extraction_parameters *p)
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

static void
set_rotation_parameters (const struct factor *fd, struct rotation_parameters *p)
{
  p->iterations = gtk_spin_button_get_value (GTK_SPIN_BUTTON (fd->rotate_iterations));
  p->rotated_solution = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->display_rotated_solution));


  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->rotation_none)))
    p->method = ROT_NONE;

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->rotation_varimax)))
    p->method = ROT_VARIMAX;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->rotation_quartimax)))
    p->method = ROT_QUARTIMAX;

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->rotation_equimax)))
    p->method = ROT_EQUIMAX;
}

static void
set_extraction_parameters (const struct factor *fd, struct extraction_parameters *p)
{
  p->mineigen = gtk_spin_button_get_value (GTK_SPIN_BUTTON (fd->mineigen));
  p->n_factors = gtk_spin_button_get_value (GTK_SPIN_BUTTON (fd->n_factors));
  p->n_iterations = gtk_spin_button_get_value (GTK_SPIN_BUTTON (fd->extract_iterations));

  p->explicit_nfactors = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->nfactors_toggle));
  p->covariance = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->covariance_toggle));

  p->scree = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->scree_button));
  p->unrotated = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->unrotated_button));

  p->paf = (gtk_combo_box_get_active (GTK_COMBO_BOX (fd->extraction_combo)) == 1);
}

static void
run_extractions_subdialog (struct factor *fd)
{
  struct extraction_parameters *ex = &fd->extraction;

  gint response = psppire_dialog_run (PSPPIRE_DIALOG (fd->extraction_dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      /* Set the parameters from their respective widgets */
      set_extraction_parameters (fd, ex);
    }
  else
    {
      /* Cancelled.  Reset the widgets to their old state */
      load_extraction_parameters (fd, ex);
    }
}

static void
run_rotations_subdialog (struct factor *fd)
{
  struct rotation_parameters *rot = &fd->rotation;

  gint response = psppire_dialog_run (PSPPIRE_DIALOG (fd->rotation_dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      /* Set the parameters from their respective widgets */
      set_rotation_parameters (fd, rot);
    }
  else
    {
      /* Cancelled.  Reset the widgets to their old state */
      load_rotation_parameters (fd, rot);
    }
}


static char * generate_syntax (const struct factor *rd);


static void
refresh (struct factor *fd)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  load_extraction_parameters (fd, &default_extraction_parameters);
  load_rotation_parameters (fd, &default_rotation_parameters);
}


static gboolean
dialog_state_valid (gpointer data)
{
  struct factor *fd = data;

  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));

  if  (gtk_tree_model_iter_n_children (liststore, NULL) < 2)
    return FALSE;

  return TRUE;
}

static void
on_show (struct factor *fd, GtkWidget *dialog)
{
  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));

  gint n_vars = gtk_tree_model_iter_n_children (liststore, NULL);

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (fd->n_factors), 1, n_vars - 1);
}


static void
on_extract_toggle (GtkToggleButton *button, struct factor *f)
{
  gboolean active = gtk_toggle_button_get_active (button);

  gtk_widget_set_sensitive (GTK_WIDGET (f->n_factors), active);
  gtk_widget_set_sensitive (GTK_WIDGET (f->mineigen), ! active);
}

/* Pops up the Factor dialog box */
void
factor_dialog (PsppireDataWindow *dw)
{
  struct factor fd;
  gint response;

  PsppireVarStore *vs;

  GtkWidget *dialog ;
  GtkWidget *source ;
  GtkWidget *extraction_button ;
  GtkWidget *rotation_button ;

  fd.xml = builder_new ("factor.ui");

  fd.extraction = default_extraction_parameters;
  fd.rotation = default_rotation_parameters;
  
  dialog = get_widget_assert   (fd.xml, "factor-dialog");
  source = get_widget_assert   (fd.xml, "dict-view");
  extraction_button = get_widget_assert (fd.xml, "button-extractions");
  rotation_button = get_widget_assert (fd.xml, "button-rotations");

  fd.extraction_dialog = get_widget_assert (fd.xml, "extractions-dialog");
  fd.rotation_dialog = get_widget_assert (fd.xml, "rotations-dialog");

  fd.de = dw;

  g_signal_connect_swapped (dialog, "refresh", G_CALLBACK (refresh),  &fd);

  {
    GtkWidget *hbox = get_widget_assert (fd.xml, "hbox6");
    GtkWidget *eigenvalue_extraction = widget_scanf (_("Eigenvalues over %4.2f times the mean eigenvalue"), &fd.mineigen);

    fd.nfactors_toggle = get_widget_assert (fd.xml, "nfactors-radiobutton");
    fd.mineigen_toggle = get_widget_assert (fd.xml, "mineigen-radiobutton");
    fd.n_factors = get_widget_assert (fd.xml, "spinbutton-nfactors");
    fd.extract_iterations = get_widget_assert (fd.xml, "spinbutton-extract-iterations");
    fd.covariance_toggle = get_widget_assert (fd.xml,  "covariance-radiobutton");
    fd.correlation_toggle = get_widget_assert (fd.xml, "correlations-radiobutton");

    fd.scree_button = get_widget_assert (fd.xml, "scree-button");
    fd.unrotated_button = get_widget_assert (fd.xml, "unrotated-button");
    fd.extraction_combo = get_widget_assert (fd.xml, "combobox1");

    gtk_container_add (GTK_CONTAINER (hbox), eigenvalue_extraction);

    g_signal_connect (fd.nfactors_toggle, "toggled", G_CALLBACK (on_extract_toggle), &fd);

    gtk_widget_show_all (eigenvalue_extraction);
  }

  {
    fd.rotate_iterations = get_widget_assert (fd.xml, "spinbutton-rot-iterations");

    fd.display_rotated_solution = get_widget_assert (fd.xml, "checkbutton-rotated-solution");

    fd.rotation_none      = get_widget_assert (fd.xml, "radiobutton-none");
    fd.rotation_varimax   = get_widget_assert (fd.xml, "radiobutton-varimax");
    fd.rotation_quartimax = get_widget_assert (fd.xml, "radiobutton-quartimax");
    fd.rotation_equimax   = get_widget_assert (fd.xml, "radiobutton-equimax");
  }

  g_signal_connect_swapped (extraction_button, "clicked", G_CALLBACK (run_extractions_subdialog), &fd);
  g_signal_connect_swapped (rotation_button, "clicked", G_CALLBACK (run_rotations_subdialog), &fd);

  g_signal_connect_swapped (fd.extraction_dialog, "show", G_CALLBACK (on_show), &fd);

  fd.variables = get_widget_assert   (fd.xml, "psppire-var-view1");

  g_object_get (fd.de->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fd.de));
  gtk_window_set_transient_for (GTK_WINDOW (fd.extraction_dialog), GTK_WINDOW (fd.de));
  gtk_window_set_transient_for (GTK_WINDOW (fd.rotation_dialog), GTK_WINDOW (fd.de));

  g_object_get (vs, "dictionary", &fd.dict, NULL);
  g_object_set (source, "model", fd.dict, NULL);


  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &fd);

  psppire_selector_set_allow (PSPPIRE_SELECTOR (get_widget_assert (fd.xml, "dep-selector")),
			      numeric_only);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&fd);

	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&fd);
        paste_syntax_to_window (syntax);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (fd.xml);
}




static char *
generate_syntax (const struct factor *rd)
{
  gchar *text;

  GString *string = g_string_new ("FACTOR ");

  g_string_append (string, "VARIABLES =  ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->variables), 0, string);


  g_string_append (string, "\n\t/CRITERIA = ");
  if ( rd->extraction.explicit_nfactors )
    g_string_append_printf (string, "FACTORS (%d)", rd->extraction.n_factors);
  else
    g_string_append_printf (string, "MINEIGEN (%g)", rd->extraction.mineigen);

  /*
    The CRITERIA = ITERATE subcommand is overloaded.
     It applies to the next /ROTATION and/or EXTRACTION command whatever comes first.
  */
  g_string_append_printf (string, " ITERATE (%d)", rd->extraction.n_iterations);


  g_string_append (string, "\n\t/EXTRACTION =");
  if ( rd->extraction.paf)
    g_string_append (string, "PAF");
  else
    g_string_append (string, "PC");




  g_string_append (string, "\n\t/METHOD = ");
  if ( rd->extraction.covariance )
    g_string_append (string, "COVARIANCE");
  else
    g_string_append (string, "CORRELATION");



  if ( rd->extraction.scree )
    {
      g_string_append (string, "\n\t/PLOT = ");
      g_string_append (string, "EIGEN");
    }

  g_string_append (string, "\n\t/PRINT = ");
  g_string_append (string, "INITIAL ");

  if ( rd->extraction.unrotated )  
    g_string_append (string, "EXTRACTION ");

  if ( rd->rotation.rotated_solution )
    g_string_append (string, "ROTATION");


  /* The CRITERIA = ITERATE subcommand is overloaded.
     It applies to the next /ROTATION and/or EXTRACTION command whatever comes first.
  */
  g_string_append_printf (string, "\n\t/CRITERIA = ITERATE (%d)",  rd->rotation.iterations  );

  g_string_append (string, "\n\t/ROTATION = ");
  g_string_append (string, rot_method_syntax[rd->rotation.method]);


  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}
