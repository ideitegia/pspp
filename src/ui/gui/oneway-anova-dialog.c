/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007  Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <config.h>
#include <glade/glade.h>
#include <gtk/gtk.h>
#include "oneway-anova-dialog.h"
#include "psppire-dict.h"
#include "psppire-var-store.h"
#include "helper.h"
#include "data-editor.h"
#include "psppire-dialog.h"
#include "dialog-common.h"
#include "dict-display.h"
#include "psppire-acr.h"


#include <language/syntax-string-source.h>
#include "syntax-editor.h"


#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct contrasts_subdialog;
struct oneway_anova_dialog;


static void run_contrasts_dialog (struct oneway_anova_dialog *);
static void push_new_store (GArray *, struct contrasts_subdialog *);

static void next (GtkWidget *, gpointer);
static void prev (GtkWidget *, gpointer);


struct contrasts_subdialog
{
  /* Contrasts Dialog widgets */
  GtkWidget *contrasts_dialog;
  GtkWidget *stack_label;
  PsppireAcr *acr;


  /* Gets copied into contrasts when "Continue"
     is clicked */
  GArray *temp_contrasts;

  /* Index into the temp_contrasts */
  guint c;

  GtkWidget *prev;
  GtkWidget *next;

  GtkWidget *ctotal;
};


struct oneway_anova_dialog
{
  PsppireDict *dict;

  GtkWidget *factor_entry;
  GtkWidget *vars_treeview;
  GtkWindow *dialog;
  GArray *contrasts_array;

  GtkToggleButton *descriptives;
  GtkToggleButton *homogeneity;

  struct contrasts_subdialog contrasts;
};

static gboolean
dialog_state_valid (gpointer data)
{
  struct oneway_anova_dialog *ow = data;

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (ow->vars_treeview));

  GtkTreeIter notused;

  if ( !gtk_tree_model_get_iter_first (vars, &notused) )
    return FALSE;

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (ow->factor_entry))))
    return FALSE;

  return TRUE;
}

static gchar * generate_syntax (const struct oneway_anova_dialog *);


static void
refresh (struct oneway_anova_dialog *ow)
{
  GtkTreeModel *model =
    gtk_tree_view_get_model (GTK_TREE_VIEW (ow->vars_treeview));

  gtk_entry_set_text (GTK_ENTRY (ow->factor_entry), "");

  gtk_list_store_clear (GTK_LIST_STORE (model));
}


/* Pops up the dialog box */
void
oneway_anova_dialog (GObject *o, gpointer data)
{
  gint response;
  struct data_editor *de = data;

  PsppireVarStore *vs = NULL;

  GladeXML *xml = XML_NEW ("oneway.glade");

  struct oneway_anova_dialog ow;

  GtkWidget *dict_view =
    get_widget_assert (xml, "oneway-anova-treeview1");

  GtkWidget *selector2 =
    get_widget_assert (xml, "oneway-anova-selector2");

  GtkWidget *selector1 =
    get_widget_assert (xml, "oneway-anova-selector1");

  GtkWidget *contrasts_button =
    get_widget_assert (xml, "contrasts-button");


  g_signal_connect_swapped (contrasts_button, "clicked",
		    G_CALLBACK (run_contrasts_dialog), &ow);


  ow.factor_entry = get_widget_assert (xml, "oneway-anova-entry");
  ow.vars_treeview =
    get_widget_assert (xml, "oneway-anova-treeview2");

  ow.descriptives =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "checkbutton1"));

  ow.homogeneity =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "checkbutton2"));

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  ow.dict = vs->dict;

  ow.dialog =
    GTK_WINDOW (get_widget_assert (xml, "oneway-anova-dialog"));

  gtk_window_set_transient_for (ow.dialog, de->parent.window);

  attach_dictionary_to_treeview (GTK_TREE_VIEW (dict_view),
				 vs->dict,
				 GTK_SELECTION_MULTIPLE, NULL);

  set_dest_model (GTK_TREE_VIEW (ow.vars_treeview), vs->dict);


  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector1),
				 dict_view, ow.vars_treeview,
				 insert_source_row_into_tree_view,
				 NULL,
				 NULL);


  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector2),
				 dict_view, ow.factor_entry,
				 insert_source_row_into_entry,
				 is_currently_in_entry,
				 NULL);



  g_signal_connect_swapped (ow.dialog, "refresh", G_CALLBACK (refresh),  &ow);



  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (ow.dialog),
				      dialog_state_valid, &ow);


  {
    struct contrasts_subdialog *cd = &ow.contrasts;
    GtkEntry *entry = GTK_ENTRY (get_widget_assert (xml, "entry1"));

    cd->acr = PSPPIRE_ACR (get_widget_assert (xml, "psppire-acr1"));
    cd->contrasts_dialog = get_widget_assert (xml, "contrasts-dialog");

    cd->next = get_widget_assert (xml, "next-button");
    cd->prev = get_widget_assert (xml, "prev-button");
    cd->ctotal = get_widget_assert (xml, "entry2");

    cd->stack_label = get_widget_assert (xml, "contrast-stack-label");

    /* Contrasts */
    ow.contrasts_array = g_array_new (FALSE, FALSE, sizeof (GtkListStore *));

    g_signal_connect (cd->next, "clicked", G_CALLBACK (next), cd);
    g_signal_connect (cd->prev, "clicked", G_CALLBACK (prev), cd);

    psppire_acr_set_entry (cd->acr, entry);

    gtk_window_set_transient_for (GTK_WINDOW (cd->contrasts_dialog),
				  de->parent.window);
  }

  response = psppire_dialog_run (PSPPIRE_DIALOG (ow.dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&ow);
	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&ow);

	struct syntax_editor *se =
	  (struct syntax_editor *) window_create (WINDOW_SYNTAX, NULL);

	gtk_text_buffer_insert_at_cursor (se->buffer, syntax, -1);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_array_free (ow.contrasts_array, FALSE);

  g_object_unref (xml);
}


static gchar * generate_syntax (const struct oneway_anova_dialog *ow)
{
  gchar *text;
  gint i;
  gboolean descriptives = gtk_toggle_button_get_active (ow->descriptives);
  gboolean homogeneity = gtk_toggle_button_get_active (ow->homogeneity);

  GString *str = g_string_new ("ONEWAY /VARIABLES=");

  append_variable_names (str, ow->dict, GTK_TREE_VIEW (ow->vars_treeview), 0);

  g_string_append (str, " BY ");

  g_string_append (str, gtk_entry_get_text (GTK_ENTRY (ow->factor_entry)));

  if (descriptives || homogeneity )
    {
      g_string_append (str, "\n\t/STATISTICS=");
      if (descriptives)
	g_string_append (str, "DESCRIPTIVES ");
      if (homogeneity)
	g_string_append (str, "HOMOGENEITY ");
    }

  for (i = 0 ; i < ow->contrasts_array->len ; ++i )
    {
      GtkListStore *ls = g_array_index (ow->contrasts_array, GtkListStore*, i);
      GtkTreeIter iter;
      gboolean ok;

      g_string_append (str, "\n\t/CONTRAST=");

      for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(ls),
					       &iter);
 	   ok;
	   ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (ls), &iter))
	{
	  gdouble v;

	  gtk_tree_model_get (GTK_TREE_MODEL (ls), &iter, 0, &v, -1);

	  g_string_append_printf (str, " %g", v);
	}
    }

  g_string_append (str, ".\n");

  text = str->str;
  g_string_free (str, FALSE);

  return text;
}



/* Contrasts stuff */




/* Callback for when the list store currently associated with the
   treeview has changed.  It sets the widgets of the subdialog
   to reflect the store's new state.
*/
static void
list_store_changed (struct contrasts_subdialog *csd)
{
  gboolean ok;
  gdouble total = 0.0;
  GtkTreeIter iter;
  GtkTreeModel *ls = NULL;
  gchar *text =
    g_strdup_printf (_("Contrast %d of %d"),
		     csd->c, csd->temp_contrasts->len);

  gtk_label_set_label (GTK_LABEL (csd->stack_label), text);

  g_free (text);

  gtk_widget_set_sensitive (csd->prev, csd->c > 1);

  if ( csd->c > 0 )
    ls = g_array_index (csd->temp_contrasts, GtkTreeModel*, csd->c - 1);

  psppire_acr_set_model (csd->acr, GTK_LIST_STORE (ls));

  /* Sensitive iff the liststore has two items or more */
  gtk_widget_set_sensitive (csd->next,
			    gtk_tree_model_iter_nth_child
			    (ls, &iter,  NULL, 1));

  for (ok = gtk_tree_model_get_iter_first (ls, &iter);
       ok;
       ok = gtk_tree_model_iter_next (ls, &iter)
       )
    {
      gdouble v;
      gtk_tree_model_get (ls, &iter, 0, &v, -1);
      total += v;
    }

  text = g_strdup_printf ("%g", total);

  gtk_entry_set_text (GTK_ENTRY (csd->ctotal), text);

  g_free (text);
}



/* Copy the contrasts array into the local array */
static GArray *
clone_contrasts_array (GArray *src_array)
{
  gint i;

  GArray *dest_array =
    g_array_sized_new (FALSE, FALSE, sizeof (GtkListStore *),
		       src_array->len);

  for (i = 0 ; i < src_array->len ; ++i )
    {

      GtkTreeIter src_iter;
      GtkListStore *src = g_array_index (src_array, GtkListStore*, i);
      GtkListStore *dest;

      /* Refuse to copy empty stores */
      if (! gtk_tree_model_get_iter_first (GTK_TREE_MODEL (src),
					   &src_iter))
	continue;

      dest = clone_list_store (src);

      g_array_append_val (dest_array, dest);
    }

  return dest_array;
}


static void
run_contrasts_dialog (struct oneway_anova_dialog *ow)
{
  struct contrasts_subdialog *csd = &ow->contrasts;
  gint response;

  csd->temp_contrasts = clone_contrasts_array (ow->contrasts_array);

  csd->c = 1;

  push_new_store (csd->temp_contrasts, csd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (csd->contrasts_dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      ow->contrasts_array = clone_contrasts_array (csd->temp_contrasts);
    }

  /* Destroy the temp contrasts here */

}


static void
push_new_store (GArray *contrast_stack, struct contrasts_subdialog *csd)
{
  GtkListStore *ls = gtk_list_store_new (1, G_TYPE_DOUBLE);

  g_array_append_val (contrast_stack, ls);

  g_signal_connect_swapped (ls, "row-deleted",
			    G_CALLBACK (list_store_changed), csd);

  g_signal_connect_swapped (ls, "row-changed",
			    G_CALLBACK (list_store_changed), csd);

  list_store_changed (csd);
}

static void
next (GtkWidget *widget, gpointer data)
{
  struct contrasts_subdialog *csd = data;

  if (csd->c >= csd->temp_contrasts->len)
    push_new_store (csd->temp_contrasts, csd);

  csd->c++;

  list_store_changed (csd);
}


static void
prev (GtkWidget *widget, gpointer data)
{
  struct contrasts_subdialog *csd = data;

  if ( csd->c > 0 )
    --csd->c;

  list_store_changed (csd);
}


