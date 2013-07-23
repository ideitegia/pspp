/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012, 2013  Free Software Foundation

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

#include "ui/gui/text-data-import-dialog.h"

#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "data/data-in.h"
#include "data/data-out.h"
#include "data/format-guesser.h"
#include "data/value-labels.h"
#include "language/data-io/data-parser.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/line-reader.h"
#include "libpspp/message.h"
#include "ui/gui/checkbox-treeview.h"
#include "ui/gui/dialog-common.h"
#include "ui/gui/executor.h"
#include "ui/gui/helper.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog.h"
#include "ui/gui/psppire-encoding-selector.h"
#include "ui/gui/psppire-empty-list-store.h"
#include "ui/gui/psppire-var-sheet.h"
#include "ui/gui/psppire-scanf.h"

#include "gl/error.h"
#include "gl/intprops.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Page where the user verifies and adjusts input formats. */
struct formats_page
  {
    GtkWidget *page;
    PsppSheetView *data_tree_view;
    PsppireDict *psppire_dict;
    struct variable **modified_vars;
    size_t modified_var_cnt;
  };

/* The "formats" page of the assistant. */

static void on_variable_change (PsppireDict *dict, int idx,
				unsigned int what,
				const struct variable *oldvar,
                                struct import_assistant *);

static void clear_modified_vars (struct import_assistant *);

/* Initializes IA's formats substructure. */

struct formats_page *
formats_page_create (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  struct formats_page *p = xzalloc (sizeof *p);

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "Formats"),
			 GTK_ASSISTANT_PAGE_CONFIRM);

  p->data_tree_view = PSPP_SHEET_VIEW (get_widget_assert (builder, "data"));
  p->modified_vars = NULL;
  p->modified_var_cnt = 0;

  return p;
}

/* Frees IA's formats substructure. */
void
destroy_formats_page (struct import_assistant *ia)
{
  struct formats_page *p = ia->formats;

  if (p->psppire_dict != NULL)
    {
      dict_destroy (p->psppire_dict->dict);
      g_object_unref (p->psppire_dict);
    }
  clear_modified_vars (ia);
}

/* Called just before the formats page of the assistant is
   displayed. */
void
prepare_formats_page (struct import_assistant *ia)
{
  struct dictionary *dict;
  PsppireDict *psppire_dict;
  GtkBin *vars_scroller;
  GtkWidget *old_var_sheet;
  PsppireVarSheet *var_sheet;
  struct formats_page *p = ia->formats;
  struct fmt_guesser *fg;
  unsigned long int number = 0;
  size_t column_idx;

  push_watch_cursor (ia);

  dict = dict_create (get_default_encoding ());
  fg = fmt_guesser_create ();
  for (column_idx = 0; column_idx < ia->column_cnt; column_idx++)
    {
      struct variable *modified_var = 
	(column_idx < p->modified_var_cnt ? p->modified_vars[column_idx] : NULL);
      if (modified_var == NULL)
        {
          struct column *column = &ia->columns[column_idx];
          struct variable *var;
          struct fmt_spec format;
          char *name;
          size_t row;

          /* Choose variable name. */
          name = dict_make_unique_var_name (dict, column->name, &number);

          /* Choose variable format. */
          fmt_guesser_clear (fg);
          for (row = ia->skip_lines; row < ia->file.line_cnt; row++)
            fmt_guesser_add (fg, column->contents[row]);
          fmt_guesser_guess (fg, &format);
          fmt_fix_input (&format);

          /* Create variable. */
          var = dict_create_var_assert (dict, name, fmt_var_width (&format));
          var_set_both_formats (var, &format);

          free (name);
        }
      else
        {
          char *name;

          name = dict_make_unique_var_name (dict, var_get_name (modified_var),
                                            &number);
          dict_clone_var_as_assert (dict, modified_var, name);
          free (name);
        }
    }
  fmt_guesser_destroy (fg);

  psppire_dict = psppire_dict_new_from_dict (dict);
  g_signal_connect (psppire_dict, "variable-changed",
                    G_CALLBACK (on_variable_change), ia);
  ia->dict = dict;
  ia->formats->psppire_dict = psppire_dict;

  /* XXX: PsppireVarStore doesn't hold a reference to
     psppire_dict for now, but it should.  After it does, we
     should g_object_ref the psppire_dict here, since we also
     hold a reference via ia->formats->dict. */
  var_sheet = PSPPIRE_VAR_SHEET (psppire_var_sheet_new ());
  g_object_set (var_sheet,
                "dictionary", psppire_dict,
                "may-create-vars", FALSE,
                "may-delete-vars", FALSE,
                "format-use", FMT_FOR_INPUT,
                "enable-grid-lines", PSPP_SHEET_VIEW_GRID_LINES_BOTH,
                (void *) NULL);

  vars_scroller = GTK_BIN (get_widget_assert (ia->asst.builder, "vars-scroller"));
  old_var_sheet = gtk_bin_get_child (vars_scroller);
  if (old_var_sheet != NULL)
    gtk_widget_destroy (old_var_sheet);
  gtk_container_add (GTK_CONTAINER (vars_scroller), GTK_WIDGET (var_sheet));
  gtk_widget_show (GTK_WIDGET (var_sheet));

  gtk_widget_destroy (GTK_WIDGET (ia->formats->data_tree_view));
  ia->formats->data_tree_view = create_data_tree_view (
    false,
    GTK_CONTAINER (get_widget_assert (ia->asst.builder, "data-scroller")),
    ia);

  gtk_widget_show (ia->asst.paste_button);

  pop_watch_cursor (ia);
}

/* Clears the set of user-modified variables from IA's formats
   substructure.  This discards user modifications to variable
   formats, thereby causing formats to revert to their
   defaults.  */
static void
clear_modified_vars (struct import_assistant *ia)
{
  struct formats_page *p = ia->formats;
  size_t i;

  for (i = 0; i < p->modified_var_cnt; i++)
    var_destroy (p->modified_vars[i]);
  free (p->modified_vars);
  p->modified_vars = NULL;
  p->modified_var_cnt = 0;
}

/* Resets the formats page to its defaults, discarding user
   modifications. */
void
reset_formats_page (struct import_assistant *ia)
{
  clear_modified_vars (ia);
  prepare_formats_page (ia);
}



/* Called when the user changes one of the variables in the
   dictionary. */
static void
on_variable_change (PsppireDict *dict, int dict_idx,
		    unsigned int what, const struct variable *oldvar,
                    struct import_assistant *ia)
{
  struct formats_page *p = ia->formats;
  PsppSheetView *tv = ia->formats->data_tree_view;
  gint column_idx = dict_idx + 1;

  push_watch_cursor (ia);

  /* Remove previous column and replace with new column. */
  pspp_sheet_view_remove_column (tv, pspp_sheet_view_get_column (tv, column_idx));
  pspp_sheet_view_insert_column (tv, make_data_column (ia, tv, false, dict_idx),
                                 column_idx);

  /* Save a copy of the modified variable in modified_vars, so
     that its attributes will be preserved if we back up to the
     previous page with the Prev button and then come back
     here. */
  if (dict_idx >= p->modified_var_cnt)
    {
      size_t i;
      p->modified_vars = xnrealloc (p->modified_vars, dict_idx + 1,
                                    sizeof *p->modified_vars);
      for (i = 0; i <= dict_idx; i++)
        p->modified_vars[i] = NULL;
      p->modified_var_cnt = dict_idx + 1;
    }
  if (p->modified_vars[dict_idx])
    var_destroy (p->modified_vars[dict_idx]);
  p->modified_vars[dict_idx]
    = var_clone (psppire_dict_get_variable (dict, dict_idx));

  pop_watch_cursor (ia);
}




void
formats_append_syntax (const struct import_assistant *ia, struct string *s)
{
  int i;
  int var_cnt;
  ds_put_cstr (s, "  /VARIABLES=\n");
  
  var_cnt = dict_get_var_cnt (ia->dict);
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *var = dict_get_var (ia->dict, i);
      char format_string[FMT_STRING_LEN_MAX + 1];
      fmt_to_string (var_get_print_format (var), format_string);
      ds_put_format (s, "    %s %s%s\n",
		     var_get_name (var), format_string,
		     i == var_cnt - 1 ? "." : "");
    }
}
