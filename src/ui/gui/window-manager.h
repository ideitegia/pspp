#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <gtk/gtk.h>

enum window_type
  {
    WINDOW_DATA,
    WINDOW_SYNTAX
  };


struct editor_window
 {
  GtkWidget *window;      /* The top level window of the editor */
  gchar *name;            /* The name of this editor */
  enum window_type type;
 } ;

struct editor_window * window_create (enum window_type type,
				      const gchar *name);


GtkWindow * window_toplevel (const struct editor_window *);

const gchar * window_name (const struct editor_window *);

void window_set_name_from_filename (struct editor_window *e,
				    const gchar *filename);

#endif
