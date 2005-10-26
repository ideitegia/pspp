/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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
#include "sfm-read.h"
#include "sfmP.h"
#include "error.h"
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <setjmp.h>
#include "alloc.h"
#include "case.h"
#include "dictionary.h"
#include "error.h"
#include "file-handle.h"
#include "filename.h"
#include "format.h"
#include "getl.h"
#include "hash.h"
#include "magic.h"
#include "misc.h"
#include "value-labels.h"
#include "str.h"
#include "var.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include "debug-print.h"

/* System file reader. */
struct sfm_reader
  {
    struct file_handle *fh;     /* File handle. */
    FILE *file;			/* File stream. */

    int reverse_endian;		/* 1=file has endianness opposite us. */
    int fix_specials;           /* 1=SYSMIS/HIGHEST/LOWEST differs from us. */
    int value_cnt;		/* Number of `union values's per case. */
    long case_cnt;		/* Number of cases, -1 if unknown. */
    int compressed;		/* 1=compressed, 0=not compressed. */
    double bias;		/* Compression bias, usually 100.0. */
    int weight_idx;		/* 0-based index of weighting variable, or -1. */

    /* Variables. */
    struct sfm_var *vars;       /* Variables. */

    /* File's special constants. */
    flt64 sysmis;
    flt64 highest;
    flt64 lowest;

    /* Decompression buffer. */
    flt64 *buf;			/* Buffer data. */
    flt64 *ptr;			/* Current location in buffer. */
    flt64 *end;			/* End of buffer data. */

    /* Compression instruction octet. */
    unsigned char x[8];         /* Current instruction octet. */
    unsigned char *y;		/* Location in current instruction octet. */
  };

/* A variable in a system file. */
struct sfm_var 
  {
    int width;                  /* 0=numeric, otherwise string width. */
    int fv;                     /* Index into case. */
  };

/* Utilities. */

/* Swap bytes *A and *B. */
static inline void
bswap (char *a, char *b) 
{
  char t = *a;
  *a = *b;
  *b = t;
}

/* Reverse the byte order of 32-bit integer *X. */
static inline void
bswap_int32 (int32 *x_)
{
  char *x = (char *) x_;
  bswap (x + 0, x + 3);
  bswap (x + 1, x + 2);
}

/* Reverse the byte order of 64-bit floating point *X. */
static inline void
bswap_flt64 (flt64 *x_)
{
  char *x = (char *) x_;
  bswap (x + 0, x + 7);
  bswap (x + 1, x + 6);
  bswap (x + 2, x + 5);
  bswap (x + 3, x + 4);
}

static void
corrupt_msg (int class, const char *format,...)
     PRINTF_FORMAT (2, 3);

/* Displays a corrupt sysfile error. */
static void
corrupt_msg (int class, const char *format,...)
{
  struct error e;
  va_list args;

  e.class = class;
  getl_location (&e.where.filename, &e.where.line_number);
  e.title = _("corrupt system file: ");

  va_start (args, format);
  err_vmsg (&e, format, args);
  va_end (args);
}

/* Closes a system file after we're done with it. */
void
sfm_close_reader (struct sfm_reader *r)
{
  if (r == NULL)
    return;

  if (r->fh != NULL)
    fh_close (r->fh, "system file", "rs");
  
  if ( r->file ) {
    if (fn_close (handle_get_filename (r->fh), r->file) == EOF)
      msg (ME, _("%s: Closing system file: %s."),
	   handle_get_filename (r->fh), strerror (errno));
    r->file = NULL;
  }
  free (r->vars);
  free (r->buf);
  free (r);
}

/* Dictionary reader. */

static void buf_unread(struct sfm_reader *r, size_t byte_cnt);

static void *buf_read (struct sfm_reader *, void *buf, size_t byte_cnt,
                       size_t min_alloc);

static int read_header (struct sfm_reader *,
                        struct dictionary *, struct sfm_read_info *);
static int parse_format_spec (struct sfm_reader *, int32,
			      struct fmt_spec *, struct variable *);
static int read_value_labels (struct sfm_reader *, struct dictionary *,
                              struct variable **var_by_idx);
static int read_variables (struct sfm_reader *,
                           struct dictionary *, struct variable ***var_by_idx);
static int read_machine_int32_info (struct sfm_reader *, int size, int count);
static int read_machine_flt64_info (struct sfm_reader *, int size, int count);
static int read_documents (struct sfm_reader *, struct dictionary *);

static int fread_ok (struct sfm_reader *, void *, size_t);

/* Displays the message X with corrupt_msg, then jumps to the error
   label. */
#define lose(X)                                 \
	do {                                    \
	    corrupt_msg X;                      \
	    goto error;                         \
	} while (0)

/* Calls buf_read with the specified arguments, and jumps to
   error if the read fails. */
#define assertive_buf_read(a,b,c,d)             \
	do {                                    \
	    if (!buf_read (a,b,c,d))            \
	      goto error;                       \
	} while (0)

/* Opens the system file designated by file handle FH for
   reading.  Reads the system file's dictionary into *DICT.
   If INFO is non-null, then it receives additional info about the
   system file. */
struct sfm_reader *
sfm_open_reader (struct file_handle *fh, struct dictionary **dict,
                 struct sfm_read_info *info)
{
  struct sfm_reader *r = NULL;
  struct variable **var_by_idx = NULL;

  *dict = dict_create ();
  if (!fh_open (fh, "system file", "rs"))
    goto error;

  /* Create and initialize reader. */
  r = xmalloc (sizeof *r);
  r->fh = fh;
  r->file = fn_open (handle_get_filename (fh), "rb");

  r->reverse_endian = 0;
  r->fix_specials = 0;
  r->value_cnt = 0;
  r->case_cnt = 0;
  r->compressed = 0;
  r->bias = 100.0;
  r->weight_idx = -1;

  r->vars = NULL;

  r->sysmis = -FLT64_MAX;
  r->highest = FLT64_MAX;
  r->lowest = second_lowest_flt64;

  r->buf = r->ptr = r->end = NULL;
  r->y = r->x + sizeof r->x;

  /* Check that file open succeeded. */
  if (r->file == NULL)
    {
      msg (ME, _("An error occurred while opening \"%s\" for reading "
                 "as a system file: %s."),
           handle_get_filename (r->fh), strerror (errno));
      err_cond_fail ();
      goto error;
    }

  /* Read header and variables. */
  if (!read_header (r, *dict, info) || !read_variables (r, *dict, &var_by_idx))
    goto error;


  /* Handle weighting. */
  if (r->weight_idx != -1)
    {
      struct variable *weight_var;

      if (r->weight_idx < 0 || r->weight_idx >= r->value_cnt)
	lose ((ME, _("%s: Index of weighting variable (%d) is not between 0 "
		     "and number of elements per case (%d)."),
	       handle_get_filename (r->fh), r->weight_idx, r->value_cnt));


      weight_var = var_by_idx[r->weight_idx];

      if (weight_var == NULL)
	lose ((ME,
               _("%s: Weighting variable may not be a continuation of "
	       "a long string variable."), handle_get_filename (fh)));
      else if (weight_var->type == ALPHA)
	lose ((ME, _("%s: Weighting variable may not be a string variable."),
	       handle_get_filename (fh)));

      dict_set_weight (*dict, weight_var);
    }
  else
    dict_set_weight (*dict, NULL);

  /* Read records of types 3, 4, 6, and 7. */
  for (;;)
    {
      int32 rec_type;

      assertive_buf_read (r, &rec_type, sizeof rec_type, 0);
      if (r->reverse_endian)
	bswap_int32 (&rec_type);

      switch (rec_type)
	{
	case 3:
	  if (!read_value_labels (r, *dict, var_by_idx))
	    goto error;
	  break;

	case 4:
	  lose ((ME, _("%s: Orphaned variable index record (type 4).  Type 4 "
                       "records must always immediately follow type 3 "
                       "records."),
		 handle_get_filename (r->fh)));

	case 6:
	  if (!read_documents (r, *dict))
	    goto error;
	  break;

	case 7:
	  {
	    struct
	      {
		int32 subtype P;
		int32 size P;
		int32 count P;
	      }
	    data;
            unsigned long bytes;

	    int skip = 0;

	    assertive_buf_read (r, &data, sizeof data, 0);
	    if (r->reverse_endian)
	      {
		bswap_int32 (&data.subtype);
		bswap_int32 (&data.size);
		bswap_int32 (&data.count);
	      }
            bytes = data.size * data.count;
            if (bytes < data.size || bytes < data.count)
              lose ((ME, "%s: Record type %d subtype %d too large.",
                     handle_get_filename (r->fh), rec_type, data.subtype));

	    switch (data.subtype)
	      {
	      case 3:
		if (!read_machine_int32_info (r, data.size, data.count))
		  goto error;
		break;

	      case 4:
		if (!read_machine_flt64_info (r, data.size, data.count))
		  goto error;
		break;

	      case 5:
	      case 6:  /* ?? Used by SPSS 8.0. */
		skip = 1;
		break;
		
	      case 11: /* Variable display parameters */
		{
		  const int  n_vars = data.count / 3 ;
		  int i;
		  if ( data.count % 3 || n_vars > dict_get_var_cnt(*dict) ) 
		    {
		      msg (MW, _("%s: Invalid subrecord length. "
				 "Record: 7; Subrecord: 11"), 
			   handle_get_filename (r->fh));
		      skip = 1;
		    }

		  for ( i = 0 ; i < min(n_vars, dict_get_var_cnt(*dict)) ; ++i ) 
		    {
		      struct
		      {
			int32 measure P;
			int32 width P;
			int32 align P;
		      }
		      params;

		      struct variable *v;

		      assertive_buf_read (r, &params, sizeof(params), 0);

		      v = dict_get_var(*dict, i);

		      v->measure = params.measure;
		      v->display_width = params.width;
		      v->alignment = params.align;
		    }
		}
		break;

	      case 13: /* SPSS 12.0 Long variable name map */
		{
		  char *buf, *short_name, *save_ptr;
                  int idx;

                  /* Read data. */
                  buf = xmalloc (bytes + 1);
		  if (!buf_read (r, buf, bytes, 0)) 
                    {
                      free (buf);
                      goto error;
                    }
		  buf[bytes] = '\0';

                  /* Parse data. */
		  for (short_name = strtok_r (buf, "=", &save_ptr), idx = 0;
                       short_name != NULL;
                       short_name = strtok_r (NULL, "=", &save_ptr), idx++)
		    {
                      char *long_name = strtok_r (NULL, "\t", &save_ptr);
                      struct variable *v;

                      /* Validate long name. */
                      if (long_name == NULL)
                        {
                          msg (MW, _("%s: Trailing garbage in long variable "
                                     "name map."),
                               handle_get_filename (r->fh));
                          break;
                        }
                      if (!var_is_valid_name (long_name, false))
                        {
                          msg (MW, _("%s: Long variable mapping to invalid "
                                     "variable name `%s'."),
                               handle_get_filename (r->fh), long_name);
                          break;
                        }
                      
                      /* Find variable using short name. */
                      v = dict_lookup_var (*dict, short_name);
                      if (v == NULL)
                        {
                          msg (MW, _("%s: Long variable mapping for "
                                     "nonexistent variable %s."),
                               handle_get_filename (r->fh), short_name);
                          break;
                        }

                      /* Identify any duplicates. */
		      if ( compare_var_names(short_name, long_name, 0) &&
			  NULL != dict_lookup_var (*dict, long_name))
                        {
			  lose ((ME, _("%s: Duplicate long variable name `%s' "
				       "within system file."),
				 handle_get_filename (r->fh), long_name));
                          break;
                        }

                      /* Set long name.
                         Renaming a variable may clear the short
                         name, but we want to retain it, so
                         re-set it explicitly. */
                      dict_rename_var (*dict, v, long_name);
                      var_set_short_name (v, short_name);

                      /* For compatability, make sure dictionary
                         is in long variable name map order.  In
                         the common case, this has no effect,
                         because the dictionary and the long
                         variable name map are already in the
                         same order. */
                      dict_reorder_var (*dict, v, idx);
		    }

		  /* Free data. */
		  free (buf);
		}
		break;

	      default:
		msg (MW, _("%s: Unrecognized record type 7, subtype %d "
                           "encountered in system file."),
                     handle_get_filename (r->fh), data.subtype);
		skip = 1;
	      }

	    if (skip)
	      {
		void *x = buf_read (r, NULL, data.size * data.count, 0);
		if (x == NULL)
		  goto error;
		free (x);
	      }
	  }
	  break;

	case 999:
	  {
	    int32 filler;

	    assertive_buf_read (r, &filler, sizeof filler, 0);
	    goto success;
	  }

	default:
	  corrupt_msg(MW, _("%s: Unrecognized record type %d."),
                 handle_get_filename (r->fh), rec_type);
	}
    }

success:
  /* Come here on successful completion. */
  free (var_by_idx);
  return r;

error:
  /* Come here on unsuccessful completion. */
  sfm_close_reader (r);
  free (var_by_idx);
  if (*dict != NULL) 
    {
      dict_destroy (*dict);
      *dict = NULL; 
    }
  return NULL;
}

/* Read record type 7, subtype 3. */
static int
read_machine_int32_info (struct sfm_reader *r, int size, int count)
{
  int32 data[8];
  int file_bigendian;

  int i;

  if (size != sizeof (int32) || count != 8)
    lose ((ME, _("%s: Bad size (%d) or count (%d) field on record type 7, "
                 "subtype 3.	Expected size %d, count 8."),
	   handle_get_filename (r->fh), size, count, sizeof (int32)));

  assertive_buf_read (r, data, sizeof data, 0);
  if (r->reverse_endian)
    for (i = 0; i < 8; i++)
      bswap_int32 (&data[i]);

#ifdef FPREP_IEEE754
  if (data[4] != 1)
    lose ((ME, _("%s: Floating-point representation in system file is not "
                 "IEEE-754.  PSPP cannot convert between floating-point "
                 "formats."),
           handle_get_filename (r->fh)));
#else
#error Add support for your floating-point format.
#endif

#ifdef WORDS_BIGENDIAN
  file_bigendian = 1;
#else
  file_bigendian = 0;
#endif
  if (r->reverse_endian)
    file_bigendian ^= 1;
  if (file_bigendian ^ (data[6] == 1))
    lose ((ME, _("%s: File-indicated endianness (%s) does not match "
                 "endianness intuited from file header (%s)."),
	   handle_get_filename (r->fh),
           file_bigendian ? _("big-endian") : _("little-endian"),
	   data[6] == 1 ? _("big-endian") : (data[6] == 2 ? _("little-endian")
					  : _("unknown"))));

  /* PORTME: Character representation code. */
  if (data[7] != 2 && data[7] != 3) 
    lose ((ME, _("%s: File-indicated character representation code (%s) is "
                 "not ASCII."),
           handle_get_filename (r->fh),
           (data[7] == 1 ? "EBCDIC"
            : (data[7] == 4 ? _("DEC Kanji") : _("Unknown")))));

  return 1;

error:
  return 0;
}

/* Read record type 7, subtype 4. */
static int
read_machine_flt64_info (struct sfm_reader *r, int size, int count)
{
  flt64 data[3];
  int i;

  if (size != sizeof (flt64) || count != 3)
    lose ((ME, _("%s: Bad size (%d) or count (%d) field on record type 7, "
                 "subtype 4.	Expected size %d, count 8."),
	   handle_get_filename (r->fh), size, count, sizeof (flt64)));

  assertive_buf_read (r, data, sizeof data, 0);
  if (r->reverse_endian)
    for (i = 0; i < 3; i++)
      bswap_flt64 (&data[i]);

  if (data[0] != SYSMIS || data[1] != FLT64_MAX
      || data[2] != second_lowest_flt64)
    {
      r->sysmis = data[0];
      r->highest = data[1];
      r->lowest = data[2];
      msg (MW, _("%s: File-indicated value is different from internal value "
		 "for at least one of the three system values.  SYSMIS: "
		 "indicated %g, expected %g; HIGHEST: %g, %g; LOWEST: "
		 "%g, %g."),
	   handle_get_filename (r->fh), (double) data[0], (double) SYSMIS,
	   (double) data[1], (double) FLT64_MAX,
	   (double) data[2], (double) second_lowest_flt64);
    }
  
  return 1;

error:
  return 0;
}

static int
read_header (struct sfm_reader *r,
             struct dictionary *dict, struct sfm_read_info *info)
{
  struct sysfile_header hdr;		/* Disk buffer. */
  char prod_name[sizeof hdr.prod_name + 1];	/* Buffer for product name. */
  int skip_amt = 0;			/* Amount of product name to omit. */
  int i;

  /* Read header, check magic. */
  assertive_buf_read (r, &hdr, sizeof hdr, 0);
  if (strncmp ("$FL2", hdr.rec_type, 4) != 0)
    lose ((ME, _("%s: Bad magic.  Proper system files begin with "
		 "the four characters `$FL2'. This file will not be read."),
	   handle_get_filename (r->fh)));

  /* Check eye-catcher string. */
  memcpy (prod_name, hdr.prod_name, sizeof hdr.prod_name);
  for (i = 0; i < 60; i++)
    if (!isprint ((unsigned char) prod_name[i]))
      prod_name[i] = ' ';
  for (i = 59; i >= 0; i--)
    if (!isgraph ((unsigned char) prod_name[i]))
      {
	prod_name[i] = '\0';
	break;
      }
  prod_name[60] = '\0';
  
  {
#define N_PREFIXES 2
    static const char *prefix[N_PREFIXES] =
      {
	"@(#) SPSS DATA FILE",
	"SPSS SYSTEM FILE.",
      };

    int i;

    for (i = 0; i < N_PREFIXES; i++)
      if (!strncmp (prefix[i], hdr.prod_name, strlen (prefix[i])))
	{
	  skip_amt = strlen (prefix[i]);
	  break;
	}
  }
  
  /* Check endianness. */
  if (hdr.layout_code == 2)
    r->reverse_endian = 0;
  else
    {
      bswap_int32 (&hdr.layout_code);
      if (hdr.layout_code != 2)
	lose ((ME, _("%s: File layout code has unexpected value %d.  Value "
                     "should be 2, in big-endian or little-endian format."),
	       handle_get_filename (r->fh), hdr.layout_code));

      r->reverse_endian = 1;
      bswap_int32 (&hdr.case_size);
      bswap_int32 (&hdr.compress);
      bswap_int32 (&hdr.weight_idx);
      bswap_int32 (&hdr.case_cnt);
      bswap_flt64 (&hdr.bias);
    }


  /* Copy basic info and verify correctness. */
  r->value_cnt = hdr.case_size;

  /* If value count is rediculous, then force it to -1 (a sentinel value) */
  if ( r->value_cnt < 0 || 
       r->value_cnt > (INT_MAX / (int) sizeof (union value) / 2))
    r->value_cnt = -1;

  r->compressed = hdr.compress;

  r->weight_idx = hdr.weight_idx - 1;

  r->case_cnt = hdr.case_cnt;
  if (r->case_cnt < -1 || r->case_cnt > INT_MAX / 2)
    lose ((ME,
           _("%s: Number of cases in file (%ld) is not between -1 and %d."),
           handle_get_filename (r->fh), (long) r->case_cnt, INT_MAX / 2));

  r->bias = hdr.bias;
  if (r->bias != 100.0)
    corrupt_msg (MW, _("%s: Compression bias (%g) is not the usual "
                       "value of 100."),
                 handle_get_filename (r->fh), r->bias);

  /* Make a file label only on the condition that the given label is
     not all spaces or nulls. */
  {
    int i;

    for (i = sizeof hdr.file_label - 1; i >= 0; i--)
      if (!isspace ((unsigned char) hdr.file_label[i])
	  && hdr.file_label[i] != 0)
	{
          char *label = xmalloc (i + 2);
	  memcpy (label, hdr.file_label, i + 1);
	  label[i + 1] = 0;
          dict_set_label (dict, label);
          free (label);
	  break;
	}
  }

  if (info)
    {
      char *cp;

      memcpy (info->creation_date, hdr.creation_date, 9);
      info->creation_date[9] = 0;

      memcpy (info->creation_time, hdr.creation_time, 8);
      info->creation_time[8] = 0;

#ifdef WORDS_BIGENDIAN
      info->big_endian = !r->reverse_endian;
#else
      info->big_endian = r->reverse_endian;
#endif

      info->compressed = hdr.compress;

      info->case_cnt = hdr.case_cnt;

      for (cp = &prod_name[skip_amt]; cp < &prod_name[60]; cp++)
	if (isgraph ((unsigned char) *cp))
	  break;
      strcpy (info->product, cp);
    }

  return 1;

error:
  return 0;
}

/* Reads most of the dictionary from file H; also fills in the
   associated VAR_BY_IDX array. */
static int
read_variables (struct sfm_reader *r,
                struct dictionary *dict, struct variable ***var_by_idx)
{
  int i;

  struct sysfile_variable sv;		/* Disk buffer. */
  int long_string_count = 0;	/* # of long string continuation
				   records still expected. */
  int next_value = 0;		/* Index to next `value' structure. */

  assert(r);

  *var_by_idx = 0;

  /* Pre-allocate variables. */
  if (r->value_cnt != -1) 
    {
      *var_by_idx = xnmalloc (r->value_cnt, sizeof **var_by_idx);
      r->vars = xnmalloc (r->value_cnt, sizeof *r->vars);
    }


  /* Read in the entry for each variable and use the info to
     initialize the dictionary. */
  for (i = 0; ; ++i)
    {
      struct variable *vv;
      char name[SHORT_NAME_LEN + 1];
      int nv;
      int j;

      if ( r->value_cnt != -1  && i >= r->value_cnt ) 
	break;

      assertive_buf_read (r, &sv, sizeof sv, 0);

      if (r->reverse_endian)
	{
	  bswap_int32 (&sv.rec_type);
	  bswap_int32 (&sv.type);
	  bswap_int32 (&sv.has_var_label);
	  bswap_int32 (&sv.n_missing_values);
	  bswap_int32 (&sv.print);
	  bswap_int32 (&sv.write);
	}

      /* We've come to the end of the variable entries */
      if (sv.rec_type != 2)
	{
	  buf_unread(r, sizeof sv);
	  r->value_cnt = i;
	  break;
	}

      if ( -1 == r->value_cnt ) 
	{
	  *var_by_idx = xnrealloc (*var_by_idx, i + 1, sizeof **var_by_idx);
	  r->vars = xnrealloc (r->vars, i + 1, sizeof *r->vars);
	}

      /* If there was a long string previously, make sure that the
	 continuations are present; otherwise make sure there aren't
	 any. */
      if (long_string_count)
	{
	  if (sv.type != -1)
	    lose ((ME, _("%s: position %d: String variable does not have "
			 "proper number of continuation records."),
                   handle_get_filename (r->fh), i));


	  r->vars[i].width = -1;
	  (*var_by_idx)[i] = NULL;
	  long_string_count--;
	  continue;
	}
      else if (sv.type == -1)
	lose ((ME, _("%s: position %d: Superfluous long string continuation "
                     "record."),
               handle_get_filename (r->fh), i));

      /* Check fields for validity. */
      if (sv.type < 0 || sv.type > 255)
	lose ((ME, _("%s: position %d: Bad variable type code %d."),
	       handle_get_filename (r->fh), i, sv.type));
      if (sv.has_var_label != 0 && sv.has_var_label != 1)
	lose ((ME, _("%s: position %d: Variable label indicator field is not "
	       "0 or 1."), handle_get_filename (r->fh), i));
      if (sv.n_missing_values < -3 || sv.n_missing_values > 3
	  || sv.n_missing_values == -1)
	lose ((ME, _("%s: position %d: Missing value indicator field is not "
		     "-3, -2, 0, 1, 2, or 3."), handle_get_filename (r->fh), i));

      /* Copy first character of variable name. */
      if (!isalpha ((unsigned char) sv.name[0])
	  && sv.name[0] != '@' && sv.name[0] != '#')
	lose ((ME, _("%s: position %d: Variable name begins with invalid "
                     "character."),
               handle_get_filename (r->fh), i));
      if (islower ((unsigned char) sv.name[0]))
	msg (MW, _("%s: position %d: Variable name begins with lowercase letter "
                   "%c."),
             handle_get_filename (r->fh), i, sv.name[0]);
      if (sv.name[0] == '#')
	msg (MW, _("%s: position %d: Variable name begins with octothorpe "
		   "(`#').  Scratch variables should not appear in system "
		   "files."),
             handle_get_filename (r->fh), i);
      name[0] = toupper ((unsigned char) (sv.name[0]));

      /* Copy remaining characters of variable name. */
      for (j = 1; j < SHORT_NAME_LEN; j++)
	{
	  int c = (unsigned char) sv.name[j];

	  if (isspace (c))
	    break;
	  else if (islower (c))
	    {
	      msg (MW, _("%s: position %d: Variable name character %d is "
                         "lowercase letter %c."),
                   handle_get_filename (r->fh), i, j + 1, sv.name[j]);
	      name[j] = toupper ((unsigned char) (c));
	    }
	  else if (isalnum (c) || c == '.' || c == '@'
		   || c == '#' || c == '$' || c == '_')
	    name[j] = c;
	  else
	    lose ((ME, _("%s: position %d: character `\\%03o' (%c) is not valid in a "
                         "variable name."),
                   handle_get_filename (r->fh), i, c, c));
	}
      name[j] = 0;

      if ( ! var_is_valid_name(name, false) ) 
        lose ((ME, _("%s: Invalid variable name `%s' within system file."),
               handle_get_filename (r->fh), name));

      /* Create variable. */

      vv = (*var_by_idx)[i] = dict_create_var (dict, name, sv.type);
      if (vv == NULL) 
        lose ((ME, _("%s: Duplicate variable name `%s' within system file."),
               handle_get_filename (r->fh), name));

      var_set_short_name (vv, vv->name);

      /* Case reading data. */
      nv = sv.type == 0 ? 1 : DIV_RND_UP (sv.type, sizeof (flt64));
      long_string_count = nv - 1;
      next_value += nv;

      /* Get variable label, if any. */
      if (sv.has_var_label == 1)
	{
	  /* Disk buffer. */
	  int32 len;

	  /* Read length of label. */
	  assertive_buf_read (r, &len, sizeof len, 0);
	  if (r->reverse_endian)
	    bswap_int32 (&len);

	  /* Check len. */
	  if (len < 0 || len > 255)
	    lose ((ME, _("%s: Variable %s indicates variable label of invalid "
                         "length %d."),
                   handle_get_filename (r->fh), vv->name, len));

	  if ( len != 0 ) 
	    {
	      /* Read label into variable structure. */
	      vv->label = buf_read (r, NULL, ROUND_UP (len, sizeof (int32)), len + 1);
	      if (vv->label == NULL)
		goto error;
	      vv->label[len] = '\0';
	    }
	}

      /* Set missing values. */
      if (sv.n_missing_values != 0)
	{
	  flt64 mv[3];
          int mv_cnt = abs (sv.n_missing_values);

	  if (vv->width > MAX_SHORT_STRING)
	    lose ((ME, _("%s: Long string variable %s may not have missing "
                         "values."),
                   handle_get_filename (r->fh), vv->name));

	  assertive_buf_read (r, mv, sizeof *mv * mv_cnt, 0);

	  if (r->reverse_endian && vv->type == NUMERIC)
	    for (j = 0; j < mv_cnt; j++)
	      bswap_flt64 (&mv[j]);

	  if (sv.n_missing_values > 0)
	    {
              for (j = 0; j < sv.n_missing_values; j++)
                if (vv->type == NUMERIC)
                  mv_add_num (&vv->miss, mv[j]);
                else
                  mv_add_str (&vv->miss, (char *) &mv[j]);
	    }
	  else
	    {
	      if (vv->type == ALPHA)
		lose ((ME, _("%s: String variable %s may not have missing "
                             "values specified as a range."),
                       handle_get_filename (r->fh), vv->name));

	      if (mv[0] == r->lowest)
                mv_add_num_range (&vv->miss, LOWEST, mv[1]);
	      else if (mv[1] == r->highest)
                mv_add_num_range (&vv->miss, mv[0], HIGHEST);
	      else
                mv_add_num_range (&vv->miss, mv[0], mv[1]);

	      if (sv.n_missing_values == -3)
                mv_add_num (&vv->miss, mv[2]);
	    }
	}

      if (!parse_format_spec (r, sv.print, &vv->print, vv)
	  || !parse_format_spec (r, sv.write, &vv->write, vv))
	goto error;

      r->vars[i].width = vv->width;
      r->vars[i].fv = vv->fv;

    }

  /* Some consistency checks. */
  if (long_string_count != 0)
    lose ((ME, _("%s: Long string continuation records omitted at end of "
                 "dictionary."),
           handle_get_filename (r->fh)));

  if (next_value != r->value_cnt)
    corrupt_msg(MW, _("%s: System file header indicates %d variable positions but "
                 "%d were read from file."),
           handle_get_filename (r->fh), r->value_cnt, next_value);


  return 1;

error:
  return 0;
}

/* Translates the format spec from sysfile format to internal
   format. */
static int
parse_format_spec (struct sfm_reader *r, int32 s,
                   struct fmt_spec *f, struct variable *v)
{
  f->type = translate_fmt ((s >> 16) & 0xff);
  if (f->type == -1)
    lose ((ME, _("%s: Bad format specifier byte (%d)."),
	   handle_get_filename (r->fh), (s >> 16) & 0xff));
  f->w = (s >> 8) & 0xff;
  f->d = s & 0xff;

  if ((v->type == ALPHA) ^ ((formats[f->type].cat & FCAT_STRING) != 0))
    lose ((ME, _("%s: %s variable %s has %s format specifier %s."),
	   handle_get_filename (r->fh),
           v->type == ALPHA ? _("String") : _("Numeric"),
	   v->name,
	   formats[f->type].cat & FCAT_STRING ? _("string") : _("numeric"),
	   formats[f->type].name));

  if (!check_output_specifier (f, false)
      || !check_specifier_width (f, v->width, false)) 
    {
      msg (ME, _("%s variable %s has invalid format specifier %s."),
           v->type == NUMERIC ? _("Numeric") : _("String"),
           v->name, fmt_to_string (f));
      *f = v->type == NUMERIC ? f8_2 : make_output_format (FMT_A, v->width, 0);
    }
  return 1;

error:
  return 0;
}

/* Reads value labels from sysfile H and inserts them into the
   associated dictionary. */
int
read_value_labels (struct sfm_reader *r,
                   struct dictionary *dict, struct variable **var_by_idx)
{
  struct label 
    {
      char raw_value[8];        /* Value as uninterpreted bytes. */
      union value value;        /* Value. */
      char *label;              /* Null-terminated label string. */
    };

  struct label *labels = NULL;
  int32 n_labels;		/* Number of labels. */

  struct variable **var = NULL;	/* Associated variables. */
  int32 n_vars;			/* Number of associated variables. */

  int i;

  /* First step: read the contents of the type 3 record and record its
     contents.	Note that we can't do much with the data since we
     don't know yet whether it is of numeric or string type. */

  /* Read number of labels. */
  assertive_buf_read (r, &n_labels, sizeof n_labels, 0);
  if (r->reverse_endian)
    bswap_int32 (&n_labels);

  if ( n_labels >= ((int32) ~0) / sizeof *labels)
    {    
      corrupt_msg(MW, _("%s: Invalid number of labels: %d.  Ignoring labels."),
		  handle_get_filename (r->fh), n_labels);
      n_labels = 0;
    }

  /* Allocate memory. */
  labels = xcalloc (n_labels, sizeof *labels);
  for (i = 0; i < n_labels; i++)
    labels[i].label = NULL;

  /* Read each value/label tuple into labels[]. */
  for (i = 0; i < n_labels; i++)
    {
      struct label *label = labels + i;
      unsigned char label_len;
      size_t padded_len;

      /* Read value. */
      assertive_buf_read (r, label->raw_value, sizeof label->raw_value, 0);

      /* Read label length. */
      assertive_buf_read (r, &label_len, sizeof label_len, 0);
      padded_len = ROUND_UP (label_len + 1, sizeof (flt64));

      /* Read label, padding. */
      label->label = xmalloc (padded_len + 1);
      assertive_buf_read (r, label->label, padded_len - 1, 0);
      label->label[label_len] = 0;
    }

  /* Second step: Read the type 4 record that has the list of
     variables to which the value labels are to be applied. */

  /* Read record type of type 4 record. */
  {
    int32 rec_type;
    
    assertive_buf_read (r, &rec_type, sizeof rec_type, 0);
    if (r->reverse_endian)
      bswap_int32 (&rec_type);
    
    if (rec_type != 4)
      lose ((ME, _("%s: Variable index record (type 4) does not immediately "
                   "follow value label record (type 3) as it should."),
             handle_get_filename (r->fh)));
  }

  /* Read number of variables associated with value label from type 4
     record. */
  assertive_buf_read (r, &n_vars, sizeof n_vars, 0);
  if (r->reverse_endian)
    bswap_int32 (&n_vars);
  if (n_vars < 1 || n_vars > dict_get_var_cnt (dict))
    lose ((ME, _("%s: Number of variables associated with a value label (%d) "
                 "is not between 1 and the number of variables (%d)."),
	   handle_get_filename (r->fh), n_vars, dict_get_var_cnt (dict)));

  /* Read the list of variables. */
  var = xnmalloc (n_vars, sizeof *var);
  for (i = 0; i < n_vars; i++)
    {
      int32 var_idx;
      struct variable *v;

      /* Read variable index, check range. */
      assertive_buf_read (r, &var_idx, sizeof var_idx, 0);
      if (r->reverse_endian)
	bswap_int32 (&var_idx);
      if (var_idx < 1 || var_idx > r->value_cnt)
	lose ((ME, _("%s: Variable index associated with value label (%d) is "
                     "not between 1 and the number of values (%d)."),
	       handle_get_filename (r->fh), var_idx, r->value_cnt));

      /* Make sure it's a real variable. */
      v = var_by_idx[var_idx - 1];
      if (v == NULL)
	lose ((ME, _("%s: Variable index associated with value label (%d) "
                     "refers to a continuation of a string variable, not to "
                     "an actual variable."),
               handle_get_filename (r->fh), var_idx));
      if (v->type == ALPHA && v->width > MAX_SHORT_STRING)
	lose ((ME, _("%s: Value labels are not allowed on long string "
                     "variables (%s)."),
               handle_get_filename (r->fh), v->name));

      /* Add it to the list of variables. */
      var[i] = v;
    }

  /* Type check the variables. */
  for (i = 1; i < n_vars; i++)
    if (var[i]->type != var[0]->type)
      lose ((ME, _("%s: Variables associated with value label are not all of "
                   "identical type.  Variable %s has %s type, but variable "
                   "%s has %s type."),
             handle_get_filename (r->fh),
	     var[0]->name, var[0]->type == ALPHA ? _("string") : _("numeric"),
	     var[i]->name, var[i]->type == ALPHA ? _("string") : _("numeric")));

  /* Fill in labels[].value, now that we know the desired type. */
  for (i = 0; i < n_labels; i++) 
    {
      struct label *label = labels + i;
      
      if (var[0]->type == ALPHA)
        {
          const int copy_len = min (sizeof label->raw_value,
                                    sizeof label->label);
          memcpy (label->value.s, label->raw_value, copy_len);
        } else {
          flt64 f;
          assert (sizeof f == sizeof label->raw_value);
          memcpy (&f, label->raw_value, sizeof f);
          if (r->reverse_endian)
            bswap_flt64 (&f);
          label->value.f = f;
        }
    }
  
  /* Assign the value_label's to each variable. */
  for (i = 0; i < n_vars; i++)
    {
      struct variable *v = var[i];
      int j;

      /* Add each label to the variable. */
      for (j = 0; j < n_labels; j++)
	{
          struct label *label = labels + j;
	  if (!val_labs_replace (v->val_labs, label->value, label->label))
	    continue;

	  if (var[0]->type == NUMERIC)
	    msg (MW, _("%s: File contains duplicate label for value %g for "
                       "variable %s."),
                 handle_get_filename (r->fh), label->value.f, v->name);
	  else
	    msg (MW, _("%s: File contains duplicate label for value `%.*s' "
                       "for variable %s."),
                 handle_get_filename (r->fh), v->width, label->value.s, v->name);
	}
    }

  for (i = 0; i < n_labels; i++)
    free (labels[i].label);
  free (labels);
  free (var);
  return 1;

error:
  if (labels) 
    {
      for (i = 0; i < n_labels; i++)
        free (labels[i].label);
      free (labels); 
    }
  free (var);
  return 0;
}

/* Reads BYTE_CNT bytes from the file represented by H.  If BUF is
   non-NULL, uses that as the buffer; otherwise allocates at least
   MIN_ALLOC bytes.  Returns a pointer to the buffer on success, NULL
   on failure. */
static void *
buf_read (struct sfm_reader *r, void *buf, size_t byte_cnt, size_t min_alloc)
{
  assert (r);

  if (buf == NULL && byte_cnt > 0 )
    buf = xmalloc (max (byte_cnt, min_alloc));

  if ( byte_cnt == 0 )
    return buf;

  
  if (1 != fread (buf, byte_cnt, 1, r->file))
    {
      if (ferror (r->file))
	msg (ME, _("%s: Reading system file: %s."),
             handle_get_filename (r->fh), strerror (errno));
      else
	corrupt_msg (ME, _("%s: Unexpected end of file."),
                     handle_get_filename (r->fh));
      return NULL;
    }
  return buf;
}

/* Winds the reader BYTE_CNT bytes back in the reader stream.   */
void
buf_unread(struct sfm_reader *r, size_t byte_cnt)
{
  assert(byte_cnt > 0);

  if ( 0 != fseek(r->file, -byte_cnt, SEEK_CUR))
    {
      msg (ME, _("%s: Seeking system file: %s."),
	   handle_get_filename (r->fh), strerror (errno));
    }
}

/* Reads a document record, type 6, from system file R, and sets up
   the documents and n_documents fields in the associated
   dictionary. */
static int
read_documents (struct sfm_reader *r, struct dictionary *dict)
{
  int32 line_cnt;
  char *documents;

  if (dict_get_documents (dict) != NULL)
    lose ((ME, _("%s: System file contains multiple "
                 "type 6 (document) records."),
	   handle_get_filename (r->fh)));

  assertive_buf_read (r, &line_cnt, sizeof line_cnt, 0);
  if (line_cnt <= 0)
    lose ((ME, _("%s: Number of document lines (%ld) "
                 "must be greater than 0."),
	   handle_get_filename (r->fh), (long) line_cnt));

  documents = buf_read (r, NULL, 80 * line_cnt, line_cnt * 80 + 1);
  /* FIXME?  Run through asciify. */
  if (documents == NULL)
    return 0;
  documents[80 * line_cnt] = '\0';
  dict_set_documents (dict, documents);
  free (documents);
  return 1;

error:
  return 0;
}

/* Data reader. */

/* Reads compressed data into H->BUF and sets other pointers
   appropriately.  Returns nonzero only if both no errors occur and
   data was read. */
static int
buffer_input (struct sfm_reader *r)
{
  size_t amt;

  if (r->buf == NULL)
    r->buf = xnmalloc (128, sizeof *r->buf);
  amt = fread (r->buf, sizeof *r->buf, 128, r->file);
  if (ferror (r->file))
    {
      msg (ME, _("%s: Error reading file: %s."),
           handle_get_filename (r->fh), strerror (errno));
      return 0;
    }
  r->ptr = r->buf;
  r->end = &r->buf[amt];
  return amt;
}

/* Reads a single case consisting of compressed data from system
   file H into the array BUF[] according to reader R, and
   returns nonzero only if successful. */
/* Data in system files is compressed in this manner.  Data
   values are grouped into sets of eight ("octets").  Each value
   in an octet has one instruction byte that are output together.
   Each instruction byte gives a value for that byte or indicates
   that the value can be found following the instructions. */
static int
read_compressed_data (struct sfm_reader *r, flt64 *buf)
{
  const unsigned char *p_end = r->x + sizeof (flt64);
  unsigned char *p = r->y;

  const flt64 *buf_beg = buf;
  const flt64 *buf_end = &buf[r->value_cnt];

  for (;;)
    {
      for (; p < p_end; p++){
	switch (*p)
	  {
	  case 0:
	    /* Code 0 is ignored. */
	    continue;
	  case 252:
	    /* Code 252 is end of file. */
	    if (buf_beg != buf)
	      lose ((ME, _("%s: Compressed data is corrupted.  Data ends "
		     "in partial case."),
                     handle_get_filename (r->fh)));
	    goto error;
	  case 253:
	    /* Code 253 indicates that the value is stored explicitly
	       following the instruction bytes. */
	    if (r->ptr == NULL || r->ptr >= r->end)
	      if (!buffer_input (r))
		{
		  lose ((ME, _("%s: Unexpected end of file."),
                         handle_get_filename (r->fh)));
		  goto error;
		}
	    memcpy (buf++, r->ptr++, sizeof *buf);
	    if (buf >= buf_end)
	      goto success;
	    break;
	  case 254:
	    /* Code 254 indicates a string that is all blanks. */
	    memset (buf++, ' ', sizeof *buf);
	    if (buf >= buf_end)
	      goto success;
	    break;
	  case 255:
	    /* Code 255 indicates the system-missing value. */
	    *buf = r->sysmis;
	    if (r->reverse_endian)
	      bswap_flt64 (buf);
	    buf++;
	    if (buf >= buf_end)
	      goto success;
	    break;
	  default:
	    /* Codes 1 through 251 inclusive are taken to indicate a
	       value of (BYTE - BIAS), where BYTE is the byte's value
	       and BIAS is the compression bias (generally 100.0). */
	    *buf = *p - r->bias;
	    if (r->reverse_endian)
	      bswap_flt64 (buf);
	    buf++;
	    if (buf >= buf_end)
	      goto success;
	    break;
	  }
      }
      /* We have reached the end of this instruction octet.  Read
	 another. */
      if (r->ptr == NULL || r->ptr >= r->end)
	if (!buffer_input (r))
	  {
	    if (buf_beg != buf)
	      lose ((ME, _("%s: Unexpected end of file."),
                     handle_get_filename (r->fh)));
	    goto error;
	  }
      memcpy (r->x, r->ptr++, sizeof *buf);
      p = r->x;
    }

  /* Not reached. */
  assert (0);

success:
  /* We have filled up an entire record.  Update state and return
     successfully. */
  r->y = ++p;
  return 1;

error:
  /* We have been unsuccessful at filling a record, either through i/o
     error or through an end-of-file indication.  Update state and
     return unsuccessfully. */
  return 0;
}

/* Reads one case from READER's file into C.  Returns nonzero
   only if successful. */
int
sfm_read_case (struct sfm_reader *r, struct ccase *c)
{
  if (!r->compressed && sizeof (flt64) == sizeof (double)) 
    {
      /* Fast path: external and internal representations are the
         same, except possibly for endianness or SYSMIS.  Read
         directly into the case's buffer, then fix up any minor
         details as needed. */
      if (!fread_ok (r, case_data_all_rw (c),
                     sizeof (union value) * r->value_cnt))
        return 0;

      /* Fix up endianness if needed. */
      if (r->reverse_endian) 
        {
          int i;
          
          for (i = 0; i < r->value_cnt; i++) 
            if (r->vars[i].width == 0)
              bswap_flt64 (&case_data_rw (c, r->vars[i].fv)->f);
        }

      /* Fix up SYSMIS values if needed.
         I don't think this will ever actually kick in, but it
         can't hurt. */
      if (r->sysmis != SYSMIS) 
        {
          int i;
          
          for (i = 0; i < r->value_cnt; i++) 
            if (r->vars[i].width == 0 && case_num (c, i) == r->sysmis)
              case_data_rw (c, r->vars[i].fv)->f = SYSMIS;
        }
    }
  else 
    {
      /* Slow path: internal and external representations differ.
         Read into a bounce buffer, then copy to C. */
      flt64 *bounce;
      flt64 *bounce_cur;
      size_t bounce_size;
      int read_ok;
      int i;

      bounce_size = sizeof *bounce * r->value_cnt;
      bounce = bounce_cur = local_alloc (bounce_size);

      if (!r->compressed)
        read_ok = fread_ok (r, bounce, bounce_size);
      else
        read_ok = read_compressed_data (r, bounce);
      if (!read_ok) 
        {
          local_free (bounce);
          return 0;
        }

      for (i = 0; i < r->value_cnt; i++)
        {
          struct sfm_var *v = &r->vars[i];

          if (v->width == 0)
            {
              flt64 f = *bounce_cur++;
              if (r->reverse_endian)
                bswap_flt64 (&f);
              case_data_rw (c, v->fv)->f = f == r->sysmis ? SYSMIS : f;
            }
          else if (v->width != -1)
            {
              memcpy (case_data_rw (c, v->fv)->s, bounce_cur, v->width);
              bounce_cur += DIV_RND_UP (v->width, sizeof (flt64));
            }
        }

      local_free (bounce);
    }
  return 1; 
}

static int
fread_ok (struct sfm_reader *r, void *buffer, size_t byte_cnt)
{
  size_t read_bytes = fread (buffer, 1, byte_cnt, r->file);

  if (read_bytes == byte_cnt)
    return 1;
  else
    {
      if (ferror (r->file))
        msg (ME, _("%s: Reading system file: %s."),
             handle_get_filename (r->fh), strerror (errno));
      else if (read_bytes != 0)
        msg (ME, _("%s: Partial record at end of system file."),
             handle_get_filename (r->fh));
      return 0;
    }
}
