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
  GtkWindow *window;      /* The top level window of the editor */
  gchar *name;            /* The name of this editor (UTF-8) */
  enum window_type type;
 } ;

struct editor_window * window_create (enum window_type type,
				      const gchar *name);

const gchar * window_name (const struct editor_window *);

/* Set the name of this window based on FILENAME.
   FILENAME is in "filename encoding" */
void window_set_name_from_filename (struct editor_window *e,
				    const gchar *filename);

void default_window_name (struct editor_window *w);

void minimise_all_windows (void);


#endif
