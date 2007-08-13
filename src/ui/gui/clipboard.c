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

#include <config.h>

#include <gtksheet/gtksheet.h>
#include "clipboard.h"
#include <data/case.h>
#include "psppire-data-store.h"
#include <data/casereader.h>
#include <data/case-map.h>
#include <libpspp/alloc.h>
#include <data/casewriter.h>
#include <data/format.h>
#include <data/data-out.h>
#include <stdlib.h>


/* A casereader and dictionary holding the data currently in the clip */
static struct casereader *clip_datasheet = NULL;
struct dictionary *clip_dict = NULL;




static void data_sheet_update_clipboard (GtkSheet *);

/* Set the clip according to the currently
   selected range in the data sheet */
void
data_sheet_set_clip (GtkSheet *sheet)
{
  int i;
  struct casewriter *writer ;
  GtkSheetRange range;
  PsppireDataStore *ds;
  struct case_map *map = NULL;
  casenumber max_rows;
  size_t max_columns;

  ds = PSPPIRE_DATA_STORE (gtk_sheet_get_model (sheet));

  gtk_sheet_get_selected_range (sheet, &range);

   /* If nothing selected, then use active cell */
  if ( range.row0 < 0 || range.col0 < 0 )
    {
      gint row, col;
      gtk_sheet_get_active_cell (sheet, &row, &col);

      range.row0 = range.rowi = row;
      range.col0 = range.coli = col;
    }

  /* The sheet range can include cells that do not include data.
     Exclude them from the range. */
  max_rows = psppire_data_store_get_case_count (ds);
  if (range.rowi >= max_rows)
    {
      if (max_rows == 0)
        return;
      range.rowi = max_rows - 1;
    }
  max_columns = dict_get_var_cnt (ds->dict->dict);
  if (range.coli >= max_columns)
    {
      if (max_columns == 0)
        return;
      range.coli = max_columns - 1;
    }

  g_return_if_fail (range.rowi >= range.row0);
  g_return_if_fail (range.row0 >= 0);
  g_return_if_fail (range.coli >= range.col0);
  g_return_if_fail (range.col0 >= 0);

  /* Destroy any existing clip */
  if ( clip_datasheet )
    {
      casereader_destroy (clip_datasheet);
      clip_datasheet = NULL;
    }

  if ( clip_dict )
    {
      dict_destroy (clip_dict);
      clip_dict = NULL;
    }

  /* Construct clip dictionary. */
  clip_dict = dict_create ();
  for (i = range.col0; i <= range.coli; i++)
    {
      const struct variable *old = dict_get_var (ds->dict->dict, i);
      dict_clone_var_assert (clip_dict, old, var_get_name (old));
    }

  /* Construct clip data. */
  map = case_map_by_name (ds->dict->dict, clip_dict);
  writer = autopaging_writer_create (dict_get_next_value_idx (clip_dict));
  for (i = range.row0; i <= range.rowi ; ++i )
    {
      struct ccase old;

      if (psppire_case_file_get_case (ds->case_file, i, &old))
        {
          struct ccase new;

          case_map_execute (map, &old, &new);
          case_destroy (&old);
          casewriter_write (writer, &new);
        }
      else
        casewriter_force_error (writer);
    }
  case_map_destroy (map);

  clip_datasheet = casewriter_make_reader (writer);

  data_sheet_update_clipboard (sheet);
}

enum {
  SELECT_FMT_NULL,
  SELECT_FMT_TEXT,
  SELECT_FMT_HTML
};


/* Perform data_out for case CC, variable V, appending to STRING */
static void
data_out_g_string (GString *string, const struct variable *v,
		   const struct ccase *cc)
{
  char *buf ;

  const struct fmt_spec *fs = var_get_print_format (v);
  const union value *val = case_data (cc, v);
  buf = xzalloc (fs->w);

  data_out (val, fs, buf);

  g_string_append_len (string, buf, fs->w);

  g_free (buf);
}

static GString *
clip_to_text (void)
{
  casenumber r;
  GString *string;

  const size_t val_cnt = casereader_get_value_cnt (clip_datasheet);
  const casenumber case_cnt = casereader_get_case_cnt (clip_datasheet);

  string = g_string_sized_new (10 * val_cnt * case_cnt);

  for (r = 0 ; r < case_cnt ; ++r )
    {
      int c;
      struct ccase cc;
      if ( !  casereader_peek (clip_datasheet, r, &cc))
	{
	  g_warning ("Clipboard seems to have inexplicably shrunk");
	  break;
	}

      for (c = 0 ; c < val_cnt ; ++c)
	{
	  const struct variable *v = dict_get_var (clip_dict, c);
	  data_out_g_string (string, v, &cc);
	  if ( c < val_cnt - 1 )
	    g_string_append (string, "\t");
	}

      if ( r < case_cnt)
	g_string_append (string, "\n");

      case_destroy (&cc);
    }

  return string;
}


static GString *
clip_to_html (void)
{
  casenumber r;
  GString *string;

  const size_t val_cnt = casereader_get_value_cnt (clip_datasheet);
  const casenumber case_cnt = casereader_get_case_cnt (clip_datasheet);

  /* Guestimate the size needed */
  string = g_string_sized_new (20 * val_cnt * case_cnt);

  g_string_append (string, "<table>\n");
  for (r = 0 ; r < case_cnt ; ++r )
    {
      int c;
      struct ccase cc;
      if ( !  casereader_peek (clip_datasheet, r, &cc))
	{
	  g_warning ("Clipboard seems to have inexplicably shrunk");
	  break;
	}
      g_string_append (string, "<tr>\n");

      for (c = 0 ; c < val_cnt ; ++c)
	{
	  const struct variable *v = dict_get_var (clip_dict, c);
	  g_string_append (string, "<td>");
	  data_out_g_string (string, v, &cc);
	  g_string_append (string, "</td>\n");
	}

      g_string_append (string, "</tr>\n");

      case_destroy (&cc);
    }
  g_string_append (string, "</table>\n");

  return string;
}



static void
clipboard_get_cb (GtkClipboard     *clipboard,
		  GtkSelectionData *selection_data,
		  guint             info,
		  gpointer          data)
{
  GString *string = NULL;

  switch (info)
    {
    case SELECT_FMT_TEXT:
      string = clip_to_text ();
      break;
    case SELECT_FMT_HTML:
      string = clip_to_html ();
      break;
    default:
      g_assert_not_reached ();
    }

  gtk_selection_data_set (selection_data, selection_data->target,
			  8,
			  (const guchar *) string->str, string->len);

  g_string_free (string, TRUE);
}

static void
clipboard_clear_cb (GtkClipboard *clipboard,
		    gpointer data)
{
  dict_destroy (clip_dict);
  clip_dict = NULL;

  casereader_destroy (clip_datasheet);
  clip_datasheet = NULL;
}



static void
data_sheet_update_clipboard (GtkSheet *sheet)
{
  static const GtkTargetEntry targets[] = {
    { "UTF8_STRING",   0, SELECT_FMT_TEXT },
    { "STRING",        0, SELECT_FMT_TEXT },
    { "TEXT",          0, SELECT_FMT_TEXT },
    { "COMPOUND_TEXT", 0, SELECT_FMT_TEXT },
    { "text/plain;charset=utf-8", 0, SELECT_FMT_TEXT },
    { "text/plain",    0, SELECT_FMT_TEXT },
    { "text/html",     0, SELECT_FMT_HTML }
  };

  GtkClipboard *clipboard =
    gtk_widget_get_clipboard (GTK_WIDGET (sheet),
			      GDK_SELECTION_CLIPBOARD);

  if (!gtk_clipboard_set_with_owner (clipboard, targets,
				     G_N_ELEMENTS (targets),
				     clipboard_get_cb, clipboard_clear_cb,
				     G_OBJECT (sheet)))
    clipboard_clear_cb (clipboard, sheet);
}

