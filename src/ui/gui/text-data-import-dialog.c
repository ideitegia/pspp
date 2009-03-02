/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009  Free Software Foundation

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

#include <gtk/gtk.h>



#include "checkbox-treeview.h"
#include "descriptives-dialog.h"

#include <errno.h>

#include <gtk-contrib/psppire-sheet.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <data/data-in.h>
#include <data/data-out.h>
#include <data/format-guesser.h>
#include <data/value-labels.h>
#include <language/data-io/data-parser.h>
#include <language/syntax-string-source.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <ui/syntax-gen.h>
#include <ui/gui/psppire-data-window.h>
#include <ui/gui/dialog-common.h>
#include <ui/gui/helper.h>
#include <ui/gui/psppire-dialog.h>
#include <ui/gui/psppire-var-sheet.h>
#include <ui/gui/psppire-var-store.h>
#include <ui/gui/helper.h>

#include "error.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


/* TextImportModel, a GtkTreeModel used by the text data import
   dialog. */
enum
  {
    TEXT_IMPORT_MODEL_COLUMN_LINE_NUMBER, /* 1-based line number in file */
    TEXT_IMPORT_MODEL_COLUMN_LINE,        /* The line from the file. */
  };
typedef struct TextImportModel TextImportModel;
typedef struct TextImportModelClass TextImportModelClass;

TextImportModel *text_import_model_new (struct string *lines, size_t line_cnt,
                                        size_t first_line);
gint text_import_model_iter_to_row (const GtkTreeIter *);

struct import_assistant;

/* The file to be imported. */
struct file
  {
    char *file_name;        /* File name. */
    unsigned long int total_lines; /* Number of lines in file. */
    bool total_is_exact;    /* Is total_lines exact (or an estimate)? */

    /* The first several lines of the file. */
    struct string *lines;
    size_t line_cnt;
  };
static bool init_file (struct import_assistant *, GtkWindow *parent);
static void destroy_file (struct import_assistant *);

/* The main body of the GTK+ assistant and related data. */
struct assistant
  {
    GtkBuilder *builder;
    GtkAssistant *assistant;
    GMainLoop *main_loop;
    GtkWidget *paste_button;
    GtkWidget *reset_button;
    int response;
    int watch_cursor;

    GtkCellRenderer *prop_renderer;
    GtkCellRenderer *fixed_renderer;
  };
static void init_assistant (struct import_assistant *, GtkWindow *);
static void destroy_assistant (struct import_assistant *);
static GtkWidget *add_page_to_assistant (struct import_assistant *,
                                         GtkWidget *page,
                                         GtkAssistantPageType);

/* The introduction page of the assistant. */
struct intro_page
  {
    GtkWidget *page;
    GtkWidget *all_cases_button;
    GtkWidget *n_cases_button;
    GtkWidget *n_cases_spin;
    GtkWidget *percent_button;
    GtkWidget *percent_spin;
  };
static void init_intro_page (struct import_assistant *);
static void reset_intro_page (struct import_assistant *);

/* Page where the user chooses the first line of data. */
struct first_line_page
  {
    int skip_lines;    /* Number of initial lines to skip? */
    bool variable_names; /* Variable names above first line of data? */

    GtkWidget *page;
    GtkTreeView *tree_view;
    GtkWidget *variable_names_cb;
  };
static void init_first_line_page (struct import_assistant *);
static void reset_first_line_page (struct import_assistant *);

/* Page where the user chooses field separators. */
struct separators_page
  {
    /* How to break lines into columns. */
    struct string separators;   /* Field separators. */
    struct string quotes;       /* Quote characters. */
    bool escape;                /* Doubled quotes yield a quote mark? */

    /* The columns produced thereby. */
    struct column *columns;     /* Information about each column. */
    size_t column_cnt;          /* Number of columns. */

    GtkWidget *page;
    GtkWidget *custom_cb;
    GtkWidget *custom_entry;
    GtkWidget *quote_cb;
    GtkWidget *quote_combo;
    GtkEntry *quote_entry;
    GtkWidget *escape_cb;
    GtkTreeView *fields_tree_view;
  };
/* The columns that the separators divide the data into. */
struct column
  {
    /* Variable name for this column.  This is the variable name
       used on the separators page; it can be overridden by the
       user on the formats page. */
    char *name;

    /* Maximum length of any row in this column. */
    size_t width;

    /* Contents of this column: contents[row] is the contents for
       the given row.

       A null substring indicates a missing column for that row
       (because the line contains an insufficient number of
       separators).

       contents[] elements may be substrings of the lines[]
       strings that represent the whole lines of the file, to
       save memory.  Other elements are dynamically allocated
       with ss_alloc_substring. */
    struct substring *contents;
  };
static void init_separators_page (struct import_assistant *);
static void destroy_separators_page (struct import_assistant *);
static void prepare_separators_page (struct import_assistant *);
static void reset_separators_page (struct import_assistant *);

/* Page where the user verifies and adjusts input formats. */
struct formats_page
  {
    struct dictionary *dict;

    GtkWidget *page;
    GtkTreeView *data_tree_view;
    PsppireDict *psppire_dict;
    struct variable **modified_vars;
    size_t modified_var_cnt;
  };
static void init_formats_page (struct import_assistant *);
static void destroy_formats_page (struct import_assistant *);
static void prepare_formats_page (struct import_assistant *);
static void reset_formats_page (struct import_assistant *);

struct import_assistant
  {
    struct file file;
    struct assistant asst;
    struct intro_page intro;
    struct first_line_page first_line;
    struct separators_page separators;
    struct formats_page formats;
  };

static void apply_dict (const struct dictionary *, struct string *);
static char *generate_syntax (const struct import_assistant *);

static gboolean get_tooltip_location (GtkWidget *widget, gint wx, gint wy,
                                      const struct import_assistant *,
                                      size_t *row, size_t *column);
static void make_tree_view (const struct import_assistant *ia,
                            size_t first_line,
                            GtkTreeView **tree_view);
static void add_line_number_column (const struct import_assistant *,
                                    GtkTreeView *);
static gint get_monospace_width (GtkTreeView *, GtkCellRenderer *,
                                 size_t char_cnt);
static gint get_string_width (GtkTreeView *, GtkCellRenderer *,
                              const char *string);
static GtkTreeViewColumn *make_data_column (struct import_assistant *,
                                            GtkTreeView *, bool input,
                                            gint column_idx);
static GtkTreeView *create_data_tree_view (bool input, GtkContainer *parent,
                                           struct import_assistant *);
static void escape_underscores (const char *in, char *out);
static void push_watch_cursor (struct import_assistant *);
static void pop_watch_cursor (struct import_assistant *);

/* Pops up the Text Data Import assistant. */
void
text_data_import_assistant (GObject *o, GtkWindow *parent_window)
{
  struct import_assistant *ia;

  ia = xzalloc (sizeof *ia);
  if (!init_file (ia, parent_window))
    {
      free (ia);
      return;
    }

  init_assistant (ia, parent_window);
  init_intro_page (ia);
  init_first_line_page (ia);
  init_separators_page (ia);
  init_formats_page (ia);

  gtk_widget_show_all (GTK_WIDGET (ia->asst.assistant));

  ia->asst.main_loop = g_main_loop_new (NULL, false);
  g_main_loop_run (ia->asst.main_loop);
  g_main_loop_unref (ia->asst.main_loop);

  switch (ia->asst.response)
    {
    case GTK_RESPONSE_APPLY:
      {
	char *syntax = generate_syntax (ia);
	execute_syntax (create_syntax_string_source (syntax));
	free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	char *syntax = generate_syntax (ia);
        paste_syntax_in_new_window (syntax);
	free (syntax);
      }
      break;
    default:
      break;
    }

  destroy_formats_page (ia);
  destroy_separators_page (ia);
  destroy_assistant (ia);
  destroy_file (ia);
  free (ia);
}

/* Emits PSPP syntax to S that applies the dictionary attributes
   (such as missing values and value labels) of the variables in
   DICT.  */
static void
apply_dict (const struct dictionary *dict, struct string *s)
{
  size_t var_cnt = dict_get_var_cnt (dict);
  size_t i;

  for (i = 0; i < var_cnt; i++)
    {
      struct variable *var = dict_get_var (dict, i);
      const char *name = var_get_name (var);
      enum val_type type = var_get_type (var);
      int width = var_get_width (var);
      enum measure measure = var_get_measure (var);
      enum alignment alignment = var_get_alignment (var);
      const struct fmt_spec *format = var_get_print_format (var);

      if (var_has_missing_values (var))
        {
          const struct missing_values *mv = var_get_missing_values (var);
          size_t j;

          syntax_gen_pspp (s, "MISSING VALUES %ss (", name);
          for (j = 0; j < mv_n_values (mv); j++)
            {
              union value value;
              if (j)
                ds_put_cstr (s, ", ");
              mv_get_value (mv, &value, j);
              syntax_gen_value (s, &value, width, format);
            }

          if (mv_has_range (mv))
            {
              double low, high;
              if (mv_has_value (mv))
                ds_put_cstr (s, ", ");
              mv_get_range (mv, &low, &high);
              syntax_gen_num_range (s, low, high, format);
            }
          ds_put_cstr (s, ").\n");
        }
      if (var_has_value_labels (var))
        {
          const struct val_labs *vls = var_get_value_labels (var);
          struct val_labs_iterator *iter;
          struct val_lab *vl;

          syntax_gen_pspp (s, "VALUE LABELS %ss", name);
          for (vl = val_labs_first_sorted (vls, &iter); vl != NULL;
               vl = val_labs_next (vls, &iter))
            {
              ds_put_cstr (s, "\n  ");
              syntax_gen_value (s, &vl->value, width, format);
              ds_put_char (s, ' ');
              syntax_gen_string (s, ss_cstr (vl->label));
            }
          ds_put_cstr (s, ".\n");
        }
      if (var_has_label (var))
        syntax_gen_pspp (s, "VARIABLE LABELS %ss %sq.\n",
                         name, var_get_label (var));
      if (measure != var_default_measure (type))
        syntax_gen_pspp (s, "VARIABLE LEVEL %ss (%ss).\n",
                         name,
                         (measure == MEASURE_NOMINAL ? "NOMINAL"
                          : measure == MEASURE_ORDINAL ? "ORDINAL"
                          : "SCALE"));
      if (alignment != var_default_alignment (type))
        syntax_gen_pspp (s, "VARIABLE ALIGNMENT %ss (%ss).\n",
                         name,
                         (alignment == ALIGN_LEFT ? "LEFT"
                          : alignment == ALIGN_CENTRE ? "CENTER"
                          : "RIGHT"));
      if (var_get_display_width (var) != var_default_display_width (width))
        syntax_gen_pspp (s, "VARIABLE WIDTH %ss (%d).\n",
                         name, var_get_display_width (var));
    }
}

/* Generates and returns PSPP syntax to execute the import
   operation described by IA.  The caller must free the syntax
   with free(). */
static char *
generate_syntax (const struct import_assistant *ia)
{
  struct string s = DS_EMPTY_INITIALIZER;
  size_t var_cnt;
  size_t i;

  syntax_gen_pspp (&s,
                   "GET DATA\n"
                   "  /TYPE=TXT\n"
                   "  /FILE=%sq\n",
                   ia->file.file_name);
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
                                      ia->intro.n_cases_button)))
    ds_put_format (&s, "  /IMPORTCASES=FIRST %d\n",
                   gtk_spin_button_get_value_as_int (
                     GTK_SPIN_BUTTON (ia->intro.n_cases_spin)));
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (
                                           ia->intro.percent_button)))
    ds_put_format (&s, "  /IMPORTCASES=PERCENT %d\n",
                   gtk_spin_button_get_value_as_int (
                     GTK_SPIN_BUTTON (ia->intro.percent_spin)));
  else
    ds_put_cstr (&s, "  /IMPORTCASES=ALL\n");
  ds_put_cstr (&s,
               "  /ARRANGEMENT=DELIMITED\n"
               "  /DELCASE=LINE\n");
  if (ia->first_line.skip_lines > 0)
    ds_put_format (&s, "  /FIRSTCASE=%d\n", ia->first_line.skip_lines + 1);
  ds_put_cstr (&s, "  /DELIMITERS=\"");
  if (ds_find_char (&ia->separators.separators, '\t') != SIZE_MAX)
    ds_put_cstr (&s, "\\t");
  if (ds_find_char (&ia->separators.separators, '\\') != SIZE_MAX)
    ds_put_cstr (&s, "\\\\");
  for (i = 0; i < ds_length (&ia->separators.separators); i++)
    {
      char c = ds_at (&ia->separators.separators, i);
      if (c == '"')
        ds_put_cstr (&s, "\"\"");
      else if (c != '\t' && c != '\\')
        ds_put_char (&s, c);
    }
  ds_put_cstr (&s, "\"\n");
  if (!ds_is_empty (&ia->separators.quotes))
    syntax_gen_pspp (&s, "  /QUALIFIER=%sq\n", ds_cstr (&ia->separators.quotes));
  if (!ds_is_empty (&ia->separators.quotes) && ia->separators.escape)
    ds_put_cstr (&s, "  /ESCAPE\n");
  ds_put_cstr (&s, "  /VARIABLES=\n");

  var_cnt = dict_get_var_cnt (ia->formats.dict);
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *var = dict_get_var (ia->formats.dict, i);
      char format_string[FMT_STRING_LEN_MAX + 1];
      fmt_to_string (var_get_print_format (var), format_string);
      ds_put_format (&s, "    %s %s%s\n",
                     var_get_name (var), format_string,
                     i == var_cnt - 1 ? "." : "");
    }

  apply_dict (ia->formats.dict, &s);

  return ds_cstr (&s);
}

/* Choosing a file and reading it. */

static char *choose_file (GtkWindow *parent_window);

/* Obtains the file to import from the user and initializes IA's
   file substructure.  PARENT_WINDOW must be the window to use
   as the file chooser window's parent.

   Returns true if successful, false if the file name could not
   be obtained or the file could not be read. */
static bool
init_file (struct import_assistant *ia, GtkWindow *parent_window)
{
  struct file *file = &ia->file;
  enum { MAX_PREVIEW_LINES = 1000 }; /* Max number of lines to read. */
  enum { MAX_LINE_LEN = 16384 }; /* Max length of an acceptable line. */
  FILE *stream;

  file->file_name = choose_file (parent_window);
  if (file->file_name == NULL)
    return false;

  stream = fopen (file->file_name, "r");
  if (stream == NULL)
    {
      msg (ME, _("Could not open \"%s\": %s"),
           file->file_name, strerror (errno));
      return false;
    }

  file->lines = xnmalloc (MAX_PREVIEW_LINES, sizeof *file->lines);
  for (; file->line_cnt < MAX_PREVIEW_LINES; file->line_cnt++)
    {
      struct string *line = &file->lines[file->line_cnt];

      ds_init_empty (line);
      if (!ds_read_line (line, stream, MAX_LINE_LEN))
        {
          if (feof (stream))
            break;
          else if (ferror (stream))
            msg (ME, _("Error reading \"%s\": %s"),
                 file->file_name, strerror (errno));
          else
            msg (ME, _("Failed to read \"%s\", because it contains a line "
                       "over %d bytes long and therefore appears not to be "
                       "a text file."),
                 file->file_name, MAX_LINE_LEN);
          fclose (stream);
          destroy_file (ia);
          return false;
        }
      ds_chomp (line, '\n');
      ds_chomp (line, '\r');
    }

  if (file->line_cnt == 0)
    {
      msg (ME, _("\"%s\" is empty."), file->file_name);
      fclose (stream);
      destroy_file (ia);
      return false;
    }

  /* Estimate the number of lines in the file. */
  if (file->line_cnt < MAX_PREVIEW_LINES)
    file->total_lines = file->line_cnt;
  else
    {
      struct stat s;
      off_t position = ftello (stream);
      if (fstat (fileno (stream), &s) == 0 && position > 0)
        file->total_lines = (double) file->line_cnt / position * s.st_size;
      else
        file->total_lines = 0;
    }

  return true;
}

/* Frees IA's file substructure. */
static void
destroy_file (struct import_assistant *ia)
{
  struct file *f = &ia->file;
  size_t i;

  for (i = 0; i < f->line_cnt; i++)
    ds_destroy (&f->lines[i]);
  free (f->lines);
  g_free (f->file_name);
}

/* Obtains the file to read from the user and returns the name of
   the file as a string that must be freed with g_free if
   successful, otherwise a null pointer.  PARENT_WINDOW must be
   the window to use as the file chooser window's parent. */
static char *
choose_file (GtkWindow *parent_window)
{
  GtkWidget *dialog;
  char *file_name;

  dialog = gtk_file_chooser_dialog_new (_("Import Delimited Text Data"),
                                        parent_window,
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                        NULL);

  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_ACCEPT:
      file_name = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
      break;
    default:
      file_name = NULL;
      break;
    }
  gtk_widget_destroy (dialog);

  return file_name;
}

/* Assistant. */

static void close_assistant (struct import_assistant *, int response);
static void on_prepare (GtkAssistant *assistant, GtkWidget *page,
                        struct import_assistant *);
static void on_cancel (GtkAssistant *assistant, struct import_assistant *);
static void on_close (GtkAssistant *assistant, struct import_assistant *);
static void on_paste (GtkButton *button, struct import_assistant *);
static void on_reset (GtkButton *button, struct import_assistant *);
static void close_assistant (struct import_assistant *, int response);

/* Initializes IA's asst substructure.  PARENT_WINDOW must be the
   window to use as the assistant window's parent.  */
static void
init_assistant (struct import_assistant *ia, GtkWindow *parent_window)
{
  struct assistant *a = &ia->asst;

  a->builder = builder_new ("text-data-import.ui");
  a->assistant = GTK_ASSISTANT (gtk_assistant_new ());
  g_signal_connect (a->assistant, "prepare", G_CALLBACK (on_prepare), ia);
  g_signal_connect (a->assistant, "cancel", G_CALLBACK (on_cancel), ia);
  g_signal_connect (a->assistant, "close", G_CALLBACK (on_close), ia);
  a->paste_button = gtk_button_new_from_stock (GTK_STOCK_PASTE);
  gtk_assistant_add_action_widget (a->assistant, a->paste_button);
  g_signal_connect (a->paste_button, "clicked", G_CALLBACK (on_paste), ia);
  a->reset_button = gtk_button_new_from_stock ("pspp-stock-reset");
  gtk_assistant_add_action_widget (a->assistant, a->reset_button);
  g_signal_connect (a->reset_button, "clicked", G_CALLBACK (on_reset), ia);
  gtk_window_set_title (GTK_WINDOW (a->assistant),
                        _("Importing Delimited Text Data"));
  gtk_window_set_transient_for (GTK_WINDOW (a->assistant), parent_window);

  a->prop_renderer = gtk_cell_renderer_text_new ();
  g_object_ref_sink (a->prop_renderer);
  a->fixed_renderer = gtk_cell_renderer_text_new ();
  g_object_ref_sink (a->fixed_renderer);
  g_object_set (G_OBJECT (a->fixed_renderer),
                "family", "Monospace",
                (void *) NULL);
}

/* Frees IA's asst substructure. */
static void
destroy_assistant (struct import_assistant *ia)
{
  struct assistant *a = &ia->asst;

  g_object_unref (a->prop_renderer);
  g_object_unref (a->fixed_renderer);
  g_object_unref (a->builder);
}

/* Appends a page of the given TYPE, with PAGE as its content, to
   the GtkAssistant encapsulated by IA.  Returns the GtkWidget
   that represents the page. */
static GtkWidget *
add_page_to_assistant (struct import_assistant *ia,
                       GtkWidget *page, GtkAssistantPageType type)
{
  const char *title;
  char *title_copy;
  GtkWidget *content;

  title = gtk_window_get_title (GTK_WINDOW (page));
  title_copy = xstrdup (title ? title : "");

  content = gtk_bin_get_child (GTK_BIN (page));
  assert (content);
  g_object_ref (content);
  gtk_container_remove (GTK_CONTAINER (page), content);

  gtk_widget_destroy (page);

  gtk_assistant_append_page (ia->asst.assistant, content);
  gtk_assistant_set_page_type (ia->asst.assistant, content, type);
  gtk_assistant_set_page_title (ia->asst.assistant, content, title_copy);
  gtk_assistant_set_page_complete (ia->asst.assistant, content, true);

  free (title_copy);

  return content;
}

/* Called just before PAGE is displayed as the current page of
   ASSISTANT, this updates IA content according to the new
   page. */
static void
on_prepare (GtkAssistant *assistant, GtkWidget *page,
            struct import_assistant *ia)
{
  if (page == ia->separators.page)
    prepare_separators_page (ia);
  else if (page == ia->formats.page)
    prepare_formats_page (ia);

  gtk_widget_show (ia->asst.reset_button);
  if (page == ia->formats.page)
    gtk_widget_show (ia->asst.paste_button);
  else
    gtk_widget_hide (ia->asst.paste_button);
}

/* Called when the Cancel button in the assistant is clicked. */
static void
on_cancel (GtkAssistant *assistant, struct import_assistant *ia)
{
  close_assistant (ia, GTK_RESPONSE_CANCEL);
}

/* Called when the Apply button on the last page of the assistant
   is clicked. */
static void
on_close (GtkAssistant *assistant, struct import_assistant *ia)
{
  close_assistant (ia, GTK_RESPONSE_APPLY);
}

/* Called when the Paste button on the last page of the assistant
   is clicked. */
static void
on_paste (GtkButton *button, struct import_assistant *ia)
{
  close_assistant (ia, PSPPIRE_RESPONSE_PASTE);
}

/* Called when the Reset button is clicked. */
static void
on_reset (GtkButton *button, struct import_assistant *ia)
{
  gint page_num = gtk_assistant_get_current_page (ia->asst.assistant);
  GtkWidget *page = gtk_assistant_get_nth_page (ia->asst.assistant, page_num);

  if (page == ia->intro.page)
    reset_intro_page (ia);
  else if (page == ia->first_line.page)
    reset_first_line_page (ia);
  else if (page == ia->separators.page)
    reset_separators_page (ia);
  else if (page == ia->formats.page)
    reset_formats_page (ia);
}

/* Causes the assistant to close, returning RESPONSE for
   interpretation by text_data_import_assistant. */
static void
close_assistant (struct import_assistant *ia, int response)
{
  ia->asst.response = response;
  g_main_loop_quit (ia->asst.main_loop);
  gtk_widget_hide (GTK_WIDGET (ia->asst.assistant));
}

/* The "intro" page of the assistant. */

static void on_intro_amount_changed (GtkToggleButton *button,
                                     struct import_assistant *);

/* Initializes IA's intro substructure. */
static void
init_intro_page (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  struct intro_page *p = &ia->intro;
  struct string s;

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "Intro"),
                                   GTK_ASSISTANT_PAGE_INTRO);
  p->all_cases_button = get_widget_assert (builder, "import-all-cases");
  p->n_cases_button = get_widget_assert (builder, "import-n-cases");
  p->n_cases_spin = get_widget_assert (builder, "n-cases-spin");
  p->percent_button = get_widget_assert (builder, "import-percent");
  p->percent_spin = get_widget_assert (builder, "percent-spin");
  g_signal_connect (p->all_cases_button, "toggled",
                    G_CALLBACK (on_intro_amount_changed), ia);
  g_signal_connect (p->n_cases_button, "toggled",
                    G_CALLBACK (on_intro_amount_changed), ia);
  g_signal_connect (p->percent_button, "toggled",
                    G_CALLBACK (on_intro_amount_changed), ia);

  ds_init_empty (&s);
  ds_put_cstr (&s, _("This assistant will guide you through the process of "
                     "importing data into PSPP from a text file with one line "
                     "per case,  in which fields are separated by tabs, "
                     "commas, or other delimiters.\n\n"));
  if (ia->file.total_is_exact)
    ds_put_format (
      &s, ngettext ("The selected file contains %zu line of text.  ",
                    "The selected file contains %zu lines of text.  ",
                    ia->file.line_cnt),
      ia->file.line_cnt);
  else if (ia->file.total_lines > 0)
    {
      ds_put_format (
        &s, ngettext (
          "The selected file contains approximately %lu line of text.  ",
          "The selected file contains approximately %lu lines of text.  ",
          ia->file.total_lines),
        ia->file.total_lines);
      ds_put_format (
        &s, ngettext (
          "Only the first %zu line of the file will be shown for "
          "preview purposes in the following screens.  ",
          "Only the first %zu lines of the file will be shown for "
          "preview purposes in the following screens.  ",
          ia->file.line_cnt),
        ia->file.line_cnt);
    }
  ds_put_cstr (&s, _("You may choose below how much of the file should "
                     "actually be imported."));
  gtk_label_set_text (GTK_LABEL (get_widget_assert (builder, "intro-label")),
                      ds_cstr (&s));
  ds_destroy (&s);
}

/* Resets IA's intro page to its initial state. */
static void
reset_intro_page (struct import_assistant *ia)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->intro.all_cases_button),
                                true);
}

/* Called when one of the radio buttons is clicked. */
static void
on_intro_amount_changed (GtkToggleButton *button UNUSED,
                         struct import_assistant *ia)
{
  struct intro_page *p = &ia->intro;

  gtk_widget_set_sensitive (p->n_cases_spin,
                            gtk_toggle_button_get_active (
                              GTK_TOGGLE_BUTTON (p->n_cases_button)));

  gtk_widget_set_sensitive (ia->intro.percent_spin,
                            gtk_toggle_button_get_active (
                              GTK_TOGGLE_BUTTON (p->percent_button)));
}

/* The "first line" page of the assistant. */

static GtkTreeView *create_lines_tree_view (GtkContainer *parent_window,
                                            struct import_assistant *);
static void on_first_line_change (GtkTreeSelection *,
                                  struct import_assistant *);
static void on_variable_names_cb_toggle (GtkToggleButton *,
                                         struct import_assistant *);
static void set_first_line (struct import_assistant *);
static void get_first_line (struct import_assistant *);

/* Initializes IA's first_line substructure. */
static void
init_first_line_page (struct import_assistant *ia)
{
  struct first_line_page *p = &ia->first_line;
  GtkBuilder *builder = ia->asst.builder;

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "FirstLine"),
                                   GTK_ASSISTANT_PAGE_CONTENT);
  gtk_widget_destroy (get_widget_assert (builder, "first-line"));
  p->tree_view = create_lines_tree_view (
    GTK_CONTAINER (get_widget_assert (builder, "first-line-scroller")), ia);
  p->variable_names_cb = get_widget_assert (builder, "variable-names");
  gtk_tree_selection_set_mode (
    gtk_tree_view_get_selection (GTK_TREE_VIEW (p->tree_view)),
    GTK_SELECTION_BROWSE);
  set_first_line (ia);
  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (p->tree_view)),
                    "changed", G_CALLBACK (on_first_line_change), ia);
  g_signal_connect (p->variable_names_cb, "toggled",
                    G_CALLBACK (on_variable_names_cb_toggle), ia);
}

/* Resets the first_line page to its initial content. */
static void
reset_first_line_page (struct import_assistant *ia)
{
  ia->first_line.skip_lines = 0;
  ia->first_line.variable_names = false;
  set_first_line (ia);
}

/* Creates and returns a tree view that contains each of the
   lines in IA's file as a row. */
static GtkTreeView *
create_lines_tree_view (GtkContainer *parent, struct import_assistant *ia)
{
  GtkTreeView *tree_view;
  GtkTreeViewColumn *column;
  size_t max_line_length;
  gint content_width, header_width;
  size_t i;

  make_tree_view (ia, 0, &tree_view);

  column = gtk_tree_view_column_new_with_attributes (
    "Text", ia->asst.fixed_renderer,
    "text", TEXT_IMPORT_MODEL_COLUMN_LINE,
    (void *) NULL);
  gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);

  max_line_length = 0;
  for (i = 0; i < ia->file.line_cnt; i++)
    {
      size_t w = ds_length (&ia->file.lines[i]);
      max_line_length = MAX (max_line_length, w);
    }

  content_width = get_monospace_width (tree_view, ia->asst.fixed_renderer,
                                       max_line_length);
  header_width = get_string_width (tree_view, ia->asst.prop_renderer, "Text");
  gtk_tree_view_column_set_fixed_width (column, MAX (content_width,
                                                     header_width));
  gtk_tree_view_append_column (tree_view, column);

  gtk_tree_view_set_fixed_height_mode (tree_view, true);

  gtk_container_add (parent, GTK_WIDGET (tree_view));
  gtk_widget_show (GTK_WIDGET (tree_view));

  return tree_view;
}

/* Called when the line selected in the first_line tree view
   changes. */
static void
on_first_line_change (GtkTreeSelection *selection UNUSED,
                      struct import_assistant *ia)
{
  get_first_line (ia);
}

/* Called when the checkbox that indicates whether variable
   names are in the row above the first line is toggled. */
static void
on_variable_names_cb_toggle (GtkToggleButton *variable_names_cb UNUSED,
                             struct import_assistant *ia)
{
  get_first_line (ia);
}

/* Sets the widgets to match IA's first_line substructure. */
static void
set_first_line (struct import_assistant *ia)
{
  GtkTreePath *path;

  path = gtk_tree_path_new_from_indices (ia->first_line.skip_lines, -1);
  gtk_tree_view_set_cursor (GTK_TREE_VIEW (ia->first_line.tree_view),
                            path, NULL, false);
  gtk_tree_path_free (path);

  gtk_toggle_button_set_active (
    GTK_TOGGLE_BUTTON (ia->first_line.variable_names_cb),
    ia->first_line.variable_names);
  gtk_widget_set_sensitive (ia->first_line.variable_names_cb,
                            ia->first_line.skip_lines > 0);
}

/* Sets IA's first_line substructure to match the widgets. */
static void
get_first_line (struct import_assistant *ia)
{
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GtkTreeModel *model;

  selection = gtk_tree_view_get_selection (ia->first_line.tree_view);
  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
      int row = gtk_tree_path_get_indices (path)[0];
      gtk_tree_path_free (path);

      ia->first_line.skip_lines = row;
      ia->first_line.variable_names =
        (ia->first_line.skip_lines > 0
         && gtk_toggle_button_get_active (
           GTK_TOGGLE_BUTTON (ia->first_line.variable_names_cb)));
    }
  gtk_widget_set_sensitive (ia->first_line.variable_names_cb,
                            ia->first_line.skip_lines > 0);
}

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
static void render_input_cell (GtkTreeViewColumn *tree_column,
                               GtkCellRenderer *cell,
                               GtkTreeModel *model, GtkTreeIter *iter,
                               gpointer ia);
static gboolean on_query_input_tooltip (GtkWidget *widget, gint wx, gint wy,
                                        gboolean keyboard_mode UNUSED,
                                        GtkTooltip *tooltip,
                                        struct import_assistant *);

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

  gtk_combo_box_entry_set_text_column (cb, 0);
}

/* Initializes IA's separators substructure. */
static void
init_separators_page (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  struct separators_page *p = &ia->separators;
  size_t i;

  choose_likely_separators (ia);

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "Separators"),
                                   GTK_ASSISTANT_PAGE_CONTENT);
  p->custom_cb = get_widget_assert (builder, "custom-cb");
  p->custom_entry = get_widget_assert (builder, "custom-entry");
  p->quote_combo = get_widget_assert (builder, "quote-combo");
  p->quote_entry = GTK_ENTRY (gtk_bin_get_child (GTK_BIN (p->quote_combo)));
  p->quote_cb = get_widget_assert (builder, "quote-cb");
  p->escape_cb = get_widget_assert (builder, "escape");

  set_separators (ia);
  set_quote_list (GTK_COMBO_BOX_ENTRY (p->quote_combo));
  p->fields_tree_view = GTK_TREE_VIEW (get_widget_assert (builder, "fields"));
  g_signal_connect (GTK_COMBO_BOX (p->quote_combo), "changed",
                    G_CALLBACK (on_quote_combo_change), ia);
  g_signal_connect (p->quote_cb, "toggled",
                    G_CALLBACK (on_quote_cb_toggle), ia);
  g_signal_connect (GTK_ENTRY (p->custom_entry), "notify::text",
                    G_CALLBACK (on_separators_custom_entry_notify), ia);
  g_signal_connect (p->custom_cb, "toggled",
                    G_CALLBACK (on_separators_custom_cb_toggle), ia);
  for (i = 0; i < SEPARATOR_CNT; i++)
    g_signal_connect (get_widget_assert (builder, separators[i].name),
                      "toggled", G_CALLBACK (on_separator_toggle), ia);
  g_signal_connect (p->escape_cb, "toggled",
                    G_CALLBACK (on_separator_toggle), ia);
}

/* Frees IA's separators substructure. */
static void
destroy_separators_page (struct import_assistant *ia)
{
  struct separators_page *s = &ia->separators;

  ds_destroy (&s->separators);
  ds_destroy (&s->quotes);
  clear_fields (ia);
}

/* Called just before the separators page becomes visible in the
   assistant. */
static void
prepare_separators_page (struct import_assistant *ia)
{
  revise_fields_preview (ia);
}

/* Called when the Reset button is clicked on the separators
   page, resets the separators to the defaults. */
static void
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
  struct separators_page *s = &ia->separators;

  if (s->column_cnt > 0)
    {
      struct column *col;
      size_t row;

      for (row = 0; row < ia->file.line_cnt; row++)
        {
          const struct string *line = &ia->file.lines[row];
          const char *line_start = ds_data (line);
          const char *line_end = ds_end (line);

          for (col = s->columns; col < &s->columns[s->column_cnt]; col++)
            {
              char *s = ss_data (col->contents[row]);
              if (!(s >= line_start && s <= line_end))
                ss_dealloc (&col->contents[row]);
            }
        }

      for (col = s->columns; col < &s->columns[s->column_cnt]; col++)
        {
          free (col->name);
          free (col->contents);
        }

      free (s->columns);
      s->columns = NULL;
      s->column_cnt = 0;
    }
}

/* Breaks the file data in IA into columns based on the
   separators set in IA's separators substructure. */
static void
split_fields (struct import_assistant *ia)
{
  struct separators_page *s = &ia->separators;
  size_t columns_allocated;
  bool space_sep;
  size_t row;

  clear_fields (ia);

  /* Is space in the set of separators? */
  space_sep = ss_find_char (ds_ss (&s->separators), ' ') != SIZE_MAX;

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
                   && ds_find_char (&s->quotes, text.string[0]) != SIZE_MAX)
            {
              int quote = ss_get_char (&text);
              if (!s->escape)
                ss_get_until (&text, quote, &field);
              else
                {
                  struct string s;
                  int c;

                  ds_init_empty (&s);
                  while ((c = ss_get_char (&text)) != EOF)
                    if (c != quote)
                      ds_put_char (&s, c);
                    else if (ss_match_char (&text, quote))
                      ds_put_char (&s, quote);
                    else
                      break;
                  field = ds_ss (&s);
                }
            }
          else
            ss_get_chars (&text, ss_cspan (text, ds_ss (&s->separators)),
                          &field);

          if (column_idx >= s->column_cnt)
            {
              struct column *column;

              if (s->column_cnt >= columns_allocated)
                s->columns = x2nrealloc (s->columns, &columns_allocated,
                                         sizeof *s->columns);
              column = &s->columns[s->column_cnt++];
              column->name = NULL;
              column->width = 0;
              column->contents = xcalloc (ia->file.line_cnt,
                                          sizeof *column->contents);
            }
          column = &s->columns[column_idx];
          column->contents[row] = field;
          if (ss_length (field) > column->width)
            column->width = ss_length (field);

          if (space_sep)
            ss_ltrim (&text, ss_cstr (" "));
          if (ss_is_empty (text))
            break;
          if (ss_find_char (ds_ss (&s->separators), ss_first (text))
              != SIZE_MAX)
            ss_advance (&text, 1);
        }
    }
}

/* Chooses a name for each column on the separators page */
static void
choose_column_names (struct import_assistant *ia)
{
  const struct first_line_page *f = &ia->first_line;
  struct separators_page *s = &ia->separators;
  struct dictionary *dict;
  unsigned long int generated_name_count = 0;
  struct column *col;
  size_t name_row;

  dict = dict_create ();
  name_row = f->variable_names && f->skip_lines ? f->skip_lines : 0;
  for (col = s->columns; col < &s->columns[s->column_cnt]; col++)
    {
      char name[VAR_NAME_LEN + 1];
      char *hint;

      hint = name_row ? ss_xstrdup (col->contents[name_row - 1]) : NULL;
      if (!dict_make_unique_var_name (dict, hint, &generated_name_count, name))
        NOT_REACHED ();
      free (hint);

      col->name = xstrdup (name);
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

  find_commonest_chars (histogram, "\"'", "", &ia->separators.quotes);
  find_commonest_chars (histogram, ",;:/|!\t-", ",",
                        &ia->separators.separators);
  ia->separators.escape = true;
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
      ds_put_char (result, max);
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

  w = GTK_WIDGET (ia->separators.fields_tree_view);
  gtk_widget_destroy (w);
  get_separators (ia);
  split_fields (ia);
  choose_column_names (ia);
  ia->separators.fields_tree_view = create_data_tree_view (
    true,
    GTK_CONTAINER (get_widget_assert (ia->asst.builder, "fields-scroller")),
    ia);

  pop_watch_cursor (ia);
}

/* Sets the widgets to match IA's separators substructure. */
static void
set_separators (struct import_assistant *ia)
{
  struct separators_page *s = &ia->separators;
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

      ds_put_char (&custom, c);
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
  struct separators_page *s = &ia->separators;
  int i;

  ds_clear (&s->separators);
  for (i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *sep = &separators[i];
      GtkWidget *button = get_widget_assert (ia->asst.builder, sep->name);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
        ds_put_char (&s->separators, sep->c);
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
  gtk_widget_set_sensitive (ia->separators.custom_entry, is_active);
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
  gtk_widget_set_sensitive (ia->separators.quote_combo, is_active);
  gtk_widget_set_sensitive (ia->separators.escape_cb, is_active);
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

/* Called to render one of the cells in the fields preview tree
   view. */
static void
render_input_cell (GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
                   GtkTreeModel *model, GtkTreeIter *iter,
                   gpointer ia_)
{
  struct import_assistant *ia = ia_;
  struct substring field;
  size_t row;
  gint column;

  column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                               "column-number"));
  row = text_import_model_iter_to_row (iter) + ia->first_line.skip_lines;
  field = ia->separators.columns[column].contents[row];
  if (field.string != NULL)
    {
      GValue text = {0, };
      g_value_init (&text, G_TYPE_STRING);
      g_value_take_string (&text, ss_xstrdup (field));
      g_object_set_property (G_OBJECT (cell), "text", &text);
      g_value_unset (&text);
      g_object_set (cell, "background-set", FALSE, (void *) NULL);
    }
  else
    g_object_set (cell,
                  "text", "",
                  "background", "red",
                  "background-set", TRUE,
                  (void *) NULL);
}

/* Called to render a tooltip on one of the cells in the fields
   preview tree view. */
static gboolean
on_query_input_tooltip (GtkWidget *widget, gint wx, gint wy,
                        gboolean keyboard_mode UNUSED,
                        GtkTooltip *tooltip, struct import_assistant *ia)
{
  size_t row, column;

  if (!get_tooltip_location (widget, wx, wy, ia, &row, &column))
    return FALSE;

  if (ia->separators.columns[column].contents[row].string != NULL)
    return FALSE;

  gtk_tooltip_set_text (tooltip,
                        _("This input line has too few separators "
                          "to fill in this field."));
  return TRUE;
}

/* The "formats" page of the assistant. */

static void on_variable_change (PsppireDict *dict, int idx,
                                struct import_assistant *);
static void clear_modified_vars (struct import_assistant *);

/* Initializes IA's formats substructure. */
static void
init_formats_page (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  struct formats_page *p = &ia->formats;

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "Formats"),
                                   GTK_ASSISTANT_PAGE_CONFIRM);
  p->data_tree_view = GTK_TREE_VIEW (get_widget_assert (builder, "data"));
  p->modified_vars = NULL;
  p->modified_var_cnt = 0;
}

/* Frees IA's formats substructure. */
static void
destroy_formats_page (struct import_assistant *ia)
{
  struct formats_page *p = &ia->formats;

  if (p->psppire_dict != NULL)
    {
      /* This destroys p->dict also. */
      g_object_unref (p->psppire_dict);
    }
  clear_modified_vars (ia);
}

/* Called just before the formats page of the assistant is
   displayed. */
static void
prepare_formats_page (struct import_assistant *ia)
{
  struct dictionary *dict;
  PsppireDict *psppire_dict;
  PsppireVarStore *var_store;
  GtkBin *vars_scroller;
  GtkWidget *old_var_sheet;
  PsppireVarSheet *var_sheet;
  struct separators_page *s = &ia->separators;
  struct formats_page *p = &ia->formats;
  struct fmt_guesser *fg;
  unsigned long int number = 0;
  size_t column_idx;

  push_watch_cursor (ia);

  dict = dict_create ();
  fg = fmt_guesser_create ();
  for (column_idx = 0; column_idx < s->column_cnt; column_idx++)
    {
      struct variable *modified_var;
      char name[VAR_NAME_LEN + 1];

      modified_var = (column_idx < p->modified_var_cnt
                      ? p->modified_vars[column_idx] : NULL);
      if (modified_var == NULL)
        {
          struct column *column = &s->columns[column_idx];
          struct variable *var;
          struct fmt_spec format;
          size_t row;

          /* Choose variable name. */
          if (!dict_make_unique_var_name (dict, column->name, &number, name))
            NOT_REACHED ();

          /* Choose variable format. */
          fmt_guesser_clear (fg);
          for (row = ia->first_line.skip_lines; row < ia->file.line_cnt; row++)
            fmt_guesser_add (fg, column->contents[row]);
          fmt_guesser_guess (fg, &format);
          fmt_fix_input (&format);

          /* Create variable. */
          var = dict_create_var_assert (dict, name, fmt_var_width (&format));
          var_set_both_formats (var, &format);
        }
      else
        {
          if (!dict_make_unique_var_name (dict, var_get_name (modified_var),
                                          &number, name))
            NOT_REACHED ();
          dict_clone_var_assert (dict, modified_var, name);
        }
    }
  fmt_guesser_destroy (fg);

  psppire_dict = psppire_dict_new_from_dict (dict);
  g_signal_connect (psppire_dict, "variable_changed",
                    G_CALLBACK (on_variable_change), ia);
  ia->formats.dict = dict;
  ia->formats.psppire_dict = psppire_dict;

  /* XXX: PsppireVarStore doesn't hold a reference to
     psppire_dict for now, but it should.  After it does, we
     should g_object_ref the psppire_dict here, since we also
     hold a reference via ia->formats.dict. */
  var_store = psppire_var_store_new (psppire_dict);
  g_object_set (var_store,
                "format-type", PSPPIRE_VAR_STORE_INPUT_FORMATS,
                (void *) NULL);
  var_sheet = PSPPIRE_VAR_SHEET (psppire_var_sheet_new ());
  g_object_set (var_sheet,
                "model", var_store,
                "may-create-vars", FALSE,
                (void *) NULL);

  vars_scroller = GTK_BIN (get_widget_assert (ia->asst.builder, "vars-scroller"));
  old_var_sheet = gtk_bin_get_child (vars_scroller);
  if (old_var_sheet != NULL)
    gtk_widget_destroy (old_var_sheet);
  gtk_container_add (GTK_CONTAINER (vars_scroller), GTK_WIDGET (var_sheet));
  gtk_widget_show (GTK_WIDGET (var_sheet));

  gtk_widget_destroy (GTK_WIDGET (ia->formats.data_tree_view));
  ia->formats.data_tree_view = create_data_tree_view (
    false,
    GTK_CONTAINER (get_widget_assert (ia->asst.builder, "data-scroller")),
    ia);

  pop_watch_cursor (ia);
}

/* Clears the set of user-modified variables from IA's formats
   substructure.  This discards user modifications to variable
   formats, thereby causing formats to revert to their
   defaults.  */
static void
clear_modified_vars (struct import_assistant *ia)
{
  struct formats_page *p = &ia->formats;
  size_t i;

  for (i = 0; i < p->modified_var_cnt; i++)
    var_destroy (p->modified_vars[i]);
  free (p->modified_vars);
  p->modified_vars = NULL;
  p->modified_var_cnt = 0;
}

/* Resets the formats page to its defaults, discarding user
   modifications. */
static void
reset_formats_page (struct import_assistant *ia)
{
  clear_modified_vars (ia);
  prepare_formats_page (ia);
}

/* Called when the user changes one of the variables in the
   dictionary. */
static void
on_variable_change (PsppireDict *dict, int dict_idx,
                    struct import_assistant *ia)
{
  struct formats_page *p = &ia->formats;
  GtkTreeView *tv = ia->formats.data_tree_view;
  gint column_idx = dict_idx + 1;

  push_watch_cursor (ia);

  /* Remove previous column and replace with new column. */
  gtk_tree_view_remove_column (tv, gtk_tree_view_get_column (tv, column_idx));
  gtk_tree_view_insert_column (tv, make_data_column (ia, tv, false, dict_idx),
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

/* Parses the contents of the field at (ROW,COLUMN) according to
   its variable format.  If OUTPUTP is non-null, then *OUTPUTP
   receives the formatted output for that field (which must be
   freed with free).  If TOOLTIPP is non-null, then *TOOLTIPP
   receives a message suitable for use in a tooltip, if one is
   needed, or a null pointer otherwise.  Returns true if a
   tooltip message is needed, otherwise false. */
static bool
parse_field (struct import_assistant *ia,
             size_t row, size_t column,
             char **outputp, char **tooltipp)
{
  struct substring field;
  union value *val;
  struct variable *var;
  const struct fmt_spec *in;
  struct fmt_spec out;
  char *tooltip;
  bool ok;

  field = ia->separators.columns[column].contents[row];
  var = dict_get_var (ia->formats.dict, column);
  val = value_create (var_get_width (var));
  in = var_get_print_format (var);
  out = fmt_for_output_from_input (in);
  tooltip = NULL;
  if (field.string != NULL)
    {
      msg_disable ();
      if (!data_in (field, LEGACY_NATIVE, in->type, 0, 0, 0,
                    val, var_get_width (var)))
        {
          char fmt_string[FMT_STRING_LEN_MAX + 1];
          fmt_to_string (in, fmt_string);
          tooltip = xasprintf (_("Field content \"%.*s\" cannot be parsed in "
                                 "format %s."),
                               (int) field.length, field.string,
                               fmt_string);
        }
      msg_enable ();
    }
  else
    {
      tooltip = xstrdup (_("This input line has too few separators "
                           "to fill in this field."));
      value_set_missing (val, var_get_width (var));
    }
  if (outputp != NULL)
    {
      char *output = xmalloc (out.w + 1);
      data_out (val, &out, output);
      output[out.w] = '\0';
      *outputp = output;
    }
  free (val);

  ok = tooltip == NULL;
  if (tooltipp != NULL)
    *tooltipp = tooltip;
  else
    free (tooltip);
  return ok;
}

/* Called to render one of the cells in the data preview tree
   view. */
static void
render_output_cell (GtkTreeViewColumn *tree_column,
                    GtkCellRenderer *cell,
                    GtkTreeModel *model,
                    GtkTreeIter *iter,
                    gpointer ia_)
{
  struct import_assistant *ia = ia_;
  char *output;
  GValue gvalue = { 0, };
  bool ok;

  ok = parse_field (ia,
                    (text_import_model_iter_to_row (iter)
                     + ia->first_line.skip_lines),
                    GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                                        "column-number")),
                    &output, NULL);

  g_value_init (&gvalue, G_TYPE_STRING);
  g_value_take_string (&gvalue, output);
  g_object_set_property (G_OBJECT (cell), "text", &gvalue);
  g_value_unset (&gvalue);

  if (ok)
    g_object_set (cell, "background-set", FALSE, (void *) NULL);
  else
    g_object_set (cell,
                  "background", "red",
                  "background-set", TRUE,
                  (void *) NULL);
}

/* Called to render a tooltip for one of the cells in the data
   preview tree view. */
static gboolean
on_query_output_tooltip (GtkWidget *widget, gint wx, gint wy,
                        gboolean keyboard_mode UNUSED,
                        GtkTooltip *tooltip, struct import_assistant *ia)
{
  size_t row, column;
  char *text;

  if (!get_tooltip_location (widget, wx, wy, ia, &row, &column))
    return FALSE;

  if (parse_field (ia, row, column, NULL, &text))
    return FALSE;

  gtk_tooltip_set_text (tooltip, text);
  free (text);
  return TRUE;
}

/* Utility functions used by multiple pages of the assistant. */

static gboolean
get_tooltip_location (GtkWidget *widget, gint wx, gint wy,
                      const struct import_assistant *ia,
                      size_t *row, size_t *column)
{
  GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
  gint bx, by;
  GtkTreePath *path;
  GtkTreeIter iter;
  GtkTreeViewColumn *tree_column;
  GtkTreeModel *tree_model;
  bool ok;

  /* Check that WIDGET is really visible on the screen before we
     do anything else.  This is a bug fix for a sticky situation:
     when text_data_import_assistant() returns, it frees the data
     necessary to compose the tool tip message, but there may be
     a tool tip under preparation at that point (even if there is
     no visible tool tip) that will call back into us a little
     bit later.  Perhaps the correct solution to this problem is
     to make the data related to the tool tips part of a GObject
     that only gets destroyed when all references are released,
     but this solution appears to be effective too. */
  if (!GTK_WIDGET_MAPPED (widget))
    return FALSE;

  gtk_tree_view_convert_widget_to_bin_window_coords (tree_view,
                                                     wx, wy, &bx, &by);
  if (!gtk_tree_view_get_path_at_pos (tree_view, bx, by,
                                      &path, &tree_column, NULL, NULL))
    return FALSE;

  *column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                                "column-number"));

  tree_model = gtk_tree_view_get_model (tree_view);
  ok = gtk_tree_model_get_iter (tree_model, &iter, path);
  gtk_tree_path_free (path);
  if (!ok)
    return FALSE;

  *row = text_import_model_iter_to_row (&iter) + ia->first_line.skip_lines;
  return TRUE;
}

static void
make_tree_view (const struct import_assistant *ia,
                size_t first_line,
                GtkTreeView **tree_view)
{
  GtkTreeModel *model;

  *tree_view = GTK_TREE_VIEW (gtk_tree_view_new ());
  model = GTK_TREE_MODEL (text_import_model_new (
                            ia->file.lines + first_line,
                            ia->file.line_cnt - first_line, first_line));
  gtk_tree_view_set_model (*tree_view, model);

  add_line_number_column (ia, *tree_view);
}

static void
add_line_number_column (const struct import_assistant *ia,
                        GtkTreeView *treeview)
{
  GtkTreeViewColumn *column;

  column = gtk_tree_view_column_new_with_attributes (
    "Line", ia->asst.prop_renderer,
    "text", TEXT_IMPORT_MODEL_COLUMN_LINE_NUMBER,
    (void *) NULL);
  gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_fixed_width (
    column, get_monospace_width (treeview, ia->asst.prop_renderer, 5));
  gtk_tree_view_append_column (treeview, column);
}

static gint
get_monospace_width (GtkTreeView *treeview, GtkCellRenderer *renderer,
                     size_t char_cnt)
{
  struct string s;
  gint width;

  ds_init_empty (&s);
  ds_put_char_multiple (&s, '0', char_cnt);
  ds_put_char (&s, ' ');
  width = get_string_width (treeview, renderer, ds_cstr (&s));
  ds_destroy (&s);

  return width;
}

static gint
get_string_width (GtkTreeView *treeview, GtkCellRenderer *renderer,
                  const char *string)
{
  gint width;
  g_object_set (G_OBJECT (renderer), "text", string, (void *) NULL);
  gtk_cell_renderer_get_size (renderer, GTK_WIDGET (treeview),
                              NULL, NULL, NULL, &width, NULL);
  return width;
}

static GtkTreeViewColumn *
make_data_column (struct import_assistant *ia, GtkTreeView *tree_view,
                  bool input, gint dict_idx)
{
  struct variable *var = NULL;
  struct column *column = NULL;
  char name[(VAR_NAME_LEN * 2) + 1];
  size_t char_cnt;
  gint content_width, header_width;
  GtkTreeViewColumn *tree_column;

  if (input)
    column = &ia->separators.columns[dict_idx];
  else
    var = dict_get_var (ia->formats.dict, dict_idx);

  escape_underscores (input ? column->name : var_get_name (var), name);
  char_cnt = input ? column->width : var_get_print_format (var)->w;
  content_width = get_monospace_width (tree_view, ia->asst.fixed_renderer,
                                       char_cnt);
  header_width = get_string_width (tree_view, ia->asst.prop_renderer,
                                   name);

  tree_column = gtk_tree_view_column_new ();
  g_object_set_data (G_OBJECT (tree_column), "column-number",
                     GINT_TO_POINTER (dict_idx));
  gtk_tree_view_column_set_title (tree_column, name);
  gtk_tree_view_column_pack_start (tree_column, ia->asst.fixed_renderer,
                                   FALSE);
  gtk_tree_view_column_set_cell_data_func (
    tree_column, ia->asst.fixed_renderer,
    input ? render_input_cell : render_output_cell, ia, NULL);
  gtk_tree_view_column_set_sizing (tree_column, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_fixed_width (tree_column, MAX (content_width,
                                                          header_width));

  return tree_column;
}

static GtkTreeView *
create_data_tree_view (bool input, GtkContainer *parent,
                       struct import_assistant *ia)
{
  GtkTreeView *tree_view;
  gint i;

  make_tree_view (ia, ia->first_line.skip_lines, &tree_view);
  gtk_tree_selection_set_mode (gtk_tree_view_get_selection (tree_view),
                               GTK_SELECTION_NONE);

  for (i = 0; i < ia->separators.column_cnt; i++)
    gtk_tree_view_append_column (tree_view,
                                 make_data_column (ia, tree_view, input, i));

  g_object_set (G_OBJECT (tree_view), "has-tooltip", TRUE, (void *) NULL);
  g_signal_connect (tree_view, "query-tooltip",
                    G_CALLBACK (input ? on_query_input_tooltip
                                : on_query_output_tooltip), ia);
  gtk_tree_view_set_fixed_height_mode (tree_view, true);

  gtk_container_add (parent, GTK_WIDGET (tree_view));
  gtk_widget_show (GTK_WIDGET (tree_view));

  return tree_view;
}

static void
escape_underscores (const char *in, char *out)
{
  for (; *in != '\0'; in++)
    {
      if (*in == '_')
        *out++ = '_';
      *out++ = *in;
    }
  *out = '\0';
}

/* TextImportModel, a GtkTreeModel implementation used by some
   pages of the assistant. */

#define G_TYPE_TEXT_IMPORT_MODEL (text_import_model_get_type ())
#define TEXT_IMPORT_MODEL(object) (G_TYPE_CHECK_INSTANCE_CAST ((object), G_TYPE_TEXT_IMPORT_MODEL, TextImportModel))
#define TEXT_IMPORT_MODEL_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_TEXT_IMPORT_MODEL, TextImportModelClass))
#define IS_TEXT_IMPORT_MODEL(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), G_TYPE_TEXT_IMPORT_MODEL))
#define IS_TEXT_IMPORT_MODEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_TEXT_IMPORT_MODEL))
#define TEXT_IMPORT_MODEL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_TEXT_IMPORT_MODEL, TextImportModelClass))

/* Random number used in 'stamp' member of GtkTreeIter. */
#define TREE_MODEL_STAMP 0x7efd67d3

struct TextImportModel
{
  GObject             parent;
  struct string *lines;
  size_t line_cnt;
  size_t first_line;
};

struct TextImportModelClass
{
  GObjectClass parent_class;
};

GType text_import_model_get_type (void);
static void text_import_model_tree_model_init (gpointer iface, gpointer data);

GType
text_import_model_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info = {
	sizeof (TextImportModelClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	NULL,   /* class_init */
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (TextImportModel),
	0,      /* n_preallocs */
	NULL,   /* instance_init */
      };

      static const GInterfaceInfo tree_model_info = {
	text_import_model_tree_model_init,
	NULL,
	NULL
      };

      object_type = g_type_register_static (G_TYPE_OBJECT,
					    "TextImportModel",
					    &object_info, 0);

      g_type_add_interface_static (object_type, GTK_TYPE_TREE_MODEL,
				   &tree_model_info);


    }

  return object_type;
}


/* Creates and returns a new TextImportModel that contains the
   LINE_CNT lines in LINES.  The lines before FIRST_LINE in LINES
   are not part of the model, but they are included in the line
   numbers in the TEXT_IMPORT_MODEL_COLUMN_LINE_NUMBER column.

   The caller retains responsibility for freeing LINES and must
   ensure that its lifetime and that of the strings that it
   contains exceeds that of the TextImportModel. */
TextImportModel *
text_import_model_new (struct string *lines, size_t line_cnt,
                       size_t first_line)
{
  TextImportModel *new_text_import_model
    = g_object_new (G_TYPE_TEXT_IMPORT_MODEL, NULL);
  new_text_import_model->lines = lines;
  new_text_import_model->line_cnt = line_cnt;
  new_text_import_model->first_line = first_line;
  return new_text_import_model;
}


static gboolean
tree_model_iter_has_child  (GtkTreeModel *tree_model,
			    GtkTreeIter  *iter)
{
  return FALSE;
}

static gboolean
tree_model_iter_parent (GtkTreeModel *tree_model,
		        GtkTreeIter *iter,
		        GtkTreeIter *child)
{
  return TRUE;
}

static GtkTreeModelFlags
tree_model_get_flags (GtkTreeModel *model)
{
  g_return_val_if_fail (IS_TEXT_IMPORT_MODEL (model), (GtkTreeModelFlags) 0);

  return GTK_TREE_MODEL_LIST_ONLY | GTK_TREE_MODEL_ITERS_PERSIST;
}


static gint
tree_model_n_columns (GtkTreeModel *model)
{
  return 2;
}

static GType
tree_model_column_type (GtkTreeModel *model, gint index)
{
  return (index == TEXT_IMPORT_MODEL_COLUMN_LINE_NUMBER ? G_TYPE_INT
          : index == TEXT_IMPORT_MODEL_COLUMN_LINE ? G_TYPE_STRING
          : -1);
}

static gboolean
init_iter (TextImportModel *list, gint idx, GtkTreeIter *iter)
{
  if (idx < 0 || idx >= list->line_cnt)
    {
      iter->stamp = 0;
      iter->user_data = GINT_TO_POINTER (-1);
      return FALSE;
    }
  else
    {
      iter->stamp = TREE_MODEL_STAMP;
      iter->user_data = GINT_TO_POINTER (idx);
      return TRUE;
    }
}

static gboolean
tree_model_get_iter (GtkTreeModel *model, GtkTreeIter *iter, GtkTreePath *path)
{
  gint *indices, depth;

  TextImportModel *list = TEXT_IMPORT_MODEL (model);

  g_return_val_if_fail (path, FALSE);

  indices = gtk_tree_path_get_indices (path);
  depth = gtk_tree_path_get_depth (path);

  g_return_val_if_fail (depth == 1, FALSE);

  return init_iter (list, indices[0], iter);
}


static gboolean
tree_model_iter_next (GtkTreeModel *model, GtkTreeIter *iter)
{
  TextImportModel *list = TEXT_IMPORT_MODEL (model);
  gint idx;

  assert (iter->stamp == TREE_MODEL_STAMP);

  idx = GPOINTER_TO_INT (iter->user_data);
  return init_iter (list, idx == -1 ? -1 : idx + 1, iter);
}

static GtkTreePath *
tree_model_get_path (GtkTreeModel *model, GtkTreeIter *iter)
{
  GtkTreePath *path;

  g_return_val_if_fail (iter->stamp == TREE_MODEL_STAMP, FALSE);

  path = gtk_tree_path_new ();
  gtk_tree_path_append_index (path, GPOINTER_TO_INT (iter->user_data));

  return path;
}

static void
tree_model_get_value (GtkTreeModel *model, GtkTreeIter *iter,
		      gint column, GValue *value)
{
  TextImportModel *list = TEXT_IMPORT_MODEL (model);
  gint idx;

  g_return_if_fail (iter->stamp == TREE_MODEL_STAMP);

  idx = GPOINTER_TO_INT (iter->user_data);
  assert (idx >= 0 && idx < list->line_cnt);

  if (column == 0)
    {
      g_value_init (value, G_TYPE_INT);
      g_value_set_int (value, idx + list->first_line + 1);
    }
  else
    {
      g_value_init (value, G_TYPE_STRING);
      g_value_set_static_string (value, ds_cstr (&list->lines[idx]));
    }
}

static gboolean
tree_model_iter_children (GtkTreeModel *tree_model,
			  GtkTreeIter *iter,
			  GtkTreeIter *parent)
{
  return FALSE;
}

static gint
tree_model_n_children (GtkTreeModel *model, GtkTreeIter  *iter)
{
  TextImportModel *list = TEXT_IMPORT_MODEL (model);

  return iter == NULL ? list->line_cnt : 0;
}

static gboolean
tree_model_nth_child (GtkTreeModel *model, GtkTreeIter *iter,
		      GtkTreeIter *parent, gint n)
{
  TextImportModel *list = TEXT_IMPORT_MODEL (model);
  g_return_val_if_fail (IS_TEXT_IMPORT_MODEL (model), FALSE);

  if (parent)
    return FALSE;
  return init_iter (list, n, iter);
}

static void
text_import_model_tree_model_init (gpointer iface_, gpointer data UNUSED)
{
  GtkTreeModelIface *iface = (GtkTreeModelIface *) iface_;

  iface->get_flags = tree_model_get_flags;
  iface->get_n_columns = tree_model_n_columns;
  iface->get_column_type = tree_model_column_type;
  iface->get_iter = tree_model_get_iter;
  iface->iter_next = tree_model_iter_next;
  iface->get_path = tree_model_get_path;
  iface->get_value = tree_model_get_value;

  iface->iter_children = tree_model_iter_children;
  iface->iter_has_child = tree_model_iter_has_child;
  iface->iter_n_children = tree_model_n_children;
  iface->iter_nth_child = tree_model_nth_child;
  iface->iter_parent = tree_model_iter_parent;
}

gint
text_import_model_iter_to_row (const GtkTreeIter *iter)
{
  assert (iter->stamp == TREE_MODEL_STAMP);
  return GPOINTER_TO_INT (iter->user_data);
}

/* Increments the "watch cursor" level, setting the cursor for
   the assistant window to a watch face to indicate to the user
   that the ongoing operation may take some time. */
static void
push_watch_cursor (struct import_assistant *ia)
{
  if (++ia->asst.watch_cursor == 1)
    {
      GtkWidget *widget = GTK_WIDGET (ia->asst.assistant);
      GdkDisplay *display = gtk_widget_get_display (widget);
      GdkCursor *cursor = gdk_cursor_new_for_display (display, GDK_WATCH);
      gdk_window_set_cursor (widget->window, cursor);
      gdk_cursor_unref (cursor);
      gdk_display_flush (display);
    }
}

/* Decrements the "watch cursor" level.  If the level reaches
   zero, the cursor is reset to its default shape. */
static void
pop_watch_cursor (struct import_assistant *ia)
{
  if (--ia->asst.watch_cursor == 0)
    {
      GtkWidget *widget = GTK_WIDGET (ia->asst.assistant);
      gdk_window_set_cursor (widget->window, NULL);
    }
}
