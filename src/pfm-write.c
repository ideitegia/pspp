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
#include "pfm-write.h"
#include "error.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "alloc.h"
#include "case.h"
#include "dictionary.h"
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

/* Portable file writer. */
struct pfm_writer
  {
    struct file_handle *fh;     /* File handle. */
    FILE *file;			/* File stream. */

    int lc;			/* Number of characters on this line so far. */

    size_t var_cnt;             /* Number of variables. */
    struct pfm_var *vars;       /* Variables. */
  };

/* A variable to write to the portable file. */
struct pfm_var 
  {
    int width;                  /* 0=numeric, otherwise string var width. */
    int fv;                     /* Starting case index. */
  };

static int buf_write (struct pfm_writer *, const void *, size_t);
static int write_header (struct pfm_writer *);
static int write_version_data (struct pfm_writer *);
static int write_variables (struct pfm_writer *, const struct dictionary *);
static int write_value_labels (struct pfm_writer *, const struct dictionary *);

/* Writes the dictionary DICT to portable file HANDLE.  Returns
   nonzero only if successful. */
struct pfm_writer *
pfm_open_writer (struct file_handle *fh, const struct dictionary *dict)
{
  struct pfm_writer *w = NULL;
  size_t i;

  if (!fh_open (fh, "portable file", "we"))
    goto error;
  
  /* Open the physical disk file. */
  w = xmalloc (sizeof *w);
  w->fh = fh;
  w->file = fopen (handle_get_filename (fh), "wb");
  w->lc = 0;
  w->var_cnt = 0;
  w->vars = NULL;
  
  /* Check that file create succeeded. */
  if (w->file == NULL)
    {
      msg (ME, _("An error occurred while opening \"%s\" for writing "
	   "as a portable file: %s."),
           handle_get_filename (fh), strerror (errno));
      err_cond_fail ();
      goto error;
    }
  
  w->var_cnt = dict_get_var_cnt (dict);
  w->vars = xmalloc (sizeof *w->vars * w->var_cnt);
  for (i = 0; i < w->var_cnt; i++) 
    {
      const struct variable *dv = dict_get_var (dict, i);
      struct pfm_var *pv = &w->vars[i];
      pv->width = dv->width;
      pv->fv = dv->fv;
    }

  /* Write file header. */
  if (!write_header (w)
      || !write_version_data (w)
      || !write_variables (w, dict)
      || !write_value_labels (w, dict)
      || !buf_write (w, "F", 1))
    goto error;

  return w;

error:
  pfm_close_writer (w);
  return NULL;
}
  
/* Write NBYTES starting at BUF to the portable file represented by
   H.  Break lines properly every 80 characters.  */
static int
buf_write (struct pfm_writer *w, const void *buf_, size_t nbytes)
{
  const char *buf = buf_;

  assert (buf != NULL);
  while (nbytes + w->lc >= 80)
    {
      size_t n = 80 - w->lc;
      
      if (n && fwrite (buf, n, 1, w->file) != 1)
	goto error;
      
      if (fwrite ("\r\n", 2, 1, w->file) != 1)
	goto error;

      nbytes -= n;
      buf += n;
      w->lc = 0;
    }

  if (nbytes && 1 != fwrite (buf, nbytes, 1, w->file))
    goto error;
  w->lc += nbytes;
  
  return 1;

 error:
  msg (ME, _("%s: Writing portable file: %s."),
       handle_get_filename (w->fh), strerror (errno));
  return 0;
}

/* Write D to the portable file as a floating-point field, and return
   success. */
static int
write_float (struct pfm_writer *w, double d)
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
    return buf_write (w, "*.", 2);
  
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
  
  success = buf_write (w, buf, cp - buf);
  local_free (buf);
  free (mantissa);
  return success;
}

/* Write N to the portable file as an integer field, and return success. */
static int
write_int (struct pfm_writer *w, int n)
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

  return buf_write (w, bp, &buf[64] - bp);
}

/* Write S to the portable file as a string field. */
static int
write_string (struct pfm_writer *w, const char *s)
{
  size_t n = strlen (s);
  return write_int (w, (int) n) && buf_write (w, s, n);
}

/* Write file header. */
static int
write_header (struct pfm_writer *w)
{
  /* PORTME. */
  {
    int i;

    for (i = 0; i < 5; i++)
      if (!buf_write (w, "ASCII SPSS PORT FILE                    ", 40))
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

    if (!buf_write (w, spss2ascii, 256))
      return 0;
  }

  if (!buf_write (w, "SPSSPORT", 8))
    return 0;

  return 1;
}

/* Writes version, date, and identification records. */
static int
write_version_data (struct pfm_writer *w)
{
  if (!buf_write (w, "A", 1))
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
    if (!write_string (w, date_str) || !write_string (w, time_str))
      return 0;
  }

  /* Product identification. */
  if (!buf_write (w, "1", 1) || !write_string (w, version))
    return 0;

  /* Subproduct identification. */
  if (!buf_write (w, "3", 1) || !write_string (w, host_system))
    return 0;

  return 1;
}

/* Write format F to file H, and return success. */
static int
write_format (struct pfm_writer *w, struct fmt_spec *f)
{
  return (write_int (w, formats[f->type].spss)
	  && write_int (w, f->w)
	  && write_int (w, f->d));
}

/* Write value V for variable VV to file H, and return success. */
static int
write_value (struct pfm_writer *w, union value *v, struct variable *vv)
{
  if (vv->type == NUMERIC)
    return write_float (w, v->f);
  else
    return write_int (w, vv->width) && buf_write (w, v->s, vv->width);
}

/* Write variable records, and return success. */
static int
write_variables (struct pfm_writer *w, const struct dictionary *dict)
{
  int i;
  
  if (!buf_write (w, "4", 1) || !write_int (w, dict_get_var_cnt (dict))
      || !write_int (w, 161))
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
      
      if (!buf_write (w, "7", 1) || !write_int (w, v->width)
	  || !write_string (w, v->name)
	  || !write_format (w, &v->print) || !write_format (w, &v->write))
	return 0;

      for (m = miss_types[v->miss_type], j = 0; j < (int) strlen (m); j++)
	if ((m[j] != ' ' && !buf_write (w, &m[j], 1))
	    || !write_value (w, &v->missing[j], v))
	  return 0;

      if (v->label && (!buf_write (w, "C", 1) || !write_string (w, v->label)))
	return 0;
    }

  return 1;
}

/* Write value labels to disk.  FIXME: Inefficient. */
static int
write_value_labels (struct pfm_writer *w, const struct dictionary *dict)
{
  int i;

  for (i = 0; i < dict_get_var_cnt (dict); i++)
    {
      struct val_labs_iterator *j;
      struct variable *v = dict_get_var (dict, i);
      struct val_lab *vl;

      if (!val_labs_count (v->val_labs))
	continue;

      if (!buf_write (w, "D", 1)
	  || !write_int (w, 1)
	  || !write_string (w, v->name)
	  || !write_int (w, val_labs_count (v->val_labs)))
	return 0;

      for (vl = val_labs_first_sorted (v->val_labs, &j); vl != NULL;
           vl = val_labs_next (v->val_labs, &j)) 
	if (!write_value (w, &vl->value, v)
	    || !write_string (w, vl->label)) 
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
pfm_write_case (struct pfm_writer *w, struct ccase *c)
{
  int i;
  
  for (i = 0; i < w->var_cnt; i++)
    {
      struct pfm_var *v = &w->vars[i];
      
      if (v->width == 0)
	{
	  if (!write_float (w, case_num (c, v->fv)))
	    return 0;
	}
      else
	{
	  if (!write_int (w, v->width)
              || !buf_write (w, case_str (c, v->fv), v->width))
	    return 0;
	}
    }

  return 1;
}

/* Closes a portable file after we're done with it. */
void
pfm_close_writer (struct pfm_writer *w)
{
  if (w == NULL)
    return;

  fh_close (w->fh, "portable file", "we");
  
  if (w->file != NULL)
    {
      char buf[80];
    
      int n = 80 - w->lc;
      if (n == 0)
        n = 80;

      memset (buf, 'Z', n);
      buf_write (w, buf, n);

      if (fclose (w->file) == EOF)
        msg (ME, _("%s: Closing portable file: %s."),
             handle_get_filename (w->fh), strerror (errno));
    }

  free (w->vars);
  free (w);
}
