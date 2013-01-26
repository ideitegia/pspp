/* This is a file */

#include <config.h>

#include <gtk/gtk.h>

#include "psppire-spreadsheet-model.h"

#define N 10

static GtkListStore *
make_store ()
  {
    int i;
    GtkTreeIter iter;
    
    GtkListStore * list_store  = gtk_list_store_new (2, G_TYPE_INT, G_TYPE_STRING);

    for (i = 0; i < N; ++i)
      {
	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter,
			    0, N - i,
			    1, "xxx", 
			    -1);
      }
    return list_store;
  }



int
main (int argc, char *argv[] )
{
  GtkWidget *window;
  GtkWidget *treeview;
  GtkTreeModel *tm;
  gtk_init (&argc, &argv);
    

  tm = psppire_spreadsheet_model_new ();
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    
  gtk_container_set_border_width (GTK_CONTAINER (window), 10);

  
  treeview = gtk_tree_view_new_with_model (tm);

  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
					       0, "sheet name",
					       gtk_cell_renderer_text_new (),
					       "text", 0,
					       NULL);


  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
					       1, "range",
					       gtk_cell_renderer_text_new (),
					       "text", 1,
					       NULL);

  gtk_container_add (GTK_CONTAINER (window), treeview);

  gtk_widget_show_all (window);
    
  gtk_main ();
    
  return 0;
}
