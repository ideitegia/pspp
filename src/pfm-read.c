/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include "pfm-read.h"
#include "error.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include "alloc.h"
#include "case.h"
#include "dictionary.h"
#include "file-handle.h"
#include "format.h"
#include "getline.h"
#include "hash.h"
#include "magic.h"
#include "misc.h"
#include "str.h"
#include "value-labels.h"
#include "var.h"

#include "debug-print.h"

/* Portable file reader. */
struct pfm_reader
  {
    struct file_handle *fh;     /* File handle. */
    FILE *file;			/* File stream. */

    int weight_index;		/* 0-based index of weight variable, or -1. */

    unsigned char *trans;	/* 256-byte character set translation table. */

    int var_cnt;                /* Number of variables. */
    int *widths;                /* Variable widths, 0 for numeric. */
    int value_cnt;		/* Number of `value's per case. */

    unsigned char buf[83];	/* Input buffer. */
    unsigned char *bp;		/* Buffer pointer. */
    int cc;			/* Current character. */
  };

static int 
corrupt_msg (struct pfm_reader *r, const char *format,...)
     PRINTF_FORMAT (2, 3);

/* Displays a corruption error. */
static int
corrupt_msg (struct pfm_reader *r, const char *format, ...)
{
  char *title;
  struct error e;
  const char *filename;
  va_list args;

  e.class = ME;
  getl_location (&e.where.filename, &e.where.line_number);
  filename = handle_get_filename (r->fh);
  e.title = title = local_alloc (strlen (filename) + 80);
  sprintf (title, _("portable file %s corrupt at offset %ld: "),
           filename, ftell (r->file) - (82 - (long) (r->bp - r->buf)));

  va_start (args, format);
  err_vmsg (&e, format, args);
  va_end (args);

  local_free (title);

  return 0;
}

static unsigned char * read_string (struct pfm_reader *r);

/* Closes a portable file after we're done with it. */
void
pfm_close_reader (struct pfm_reader *r)
{
  if (r == NULL)
    return;

  read_string (NULL);

  if (r->fh != NULL)
    fh_close (r->fh, "portable file", "rs");
  if (fclose (r->file) == EOF)
    msg (ME, _("%s: Closing portable file: %s."),
         handle_get_filename (r->fh), strerror (errno));
  free (r->trans);
  free (r->widths);
  free (r);
}

/* Displays the message X with corrupt_msg, then jumps to the error
   label. */
#define lose(X)                                 \
	do {                                    \
	    corrupt_msg X;                      \
	    goto error;                       \
	} while (0)

/* Read an 80-character line into handle H's buffer.  Return
   success. */
static int
fill_buf (struct pfm_reader *r)
{
  if (80 != fread (r->buf, 1, 80, r->file))
    lose ((r, _("Unexpected end of file.")));

  /* PORTME: line ends. */
  {
    int c;
    
    c = getc (r->file);
    if (c != '\n' && c != '\r')
      lose ((r, _("Bad line end.")));

    c = getc (r->file);
    if (c != '\n' && c != '\r')
      ungetc (c, r->file);
  }
  
  if (r->trans)
    {
      int i;
      
      for (i = 0; i < 80; i++)
	r->buf[i] = r->trans[r->buf[i]];
    }

  r->bp = r->buf;

  return 1;

 error:
  return 0;
}

/* Read a single character into cur_char.  Return success; */
static int
read_char (struct pfm_reader *r)
{
  if (r->bp >= &r->buf[80] && !fill_buf (r))
    return 0;
  r->cc = *r->bp++;
  return 1;
}

/* Advance a single character. */
#define advance()                               \
        do {                                    \
          if (!read_char (r))                   \
            goto error;                       \
        } while (0)

/* Skip a single character if present, and return whether it was
   skipped. */
static inline int
skip_char (struct pfm_reader *r, int c)
{
  if (r->cc == c)
    {
      advance ();
      return 1;
    }
 error:
  return 0;
}

/* Skip a single character if present, and return whether it was
   skipped. */
#define match(C) skip_char (r, C)

static int read_header (struct pfm_reader *);
static int read_version_data (struct pfm_reader *, struct pfm_read_info *);
static int read_variables (struct pfm_reader *, struct dictionary *);
static int read_value_label (struct pfm_reader *, struct dictionary *);
void dump_dictionary (struct dictionary *);

/* Reads the dictionary from file with handle H, and returns it in a
   dictionary structure.  This dictionary may be modified in order to
   rename, reorder, and delete variables, etc. */
struct pfm_reader *
pfm_open_reader (struct file_handle *fh, struct dictionary **dict,
                 struct pfm_read_info *info)
{
  struct pfm_reader *r = NULL;

  *dict = dict_create ();
  if (!fh_open (fh, "portable file", "rs"))
    goto error;

  /* Create and initialize reader. */
  r = xmalloc (sizeof *r);
  r->fh = fh;
  r->file = fopen (handle_get_filename (r->fh), "rb");
  r->weight_index = -1;
  r->trans = NULL;
  r->var_cnt = 0;
  r->widths = NULL;
  r->value_cnt = 0;
  r->bp = NULL;

  /* Check that file open succeeded, prime reading. */
  if (r->file == NULL)
    {
      msg (ME, _("An error occurred while opening \"%s\" for reading "
                 "as a portable file: %s."),
           handle_get_filename (r->fh), strerror (errno));
      err_cond_fail ();
      goto error;
    }
  if (!fill_buf (r))
    goto error;
  advance ();

  /* Read header, version, date info, product id, variables. */
  if (!read_header (r)
      || !read_version_data (r, info)
      || !read_variables (r, *dict))
    goto error;

  /* Read value labels. */
  while (match (77 /* D */))
    if (!read_value_label (r, *dict))
      goto error;

  /* Check that we've made it to the data. */
  if (!match (79 /* F */))
    lose ((r, _("Data record expected.")));

  return r;

 error:
  pfm_close_reader (r);
  dict_destroy (*dict);
  *dict = NULL;
  return NULL;
}

/* Read a floating point value and return its value, or
   second_lowest_value on error. */
static double
read_float (struct pfm_reader *r)
{
  double num = 0.;
  int got_dot = 0;
  int got_digit = 0;
  int exponent = 0;
  int neg = 0;

  /* Skip leading spaces. */
  while (match (126 /* space */))
    ;

  if (match (137 /* * */))
    {
      advance ();	/* Probably a dot (.) but doesn't appear to matter. */
      return SYSMIS;
    }
  else if (match (141 /* - */))
    neg = 1;

  for (;;)
    {
      if (r->cc >= 64 /* 0 */ && r->cc <= 93 /* T */)
	{
	  got_digit++;

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
	    num = (num * 30.0) + (r->cc - 64);

	  /* Keep track of the number of digits after the decimal point.
	     If we just divided by 30 here, we would lose precision.  */
	  if (got_dot)
	    --exponent;
	}
      else if (!got_dot && r->cc == 127 /* . */)
	/* Record that we have found the decimal point.  */
	got_dot = 1;
      else
	/* Any other character terminates the number.  */
	break;

      advance ();
    }

  if (!got_digit)
    lose ((r, "Number expected."));
      
  if (r->cc == 130 /* + */ || r->cc == 141 /* - */)
    {
      /* Get the exponent.  */
      long int exp = 0;
      int neg_exp = r->cc == 141 /* - */;

      for (;;)
	{
	  advance ();

	  if (r->cc < 64 /* 0 */ || r->cc > 93 /* T */)
	    break;

	  if (exp > LONG_MAX / 30)
	    goto overflow;
	  exp = exp * 30 + (r->cc - 64);
	}

      /* We don't check whether there were actually any digits, but we
         probably should. */
      if (neg_exp)
	exp = -exp;
      exponent += exp;
    }
  
  if (!match (142 /* / */))
    lose ((r, _("Missing numeric terminator.")));

  /* Multiply NUM by 30 to the EXPONENT power, checking for overflow.  */

  if (exponent < 0)
    num *= pow (30.0, (double) exponent);
  else if (exponent > 0)
    {
      if (num > DBL_MAX * pow (30.0, (double) -exponent))
	goto overflow;
      num *= pow (30.0, (double) exponent);
    }

  if (neg)
    return -num;
  else
    return num;

 overflow:
  if (neg)
    return -DBL_MAX / 10.;
  else
    return DBL_MAX / 10;

 error:
  return second_lowest_value;
}
  
/* Read an integer and return its value, or NOT_INT on failure. */
static int
read_int (struct pfm_reader *r)
{
  double f = read_float (r);

  if (f == second_lowest_value)
    goto error;
  if (floor (f) != f || f >= INT_MAX || f <= INT_MIN)
    lose ((r, _("Bad integer format.")));
  return f;

 error:
  return NOT_INT;
}

/* Reads a string and returns its value in a static buffer, or NULL on
   failure.  The buffer can be deallocated by calling with a NULL
   argument. */
static unsigned char *
read_string (struct pfm_reader *r)
{
  static char *buf;
  int n;
  
  if (r == NULL)
    {
      free (buf);
      buf = NULL;
      return NULL;
    }
  else if (buf == NULL)
    buf = xmalloc (256);

  n = read_int (r);
  if (n == NOT_INT)
    return NULL;
  if (n < 0 || n > 255)
    lose ((r, _("Bad string length %d."), n));
  
  {
    int i;

    for (i = 0; i < n; i++)
      {
	buf[i] = r->cc;
	advance ();
      }
  }
  
  buf[n] = 0;
  return buf;

 error:
  return NULL;
}

/* Reads the 464-byte file header. */
int
read_header (struct pfm_reader *r)
{
  /* For now at least, just ignore the vanity splash strings. */
  {
    int i;

    for (i = 0; i < 200; i++)
      advance ();
  }
  
  {
    unsigned char src[256];
    int trans_temp[256];
    int i;

    for (i = 0; i < 256; i++)
      {
	src[i] = (unsigned char) r->cc;
	advance ();
      }

    for (i = 0; i < 256; i++)
      trans_temp[i] = -1;

    /* 0 is used to mark untranslatable characters, so we have to mark
       it specially. */
    trans_temp[src[64]] = 64;
    for (i = 0; i < 256; i++)
      if (trans_temp[src[i]] == -1)
	trans_temp[src[i]] = i;
    
    r->trans = xmalloc (256);
    for (i = 0; i < 256; i++)
      r->trans[i] = trans_temp[i] == -1 ? 0 : trans_temp[i];

    /* Translate the input buffer. */
    for (i = 0; i < 80; i++)
      r->buf[i] = r->trans[r->buf[i]];
    r->cc = r->trans[r->cc];
  }
  
  {
    unsigned char sig[8] = {92, 89, 92, 92, 89, 88, 91, 93};
    int i;

    for (i = 0; i < 8; i++)
      if (!match (sig[i]))
	lose ((r, "Missing SPSSPORT signature."));
  }

  return 1;

 error:
  return 0;
}

/* Reads the version and date info record, as well as product and
   subproduct identification records if present. */
int
read_version_data (struct pfm_reader *r, struct pfm_read_info *info)
{
  /* Version. */
  if (!match (74 /* A */))
    lose ((r, "Unrecognized version code %d.", r->cc));

  /* Date. */
  {
    static const int map[] = {6, 7, 8, 9, 3, 4, 0, 1};
    char *date = read_string (r);
    int i;
    
    if (!date)
      return 0;
    if (strlen (date) != 8)
      lose ((r, _("Bad date string length %d."), strlen (date)));
    for (i = 0; i < 8; i++)
      {
	if (date[i] < 64 /* 0 */ || date[i] > 73 /* 9 */)
	  lose ((r, _("Bad character in date.")));
	if (info)
	  info->creation_date[map[i]] = date[i] - 64 /* 0 */ + '0';
      }
    if (info)
      {
	info->creation_date[2] = info->creation_date[5] = ' ';
	info->creation_date[10] = 0;
      }
  }
  
  /* Time. */
  {
    static const int map[] = {0, 1, 3, 4, 6, 7};
    char *time = read_string (r);
    int i;

    if (!time)
      return 0;
    if (strlen (time) != 6)
      lose ((r, _("Bad time string length %d."), strlen (time)));
    for (i = 0; i < 6; i++)
      {
	if (time[i] < 64 /* 0 */ || time[i] > 73 /* 9 */)
	  lose ((r, _("Bad character in time.")));
	if (info)
	  info->creation_time[map[i]] = time[i] - 64 /* 0 */ + '0';
      }
    if (info)
      {
	info->creation_time[2] = info->creation_time[5] = ' ';
	info->creation_time[8] = 0;
      }
  }

  /* Product. */
  if (match (65 /* 1 */))
    {
      char *product;
      
      product = read_string (r);
      if (product == NULL)
	return 0;
      if (info)
	strncpy (info->product, product, 61);
    }
  else if (info)
    info->product[0] = 0;

  /* Subproduct. */
  if (match (67 /* 3 */))
    {
      char *subproduct;

      subproduct = read_string (r);
      if (subproduct == NULL)
	return 0;
      if (info)
	strncpy (info->subproduct, subproduct, 61);
    }
  else if (info)
    info->subproduct[0] = 0;
  return 1;
  
 error:
  return 0;
}

static int
convert_format (struct pfm_reader *r, int fmt[3], struct fmt_spec *v,
		struct variable *vv)
{
  v->type = translate_fmt (fmt[0]);
  if (v->type == -1)
    lose ((r, _("%s: Bad format specifier byte (%d)."), vv->name, fmt[0]));
  v->w = fmt[1];
  v->d = fmt[2];

  /* FIXME?  Should verify the resulting specifier more thoroughly. */

  if (v->type == -1)
    lose ((r, _("%s: Bad format specifier byte (%d)."), vv->name, fmt[0]));
  if ((vv->type == ALPHA) ^ ((formats[v->type].cat & FCAT_STRING) != 0))
    lose ((r, _("%s variable %s has %s format specifier %s."),
	   vv->type == ALPHA ? _("String") : _("Numeric"),
	   vv->name,
	   formats[v->type].cat & FCAT_STRING ? _("string") : _("numeric"),
	   formats[v->type].name));
  return 1;

 error:
  return 0;
}

/* Translation table from SPSS character code to this computer's
   native character code (which is probably ASCII). */
static const unsigned char spss2ascii[256] =
  {
    "                                                                "
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ."
    "<(+|&[]!$*);^-/|,%_>?`:$@'=\"      ~-   0123456789   -() {}\\     "
    "                                                                "
  };

/* Translate string S into ASCII. */
static void
asciify (char *s)
{
  for (; *s; s++)
    *s = spss2ascii[(unsigned char) *s];
}

static int parse_value (struct pfm_reader *, union value *, struct variable *);

/* Read information on all the variables.  */
static int
read_variables (struct pfm_reader *r, struct dictionary *dict)
{
  char *weight_name = NULL;
  int i;
  
  if (!match (68 /* 4 */))
    lose ((r, _("Expected variable count record.")));
  
  r->var_cnt = read_int (r);
  if (r->var_cnt <= 0 || r->var_cnt == NOT_INT)
    lose ((r, _("Invalid number of variables %d."), r->var_cnt));
  r->widths = xmalloc (sizeof *r->widths * r->var_cnt);

  /* Purpose of this value is unknown.  It is typically 161. */
  {
    int x = read_int (r);

    if (x == NOT_INT)
      goto error;
    if (x != 161)
      corrupt_msg (r, _("Unexpected flag value %d."), x);
  }

  if (match (70 /* 6 */))
    {
      weight_name = read_string (r);
      if (!weight_name)
	goto error;

      asciify (weight_name);
      if (strlen (weight_name) > 8) 
        {
          corrupt_msg (r, _("Weight variable name (%s) truncated."),
                       weight_name);
          weight_name[8] = '\0';
        }
    }
  
  for (i = 0; i < r->var_cnt; i++)
    {
      int width;
      unsigned char *name;
      int fmt[6];
      struct variable *v;
      int j;

      if (!match (71 /* 7 */))
	lose ((r, _("Expected variable record.")));

      width = read_int (r);
      if (width == NOT_INT)
	goto error;
      if (width < 0)
	lose ((r, _("Invalid variable width %d."), width));
      r->widths[i] = width;
      
      name = read_string (r);
      if (name == NULL)
	goto error;
      for (j = 0; j < 6; j++)
	{
	  fmt[j] = read_int (r);
	  if (fmt[j] == NOT_INT)
	    goto error;
	}

      /* Verify first character of variable name.

	 Weirdly enough, there is no # character in the SPSS portable
	 character set, so we can't check for it. */
      if (strlen (name) > 8)
	lose ((r, _("position %d: Variable name has %u characters."),
	       i, strlen (name)));
      if ((name[0] < 74 /* A */ || name[0] > 125 /* Z */)
	  && name[0] != 152 /* @ */)
	lose ((r, _("position %d: Variable name begins with invalid "
	       "character."), i));
      if (name[0] >= 100 /* a */ && name[0] <= 125 /* z */)
	{
	  corrupt_msg (r, _("position %d: Variable name begins with "
			    "lowercase letter %c."),
		       i, name[0] - 100 + 'a');
	  name[0] -= 26 /* a - A */;
	}

      /* Verify remaining characters of variable name. */
      for (j = 1; j < (int) strlen (name); j++)
	{
	  int c = name[j];

	  if (c >= 100 /* a */ && c <= 125 /* z */)
	    {
	      corrupt_msg (r, _("position %d: Variable name character %d "
				"is lowercase letter %c."),
			   i, j + 1, c - 100 + 'a');
	      name[j] -= 26 /* z - Z */;
	    }
	  else if ((c >= 64 /* 0 */ && c <= 99 /* Z */)
		   || c == 127 /* . */ || c == 152 /* @ */
		   || c == 136 /* $ */ || c == 146 /* _ */)
	    name[j] = c;
	  else
	    lose ((r, _("position %d: character `\\%03o' is not "
			"valid in a variable name."), i, c));
	}

      asciify (name);
      if (width < 0 || width > 255)
	lose ((r, "Bad width %d for variable %s.", width, name));

      v = dict_create_var (dict, name, width);
      if (v == NULL)
	lose ((r, _("Duplicate variable name %s."), name));
      if (!convert_format (r, &fmt[0], &v->print, v))
	goto error;
      if (!convert_format (r, &fmt[3], &v->write, v))
	goto error;

      /* Range missing values. */
      if (match (75 /* B */))
	{
	  v->miss_type = MISSING_RANGE;
	  if (!parse_value (r, &v->missing[0], v)
	      || !parse_value (r, &v->missing[1], v))
	    goto error;
	}
      else if (match (74 /* A */))
	{
	  v->miss_type = MISSING_HIGH;
	  if (!parse_value (r, &v->missing[0], v))
	    goto error;
	}
      else if (match (73 /* 9 */))
	{
	  v->miss_type = MISSING_LOW;
	  if (!parse_value (r, &v->missing[0], v))
	    goto error;
	}

      /* Single missing values. */
      while (match (72 /* 8 */))
	{
	  static const int map_next[MISSING_COUNT] =
	    {
	      MISSING_1, MISSING_2, MISSING_3, -1,
	      MISSING_RANGE_1, MISSING_LOW_1, MISSING_HIGH_1,
	      -1, -1, -1,
	    };

	  static const int map_ofs[MISSING_COUNT] = 
	    {
	      -1, 0, 1, 2, -1, -1, -1, 2, 1, 1,
	    };

	  v->miss_type = map_next[v->miss_type];
	  if (v->miss_type == -1)
	    lose ((r, _("Bad missing values for %s."), v->name));
	  
	  assert (map_ofs[v->miss_type] != -1);
	  if (!parse_value (r, &v->missing[map_ofs[v->miss_type]], v))
	    goto error;
	}

      if (match (76 /* C */))
	{
	  char *label = read_string (r);
	  
	  if (label == NULL)
	    goto error;

	  v->label = xstrdup (label);
	  asciify (v->label);
	}
    }

  if (weight_name != NULL) 
    {
      struct variable *weight_var = dict_lookup_var (dict, weight_name);
      if (weight_var == NULL)
        lose ((r, _("Weighting variable %s not present in dictionary."),
               weight_name));
      free (weight_name);

      dict_set_weight (dict, weight_var);
    }

  return 1;

 error:
  free (weight_name);
  return 0;
}

/* Parse a value for variable VV into value V.  Returns success. */
static int
parse_value (struct pfm_reader *r, union value *v, struct variable *vv)
{
  if (vv->type == ALPHA)
    {
      char *mv = read_string (r);
      int j;
      
      if (mv == NULL)
	return 0;

      strncpy (v->s, mv, 8);
      for (j = 0; j < 8; j++)
	if (v->s[j])
	  v->s[j] = spss2ascii[v->s[j]];
	else
	  /* Value labels are always padded with spaces. */
	  v->s[j] = ' ';
    }
  else
    {
      v->f = read_float (r);
      if (v->f == second_lowest_value)
	return 0;
    }

  return 1;
}

/* Parse a value label record and return success. */
static int
read_value_label (struct pfm_reader *r, struct dictionary *dict)
{
  /* Variables. */
  int nv;
  struct variable **v;

  /* Labels. */
  int n_labels;

  int i;

  nv = read_int (r);
  if (nv == NOT_INT)
    return 0;

  v = xmalloc (sizeof *v * nv);
  for (i = 0; i < nv; i++)
    {
      char *name = read_string (r);
      if (name == NULL)
	goto error;
      asciify (name);

      v[i] = dict_lookup_var (dict, name);
      if (v[i] == NULL)
	lose ((r, _("Unknown variable %s while parsing value labels."), name));

      if (v[0]->width != v[i]->width)
	lose ((r, _("Cannot assign value labels to %s and %s, which "
		    "have different variable types or widths."),
	       v[0]->name, v[i]->name));
    }

  n_labels = read_int (r);
  if (n_labels == NOT_INT)
    goto error;

  for (i = 0; i < n_labels; i++)
    {
      union value val;
      char *label;

      int j;
      
      if (!parse_value (r, &val, v[0]))
	goto error;
      
      label = read_string (r);
      if (label == NULL)
	goto error;
      asciify (label);

      /* Assign the value_label's to each variable. */
      for (j = 0; j < nv; j++)
	{
	  struct variable *var = v[j];

	  if (!val_labs_replace (var->val_labs, val, label))
	    continue;

	  if (var->type == NUMERIC)
	    lose ((r, _("Duplicate label for value %g for variable %s."),
		   val.f, var->name));
	  else
	    lose ((r, _("Duplicate label for value `%.*s' for variable %s."),
		   var->width, val.s, var->name));
	}
    }
  free (v);
  return 1;

 error:
  free (v);
  return 0;
}

/* Reads one case from portable file R into C.  Returns nonzero
   only if successful. */
int
pfm_read_case (struct pfm_reader *r, struct ccase *c)
{
  size_t i;
  size_t idx;

  /* Check for end of file. */
  if (r->cc == 99 /* Z */)
    return 0;
  
  idx = 0;
  for (i = 0; i < r->var_cnt; i++) 
    {
      int width = r->widths[i];
      
      if (width == 0)
        {
          double f = read_float (r);
          if (f == second_lowest_value)
            goto unexpected_eof;

          case_data_rw (c, idx)->f = f;
          idx++;
        }
      else
        {
          char *s = read_string (r);
          if (s == NULL)
            goto unexpected_eof;
          asciify (s);

          st_bare_pad_copy (case_data_rw (c, idx)->s, s, width);
          idx += DIV_RND_UP (width, MAX_SHORT_STRING);
        }
    }
  
  return 1;

 unexpected_eof:
  lose ((r, _("End of file midway through case.")));

 error:
  return 0;
}
