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
#include "sfm-write.h"
#include "sfmP.h"
#include "error.h"
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#if HAVE_UNISTD_H
#include <unistd.h>	/* Required by SunOS4. */
#endif
#include "alloc.h"
#include "case.h"
#include "dictionary.h"
#include "error.h"
#include "file-handle.h"
#include "getl.h"
#include "hash.h"
#include "magic.h"
#include "misc.h"
#include "settings.h"
#include "stat-macros.h"
#include "str.h"
#include "value-labels.h"
#include "var.h"
#include "version.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include "debug-print.h"

/* Compression bias used by PSPP.  Values between (1 -
   COMPRESSION_BIAS) and (251 - COMPRESSION_BIAS) inclusive can be
   compressed. */
#define COMPRESSION_BIAS 100

/* System file writer. */
struct sfm_writer
  {
    struct file_handle *fh;     /* File handle. */
    FILE *file;			/* File stream. */

    int needs_translation;      /* 0=use fast path, 1=translation needed. */
    int compress;		/* 1=compressed, 0=not compressed. */
    int case_cnt;		/* Number of cases written so far. */
    size_t flt64_cnt;           /* Number of flt64 elements in case. */

    /* Compression buffering. */
    flt64 *buf;			/* Buffered data. */
    flt64 *end;			/* Buffer end. */
    flt64 *ptr;			/* Current location in buffer. */
    unsigned char *x;		/* Location in current instruction octet. */
    unsigned char *y;		/* End of instruction octet. */

    /* Variables. */
    struct sfm_var *vars;       /* Variables. */
    size_t var_cnt;             /* Number of variables. */
  };

/* A variable in a system file. */
struct sfm_var 
  {
    int width;                  /* 0=numeric, otherwise string width. */
    int fv;                     /* Index into case. */
    size_t flt64_cnt;           /* Number of flt64 elements. */
  };

static char *append_string_max (char *, const char *, const char *);
static int write_header (struct sfm_writer *, const struct dictionary *);
static int buf_write (struct sfm_writer *, const void *, size_t);
static int write_variable (struct sfm_writer *, struct variable *);
static int write_value_labels (struct sfm_writer *,
                               struct variable *, int idx);
static int write_rec_7_34 (struct sfm_writer *);

static int write_longvar_table (struct sfm_writer *w, 
				const struct dictionary *dict);

static int write_variable_display_parameters (struct sfm_writer *w, 
					      const struct dictionary *dict);


static int write_documents (struct sfm_writer *, const struct dictionary *);
static int does_dict_need_translation (const struct dictionary *);

static inline int
var_flt64_cnt (const struct variable *v) 
{
  return v->type == NUMERIC ? 1 : DIV_RND_UP (v->width, sizeof (flt64));
}

/* Returns default options for writing a system file. */
struct sfm_write_options
sfm_writer_default_options (void) 
{
  struct sfm_write_options opts;
  opts.create_writeable = true;
  opts.compress = get_scompression ();
  opts.version = 3;
  return opts;
}

/* Opens the system file designated by file handle FH for writing
   cases from dictionary D according to the given OPTS.  If
   COMPRESS is nonzero, the system file will be compressed.

   No reference to D is retained, so it may be modified or
   destroyed at will after this function returns.  D is not
   modified by this function, except to assign short names. */
struct sfm_writer *
sfm_open_writer (struct file_handle *fh, struct dictionary *d,
                 struct sfm_write_options opts)
{
  struct sfm_writer *w = NULL;
  mode_t mode;
  int fd;
  int idx;
  int i;

  /* Check version. */
  if (opts.version != 2 && opts.version != 3) 
    {
      msg (ME, _("Unknown system file version %d. Treating as version %d."),
           opts.version, 3);
      opts.version = 3;
    }

  /* Create file. */
  mode = S_IRUSR | S_IRGRP | S_IROTH;
  if (opts.create_writeable)
    mode |= S_IWUSR | S_IWGRP | S_IWOTH;
  fd = open (handle_get_filename (fh), O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd < 0) 
    goto open_error;

  /* Open file handle. */
  if (!fh_open (fh, "system file", "we"))
    goto error;

  /* Create and initialize writer. */
  w = xmalloc (sizeof *w);
  w->fh = fh;
  w->file = fdopen (fd, "w");

  w->needs_translation = does_dict_need_translation (d);
  w->compress = opts.compress;
  w->case_cnt = 0;
  w->flt64_cnt = 0;

  w->buf = w->end = w->ptr = NULL;
  w->x = w->y = NULL;

  w->var_cnt = dict_get_var_cnt (d);
  w->vars = xnmalloc (w->var_cnt, sizeof *w->vars);
  for (i = 0; i < w->var_cnt; i++) 
    {
      const struct variable *dv = dict_get_var (d, i);
      struct sfm_var *sv = &w->vars[i];
      sv->width = dv->width;
      sv->fv = dv->fv;
      sv->flt64_cnt = var_flt64_cnt (dv);
    }

  /* Check that file create succeeded. */
  if (w->file == NULL) 
    {
      close (fd);
      goto open_error;
    }

  /* Write the file header. */
  if (!write_header (w, d))
    goto error;

  /* Write basic variable info. */
  dict_assign_short_names (d);
  for (i = 0; i < dict_get_var_cnt (d); i++)
    write_variable (w, dict_get_var (d, i));

  /* Write out value labels. */
  for (idx = i = 0; i < dict_get_var_cnt (d); i++)
    {
      struct variable *v = dict_get_var (d, i);

      if (!write_value_labels (w, v, idx))
	goto error;
      idx += var_flt64_cnt (v);
    }

  if (dict_get_documents (d) != NULL && !write_documents (w, d))
    goto error;

  if (!write_rec_7_34 (w))
    goto error;

  if (!write_variable_display_parameters (w, d))
    goto error;

  if (opts.version >= 3) 
    {
      if (!write_longvar_table (w, d))
	goto error;
    }

  /* Write end-of-headers record. */
  {
    struct
      {
	int32 rec_type P;
	int32 filler P;
      }
    rec_999;

    rec_999.rec_type = 999;
    rec_999.filler = 0;

    if (!buf_write (w, &rec_999, sizeof rec_999))
      goto error;
  }

  if (w->compress) 
    {
      w->buf = xnmalloc (128, sizeof *w->buf);
      w->ptr = w->buf;
      w->end = &w->buf[128];
      w->x = (unsigned char *) w->ptr++;
      w->y = (unsigned char *) w->ptr;
    }
  
  return w;

 error:
  sfm_close_writer (w);
  return NULL;

 open_error:
  msg (ME, _("Error opening \"%s\" for writing as a system file: %s."),
       handle_get_filename (w->fh), strerror (errno));
  err_cond_fail ();
  goto error;
}

static int
does_dict_need_translation (const struct dictionary *d)
{
  size_t case_idx;
  size_t i;

  case_idx = 0;
  for (i = 0; i < dict_get_var_cnt (d); i++) 
    {
      struct variable *v = dict_get_var (d, i);
      if (v->fv != case_idx)
        return 0;
      case_idx += v->nv;
    }
  return 1;
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

/* Write the sysfile_header header to system file W. */
static int
write_header (struct sfm_writer *w, const struct dictionary *d)
{
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

  w->flt64_cnt = 0;
  for (i = 0; i < dict_get_var_cnt (d); i++)
    w->flt64_cnt += var_flt64_cnt (dict_get_var (d, i));
  hdr.case_size = w->flt64_cnt;

  hdr.compress = w->compress;

  if (dict_get_weight (d) != NULL)
    {
      struct variable *weight_var;
      int recalc_weight_idx = 1;
      int i;

      weight_var = dict_get_weight (d);
      for (i = 0; ; i++) 
        {
	  struct variable *v = dict_get_var (d, i);
          if (v == weight_var)
            break;
	  recalc_weight_idx += var_flt64_cnt (v);
	}
      hdr.weight_idx = recalc_weight_idx;
    }
  else
    hdr.weight_idx = 0;

  hdr.case_cnt = -1;
  hdr.bias = COMPRESSION_BIAS;

  if (time (&t) == (time_t) -1)
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

    buf_copy_str_rpad (hdr.file_label, sizeof hdr.file_label, label); 
  }
  
  memset (hdr.padding, 0, sizeof hdr.padding);

  if (!buf_write (w, &hdr, sizeof hdr))
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
   variable S to system file W. */
static int
write_variable (struct sfm_writer *w, struct variable *v)
{
  struct sysfile_variable sv;

  /* Missing values. */
  struct missing_values mv;
  flt64 m[3];           /* Missing value values. */
  int nm;               /* Number of missing values, possibly negative. */

  sv.rec_type = 2;
  sv.type = v->width;
  sv.has_var_label = (v->label != NULL);

  mv_copy (&mv, &v->miss);
  nm = 0;
  if (mv_has_range (&mv)) 
    {
      double x, y;
      mv_pop_range (&mv, &x, &y);
      m[nm++] = x == LOWEST ? second_lowest_flt64 : x;
      m[nm++] = y == HIGHEST ? FLT64_MAX : y;
    }
  while (mv_has_value (&mv))
    {
      union value value;
      mv_pop_value (&mv, &value);
      if (v->type == NUMERIC)
        m[nm] = value.f;
      else
        buf_copy_rpad ((char *) &m[nm], sizeof m[nm], value.s, v->width);
      nm++;
    }
  if (mv_has_range (&v->miss))
    nm = -nm;

  sv.n_missing_values = nm;
  write_format_spec (&v->print, &sv.print);
  write_format_spec (&v->write, &sv.write);
  buf_copy_str_rpad (sv.name, sizeof sv.name, v->short_name);
  if (!buf_write (w, &sv, sizeof sv))
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

      if (!buf_write (w, &l, offsetof (struct label, label) + ext_len))
        return 0;
    }

  if (nm && !buf_write (w, m, sizeof *m * abs (nm)))
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
	if (!buf_write (w, &sv, sizeof sv))
	  return 0;
    }

  return 1;
}

/* Writes the value labels for variable V having system file variable
   index IDX to system file W.  Returns
   nonzero only if successful. */
static int
write_value_labels (struct sfm_writer *w, struct variable *v, int idx)
{
  struct value_label_rec
    {
      int32 rec_type P;
      int32 n_labels P;
      flt64 labels[1] P;
    };

  struct var_idx_rec
    {
      int32 rec_type P;
      int32 n_vars P;
      int32 vars[1] P;
    };

  struct val_labs_iterator *i;
  struct value_label_rec *vlr;
  struct var_idx_rec vir;
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
      memcpy (&((char *) loc)[1], vl->label, len);
      memset (&((char *) loc)[1 + len], ' ',
	      REM_RND_UP (len + 1, sizeof (flt64)));
      loc += DIV_RND_UP (len + 1, sizeof (flt64));
    }
  
  if (!buf_write (w, vlr, vlr_size))
    {
      free (vlr);
      return 0;
    }
  free (vlr);

  vir.rec_type = 4;
  vir.n_vars = 1;
  vir.vars[0] = idx + 1;
  if (!buf_write (w, &vir, sizeof vir))
    return 0;

  return 1;
}

/* Writes record type 6, document record. */
static int
write_documents (struct sfm_writer *w, const struct dictionary *d)
{
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
  if (!buf_write (w, &rec_6, sizeof rec_6))
    return 0;
  if (!buf_write (w, documents, 80 * n_lines))
    return 0;

  return 1;
}

/* Write the alignment, width and scale values */
static int
write_variable_display_parameters (struct sfm_writer *w, 
				   const struct dictionary *dict)
{
  int i;

  struct
  {
    int32 rec_type P;
    int32 subtype P;
    int32 elem_size P;
    int32 n_elem P;
  } vdp_hdr;

  vdp_hdr.rec_type = 7;
  vdp_hdr.subtype = 11;
  vdp_hdr.elem_size = 4;
  vdp_hdr.n_elem = w->var_cnt * 3;

  if (!buf_write (w, &vdp_hdr, sizeof vdp_hdr))
    return 0;

  for ( i = 0 ; i < w->var_cnt ; ++i ) 
    {
      struct variable *v;
      struct
      {
	int32 measure P;
	int32 width P;
	int32 align P;
      }
      params;

      v = dict_get_var(dict, i);

      params.measure = v->measure;
      params.width = v->display_width;
      params.align = v->alignment;
      
      if (!buf_write (w, &params, sizeof(params)))
	return 0;
    }
  
  return 1;
}

/* Writes the long variable name table */
static int
write_longvar_table (struct sfm_writer *w, const struct dictionary *dict)
{
  struct
    {
      int32 rec_type P;
      int32 subtype P;
      int32 elem_size P;
      int32 n_elem P;
    }
  lv_hdr;

  struct string long_name_map;
  size_t i;

  ds_init (&long_name_map, 10 * dict_get_var_cnt (dict));
  for (i = 0; i < dict_get_var_cnt (dict); i++)
    {
      struct variable *v = dict_get_var (dict, i);
      
      if (i)
        ds_putc (&long_name_map, '\t');
      ds_printf (&long_name_map, "%s=%s", v->short_name, v->name);
    }

  lv_hdr.rec_type = 7;
  lv_hdr.subtype = 13;
  lv_hdr.elem_size = 1;
  lv_hdr.n_elem = ds_length (&long_name_map);

  if (!buf_write (w, &lv_hdr, sizeof lv_hdr)
      || !buf_write (w, ds_data (&long_name_map), ds_length (&long_name_map)))
    goto error;

  ds_destroy (&long_name_map);
  return 1;

 error:
  ds_destroy (&long_name_map);
  return 0;
}

/* Writes record type 7, subtypes 3 and 4. */
static int
write_rec_7_34 (struct sfm_writer *w)
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

  if (!buf_write (w, &rec_7, sizeof rec_7))
    return 0;
  return 1;
}

/* Write NBYTES starting at BUF to the system file represented by
   H. */
static int
buf_write (struct sfm_writer *w, const void *buf, size_t nbytes)
{
  assert (buf != NULL);
  if (fwrite (buf, nbytes, 1, w->file) != 1)
    {
      msg (ME, _("%s: Writing system file: %s."),
           handle_get_filename (w->fh), strerror (errno));
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
ensure_buf_space (struct sfm_writer *w)
{
  if (w->ptr >= w->end)
    {
      memset (w->x, 0, w->y - w->x);
      w->x = w->y;
      w->ptr = w->buf;
      if (!buf_write (w, w->buf, sizeof *w->buf * 128))
	return 0;
    }
  return 1;
}

static void write_compressed_data (struct sfm_writer *w, const flt64 *elem);

/* Writes case C to system file W.
   Returns nonzero if successful. */
int
sfm_write_case (struct sfm_writer *w, struct ccase *c)
{
  w->case_cnt++;

  if (!w->needs_translation && !w->compress
      && sizeof (flt64) == sizeof (union value)) 
    {
      /* Fast path: external and internal representations are the
         same and the dictionary is properly ordered.  Write
         directly to file. */
      buf_write (w, case_data_all (c), sizeof (union value) * w->flt64_cnt);
    }
  else 
    {
      /* Slow path: internal and external representations differ.
         Write into a bounce buffer, then write to W. */
      flt64 *bounce;
      flt64 *bounce_cur;
      size_t bounce_size;
      size_t i;

      bounce_size = sizeof *bounce * w->flt64_cnt;
      bounce = bounce_cur = local_alloc (bounce_size);

      for (i = 0; i < w->var_cnt; i++) 
        {
          struct sfm_var *v = &w->vars[i];

          if (v->width == 0) 
            *bounce_cur = case_num (c, v->fv);
          else 
            memcpy (bounce_cur, case_data (c, v->fv)->s, v->width);
          bounce_cur += v->flt64_cnt;
        }

      if (!w->compress)
        buf_write (w, bounce, bounce_size);
      else
        write_compressed_data (w, bounce);

      local_free (bounce); 
    }
  
  return 1;
}

static void
put_instruction (struct sfm_writer *w, unsigned char instruction) 
{
  if (w->x >= w->y)
    {
      if (!ensure_buf_space (w))
        return;
      w->x = (unsigned char *) w->ptr++;
      w->y = (unsigned char *) w->ptr;
    }
  *w->x++ = instruction;
}

static void
put_element (struct sfm_writer *w, const flt64 *elem) 
{
  if (!ensure_buf_space (w))
    return;
  memcpy (w->ptr++, elem, sizeof *elem);
}

static void
write_compressed_data (struct sfm_writer *w, const flt64 *elem) 
{
  size_t i;

  for (i = 0; i < w->var_cnt; i++)
    {
      struct sfm_var *v = &w->vars[i];

      if (v->width == 0) 
        {
          if (*elem == -FLT64_MAX)
            put_instruction (w, 255);
          else if (*elem >= 1 - COMPRESSION_BIAS
                   && *elem <= 251 - COMPRESSION_BIAS
                   && *elem == (int) *elem) 
            put_instruction (w, (int) *elem + COMPRESSION_BIAS);
          else
            {
              put_instruction (w, 253);
              put_element (w, elem);
            }
          elem++;
        }
      else 
        {
          size_t j;
          
          for (j = 0; j < v->flt64_cnt; j++, elem++) 
            {
              if (!memcmp (elem, "        ", sizeof (flt64)))
                put_instruction (w, 254);
              else 
                {
                  put_instruction (w, 253);
                  put_element (w, elem);
                }
            }
        }
    }
}

/* Closes a system file after we're done with it. */
void
sfm_close_writer (struct sfm_writer *w)
{
  if (w == NULL)
    return;

  fh_close (w->fh, "system file", "we");
  
  if (w->file != NULL) 
    {
      /* Flush buffer. */
      if (w->buf != NULL && w->ptr > w->buf)
        {
          memset (w->x, 0, w->y - w->x);
          buf_write (w, w->buf, (w->ptr - w->buf) * sizeof *w->buf);
        }

      /* Seek back to the beginning and update the number of cases.
         This is just a courtesy to later readers, so there's no need
         to check return values or report errors. */
      if (!fseek (w->file, offsetof (struct sysfile_header, case_cnt), SEEK_SET))
        {
          int32 case_cnt = w->case_cnt;

          /* I don't really care about the return value: it doesn't
             matter whether this data is written. */
          fwrite (&case_cnt, sizeof case_cnt, 1, w->file);
        }

      if (fclose (w->file) == EOF)
        msg (ME, _("%s: Closing system file: %s."),
             handle_get_filename (w->fh), strerror (errno));
    }

  free (w->buf);
  free (w->vars);
  free (w);
}
