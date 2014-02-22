/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006, 2009, 2010, 2011, 2012, 2013, 2014  Free Software Foundation

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


#include <assert.h>
#include <gsl/gsl_errno.h>
#include <gtk/gtk.h>
#include <libintl.h>
#include <unistd.h>

#include "data/any-reader.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/datasheet.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "data/por-file-reader.h"
#include "data/session.h"
#include "data/settings.h"
#include "data/sys-file-reader.h"

#include "language/lexer/lexer.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/version.h"

#include "output/driver.h"
#include "output/journal.h"
#include "output/message-item.h"

#include "ui/gui/dict-display.h"
#include "ui/gui/executor.h"
#include "ui/gui/psppire-data-store.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dict.h"
#include "ui/gui/psppire.h"
#include "ui/gui/psppire-output-window.h"
#include "ui/gui/psppire-syntax-window.h"
#include "ui/gui/psppire-selector.h"
#include "ui/gui/psppire-var-view.h"
#include "ui/gui/psppire-means-layer.h"
#include "ui/gui/psppire-window-register.h"
#include "ui/gui/widgets.h"
#include "ui/source-init-opts.h"
#include "ui/syntax-gen.h"

#include "ui/gui/icons/icon-names.h"


#include "gl/configmake.h"
#include "gl/xalloc.h"
#include "gl/relocatable.h"

static void create_icon_factory (void);
static gchar *local_to_filename_encoding (const char *fn);


#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


void
initialize (const char *data_file)
{
  i18n_init ();

  preregister_widgets ();

  gsl_set_error_handler_off ();
  settings_init ();
  fh_init ();

  psppire_set_lexer (NULL);

  bind_textdomain_codeset (PACKAGE, "UTF-8");

  create_icon_factory ();

  psppire_output_window_setup ();

  journal_init ();
  textdomain (PACKAGE);

  /* FIXME: This should be implemented with a GtkInterface */
  psppire_selector_set_default_selection_func (GTK_TYPE_ENTRY, insert_source_row_into_entry);
  psppire_selector_set_default_selection_func (PSPPIRE_VAR_VIEW_TYPE, insert_source_row_into_tree_view);
  psppire_selector_set_default_selection_func (GTK_TYPE_TREE_VIEW, insert_source_row_into_tree_view);
  psppire_selector_set_default_selection_func (PSPPIRE_TYPE_MEANS_LAYER, insert_source_row_into_layers);

  if (data_file)
    {
      gchar *filename = local_to_filename_encoding (data_file);

      enum detect_result res = any_reader_may_open (filename);

      /* Check to see if the file is a .sav or a .por file.  If not
         assume that it is a syntax file */
      if (res == ANY_YES)
	open_data_window (NULL, filename, NULL, NULL);
      else if (res == ANY_NO)
        {
          create_data_window ();
          open_syntax_window (filename, NULL);
        }

      g_free (filename);
    }
  else
    create_data_window ();
}


void
de_initialize (void)
{
  settings_done ();
  output_close ();
  i18n_done ();
}

static void
func (gpointer key, gpointer value, gpointer data)
{
  gboolean rv;
  PsppireWindow *window = PSPPIRE_WINDOW (value);

  g_signal_emit_by_name (window, "delete-event", 0, &rv);
}

void
psppire_quit (void)
{
  PsppireWindowRegister *reg = psppire_window_register_new ();
  psppire_window_register_foreach (reg, func, NULL);

  gtk_main_quit ();
}

struct icon_size
{
  int resolution;  /* The dimension of the images which will be used */
  size_t n_sizes;  /* The number of items in the array below. */
  const GtkIconSize *usage; /* An array determining for what the icon set is used */
};

static const GtkIconSize menus[] = {GTK_ICON_SIZE_MENU};
static const GtkIconSize large_toolbar[] = {GTK_ICON_SIZE_LARGE_TOOLBAR};
static const GtkIconSize small_toolbar[] = {GTK_ICON_SIZE_SMALL_TOOLBAR};


/* We currently have three icon sets viz: 16x16, 24x24 and 32x32
   We use the 16x16 for menus, the 32x32 for the large_toolbars and 
   the 24x24 for small_toolbars.

   The order of this array is pertinent.  The icons in the sets occuring
   earlier in the array will be used a the wildcard (default) icon size,
   if such an icon exists.
*/
static const struct icon_size sizemap[] = 
{
  {24,  sizeof (small_toolbar) / sizeof (GtkIconSize), small_toolbar},
  {16,  sizeof (menus) / sizeof (GtkIconSize), menus},
  {32,  sizeof (large_toolbar) / sizeof (GtkIconSize), large_toolbar}
};


static void
create_icon_factory (void)
{
  gint c;
  GtkIconFactory *factory = gtk_icon_factory_new ();
  struct icon_context ctx[2];
  ctx[0] = action_icon_context;
  ctx[1] = category_icon_context;
  for (c = 0 ; c < 2 ; ++c)
  {
    const struct icon_context *ic = &ctx[c];
    gint i;
    for (i = 0 ; i < ic->n_icons ; ++i)
      {
	gboolean wildcarded = FALSE;
	GtkIconSet *icon_set = gtk_icon_set_new ();
	int r;
	for (r = 0 ; r < sizeof (sizemap) / sizeof (sizemap[0]); ++r)
	  {
	    int s;
	    GtkIconSource *source = gtk_icon_source_new ();
	    gchar *filename = g_strdup_printf ("%s/%s/%dx%d/%s.png", PKGDATADIR,
					       ic->context_name,
					       sizemap[r].resolution, sizemap[r].resolution,
					       ic->icon_name[i]);
	    const char *relocated_filename = relocate (filename);
	    GFile *gf = g_file_new_for_path (relocated_filename);
	    if (g_file_query_exists (gf, NULL))
	      {
		gtk_icon_source_set_filename (source, relocated_filename);
		if (!wildcarded)
		  {
		    gtk_icon_source_set_size_wildcarded (source, TRUE);
		    wildcarded = TRUE;
		  }
	      }
	    g_object_unref (gf);

	    for (s = 0 ; s < sizemap[r].n_sizes ; ++s)
	      gtk_icon_source_set_size (source, sizemap[r].usage[s]);
	    if (filename != relocated_filename)
	      free (CONST_CAST (char *, relocated_filename));
	    g_free (filename);

	    if ( gtk_icon_source_get_filename (source))
	      gtk_icon_set_add_source (icon_set, source);
	  }
      
	gtk_icon_factory_add (factory, ic->icon_name[i], icon_set);
    }
  }

  {
    struct iconmap
    {
      const gchar *gtk_id;
      gchar *pspp_id;
    };

    /* We have our own icons for some things.
       But we want the Stock Item to be identical to the Gtk standard
       ones in all other respects.
    */
    const struct iconmap map[] = {
      {GTK_STOCK_NEW,    "file-new-document"},
      {GTK_STOCK_QUIT,   "file-quit"},
      {GTK_STOCK_SAVE,   "file-save-document"},
      {GTK_STOCK_CUT,    "edit-cut"},
      {GTK_STOCK_COPY,   "edit-copy"},
      {GTK_STOCK_PASTE,  "edit-paste"},
      {GTK_STOCK_UNDO,   "edit-undo"},
      {GTK_STOCK_REDO,   "edit-redo"},
      {GTK_STOCK_DELETE, "edit-delete"},
      {GTK_STOCK_ABOUT,  "help-about"},
      {GTK_STOCK_PRINT,  "file-print-document"}
    };

    GtkStockItem customised[sizeof (map) / sizeof (map[0])];
    int i;

    for (i = 0; i < sizeof (map) / sizeof (map[0]); ++i)
    {
      gtk_stock_lookup (map[i].gtk_id, &customised[i]);
      customised[i].stock_id =  map[i].pspp_id;
    }



    gtk_stock_add (customised, sizeof (map) / sizeof (map[0]));
  }

  {
    /* Create our own "pspp-stock-reset" item, using the
       GTK_STOCK_REFRESH icon set */
    GtkStockItem items[2] = {
      {"pspp-stock-reset", N_("_Reset"), 0, 0, PACKAGE},
      {"pspp-stock-select", N_("_Select"), 0, 0, PACKAGE}
    };

    gtk_stock_add (items, 2);

    gtk_icon_factory_add (factory, "pspp-stock-reset",
			  gtk_icon_factory_lookup_default (GTK_STOCK_REFRESH)
			  );

    gtk_icon_factory_add (factory, "pspp-stock-select",
			  gtk_icon_factory_lookup_default (GTK_STOCK_INDEX)
			  );
  }

  gtk_icon_factory_add_default (factory);
}

/* 
   Convert a filename from the local encoding into "filename" encoding.
   The return value will be allocated on the heap.  It is the responsibility
   of the caller to free it.
 */
static gchar *
local_to_filename_encoding (const char *fn)
{
  gchar *filename = NULL;
  gchar *utf8 = NULL;
  const gchar *local_encoding = NULL;
  gsize written = -1;
  const gboolean local_is_utf8 = g_get_charset (&local_encoding);

  /* There seems to be no Glib function to convert from local encoding
     to filename encoding.  Therefore it has to be done in two steps:
     the intermediate encoding is UTF8.

     Either step could fail.  However, in many cases the file can still
     be loaded even if the conversion fails. So in those cases, after showing
     a warning, we simply copy the locally encoded filename to the destination
     and hope for the best.
  */

  if ( local_is_utf8)
    {
      utf8 = xstrdup (fn);
    }
  else
    {
      GError *err = NULL;
      utf8 = g_locale_to_utf8 (fn, -1, NULL, &written, &err);
      if ( NULL == utf8)
        {
          g_warning ("Cannot convert filename from local encoding `%s' to UTF-8: %s",
                     local_encoding,
                     err->message);
          g_clear_error (&err);
        }
    }

  if ( NULL != utf8)
    {
      GError *err = NULL;
      filename = g_filename_from_utf8 (utf8, written, NULL, NULL, &err);
      if ( NULL == filename)
        {
          g_warning ("Cannot convert filename from UTF8 to filename encoding: %s",
                     err->message);
          g_clear_error (&err);
        }
    }

  g_free (utf8);

  if ( filename == NULL)
    filename = xstrdup (fn);

  return filename;
}

static void
handle_msg (const struct msg *m_, void *lexer_)
{
  struct lexer *lexer = lexer_;
  struct msg m = *m_;

  if (lexer != NULL && m.file_name == NULL)
    {
      m.file_name = CONST_CAST (char *, lex_get_file_name (lexer));
      m.first_line = lex_get_first_line_number (lexer, 0);
      m.last_line = lex_get_last_line_number (lexer, 0);
      m.first_column = lex_get_first_column (lexer, 0);
      m.last_column = lex_get_last_column (lexer, 0);
    }

  message_item_submit (message_item_create (&m));
}

void
psppire_set_lexer (struct lexer *lexer)
{
  msg_set_handler (handle_msg, lexer);
}
