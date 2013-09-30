/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-2000, 2006-2013 Free Software Foundation, Inc.

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

#include "data/sys-file-writer.h"
#include "data/sys-file-private.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "data/attributes.h"
#include "data/case.h"
#include "data/casewriter-provider.h"
#include "data/casewriter.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "data/format.h"
#include "data/make-file.h"
#include "data/missing-values.h"
#include "data/mrset.h"
#include "data/settings.h"
#include "data/short-names.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "libpspp/float-format.h"
#include "libpspp/i18n.h"
#include "libpspp/integer-format.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"
#include "libpspp/version.h"

#include "gl/xmemdup0.h"
#include "gl/minmax.h"
#include "gl/unlocked-io.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* Compression bias used by PSPP.  Values between (1 -
   COMPRESSION_BIAS) and (251 - COMPRESSION_BIAS) inclusive can be
   compressed. */
#define COMPRESSION_BIAS 100

/* System file writer. */
struct sfm_writer
  {
    struct file_handle *fh;     /* File handle. */
    struct fh_lock *lock;       /* Mutual exclusion for file. */
    FILE *file;			/* File stream. */
    struct replace_file *rf;    /* Ticket for replacing output file. */

    bool compress;		/* 1=compressed, 0=not compressed. */
    casenumber case_cnt;	/* Number of cases written so far. */
    uint8_t space;              /* ' ' in the file's character encoding. */

    /* Compression buffering.

       Compressed data is output as groups of 8 1-byte opcodes
       followed by up to 8 (depending on the opcodes) 8-byte data
       items.  Data items and opcodes arrive at the same time but
       must be reordered for writing to disk, thus a small amount
       of buffering here. */
    uint8_t opcodes[8];         /* Buffered opcodes. */
    int opcode_cnt;             /* Number of buffered opcodes. */
    uint8_t data[8][8];         /* Buffered data. */
    int data_cnt;               /* Number of buffered data items. */

    /* Variables. */
    struct sfm_var *sfm_vars;   /* Variables. */
    size_t sfm_var_cnt;         /* Number of variables. */
    size_t segment_cnt;         /* Number of variables including extra segments
                                   for long string variables. */
  };

static const struct casewriter_class sys_file_casewriter_class;

static void write_header (struct sfm_writer *, const struct dictionary *);
static void write_variable (struct sfm_writer *, const struct variable *);
static void write_value_labels (struct sfm_writer *,
                                const struct dictionary *);
static void write_integer_info_record (struct sfm_writer *,
                                       const struct dictionary *);
static void write_float_info_record (struct sfm_writer *);

static void write_longvar_table (struct sfm_writer *w,
                                 const struct dictionary *dict);

static void write_encoding_record (struct sfm_writer *w,
				   const struct dictionary *);

static void write_vls_length_table (struct sfm_writer *w,
			      const struct dictionary *dict);

static void write_long_string_value_labels (struct sfm_writer *,
                                            const struct dictionary *);
static void write_long_string_missing_values (struct sfm_writer *,
                                              const struct dictionary *);

static void write_mrsets (struct sfm_writer *, const struct dictionary *,
                          bool pre_v14);

static void write_variable_display_parameters (struct sfm_writer *w,
                                               const struct dictionary *dict);

static void write_documents (struct sfm_writer *, const struct dictionary *);

static void write_data_file_attributes (struct sfm_writer *,
                                        const struct dictionary *);
static void write_variable_attributes (struct sfm_writer *,
                                       const struct dictionary *);

static void write_int (struct sfm_writer *, int32_t);
static inline void convert_double_to_output_format (double, uint8_t[8]);
static void write_float (struct sfm_writer *, double);
static void write_string (struct sfm_writer *, const char *, size_t);
static void write_utf8_string (struct sfm_writer *, const char *encoding,
                               const char *string, size_t width);
static void write_utf8_record (struct sfm_writer *, const char *encoding,
                               const struct string *content, int subtype);
static void write_string_record (struct sfm_writer *,
                                 const struct substring content, int subtype);
static void write_bytes (struct sfm_writer *, const void *, size_t);
static void write_zeros (struct sfm_writer *, size_t);
static void write_spaces (struct sfm_writer *, size_t);
static void write_value (struct sfm_writer *, const union value *, int width);

static void write_case_uncompressed (struct sfm_writer *,
                                     const struct ccase *);
static void write_case_compressed (struct sfm_writer *, const struct ccase *);
static void flush_compressed (struct sfm_writer *);
static void put_cmp_opcode (struct sfm_writer *, uint8_t);
static void put_cmp_number (struct sfm_writer *, double);
static void put_cmp_string (struct sfm_writer *, const void *, size_t);

static bool write_error (const struct sfm_writer *);
static bool close_writer (struct sfm_writer *);

/* Returns default options for writing a system file. */
struct sfm_write_options
sfm_writer_default_options (void)
{
  struct sfm_write_options opts;
  opts.create_writeable = true;
  opts.compress = settings_get_scompression ();
  opts.version = 3;
  return opts;
}

/* Opens the system file designated by file handle FH for writing
   cases from dictionary D according to the given OPTS.

   No reference to D is retained, so it may be modified or
   destroyed at will after this function returns.  D is not
   modified by this function, except to assign short names. */
struct casewriter *
sfm_open_writer (struct file_handle *fh, struct dictionary *d,
                 struct sfm_write_options opts)
{
  struct encoding_info encoding_info;
  struct sfm_writer *w;
  mode_t mode;
  int i;

  /* Check version. */
  if (opts.version != 2 && opts.version != 3)
    {
      msg (ME, _("Unknown system file version %d. Treating as version %d."),
           opts.version, 3);
      opts.version = 3;
    }

  /* Create and initialize writer. */
  w = xmalloc (sizeof *w);
  w->fh = fh_ref (fh);
  w->lock = NULL;
  w->file = NULL;
  w->rf = NULL;

  w->compress = opts.compress;
  w->case_cnt = 0;

  w->opcode_cnt = w->data_cnt = 0;

  /* Figure out how to map in-memory case data to on-disk case
     data.  Also count the number of segments.  Very long strings
     occupy multiple segments, otherwise each variable only takes
     one segment. */
  w->segment_cnt = sfm_dictionary_to_sfm_vars (d, &w->sfm_vars,
                                               &w->sfm_var_cnt);

  /* Open file handle as an exclusive writer. */
  /* TRANSLATORS: this fragment will be interpolated into
     messages in fh_lock() that identify types of files. */
  w->lock = fh_lock (fh, FH_REF_FILE, N_("system file"), FH_ACC_WRITE, true);
  if (w->lock == NULL)
    goto error;

  /* Create the file on disk. */
  mode = 0444;
  if (opts.create_writeable)
    mode |= 0222;
  w->rf = replace_file_start (fh_get_file_name (fh), "wb", mode,
                              &w->file, NULL);
  if (w->rf == NULL)
    {
      msg (ME, _("Error opening `%s' for writing as a system file: %s."),
           fh_get_file_name (fh), strerror (errno));
      goto error;
    }

  get_encoding_info (&encoding_info, dict_get_encoding (d));
  w->space = encoding_info.space[0];

  /* Write the file header. */
  write_header (w, d);

  /* Write basic variable info. */
  short_names_assign (d);
  for (i = 0; i < dict_get_var_cnt (d); i++)
    write_variable (w, dict_get_var (d, i));

  write_value_labels (w, d);

  if (dict_get_document_line_cnt (d) > 0)
    write_documents (w, d);

  write_integer_info_record (w, d);
  write_float_info_record (w);

  write_mrsets (w, d, true);

  write_variable_display_parameters (w, d);

  if (opts.version >= 3)
    write_longvar_table (w, d);

  write_vls_length_table (w, d);

  write_long_string_value_labels (w, d);
  write_long_string_missing_values (w, d);

  if (opts.version >= 3)
    {
      if (attrset_count (dict_get_attributes (d)))
        write_data_file_attributes (w, d);
      write_variable_attributes (w, d);
    }

  write_mrsets (w, d, false);

  write_encoding_record (w, d);

  /* Write end-of-headers record. */
  write_int (w, 999);
  write_int (w, 0);

  if (write_error (w))
    goto error;

  return casewriter_create (dict_get_proto (d), &sys_file_casewriter_class, w);

error:
  close_writer (w);
  return NULL;
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

/* Calculates the offset of data for TARGET_VAR from the
   beginning of each case's data for dictionary D.  The return
   value is in "octs" (8-byte units). */
static int
calc_oct_idx (const struct dictionary *d, struct variable *target_var)
{
  int oct_idx;
  int i;

  oct_idx = 0;
  for (i = 0; i < dict_get_var_cnt (d); i++)
    {
      struct variable *var = dict_get_var (d, i);
      if (var == target_var)
        break;
      oct_idx += sfm_width_to_octs (var_get_width (var));
    }
  return oct_idx;
}

/* Write the sysfile_header header to system file W. */
static void
write_header (struct sfm_writer *w, const struct dictionary *d)
{
  const char *dict_encoding = dict_get_encoding (d);
  char prod_name[61];
  char creation_date[10];
  char creation_time[9];
  const char *file_label;
  struct variable *weight;

  time_t t;

  /* Record-type code. */
  if (is_encoding_ebcdic_compatible (dict_encoding))
    write_string (w, EBCDIC_MAGIC, 4);
  else
    write_string (w, ASCII_MAGIC, 4);

  /* Product identification. */
  snprintf (prod_name, sizeof prod_name, "@(#) SPSS DATA FILE %s - %s",
            version, host_system);
  write_utf8_string (w, dict_encoding, prod_name, 60);

  /* Layout code. */
  write_int (w, 2);

  /* Number of `union value's per case. */
  write_int (w, calc_oct_idx (d, NULL));

  /* Compressed? */
  write_int (w, w->compress);

  /* Weight variable. */
  weight = dict_get_weight (d);
  write_int (w, weight != NULL ? calc_oct_idx (d, weight) + 1 : 0);

  /* Number of cases.  We don't know this in advance, so we write
     -1 to indicate an unknown number of cases.  Later we can
     come back and overwrite it with the true value. */
  write_int (w, -1);

  /* Compression bias. */
  write_float (w, COMPRESSION_BIAS);

  /* Creation date and time. */
  if (time (&t) == (time_t) -1)
    {
      strcpy (creation_date, "01 Jan 70");
      strcpy (creation_time, "00:00:00");
    }
  else
    {
      static const char *const month_name[12] =
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

      snprintf (creation_date, sizeof creation_date,
                "%02d %s %02d", day, month_name[mon - 1], year);
      snprintf (creation_time, sizeof creation_time,
                "%02d:%02d:%02d", hour - 1, min - 1, sec - 1);
    }
  write_utf8_string (w, dict_encoding, creation_date, 9);
  write_utf8_string (w, dict_encoding, creation_time, 8);

  /* File label. */
  file_label = dict_get_label (d);
  if (file_label == NULL)
    file_label = "";
  write_utf8_string (w, dict_encoding, file_label, 64);

  /* Padding. */
  write_zeros (w, 3);
}

/* Write format spec FMT to W, after adjusting it to be
   compatible with the given WIDTH. */
static void
write_format (struct sfm_writer *w, struct fmt_spec fmt, int width)
{
  assert (fmt_check_output (&fmt));
  assert (sfm_width_to_segments (width) == 1);

  if (width > 0)
    fmt_resize (&fmt, width);
  write_int (w, (fmt_to_io (fmt.type) << 16) | (fmt.w << 8) | fmt.d);
}

/* Write a string continuation variable record for each 8-byte
   section beyond the initial 8 bytes, for a variable of the
   given WIDTH. */
static void
write_variable_continuation_records (struct sfm_writer *w, int width)
{
  int position;

  assert (sfm_width_to_segments (width) == 1);
  for (position = 8; position < width; position += 8)
    {
      write_int (w, 2);   /* Record type. */
      write_int (w, -1);  /* Width. */
      write_int (w, 0);   /* No variable label. */
      write_int (w, 0);   /* No missing values. */
      write_int (w, 0);   /* Print format. */
      write_int (w, 0);   /* Write format. */
      write_zeros (w, 8);   /* Name. */
    }
}

/* Write the variable record(s) for variable V to system file
   W. */
static void
write_variable (struct sfm_writer *w, const struct variable *v)
{
  int width = var_get_width (v);
  int segment_cnt = sfm_width_to_segments (width);
  int seg0_width = sfm_segment_alloc_width (width, 0);
  const char *encoding = var_get_encoding (v);
  int i;

  /* Record type. */
  write_int (w, 2);

  /* Width. */
  write_int (w, seg0_width);

  /* Variable has a variable label? */
  write_int (w, var_has_label (v));

  /* Number of missing values.  If there is a range, then the
     range counts as 2 missing values and causes the number to be
     negated.

     Missing values for long string variables are written in a separate
     record. */
  if (width <= MAX_SHORT_STRING)
    {
      const struct missing_values *mv = var_get_missing_values (v);
      if (mv_has_range (mv))
        write_int (w, -2 - mv_n_values (mv));
      else
        write_int (w, mv_n_values (mv));
    }
  else
    write_int (w, 0);

  /* Print and write formats. */
  write_format (w, *var_get_print_format (v), seg0_width);
  write_format (w, *var_get_write_format (v), seg0_width);

  /* Short name.
     The full name is in a translation table written
     separately. */
  write_utf8_string (w, encoding, var_get_short_name (v, 0), 8);

  /* Value label. */
  if (var_has_label (v))
    {
      char *label = recode_string (encoding, UTF8, var_get_label (v), -1);
      size_t label_len = MIN (strlen (label), 255);
      size_t padded_len = ROUND_UP (label_len, 4);
      write_int (w, label_len);
      write_string (w, label, padded_len);
      free (label);
    }

  /* Write the missing values, if any, range first. */
  if (width <= MAX_SHORT_STRING)
    {
      const struct missing_values *mv = var_get_missing_values (v);
      if (mv_has_range (mv))
        {
          double x, y;
          mv_get_range (mv, &x, &y);
          write_float (w, x);
          write_float (w, y);
        }
      for (i = 0; i < mv_n_values (mv); i++)
        write_value (w, mv_get_value (mv, i), width);
    }

  write_variable_continuation_records (w, seg0_width);

  /* Write additional segments for very long string variables. */
  for (i = 1; i < segment_cnt; i++)
    {
      int seg_width = sfm_segment_alloc_width (width, i);
      struct fmt_spec fmt = fmt_for_output (FMT_A, MAX (seg_width, 1), 0);

      write_int (w, 2);           /* Variable record. */
      write_int (w, seg_width);   /* Width. */
      write_int (w, 0);           /* No variable label. */
      write_int (w, 0);           /* No missing values. */
      write_format (w, fmt, seg_width); /* Print format. */
      write_format (w, fmt, seg_width); /* Write format. */
      write_utf8_string (w, encoding, var_get_short_name (v, i), 8);

      write_variable_continuation_records (w, seg_width);
    }
}

/* Writes the value labels to system file W.

   Value labels for long string variables are written separately,
   by write_long_string_value_labels. */
static void
write_value_labels (struct sfm_writer *w, const struct dictionary *d)
{
  struct label_set
    {
      struct hmap_node hmap_node;
      const struct val_labs *val_labs;
      int *indexes;
      size_t n_indexes, allocated_indexes;
    };

  size_t n_sets, allocated_sets;
  struct label_set **sets;
  struct hmap same_sets;
  size_t i;
  int idx;

  n_sets = allocated_sets = 0;
  sets = NULL;
  hmap_init (&same_sets);

  idx = 0;
  for (i = 0; i < dict_get_var_cnt (d); i++)
    {
      struct variable *v = dict_get_var (d, i);

      if (var_has_value_labels (v) && var_get_width (v) <= 8)
        {
          const struct val_labs *val_labs = var_get_value_labels (v);
          unsigned int hash = val_labs_hash (val_labs, 0);
          struct label_set *set;

          HMAP_FOR_EACH_WITH_HASH (set, struct label_set, hmap_node,
                                   hash, &same_sets)
            {
              if (val_labs_equal (set->val_labs, val_labs))
                {
                  if (set->n_indexes >= set->allocated_indexes)
                    set->indexes = x2nrealloc (set->indexes,
                                               &set->allocated_indexes,
                                               sizeof *set->indexes);
                  set->indexes[set->n_indexes++] = idx;
                  goto next_var;
                }
            }

          set = xmalloc (sizeof *set);
          set->val_labs = val_labs;
          set->indexes = xmalloc (sizeof *set->indexes);
          set->indexes[0] = idx;
          set->n_indexes = 1;
          set->allocated_indexes = 1;
          hmap_insert (&same_sets, &set->hmap_node, hash);

          if (n_sets >= allocated_sets)
            sets = x2nrealloc (sets, &allocated_sets, sizeof *sets);
          sets[n_sets++] = set;
        }

    next_var:
      idx += sfm_width_to_octs (var_get_width (v));
    }

  for (i = 0; i < n_sets; i++)
    {
      const struct label_set *set = sets[i];
      const struct val_labs *val_labs = set->val_labs;
      size_t n_labels = val_labs_count (val_labs);
      int width = val_labs_get_width (val_labs);
      const struct val_lab **labels;
      size_t j;

      /* Value label record. */
      write_int (w, 3);             /* Record type. */
      write_int (w, n_labels);
      labels = val_labs_sorted (val_labs);
      for (j = 0; j < n_labels; j++)
        {
          const struct val_lab *vl = labels[j];
          char *label = recode_string (dict_get_encoding (d), UTF8,
                                       val_lab_get_escaped_label (vl), -1);
          uint8_t len = MIN (strlen (label), 255);

          write_value (w, val_lab_get_value (vl), width);
          write_bytes (w, &len, 1);
          write_bytes (w, label, len);
          write_zeros (w, REM_RND_UP (len + 1, 8));
          free (label);
        }
      free (labels);

      /* Value label variable record. */
      write_int (w, 4);              /* Record type. */
      write_int (w, set->n_indexes);
      for (j = 0; j < set->n_indexes; j++)
        write_int (w, set->indexes[j] + 1);
    }

  for (i = 0; i < n_sets; i++)
    {
      struct label_set *set = sets[i];

      free (set->indexes);
      free (set);
    }
  free (sets);
  hmap_destroy (&same_sets);
}

/* Writes record type 6, document record. */
static void
write_documents (struct sfm_writer *w, const struct dictionary *d)
{
  const struct string_array *docs = dict_get_documents (d);
  const char *enc = dict_get_encoding (d);
  size_t i;

  write_int (w, 6);             /* Record type. */
  write_int (w, docs->n);
  for (i = 0; i < docs->n; i++)
    {
      char *s = recode_string (enc, "UTF-8", docs->strings[i], -1);
      size_t s_len = strlen (s);
      size_t write_len = MIN (s_len, DOC_LINE_LENGTH);

      write_bytes (w, s, write_len);
      write_spaces (w, DOC_LINE_LENGTH - write_len);
      free (s);
    }
}

static void
put_attrset (struct string *string, const struct attrset *attrs)
{
  const struct attribute *attr;
  struct attrset_iterator i;

  for (attr = attrset_first (attrs, &i); attr != NULL;
       attr = attrset_next (attrs, &i)) 
    {
      size_t n_values = attribute_get_n_values (attr);
      size_t j;

      ds_put_cstr (string, attribute_get_name (attr));
      ds_put_byte (string, '(');
      for (j = 0; j < n_values; j++) 
        ds_put_format (string, "'%s'\n", attribute_get_value (attr, j));
      ds_put_byte (string, ')');
    }
}

static void
write_data_file_attributes (struct sfm_writer *w,
                            const struct dictionary *d)
{
  struct string s = DS_EMPTY_INITIALIZER;
  put_attrset (&s, dict_get_attributes (d));
  write_utf8_record (w, dict_get_encoding (d), &s, 17);
  ds_destroy (&s);
}

static void
add_role_attribute (enum var_role role, struct attrset *attrs)
{
  struct attribute *attr;
  const char *s;

  switch (role)
    {
    case ROLE_INPUT:
    default:
      s = "0";
      break;

    case ROLE_OUTPUT:
      s = "1";
      break;

    case ROLE_BOTH:
      s = "2";
      break;

    case ROLE_NONE:
      s = "3";
      break;

    case ROLE_PARTITION:
      s = "4";
      break;

    case ROLE_SPLIT:
      s = "5";
      break;
    }
  attrset_delete (attrs, "$@Role");

  attr = attribute_create ("$@Role");
  attribute_add_value (attr, s);
  attrset_add (attrs, attr);
}

static void
write_variable_attributes (struct sfm_writer *w, const struct dictionary *d)
{
  struct string s = DS_EMPTY_INITIALIZER;
  size_t n_vars = dict_get_var_cnt (d);
  size_t n_attrsets = 0;
  size_t i;

  for (i = 0; i < n_vars; i++)
    { 
      struct variable *v = dict_get_var (d, i);
      struct attrset attrs;

      attrset_clone (&attrs, var_get_attributes (v));

      add_role_attribute (var_get_role (v), &attrs);
      if (n_attrsets++)
        ds_put_byte (&s, '/');
      ds_put_format (&s, "%s:", var_get_name (v));
      put_attrset (&s, &attrs);
      attrset_destroy (&attrs);
    }
  if (n_attrsets)
    write_utf8_record (w, dict_get_encoding (d), &s, 18);
  ds_destroy (&s);
}

/* Write multiple response sets.  If PRE_V14 is true, writes sets supported by
   SPSS before release 14, otherwise writes sets supported only by later
   versions. */
static void
write_mrsets (struct sfm_writer *w, const struct dictionary *dict,
              bool pre_v14)
{
  const char *encoding = dict_get_encoding (dict);
  struct string s = DS_EMPTY_INITIALIZER;
  size_t n_mrsets;
  size_t i;

  if (is_encoding_ebcdic_compatible (encoding))
    {
      /* FIXME. */
      return;
    }

  n_mrsets = dict_get_n_mrsets (dict);
  if (n_mrsets == 0)
    return;

  for (i = 0; i < n_mrsets; i++)
    {
      const struct mrset *mrset = dict_get_mrset (dict, i);
      char *name;
      size_t j;

      if ((mrset->type != MRSET_MD || mrset->cat_source != MRSET_COUNTEDVALUES)
          != pre_v14)
        continue;

      name = recode_string (encoding, "UTF-8", mrset->name, -1);
      ds_put_format (&s, "%s=", name);
      free (name);

      if (mrset->type == MRSET_MD)
        {
          char *counted;

          if (mrset->cat_source == MRSET_COUNTEDVALUES)
            ds_put_format (&s, "E %d ", mrset->label_from_var_label ? 11 : 1);
          else
            ds_put_byte (&s, 'D');

          if (mrset->width == 0)
            counted = xasprintf ("%.0f", mrset->counted.f);
          else
            counted = xmemdup0 (value_str (&mrset->counted, mrset->width),
                                mrset->width);
          ds_put_format (&s, "%zu %s", strlen (counted), counted);
          free (counted);
        }
      else
        ds_put_byte (&s, 'C');
      ds_put_byte (&s, ' ');

      if (mrset->label && !mrset->label_from_var_label)
        {
          char *label = recode_string (encoding, "UTF-8", mrset->label, -1);
          ds_put_format (&s, "%zu %s", strlen (label), label);
          free (label);
        }
      else
        ds_put_cstr (&s, "0 ");

      for (j = 0; j < mrset->n_vars; j++)
        {
          const char *short_name_utf8 = var_get_short_name (mrset->vars[j], 0);
          char *lower_name_utf8 = utf8_to_lower (short_name_utf8);
          char *short_name = recode_string (encoding, "UTF-8",
                                            lower_name_utf8, -1);
          ds_put_format (&s, " %s", short_name);
          free (short_name);
          free (lower_name_utf8);
        }
      ds_put_byte (&s, '\n');
    }

  if (!ds_is_empty (&s))
    write_string_record (w, ds_ss (&s), pre_v14 ? 7 : 19);
  ds_destroy (&s);
}

/* Write the alignment, width and scale values. */
static void
write_variable_display_parameters (struct sfm_writer *w,
				   const struct dictionary *dict)
{
  int i;

  write_int (w, 7);             /* Record type. */
  write_int (w, 11);            /* Record subtype. */
  write_int (w, 4);             /* Data item (int32) size. */
  write_int (w, w->segment_cnt * 3); /* Number of data items. */

  for (i = 0; i < dict_get_var_cnt (dict); ++i)
    {
      struct variable *v = dict_get_var (dict, i);
      int width = var_get_width (v);
      int segment_cnt = sfm_width_to_segments (width);
      int measure = (var_get_measure (v) == MEASURE_NOMINAL ? 1
                     : var_get_measure (v) == MEASURE_ORDINAL ? 2
                     : 3);
      int alignment = (var_get_alignment (v) == ALIGN_LEFT ? 0
                       : var_get_alignment (v) == ALIGN_RIGHT ? 1
                       : 2);
      int i;

      for (i = 0; i < segment_cnt; i++)
        {
          int width_left = width - sfm_segment_effective_offset (width, i);
          write_int (w, measure);
          write_int (w, (i == 0 ? var_get_display_width (v)
                         : var_default_display_width (width_left)));
          write_int (w, alignment);
        }
    }
}

/* Writes the table of lengths for very long string variables. */
static void
write_vls_length_table (struct sfm_writer *w,
			const struct dictionary *dict)
{
  struct string map;
  int i;

  ds_init_empty (&map);
  for (i = 0; i < dict_get_var_cnt (dict); ++i)
    {
      const struct variable *v = dict_get_var (dict, i);
      if (sfm_width_to_segments (var_get_width (v)) > 1)
        ds_put_format (&map, "%s=%05d%c\t",
                       var_get_short_name (v, 0), var_get_width (v), 0);
    }
  if (!ds_is_empty (&map))
    write_utf8_record (w, dict_get_encoding (dict), &map, 14);
  ds_destroy (&map);
}

static void
write_long_string_value_labels (struct sfm_writer *w,
                                const struct dictionary *dict)
{
  const char *encoding = dict_get_encoding (dict);
  size_t n_vars = dict_get_var_cnt (dict);
  size_t size, i;
  off_t start UNUSED;

  /* Figure out the size in advance. */
  size = 0;
  for (i = 0; i < n_vars; i++)
    {
      struct variable *var = dict_get_var (dict, i);
      const struct val_labs *val_labs = var_get_value_labels (var);
      int width = var_get_width (var);
      const struct val_lab *val_lab;

      if (val_labs_count (val_labs) == 0 || width < 9)
        continue;

      size += 12;
      size += recode_string_len (encoding, "UTF-8", var_get_name (var), -1);
      for (val_lab = val_labs_first (val_labs); val_lab != NULL;
           val_lab = val_labs_next (val_labs, val_lab))
        {
          size += 8 + width;
          size += recode_string_len (encoding, "UTF-8",
                                     val_lab_get_escaped_label (val_lab), -1);
        }
    }
  if (size == 0)
    return;

  write_int (w, 7);             /* Record type. */
  write_int (w, 21);            /* Record subtype */
  write_int (w, 1);             /* Data item (byte) size. */
  write_int (w, size);          /* Number of data items. */

  start = ftello (w->file);
  for (i = 0; i < n_vars; i++)
    {
      struct variable *var = dict_get_var (dict, i);
      const struct val_labs *val_labs = var_get_value_labels (var);
      int width = var_get_width (var);
      const struct val_lab *val_lab;
      char *var_name;

      if (val_labs_count (val_labs) == 0 || width < 9)
        continue;

      var_name = recode_string (encoding, "UTF-8", var_get_name (var), -1);
      write_int (w, strlen (var_name));
      write_bytes (w, var_name, strlen (var_name));
      free (var_name);

      write_int (w, width);
      write_int (w, val_labs_count (val_labs));
      for (val_lab = val_labs_first (val_labs); val_lab != NULL;
           val_lab = val_labs_next (val_labs, val_lab))
        {
          char *label;
          size_t len;

          write_int (w, width);
          write_bytes (w, value_str (val_lab_get_value (val_lab), width),
                       width);

          label = recode_string (var_get_encoding (var), "UTF-8",
                                 val_lab_get_escaped_label (val_lab), -1);
          len = strlen (label);
          write_int (w, len);
          write_bytes (w, label, len);
          free (label);
        }
    }
  assert (ftello (w->file) == start + size);
}

static void
write_long_string_missing_values (struct sfm_writer *w,
                                  const struct dictionary *dict)
{
  const char *encoding = dict_get_encoding (dict);
  size_t n_vars = dict_get_var_cnt (dict);
  size_t size, i;
  off_t start UNUSED;

  /* Figure out the size in advance. */
  size = 0;
  for (i = 0; i < n_vars; i++)
    {
      struct variable *var = dict_get_var (dict, i);
      const struct missing_values *mv = var_get_missing_values (var);
      int width = var_get_width (var);

      if (mv_is_empty (mv) || width < 9)
        continue;

      size += 4;
      size += recode_string_len (encoding, "UTF-8", var_get_name (var), -1);
      size += 1;
      size += mv_n_values (mv) * (4 + 8);
    }
  if (size == 0)
    return;

  write_int (w, 7);             /* Record type. */
  write_int (w, 22);            /* Record subtype */
  write_int (w, 1);             /* Data item (byte) size. */
  write_int (w, size);          /* Number of data items. */

  start = ftello (w->file);
  for (i = 0; i < n_vars; i++)
    {
      struct variable *var = dict_get_var (dict, i);
      const struct missing_values *mv = var_get_missing_values (var);
      int width = var_get_width (var);
      uint8_t n_missing_values;
      char *var_name;
      int j;

      if (mv_is_empty (mv) || width < 9)
        continue;

      var_name = recode_string (encoding, "UTF-8", var_get_name (var), -1);
      write_int (w, strlen (var_name));
      write_bytes (w, var_name, strlen (var_name));
      free (var_name);

      n_missing_values = mv_n_values (mv);
      write_bytes (w, &n_missing_values, 1);

      for (j = 0; j < n_missing_values; j++)
        {
          const union value *value = mv_get_value (mv, j);

          write_int (w, 8);
          write_bytes (w, value_str (value, width), 8);
        }
    }
  assert (ftello (w->file) == start + size);
}

static void
write_encoding_record (struct sfm_writer *w,
		       const struct dictionary *d)
{
  /* IANA says "...character set names may be up to 40 characters taken
     from the printable characters of US-ASCII," so character set names
     don't need to be recoded to be in UTF-8.

     We convert encoding names to uppercase because SPSS writes encoding
     names in uppercase. */
  char *encoding = xstrdup (dict_get_encoding (d));
  str_uppercase (encoding);
  write_string_record (w, ss_cstr (encoding), 20);
  free (encoding);
}

/* Writes the long variable name table. */
static void
write_longvar_table (struct sfm_writer *w, const struct dictionary *dict)
{
  struct string map;
  size_t i;

  ds_init_empty (&map);
  for (i = 0; i < dict_get_var_cnt (dict); i++)
    {
      struct variable *v = dict_get_var (dict, i);
      if (i)
        ds_put_byte (&map, '\t');
      ds_put_format (&map, "%s=%s",
                     var_get_short_name (v, 0), var_get_name (v));
    }
  write_utf8_record (w, dict_get_encoding (dict), &map, 13);
  ds_destroy (&map);
}

/* Write integer information record. */
static void
write_integer_info_record (struct sfm_writer *w,
                           const struct dictionary *d)
{
  const char *dict_encoding = dict_get_encoding (d);
  int version_component[3];
  int float_format;
  int codepage;

  /* Parse the version string. */
  memset (version_component, 0, sizeof version_component);
  sscanf (bare_version, "%d.%d.%d",
          &version_component[0], &version_component[1], &version_component[2]);

  /* Figure out the floating-point format. */
  if (FLOAT_NATIVE_64_BIT == FLOAT_IEEE_DOUBLE_LE
      || FLOAT_NATIVE_64_BIT == FLOAT_IEEE_DOUBLE_BE)
    float_format = 1;
  else if (FLOAT_NATIVE_64_BIT == FLOAT_Z_LONG)
    float_format = 2;
  else if (FLOAT_NATIVE_64_BIT == FLOAT_VAX_D)
    float_format = 3;
  else
    abort ();

  /* Choose codepage. */
  codepage = sys_get_codepage_from_encoding (dict_encoding);
  if (codepage == 0)
    {
      /* The codepage is unknown.  Choose a default.

         For an EBCDIC-compatible encoding, use the value for EBCDIC.

         For an ASCII-compatible encoding, default to "7-bit ASCII", because
         many files use this codepage number regardless of their actual
         encoding.
      */
      if (is_encoding_ascii_compatible (dict_encoding))
        codepage = 2;
      else if (is_encoding_ebcdic_compatible (dict_encoding))
        codepage = 1;
    }

  /* Write record. */
  write_int (w, 7);             /* Record type. */
  write_int (w, 3);             /* Record subtype. */
  write_int (w, 4);             /* Data item (int32) size. */
  write_int (w, 8);             /* Number of data items. */
  write_int (w, version_component[0]);
  write_int (w, version_component[1]);
  write_int (w, version_component[2]);
  write_int (w, -1);          /* Machine code. */
  write_int (w, float_format);
  write_int (w, 1);           /* Compression code. */
  write_int (w, INTEGER_NATIVE == INTEGER_MSB_FIRST ? 1 : 2);
  write_int (w, codepage);
}

/* Write floating-point information record. */
static void
write_float_info_record (struct sfm_writer *w)
{
  write_int (w, 7);             /* Record type. */
  write_int (w, 4);             /* Record subtype. */
  write_int (w, 8);             /* Data item (flt64) size. */
  write_int (w, 3);             /* Number of data items. */
  write_float (w, SYSMIS);      /* System-missing value. */
  write_float (w, HIGHEST);     /* Value used for HIGHEST in missing values. */
  write_float (w, LOWEST);      /* Value used for LOWEST in missing values. */
}

/* Writes case C to system file W. */
static void
sys_file_casewriter_write (struct casewriter *writer, void *w_,
                           struct ccase *c)
{
  struct sfm_writer *w = w_;

  if (ferror (w->file))
    {
      casewriter_force_error (writer);
      case_unref (c);
      return;
    }

  w->case_cnt++;

  if (!w->compress)
    write_case_uncompressed (w, c);
  else
    write_case_compressed (w, c);

  case_unref (c);
}

/* Destroys system file writer W. */
static void
sys_file_casewriter_destroy (struct casewriter *writer, void *w_)
{
  struct sfm_writer *w = w_;
  if (!close_writer (w))
    casewriter_force_error (writer);
}

/* Returns true if an I/O error has occurred on WRITER, false otherwise. */
static bool
write_error (const struct sfm_writer *writer)
{
  return ferror (writer->file);
}

/* Closes a system file after we're done with it.
   Returns true if successful, false if an I/O error occurred. */
static bool
close_writer (struct sfm_writer *w)
{
  bool ok;

  if (w == NULL)
    return true;

  ok = true;
  if (w->file != NULL)
    {
      /* Flush buffer. */
      if (w->opcode_cnt > 0)
        flush_compressed (w);
      fflush (w->file);

      ok = !write_error (w);

      /* Seek back to the beginning and update the number of cases.
         This is just a courtesy to later readers, so there's no need
         to check return values or report errors. */
      if (ok && w->case_cnt <= INT32_MAX && !fseeko (w->file, 80, SEEK_SET))
        {
          write_int (w, w->case_cnt);
          clearerr (w->file);
        }

      if (fclose (w->file) == EOF)
        ok = false;

      if (!ok)
        msg (ME, _("An I/O error occurred writing system file `%s'."),
             fh_get_file_name (w->fh));

      if (ok ? !replace_file_commit (w->rf) : !replace_file_abort (w->rf))
        ok = false;
    }

  fh_unlock (w->lock);
  fh_unref (w->fh);

  free (w->sfm_vars);
  free (w);

  return ok;
}

/* System file writer casewriter class. */
static const struct casewriter_class sys_file_casewriter_class =
  {
    sys_file_casewriter_write,
    sys_file_casewriter_destroy,
    NULL,
  };

/* Writes case C to system file W, without compressing it. */
static void
write_case_uncompressed (struct sfm_writer *w, const struct ccase *c)
{
  size_t i;

  for (i = 0; i < w->sfm_var_cnt; i++)
    {
      struct sfm_var *v = &w->sfm_vars[i];

      if (v->var_width == 0)
        write_float (w, case_num_idx (c, v->case_index));
      else
        {
          write_bytes (w, case_str_idx (c, v->case_index) + v->offset,
                       v->segment_width);
          write_spaces (w, v->padding);
        }
    }
}

/* Writes case C to system file W, with compression. */
static void
write_case_compressed (struct sfm_writer *w, const struct ccase *c)
{
  size_t i;

  for (i = 0; i < w->sfm_var_cnt; i++)
    {
      struct sfm_var *v = &w->sfm_vars[i];

      if (v->var_width == 0)
        {
          double d = case_num_idx (c, v->case_index);
          if (d == SYSMIS)
            put_cmp_opcode (w, 255);
          else if (d >= 1 - COMPRESSION_BIAS
                   && d <= 251 - COMPRESSION_BIAS
                   && d == (int) d)
            put_cmp_opcode (w, (int) d + COMPRESSION_BIAS);
          else
            {
              put_cmp_opcode (w, 253);
              put_cmp_number (w, d);
            }
        }
      else
        {
          int offset = v->offset;
          int width, padding;

          /* This code properly deals with a width that is not a
             multiple of 8, by ensuring that the final partial
             oct (8 byte unit) is treated as padded with spaces
             on the right. */
          for (width = v->segment_width; width > 0; width -= 8, offset += 8)
            {
              const void *data = case_str_idx (c, v->case_index) + offset;
              int chunk_size = MIN (width, 8);
              if (!memcmp (data, "        ", chunk_size))
                put_cmp_opcode (w, 254);
              else
                {
                  put_cmp_opcode (w, 253);
                  put_cmp_string (w, data, chunk_size);
                }
            }

          /* This code deals properly with padding that is not a
             multiple of 8 bytes, by discarding the remainder,
             which was already effectively padded with spaces in
             the previous loop.  (Note that v->width + v->padding
             is always a multiple of 8.) */
          for (padding = v->padding / 8; padding > 0; padding--)
            put_cmp_opcode (w, 254);
        }
    }
}

/* Flushes buffered compressed opcodes and data to W.
   The compression buffer must not be empty. */
static void
flush_compressed (struct sfm_writer *w)
{
  assert (w->opcode_cnt > 0 && w->opcode_cnt <= 8);

  write_bytes (w, w->opcodes, w->opcode_cnt);
  write_zeros (w, 8 - w->opcode_cnt);

  write_bytes (w, w->data, w->data_cnt * sizeof *w->data);

  w->opcode_cnt = w->data_cnt = 0;
}

/* Appends OPCODE to the buffered set of compression opcodes in
   W.  Flushes the compression buffer beforehand if necessary. */
static void
put_cmp_opcode (struct sfm_writer *w, uint8_t opcode)
{
  if (w->opcode_cnt >= 8)
    flush_compressed (w);

  w->opcodes[w->opcode_cnt++] = opcode;
}

/* Appends NUMBER to the buffered compression data in W.  The
   buffer must not be full; the way to assure that is to call
   this function only just after a call to put_cmp_opcode, which
   will flush the buffer as necessary. */
static void
put_cmp_number (struct sfm_writer *w, double number)
{
  assert (w->opcode_cnt > 0);
  assert (w->data_cnt < 8);

  convert_double_to_output_format (number, w->data[w->data_cnt++]);
}

/* Appends SIZE bytes of DATA to the buffered compression data in
   W, followed by enough spaces to pad the output data to exactly
   8 bytes (thus, SIZE must be no greater than 8).  The buffer
   must not be full; the way to assure that is to call this
   function only just after a call to put_cmp_opcode, which will
   flush the buffer as necessary. */
static void
put_cmp_string (struct sfm_writer *w, const void *data, size_t size)
{
  assert (w->opcode_cnt > 0);
  assert (w->data_cnt < 8);
  assert (size <= 8);

  memset (w->data[w->data_cnt], w->space, 8);
  memcpy (w->data[w->data_cnt], data, size);
  w->data_cnt++;
}

/* Writes 32-bit integer X to the output file for writer W. */
static void
write_int (struct sfm_writer *w, int32_t x)
{
  write_bytes (w, &x, sizeof x);
}

/* Converts NATIVE to the 64-bit format used in output files in
   OUTPUT. */
static inline void
convert_double_to_output_format (double native, uint8_t output[8])
{
  /* If "double" is not a 64-bit type, then convert it to a
     64-bit type.  Otherwise just copy it. */
  if (FLOAT_NATIVE_DOUBLE != FLOAT_NATIVE_64_BIT)
    float_convert (FLOAT_NATIVE_DOUBLE, &native, FLOAT_NATIVE_64_BIT, output);
  else
    memcpy (output, &native, sizeof native);
}

/* Writes floating-point number X to the output file for writer
   W. */
static void
write_float (struct sfm_writer *w, double x)
{
  uint8_t output[8];
  convert_double_to_output_format (x, output);
  write_bytes (w, output, sizeof output);
}

/* Writes contents of VALUE with the given WIDTH to W, padding
   with zeros to a multiple of 8 bytes.
   To avoid a branch, and because we don't actually need to
   support it, WIDTH must be no bigger than 8. */
static void
write_value (struct sfm_writer *w, const union value *value, int width)
{
  assert (width <= 8);
  if (width == 0)
    write_float (w, value->f);
  else
    {
      write_bytes (w, value_str (value, width), width);
      write_zeros (w, 8 - width);
    }
}

/* Writes null-terminated STRING in a field of the given WIDTH to W.  If STRING
   is longer than WIDTH, it is truncated; if STRING is shorter than WIDTH, it
   is padded on the right with spaces. */
static void
write_string (struct sfm_writer *w, const char *string, size_t width)
{
  size_t data_bytes = MIN (strlen (string), width);
  size_t pad_bytes = width - data_bytes;
  write_bytes (w, string, data_bytes);
  while (pad_bytes-- > 0)
    putc (w->space, w->file);
}

/* Recodes null-terminated UTF-8 encoded STRING into ENCODING, and writes the
   recoded version in a field of the given WIDTH to W.  The string is truncated
   or padded on the right with spaces to exactly WIDTH bytes. */
static void
write_utf8_string (struct sfm_writer *w, const char *encoding,
                   const char *string, size_t width)
{
  char *s = recode_string (encoding, "UTF-8", string, -1);
  write_string (w, s, width);
  free (s);
}

/* Writes a record with type 7, subtype SUBTYPE that contains CONTENT recoded
   from UTF-8 encoded into ENCODING. */
static void
write_utf8_record (struct sfm_writer *w, const char *encoding,
                   const struct string *content, int subtype)
{
  struct substring s;

  s = recode_substring_pool (encoding, "UTF-8", ds_ss (content), NULL);
  write_string_record (w, s, subtype);
  ss_dealloc (&s);
}

/* Writes a record with type 7, subtype SUBTYPE that contains the string
   CONTENT. */
static void
write_string_record (struct sfm_writer *w,
                     const struct substring content, int subtype)
{
  write_int (w, 7);
  write_int (w, subtype);
  write_int (w, 1);
  write_int (w, ss_length (content));
  write_bytes (w, ss_data (content), ss_length (content));
}

/* Writes SIZE bytes of DATA to W's output file. */
static void
write_bytes (struct sfm_writer *w, const void *data, size_t size)
{
  fwrite (data, 1, size, w->file);
}

/* Writes N zeros to W's output file. */
static void
write_zeros (struct sfm_writer *w, size_t n)
{
  while (n-- > 0)
    putc (0, w->file);
}

/* Writes N spaces to W's output file. */
static void
write_spaces (struct sfm_writer *w, size_t n)
{
  while (n-- > 0)
    putc (w->space, w->file);
}
