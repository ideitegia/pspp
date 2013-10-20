/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include "data/csv-file-writer.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "data/calendar.h"
#include "data/case.h"
#include "data/casewriter-provider.h"
#include "data/casewriter.h"
#include "data/data-out.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "data/format.h"
#include "data/make-file.h"
#include "data/missing-values.h"
#include "data/settings.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/ftoastr.h"
#include "gl/minmax.h"
#include "gl/unlocked-io.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* A variable in a CSV file. */
struct csv_var
  {
    int width;                     /* Variable width (0 to 32767). */
    int case_index;                /* Index into case. */
    struct fmt_spec format;        /* Print format. */
    struct missing_values missing; /* User-missing values, if recoding. */
    struct val_labs *val_labs;  /* Value labels, if any and they are in use. */
  };

/* Comma-separated value (CSV) file writer. */
struct csv_writer
  {
    struct file_handle *fh;     /* File handle. */
    struct fh_lock *lock;       /* Mutual exclusion for file. */
    FILE *file;			/* File stream. */
    struct replace_file *rf;    /* Ticket for replacing output file. */

    struct csv_writer_options opts;

    char *encoding;             /* Encoding used by variables. */

    /* Variables. */
    struct csv_var *csv_vars;   /* Variables. */
    size_t n_csv_vars;         /* Number of variables. */
  };

static const struct casewriter_class csv_file_casewriter_class;

static void write_var_names (struct csv_writer *, const struct dictionary *);

static bool write_error (const struct csv_writer *);
static bool close_writer (struct csv_writer *);

/* Initializes OPTS with default options for writing a CSV file. */
void
csv_writer_options_init (struct csv_writer_options *opts)
{
  opts->recode_user_missing = false;
  opts->include_var_names = false;
  opts->use_value_labels = false;
  opts->use_print_formats = false;
  opts->decimal = settings_get_decimal_char (FMT_F);
  opts->delimiter = ',';
  opts->qualifier = '"';
}

/* Opens the CSV file designated by file handle FH for writing cases from
   dictionary DICT according to the given OPTS.

   No reference to D is retained, so it may be modified or
   destroyed at will after this function returns. */
struct casewriter *
csv_writer_open (struct file_handle *fh, const struct dictionary *dict,
                 const struct csv_writer_options *opts)
{
  struct csv_writer *w;
  int i;

  /* Create and initialize writer. */
  w = xmalloc (sizeof *w);
  w->fh = fh_ref (fh);
  w->lock = NULL;
  w->file = NULL;
  w->rf = NULL;

  w->opts = *opts;

  w->encoding = xstrdup (dict_get_encoding (dict));

  w->n_csv_vars = dict_get_var_cnt (dict);
  w->csv_vars = xnmalloc (w->n_csv_vars, sizeof *w->csv_vars);
  for (i = 0; i < w->n_csv_vars; i++)
    {
      const struct variable *var = dict_get_var (dict, i);
      struct csv_var *cv = &w->csv_vars[i];

      cv->width = var_get_width (var);
      cv->case_index = var_get_case_index (var);

      cv->format = *var_get_print_format (var);
      if (opts->recode_user_missing)
        mv_copy (&cv->missing, var_get_missing_values (var));
      else
        mv_init (&cv->missing, cv->width);

      if (opts->use_value_labels)
        cv->val_labs = val_labs_clone (var_get_value_labels (var));
      else
        cv->val_labs = NULL;
    }

  /* Open file handle as an exclusive writer. */
  /* TRANSLATORS: this fragment will be interpolated into messages in fh_lock()
     that identify types of files. */
  w->lock = fh_lock (fh, FH_REF_FILE, N_("CSV file"), FH_ACC_WRITE, true);
  if (w->lock == NULL)
    goto error;

  /* Create the file on disk. */
  w->rf = replace_file_start (fh_get_file_name (fh), "w", 0666,
                              &w->file, NULL);
  if (w->rf == NULL)
    {
      msg (ME, _("Error opening `%s' for writing as a system file: %s."),
           fh_get_file_name (fh), strerror (errno));
      goto error;
    }

  if (opts->include_var_names)
    write_var_names (w, dict);

  if (write_error (w))
    goto error;

  return casewriter_create (dict_get_proto (dict),
                            &csv_file_casewriter_class, w);

error:
  close_writer (w);
  return NULL;
}

static bool
csv_field_needs_quoting (struct csv_writer *w, const char *s, size_t len)
{
  const char *p;

  for (p = s; p < &s[len]; p++)
    if (*p == w->opts.qualifier || *p == w->opts.delimiter
        || *p == '\n' || *p == '\r')
      return true;

  return false;
}

static void
csv_output_buffer (struct csv_writer *w, const char *s, size_t len)
{
  if (csv_field_needs_quoting (w, s, len))
    {
      const char *p;

      putc (w->opts.qualifier, w->file);
      for (p = s; p < &s[len]; p++)
        {
          if (*p == w->opts.qualifier)
            putc (w->opts.qualifier, w->file);
          putc (*p, w->file);
        }
      putc (w->opts.qualifier, w->file);
    }
  else
    fwrite (s, 1, len, w->file);
}

static void
csv_output_string (struct csv_writer *w, const char *s)
{
  csv_output_buffer (w, s, strlen (s));
}

static void
write_var_names (struct csv_writer *w, const struct dictionary *d)
{
  size_t i;

  for (i = 0; i < w->n_csv_vars; i++)
    {
      if (i > 0)
        putc (w->opts.delimiter, w->file);
      csv_output_string (w, var_get_name (dict_get_var (d, i)));
    }
  putc ('\n', w->file);
}

static void
csv_output_format (struct csv_writer *w, const struct csv_var *cv,
                   const union value *value)
{
  char *s = data_out (value, w->encoding, &cv->format);
  struct substring ss = ss_cstr (s);
  if (cv->format.type != FMT_A)
    ss_trim (&ss, ss_cstr (" "));
  else
    ss_rtrim (&ss, ss_cstr (" "));
  csv_output_buffer (w, ss.string, ss.length);
  free (s);
}

static double
extract_date (double number, int *y, int *m, int *d)
{
  int yd;

  calendar_offset_to_gregorian (number / 60. / 60. / 24., y, m, d, &yd);
  return fmod (number, 60. * 60. * 24.);
}

static void
extract_time (double number, double *H, int *M, int *S)
{
  *H = floor (number / 60. / 60.);
  number = fmod (number, 60. * 60.);

  *M = floor (number / 60.);
  number = fmod (number, 60.);

  *S = floor (number);
}

static void
csv_write_var__ (struct csv_writer *w, const struct csv_var *cv,
                 const union value *value)
{
  const char *label;

  label = val_labs_find (cv->val_labs, value);
  if (label != NULL)
    csv_output_string (w, label);
  else if (cv->width == 0 && value->f == SYSMIS)
    csv_output_buffer (w, " ", 1);
  else if (w->opts.use_print_formats)
    csv_output_format (w, cv, value);
  else
    {
      char s[MAX (DBL_STRLEN_BOUND, 128)];
      char *cp;

      switch (cv->format.type)
        {
        case FMT_F:
        case FMT_COMMA:
        case FMT_DOT:
        case FMT_DOLLAR:
        case FMT_PCT:
        case FMT_E:
        case FMT_CCA:
        case FMT_CCB:
        case FMT_CCC:
        case FMT_CCD:
        case FMT_CCE:
        case FMT_N:
        case FMT_Z:
        case FMT_P:
        case FMT_PK:
        case FMT_IB:
        case FMT_PIB:
        case FMT_PIBHEX:
        case FMT_RB:
        case FMT_RBHEX:
        case FMT_WKDAY:
        case FMT_MONTH:
          dtoastr (s, sizeof s, 0, 0, value->f);
          cp = strpbrk (s, ".,");
          if (cp != NULL)
            *cp = w->opts.decimal;
          break;

        case FMT_DATE:
        case FMT_ADATE:
        case FMT_EDATE:
        case FMT_JDATE:
        case FMT_SDATE:
        case FMT_QYR:
        case FMT_MOYR:
        case FMT_WKYR:
          if (value->f < 0)
            strcpy (s, " ");
          else
            {
              int y, m, d;

              extract_date (value->f, &y, &m, &d);
              snprintf (s, sizeof s, "%02d/%02d/%04d", m, d, y);
            }
          break;

        case FMT_DATETIME:
          if (value->f < 0)
            strcpy (s, " ");
          else
            {
              int y, m, d, M, S;
              double H;

              extract_time (extract_date (value->f, &y, &m, &d), &H, &M, &S);
              snprintf (s, sizeof s, "%02d/%02d/%04d %02.0f:%02d:%02d",
                        m, d, y, H, M, S);
            }
          break;

        case FMT_TIME:
        case FMT_DTIME:
          {
            double H;
            int M, S;

            extract_time (fabs (value->f), &H, &M, &S);
            snprintf (s, sizeof s, "%s%02.0f:%02d:%02d",
                      value->f < 0 ? "-" : "", H, M, S);
          }
          break;

        case FMT_A:
        case FMT_AHEX:
          csv_output_format (w, cv, value);
          return;

        case FMT_NUMBER_OF_FORMATS:
          NOT_REACHED ();
        }
      csv_output_string (w, s);
    }
}

static void
csv_write_var (struct csv_writer *w, const struct csv_var *cv,
               const union value *value)
{
  if (mv_is_value_missing (&cv->missing, value, MV_USER))
    {
      union value missing;

      value_init (&missing, cv->width);
      value_set_missing (&missing, cv->width);
      csv_write_var__ (w, cv, &missing);
      value_destroy (&missing, cv->width);
    }
  else
    csv_write_var__ (w, cv, value);
}

static void
csv_write_case (struct csv_writer *w, const struct ccase *c)
{
  size_t i;

  for (i = 0; i < w->n_csv_vars; i++)
    {
      const struct csv_var *cv = &w->csv_vars[i];

      if (i > 0)
        putc (w->opts.delimiter, w->file);
      csv_write_var (w, cv, case_data_idx (c, cv->case_index));
    }
  putc ('\n', w->file);
}

/* Writes case C to CSV file W. */
static void
csv_file_casewriter_write (struct casewriter *writer, void *w_,
                           struct ccase *c)
{
  struct csv_writer *w = w_;

  if (ferror (w->file))
    {
      casewriter_force_error (writer);
      case_unref (c);
      return;
    }

  csv_write_case (w, c);
  case_unref (c);
}

/* Destroys CSV file writer W. */
static void
csv_file_casewriter_destroy (struct casewriter *writer, void *w_)
{
  struct csv_writer *w = w_;
  if (!close_writer (w))
    casewriter_force_error (writer);
}

/* Returns true if an I/O error has occurred on WRITER, false otherwise. */
bool
write_error (const struct csv_writer *writer)
{
  return ferror (writer->file);
}

/* Closes a CSV file after we're done with it.
   Returns true if successful, false if an I/O error occurred. */
bool
close_writer (struct csv_writer *w)
{
  size_t i;
  bool ok;

  if (w == NULL)
    return true;

  ok = true;
  if (w->file != NULL)
    {
      if (write_error (w))
        ok = false;
      if (fclose (w->file) == EOF)
        ok = false;

      if (!ok)
        msg (ME, _("An I/O error occurred writing CSV file `%s'."),
             fh_get_file_name (w->fh));

      if (ok ? !replace_file_commit (w->rf) : !replace_file_abort (w->rf))
        ok = false;
    }

  fh_unlock (w->lock);
  fh_unref (w->fh);

  free (w->encoding);

  for (i = 0; i < w->n_csv_vars; i++)
    {
      struct csv_var *cv = &w->csv_vars[i];
      mv_destroy (&cv->missing);
      val_labs_destroy (cv->val_labs);
    }

  free (w->csv_vars);
  free (w);

  return ok;
}

/* CSV file writer casewriter class. */
static const struct casewriter_class csv_file_casewriter_class =
  {
    csv_file_casewriter_write,
    csv_file_casewriter_destroy,
    NULL,
  };
