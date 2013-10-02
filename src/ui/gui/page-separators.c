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

#include "page-separators.h"

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

#include "gl/intprops.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Page where the user chooses field separators. */
struct separators_page
  {
    /* How to break lines into columns. */
    struct string separators;   /* Field separators. */
    struct string quotes;       /* Quote characters. */
    bool escape;                /* Doubled quotes yield a quote mark? */

    GtkWidget *page;
    GtkWidget *custom_cb;
    GtkWidget *custom_entry;
    GtkWidget *quote_cb;
    GtkWidget *quote_combo;
    GtkEntry *quote_entry;
    GtkWidget *escape_cb;
    PsppSheetView *fields_tree_view;
  };

/* The "separators" page of the assistant. */

static void revise_fields_preview (struct import_assistant *ia);
static void choose_likely_separators (struct import_assistant *ia);
static void find_commonest_chars (unsigned long int histogram[UCHAR_MAX + 1],
                                  const char *targets, const char *def,
                                  struct string *result);
static void clear_fields (struct import_assistant *ia);
static void revise_fields_preview (struct import_assistant *);
static void set_separators (struct import_assistant *);
static void get_separators (struct import_assistant *);
static void on_separators_custom_entry_notify (GObject *UNUSED,
                                               GParamSpec *UNUSED,
                                               struct import_assistant *);
static void on_separators_custom_cb_toggle (GtkToggleButton *custom_cb,
                                            struct import_assistant *);
static void on_quote_combo_change (GtkComboBox *combo,
                                   struct import_assistant *);
static void on_quote_cb_toggle (GtkToggleButton *quote_cb,
                                struct import_assistant *);
static void on_separator_toggle (GtkToggleButton *, struct import_assistant *);

/* A common field separator and its identifying name. */
struct separator
  {
    const char *name;           /* Name (for use with get_widget_assert). */
    int c;                      /* Separator character. */
  };

/* All the separators in the dialog box. */
static const struct separator separators[] =
  {
    {"space", ' '},
    {"tab", '\t'},
    {"bang", '!'},
    {"colon", ':'},
    {"comma", ','},
    {"hyphen", '-'},
    {"pipe", '|'},
    {"semicolon", ';'},
    {"slash", '/'},
  };
#define SEPARATOR_CNT (sizeof separators / sizeof *separators)

static void
set_quote_list (GtkComboBoxEntry *cb)
{
  GtkListStore *list =  gtk_list_store_new (1, G_TYPE_STRING);
  GtkTreeIter iter;
  gint i;
  const gchar *seperator[3] = {"'\"", "\'", "\""};

  for (i = 0; i < 3; i++)
    {
      const gchar *s = seperator[i];

      /* Add a new row to the model */
      gtk_list_store_append (list, &iter);
      gtk_list_store_set (list, &iter,
                          0, s,
                          -1);

    }

  gtk_combo_box_set_model (GTK_COMBO_BOX (cb), GTK_TREE_MODEL (list));
  g_object_unref (list);

  gtk_combo_box_entry_set_text_column (cb, 0);
}

/* Initializes IA's separators substructure. */

struct separators_page *
separators_page_create (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;

  size_t i;

  struct separators_page *p = xzalloc (sizeof *p);

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "Separators"),
                                   GTK_ASSISTANT_PAGE_CONTENT);

  p->custom_cb = get_widget_assert (builder, "custom-cb");
  p->custom_entry = get_widget_assert (builder, "custom-entry");
  p->quote_combo = get_widget_assert (builder, "quote-combo");
  p->quote_entry = GTK_ENTRY (gtk_bin_get_child (GTK_BIN (p->quote_combo)));
  p->quote_cb = get_widget_assert (builder, "quote-cb");
  p->escape_cb = get_widget_assert (builder, "escape");

  set_quote_list (GTK_COMBO_BOX_ENTRY (p->quote_combo));
  p->fields_tree_view = PSPP_SHEET_VIEW (get_widget_assert (builder, "fields"));
  g_signal_connect (p->quote_combo, "changed",
                    G_CALLBACK (on_quote_combo_change), ia);
  g_signal_connect (p->quote_cb, "toggled",
                    G_CALLBACK (on_quote_cb_toggle), ia);
  g_signal_connect (p->custom_entry, "notify::text",
                    G_CALLBACK (on_separators_custom_entry_notify), ia);
  g_signal_connect (p->custom_cb, "toggled",
                    G_CALLBACK (on_separators_custom_cb_toggle), ia);
  for (i = 0; i < SEPARATOR_CNT; i++)
    g_signal_connect (get_widget_assert (builder, separators[i].name),
                      "toggled", G_CALLBACK (on_separator_toggle), ia);
  g_signal_connect (p->escape_cb, "toggled",
                    G_CALLBACK (on_separator_toggle), ia);

  return p;
}

/* Frees IA's separators substructure. */
void
destroy_separators_page (struct import_assistant *ia)
{
  struct separators_page *s = ia->separators;

  ds_destroy (&s->separators);
  ds_destroy (&s->quotes);
  clear_fields (ia);
}

/* Called just before the separators page becomes visible in the
   assistant. */
void
prepare_separators_page (struct import_assistant *ia)
{
  revise_fields_preview (ia);
}

/* Called when the Reset button is clicked on the separators
   page, resets the separators to the defaults. */
void
reset_separators_page (struct import_assistant *ia)
{
  choose_likely_separators (ia);
  set_separators (ia);
}

/* Frees and clears the column data in IA's separators
   substructure. */
static void
clear_fields (struct import_assistant *ia)
{
  if (ia->column_cnt > 0)
    {
      struct column *col;
      size_t row;

      for (row = 0; row < ia->file.line_cnt; row++)
        {
          const struct string *line = &ia->file.lines[row];
          const char *line_start = ds_data (line);
          const char *line_end = ds_end (line);

          for (col = ia->columns; col < &ia->columns[ia->column_cnt]; col++)
            {
              char *s = ss_data (col->contents[row]);
              if (!(s >= line_start && s <= line_end))
                ss_dealloc (&col->contents[row]);
            }
        }

      for (col = ia->columns; col < &ia->columns[ia->column_cnt]; col++)
        {
          free (col->name);
          free (col->contents);
        }

      free (ia->columns);
      ia->columns = NULL;
      ia->column_cnt = 0;
    }
}

/* Breaks the file data in IA into columns based on the
   separators set in IA's separators substructure. */
static void
split_fields (struct import_assistant *ia)
{
  struct separators_page *s = ia->separators;
  size_t columns_allocated;
  bool space_sep;
  size_t row;

  clear_fields (ia);

  /* Is space in the set of separators? */
  space_sep = ss_find_byte (ds_ss (&s->separators), ' ') != SIZE_MAX;

  /* Split all the lines, not just those from
     ia->first_line.skip_lines on, so that we split the line that
     contains variables names if ia->first_line.variable_names is
     true. */
  columns_allocated = 0;
  for (row = 0; row < ia->file.line_cnt; row++)
    {
      struct string *line = &ia->file.lines[row];
      struct substring text = ds_ss (line);
      size_t column_idx;

      for (column_idx = 0; ; column_idx++)
        {
          struct substring field;
          struct column *column;

          if (space_sep)
            ss_ltrim (&text, ss_cstr (" "));
          if (ss_is_empty (text))
            {
              if (column_idx != 0)
                break;
              field = text;
            }
          else if (!ds_is_empty (&s->quotes)
                   && ds_find_byte (&s->quotes, text.string[0]) != SIZE_MAX)
            {
              int quote = ss_get_byte (&text);
              if (!s->escape)
                ss_get_until (&text, quote, &field);
              else
                {
                  struct string s;
                  int c;

                  ds_init_empty (&s);
                  while ((c = ss_get_byte (&text)) != EOF)
                    if (c != quote)
                      ds_put_byte (&s, c);
                    else if (ss_match_byte (&text, quote))
                      ds_put_byte (&s, quote);
                    else
                      break;
                  field = ds_ss (&s);
                }
            }
          else
            ss_get_bytes (&text, ss_cspan (text, ds_ss (&s->separators)),
                          &field);

          if (column_idx >= ia->column_cnt)
            {
              struct column *column;

              if (ia->column_cnt >= columns_allocated)
                ia->columns = x2nrealloc (ia->columns, &columns_allocated,
                                         sizeof *ia->columns);
              column = &ia->columns[ia->column_cnt++];
              column->name = NULL;
              column->width = 0;
              column->contents = xcalloc (ia->file.line_cnt,
                                          sizeof *column->contents);
            }
          column = &ia->columns[column_idx];
          column->contents[row] = field;
          if (ss_length (field) > column->width)
            column->width = ss_length (field);

          if (space_sep)
            ss_ltrim (&text, ss_cstr (" "));
          if (ss_is_empty (text))
            break;
          if (ss_find_byte (ds_ss (&s->separators), ss_first (text))
              != SIZE_MAX)
            ss_advance (&text, 1);
        }
    }
}

/* Chooses a name for each column on the separators page */
static void
choose_column_names (struct import_assistant *ia)
{
  struct dictionary *dict;
  unsigned long int generated_name_count = 0;
  struct column *col;
  size_t name_row;

  dict = dict_create (get_default_encoding ());
  name_row = ia->variable_names && ia->skip_lines ? ia->skip_lines : 0;
  for (col = ia->columns; col < &ia->columns[ia->column_cnt]; col++)
    {
      char *hint, *name;

      hint = name_row ? ss_xstrdup (col->contents[name_row - 1]) : NULL;
      name = dict_make_unique_var_name (dict, hint, &generated_name_count);
      free (hint);

      col->name = name;
      dict_create_var_assert (dict, name, 0);
    }
  dict_destroy (dict);
}

/* Picks the most likely separator and quote characters based on
   IA's file data. */
static void
choose_likely_separators (struct import_assistant *ia)
{
  unsigned long int histogram[UCHAR_MAX + 1] = { 0 };
  size_t row;

  /* Construct a histogram of all the characters used in the
     file. */
  for (row = 0; row < ia->file.line_cnt; row++)
    {
      struct substring line = ds_ss (&ia->file.lines[row]);
      size_t length = ss_length (line);
      size_t i;
      for (i = 0; i < length; i++)
        histogram[(unsigned char) line.string[i]]++;
    }

  find_commonest_chars (histogram, "\"'", "", &ia->separators->quotes);
  find_commonest_chars (histogram, ",;:/|!\t-", ",", &ia->separators->separators);
  ia->separators->escape = true;
}

/* Chooses the most common character among those in TARGETS,
   based on the frequency data in HISTOGRAM, and stores it in
   RESULT.  If there is a tie for the most common character among
   those in TARGETS, the earliest character is chosen.  If none
   of the TARGETS appear at all, then DEF is used as a
   fallback. */
static void
find_commonest_chars (unsigned long int histogram[UCHAR_MAX + 1],
                      const char *targets, const char *def,
                      struct string *result)
{
  unsigned char max = 0;
  unsigned long int max_count = 0;

  for (; *targets != '\0'; targets++)
    {
      unsigned char c = *targets;
      unsigned long int count = histogram[c];
      if (count > max_count)
        {
          max = c;
          max_count = count;
        }
    }
  if (max_count > 0)
    {
      ds_clear (result);
      ds_put_byte (result, max);
    }
  else
    ds_assign_cstr (result, def);
}

/* Revises the contents of the fields tree view based on the
   currently chosen set of separators. */
static void
revise_fields_preview (struct import_assistant *ia)
{
  GtkWidget *w;

  push_watch_cursor (ia);

  w = GTK_WIDGET (ia->separators->fields_tree_view);
  gtk_widget_destroy (w);
  get_separators (ia);
  split_fields (ia);
  choose_column_names (ia);
  ia->separators->fields_tree_view = create_data_tree_view (
    true,
    GTK_CONTAINER (get_widget_assert (ia->asst.builder, "fields-scroller")),
    ia);

  pop_watch_cursor (ia);
}

/* Sets the widgets to match IA's separators substructure. */
static void
set_separators (struct import_assistant *ia)
{
  struct separators_page *s = ia->separators;
  unsigned int seps;
  struct string custom;
  bool any_custom;
  bool any_quotes;
  size_t i;

  ds_init_empty (&custom);
  seps = 0;
  for (i = 0; i < ds_length (&s->separators); i++)
    {
      unsigned char c = ds_at (&s->separators, i);
      int j;

      for (j = 0; j < SEPARATOR_CNT; j++)
        {
          const struct separator *s = &separators[j];
          if (s->c == c)
            {
              seps += 1u << j;
              goto next;
            }
        }

      ds_put_byte (&custom, c);
    next:;
    }

  for (i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *s = &separators[i];
      GtkWidget *button = get_widget_assert (ia->asst.builder, s->name);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                    (seps & (1u << i)) != 0);
    }
  any_custom = !ds_is_empty (&custom);
  gtk_entry_set_text (GTK_ENTRY (s->custom_entry), ds_cstr (&custom));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (s->custom_cb),
                                any_custom);
  gtk_widget_set_sensitive (s->custom_entry, any_custom);
  ds_destroy (&custom);

  any_quotes = !ds_is_empty (&s->quotes);

  gtk_entry_set_text (s->quote_entry,
                      any_quotes ? ds_cstr (&s->quotes) : "\"");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (s->quote_cb),
                                any_quotes);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (s->escape_cb),
                                s->escape);
  gtk_widget_set_sensitive (s->quote_combo, any_quotes);
  gtk_widget_set_sensitive (s->escape_cb, any_quotes);
}

/* Sets IA's separators substructure to match the widgets. */
static void
get_separators (struct import_assistant *ia)
{
  struct separators_page *s = ia->separators;
  int i;

  ds_clear (&s->separators);
  for (i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *sep = &separators[i];
      GtkWidget *button = get_widget_assert (ia->asst.builder, sep->name);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
        ds_put_byte (&s->separators, sep->c);
    }

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (s->custom_cb)))
    ds_put_cstr (&s->separators,
                 gtk_entry_get_text (GTK_ENTRY (s->custom_entry)));

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (s->quote_cb)))
    {
      gchar *text = gtk_combo_box_get_active_text (
                      GTK_COMBO_BOX (s->quote_combo));
      ds_assign_cstr (&s->quotes, text);
      g_free (text);
    }
  else
    ds_clear (&s->quotes);
  s->escape = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (s->escape_cb));
}

/* Called when the user changes the entry field for custom
   separators. */
static void
on_separators_custom_entry_notify (GObject *gobject UNUSED,
                                   GParamSpec *arg1 UNUSED,
                                   struct import_assistant *ia)
{
  revise_fields_preview (ia);
}

/* Called when the user toggles the checkbox that enables custom
   separators. */
static void
on_separators_custom_cb_toggle (GtkToggleButton *custom_cb,
                                struct import_assistant *ia)
{
  bool is_active = gtk_toggle_button_get_active (custom_cb);
  gtk_widget_set_sensitive (ia->separators->custom_entry, is_active);
  revise_fields_preview (ia);
}

/* Called when the user changes the selection in the combo box
   that selects a quote character. */
static void
on_quote_combo_change (GtkComboBox *combo, struct import_assistant *ia)
{
  revise_fields_preview (ia);
}

/* Called when the user toggles the checkbox that enables
   quoting. */
static void
on_quote_cb_toggle (GtkToggleButton *quote_cb, struct import_assistant *ia)
{
  bool is_active = gtk_toggle_button_get_active (quote_cb);
  gtk_widget_set_sensitive (ia->separators->quote_combo, is_active);
  gtk_widget_set_sensitive (ia->separators->escape_cb, is_active);
  revise_fields_preview (ia);
}

/* Called when the user toggles one of the separators
   checkboxes. */
static void
on_separator_toggle (GtkToggleButton *toggle UNUSED,
                     struct import_assistant *ia)
{
  revise_fields_preview (ia);
}



void 
separators_append_syntax (const struct import_assistant *ia, struct string *s)
{
  int i;
  ds_put_cstr (s, "  /DELIMITERS=\"");
  if (ds_find_byte (&ia->separators->separators, '\t') != SIZE_MAX)
    ds_put_cstr (s, "\\t");
  if (ds_find_byte (&ia->separators->separators, '\\') != SIZE_MAX)
    ds_put_cstr (s, "\\\\");
  for (i = 0; i < ds_length (&ia->separators->separators); i++)
    {
      char c = ds_at (&ia->separators->separators, i);
      if (c == '"')
	ds_put_cstr (s, "\"\"");
      else if (c != '\t' && c != '\\')
	ds_put_byte (s, c);
    }
  ds_put_cstr (s, "\"\n");
  if (!ds_is_empty (&ia->separators->quotes))
    syntax_gen_pspp (s, "  /QUALIFIER=%sq\n", ds_cstr (&ia->separators->quotes));
  if (!ds_is_empty (&ia->separators->quotes) && ia->separators->escape)
    ds_put_cstr (s, "  /ESCAPE\n");
}
