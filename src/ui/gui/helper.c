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
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */


/* This file is a rubbish bin where stuff gets put when it doesn't seem to
   belong anywhere else.
*/
#include <config.h>

#include "psppire-syntax-window.h"

#include	<glib-object.h>

#include <glib.h>
#include "helper.h"
#include "message-dialog.h"
#include <data/format.h>
#include <data/data-in.h>
#include <data/data-out.h>
#include <data/dictionary.h>
#include <data/casereader-provider.h>
#include <libpspp/message.h>

#include <gtk/gtkbuilder.h>
#include <libpspp/i18n.h>

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <data/settings.h>

#include <language/command.h>
#include <data/lazy-casereader.h>
#include <data/procedure.h>
#include <language/lexer/lexer.h>
#include "psppire-data-store.h"
#include <output/manager.h>
#include "psppire-output-window.h"

#include "xalloc.h"

#include <gettext.h>

/* Formats a value according to FORMAT
   The returned string must be freed when no longer required */
gchar *
value_to_text (union value v, struct fmt_spec format)
{
  gchar *s = 0;

  s = g_new (gchar, format.w + 1);
  data_out (&v, &format, s);
  s[format.w]='\0';
  g_strchug (s);

  return s;
}



gboolean
text_to_value (const gchar *text, union value *v,
	      struct fmt_spec format)
{
  bool ok;

  if ( format.type != FMT_A)
    {
      if ( ! text ) return FALSE;

      {
	const gchar *s = text;
	while (*s)
	  {
	    if ( !isspace (*s))
	      break;
	    s++;
	  }

	if ( !*s) return FALSE;
      }
    }

  msg_disable ();
  ok = data_in (ss_cstr (text), LEGACY_NATIVE, format.type, 0, 0, 0,
                v, fmt_var_width (&format));
  msg_enable ();

  return ok;
}


GtkBuilder *
builder_new_real (const gchar *name)
{
  GtkBuilder *builder = gtk_builder_new ();

  GError *err = NULL;
  if ( ! gtk_builder_add_from_file (builder, name,  &err))
    {
      g_critical ("Couldnt open user interface  file %s: %s", name, err->message);
      g_clear_error (&err);
    }

  return builder;
}



GtkWidget *
get_widget_assert (gpointer x, const gchar *name)
{
  GObject *obj = G_OBJECT (x);
  GtkWidget *w = NULL;
  g_assert (name);

  if (GTK_IS_BUILDER (obj))
    w = GTK_WIDGET (gtk_builder_get_object (GTK_BUILDER (obj), name));

  if (GLADE_IS_XML (obj))
    w = glade_xml_get_widget (GLADE_XML (obj), name);

  if ( !w )
    g_critical ("Widget \"%s\" could not be found\n", name);

  return w;
}

/* Converts a string in the pspp locale to utf-8.
   The return value must be freed when no longer required*/
gchar *
pspp_locale_to_utf8 (const gchar *text, gssize len, GError **err)
{
  return recode_string (CONV_PSPP_TO_UTF8, text, len);
}

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void
give_help (void)
{
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (NULL,
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_INFO,
                                   GTK_BUTTONS_CLOSE,
                                   _("Sorry. The help system hasn't yet "
                                     "been implemented."));
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

void
connect_help (GladeXML *xml)
{
  GList *helps = glade_xml_get_widget_prefix (xml, "help_button_");

  GList *i;
  for ( i = g_list_first (helps); i ; i = g_list_next (i))
    g_signal_connect (GTK_WIDGET (i->data), "clicked", give_help, 0);

  g_list_free (helps);
}



void
reference_manual (GtkMenuItem *menu, gpointer data)
{
  GError *err = NULL;
  if ( ! g_spawn_command_line_async ("yelp info:pspp", &err) )
    {
      msg (ME, _("Cannot open reference manual: %s"), err->message);
    }
  g_clear_error (&err);
}


extern struct dataset *the_dataset;
extern struct source_stream *the_source_stream;
extern PsppireDataStore *the_data_store;

/* Lazy casereader callback function used by execute_syntax. */
static struct casereader *
create_casereader_from_data_store (void *data_store_)
{
  PsppireDataStore *data_store = data_store_;
  return psppire_data_store_get_reader (data_store);
}

gboolean
execute_syntax (struct getl_interface *sss)
{
  struct lexer *lexer;
  gboolean retval = TRUE;

  struct casereader *reader;
  size_t value_cnt;
  casenumber case_cnt;
  unsigned long int lazy_serial;

  /* When the user executes a number of snippets of syntax in a
     row, none of which read from the active file, the GUI becomes
     progressively less responsive.  The reason is that each syntax
     execution encapsulates the active file data in another
     datasheet layer.  The cumulative effect of having a number of
     layers of datasheets wastes time and space.

     To solve the problem, we use a "lazy casereader", a wrapper
     around the casereader obtained from the data store, that
     only actually instantiates that casereader when it is
     needed.  If the data store casereader is never needed, then
     it is reused the next time syntax is run, without wrapping
     it in another layer. */
  value_cnt = psppire_data_store_get_value_count (the_data_store);
  case_cnt = psppire_data_store_get_case_count (the_data_store);
  reader = lazy_casereader_create (value_cnt, case_cnt,
                                   create_casereader_from_data_store,
                                   the_data_store, &lazy_serial);
  proc_set_active_file_data (the_dataset, reader);

  g_return_val_if_fail (proc_has_active_file (the_dataset), FALSE);

  lexer = lex_create (the_source_stream);

  getl_append_source (the_source_stream, sss, GETL_BATCH, ERRMODE_CONTINUE);

  for (;;)
    {
      enum cmd_result result = cmd_parse (lexer, the_dataset);

      if ( cmd_result_is_failure (result))
	{
	  retval = FALSE;
	  if ( source_stream_current_error_mode (the_source_stream)
	       == ERRMODE_STOP )
	    break;
	}

      if ( result == CMD_EOF || result == CMD_FINISH)
	break;
    }

  getl_abort_noninteractive (the_source_stream);

  lex_destroy (lexer);

  psppire_dict_replace_dictionary (the_data_store->dict,
				   dataset_dict (the_dataset));

  reader = proc_extract_active_file_data (the_dataset);
  if (!lazy_casereader_destroy (reader, lazy_serial))
    psppire_data_store_set_reader (the_data_store, reader);

  som_flush ();

  psppire_output_window_reload ();

  return retval;
}



/* Create a deep copy of SRC */
GtkListStore *
clone_list_store (const GtkListStore *src)
{
  GtkTreeIter src_iter;
  gboolean ok;
  gint i;
  const gint n_cols =  gtk_tree_model_get_n_columns (GTK_TREE_MODEL (src));
  GType *types = g_malloc (sizeof (*types) *  n_cols);

  int row = 0;
  GtkListStore *dest;

  for (i = 0 ; i < n_cols; ++i )
    types[i] = gtk_tree_model_get_column_type (GTK_TREE_MODEL (src), i);

  dest = gtk_list_store_newv (n_cols, types);

  for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (src),
					   &src_iter);
       ok;
       ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (src), &src_iter))
    {
      GtkTreeIter dest_iter;
      gtk_list_store_append  (dest, &dest_iter);

      for (i = 0 ; i < n_cols; ++i )
	{
	  GValue val = {0};

	  gtk_tree_model_get_value (GTK_TREE_MODEL (src), &src_iter, i, &val);
	  gtk_list_store_set_value (dest, &dest_iter, i, &val);

	  g_value_unset (&val);
	}
      row++;
    }

  g_free (types);

  return dest;
}



void
paste_syntax_in_new_window (const gchar *syntax)
{
  GtkWidget *se = psppire_syntax_window_new ();

  gtk_text_buffer_insert_at_cursor (PSPPIRE_SYNTAX_WINDOW (se)->buffer, syntax, -1);

  gtk_widget_show (se);
}
