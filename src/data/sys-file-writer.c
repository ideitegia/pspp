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
#include "sys-file-writer.h"
#include "sfm-private.h"
#include <libpspp/message.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <libpspp/alloc.h>
#include "case.h"
#include "dictionary.h"
#include <libpspp/message.h>
#include "file-handle-def.h"
#include <libpspp/hash.h>
#include <libpspp/magic.h>
#include <libpspp/misc.h>
#include "settings.h"
#include "stat-macros.h"
#include <libpspp/str.h>
#include "value-labels.h"
#include "variable.h"
#include <libpspp/version.h>
#include <minmax.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

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
    bool has_vls;               /* Does the dict have very long strings? */

    /* Compression buffering. */
    flt64 *buf;			/* Buffered data. */
    flt64 *end;			/* Buffer end. */
    flt64 *ptr;			/* Current location in buffer. */
    unsigned char *x;		/* Location in current instruction octet. */
    unsigned char *y;		/* End of instruction octet. */

    /* Variables. */
    struct sfm_var *vars;       /* Variables. */
    size_t var_cnt;             /* Number of variables. */
    size_t var_cnt_vls;         /* Number of variables including 
				   very long string components. */
  };

/* A variable in a system file. */
struct sfm_var 
  {
    int width;                  /* 0=numeric, otherwise string width. */
    int fv;                     /* Index into case. */
    size_t flt64_cnt;           /* Number of flt64 elements. */
  };

static char *append_string_max (char *, const char *, const char *);
static void write_header (struct sfm_writer *, const struct dictionary *);
static void buf_write (struct sfm_writer *, const void *, size_t);
static void write_variable (struct sfm_writer *, const struct variable *);
static void write_value_labels (struct sfm_writer *,
                                struct variable *, int idx);
static void write_rec_7_34 (struct sfm_writer *);

static void write_longvar_table (struct sfm_writer *w, 
                                 const struct dictionary *dict);

static void write_vls_length_table (struct sfm_writer *w, 
			      const struct dictionary *dict);


static void write_variable_display_parameters (struct sfm_writer *w, 
                                               const struct dictionary *dict);

static void write_documents (struct sfm_writer *, const struct dictionary *);

static inline int
var_flt64_cnt (const struct variable *v) 
{
  assert(sizeof(flt64) == MAX_SHORT_STRING);
  return width_to_bytes(v->width) / MAX_SHORT_STRING ;
}

static inline int
var_flt64_cnt_nom (const struct variable *v) 
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


/* Return a short variable name to be used as the continuation of the
   variable with the short name SN.

   FIXME: Need to resolve clashes somehow.

 */
static const char *
cont_var_name(const char *sn, int idx)
{
  static char s[SHORT_NAME_LEN + 1];

  char abb[SHORT_NAME_LEN + 1 - 3]= {0}; 

  strncpy(abb, sn, SHORT_NAME_LEN - 3);

  snprintf(s, SHORT_NAME_LEN + 1, "%s%03d", abb, idx);

  return s;
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
  fd = open (fh_get_file_name (fh), O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd < 0) 
    goto open_error;

  /* Open file handle. */
  if (!fh_open (fh, FH_REF_FILE, "system file", "we"))
    goto error;

  /* Create and initialize writer. */
  w = xmalloc (sizeof *w);
  w->fh = fh;
  w->file = fdopen (fd, "w");

  w->needs_translation = dict_compacting_would_change (d);
  w->compress = opts.compress;
  w->case_cnt = 0;
  w->flt64_cnt = 0;
  w->has_vls = false;

  w->buf = w->end = w->ptr = NULL;
  w->x = w->y = NULL;

  w->var_cnt = dict_get_var_cnt (d);
  w->var_cnt_vls = w->var_cnt;
  w->vars = xnmalloc (w->var_cnt, sizeof *w->vars);
  for (i = 0; i < w->var_cnt; i++) 
    {
      const struct variable *dv = dict_get_var (d, i);
      struct sfm_var *sv = &w->vars[i];
      sv->width = dv->width;
      /* spss compatibility nonsense */
      if ( dv->width > MAX_LONG_STRING ) 
	  w->has_vls = true;

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
  write_header (w, d);

  /* Write basic variable info. */
  dict_assign_short_names (d);
  for (i = 0; i < dict_get_var_cnt (d); i++)
    {
      int count = 0;
      const struct variable *v = dict_get_var(d, i);
      int wcount = v->width;

      do {
	struct variable var_cont = *v;
	if ( v->type == ALPHA) 
	  {
	    if ( 0 != count ) 
	      {
		mv_init(&var_cont.miss, 0);
		strcpy(var_cont.short_name,
		       cont_var_name(v->short_name, count));
		var_cont.label = NULL;
		w->var_cnt_vls++;
	      }
	    count++;
	    if ( wcount > MAX_LONG_STRING ) 
	      {
		var_cont.width = MAX_LONG_STRING;
		wcount -= EFFECTIVE_LONG_STRING_LENGTH;
	      }
	    else
	      {
		var_cont.width = wcount;
		wcount -= var_cont.width;
	      }
	
	    var_cont.write.w = var_cont.width;
	    var_cont.print.w = var_cont.width;
	  }

	write_variable (w, &var_cont);
      } while(wcount > 0);
    }

  /* Write out value labels. */
  for (idx = i = 0; i < dict_get_var_cnt (d); i++)
    {
      struct variable *v = dict_get_var (d, i);

      write_value_labels (w, v, idx);
      idx += var_flt64_cnt (v);
    }

  if (dict_get_documents (d) != NULL)
    write_documents (w, d);

  write_rec_7_34 (w);

  write_variable_display_parameters (w, d);

  if (opts.version >= 3) 
    write_longvar_table (w, d);

  write_vls_length_table(w, d);

  /* Write end-of-headers record. */
  {
    struct
      {
	int32_t rec_type P;
	int32_t filler P;
      }
    rec_999;

    rec_999.rec_type = 999;
    rec_999.filler = 0;

    buf_write (w, &rec_999, sizeof rec_999);
  }

  if (w->compress) 
    {
      w->buf = xnmalloc (128, sizeof *w->buf);
      w->ptr = w->buf;
      w->end = &w->buf[128];
      w->x = (unsigned char *) w->ptr++;
      w->y = (unsigned char *) w->ptr;
    }

  if (sfm_write_error (w))
    goto error;
  
  return w;

 error:
  sfm_close_writer (w);
  return NULL;

 open_error:
  msg (ME, _("Error opening \"%s\" for writing as a system file: %s."),
       fh_get_file_name (fh), strerror (errno));
  goto error;
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
static void
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
    {
      w->flt64_cnt += var_flt64_cnt (dict_get_var (d, i));
    }
  hdr.nominal_case_size = w->flt64_cnt;

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

  buf_write (w, &hdr, sizeof hdr);
}

/* Translates format spec from internal form in SRC to system file
   format in DEST. */
static inline void
write_format_spec (const struct fmt_spec *src, int32_t *dest)
{
  assert(check_output_specifier(src, true));
  *dest = (formats[src->type].spss << 16) | (src->w << 8) | src->d;
}

/* Write the variable record(s) for primary variable P and secondary
   variable S to system file W. */
static void
write_variable (struct sfm_writer *w, const struct variable *v)
{
  struct sysfile_variable sv;

  /* Missing values. */
  struct missing_values mv;
  flt64 m[3];           /* Missing value values. */
  int nm;               /* Number of missing values, possibly negative. */

  sv.rec_type = 2;
  sv.type = min(v->width, MAX_LONG_STRING);
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
  buf_write (w, &sv, sizeof sv);

  if (v->label)
    {
      struct label
	{
	  int32_t label_len P;
	  char label[255] P;
	}
      l;

      int ext_len;

      l.label_len = min (strlen (v->label), 255);
      ext_len = ROUND_UP (l.label_len, sizeof l.label_len);
      memcpy (l.label, v->label, l.label_len);
      memset (&l.label[l.label_len], ' ', ext_len - l.label_len);

      buf_write (w, &l, offsetof (struct label, label) + ext_len);
    }

  if (nm)
    buf_write (w, m, sizeof *m * abs (nm));

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

      pad_count = DIV_RND_UP (min(v->width, MAX_LONG_STRING),
			      (int) sizeof (flt64)) - 1;
      for (i = 0; i < pad_count; i++)
	buf_write (w, &sv, sizeof sv);
    }
}

/* Writes the value labels for variable V having system file
   variable index IDX to system file W. */
static void
write_value_labels (struct sfm_writer *w, struct variable *v, int idx)
{
  struct value_label_rec
    {
      int32_t rec_type P;
      int32_t n_labels P;
      flt64 labels[1] P;
    };

  struct var_idx_rec
    {
      int32_t rec_type P;
      int32_t n_vars P;
      int32_t vars[1] P;
    };

  struct val_labs_iterator *i;
  struct value_label_rec *vlr;
  struct var_idx_rec vir;
  struct val_lab *vl;
  size_t vlr_size;
  flt64 *loc;

  if (!val_labs_count (v->val_labs))
    return;

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
  
  buf_write (w, vlr, vlr_size);
  free (vlr);

  vir.rec_type = 4;
  vir.n_vars = 1;
  vir.vars[0] = idx + 1;
  buf_write (w, &vir, sizeof vir);
}

/* Writes record type 6, document record. */
static void
write_documents (struct sfm_writer *w, const struct dictionary *d)
{
  struct
    {
      int32_t rec_type P;		/* Always 6. */
      int32_t n_lines P;		/* Number of lines of documents. */
    }
  rec_6;

  const char *documents;
  size_t n_lines;

  documents = dict_get_documents (d);
  n_lines = strlen (documents) / 80;

  rec_6.rec_type = 6;
  rec_6.n_lines = n_lines;
  buf_write (w, &rec_6, sizeof rec_6);
  buf_write (w, documents, 80 * n_lines);
}

/* Write the alignment, width and scale values */
static void
write_variable_display_parameters (struct sfm_writer *w, 
				   const struct dictionary *dict)
{
  int i;

  struct
  {
    int32_t rec_type P;
    int32_t subtype P;
    int32_t elem_size P;
    int32_t n_elem P;
  } vdp_hdr;

  vdp_hdr.rec_type = 7;
  vdp_hdr.subtype = 11;
  vdp_hdr.elem_size = 4;
  vdp_hdr.n_elem = w->var_cnt_vls * 3;

  buf_write (w, &vdp_hdr, sizeof vdp_hdr);

  for ( i = 0 ; i < w->var_cnt ; ++i ) 
    {
      struct variable *v;
      struct
      {
	int32_t measure P;
	int32_t width P;
	int32_t align P;
      }
      params;

      v = dict_get_var(dict, i);

      params.measure = v->measure;
      params.width = v->display_width;
      params.align = v->alignment;
      
      buf_write (w, &params, sizeof(params));

      if ( v->width > MAX_LONG_STRING ) 
	{
	  int wcount = v->width - EFFECTIVE_LONG_STRING_LENGTH ;

	  while (wcount > 0) 
	    {
	      params.width = wcount > MAX_LONG_STRING ? 32 : wcount;
	    
	      buf_write (w, &params, sizeof(params));

	      wcount -= EFFECTIVE_LONG_STRING_LENGTH ;
	    } 
	}
    }
}

/* Writes the table of lengths for Very Long String Variables */
static void 
write_vls_length_table (struct sfm_writer *w, 
			const struct dictionary *dict)
{
  int i;
  struct
    {
      int32_t rec_type P;
      int32_t subtype P;
      int32_t elem_size P;
      int32_t n_elem P;
    }
  vls_hdr;

  struct string vls_length_map;

  ds_init (&vls_length_map);

  vls_hdr.rec_type = 7;
  vls_hdr.subtype = 14;
  vls_hdr.elem_size = 1;


  for (i = 0; i < dict_get_var_cnt (dict); ++i)
    {
      const struct variable *v = dict_get_var (dict, i);
      
      if ( v->width <=  MAX_LONG_STRING ) 
	continue;

      ds_printf (&vls_length_map, "%s=%05d", v->short_name, v->width);
      ds_putc (&vls_length_map, '\0');
      ds_putc (&vls_length_map, '\t');
    }

  vls_hdr.n_elem = ds_length (&vls_length_map);

  if ( vls_hdr.n_elem > 0 ) 
    {
      buf_write (w, &vls_hdr, sizeof vls_hdr);
      buf_write (w, ds_data (&vls_length_map), ds_length (&vls_length_map));
    }

  ds_destroy (&vls_length_map);
}

/* Writes the long variable name table */
static void
write_longvar_table (struct sfm_writer *w, const struct dictionary *dict)
{
  struct
    {
      int32_t rec_type P;
      int32_t subtype P;
      int32_t elem_size P;
      int32_t n_elem P;
    }
  lv_hdr;

  struct string long_name_map;
  size_t i;

  ds_init (&long_name_map);
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

  buf_write (w, &lv_hdr, sizeof lv_hdr);
  buf_write (w, ds_data (&long_name_map), ds_length (&long_name_map));

  ds_destroy (&long_name_map);
}

/* Writes record type 7, subtypes 3 and 4. */
static void
write_rec_7_34 (struct sfm_writer *w)
{
  struct
    {
      int32_t rec_type_3 P;
      int32_t subtype_3 P;
      int32_t data_type_3 P;
      int32_t n_elem_3 P;
      int32_t elem_3[8] P;
      int32_t rec_type_4 P;
      int32_t subtype_4 P;
      int32_t data_type_4 P;
      int32_t n_elem_4 P;
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
  rec_7.data_type_3 = sizeof (int32_t);
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

  buf_write (w, &rec_7, sizeof rec_7);
}

/* Write NBYTES starting at BUF to the system file represented by
   H. */
static void
buf_write (struct sfm_writer *w, const void *buf, size_t nbytes)
{
  assert (buf != NULL);
  fwrite (buf, nbytes, 1, w->file);
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
static void
ensure_buf_space (struct sfm_writer *w)
{
  if (w->ptr >= w->end)
    {
      memset (w->x, 0, w->y - w->x);
      w->x = w->y;
      w->ptr = w->buf;
      buf_write (w, w->buf, sizeof *w->buf * 128);
    }
}

static void write_compressed_data (struct sfm_writer *w, const flt64 *elem);

/* Writes case C to system file W.
   Returns 1 if successful, 0 if an I/O error occurred. */
int
sfm_write_case (struct sfm_writer *w, const struct ccase *c)
{
  if (ferror (w->file))
    return 0;
  
  w->case_cnt++;

  if (!w->needs_translation && !w->compress
      && sizeof (flt64) == sizeof (union value) && ! w->has_vls )
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
      flt64 *bounce_end;
      size_t bounce_size;
      size_t i;

      bounce_size = sizeof *bounce * w->flt64_cnt;
      bounce = bounce_cur = local_alloc (bounce_size);
      bounce_end = bounce + bounce_size;

      for (i = 0; i < w->var_cnt; i++) 
        {
          struct sfm_var *v = &w->vars[i];

	  memset(bounce_cur, ' ', v->flt64_cnt * sizeof (flt64));

          if (v->width == 0) 
	    {
	      *bounce_cur = case_num (c, v->fv);
	      bounce_cur += v->flt64_cnt;
	    }
          else 
	    { int ofs = 0;
	    while (ofs < v->width)
	      {
		int chunk = MIN (MAX_LONG_STRING, v->width - ofs);
		int nv = DIV_RND_UP (chunk, sizeof (flt64));
		buf_copy_rpad ((char *) bounce_cur, nv * sizeof (flt64),
			       case_data (c, v->fv)->s + ofs, chunk);
		bounce_cur += nv;
		ofs += chunk;
	      }
	    }

        }

      if (!w->compress)
        buf_write (w, bounce, bounce_size);
      else
        write_compressed_data (w, bounce);

      local_free (bounce); 
    }
  
  return !sfm_write_error (w);
}

static void
put_instruction (struct sfm_writer *w, unsigned char instruction) 
{
  if (w->x >= w->y)
    {
      ensure_buf_space (w);
      w->x = (unsigned char *) w->ptr++;
      w->y = (unsigned char *) w->ptr;
    }
  *w->x++ = instruction;
}

static void
put_element (struct sfm_writer *w, const flt64 *elem) 
{
  ensure_buf_space (w);
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

/* Returns true if an I/O error has occurred on WRITER, false otherwise. */
bool
sfm_write_error (const struct sfm_writer *writer)
{
  return ferror (writer->file);
}

/* Closes a system file after we're done with it.
   Returns true if successful, false if an I/O error occurred. */
bool
sfm_close_writer (struct sfm_writer *w)
{
  bool ok;
  
  if (w == NULL)
    return true;

  ok = true;
  if (w->file != NULL) 
    {
      /* Flush buffer. */
      if (w->buf != NULL && w->ptr > w->buf)
        {
          memset (w->x, 0, w->y - w->x);
          buf_write (w, w->buf, (w->ptr - w->buf) * sizeof *w->buf);
        }
      fflush (w->file);

      ok = !sfm_write_error (w);

      /* Seek back to the beginning and update the number of cases.
         This is just a courtesy to later readers, so there's no need
         to check return values or report errors. */
      if (ok && !fseek (w->file, offsetof (struct sysfile_header, case_cnt),
                        SEEK_SET))
        {
          int32_t case_cnt = w->case_cnt;
          fwrite (&case_cnt, sizeof case_cnt, 1, w->file);
          clearerr (w->file);
        }

      if (fclose (w->file) == EOF)
        ok = false;

      if (!ok)
        msg (ME, _("An I/O error occurred writing system file \"%s\"."),
             fh_get_file_name (w->fh));
    }

  fh_close (w->fh, "system file", "we");
  
  free (w->buf);
  free (w->vars);
  free (w);

  return ok;
}
