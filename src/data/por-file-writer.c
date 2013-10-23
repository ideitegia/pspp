/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include "data/por-file-writer.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "data/case.h"
#include "data/casewriter-provider.h"
#include "data/casewriter.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "data/format.h"
#include "data/make-file.h"
#include "data/missing-values.h"
#include "data/short-names.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "libpspp/version.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* Maximum width of a variable in a portable file. */
#define MAX_POR_WIDTH 255

/* Portable file writer. */
struct pfm_writer
  {
    struct file_handle *fh;     /* File handle. */
    struct fh_lock *lock;       /* Lock on file handle. */
    FILE *file;			/* File stream. */
    struct replace_file *rf;    /* Ticket for replacing output file. */

    int lc;			/* Number of characters on this line so far. */

    size_t var_cnt;             /* Number of variables. */
    struct pfm_var *vars;       /* Variables. */

    int digits;                 /* Digits of precision. */
  };

/* A variable to write to the portable file. */
struct pfm_var
  {
    int width;                  /* 0=numeric, otherwise string var width. */
    int case_index;             /* Index in case. */
  };

static const struct casewriter_class por_file_casewriter_class;

static bool close_writer (struct pfm_writer *);
static void buf_write (struct pfm_writer *, const void *, size_t);
static void write_header (struct pfm_writer *);
static void write_version_data (struct pfm_writer *);
static void write_variables (struct pfm_writer *, struct dictionary *);
static void write_value_labels (struct pfm_writer *,
                                const struct dictionary *);
static void write_documents (struct pfm_writer *,
                             const struct dictionary *);

static void format_trig_double (long double, int base_10_precision, char[]);
static char *format_trig_int (int, bool force_sign, char[]);

/* Returns default options for writing a portable file. */
struct pfm_write_options
pfm_writer_default_options (void)
{
  struct pfm_write_options opts;
  opts.create_writeable = true;
  opts.type = PFM_COMM;
  opts.digits = DBL_DIG;
  return opts;
}

/* Writes the dictionary DICT to portable file HANDLE according
   to the given OPTS.  Returns nonzero only if successful.  DICT
   will not be modified, except to assign short names. */
struct casewriter *
pfm_open_writer (struct file_handle *fh, struct dictionary *dict,
                 struct pfm_write_options opts)
{
  struct pfm_writer *w = NULL;
  mode_t mode;
  size_t i;

  /* Initialize data structures. */
  w = xmalloc (sizeof *w);
  w->fh = fh_ref (fh);
  w->lock = NULL;
  w->file = NULL;
  w->rf = NULL;
  w->lc = 0;
  w->var_cnt = 0;
  w->vars = NULL;

  w->var_cnt = dict_get_var_cnt (dict);
  w->vars = xnmalloc (w->var_cnt, sizeof *w->vars);
  for (i = 0; i < w->var_cnt; i++)
    {
      const struct variable *dv = dict_get_var (dict, i);
      struct pfm_var *pv = &w->vars[i];
      pv->width = MIN (var_get_width (dv), MAX_POR_WIDTH);
      pv->case_index = var_get_case_index (dv);
    }

  w->digits = opts.digits;
  if (w->digits < 1)
    {
      msg (ME, _("Invalid decimal digits count %d.  Treating as %d."),
           w->digits, DBL_DIG);
      w->digits = DBL_DIG;
    }

  /* Lock file. */
  /* TRANSLATORS: this fragment will be interpolated into
     messages in fh_lock() that identify types of files. */
  w->lock = fh_lock (fh, FH_REF_FILE, N_("portable file"), FH_ACC_WRITE, true);
  if (w->lock == NULL)
    goto error;

  /* Create file. */
  mode = 0444;
  if (opts.create_writeable)
    mode |= 0222;
  w->rf = replace_file_start (fh_get_file_name (fh), "w", mode,
                              &w->file, NULL);
  if (w->rf == NULL)
    {
      msg (ME, _("Error opening `%s' for writing as a portable file: %s."),
           fh_get_file_name (fh), strerror (errno));
      goto error;
    }

  /* Write file header. */
  write_header (w);
  write_version_data (w);
  write_variables (w, dict);
  write_value_labels (w, dict);
  if (dict_get_document_line_cnt (dict) > 0)
    write_documents (w, dict);
  buf_write (w, "F", 1);
  if (ferror (w->file))
    goto error;
  return casewriter_create (dict_get_proto (dict),
                            &por_file_casewriter_class, w);

error:
  close_writer (w);
  return NULL;
}

/* Write NBYTES starting at BUF to the portable file represented by
   H.  Break lines properly every 80 characters.  */
static void
buf_write (struct pfm_writer *w, const void *buf_, size_t nbytes)
{
  const char *buf = buf_;

  if (ferror (w->file))
    return;

  assert (buf != NULL);
  while (nbytes + w->lc >= 80)
    {
      size_t n = 80 - w->lc;

      if (n)
        fwrite (buf, n, 1, w->file);
      fwrite ("\r\n", 2, 1, w->file);

      nbytes -= n;
      buf += n;
      w->lc = 0;
    }
  fwrite (buf, nbytes, 1, w->file);

  w->lc += nbytes;
}

/* Write D to the portable file as a floating-point field. */
static void
write_float (struct pfm_writer *w, double d)
{
  char buffer[64];
  format_trig_double (d, floor (d) == d ? DBL_DIG : w->digits, buffer);
  buf_write (w, buffer, strlen (buffer));
  if (d != SYSMIS)
    buf_write (w, "/", 1);
}

/* Write N to the portable file as an integer field. */
static void
write_int (struct pfm_writer *w, int n)
{
  char buffer[64];
  format_trig_int (n, false, buffer);
  buf_write (w, buffer, strlen (buffer));
  buf_write (w, "/", 1);
}

/* Write S to the portable file as a string field. */
static void
write_string (struct pfm_writer *w, const char *s)
{
  size_t n = strlen (s);
  write_int (w, (int) n);
  buf_write (w, s, n);
}

/* Write file header. */
static void
write_header (struct pfm_writer *w)
{
  static const char spss2ascii[256] =
    {
      "0000000000000000000000000000000000000000000000000000000000000000"
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ."
      "<(+|&[]!$*);^-/|,%_>?`:$@'=\"000000~-0000123456789000-()0{}\\00000"
      "0000000000000000000000000000000000000000000000000000000000000000"
    };
  int i;

  for (i = 0; i < 5; i++)
    buf_write (w, "ASCII SPSS PORT FILE                    ", 40);

  buf_write (w, spss2ascii, 256);
  buf_write (w, "SPSSPORT", 8);
}

/* Writes version, date, and identification records. */
static void
write_version_data (struct pfm_writer *w)
{
  char date_str[9];
  char time_str[7];
  time_t t;
  struct tm tm;
  struct tm *tmp;

  if ((time_t) -1 == time (&t))
    {
      tm.tm_sec = tm.tm_min = tm.tm_hour = tm.tm_mon = tm.tm_year = 0;
      tm.tm_mday = 1;
      tmp = &tm;
    }
  else
    tmp = localtime (&t);

  sprintf (date_str, "%04d%02d%02d",
           tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday);
  sprintf (time_str, "%02d%02d%02d", tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
  buf_write (w, "A", 1);
  write_string (w, date_str);
  write_string (w, time_str);

  /* Product identification. */
  buf_write (w, "1", 1);
  write_string (w, version);

  /* Subproduct identification. */
  buf_write (w, "3", 1);
  write_string (w, host_system);
}

/* Write format F to file H.  The format is first resized to fit
   a value of the given WIDTH, which is handy in case F
   represents a string longer than 255 bytes and thus WIDTH is
   truncated to 255 bytes.  */
static void
write_format (struct pfm_writer *w, struct fmt_spec f, int width)
{
  fmt_resize (&f, width);
  write_int (w, fmt_to_io (f.type));
  write_int (w, f.w);
  write_int (w, f.d);
}

/* Write value V with width WIDTH to file H. */
static void
write_value (struct pfm_writer *w, const union value *v, int width)
{
  if (width == 0)
    write_float (w, v->f);
  else
    {
      width = MIN (width, MAX_POR_WIDTH);
      write_int (w, width);
      buf_write (w, value_str (v, width), width);
    }
}

/* Write variable records. */
static void
write_variables (struct pfm_writer *w, struct dictionary *dict)
{
  int i;

  short_names_assign (dict);

  if (dict_get_weight (dict) != NULL) 
    {
      buf_write (w, "6", 1);
      write_string (w, var_get_short_name (dict_get_weight (dict), 0));
    }
  
  buf_write (w, "4", 1);
  write_int (w, dict_get_var_cnt (dict));

  buf_write (w, "5", 1);
  write_int (w, ceil (w->digits * (log (10) / log (30))));

  for (i = 0; i < dict_get_var_cnt (dict); i++)
    {
      struct variable *v = dict_get_var (dict, i);
      struct missing_values mv;
      int width = MIN (var_get_width (v), MAX_POR_WIDTH);
      int j;

      buf_write (w, "7", 1);
      write_int (w, width);
      write_string (w, var_get_short_name (v, 0));
      write_format (w, *var_get_print_format (v), width);
      write_format (w, *var_get_write_format (v), width);

      /* Write missing values. */
      mv_copy (&mv, var_get_missing_values (v));
      if (var_get_width (v) > 8)
        mv_resize (&mv, 8);
      if (mv_has_range (&mv))
        {
          double x, y;
          mv_get_range (&mv, &x, &y);
          if (x == LOWEST)
            {
              buf_write (w, "9", 1);
              write_float (w, y);
            }
          else if (y == HIGHEST)
            {
              buf_write (w, "A", 1);
              write_float (w, y);
            }
          else
            {
              buf_write (w, "B", 1);
              write_float (w, x);
              write_float (w, y);
            }
        }
      for (j = 0; j < mv_n_values (&mv); j++)
        {
          buf_write (w, "8", 1);
          write_value (w, mv_get_value (&mv, j), mv_get_width (&mv));
        }
      mv_destroy (&mv);

      /* Write variable label. */
      if (var_get_label (v) != NULL)
        {
          buf_write (w, "C", 1);
          write_string (w, var_get_label (v));
        }
    }
}

/* Write value labels to disk.  FIXME: Inefficient. */
static void
write_value_labels (struct pfm_writer *w, const struct dictionary *dict)
{
  int i;

  for (i = 0; i < dict_get_var_cnt (dict); i++)
    {
      struct variable *v = dict_get_var (dict, i);
      const struct val_labs *val_labs = var_get_value_labels (v);
      size_t n_labels = val_labs_count (val_labs);
      const struct val_lab **labels;
      int j;

      if (n_labels == 0)
	continue;

      buf_write (w, "D", 1);
      write_int (w, 1);
      write_string (w, var_get_short_name (v, 0));
      write_int (w, val_labs_count (val_labs));

      n_labels = val_labs_count (val_labs);
      labels = val_labs_sorted (val_labs);
      for (j = 0; j < n_labels; j++)
        {
          const struct val_lab *vl = labels[j];
          write_value (w, val_lab_get_value (vl), var_get_width (v));
          write_string (w, val_lab_get_escaped_label (vl));
        }
      free (labels);
    }
}

/* Write documents in DICT to portable file W. */
static void
write_documents (struct pfm_writer *w, const struct dictionary *dict)
{
  size_t line_cnt = dict_get_document_line_cnt (dict);
  struct string line = DS_EMPTY_INITIALIZER;
  int i;

  buf_write (w, "E", 1);
  write_int (w, line_cnt);
  for (i = 0; i < line_cnt; i++)
    write_string (w, dict_get_document_line (dict, i));
  ds_destroy (&line);
}

/* Writes case C to the portable file represented by WRITER. */
static void
por_file_casewriter_write (struct casewriter *writer, void *w_,
                           struct ccase *c)
{
  struct pfm_writer *w = w_;
  int i;

  if (!ferror (w->file))
    {
      for (i = 0; i < w->var_cnt; i++)
        {
          struct pfm_var *v = &w->vars[i];

          if (v->width == 0)
            write_float (w, case_num_idx (c, v->case_index));
          else
            {
              write_int (w, v->width);
              buf_write (w, case_str_idx (c, v->case_index), v->width);
            }
        }
    }
  else
    casewriter_force_error (writer);

  case_unref (c);
}

static void
por_file_casewriter_destroy (struct casewriter *writer, void *w_)
{
  struct pfm_writer *w = w_;
  if (!close_writer (w))
    casewriter_force_error (writer);
}

/* Closes a portable file after we're done with it.
   Returns true if successful, false if an I/O error occurred. */
static bool
close_writer (struct pfm_writer *w)
{
  bool ok;

  if (w == NULL)
    return true;

  ok = true;
  if (w->file != NULL)
    {
      char buf[80];
      memset (buf, 'Z', sizeof buf);
      buf_write (w, buf, w->lc >= 80 ? 80 : 80 - w->lc);

      ok = !ferror (w->file);
      if (fclose (w->file) == EOF)
        ok = false;

      if (!ok)
        msg (ME, _("An I/O error occurred writing portable file `%s'."),
             fh_get_file_name (w->fh));

      if (ok ? !replace_file_commit (w->rf) : !replace_file_abort (w->rf))
        ok = false;
    }

  fh_unlock (w->lock);
  fh_unref (w->fh);

  free (w->vars);
  free (w);

  return ok;
}

/* Base-30 conversion.

   Portable files represent numbers in base-30 format, so we need
   to be able to convert real and integer number to that base.
   Older versions of PSPP used libgmp to do so, but this added a
   big library dependency to do just one thing.  Now we do it
   ourselves internally.

   Important fact: base 30 is called "trigesimal". */

/* Conversion base. */
#define BASE 30                         /* As an integer. */
#define LDBASE ((long double) BASE)     /* As a long double. */

/* This is floor(log30(2**31)), the minimum number of trigesimal
   digits that a `long int' can hold. */
#define CHUNK_SIZE 6

/* pow_tab[i] = pow (30, pow (2, i)) */
static long double pow_tab[16];

/* Initializes pow_tab[]. */
static void
init_pow_tab (void)
{
  static bool did_init = false;
  long double power;
  size_t i;

  /* Only initialize once. */
  if (did_init)
    return;
  did_init = true;

  /* Set each element of pow_tab[] until we run out of numerical
     range. */
  i = 0;
  for (power = 30.0L; power < DBL_MAX; power *= power)
    {
      assert (i < sizeof pow_tab / sizeof *pow_tab);
      pow_tab[i++] = power;
    }
}

/* Returns 30**EXPONENT, for 0 <= EXPONENT <= log30(DBL_MAX). */
static long double
pow30_nonnegative (int exponent)
{
  long double power;
  int i;

  assert (exponent >= 0);
  assert (exponent < 1L << (sizeof pow_tab / sizeof *pow_tab));

  power = 1.L;
  for (i = 0; exponent > 0; exponent >>= 1, i++)
    if (exponent & 1)
      power *= pow_tab[i];

  return power;
}

/* Returns 30**EXPONENT, for log30(DBL_MIN) <= EXPONENT <=
   log30(DBL_MAX). */
static long double
pow30 (int exponent)
{
  if (exponent >= 0)
    return pow30_nonnegative (exponent);
  else
    return 1.L / pow30_nonnegative (-exponent);
}

/* Returns the character corresponding to TRIG. */
static int
trig_to_char (int trig)
{
  assert (trig >= 0 && trig < 30);
  return "0123456789ABCDEFGHIJKLMNOPQRST"[trig];
}

/* Formats the TRIG_CNT trigs in TRIGS[], writing them as
   null-terminated STRING.  The trigesimal point is inserted
   after TRIG_PLACES characters have been printed, if necessary
   adding extra zeros at either end for correctness.  Returns the
   character after the formatted number. */
static char *
format_trig_digits (char *string,
                    const char trigs[], int trig_cnt, int trig_places)
{
  if (trig_places < 0)
    {
      *string++ = '.';
      while (trig_places++ < 0)
        *string++ = '0';
      trig_places = -1;
    }
  while (trig_cnt-- > 0)
    {
      if (trig_places-- == 0)
        *string++ = '.';
      *string++ = trig_to_char (*trigs++);
    }
  while (trig_places-- > 0)
    *string++ = '0';
  *string = '\0';
  return string;
}

/* Helper function for format_trig_int() that formats VALUE as a
   trigesimal integer at CP.  VALUE must be nonnegative.
   Returns the character following the formatted integer. */
static char *
recurse_format_trig_int (char *cp, int value)
{
  int trig = value % BASE;
  value /= BASE;
  if (value > 0)
    cp = recurse_format_trig_int (cp, value);
  *cp++ = trig_to_char (trig);
  return cp;
}

/* Formats VALUE as a trigesimal integer in null-terminated
   STRING[].  VALUE must be in the range -DBL_MAX...DBL_MAX.  If
   FORCE_SIGN is true, a sign is always inserted; otherwise, a
   sign is only inserted if VALUE is negative. */
static char *
format_trig_int (int value, bool force_sign, char string[])
{
  /* Insert sign. */
  if (value < 0)
    {
      *string++ = '-';
      value = -value;
    }
  else if (force_sign)
    *string++ = '+';

  /* Format integer. */
  string = recurse_format_trig_int (string, value);
  *string = '\0';
  return string;
}

/* Determines whether the TRIG_CNT trigesimals in TRIGS[] warrant
   rounding up or down.  Returns true if TRIGS[] represents a
   value greater than half, false if less than half.  If TRIGS[]
   is exactly half, examines TRIGS[-1] and returns true if odd,
   false if even ("round to even"). */
static bool
should_round_up (const char trigs[], int trig_cnt)
{
  assert (trig_cnt > 0);

  if (*trigs < BASE / 2)
    {
      /* Less than half: round down. */
      return false;
    }
  else if (*trigs > BASE / 2)
    {
      /* Greater than half: round up. */
      return true;
    }
  else
    {
      /* Approximately half: look more closely. */
      int i;
      for (i = 1; i < trig_cnt; i++)
        if (trigs[i] > 0)
          {
            /* Slightly greater than half: round up. */
            return true;
          }

      /* Exactly half: round to even. */
      return trigs[-1] % 2;
    }
}

/* Rounds up the rightmost trig in the TRIG_CNT trigs in TRIGS[],
   carrying to the left as necessary.  Returns true if
   successful, false on failure (due to a carry out of the
   leftmost position). */
static bool
try_round_up (char *trigs, int trig_cnt)
{
  while (trig_cnt > 0)
    {
      char *round_trig = trigs + --trig_cnt;
      if (*round_trig != BASE - 1)
        {
          /* Round this trig up to the next value. */
          ++*round_trig;
          return true;
        }

      /* Carry over to the next trig to the left. */
      *round_trig = 0;
    }

  /* Ran out of trigs to carry. */
  return false;
}

/* Converts VALUE to trigesimal format in string OUTPUT[] with the
   equivalent of at least BASE_10_PRECISION decimal digits of
   precision.  The output format may use conventional or
   scientific notation.  Missing, infinite, and extreme values
   are represented with "*.". */
static void
format_trig_double (long double value, int base_10_precision, char output[])
{
  /* Original VALUE was negative? */
  bool negative;

  /* Number of significant trigesimals. */
  int base_30_precision;

  /* Base-2 significand and exponent for original VALUE. */
  double base_2_sig;
  int base_2_exp;

  /* VALUE as a set of trigesimals. */
  char buffer[DBL_DIG + 16];
  char *trigs;
  int trig_cnt;

  /* Number of trigesimal places for trigs.
     trigs[0] has coefficient 30**(trig_places - 1),
     trigs[1] has coefficient 30**(trig_places - 2),
     and so on.
     In other words, the trigesimal point is just before trigs[0].
   */
  int trig_places;

  /* Number of trigesimal places left to write into BUFFER. */
  int trigs_to_output;

  init_pow_tab ();

  /* Handle special cases. */
  if (value == SYSMIS)
    goto missing_value;
  if (value == 0.)
    goto zero;

  /* Make VALUE positive. */
  if (value < 0)
    {
      value = -value;
      negative = true;
    }
  else
    negative = false;

  /* Adjust VALUE to roughly 30**3, by shifting the trigesimal
     point left or right as necessary.  We approximate the
     base-30 exponent by obtaining the base-2 exponent, then
     multiplying by log30(2).  This approximation is sufficient
     to ensure that the adjusted VALUE is always in the range
     0...30**6, an invariant of the loop below. */
  errno = 0;
  base_2_sig = frexp (value, &base_2_exp);
  if (errno != 0 || !isfinite (base_2_sig))
    goto missing_value;
  if (base_2_exp == 0 && base_2_sig == 0.)
    goto zero;
  if (base_2_exp <= INT_MIN / 20379L || base_2_exp >= INT_MAX / 20379L)
    goto missing_value;
  trig_places = (base_2_exp * 20379L / 100000L) + CHUNK_SIZE / 2;
  value *= pow30 (CHUNK_SIZE - trig_places);

  /* Dump all the trigs to buffer[], CHUNK_SIZE at a time. */
  trigs = buffer;
  trig_cnt = 0;
  for (trigs_to_output = DIV_RND_UP (DBL_DIG * 2, 3) + 1 + (CHUNK_SIZE / 2);
       trigs_to_output > 0;
       trigs_to_output -= CHUNK_SIZE)
    {
      long chunk;
      int trigs_left;

      /* The current chunk is just the integer part of VALUE,
         truncated to the nearest integer.  The chunk fits in a
         long. */
      chunk = value;
      assert (pow30 (CHUNK_SIZE) <= LONG_MAX);
      assert (chunk >= 0 && chunk < pow30 (CHUNK_SIZE));

      value -= chunk;

      /* Append the chunk, in base 30, to trigs[]. */
      for (trigs_left = CHUNK_SIZE; chunk > 0 && trigs_left > 0; )
        {
          trigs[trig_cnt + --trigs_left] = chunk % 30;
          chunk /= 30;
        }
      while (trigs_left > 0)
        trigs[trig_cnt + --trigs_left] = 0;
      trig_cnt += CHUNK_SIZE;

      /* Proceed to the next chunk. */
      if (value == 0.)
        break;
      value *= pow (LDBASE, CHUNK_SIZE);
    }

  /* Strip leading zeros. */
  while (trig_cnt > 1 && *trigs == 0)
    {
      trigs++;
      trig_cnt--;
      trig_places--;
    }

  /* Round to requested precision, conservatively estimating the
     required base-30 precision as 2/3 of the base-10 precision
     (log30(10) = .68). */
  assert (base_10_precision > 0);
  if (base_10_precision > LDBL_DIG)
    base_10_precision = LDBL_DIG;
  base_30_precision = DIV_RND_UP (base_10_precision * 2, 3);
  if (trig_cnt > base_30_precision)
    {
      if (should_round_up (trigs + base_30_precision,
                           trig_cnt - base_30_precision))
        {
          /* Try to round up. */
          if (try_round_up (trigs, base_30_precision))
            {
              /* Rounding up worked. */
              trig_cnt = base_30_precision;
            }
          else
            {
              /* Couldn't round up because we ran out of trigs to
                 carry into.  Do the carry here instead. */
              *trigs = 1;
              trig_cnt = 1;
              trig_places++;
            }
        }
      else
        {
          /* Round down. */
          trig_cnt = base_30_precision;
        }
    }
  else
    {
      /* No rounding required: fewer digits available than
         requested. */
    }

  /* Strip trailing zeros. */
  while (trig_cnt > 1 && trigs[trig_cnt - 1] == 0)
    trig_cnt--;

  /* Write output. */
  if (negative)
    *output++ = '-';
  if (trig_places >= -1 && trig_places < trig_cnt + 3)
    {
      /* Use conventional notation. */
      format_trig_digits (output, trigs, trig_cnt, trig_places);
    }
  else
    {
      /* Use scientific notation. */
      char *op;
      op = format_trig_digits (output, trigs, trig_cnt, trig_cnt);
      op = format_trig_int (trig_places - trig_cnt, true, op);
    }
  return;

 zero:
  strcpy (output, "0");
  return;

 missing_value:
  strcpy (output, "*.");
  return;
}

static const struct casewriter_class por_file_casewriter_class =
  {
    por_file_casewriter_write,
    por_file_casewriter_destroy,
    NULL,
  };
