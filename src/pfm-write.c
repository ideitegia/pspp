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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include "pfm.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "alloc.h"
#include "error.h"
#include "file-handle.h"
#include "gmp.h"
#include "hash.h"
#include "magic.h"
#include "str.h"
#include "value-labels.h"
#include "var.h"
#include "version.h"

#include "debug-print.h"

/* pfm writer file_handle extension. */
struct pfm_fhuser_ext
  {
    FILE *file;			/* Actual file. */

    int lc;			/* Number of characters on this line so far. */

    int nvars;			/* Number of variables. */
    int *vars;			/* Variable widths. */
  };

static struct fh_ext_class pfm_w_class;

static int bufwrite (struct file_handle *h, const void *buf, size_t nbytes);
static int write_header (struct file_handle *h);
static int write_version_data (struct file_handle *h);
static int write_variables (struct file_handle *h, struct dictionary *d);
static int write_value_labels (struct file_handle *h, struct dictionary *d);

/* Writes the dictionary DICT to portable file HANDLE.  Returns
   nonzero only if successful. */
int
pfm_write_dictionary (struct file_handle *handle, struct dictionary *dict)
{
  struct pfm_fhuser_ext *ext;
  
  if (handle->class != NULL)
    {
      msg (ME, _("Cannot write file %s as portable file: already opened "
		 "for %s."),
	   handle_get_name (handle), handle->class->name);
      return 0;
    }

  msg (VM (1), _("%s: Opening portable-file handle %s for writing."),
       handle_get_filename (handle), handle_get_name (handle));
  
  /* Open the physical disk file. */
  handle->class = &pfm_w_class;
  handle->ext = ext = xmalloc (sizeof (struct pfm_fhuser_ext));
  ext->file = fopen (handle_get_filename (handle), "wb");
  ext->lc = 0;
  if (ext->file == NULL)
    {
      msg (ME, _("An error occurred while opening \"%s\" for writing "
	   "as a portable file: %s."),
           handle_get_filename (handle), strerror (errno));
      err_cond_fail ();
      free (ext);
      return 0;
    }
  
  {
    int i;

    ext->nvars = dict_get_var_cnt (dict);
    ext->vars = xmalloc (sizeof *ext->vars * ext->nvars);
    for (i = 0; i < ext->nvars; i++)
      ext->vars[i] = dict_get_var (dict, i)->width;
  }

  /* Write the file header. */
  if (!write_header (handle))
    goto lossage;

  /* Write version data. */
  if (!write_version_data (handle))
    goto lossage;

  /* Write variables. */
  if (!write_variables (handle, dict))
    goto lossage;

  /* Write value labels. */
  if (!write_value_labels (handle, dict))
    goto lossage;

  /* Write beginning of data marker. */
  if (!bufwrite (handle, "F", 1))
    goto lossage;

  msg (VM (2), _("Wrote portable-file header successfully."));

  return 1;

lossage:
  msg (VM (1), _("Error writing portable-file header."));
  fclose (ext->file);
  free (ext->vars);
  handle->class = NULL;
  handle->ext = NULL;
  return 0;
}
  
/* Write NBYTES starting at BUF to the portable file represented by
   H.  Break lines properly every 80 characters.  */
static int
bufwrite (struct file_handle *h, const void *buf_, size_t nbytes)
{
  const char *buf = buf_;
  struct pfm_fhuser_ext *ext = h->ext;

  assert (buf != NULL);
  while (nbytes + ext->lc >= 80)
    {
      size_t n = 80 - ext->lc;
      
      if (n && 1 != fwrite (buf, n, 1, ext->file))
	goto lossage;
      
      /* PORTME: line ends. */
      if (1 != fwrite ("\r\n", 2, 1, ext->file))
	goto lossage;

      nbytes -= n;
      buf += n;
      ext->lc = 0;
    }

  if (nbytes && 1 != fwrite (buf, nbytes, 1, ext->file))
    goto lossage;
  ext->lc += nbytes;
  
  return 1;

 lossage:
  abort ();
  msg (ME, _("%s: Writing portable file: %s."),
       handle_get_filename (h), strerror (errno));
  return 0;
}

/* Write D to the portable file as a floating-point field, and return
   success. */
static int
write_float (struct file_handle *h, double d)
{
  int neg = 0;
  char *mantissa;
  int mantissa_len;
  mp_exp_t exponent;
  char *buf, *cp;
  int success;

  if (d < 0.)
    {
      d = -d;
      neg = 1;
    }
  
  if (d == fabs (SYSMIS) || d == HUGE_VAL)
    return bufwrite (h, "*.", 2);
  
  /* Use GNU libgmp2 to convert D into base-30. */
  {
    mpf_t f;
    
    mpf_init_set_d (f, d);
    mantissa = mpf_get_str (NULL, &exponent, 30, 0, f);
    mpf_clear (f);

    for (cp = mantissa; *cp; cp++)
      *cp = toupper (*cp);
  }
  
  /* Choose standard or scientific notation. */
  mantissa_len = (int) strlen (mantissa);
  cp = buf = local_alloc (mantissa_len + 32);
  if (neg)
    *cp++ = '-';
  if (mantissa_len == 0)
    *cp++ = '0';
  else if (exponent < -4 || exponent > (mp_exp_t) mantissa_len)
    {
      /* Scientific notation. */
      *cp++ = mantissa[0];
      *cp++ = '.';
      cp = stpcpy (cp, &mantissa[1]);
      cp = spprintf (cp, "%+ld", (long) (exponent - 1));
    }
  else if (exponent <= 0)
    {
      /* Standard notation, D <= 1. */
      *cp++ = '.';
      memset (cp, '0', -exponent);
      cp += -exponent;
      cp = stpcpy (cp, mantissa);
    }
  else 
    {
      /* Standard notation, D > 1. */
      memcpy (cp, mantissa, exponent);
      cp += exponent;
      *cp++ = '.';
      cp = stpcpy (cp, &mantissa[exponent]);
    }
  *cp++ = '/';
  
  success = bufwrite (h, buf, cp - buf);
  local_free (buf);
  free (mantissa);
  return success;
}

/* Write N to the portable file as an integer field, and return success. */
static int
write_int (struct file_handle *h, int n)
{
  char buf[64];
  char *bp = &buf[64];
  int neg = 0;

  *--bp = '/';
  
  if (n < 0)
    {
      n = -n;
      neg = 1;
    }
  
  do
    {
      int r = n % 30;

      /* PORTME: character codes. */
      if (r < 10)
	*--bp = r + '0';
      else
	*--bp = r - 10 + 'A';

      n /= 30;
    }
  while (n > 0);

  if (neg)
    *--bp = '-';

  return bufwrite (h, bp, &buf[64] - bp);
}

/* Write S to the portable file as a string field. */
static int
write_string (struct file_handle *h, const char *s)
{
  size_t n = strlen (s);
  return write_int (h, (int) n) && bufwrite (h, s, n);
}

/* Write file header. */
static int
write_header (struct file_handle *h)
{
  /* PORTME. */
  {
    int i;

    for (i = 0; i < 5; i++)
      if (!bufwrite (h, "ASCII SPSS PORT FILE                    ", 40))
	return 0;
  }
  
  {
    /* PORTME: Translation table from SPSS character code to this
       computer's native character code (which is probably ASCII). */
    static const unsigned char spss2ascii[256] =
      {
	"0000000000000000000000000000000000000000000000000000000000000000"
	"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ."
	"<(+|&[]!$*);^-/|,%_>?`:$@'=\"000000~-0000123456789000-()0{}\\00000"
	"0000000000000000000000000000000000000000000000000000000000000000"
      };

    if (!bufwrite (h, spss2ascii, 256))
      return 0;
  }

  if (!bufwrite (h, "SPSSPORT", 8))
    return 0;

  return 1;
}

/* Writes version, date, and identification records. */
static int
write_version_data (struct file_handle *h)
{
  if (!bufwrite (h, "A", 1))
    return 0;
  
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
    if (!write_string (h, date_str) || !write_string (h, time_str))
      return 0;
  }

  /* Product identification. */
  if (!bufwrite (h, "1", 1) || !write_string (h, version))
    return 0;

  /* Subproduct identification. */
  if (!bufwrite (h, "3", 1) || !write_string (h, host_system))
    return 0;

  return 1;
}

/* Write format F to file H, and return success. */
static int
write_format (struct file_handle *h, struct fmt_spec *f)
{
  return (write_int (h, formats[f->type].spss)
	  && write_int (h, f->w)
	  && write_int (h, f->d));
}

/* Write value V for variable VV to file H, and return success. */
static int
write_value (struct file_handle *h, union value *v, struct variable *vv)
{
  if (vv->type == NUMERIC)
    return write_float (h, v->f);
  else
    return write_int (h, vv->width) && bufwrite (h, v->s, vv->width);
}

/* Write variable records, and return success. */
static int
write_variables (struct file_handle *h, struct dictionary *dict)
{
  int i;
  
  if (!bufwrite (h, "4", 1) || !write_int (h, dict_get_var_cnt (dict))
      || !write_int (h, 161))
    return 0;

  for (i = 0; i < dict_get_var_cnt (dict); i++)
    {
      static const char *miss_types[MISSING_COUNT] =
	{
	  "", "8", "88", "888", "B ", "9", "A", "B 8", "98", "A8",
	};

      const char *m;
      int j;

      struct variable *v = dict_get_var (dict, i);
      
      if (!bufwrite (h, "7", 1) || !write_int (h, v->width)
	  || !write_string (h, v->name)
	  || !write_format (h, &v->print) || !write_format (h, &v->write))
	return 0;

      for (m = miss_types[v->miss_type], j = 0; j < (int) strlen (m); j++)
	if ((m[j] != ' ' && !bufwrite (h, &m[j], 1))
	    || !write_value (h, &v->missing[j], v))
	  return 0;

      if (v->label && (!bufwrite (h, "C", 1) || !write_string (h, v->label)))
	return 0;
    }

  return 1;
}

/* Write value labels to disk.  FIXME: Inefficient. */
static int
write_value_labels (struct file_handle *h, struct dictionary *dict)
{
  int i;

  for (i = 0; i < dict_get_var_cnt (dict); i++)
    {
      struct val_labs_iterator *j;
      struct variable *v = dict_get_var (dict, i);
      struct val_lab *vl;

      if (!val_labs_count (v->val_labs))
	continue;

      if (!bufwrite (h, "D", 1)
	  || !write_int (h, 1)
	  || !write_string (h, v->name)
	  || !write_int (h, val_labs_count (v->val_labs)))
	return 0;

      for (vl = val_labs_first_sorted (v->val_labs, &j); vl != NULL;
           vl = val_labs_next (v->val_labs, &j)) 
	if (!write_value (h, &vl->value, v)
	    || !write_string (h, vl->label)) 
          {
            val_labs_done (&j);
            return 0; 
          }
    }

  return 1;
}

/* Writes case ELEM to the portable file represented by H.  Returns
   success. */
int 
pfm_write_case (struct file_handle *h, const union value *elem)
{
  struct pfm_fhuser_ext *ext = h->ext;
  
  int i;
  
  for (i = 0; i < ext->nvars; i++)
    {
      const int width = ext->vars[i];
      
      if (width == 0)
	{
	  if (!write_float (h, elem[i].f))
	    return 0;
	}
      else
	{
	  if (!write_int (h, width) || !bufwrite (h, elem[i].c, width))
	    return 0;
	}
    }

  return 1;
}

/* Closes a portable file after we're done with it. */
static void
pfm_close (struct file_handle *h)
{
  struct pfm_fhuser_ext *ext = h->ext;
  
  {
    char buf[80];
    
    int n = 80 - ext->lc;
    if (n == 0)
      n = 80;

    memset (buf, 'Z', n);
    bufwrite (h, buf, n);
  }

  if (EOF == fclose (ext->file))
    msg (ME, _("%s: Closing portable file: %s."),
         handle_get_filename (h), strerror (errno));

  free (ext->vars);
  free (ext);
}

static struct fh_ext_class pfm_w_class =
{
  6,
  N_("writing as a portable file"),
  pfm_close,
};
