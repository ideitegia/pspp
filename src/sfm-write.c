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
#include "sfm.h"
#include "sfmP.h"
#include "error.h"
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#if HAVE_UNISTD_H
#include <unistd.h>	/* Required by SunOS4. */
#endif
#include "alloc.h"
#include "error.h"
#include "file-handle.h"
#include "getline.h"
#include "hash.h"
#include "magic.h"
#include "misc.h"
#include "str.h"
#include "value-labels.h"
#include "var.h"
#include "version.h"

#include "debug-print.h"

/* PORTME: This file may require substantial revision for those
   systems that don't meet the typical 32-bit integer/64-bit double
   model.  It's kinda hard to tell without having one of them on my
   desk.  */

/* Compression bias used by PSPP.  Values between (1 -
   COMPRESSION_BIAS) and (251 - COMPRESSION_BIAS) inclusive can be
   compressed. */
#define COMPRESSION_BIAS 100

/* sfm writer file_handle extension. */
struct sfm_fhuser_ext
  {
    FILE *file;			/* Actual file. */

    int compressed;		/* 1=compressed, 0=not compressed. */
    flt64 *buf;			/* Buffered data. */
    flt64 *end;			/* Buffer end. */
    flt64 *ptr;			/* Current location in buffer. */
    unsigned char *x;		/* Location in current instruction octet. */
    unsigned char *y;		/* End of instruction octet. */
    int n_cases;		/* Number of cases written so far. */

    char *elem_type; 		/* ALPHA or NUMERIC for each flt64 element. */
  };

static struct fh_ext_class sfm_w_class;

static char *append_string_max (char *, const char *, const char *);
static int write_header (struct sfm_write_info *inf);
static int bufwrite (struct file_handle *h, const void *buf, size_t nbytes);
static int write_variable (struct sfm_write_info *inf, struct variable *v);
static int write_value_labels (struct sfm_write_info *inf, struct variable * s, int index);
static int write_rec_7_34 (struct sfm_write_info *inf);
static int write_documents (struct sfm_write_info *inf);

/* Writes the dictionary INF->dict to system file INF->h.  The system
   file is compressed if INF->compress is nonzero.  INF->case_size is
   set to the number of flt64 elements in a single case.  Returns
   nonzero only if successful. */
int
sfm_write_dictionary (struct sfm_write_info *inf)
{
  struct dictionary *d = inf->dict;
  struct sfm_fhuser_ext *ext;
  int i;
  int index;

  if (inf->h->class != NULL)
    {
      msg (ME, _("Cannot write file %s as system file: "
                 "already opened for %s."),
	   handle_get_name (inf->h), inf->h->class->name);
      return 0;
    }

  msg (VM (1), _("%s: Opening system-file handle %s for writing."),
       handle_get_filename (inf->h), handle_get_name (inf->h));
  
  /* Open the physical disk file. */
  inf->h->class = &sfm_w_class;
  inf->h->ext = ext = xmalloc (sizeof (struct sfm_fhuser_ext));
  ext->file = fopen (handle_get_filename (inf->h), "wb");
  ext->elem_type = NULL;
  if (ext->file == NULL)
    {
      msg (ME, _("An error occurred while opening \"%s\" for writing "
                 "as a system file: %s."),
           handle_get_filename (inf->h), strerror (errno));
      err_cond_fail ();
      free (ext);
      return 0;
    }

  /* Initialize the sfm_fhuser_ext structure. */
  ext->compressed = inf->compress;
  ext->buf = ext->ptr = NULL;
  ext->x = ext->y = NULL;
  ext->n_cases = 0;

  /* Write the file header. */
  if (!write_header (inf))
    goto lossage;

  /* Write basic variable info. */
  for (i = 0; i < dict_get_var_cnt (d); i++)
    write_variable (inf, dict_get_var (d, i));

  /* Write out value labels. */
  for (index = i = 0; i < dict_get_var_cnt (d); i++)
    {
      struct variable *v = dict_get_var (d, i);

      if (!write_value_labels (inf, v, index))
	goto lossage;
      index += (v->type == NUMERIC ? 1
		: DIV_RND_UP (v->width, sizeof (flt64)));
    }

  if (dict_get_documents (d) != NULL && !write_documents (inf))
    goto lossage;
  if (!write_rec_7_34 (inf))
    goto lossage;

  /* Write record 999. */
  {
    struct
      {
	int32 rec_type P;
	int32 filler P;
      }
    rec_999;

    rec_999.rec_type = 999;
    rec_999.filler = 0;

    if (!bufwrite (inf->h, &rec_999, sizeof rec_999))
      goto lossage;
  }

  msg (VM (2), _("Wrote system-file header successfully."));
  
  return 1;

lossage:
  msg (VM (1), _("Error writing system-file header."));
  fclose (ext->file);
  inf->h->class = NULL;
  inf->h->ext = NULL;
  free (ext->elem_type);
  ext->elem_type = NULL;
  return 0;
}

/* Returns value of X truncated to two least-significant digits. */
static int
rerange (int x)
{
  if (x < 0)
    x = -x;
  if (x >= 100)
    x %= 100;
  return x;
}

/* Write the sysfile_header header to the system file represented by
   INF. */
static int
write_header (struct sfm_write_info *inf)
{
  struct dictionary *d = inf->dict;
  struct sfm_fhuser_ext *ext = inf->h->ext;
  struct sysfile_header hdr;
  char *p;
  int i;

  time_t t;

  memcpy (hdr.rec_type, "$FL2", 4);

  p = stpcpy (hdr.prod_name, "@(#) SPSS DATA FILE ");
  p = append_string_max (p, version, &hdr.prod_name[60]);
  p = append_string_max (p, " - ", &hdr.prod_name[60]);
  p = append_string_max (p, host_system, &hdr.prod_name[60]);
  memset (p, ' ', &hdr.prod_name[60] - p);

  hdr.layout_code = 2;

  hdr.case_size = 0;
  for (i = 0; i < dict_get_var_cnt (d); i++)
    {
      struct variable *v = dict_get_var (d, i);
      hdr.case_size += (v->type == NUMERIC ? 1
			: DIV_RND_UP (v->width, sizeof (flt64)));
    }
  inf->case_size = hdr.case_size;

  p = ext->elem_type = xmalloc (inf->case_size);
  for (i = 0; i < dict_get_var_cnt (d); i++)
    {
      struct variable *v = dict_get_var (d, i);
      int count = (v->type == NUMERIC ? 1
                   : DIV_RND_UP (v->width, sizeof (flt64)));
      while (count--)
        *p++ = v->type;
    }

  hdr.compressed = inf->compress;

  if (dict_get_weight (d) != NULL)
    {
      struct variable *weight_var;
      int recalc_weight_index = 1;
      int i;

      weight_var = dict_get_weight (d);
      for (i = 0; ; i++) 
        {
	  struct variable *v = dict_get_var (d, i);
          if (v == weight_var)
            break;
	  recalc_weight_index += (v->type == NUMERIC ? 1
				  : DIV_RND_UP (v->width, sizeof (flt64)));
	}
      hdr.weight_index = recalc_weight_index;
    }
  else
    hdr.weight_index = 0;

  hdr.ncases = -1;
  hdr.bias = COMPRESSION_BIAS;

  if ((time_t) - 1 == time (&t))
    {
      memcpy (hdr.creation_date, "01 Jan 70", 9);
      memcpy (hdr.creation_time, "00:00:00", 8);
    }
  else
    {
      static const char *month_name[12] =
      {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
      };
      struct tm *tmp = localtime (&t);
      int day = rerange (tmp->tm_mday);
      int mon = rerange (tmp->tm_mon + 1);
      int year = rerange (tmp->tm_year);
      int hour = rerange (tmp->tm_hour + 1);
      int min = rerange (tmp->tm_min + 1);
      int sec = rerange (tmp->tm_sec + 1);
      char buf[10];

      sprintf (buf, "%02d %s %02d", day, month_name[mon - 1], year);
      memcpy (hdr.creation_date, buf, sizeof hdr.creation_date);
      sprintf (buf, "%02d:%02d:%02d", hour - 1, min - 1, sec - 1);
      memcpy (hdr.creation_time, buf, sizeof hdr.creation_time);
    }
  
  {
    const char *label = dict_get_label (d);
    if (label == NULL)
      label = "";

    st_bare_pad_copy (hdr.file_label, label, sizeof hdr.file_label); 
  }
  
  memset (hdr.padding, 0, sizeof hdr.padding);

  if (!bufwrite (inf->h, &hdr, sizeof hdr))
    return 0;
  return 1;
}

/* Translates format spec from internal form in SRC to system file
   format in DEST. */
static inline void
write_format_spec (struct fmt_spec *src, int32 *dest)
{
  *dest = (formats[src->type].spss << 16) | (src->w << 8) | src->d;
}

/* Write the variable record(s) for primary variable P and secondary
   variable S to the system file represented by INF. */
static int
write_variable (struct sfm_write_info *inf, struct variable *v)
{
  struct sysfile_variable sv;

  /* Missing values. */
  flt64 m[3];			/* Missing value values. */
  int nm;			/* Number of missing values, possibly negative. */

  sv.rec_type = 2;
  sv.type = (v->type == NUMERIC ? 0 : v->width);
  sv.has_var_label = (v->label != NULL);

  switch (v->miss_type)
    {
    case MISSING_NONE:
      nm = 0;
      break;
    case MISSING_1:
    case MISSING_2:
    case MISSING_3:
      for (nm = 0; nm < v->miss_type; nm++)
	m[nm] = v->missing[nm].f;
      break;
    case MISSING_RANGE:
      m[0] = v->missing[0].f;
      m[1] = v->missing[1].f;
      nm = -2;
      break;
    case MISSING_LOW:
      m[0] = second_lowest_flt64;
      m[1] = v->missing[0].f;
      nm = -2;
      break;
    case MISSING_HIGH:
      m[0] = v->missing[0].f;
      m[1] = FLT64_MAX;
      nm = -2;
      break;
    case MISSING_RANGE_1:
      m[0] = v->missing[0].f;
      m[1] = v->missing[1].f;
      m[2] = v->missing[2].f;
      nm = -3;
      break;
    case MISSING_LOW_1:
      m[0] = second_lowest_flt64;
      m[1] = v->missing[0].f;
      m[2] = v->missing[1].f;
      nm = -3;
      break;
    case MISSING_HIGH_1:
      m[0] = v->missing[0].f;
      m[1] = second_lowest_flt64;
      m[2] = v->missing[1].f;
      nm = -3;
      break;
    default:
      assert (0);
      abort ();
    }

  sv.n_missing_values = nm;
  write_format_spec (&v->print, &sv.print);
  write_format_spec (&v->write, &sv.write);
  memcpy (sv.name, v->name, strlen (v->name));
  memset (&sv.name[strlen (v->name)], ' ', 8 - strlen (v->name));
  if (!bufwrite (inf->h, &sv, sizeof sv))
    return 0;

  if (v->label)
    {
      struct label
	{
	  int32 label_len P;
	  char label[255] P;
	}
      l;

      int ext_len;

      l.label_len = min (strlen (v->label), 255);
      ext_len = ROUND_UP (l.label_len, sizeof l.label_len);
      memcpy (l.label, v->label, l.label_len);
      memset (&l.label[l.label_len], ' ', ext_len - l.label_len);

      if (!bufwrite (inf->h, &l, offsetof (struct label, label) + ext_len))
	  return 0;
    }

  if (nm && !bufwrite (inf->h, m, sizeof *m * nm))
    return 0;

  if (v->type == ALPHA && v->width > (int) sizeof (flt64))
    {
      int i;
      int pad_count;

      sv.type = -1;
      sv.has_var_label = 0;
      sv.n_missing_values = 0;
      memset (&sv.print, 0, sizeof sv.print);
      memset (&sv.write, 0, sizeof sv.write);
      memset (&sv.name, 0, sizeof sv.name);

      pad_count = DIV_RND_UP (v->width, (int) sizeof (flt64)) - 1;
      for (i = 0; i < pad_count; i++)
	if (!bufwrite (inf->h, &sv, sizeof sv))
	  return 0;
    }

  return 1;
}

/* Writes the value labels for variable V having system file variable
   index INDEX to the system file associated with INF.  Returns
   nonzero only if successful. */
static int
write_value_labels (struct sfm_write_info * inf, struct variable *v, int index)
{
  struct value_label_rec
    {
      int32 rec_type P;
      int32 n_labels P;
      flt64 labels[1] P;
    };

  struct variable_index_rec
    {
      int32 rec_type P;
      int32 n_vars P;
      int32 vars[1] P;
    };

  struct val_labs_iterator *i;
  struct value_label_rec *vlr;
  struct variable_index_rec vir;
  struct val_lab *vl;
  size_t vlr_size;
  flt64 *loc;

  if (!val_labs_count (v->val_labs))
    return 1;

  /* Pass 1: Count bytes. */
  vlr_size = (sizeof (struct value_label_rec)
	      + sizeof (flt64) * (val_labs_count (v->val_labs) - 1));
  for (vl = val_labs_first (v->val_labs, &i); vl != NULL;
       vl = val_labs_next (v->val_labs, &i))
    vlr_size += ROUND_UP (strlen (vl->label) + 1, sizeof (flt64));

  /* Pass 2: Copy bytes. */
  vlr = xmalloc (vlr_size);
  vlr->rec_type = 3;
  vlr->n_labels = val_labs_count (v->val_labs);
  loc = vlr->labels;
  for (vl = val_labs_first_sorted (v->val_labs, &i); vl != NULL;
       vl = val_labs_next (v->val_labs, &i))
    {
      size_t len = strlen (vl->label);

      *loc++ = vl->value.f;
      *(unsigned char *) loc = len;
      memcpy (&((unsigned char *) loc)[1], vl->label, len);
      memset (&((unsigned char *) loc)[1 + len], ' ',
	      REM_RND_UP (len + 1, sizeof (flt64)));
      loc += DIV_RND_UP (len + 1, sizeof (flt64));
    }
  
  if (!bufwrite (inf->h, vlr, vlr_size))
    {
      free (vlr);
      return 0;
    }
  free (vlr);

  vir.rec_type = 4;
  vir.n_vars = 1;
  vir.vars[0] = index + 1;
  if (!bufwrite (inf->h, &vir, sizeof vir))
    return 0;

  return 1;
}

/* Writes record type 6, document record. */
static int
write_documents (struct sfm_write_info * inf)
{
  struct dictionary *d = inf->dict;
  struct
  {
    int32 rec_type P;		/* Always 6. */
    int32 n_lines P;		/* Number of lines of documents. */
  }
  rec_6;

  const char *documents;
  size_t n_lines;

  documents = dict_get_documents (d);
  n_lines = strlen (documents) / 80;

  rec_6.rec_type = 6;
  rec_6.n_lines = n_lines;
  if (!bufwrite (inf->h, &rec_6, sizeof rec_6))
    return 0;
  if (!bufwrite (inf->h, documents, 80 * n_lines))
    return 0;

  return 1;
}

/* Writes record type 7, subtypes 3 and 4. */
static int
write_rec_7_34 (struct sfm_write_info * inf)
{
  struct
    {
      int32 rec_type_3 P;
      int32 subtype_3 P;
      int32 data_type_3 P;
      int32 n_elem_3 P;
      int32 elem_3[8] P;
      int32 rec_type_4 P;
      int32 subtype_4 P;
      int32 data_type_4 P;
      int32 n_elem_4 P;
      flt64 elem_4[3] P;
    }
  rec_7;

  /* Components of the version number, from major to minor. */
  int version_component[3];
  
  /* Used to step through the version string. */
  char *p;

  /* Parses the version string, which is assumed to be of the form
     #.#x, where each # is a string of digits, and x is a single
     letter. */
  version_component[0] = strtol (bare_version, &p, 10);
  if (*p == '.')
    p++;
  version_component[1] = strtol (bare_version, &p, 10);
  version_component[2] = (isalpha ((unsigned char) *p)
			  ? tolower ((unsigned char) *p) - 'a' : 0);
    
  rec_7.rec_type_3 = 7;
  rec_7.subtype_3 = 3;
  rec_7.data_type_3 = sizeof (int32);
  rec_7.n_elem_3 = 8;
  rec_7.elem_3[0] = version_component[0];
  rec_7.elem_3[1] = version_component[1];
  rec_7.elem_3[2] = version_component[2];
  rec_7.elem_3[3] = -1;

  /* PORTME: 1=IEEE754, 2=IBM 370, 3=DEC VAX E. */
#ifdef FPREP_IEEE754
  rec_7.elem_3[4] = 1;
#endif

  rec_7.elem_3[5] = 1;

  /* PORTME: 1=big-endian, 2=little-endian. */
#if WORDS_BIGENDIAN
  rec_7.elem_3[6] = 1;
#else
  rec_7.elem_3[6] = 2;
#endif

  /* PORTME: 1=EBCDIC, 2=7-bit ASCII, 3=8-bit ASCII, 4=DEC Kanji. */
  rec_7.elem_3[7] = 2;

  rec_7.rec_type_4 = 7;
  rec_7.subtype_4 = 4;
  rec_7.data_type_4 = sizeof (flt64);
  rec_7.n_elem_4 = 3;
  rec_7.elem_4[0] = -FLT64_MAX;
  rec_7.elem_4[1] = FLT64_MAX;
  rec_7.elem_4[2] = second_lowest_flt64;

  if (!bufwrite (inf->h, &rec_7, sizeof rec_7))
    return 0;
  return 1;
}

/* Write NBYTES starting at BUF to the system file represented by
   H. */
static int
bufwrite (struct file_handle * h, const void *buf, size_t nbytes)
{
  struct sfm_fhuser_ext *ext = h->ext;

  assert (buf);
  if (1 != fwrite (buf, nbytes, 1, ext->file))
    {
      msg (ME, _("%s: Writing system file: %s."),
           handle_get_filename (h), strerror (errno));
      return 0;
    }
  return 1;
}

/* Copies string DEST to SRC with the proviso that DEST does not reach
   byte END; no null terminator is copied.  Returns a pointer to the
   byte after the last byte copied. */
static char *
append_string_max (char *dest, const char *src, const char *end)
{
  int nbytes = min (end - dest, (int) strlen (src));
  memcpy (dest, src, nbytes);
  return dest + nbytes;
}

/* Makes certain that the compression buffer of H has room for another
   element.  If there's not room, pads out the current instruction
   octet with zero and dumps out the buffer. */
static inline int
ensure_buf_space (struct file_handle *h)
{
  struct sfm_fhuser_ext *ext = h->ext;

  if (ext->ptr >= ext->end)
    {
      memset (ext->x, 0, ext->y - ext->x);
      ext->x = ext->y;
      ext->ptr = ext->buf;
      if (!bufwrite (h, ext->buf, sizeof *ext->buf * 128))
	return 0;
    }
  return 1;
}

/* Writes case ELEM consisting of N_ELEM flt64 elements to the system
   file represented by H.  Return success. */
int
sfm_write_case (struct file_handle * h, const flt64 *elem, int n_elem)
{
  struct sfm_fhuser_ext *ext = h->ext;
  const flt64 *end_elem = &elem[n_elem];
  char *elem_type = ext->elem_type;

  ext->n_cases++;

  if (ext->compressed == 0)
    return bufwrite (h, elem, sizeof *elem * n_elem);

  if (ext->buf == NULL)
    {
      ext->buf = xmalloc (sizeof *ext->buf * 128);
      ext->ptr = ext->buf;
      ext->end = &ext->buf[128];
      ext->x = (unsigned char *) (ext->ptr++);
      ext->y = (unsigned char *) (ext->ptr);
    }
  for (; elem < end_elem; elem++, elem_type++)
    {
      if (ext->x >= ext->y)
	{
	  if (!ensure_buf_space (h))
	    return 0;
	  ext->x = (unsigned char *) (ext->ptr++);
	  ext->y = (unsigned char *) (ext->ptr);
	}

      if (*elem_type == NUMERIC)
        {
	  if (*elem == -FLT64_MAX)
            {
	      *ext->x++ = 255;
              continue;
            }
	  else if (*elem > INT_MIN && *elem < INT_MAX)
	    {
	      int value = *elem;

	      if (value >= 1 - COMPRESSION_BIAS
		  && value <= 251 - COMPRESSION_BIAS
		  && value == *elem)
                {
		  *ext->x++ = value + COMPRESSION_BIAS;
                  continue;
                }
            }
        }
      else
	{
          if (0 == memcmp ((char *) elem,
	                   "                                           ",
		           sizeof (flt64)))
            {
	      *ext->x++ = 254;
              continue;
            }
        }
      
      *ext->x++ = 253;
      if (!ensure_buf_space (h))
	return 0;
      *ext->ptr++ = *elem;
    }

  return 1;
}

/* Closes a system file after we're done with it. */
static void
sfm_close (struct file_handle * h)
{
  struct sfm_fhuser_ext *ext = h->ext;

  if (ext->buf != NULL && ext->ptr > ext->buf)
    {
      memset (ext->x, 0, ext->y - ext->x);
      bufwrite (h, ext->buf, (ext->ptr - ext->buf) * sizeof *ext->buf);
    }

  /* Attempt to seek back to the beginning in order to write the
     number of cases.  If that's not possible (i.e., we're writing to
     a tty or a pipe), then it's not a big deal because we wrote the
     code that indicates an unknown number of cases. */
  if (0 == fseek (ext->file, offsetof (struct sysfile_header, ncases),
		  SEEK_SET))
    {
      int32 n_cases = ext->n_cases;

      /* I don't really care about the return value: it doesn't matter
         whether this data is written.  This is the only situation in
         which you will see me fail to check a return value. */
      fwrite (&n_cases, sizeof n_cases, 1, ext->file);
    }

  if (EOF == fclose (ext->file))
    msg (ME, _("%s: Closing system file: %s."),
         handle_get_filename (h), strerror (errno));
  free (ext->buf);

  free (ext->elem_type);
  free (ext);
}

static struct fh_ext_class sfm_w_class =
{
  4,
  N_("writing as a system file"),
  sfm_close,
};
