/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006, 2009, 2010  Free Software Foundation

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

#include "data/casereader.h"
#include "data/datasheet.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "data/por-file-reader.h"
#include "data/procedure.h"
#include "data/settings.h"
#include "data/sys-file-reader.h"

#include "language/lexer/lexer.h"
#include "language/syntax-string-source.h"

#include "libpspp/getl.h"
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
#include "ui/gui/psppire-output-window.h"
#include "ui/gui/psppire-selector.h"
#include "ui/gui/psppire-var-store.h"
#include "ui/gui/psppire-var-view.h"
#include "ui/gui/psppire-window-register.h"
#include "ui/gui/psppire.h"
#include "ui/gui/widgets.h"
#include "ui/source-init-opts.h"
#include "ui/syntax-gen.h"

#include "gl/configmake.h"
#include "gl/xalloc.h"
#include "gl/relocatable.h"

GtkRecentManager *the_recent_mgr = 0;
PsppireDataStore *the_data_store = 0;
PsppireVarStore *the_var_store = 0;

static void create_icon_factory (void);

struct source_stream *the_source_stream ;
struct dataset * the_dataset = NULL;

static GtkWidget *the_data_window;

static void handle_msg (const struct msg *);
static void load_data_file (const char *);

static void
replace_casereader (struct casereader *s)
{
  psppire_data_store_set_reader (the_data_store, s);
}

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid




void
initialize (struct source_stream *ss, const char *data_file)
{
  PsppireDict *dictionary = 0;

  i18n_init ();

  preregister_widgets ();

  gsl_set_error_handler_off ();
  settings_init ();
  fh_init ();

  the_dataset = create_dataset ();

  the_source_stream = ss;
  msg_init (ss, handle_msg);

  dictionary = psppire_dict_new_from_dict (dataset_dict (the_dataset));

  bind_textdomain_codeset (PACKAGE, "UTF-8");

  /* Create the model for the var_sheet */
  the_var_store = psppire_var_store_new (dictionary);

  the_data_store = psppire_data_store_new (dictionary);
  replace_casereader (NULL);

  create_icon_factory ();

  psppire_output_window_setup ();

  journal_enable ();
  textdomain (PACKAGE);


  the_recent_mgr = gtk_recent_manager_get_default ();

  psppire_selector_set_default_selection_func (GTK_TYPE_ENTRY, insert_source_row_into_entry);
  psppire_selector_set_default_selection_func (PSPPIRE_VAR_VIEW_TYPE, insert_source_row_into_tree_view);
  psppire_selector_set_default_selection_func (GTK_TYPE_TREE_VIEW, insert_source_row_into_tree_view);

  the_data_window = psppire_data_window_new ();
  if (data_file != NULL)
    load_data_file (data_file);

  execute_const_syntax_string ("");

  gtk_widget_show (the_data_window);
}


void
de_initialize (void)
{
  destroy_source_stream (the_source_stream);
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


struct icon_info
{
  const char *file_name;
  const gchar *id;
};


static const struct icon_info icons[] =
  {
    {PKGDATADIR "/value-labels.png",    "pspp-value-labels"},
    {PKGDATADIR "/weight-cases.png",    "pspp-weight-cases"},
    {PKGDATADIR "/goto-variable.png",   "pspp-goto-variable"},
    {PKGDATADIR "/insert-variable.png", "pspp-insert-variable"},
    {PKGDATADIR "/insert-case.png",     "pspp-insert-case"},
    {PKGDATADIR "/split-file.png",      "pspp-split-file"},
    {PKGDATADIR "/select-cases.png",    "pspp-select-cases"},
    {PKGDATADIR "/recent-dialogs.png",  "pspp-recent-dialogs"},
    {PKGDATADIR "/nominal.png",         "var-nominal"},
    {PKGDATADIR "/ordinal.png",         "var-ordinal"},
    {PKGDATADIR "/scale.png",           "var-scale"},
    {PKGDATADIR "/string.png",          "var-string"},
    {PKGDATADIR "/date-scale.png",      "var-date-scale"}
  };

static void
create_icon_factory (void)
{
  gint i;
  GtkIconFactory *factory = gtk_icon_factory_new ();

  for (i = 0 ; i < sizeof (icons) / sizeof(icons[0]); ++i)
    {
      GError *err = NULL;
      GdkPixbuf *pixbuf =
	gdk_pixbuf_new_from_file (relocate (icons[i].file_name), &err);

      if ( pixbuf )
	{
	  GtkIconSet *icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
	  g_object_unref (pixbuf);
	  gtk_icon_factory_add ( factory, icons[i].id, icon_set);
	}
      else
	{
	  g_warning ("Cannot create icon: %s", err->message);
	  g_clear_error (&err);
	}
    }

  {
    /* Create our own "pspp-stock-reset" item, using the
       GTK_STOCK_REFRESH icon set */

    GtkStockItem items[] = {
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

static void
load_data_file (const char *arg)
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
      utf8 = xstrdup (arg);
    }
  else
    {
      GError *err = NULL;
      utf8 = g_locale_to_utf8 (arg, -1, NULL, &written, &err);
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
    filename = xstrdup (arg);

  psppire_window_load (PSPPIRE_WINDOW (the_data_window), filename);

  g_free (filename);
}

static void
handle_msg (const struct msg *m)
{
  message_item_submit (message_item_create (m));
}
