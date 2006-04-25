/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.
   Code for parsing floating-point numbers adapted from GNU C
   library.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include "por-file-reader.h"
#include <libpspp/message.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <libpspp/alloc.h>
#include <stdbool.h>
#include "case.h"
#include <libpspp/compiler.h>
#include "dictionary.h"
#include "file-handle-def.h"
#include "format.h"
#include <libpspp/hash.h>
#include <libpspp/magic.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>
#include "value-labels.h"
#include "variable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

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
    struct pool *pool;          /* All the portable file state. */

    jmp_buf bail_out;           /* longjmp() target for error handling. */

    struct file_handle *fh;     /* File handle. */
    FILE *file;			/* File stream. */
    char cc;			/* Current character. */
    char *trans;                /* 256-byte character set translation table. */
    int var_cnt;                /* Number of variables. */
    int weight_index;		/* 0-based index of weight variable, or -1. */
    int *widths;                /* Variable widths, 0 for numeric. */
    int value_cnt;		/* Number of `value's per case. */
    bool ok;                    /* Set false on I/O error. */
  };

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

  ds_init (&text, 64);
  ds_printf (&text, _("portable file %s corrupt at offset %ld: "),
             fh_get_file_name (r->fh), ftell (r->file));
  va_start (args, msg);
  ds_vprintf (&text, msg, args);
  va_end (args);

  m.category = MSG_GENERAL;
  m.severity = MSG_ERROR;
  m.where.file_name = NULL;
  m.where.line_number = 0;
  m.text = ds_c_str (&text);
  
  msg_emit (&m);

  r->ok = false;

  longjmp (r->bail_out, 1);
}

/* Closes portable file reader R, after we're done with it. */
void
pfm_close_reader (struct pfm_reader *r)
{
  if (r != NULL)
    pool_destroy (r->pool);
}

/* Read a single character into cur_char.  */
static void
advance (struct pfm_reader *r)
{
  int c;

  while ((c = getc (r->file)) == '\r' || c == '\n')
    continue;
  if (c == EOF)
    error (r, _("unexpected end of file")); 

  if (r->trans != NULL)
    c = r->trans[c]; 
  r->cc = c;
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
static void read_version_data (struct pfm_reader *, struct pfm_read_info *);
static void read_variables (struct pfm_reader *, struct dictionary *);
static void read_value_label (struct pfm_reader *, struct dictionary *);
void dump_dictionary (struct dictionary *);

/* Reads the dictionary from file with handle H, and returns it in a
   dictionary structure.  This dictionary may be modified in order to
   rename, reorder, and delete variables, etc. */
struct pfm_reader *
pfm_open_reader (struct file_handle *fh, struct dictionary **dict,
                 struct pfm_read_info *info)
{
  struct pool *volatile pool = NULL;
  struct pfm_reader *volatile r = NULL;

  *dict = dict_create ();
  if (!fh_open (fh, FH_REF_FILE, "portable file", "rs"))
    goto error;

  /* Create and initialize reader. */
  pool = pool_create ();
  r = pool_alloc (pool, sizeof *r);
  r->pool = pool;
  if (setjmp (r->bail_out))
    goto error;
  r->fh = fh;
  r->file = pool_fopen (r->pool, fh_get_file_name (r->fh), "rb");
  r->weight_index = -1;
  r->trans = NULL;
  r->var_cnt = 0;
  r->widths = NULL;
  r->value_cnt = 0;
  r->ok = true;

  /* Check that file open succeeded, prime reading. */
  if (r->file == NULL)
    {
      msg (ME, _("An error occurred while opening \"%s\" for reading "
                 "as a portable file: %s."),
           fh_get_file_name (r->fh), strerror (errno));
      goto error;
    }
  
  /* Read header, version, date info, product id, variables. */
  read_header (r);
  read_version_data (r, info);
  read_variables (r, *dict);

  /* Read value labels. */
  while (match (r, 'D'))
    read_value_label (r, *dict);

  /* Check that we've made it to the data. */
  if (!match (r, 'F'))
    error (r, _("Data record expected."));

  return r;

 error:
  pfm_close_reader (r);
  dict_destroy (*dict);
  *dict = NULL;
  return NULL;
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
    error (r, "Number expected.");

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
read_version_data (struct pfm_reader *r, struct pfm_read_info *info)
{
  static char empty_string[] = "";
  char *date, *time, *product, *author, *subproduct;
  int i;

  /* Read file. */
  if (!match (r, 'A'))
    error (r, "Unrecognized version code `%c'.", r->cc);
  date = read_pool_string (r);
  time = read_pool_string (r);
  product = match (r, '1') ? read_pool_string (r) : empty_string;
  author = match (r, '2') ? read_pool_string (r) : empty_string;
  subproduct = match (r, '3') ? read_pool_string (r) : empty_string;

  /* Validate file. */
  if (strlen (date) != 8)
    error (r, _("Bad date string length %d."), strlen (date));
  if (strlen (time) != 6)
    error (r, _("Bad time string length %d."), strlen (time));

  /* Save file info. */
  if (info != NULL) 
    {
      /* Date. */
      for (i = 0; i < 8; i++) 
        {
          static const int map[] = {6, 7, 8, 9, 3, 4, 0, 1};
          info->creation_date[map[i]] = date[i]; 
        }
      info->creation_date[2] = info->creation_date[5] = ' ';
      info->creation_date[10] = 0;

      /* Time. */
      for (i = 0; i < 6; i++)
        {
          static const int map[] = {0, 1, 3, 4, 6, 7};
          info->creation_time[map[i]] = time[i];
        }
      info->creation_time[2] = info->creation_time[5] = ' ';
      info->creation_time[8] = 0;

      /* Product. */
      str_copy_trunc (info->product, sizeof info->product, product);
      str_copy_trunc (info->subproduct, sizeof info->subproduct, subproduct);
    }
}

/* Translates a format specification read from portable file R as
   the three integers INTS into a normal format specifier FORMAT,
   checking that the format is appropriate for variable V. */
static void
convert_format (struct pfm_reader *r, const int portable_format[3],
                struct fmt_spec *format, struct variable *v)
{
  format->type = translate_fmt (portable_format[0]);
  if (format->type == -1)
    error (r, _("%s: Bad format specifier byte (%d)."),
           v->name, portable_format[0]);
  format->w = portable_format[1];
  format->d = portable_format[2];

  if (!check_output_specifier (format, false)
      || !check_specifier_width (format, v->width, false))
    error (r, _("%s variable %s has invalid format specifier %s."),
           v->type == NUMERIC ? _("Numeric") : _("String"),
           v->name, fmt_to_string (format));
}

static union value parse_value (struct pfm_reader *, struct variable *);

/* Read information on all the variables.  */
static void
read_variables (struct pfm_reader *r, struct dictionary *dict)
{
  char *weight_name = NULL;
  int i;
  
  if (!match (r, '4'))
    error (r, _("Expected variable count record."));
  
  r->var_cnt = read_int (r);
  if (r->var_cnt <= 0 || r->var_cnt == NOT_INT)
    error (r, _("Invalid number of variables %d."), r->var_cnt);
  r->widths = pool_nalloc (r->pool, r->var_cnt, sizeof *r->widths);

  /* Purpose of this value is unknown.  It is typically 161. */
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
      int j;

      if (!match (r, '7'))
	error (r, _("Expected variable record."));

      width = read_int (r);
      if (width < 0)
	error (r, _("Invalid variable width %d."), width);
      r->widths[i] = width;

      read_string (r, name);
      for (j = 0; j < 6; j++)
        fmt[j] = read_int (r);

      if (!var_is_valid_name (name, false) || *name == '#' || *name == '$')
        error (r, _("position %d: Invalid variable name `%s'."), i, name);
      str_uppercase (name);

      if (width < 0 || width > 255)
	error (r, "Bad width %d for variable %s.", width, name);

      v = dict_create_var (dict, name, width);
      if (v == NULL)
	error (r, _("Duplicate variable name %s."), name);

      convert_format (r, &fmt[0], &v->print, v);
      convert_format (r, &fmt[3], &v->write, v);

      /* Range missing values. */
      if (match (r, 'B')) 
        {
          double x = read_float (r);
          double y = read_float (r);
          mv_add_num_range (&v->miss, x, y);
        }
      else if (match (r, 'A'))
        mv_add_num_range (&v->miss, read_float (r), HIGHEST);
      else if (match (r, '9'))
        mv_add_num_range (&v->miss, LOWEST, read_float (r));

      /* Single missing values. */
      while (match (r, '8')) 
        {
          union value value = parse_value (r, v);
          mv_add_value (&v->miss, &value); 
        }

      if (match (r, 'C')) 
        {
          char label[256];
          read_string (r, label);
          v->label = xstrdup (label); 
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

/* Parse a value for variable VV into value V. */
static union value
parse_value (struct pfm_reader *r, struct variable *vv)
{
  union value v;
  
  if (vv->type == ALPHA) 
    {
      char string[256];
      read_string (r, string);
      buf_copy_str_rpad (v.s, 8, string); 
    }
  else
    v.f = read_float (r);

  return v;
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

      if (v[0]->width != v[i]->width)
	error (r, _("Cannot assign value labels to %s and %s, which "
		    "have different variable types or widths."),
	       v[0]->name, v[i]->name);
    }

  n_labels = read_int (r);
  for (i = 0; i < n_labels; i++)
    {
      union value val;
      char label[256];
      int j;

      val = parse_value (r, v[0]);
      read_string (r, label);

      /* Assign the value_label's to each variable. */
      for (j = 0; j < nv; j++)
	{
	  struct variable *var = v[j];

	  if (!val_labs_replace (var->val_labs, val, label))
	    continue;

	  if (var->type == NUMERIC)
	    error (r, _("Duplicate label for value %g for variable %s."),
		   val.f, var->name);
	  else
	    error (r, _("Duplicate label for value `%.*s' for variable %s."),
		   var->width, val.s, var->name);
	}
    }
}

/* Reads one case from portable file R into C. */
bool
pfm_read_case (struct pfm_reader *r, struct ccase *c)
{
  size_t i;
  size_t idx;

  setjmp (r->bail_out);
  if (!r->ok)
    return false;
  
  /* Check for end of file. */
  if (r->cc == 'Z')
    return false;

  idx = 0;
  for (i = 0; i < r->var_cnt; i++) 
    {
      int width = r->widths[i];
      
      if (width == 0)
        {
          case_data_rw (c, idx)->f = read_float (r);
          idx++;
        }
      else
        {
          char string[256];
          read_string (r, string);
          buf_copy_str_rpad (case_data_rw (c, idx)->s, width, string);
          idx += DIV_RND_UP (width, MAX_SHORT_STRING);
        }
    }
  
  return true;
}

/* Returns true if an I/O error has occurred on READER, false
   otherwise. */
bool
pfm_read_error (const struct pfm_reader *reader) 
{
  return !reader->ok;
}

/* Returns true if FILE is an SPSS portable file,
   false otherwise. */
bool
pfm_detect (FILE *file) 
{
  unsigned char header[464];
  char trans[256];
  int cooked_cnt, raw_cnt;
  int i;

  cooked_cnt = raw_cnt = 0;
  while (cooked_cnt < sizeof header)
    {
      int c = getc (file);
      if (c == EOF || raw_cnt++ > 512)
        return false;
      else if (c != '\n' && c != '\r') 
        header[cooked_cnt++] = c;
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
      return false; 

  return true;
}
