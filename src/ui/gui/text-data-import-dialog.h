/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2010, 2011, 2013  Free Software Foundation

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

#ifndef TEXT_DATA_IMPORT_DIALOG_H
#define TEXT_DATA_IMPORT_DIALOG_H

#include <glib-object.h>
#include "ui/gui/psppire-data-window.h"
#include "data/spreadsheet-reader.h"

#include "libpspp/str.h"

enum { MAX_PREVIEW_LINES = 1000 }; /* Max number of lines to read. */

struct assist_page
{
  GtkWidget *page;
};

/* The file to be imported. */
struct file
  {
    char *file_name;        /* File name. */

    /* Relevant only for text files */

    gchar *encoding;        /* Encoding. */
    unsigned long int total_lines; /* Number of lines in file. */
    bool total_is_exact;    /* Is total_lines exact (or an estimate)? */

    /* The first several lines of the file. */
    struct string *lines;
    size_t line_cnt;
  };

/* The main body of the GTK+ assistant and related data. */
struct assistant
  {
    GtkBuilder *builder;
    GtkAssistant *assistant;
    GMainLoop *main_loop;
    bool loop_done;
    GtkWidget *paste_button;
    GtkWidget *reset_button;
    int response;
    int watch_cursor;

    GtkCellRenderer *prop_renderer;
    GtkCellRenderer *fixed_renderer;
  };

struct import_assistant
  {
    struct file file;
    struct assistant asst;

    struct intro_page *intro;
    struct sheet_spec_page *sheet_spec;
    struct first_line_page *first_line;
    struct separators_page *separators;
    struct formats_page *formats;

    /* The columns produced. */
    struct column *columns;     /* Information about each column. */
    size_t column_cnt;          /* Number of columns. */

    int skip_lines;             /* Number of initial lines to skip? */
    bool variable_names;        /* Variable names above first line of data? */
    struct dictionary *dict;

    struct spreadsheet *spreadsheet;
  };

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


GtkWidget * add_page_to_assistant (struct import_assistant *ia,
				   GtkWidget *page, GtkAssistantPageType type);

void text_data_import_assistant (PsppireDataWindow *);

/* FIXME: Should this be private to first line page ? */
void make_tree_view (const struct import_assistant *ia,
                            size_t first_line,
                            PsppSheetView **tree_view);

gint get_monospace_width (PsppSheetView *, GtkCellRenderer *,
                                 size_t char_cnt);
gint get_string_width (PsppSheetView *, GtkCellRenderer *,
                              const char *string);



void push_watch_cursor (struct import_assistant *);
void pop_watch_cursor (struct import_assistant *);


PsppSheetView *create_data_tree_view (bool input, GtkContainer *parent,
                                      struct import_assistant *);

PsppSheetViewColumn *make_data_column (struct import_assistant *,
                                       PsppSheetView *, bool input,
                                       gint column_idx);


void  update_assistant (struct import_assistant *ia);

bool init_file (struct import_assistant *ia, GtkWindow *parent_window);
void destroy_file (struct import_assistant *ia);

void prepare_sheet_spec_page (struct import_assistant *ia);
void reset_sheet_spec_page (struct import_assistant *);
void post_sheet_spec_page (struct import_assistant *ia);

void prepare_first_line_page (struct import_assistant *ia);
void reset_first_line_page (struct import_assistant *);

void reset_intro_page (struct import_assistant *ia);

void prepare_separators_page (struct import_assistant *ia);
void reset_separators_page (struct import_assistant *);
void destroy_separators_page (struct import_assistant *ia);

void prepare_formats_page (struct import_assistant *ia);
void reset_formats_page (struct import_assistant *);
void destroy_formats_page (struct import_assistant *ia);

struct import_assistant * init_assistant (GtkWindow *);
void destroy_assistant (struct import_assistant *);


#endif
