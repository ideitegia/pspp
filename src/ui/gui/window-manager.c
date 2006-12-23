#include "syntax-editor.h"
#include "data-editor.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#include "window-manager.h"


static int window_count = 0;

static void
deregister (GtkObject *o, gpointer data)
{
  window_count --;

  if ( 0 == window_count )
    gtk_main_quit ();
};

static void set_window_name (struct editor_window *e, const gchar *name );


struct editor_window *
window_create (enum window_type type, const gchar *name)
{
  struct editor_window *e;
  switch (type)
    {
    case WINDOW_SYNTAX:
      e = (struct editor_window *) new_syntax_editor ();
      break;
    case WINDOW_DATA:
      e = (struct editor_window *) new_data_editor ();
      break;
    default:
      g_assert_not_reached ();
    };

  e->type = type;
  e->name = NULL;

  set_window_name (e, name);


  gtk_window_set_icon_from_file (GTK_WINDOW(e->window),
				 PKGDATADIR "/psppicon.png", 0);

  g_signal_connect (e->window, "destroy", G_CALLBACK (deregister), NULL);

  gtk_widget_show (e->window);

  window_count ++;

  return e;
}


static void
set_window_name (struct editor_window *e,
		 const gchar *name )
{
  gchar *title ;
  g_free (e->name);


  if ( name )
    {
      e->name = g_strdup (name);
      return ;
    }

  switch (e->type )
    {
    case WINDOW_SYNTAX:
      e->name = g_strdup_printf (_("Syntax%d"), window_count);
      title = g_strdup_printf (_("%s --- PSPP Syntax Editor"), e->name);
      break;
    case WINDOW_DATA:
      e->name = g_strdup_printf (_("Untitled%d"), window_count);
      title = g_strdup_printf (_("%s --- PSPP Data Editor"), e->name);
      break;
    default:
      g_assert_not_reached ();
    }

  gtk_window_set_title (GTK_WINDOW(e->window), title);

  g_free (title);
}


void
window_set_name_from_filename (struct editor_window *e,
			       const gchar *filename)
{
  gchar *title;
  gchar *basename = g_path_get_basename (filename);

  set_window_name (e, filename);

  switch (e->type )
    {
    case WINDOW_SYNTAX:
      title = g_strdup_printf (_("%s --- PSPP Syntax Editor"), basename);
      break;
    case WINDOW_DATA:
      title = g_strdup_printf (_("%s --- PSPP Data Editor"), basename);
      break;
    default:
      g_assert_not_reached ();
    }
  g_free (basename);

  gtk_window_set_title (GTK_WINDOW(e->window), title);

  g_free (title);
}


GtkWindow *
window_toplevel (const struct editor_window *e)
{
  return GTK_WINDOW(e->window);
}

const gchar *
window_name (const struct editor_window *e)
{
  return e->name;
}
