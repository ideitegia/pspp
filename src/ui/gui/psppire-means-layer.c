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

#include <config.h>

#include "psppire-means-layer.h"
#include "psppire-selector.h"
#include "psppire-var-view.h"

#include <gtk/gtk.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void psppire_means_layer_class_init    (PsppireMeansLayerClass *class);
static void psppire_means_layer_init          (PsppireMeansLayer      *window);

G_DEFINE_TYPE (PsppireMeansLayer, psppire_means_layer, GTK_TYPE_VBOX);

static GObjectClass *parent_class = NULL;

static void
psppire_means_layer_dispose (GObject *obj)
{
  PsppireMeansLayer *w = (PsppireMeansLayer *)obj;

  if (w->dispose_has_run)
    return;

  /* Make sure dispose does not run twice. */
  w->dispose_has_run = TRUE;

  g_ptr_array_unref (w->layer);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}


static void 
psppire_means_layer_class_init    (PsppireMeansLayerClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  parent_class = g_type_class_peek_parent (class);


  object_class->dispose = psppire_means_layer_dispose;
}

static void
update (PsppireMeansLayer *ml)
{
  gchar *l;

  if (!gtk_widget_get_realized (GTK_WIDGET (ml)))
    return;

  l = g_strdup_printf (_("Layer %d of %d"),
			      ml->current_layer + 1, ml->n_layers);
  
  gtk_label_set_text (GTK_LABEL (ml->label), l);
  g_free (l);

  gtk_widget_set_sensitive (ml->back, ml->current_layer > 0);
  gtk_widget_set_sensitive (ml->forward,
			    psppire_var_view_get_iter_first (PSPPIRE_VAR_VIEW (ml->var_view),
							     NULL));
}

static void
on_forward (PsppireMeansLayer *ml)
{
  ml->current_layer++;
  if (ml->current_layer >= ml->n_layers)
    {
      GtkTreeModel *tm;
      psppire_var_view_clear (PSPPIRE_VAR_VIEW (ml->var_view));
      tm = gtk_tree_view_get_model (GTK_TREE_VIEW (ml->var_view));
      g_ptr_array_add (ml->layer, tm);
      g_object_ref (tm);
      ml->n_layers = ml->current_layer + 1;      
    }
  else
    {
      GtkTreeModel *tm = g_ptr_array_index (ml->layer, ml->current_layer);
      gtk_tree_view_set_model (GTK_TREE_VIEW (ml->var_view), tm);
    }
}

static void
on_back (PsppireMeansLayer *ml)
{
  GtkTreeModel *tm;
  g_return_if_fail (ml->current_layer > 0);
  ml->current_layer--;

  tm = g_ptr_array_index (ml->layer, ml->current_layer);
  gtk_tree_view_set_model (GTK_TREE_VIEW (ml->var_view), tm);
}


static void 
psppire_means_layer_init  (PsppireMeansLayer      *ml)
{
  GtkWidget *hbox_upper = gtk_hbox_new (FALSE, 5);
  GtkWidget *hbox_lower = gtk_hbox_new (FALSE, 5);
  GtkWidget *alignment = gtk_alignment_new (0, 0.5, 0, 0);
  GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);

  ml->dispose_has_run = FALSE;
  ml->forward = gtk_button_new_from_stock (GTK_STOCK_GO_FORWARD);
  ml->back = gtk_button_new_from_stock (GTK_STOCK_GO_BACK);
  ml->var_view = psppire_var_view_new ();
  ml->selector = psppire_selector_new ();
  ml->label = gtk_label_new ("");

  g_signal_connect_swapped (ml->forward, "clicked", G_CALLBACK (on_forward),
			    ml);

  g_signal_connect_swapped (ml->back, "clicked", G_CALLBACK (on_back), ml);

  g_signal_connect_swapped (ml->selector, "selected", G_CALLBACK (update), ml);
  g_signal_connect_swapped (ml->selector, "de-selected", G_CALLBACK (update),
			    ml);

  g_object_set (ml->var_view, "headers-visible", FALSE, NULL);
  g_object_set (sw,
		"shadow-type", GTK_SHADOW_ETCHED_IN,
		"hscrollbar-policy", GTK_POLICY_AUTOMATIC,
		NULL);

  g_object_set (ml->selector, "dest-widget", ml->var_view, NULL);
  g_signal_connect_swapped (ml->var_view, "notify::model", G_CALLBACK (update), ml);

  gtk_box_pack_start (GTK_BOX (hbox_upper), ml->back, FALSE, FALSE, 5);
  gtk_box_pack_start (GTK_BOX (hbox_upper), ml->label, TRUE, FALSE, 5);
  gtk_box_pack_start (GTK_BOX (hbox_upper), ml->forward, FALSE, FALSE, 5);

  gtk_box_pack_start (GTK_BOX (hbox_lower), alignment, FALSE, FALSE, 5);
  gtk_container_add (GTK_CONTAINER (alignment), ml->selector);
  gtk_box_pack_start (GTK_BOX (hbox_lower), sw, TRUE, TRUE, 5);
  gtk_container_add (GTK_CONTAINER (sw), ml->var_view);

  gtk_box_pack_start (GTK_BOX (ml), hbox_upper, FALSE, FALSE, 5);
  gtk_box_pack_start (GTK_BOX (ml), hbox_lower, TRUE, TRUE, 5);



  ml->n_layers = 1;
  ml->current_layer = 0;
  ml->layer = g_ptr_array_new_full (3, g_object_unref);

  /* Add a model and take a reference to it */
  g_ptr_array_add (ml->layer, gtk_tree_view_get_model (GTK_TREE_VIEW (ml->var_view)));
  g_object_ref (g_ptr_array_index (ml->layer, ml->current_layer));

  update (ml);

  gtk_widget_show_all (hbox_upper);
  gtk_widget_show_all (hbox_lower);
}

GtkWidget *
psppire_means_layer_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_means_layer_get_type (), NULL));
}


void
psppire_means_layer_set_source (PsppireMeansLayer *ml, GtkWidget *w)
{
  g_object_set (ml->selector, "source-widget", w, NULL);
}


void
psppire_means_layer_clear (PsppireMeansLayer *ml)
{
  ml->n_layers = 1;
  ml->current_layer = 0;
  psppire_var_view_clear (PSPPIRE_VAR_VIEW (ml->var_view));
}


GtkTreeModel *
psppire_means_layer_get_model_n (PsppireMeansLayer *ml, gint n)
{
  return g_ptr_array_index (ml->layer, n);
}
