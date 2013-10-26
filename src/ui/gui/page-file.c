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
#include "data/casereader.h"
#include "data/gnumeric-reader.h"
#include "data/ods-reader.h"
#include "data/spreadsheet-reader.h"
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

struct import_assistant;

/* Choose a file */
static char *choose_file (GtkWindow *parent_window, gchar **encodingp);




/* Obtains the file to import from the user and initializes IA's
   file substructure.  PARENT_WINDOW must be the window to use
   as the file chooser window's parent.

   Returns true if successful, false if the file name could not
   be obtained or the file could not be read. */
bool
init_file (struct import_assistant *ia, GtkWindow *parent_window)
{
  enum { MAX_LINE_LEN = 16384 }; /* Max length of an acceptable line. */
  struct file *file = &ia->file;

  file->lines = NULL;
  file->file_name = choose_file (parent_window, &file->encoding);
  if (file->file_name == NULL)
    return false;

  if (ia->spreadsheet == NULL)
    ia->spreadsheet = gnumeric_probe (file->file_name, false);

  if (ia->spreadsheet == NULL)
    ia->spreadsheet = ods_probe (file->file_name, false);

  if (ia->spreadsheet == NULL)
    {
    struct string input;
    struct line_reader *reader = line_reader_for_file (file->encoding, file->file_name, O_RDONLY);
    if (reader == NULL)
      {
	msg_error (errno, _("Could not open `%s'"),
	     file->file_name);
	return false;
      }

    ds_init_empty (&input);
    file->lines = xnmalloc (MAX_PREVIEW_LINES, sizeof *file->lines);
    for (; file->line_cnt < MAX_PREVIEW_LINES; file->line_cnt++)
      {
	ds_clear (&input);
	if (!line_reader_read (reader, &input, MAX_LINE_LEN + 1)
	    || ds_length (&input) > MAX_LINE_LEN)
	  {
	    if (line_reader_eof (reader))
	      break;
	    else if (line_reader_error (reader))
	      msg (ME, _("Error reading `%s': %s"),
		   file->file_name, strerror (line_reader_error (reader)));
	    else
	      msg (ME, _("Failed to read `%s', because it contains a line "
			 "over %d bytes long and therefore appears not to be "
			 "a text file."),
		   file->file_name, MAX_LINE_LEN);
	    line_reader_close (reader);
	    destroy_file (ia);
	    ds_destroy (&input);
	    return false;
	  }

	ds_init_cstr (&file->lines[file->line_cnt],
		      recode_string ("UTF-8", line_reader_get_encoding (reader),
				     ds_cstr (&input), ds_length (&input)));
      }
    ds_destroy (&input);

    if (file->line_cnt == 0)
      {
	msg (ME, _("`%s' is empty."), file->file_name);
	line_reader_close (reader);
	destroy_file (ia);
	return false;
      }

    /* Estimate the number of lines in the file. */
    if (file->line_cnt < MAX_PREVIEW_LINES)
      file->total_lines = file->line_cnt;
    else
      {
	struct stat s;
	off_t position = line_reader_tell (reader);
	if (fstat (line_reader_fileno (reader), &s) == 0 && position > 0)
	  file->total_lines = (double) file->line_cnt / position * s.st_size;
	else
	  file->total_lines = 0;
      }

    line_reader_close (reader);
  }

  return true;
}

/* Frees IA's file substructure. */
void
destroy_file (struct import_assistant *ia)
{
  struct file *f = &ia->file;
  size_t i;

  if (f->lines)
    {
      for (i = 0; i < f->line_cnt; i++)
	ds_destroy (&f->lines[i]);
      free (f->lines);
    }

  g_free (f->file_name);
  g_free (f->encoding);
}

/* Obtains the file to read from the user.  If successful, returns the name of
   the file and stores the user's chosen encoding for the file into *ENCODINGP.
   The caller must free each of these strings with g_free().

   On failure, stores a null pointer and stores NULL in *ENCODINGP.

   PARENT_WINDOW must be the window to use as the file chooser window's
   parent. */
static char *
choose_file (GtkWindow *parent_window, gchar **encodingp)
{
  char *file_name;
  GtkFileFilter *filter = NULL;

  GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Import Delimited Text Data"),
                                        parent_window,
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                        NULL);

  g_object_set (dialog, "local-only", FALSE, NULL);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Text Files"));
  gtk_file_filter_add_mime_type (filter, "text/*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Text (*.txt) Files"));
  gtk_file_filter_add_pattern (filter, "*.txt");
  gtk_file_filter_add_pattern (filter, "*.TXT");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Plain Text (ASCII) Files"));
  gtk_file_filter_add_mime_type (filter, "text/plain");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Comma Separated Value Files"));
  gtk_file_filter_add_mime_type (filter, "text/csv");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  /* I've never encountered one of these, but it's listed here:
     http://www.iana.org/assignments/media-types/text/tab-separated-values  */
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Tab Separated Value Files"));
  gtk_file_filter_add_mime_type (filter, "text/tab-separated-values");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Gnumeric Spreadsheet Files"));
  gtk_file_filter_add_mime_type (filter, "application/x-gnumeric");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("OpenDocument Spreadsheet Files"));
  gtk_file_filter_add_mime_type (filter, "application/vnd.oasis.opendocument.spreadsheet");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Spreadsheet Files"));
  gtk_file_filter_add_mime_type (filter, "application/x-gnumeric");
  gtk_file_filter_add_mime_type (filter, "application/vnd.oasis.opendocument.spreadsheet");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  gtk_file_chooser_set_extra_widget (
    GTK_FILE_CHOOSER (dialog), psppire_encoding_selector_new ("Auto", true));

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_ACCEPT:
      file_name = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
      *encodingp = psppire_encoding_selector_get_encoding (
        gtk_file_chooser_get_extra_widget (GTK_FILE_CHOOSER (dialog)));
      break;
    default:
      file_name = NULL;
      *encodingp = NULL;
      break;
    }
  gtk_widget_destroy (dialog);

  return file_name;
}
