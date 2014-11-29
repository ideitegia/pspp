/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "data/any-reader.h"
#include "data/casereader-provider.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "data/short-names.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* portable_to_local[PORTABLE] translates the given portable
   character into the local character set. */
static const char portable_to_local[256] =
  {
    "                                                                "
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ."
    "<(+|&[]!$*);^-/|,%_>?`:$@'=\"      ~-   0123456789   -() {}\\     "
    "                                                                "
  };

/* Portable file reader. */
struct pfm_reader
  {
    struct any_reader any_reader;
    struct pool *pool;          /* All the portable file state. */

    jmp_buf bail_out;           /* longjmp() target for error handling. */

    struct dictionary *dict;
    struct any_read_info info;
    struct file_handle *fh;     /* File handle. */
    struct fh_lock *lock;       /* Read lock for file. */
    FILE *file;			/* File stream. */
    int line_length;            /* Number of characters so far on this line. */
    char cc;			/* Current character. */
    char *trans;                /* 256-byte character set translation table. */
    int var_cnt;                /* Number of variables. */
    int weight_index;		/* 0-based index of weight variable, or -1. */
    struct caseproto *proto;    /* Format of output cases. */
    bool ok;                    /* Set false on I/O error. */
  };

static const struct casereader_class por_file_casereader_class;

static struct pfm_reader *
pfm_reader_cast (const struct any_reader *r_)
{
  assert (r_->klass == &por_file_reader_class);
  return UP_CAST (r_, struct pfm_reader, any_reader);
}

static void
error (struct pfm_reader *r, const char *msg,...)
     PRINTF_FORMAT (2, 3)
     NO_RETURN;

/* Displays MSG as an error message and aborts reading the
   portable file via longjmp(). */
static void
error (struct pfm_reader *r, const char *msg, ...)
{
  struct msg m;
  struct string text;
  va_list args;

  ds_init_empty (&text);
  ds_put_format (&text, _("portable file %s corrupt at offset 0x%llx: "),
                 fh_get_file_name (r->fh), (long long int) ftello (r->file));
  va_start (args, msg);
  ds_put_vformat (&text, msg, args);
  va_end (args);

  m.category = MSG_C_GENERAL;
  m.severity = MSG_S_ERROR;
  m.file_name = NULL;
  m.first_line = 0;
  m.last_line = 0;
  m.first_column = 0;
  m.last_column = 0;
  m.text = ds_cstr (&text);

  msg_emit (&m);

  r->ok = false;

  longjmp (r->bail_out, 1);
}

/* Displays MSG as an warning for the current position in
   portable file reader R. */
static void
warning (struct pfm_reader *r, const char *msg, ...)
{
  struct msg m;
  struct string text;
  va_list args;

  ds_init_empty (&text);
  ds_put_format (&text, _("reading portable file %s at offset 0x%llx: "),
                 fh_get_file_name (r->fh), (long long int) ftello (r->file));
  va_start (args, msg);
  ds_put_vformat (&text, msg, args);
  va_end (args);

  m.category = MSG_C_GENERAL;
  m.severity = MSG_S_WARNING;
  m.file_name = NULL;
  m.first_line = 0;
  m.last_line = 0;
  m.first_column = 0;
  m.last_column = 0;
  m.text = ds_cstr (&text);

  msg_emit (&m);
}

/* Close and destroy R.
   Returns false if an error was detected on R, true otherwise. */
static bool
pfm_close (struct any_reader *r_)
{
  struct pfm_reader *r = pfm_reader_cast (r_);
  bool ok;

  dict_destroy (r->dict);
  any_read_info_destroy (&r->info);
  if (r->file)
    {
      if (fn_close (fh_get_file_name (r->fh), r->file) == EOF)
        {
          msg (ME, _("Error closing portable file `%s': %s."),
               fh_get_file_name (r->fh), strerror (errno));
          r->ok = false;
        }
      r->file = NULL;
    }

  fh_unlock (r->lock);
  fh_unref (r->fh);

  ok = r->ok;
  pool_destroy (r->pool);

  return ok;
}

/* Closes portable file reader R, after we're done with it. */
static void
por_file_casereader_destroy (struct casereader *reader, void *r_)
{
  struct pfm_reader *r = r_;
  if (!pfm_close (&r->any_reader))
    casereader_force_error (reader);
}

/* Read a single character into cur_char.  */
static void
advance (struct pfm_reader *r)
{
  int c;

  /* Read the next character from the file.
     Ignore carriage returns entirely.
     Mostly ignore new-lines, but if a new-line occurs before the
     line has reached 80 bytes in length, then treat the
     "missing" bytes as spaces. */
  for (;;)
    {
      while ((c = getc (r->file)) == '\r')
        continue;
      if (c != '\n')
        break;

      if (r->line_length < 80)
        {
          c = ' ';
          ungetc ('\n', r->file);
          break;
        }
      r->line_length = 0;
    }
  if (c == EOF)
    error (r, _("unexpected end of file"));

  if (r->trans != NULL)
    c = r->trans[c];
  r->cc = c;
  r->line_length++;
}

/* Skip a single character if present, and return whether it was
   skipped. */
static inline bool
match (struct pfm_reader *r, int c)
{
  if (r->cc == c)
    {
      advance (r);
      return true;
    }
  else
    return false;
}

static void read_header (struct pfm_reader *);
static void read_version_data (struct pfm_reader *, struct any_read_info *);
static void read_variables (struct pfm_reader *, struct dictionary *);
static void read_value_label (struct pfm_reader *, struct dictionary *);
static void read_documents (struct pfm_reader *, struct dictionary *);

/* Reads the dictionary from file with handle H, and returns it in a
   dictionary structure.  This dictionary may be modified in order to
   rename, reorder, and delete variables, etc. */
struct any_reader *
pfm_open (struct file_handle *fh)
{
  struct pool *volatile pool = NULL;
  struct pfm_reader *volatile r = NULL;

  /* Create and initialize reader. */
  pool = pool_create ();
  r = pool_alloc (pool, sizeof *r);
  r->any_reader.klass = &por_file_reader_class;
  r->dict = dict_create (get_default_encoding ());
  memset (&r->info, 0, sizeof r->info);
  r->pool = pool;
  r->fh = fh_ref (fh);
  r->lock = NULL;
  r->file = NULL;
  r->line_length = 0;
  r->weight_index = -1;
  r->trans = NULL;
  r->var_cnt = 0;
  r->proto = NULL;
  r->ok = true;
  if (setjmp (r->bail_out))
    goto error;

  /* Lock file. */
  /* TRANSLATORS: this fragment will be interpolated into
     messages in fh_lock() that identify types of files. */
  r->lock = fh_lock (fh, FH_REF_FILE, N_("portable file"), FH_ACC_READ, false);
  if (r->lock == NULL)
    goto error;

  /* Open file. */
  r->file = fn_open (fh_get_file_name (r->fh), "rb");
  if (r->file == NULL)
    {
      msg (ME, _("An error occurred while opening `%s' for reading "
                 "as a portable file: %s."),
           fh_get_file_name (r->fh), strerror (errno));
      goto error;
    }

  /* Read header, version, date info, product id, variables. */
  read_header (r);
  read_version_data (r, &r->info);
  read_variables (r, r->dict);

  /* Read value labels. */
  while (match (r, 'D'))
    read_value_label (r, r->dict);

  /* Read documents. */
  if (match (r, 'E'))
    read_documents (r, r->dict);

  /* Check that we've made it to the data. */
  if (!match (r, 'F'))
    error (r, _("Data record expected."));

  r->proto = caseproto_ref_pool (dict_get_proto (r->dict), r->pool);
  return &r->any_reader;

 error:
  pfm_close (&r->any_reader);
  return NULL;
}

struct casereader *
pfm_decode (struct any_reader *r_, const char *encoding UNUSED,
            struct dictionary **dictp, struct any_read_info *info)
{
  struct pfm_reader *r = pfm_reader_cast (r_);

  *dictp = r->dict;
  r->dict = NULL;

  if (info)
    {
      *info = r->info;
      memset (&r->info, 0, sizeof r->info);
    }

  return casereader_create_sequential (NULL, r->proto, CASENUMBER_MAX,
                                       &por_file_casereader_class, r);
}

/* Returns the value of base-30 digit C,
   or -1 if C is not a base-30 digit. */
static int
base_30_value (unsigned char c)
{
  static const char base_30_digits[] = "0123456789ABCDEFGHIJKLMNOPQRST";
  const char *p = strchr (base_30_digits, c);
  return p != NULL ? p - base_30_digits : -1;
}

/* Read a floating point value and return its value. */
static double
read_float (struct pfm_reader *r)
{
  double num = 0.;
  int exponent = 0;
  bool got_dot = false;         /* Seen a decimal point? */
  bool got_digit = false;       /* Seen any digits? */
  bool negative = false;        /* Number is negative? */

  /* Skip leading spaces. */
  while (match (r, ' '))
    continue;

  /* `*' indicates system-missing. */
  if (match (r, '*'))
    {
      advance (r);	/* Probably a dot (.) but doesn't appear to matter. */
      return SYSMIS;
    }

  negative = match (r, '-');
  for (;;)
    {
      int digit = base_30_value (r->cc);
      if (digit != -1)
	{
	  got_digit = true;

	  /* Make sure that multiplication by 30 will not overflow.  */
	  if (num > DBL_MAX * (1. / 30.))
	    /* The value of the digit doesn't matter, since we have already
	       gotten as many digits as can be represented in a `double'.
	       This doesn't necessarily mean the result will overflow.
	       The exponent may reduce it to within range.

	       We just need to record that there was another
	       digit so that we can multiply by 10 later.  */
	    ++exponent;
	  else
	    num = (num * 30.0) + digit;

	  /* Keep track of the number of digits after the decimal point.
	     If we just divided by 30 here, we would lose precision.  */
	  if (got_dot)
	    --exponent;
	}
      else if (!got_dot && r->cc == '.')
	/* Record that we have found the decimal point.  */
	got_dot = 1;
      else
	/* Any other character terminates the number.  */
	break;

      advance (r);
    }

  /* Check that we had some digits. */
  if (!got_digit)
    error (r, _("Number expected."));

  /* Get exponent if any. */
  if (r->cc == '+' || r->cc == '-')
    {
      long int exp = 0;
      bool negative_exponent = r->cc == '-';
      int digit;

      for (advance (r); (digit = base_30_value (r->cc)) != -1; advance (r))
	{
	  if (exp > LONG_MAX / 30)
            {
              exp = LONG_MAX;
              break;
            }
	  exp = exp * 30 + digit;
	}

      /* We don't check whether there were actually any digits, but we
         probably should. */
      if (negative_exponent)
	exp = -exp;
      exponent += exp;
    }

  /* Numbers must end with `/'. */
  if (!match (r, '/'))
    error (r, _("Missing numeric terminator."));

  /* Multiply `num' by 30 to the `exponent' power, checking for
     overflow.  */
  if (exponent < 0)
    num *= pow (30.0, (double) exponent);
  else if (exponent > 0)
    {
      if (num > DBL_MAX * pow (30.0, (double) -exponent))
        num = DBL_MAX;
      else
        num *= pow (30.0, (double) exponent);
    }

  return negative ? -num : num;
}

/* Read an integer and return its value. */
static int
read_int (struct pfm_reader *r)
{
  double f = read_float (r);
  if (floor (f) != f || f >= INT_MAX || f <= INT_MIN)
    error (r, _("Invalid integer."));
  return f;
}

/* Reads a string into BUF, which must have room for 256
   characters. */
static void
read_string (struct pfm_reader *r, char *buf)
{
  int n = read_int (r);
  if (n < 0 || n > 255)
    error (r, _("Bad string length %d."), n);

  while (n-- > 0)
    {
      *buf++ = r->cc;
      advance (r);
    }
  *buf = '\0';
}


/* Reads a string into BUF, which must have room for 256
   characters.
   Returns the number of bytes read.
*/
static size_t
read_bytes (struct pfm_reader *r, uint8_t *buf)
{
  int n = read_int (r);
  if (n < 0 || n > 255)
    error (r, _("Bad string length %d."), n);

  while (n-- > 0)
    {
      *buf++ = r->cc;
      advance (r);
    }
  return n;
}



/* Reads a string and returns a copy of it allocated from R's
   pool. */
static char *
read_pool_string (struct pfm_reader *r)
{
  char string[256];
  read_string (r, string);
  return pool_strdup (r->pool, string);
}

/* Reads the 464-byte file header. */
static void
read_header (struct pfm_reader *r)
{
  char *trans;
  int i;

  /* Read and ignore vanity splash strings. */
  for (i = 0; i < 200; i++)
    advance (r);

  /* Skip the first 64 characters of the translation table.
     We don't care about these.  They are probably all set to
     '0', marking them as untranslatable, and that would screw
     up our actual translation of the real '0'. */
  for (i = 0; i < 64; i++)
    advance (r);

  /* Read the rest of the translation table. */
  trans = pool_malloc (r->pool, 256);
  memset (trans, 0, 256);
  for (; i < 256; i++)
    {
      unsigned char c;

      advance (r);

      c = r->cc;
      if (trans[c] == 0)
        trans[c] = portable_to_local[i];
    }

  /* Set up the translation table, then read the first
     translated character. */
  r->trans = trans;
  advance (r);

  /* Skip and verify signature. */
  for (i = 0; i < 8; i++)
    if (!match (r, "SPSSPORT"[i]))
      {
        msg (SE, _("%s: Not a portable file."), fh_get_file_name (r->fh));
        longjmp (r->bail_out, 1);
      }
}

/* Reads the version and date info record, as well as product and
   subproduct identification records if present. */
static void
read_version_data (struct pfm_reader *r, struct any_read_info *info)
{
  static const char empty_string[] = "";
  char *date, *time;
  const char *product, *subproduct;
  int i;

  /* Read file. */
  if (!match (r, 'A'))
    error (r, _("Unrecognized version code `%c'."), r->cc);
  date = read_pool_string (r);
  time = read_pool_string (r);
  product = match (r, '1') ? read_pool_string (r) : empty_string;
  if (match (r, '2'))
    {
      /* Skip "author" field. */
      read_pool_string (r);
    }
  subproduct = match (r, '3') ? read_pool_string (r) : empty_string;

  /* Validate file. */
  if (strlen (date) != 8)
    error (r, _("Bad date string length %zu."), strlen (date));
  if (strlen (time) != 6)
    error (r, _("Bad time string length %zu."), strlen (time));

  /* Save file info. */
  if (info != NULL)
    {
      memset (info, 0, sizeof *info);

      info->float_format = FLOAT_NATIVE_DOUBLE;
      info->integer_format = INTEGER_NATIVE;
      info->compression = ANY_COMP_NONE;
      info->case_cnt = -1;

      /* Date. */
      info->creation_date = xmalloc (11);
      for (i = 0; i < 8; i++)
        {
          static const int map[] = {6, 7, 8, 9, 3, 4, 0, 1};
          info->creation_date[map[i]] = date[i];
        }
      info->creation_date[2] = info->creation_date[5] = ' ';
      info->creation_date[10] = '\0';

      /* Time. */
      info->creation_time = xmalloc (9);
      for (i = 0; i < 6; i++)
        {
          static const int map[] = {0, 1, 3, 4, 6, 7};
          info->creation_time[map[i]] = time[i];
        }
      info->creation_time[2] = info->creation_time[5] = ' ';
      info->creation_time[8] = 0;

      /* Product. */
      info->product = xstrdup (product);
      info->product_ext = xstrdup (subproduct);
    }
}

/* Translates a format specification read from portable file R as
   the three integers INTS into a normal format specifier FORMAT,
   checking that the format is appropriate for variable V. */
static struct fmt_spec
convert_format (struct pfm_reader *r, const int portable_format[3],
                struct variable *v, bool *report_error)
{
  struct fmt_spec format;
  bool ok;

  if (!fmt_from_io (portable_format[0], &format.type))
    {
      if (*report_error)
        warning (r, _("%s: Bad format specifier byte (%d).  Variable "
                      "will be assigned a default format."),
                 var_get_name (v), portable_format[0]);
      goto assign_default;
    }

  format.w = portable_format[1];
  format.d = portable_format[2];

  msg_disable ();
  ok = (fmt_check_output (&format)
        && fmt_check_width_compat (&format, var_get_width (v)));
  msg_enable ();

  if (!ok)
    {
      if (*report_error)
        {
          char fmt_string[FMT_STRING_LEN_MAX + 1];
          fmt_to_string (&format, fmt_string);
          if (var_is_numeric (v))
            warning (r, _("Numeric variable %s has invalid format "
                          "specifier %s."),
                     var_get_name (v), fmt_string);
          else
            warning (r, _("String variable %s with width %d has "
                          "invalid format specifier %s."),
                     var_get_name (v), var_get_width (v), fmt_string);
        }
      goto assign_default;
    }

  return format;

assign_default:
  *report_error = false;
  return fmt_default_for_width (var_get_width (v));
}

static void parse_value (struct pfm_reader *, int width, union value *);

/* Read information on all the variables.  */
static void
read_variables (struct pfm_reader *r, struct dictionary *dict)
{
  char *weight_name = NULL;
  int i;

  if (!match (r, '4'))
    error (r, _("Expected variable count record."));

  r->var_cnt = read_int (r);
  if (r->var_cnt <= 0)
    error (r, _("Invalid number of variables %d."), r->var_cnt);

  if (match (r, '5'))
    read_int (r);

  if (match (r, '6'))
    {
      weight_name = read_pool_string (r);
      if (strlen (weight_name) > SHORT_NAME_LEN)
        error (r, _("Weight variable name (%s) truncated."), weight_name);
    }

  for (i = 0; i < r->var_cnt; i++)
    {
      int width;
      char name[256];
      int fmt[6];
      struct variable *v;
      struct missing_values miss;
      struct fmt_spec print, write;
      bool report_error = true;
      int j;

      if (!match (r, '7'))
	error (r, _("Expected variable record."));

      width = read_int (r);
      if (width < 0)
	error (r, _("Invalid variable width %d."), width);

      read_string (r, name);
      for (j = 0; j < 6; j++)
        fmt[j] = read_int (r);

      if (!dict_id_is_valid (dict, name, false)
          || *name == '#' || *name == '$')
        error (r, _("Invalid variable name `%s' in position %d."), name, i);
      str_uppercase (name);

      if (width < 0 || width > 255)
	error (r, _("Bad width %d for variable %s."), width, name);

      v = dict_create_var (dict, name, width);
      if (v == NULL)
        {
          unsigned long int i;
          for (i = 1; ; i++)
            {
              char try_name[8 + 1 + INT_STRLEN_BOUND (i) + 1];
              sprintf (try_name, "%s_%lu", name, i);
              v = dict_create_var (dict, try_name, width);
              if (v != NULL)
                break;
            }
          warning (r, _("Duplicate variable name %s in position %d renamed "
                        "to %s."), name, i, var_get_name (v));
        }

      print = convert_format (r, &fmt[0], v, &report_error);
      write = convert_format (r, &fmt[3], v, &report_error);
      var_set_print_format (v, &print);
      var_set_write_format (v, &write);

      /* Range missing values. */
      mv_init (&miss, width);
      if (match (r, 'B'))
        {
          double x = read_float (r);
          double y = read_float (r);
          mv_add_range (&miss, x, y);
        }
      else if (match (r, 'A'))
        mv_add_range (&miss, read_float (r), HIGHEST);
      else if (match (r, '9'))
        mv_add_range (&miss, LOWEST, read_float (r));

      /* Single missing values. */
      while (match (r, '8'))
        {
          int mv_width = MIN (width, 8);
          union value value;

          parse_value (r, mv_width, &value);
          value_resize (&value, mv_width, width);
          mv_add_value (&miss, &value);
          value_destroy (&value, width);
        }

      var_set_missing_values (v, &miss);
      mv_destroy (&miss);

      if (match (r, 'C'))
        {
          char label[256];
          read_string (r, label);
          var_set_label (v, label); /* XXX */
        }
    }

  if (weight_name != NULL)
    {
      struct variable *weight_var = dict_lookup_var (dict, weight_name);
      if (weight_var == NULL)
        error (r, _("Weighting variable %s not present in dictionary."),
               weight_name);

      dict_set_weight (dict, weight_var);
    }
}

/* Parse a value of with WIDTH into value V. */
static void
parse_value (struct pfm_reader *r, int width, union value *v)
{
  value_init (v, width);
  if (width > 0)
    {
      uint8_t buf[256];
      size_t n_bytes = read_bytes (r, buf);
      value_copy_buf_rpad (v, width, buf, n_bytes, ' ');
    }
  else
    v->f = read_float (r);
}

/* Parse a value label record and return success. */
static void
read_value_label (struct pfm_reader *r, struct dictionary *dict)
{
  /* Variables. */
  int nv;
  struct variable **v;

  /* Labels. */
  int n_labels;

  int i;

  nv = read_int (r);
  v = pool_nalloc (r->pool, nv, sizeof *v);
  for (i = 0; i < nv; i++)
    {
      char name[256];
      read_string (r, name);

      v[i] = dict_lookup_var (dict, name);
      if (v[i] == NULL)
	error (r, _("Unknown variable %s while parsing value labels."), name);

      if (var_get_type (v[0]) != var_get_type (v[i]))
	error (r, _("Cannot assign value labels to %s and %s, which "
		    "have different variable types."),
	       var_get_name (v[0]), var_get_name (v[i]));
    }

  n_labels = read_int (r);
  for (i = 0; i < n_labels; i++)
    {
      union value val;
      char label[256];
      int j;

      parse_value (r, var_get_width (v[0]), &val);
      read_string (r, label);

      /* Assign the value label to each variable. */
      for (j = 0; j < nv; j++)
        var_replace_value_label (v[j], &val, label);

      value_destroy (&val, var_get_width (v[0]));
    }
}

/* Reads a set of documents from portable file R into DICT. */
static void
read_documents (struct pfm_reader *r, struct dictionary *dict)
{
  int line_cnt;
  int i;

  line_cnt = read_int (r);
  for (i = 0; i < line_cnt; i++)
    {
      char line[256];
      read_string (r, line);
      dict_add_document_line (dict, line, false);
    }
}

/* Reads and returns one case from portable file R.  Returns a
   null pointer on failure. */
static struct ccase *
por_file_casereader_read (struct casereader *reader, void *r_)
{
  struct pfm_reader *r = r_;
  struct ccase *volatile c;
  size_t i;

  c = case_create (r->proto);
  setjmp (r->bail_out);
  if (!r->ok)
    {
      casereader_force_error (reader);
      case_unref (c);
      return NULL;
    }

  /* Check for end of file. */
  if (r->cc == 'Z')
    {
      case_unref (c);
      return NULL;
    }

  for (i = 0; i < r->var_cnt; i++)
    {
      int width = caseproto_get_width (r->proto, i);

      if (width == 0)
        case_data_rw_idx (c, i)->f = read_float (r);
      else
        {
          uint8_t buf[256];
          size_t n_bytes = read_bytes (r, buf);
          u8_buf_copy_rpad (case_str_rw_idx (c, i), width, buf, n_bytes, ' ');
        }
    }

  return c;
}

/* Returns true if FILE is an SPSS portable file,
   false otherwise. */
int
pfm_detect (FILE *file)
{
  unsigned char header[464];
  char trans[256];
  int cooked_cnt, raw_cnt, line_len;
  int i;

  cooked_cnt = raw_cnt = 0;
  line_len = 0;
  while (cooked_cnt < sizeof header)
    {
      int c = getc (file);
      if (c == EOF || raw_cnt++ > 512)
        return 0;
      else if (c == '\n')
        {
          while (line_len < 80 && cooked_cnt < sizeof header)
            {
              header[cooked_cnt++] = ' ';
              line_len++;
            }
          line_len = 0;
        }
      else if (c != '\r')
        {
          header[cooked_cnt++] = c;
          line_len++;
        }
    }

  memset (trans, 0, 256);
  for (i = 64; i < 256; i++)
    {
      unsigned char c = header[i + 200];
      if (trans[c] == 0)
        trans[c] = portable_to_local[i];
    }

  for (i = 0; i < 8; i++)
    if (trans[header[i + 456]] != "SPSSPORT"[i])
      return 0;

  return 1;
}

static const struct casereader_class por_file_casereader_class =
  {
    por_file_casereader_read,
    por_file_casereader_destroy,
    NULL,
    NULL,
  };

const struct any_reader_class por_file_reader_class =
  {
    N_("SPSS Portable File"),
    pfm_detect,
    pfm_open,
    pfm_close,
    pfm_decode,
    NULL,                       /* get_strings */
  };
