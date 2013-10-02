/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2013  Free Software Foundation

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

#include "page-sheet-spec.h"

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
#include "data/gnumeric-reader.h"
#include "data/ods-reader.h"
#include "data/spreadsheet-reader.h"
#include "language/data-io/data-parser.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/line-reader.h"
#include "libpspp/message.h"
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
#include "ui/syntax-gen.h"

#include "ui/gui/psppire-spreadsheet-model.h"

#include <data/casereader.h>

#include "gl/intprops.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct import_assistant;

/* The "sheet-spec" page of the assistant. */

/* The sheet_spec page of the assistant (only relevant for spreadsheet imports). */
struct sheet_spec_page
  {
    GtkWidget *page;
    struct casereader *reader;
    struct dictionary *dict;
    
    struct spreadsheet_read_options opts;
  };


char *
sheet_spec_gen_syntax (const struct import_assistant *ia)
{
  const struct sheet_spec_page *ssp = ia->sheet_spec;
  GtkBuilder *builder = ia->asst.builder;
  GtkWidget *range_entry = get_widget_assert (builder, "cell-range-entry");
  const gchar *range = gtk_entry_get_text (GTK_ENTRY (range_entry));

  struct string s = DS_EMPTY_INITIALIZER;

  syntax_gen_pspp (&s,
		   "GET DATA"
		   "\n  /TYPE=%ss"
		   "\n  /FILE=%sq"
		   "\n  /SHEET=index %d"
		   "\n  /READNAMES=%ss",
		   (ia->spreadsheet->type == SPREADSHEET_GNUMERIC) ? "GNM" : "ODS",
		   ia->file.file_name,			 
		   ssp->opts.sheet_index,
		   ssp->opts.read_names ? "ON" : "OFF");


  if (range && 0 != strcmp ("", range))
    {
      syntax_gen_pspp (&s,
		       "\n  /CELLRANGE=RANGE %sq", range);
    }
  else
    {
      syntax_gen_pspp (&s,
		       "\n  /CELLRANGE=FULL");
    }


  syntax_gen_pspp (&s, ".");

  
  return ds_cstr (&s);
}


static void 
on_sheet_combo_changed (GtkComboBox *cb, struct import_assistant *ia)
{
  GtkTreeIter iter;
  gchar *range = NULL;
  GtkTreeModel *model = gtk_combo_box_get_model (cb);
  GtkBuilder *builder = ia->asst.builder;
  GtkWidget *range_entry = get_widget_assert (builder, "cell-range-entry");

  gtk_combo_box_get_active_iter (cb, &iter);
  gtk_tree_model_get (model, &iter, PSPPIRE_SPREADSHEET_MODEL_COL_RANGE, &range, -1);
  gtk_entry_set_text (GTK_ENTRY (range_entry), range ?  range : "");
  g_free (range);
}

/* Initializes IA's sheet_spec substructure. */
struct sheet_spec_page *
sheet_spec_page_create (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  struct sheet_spec_page *p = xzalloc (sizeof (*p));

  GtkWidget *combo_box = get_widget_assert (builder, "sheet-entry");
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
				  "text", 0,
				  NULL);

  g_signal_connect (combo_box, "changed", G_CALLBACK (on_sheet_combo_changed), ia);

  add_page_to_assistant (ia, get_widget_assert (builder, "Sheet"),
			 GTK_ASSISTANT_PAGE_INTRO);

  return p;
}

/* Prepares IA's sheet_spec page. */
void
prepare_sheet_spec_page (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  GtkWidget *sheet_entry = get_widget_assert (builder, "sheet-entry");
  GtkWidget *readnames_checkbox = get_widget_assert (builder, "readnames-checkbox");

  gtk_combo_box_set_model (GTK_COMBO_BOX (sheet_entry), 
			   psppire_spreadsheet_model_new (ia->spreadsheet));

  gtk_combo_box_set_active (GTK_COMBO_BOX (sheet_entry), 0);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (readnames_checkbox), FALSE);
}


/* Resets IA's sheet_spec page to its initial state. */
void
reset_sheet_spec_page (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  GtkWidget *sheet_entry = get_widget_assert (builder, "sheet-entry");
  GtkWidget *readnames_checkbox = get_widget_assert (builder, "readnames-checkbox");

  gtk_combo_box_set_active (GTK_COMBO_BOX (sheet_entry), 0);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (readnames_checkbox), FALSE);
}

/* Called when the Forward button is clicked, 
   but before displaying the new page.
*/
void
post_sheet_spec_page (struct import_assistant *ia)
{
  int row_start = -1;
  int row_stop = -1;
  int col_start = -1;
  int col_stop = -1;

  GtkBuilder *builder = ia->asst.builder;

  struct sheet_spec_page *ssp = ia->sheet_spec;
  struct casereader *creader = NULL;
  struct dictionary *dict = NULL;

  GtkWidget *readnames_checkbox = get_widget_assert (builder, "readnames-checkbox");
  GtkWidget *range_entry = get_widget_assert (builder, "cell-range-entry");
  const gchar *range = gtk_entry_get_text (GTK_ENTRY (range_entry));
  GtkWidget *combo_box = get_widget_assert (builder, "sheet-entry");

  gint num = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box));
  
  ssp->opts.sheet_name = NULL;
  ssp->opts.cell_range = NULL;
  ssp->opts.sheet_index = num + 1;

  if ( convert_cell_ref (range, &col_start, &row_start, &col_stop, &row_stop))
    {
      ssp->opts.cell_range = g_strdup (range);
    }

  ssp->opts.read_names = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (readnames_checkbox));
  ssp->opts.asw = -1;

  switch (ia->spreadsheet->type)
    {
    case SPREADSHEET_ODS:
    case SPREADSHEET_GNUMERIC:
      {
	creader = spreadsheet_make_reader (ia->spreadsheet, &ssp->opts);
	dict = ia->spreadsheet->dict;
      }
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  ssp->dict = dict;
  ssp->reader = creader;

  if (creader && dict)
    {
      update_assistant (ia);
    }
  else
    {
      GtkWidget * dialog = gtk_message_dialog_new (NULL,
			      GTK_DIALOG_MODAL,
			      GTK_MESSAGE_ERROR,
			      GTK_BUTTONS_CLOSE,
			      _("An error occurred reading the spreadsheet file."));

      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
    }
}


/*
  Update IA according to the contents of DICT and CREADER.
  CREADER will be destroyed by this function.
*/
void 
update_assistant (struct import_assistant *ia)
{
  struct sheet_spec_page *ssp = ia->sheet_spec;
  int rows = 0;

  if (ssp->dict)
    {
      struct ccase *c;
      int col;

      ia->column_cnt = dict_get_var_cnt (ssp->dict);
      ia->columns = xcalloc (ia->column_cnt, sizeof (*ia->columns));
      for (col = 0; col < ia->column_cnt ; ++col)
	{
	  const struct variable *var = dict_get_var (ssp->dict, col);
	  ia->columns[col].name = xstrdup (var_get_name (var));
	  ia->columns[col].contents = NULL;
	}

      for (; (c = casereader_read (ssp->reader)) != NULL; case_unref (c))
	{
	  rows++;
	  for (col = 0; col < ia->column_cnt ; ++col)
	    {
	      char *ss;
	      const struct variable *var = dict_get_var (ssp->dict, col);
	      
	      ia->columns[col].contents = xrealloc (ia->columns[col].contents,
						    sizeof (struct substring) * rows);
	      
	      ss = data_out (case_data (c, var), dict_get_encoding (ssp->dict), 
			     var_get_print_format (var));
	      
	      ia->columns[col].contents[rows - 1] = ss_cstr (ss);
	    }
	  
	  if (rows > MAX_PREVIEW_LINES)
	    {
	      case_unref (c);
	      break;
	    }
	}
    }
  
  ia->file.line_cnt = rows;
}
