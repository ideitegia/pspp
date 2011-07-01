/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2010, 2011  Free Software Foundation

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


#ifndef __PSPPIRE_SYNTAX_WINDOW_H__
#define __PSPPIRE_SYNTAX_WINDOW_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include "psppire-window.h"

#include <gtksourceview/gtksourcelanguage.h>
#include <gtksourceview/gtksourcelanguagemanager.h>
#include <gtksourceview/gtksourcebuffer.h>
#include <gtksourceview/gtksourceprintcompositor.h>

G_BEGIN_DECLS

#define PSPPIRE_SYNTAX_WINDOW_TYPE            (psppire_syntax_window_get_type ())
#define PSPPIRE_SYNTAX_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_SYNTAX_WINDOW_TYPE, PsppireSyntaxWindow))
#define PSPPIRE_SYNTAX_WINDOW_CLASS(class)    (G_TYPE_CHECK_CLASS_CAST ((class), \
    PSPPIRE_SYNTAX_WINDOW_TYPE, PsppireSyntaxWindowClass))
#define PSPPIRE_IS_SYNTAX_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
    PSPPIRE_SYNTAX_WINDOW_TYPE))
#define PSPPIRE_IS_SYNTAX_WINDOW_CLASS(class) (G_TYPE_CHECK_CLASS_TYPE ((class), \
    PSPPIRE_SYNTAX_WINDOW_TYPE))


typedef struct _PsppireSyntaxWindow       PsppireSyntaxWindow;
typedef struct _PsppireSyntaxWindowClass  PsppireSyntaxWindowClass;


struct _PsppireSyntaxWindow
{
  PsppireWindow parent;

  /* <private> */

  GtkSourceBuffer *buffer;  /* The buffer which contains the text */
  struct lexer *lexer;    /* Lexer to parse syntax */
  gchar *encoding;              /* File's encoding. */
  GtkWidget *sb;
  guint text_context;

  GtkPrintSettings *print_settings;
  GtkSourcePrintCompositor *compositor;
  GtkAction *undo_menuitem;
  GtkAction *redo_menuitem;

  gchar *cliptext;

  GtkAction *edit_cut;
  GtkAction *edit_copy;
  GtkAction *edit_delete;
  GtkAction *edit_paste;

  gulong ps_handler;
  gulong sel_handler;

  gboolean dispose_has_run;
};

struct _PsppireSyntaxWindowClass
{
  PsppireWindowClass parent_class;


  GtkSourceLanguage *lan ;
};

GType      psppire_syntax_window_get_type        (void);
GtkWidget* psppire_syntax_window_new             (const char *encoding);

void create_syntax_window (void);
void open_syntax_window (const char *file_name, const char *encoding);

G_END_DECLS

#endif /* __PSPPIRE_SYNTAX_WINDOW_H__ */
