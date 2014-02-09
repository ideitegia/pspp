/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-2000, 2006-2007, 2009-2014 Free Software Foundation, Inc.

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

#include "data/sys-file-reader.h"
#include "data/sys-file-private.h"

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <zlib.h>

#include "data/attributes.h"
#include "data/case.h"
#include "data/casereader-provider.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "data/format.h"
#include "data/identifier.h"
#include "data/missing-values.h"
#include "data/mrset.h"
#include "data/short-names.h"
#include "data/value-labels.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/stringi-set.h"

#include "gl/c-strtod.h"
#include "gl/c-ctype.h"
#include "gl/inttostr.h"
#include "gl/localcharset.h"
#include "gl/minmax.h"
#include "gl/unlocked-io.h"
#include "gl/xalloc.h"
#include "gl/xalloc-oversized.h"
#include "gl/xsize.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

enum
  {
    /* subtypes 0-2 unknown */
    EXT_INTEGER       = 3,      /* Machine integer info. */
    EXT_FLOAT         = 4,      /* Machine floating-point info. */
    EXT_VAR_SETS      = 5,      /* Variable sets. */
    EXT_DATE          = 6,      /* DATE. */
    EXT_MRSETS        = 7,      /* Multiple response sets. */
    EXT_DATA_ENTRY    = 8,      /* SPSS Data Entry. */
    /* subtype 9 unknown */
    EXT_PRODUCT_INFO  = 10,     /* Extra product info text. */
    EXT_DISPLAY       = 11,     /* Variable display parameters. */
    /* subtype 12 unknown */
    EXT_LONG_NAMES    = 13,     /* Long variable names. */
    EXT_LONG_STRINGS  = 14,     /* Long strings. */
    /* subtype 15 unknown */
    EXT_NCASES        = 16,     /* Extended number of cases. */
    EXT_FILE_ATTRS    = 17,     /* Data file attributes. */
    EXT_VAR_ATTRS     = 18,     /* Variable attributes. */
    EXT_MRSETS2       = 19,     /* Multiple response sets (extended). */
    EXT_ENCODING      = 20,     /* Character encoding. */
    EXT_LONG_LABELS   = 21,     /* Value labels for long strings. */
    EXT_LONG_MISSING  = 22,     /* Missing values for long strings. */
    EXT_DATAVIEW      = 24      /* "Format properties in dataview table". */
  };

/* Fields from the top-level header record. */
struct sfm_header_record
  {
    char magic[5];              /* First 4 bytes of file, then null. */
    int weight_idx;             /* 0 if unweighted, otherwise a var index. */
    int nominal_case_size;      /* Number of var positions. */

    /* These correspond to the members of struct sfm_file_info or a dictionary
       but in the system file's encoding rather than ASCII. */
    char creation_date[10];	/* "dd mmm yy". */
    char creation_time[9];	/* "hh:mm:ss". */
    char eye_catcher[61];       /* Eye-catcher string, then product name. */
    char file_label[65];        /* File label. */
  };

struct sfm_var_record
  {
    off_t pos;
    int width;
    char name[8];
    int print_format;
    int write_format;
    int missing_value_code;
    uint8_t missing[24];
    char *label;
    struct variable *var;
  };

struct sfm_value_label
  {
    uint8_t value[8];
    char *label;
  };

struct sfm_value_label_record
  {
    off_t pos;
    struct sfm_value_label *labels;
    unsigned int n_labels;

    int *vars;
    unsigned int n_vars;
  };

struct sfm_document_record
  {
    off_t pos;
    char *documents;
    size_t n_lines;
  };

struct sfm_extension_record
  {
    int subtype;                /* Record subtype. */
    off_t pos;                  /* Starting offset in file. */
    size_t size;                /* Size of data elements. */
    size_t count;               /* Number of data elements. */
    void *data;                 /* Contents. */
  };

/* System file reader. */
struct sfm_reader
  {
    /* Resource tracking. */
    struct pool *pool;          /* All system file state. */

    /* File data. */
    struct sfm_read_info info;
    struct sfm_header_record header;
    struct sfm_var_record *vars;
    size_t n_vars;
    struct sfm_value_label_record *labels;
    size_t n_labels;
    struct sfm_document_record *document;
    struct sfm_extension_record *extensions[32];

    /* File state. */
    struct file_handle *fh;     /* File handle. */
    struct fh_lock *lock;       /* Mutual exclusion for file handle. */
    FILE *file;                 /* File stream. */
    off_t pos;                  /* Position in file. */
    bool error;                 /* I/O or corruption error? */
    struct caseproto *proto;    /* Format of output cases. */

    /* File format. */
    enum integer_format integer_format; /* On-disk integer format. */
    enum float_format float_format; /* On-disk floating point format. */
    struct sfm_var *sfm_vars;   /* Variables. */
    size_t sfm_var_cnt;         /* Number of variables. */
    int case_cnt;               /* Number of cases */
    const char *encoding;       /* String encoding. */

    /* Decompression. */
    enum sfm_compression compression;
    double bias;		/* Compression bias, usually 100.0. */
    uint8_t opcodes[8];         /* Current block of opcodes. */
    size_t opcode_idx;          /* Next opcode to interpret, 8 if none left. */
    bool corruption_warning;    /* Warned about possible corruption? */

    /* ZLIB decompression. */
    long long int ztrailer_ofs; /* Offset of ZLIB trailer at end of file. */
#define ZIN_BUF_SIZE  4096
    uint8_t *zin_buf;           /* Inflation input buffer. */
#define ZOUT_BUF_SIZE 16384
    uint8_t *zout_buf;          /* Inflation output buffer. */
    unsigned int zout_end;      /* Number of bytes of data in zout_buf. */
    unsigned int zout_pos;      /* First unconsumed byte in zout_buf. */
    z_stream zstream;           /* ZLIB inflater. */
  };

static const struct casereader_class sys_file_casereader_class;

static struct variable *lookup_var_by_index (struct sfm_reader *, off_t,
                                             const struct sfm_var_record *,
                                             size_t n, int idx);

static void sys_msg (struct sfm_reader *r, off_t, int class,
                     const char *format, va_list args)
     PRINTF_FORMAT (4, 0);
static void sys_warn (struct sfm_reader *, off_t, const char *, ...)
     PRINTF_FORMAT (3, 4);
static void sys_error (struct sfm_reader *, off_t, const char *, ...)
     PRINTF_FORMAT (3, 4);

static bool read_bytes (struct sfm_reader *, void *, size_t)
  WARN_UNUSED_RESULT;
static int try_read_bytes (struct sfm_reader *, void *, size_t)
  WARN_UNUSED_RESULT;
static bool read_int (struct sfm_reader *, int *) WARN_UNUSED_RESULT;
static bool read_uint (struct sfm_reader *, unsigned int *) WARN_UNUSED_RESULT;
static bool read_int64 (struct sfm_reader *, long long int *)
  WARN_UNUSED_RESULT;
static bool read_uint64 (struct sfm_reader *, unsigned long long int *)
  WARN_UNUSED_RESULT;
static bool read_string (struct sfm_reader *, char *, size_t)
  WARN_UNUSED_RESULT;
static bool skip_bytes (struct sfm_reader *, size_t) WARN_UNUSED_RESULT;

/* ZLIB compressed data handling. */
static bool read_zheader (struct sfm_reader *) WARN_UNUSED_RESULT;
static bool open_zstream (struct sfm_reader *) WARN_UNUSED_RESULT;
static bool close_zstream (struct sfm_reader *) WARN_UNUSED_RESULT;
static int read_bytes_zlib (struct sfm_reader *, void *, size_t)
  WARN_UNUSED_RESULT;
static int read_compressed_bytes (struct sfm_reader *, void *, size_t)
  WARN_UNUSED_RESULT;
static int try_read_compressed_bytes (struct sfm_reader *, void *, size_t)
  WARN_UNUSED_RESULT;
static bool read_compressed_float (struct sfm_reader *, double *)
  WARN_UNUSED_RESULT;

static char *fix_line_ends (const char *);

static int parse_int (const struct sfm_reader *, const void *data, size_t ofs);
static double parse_float (const struct sfm_reader *,
                           const void *data, size_t ofs);

static bool read_variable_record (struct sfm_reader *,
                                  struct sfm_var_record *);
static bool read_value_label_record (struct sfm_reader *,
                                     struct sfm_value_label_record *);
static struct sfm_document_record *read_document_record (struct sfm_reader *);
static bool read_extension_record (struct sfm_reader *, int subtype,
                                   struct sfm_extension_record **);
static bool skip_extension_record (struct sfm_reader *, int subtype);

static struct text_record *open_text_record (
  struct sfm_reader *, const struct sfm_extension_record *,
  bool recode_to_utf8);
static void close_text_record (struct sfm_reader *,
                               struct text_record *);
static bool read_variable_to_value_pair (struct sfm_reader *,
                                         struct dictionary *,
                                         struct text_record *,
                                         struct variable **var, char **value);
static void text_warn (struct sfm_reader *r, struct text_record *text,
                       const char *format, ...)
  PRINTF_FORMAT (3, 4);
static char *text_get_token (struct text_record *,
                             struct substring delimiters, char *delimiter);
static bool text_match (struct text_record *, char c);
static bool text_read_variable_name (struct sfm_reader *, struct dictionary *,
                                     struct text_record *,
                                     struct substring delimiters,
                                     struct variable **);
static bool text_read_short_name (struct sfm_reader *, struct dictionary *,
                                  struct text_record *,
                                  struct substring delimiters,
                                  struct variable **);
static const char *text_parse_counted_string (struct sfm_reader *,
                                              struct text_record *);
static size_t text_pos (const struct text_record *);
static const char *text_get_all (const struct text_record *);

/* Dictionary reader. */

enum which_format
  {
    PRINT_FORMAT,
    WRITE_FORMAT
  };

static bool read_dictionary (struct sfm_reader *);
static bool read_record (struct sfm_reader *, int type,
                         size_t *allocated_vars, size_t *allocated_labels);
static bool read_header (struct sfm_reader *, struct sfm_read_info *,
                         struct sfm_header_record *);
static void parse_header (struct sfm_reader *,
                          const struct sfm_header_record *,
                          struct sfm_read_info *, struct dictionary *);
static bool parse_variable_records (struct sfm_reader *, struct dictionary *,
                                    struct sfm_var_record *, size_t n);
static void parse_format_spec (struct sfm_reader *, off_t pos,
                               unsigned int format, enum which_format,
                               struct variable *, int *format_warning_cnt);
static void parse_document (struct dictionary *, struct sfm_document_record *);
static void parse_display_parameters (struct sfm_reader *,
                                      const struct sfm_extension_record *,
                                      struct dictionary *);
static bool parse_machine_integer_info (struct sfm_reader *,
                                        const struct sfm_extension_record *,
                                        struct sfm_read_info *);
static void parse_machine_float_info (struct sfm_reader *,
                                      const struct sfm_extension_record *);
static void parse_extra_product_info (struct sfm_reader *,
                                      const struct sfm_extension_record *,
                                      struct sfm_read_info *);
static void parse_mrsets (struct sfm_reader *,
                          const struct sfm_extension_record *,
                          struct dictionary *);
static void parse_long_var_name_map (struct sfm_reader *,
                                     const struct sfm_extension_record *,
                                     struct dictionary *);
static bool parse_long_string_map (struct sfm_reader *,
                                   const struct sfm_extension_record *,
                                   struct dictionary *);
static bool parse_value_labels (struct sfm_reader *, struct dictionary *,
                                const struct sfm_var_record *,
                                size_t n_var_recs,
                                const struct sfm_value_label_record *);
static void parse_data_file_attributes (struct sfm_reader *,
                                        const struct sfm_extension_record *,
                                        struct dictionary *);
static void parse_variable_attributes (struct sfm_reader *,
                                       const struct sfm_extension_record *,
                                       struct dictionary *);
static void assign_variable_roles (struct sfm_reader *, struct dictionary *);
static bool parse_long_string_value_labels (struct sfm_reader *,
                                            const struct sfm_extension_record *,
                                            struct dictionary *);
static bool parse_long_string_missing_values (
  struct sfm_reader *, const struct sfm_extension_record *,
  struct dictionary *);

/* Frees the strings inside INFO. */
void
sfm_read_info_destroy (struct sfm_read_info *info)
{
  if (info)
    {
      free (info->creation_date);
      free (info->creation_time);
      free (info->product);
      free (info->product_ext);
    }
}

/* Tries to open FH for reading as a system file.  Returns an sfm_reader if
   successful, otherwise NULL. */
struct sfm_reader *
sfm_open (struct file_handle *fh)
{
  struct sfm_reader *r;

  /* Create and initialize reader. */
  r = xzalloc (sizeof *r);
  r->pool = pool_create ();
  pool_register (r->pool, free, r);
  r->fh = fh_ref (fh);
  r->opcode_idx = sizeof r->opcodes;

  /* TRANSLATORS: this fragment will be interpolated into
     messages in fh_lock() that identify types of files. */
  r->lock = fh_lock (fh, FH_REF_FILE, N_("system file"), FH_ACC_READ, false);
  if (r->lock == NULL)
    goto error;

  r->file = fn_open (fh_get_file_name (fh), "rb");
  if (r->file == NULL)
    {
      msg (ME, _("Error opening `%s' for reading as a system file: %s."),
           fh_get_file_name (r->fh), strerror (errno));
      goto error;
    }

  if (!read_dictionary (r))
    goto error;

  return r;
error:
  sfm_close (r);
  return NULL;
}

static bool
read_dictionary (struct sfm_reader *r)
{
  size_t allocated_vars;
  size_t allocated_labels;

  if (!read_header (r, &r->info, &r->header))
    return false;

  allocated_vars = 0;
  allocated_labels = 0;
  for (;;)
    {
      int type;

      if (!read_int (r, &type))
        return false;
      if (type == 999)
        break;
      if (!read_record (r, type, &allocated_vars, &allocated_labels))
        return false;
    }

  if (!skip_bytes (r, 4))
    return false;

  if (r->compression == SFM_COMP_ZLIB && !read_zheader (r))
    return false;

  return true;
}

static bool
read_record (struct sfm_reader *r, int type,
             size_t *allocated_vars, size_t *allocated_labels)
{
  int subtype;

  switch (type)
    {
    case 2:
      if (r->n_vars >= *allocated_vars)
        r->vars = pool_2nrealloc (r->pool, r->vars, allocated_vars,
                                  sizeof *r->vars);
      return read_variable_record (r, &r->vars[r->n_vars++]);

    case 3:
      if (r->n_labels >= *allocated_labels)
        r->labels = pool_2nrealloc (r->pool, r->labels, allocated_labels,
                                    sizeof *r->labels);
      return read_value_label_record (r, &r->labels[r->n_labels++]);

    case 4:
      /* A Type 4 record is always immediately after a type 3 record,
         so the code for type 3 records reads the type 4 record too. */
      sys_error (r, r->pos, _("Misplaced type 4 record."));
      return false;

    case 6:
      if (r->document != NULL)
        {
          sys_error (r, r->pos, _("Duplicate type 6 (document) record."));
          return false;
        }
      r->document = read_document_record (r);
      return r->document != NULL;

    case 7:
      if (!read_int (r, &subtype))
        return false;
      else if (subtype < 0
               || subtype >= sizeof r->extensions / sizeof *r->extensions)
        {
          sys_warn (r, r->pos,
                    _("Unrecognized record type 7, subtype %d.  Please "
                      "send a copy of this file, and the syntax which "
                      "created it to %s."),
                    subtype, PACKAGE_BUGREPORT);
          return skip_extension_record (r, subtype);
        }
      else if (r->extensions[subtype] != NULL)
        {
          sys_warn (r, r->pos,
                    _("Record type 7, subtype %d found here has the same "
                      "type as the record found near offset 0x%llx.  "
                      "Please send a copy of this file, and the syntax "
                      "which created it to %s."),
                    subtype, (long long int) r->extensions[subtype]->pos,
                    PACKAGE_BUGREPORT);
          return skip_extension_record (r, subtype);
        }
      else
        return read_extension_record (r, subtype, &r->extensions[subtype]);

    default:
      sys_error (r, r->pos, _("Unrecognized record type %d."), type);
      return false;
    }

  NOT_REACHED ();
}

/* Returns the character encoding obtained from R, or a null pointer if R
   doesn't have an indication of its character encoding.  */
const char *
sfm_get_encoding (const struct sfm_reader *r)
{
  /* The EXT_ENCODING record is the best way to determine dictionary
     encoding. */
  if (r->extensions[EXT_ENCODING])
    return r->extensions[EXT_ENCODING]->data;

  /* But EXT_INTEGER is better than nothing as a fallback. */
  if (r->extensions[EXT_INTEGER])
    {
      int codepage = parse_int (r, r->extensions[EXT_INTEGER]->data, 7 * 4);
      const char *encoding;

      switch (codepage)
        {
        case 1:
          return "EBCDIC-US";

        case 2:
        case 3:
          /* These ostensibly mean "7-bit ASCII" and "8-bit ASCII"[sic]
             respectively.  However, many files have character code 2 but data
             which are clearly not ASCII.  Therefore, ignore these values. */
          break;

        case 4:
          return "MS_KANJI";

        default:
          encoding = sys_get_encoding_from_codepage (codepage);
          if (encoding != NULL)
            return encoding;
          break;
        }
    }

  /* If the file magic number is EBCDIC then its character data is too. */
  if (!strcmp (r->header.magic, EBCDIC_MAGIC))
    return "EBCDIC-US";

  return NULL;
}

/* Decodes the dictionary read from R, saving it into into *DICT.  Character
   strings in R are decoded using ENCODING, or an encoding obtained from R if
   ENCODING is null, or the locale encoding if R specifies no encoding.

   If INFOP is non-null, then it receives additional info about the system
   file, which the caller must eventually free with sfm_read_info_destroy()
   when it is no longer needed.

   This function consumes R.  The caller must use it again later, even to
   destroy it with sfm_close(). */
struct casereader *
sfm_decode (struct sfm_reader *r, const char *encoding,
            struct dictionary **dictp, struct sfm_read_info *infop)
{
  struct dictionary *dict;
  size_t i;

  if (encoding == NULL)
    {
      encoding = sfm_get_encoding (r);
      if (encoding == NULL)
        encoding = locale_charset ();
    }

  dict = dict_create (encoding);
  r->encoding = dict_get_encoding (dict);

  /* These records don't use variables at all. */
  if (r->document != NULL)
    parse_document (dict, r->document);

  if (r->extensions[EXT_INTEGER] != NULL
      && !parse_machine_integer_info (r, r->extensions[EXT_INTEGER], &r->info))
    goto error;

  if (r->extensions[EXT_FLOAT] != NULL)
    parse_machine_float_info (r, r->extensions[EXT_FLOAT]);

  if (r->extensions[EXT_PRODUCT_INFO] != NULL)
    parse_extra_product_info (r, r->extensions[EXT_PRODUCT_INFO], &r->info);

  if (r->extensions[EXT_FILE_ATTRS] != NULL)
    parse_data_file_attributes (r, r->extensions[EXT_FILE_ATTRS], dict);

  parse_header (r, &r->header, &r->info, dict);

  /* Parse the variable records, the basis of almost everything else. */
  if (!parse_variable_records (r, dict, r->vars, r->n_vars))
    goto error;

  /* Parse value labels and the weight variable immediately after the variable
     records.  These records use indexes into var_recs[], so we must parse them
     before those indexes become invalidated by very long string variables. */
  for (i = 0; i < r->n_labels; i++)
    if (!parse_value_labels (r, dict, r->vars, r->n_vars, &r->labels[i]))
      goto error;
  if (r->header.weight_idx != 0)
    {
      struct variable *weight_var;

      weight_var = lookup_var_by_index (r, 76, r->vars, r->n_vars,
                                        r->header.weight_idx);
      if (weight_var != NULL)
        {
          if (var_is_numeric (weight_var))
            dict_set_weight (dict, weight_var);
          else
            sys_warn (r, -1, _("Ignoring string variable `%s' set "
                               "as weighting variable."),
                      var_get_name (weight_var));
        }
    }

  if (r->extensions[EXT_DISPLAY] != NULL)
    parse_display_parameters (r, r->extensions[EXT_DISPLAY], dict);

  /* The following records use short names, so they need to be parsed before
     parse_long_var_name_map() changes short names to long names. */
  if (r->extensions[EXT_MRSETS] != NULL)
    parse_mrsets (r, r->extensions[EXT_MRSETS], dict);

  if (r->extensions[EXT_MRSETS2] != NULL)
    parse_mrsets (r, r->extensions[EXT_MRSETS2], dict);

  if (r->extensions[EXT_LONG_STRINGS] != NULL
      && !parse_long_string_map (r, r->extensions[EXT_LONG_STRINGS], dict))
    goto error;

  /* Now rename variables to their long names. */
  parse_long_var_name_map (r, r->extensions[EXT_LONG_NAMES], dict);

  /* The following records use long names, so they need to follow renaming. */
  if (r->extensions[EXT_VAR_ATTRS] != NULL)
    {
      parse_variable_attributes (r, r->extensions[EXT_VAR_ATTRS], dict);

      /* Roles use the $@Role attribute.  */
      assign_variable_roles (r, dict);
    }

  if (r->extensions[EXT_LONG_LABELS] != NULL
      && !parse_long_string_value_labels (r, r->extensions[EXT_LONG_LABELS],
                                          dict))
    goto error;
  if (r->extensions[EXT_LONG_MISSING] != NULL
      && !parse_long_string_missing_values (r, r->extensions[EXT_LONG_MISSING],
                                            dict))
    goto error;

  /* Warn if the actual amount of data per case differs from the
     amount that the header claims.  SPSS version 13 gets this
     wrong when very long strings are involved, so don't warn in
     that case. */
  if (r->header.nominal_case_size != -1
      && r->header.nominal_case_size != r->n_vars
      && r->info.version_major != 13)
    sys_warn (r, -1, _("File header claims %d variable positions but "
                       "%zu were read from file."),
              r->header.nominal_case_size, r->n_vars);

  /* Create an index of dictionary variable widths for
     sfm_read_case to use.  We cannot use the `struct variable's
     from the dictionary we created, because the caller owns the
     dictionary and may destroy or modify its variables. */
  sfm_dictionary_to_sfm_vars (dict, &r->sfm_vars, &r->sfm_var_cnt);
  pool_register (r->pool, free, r->sfm_vars);
  r->proto = caseproto_ref_pool (dict_get_proto (dict), r->pool);

  *dictp = dict;
  if (infop)
    {
      *infop = r->info;
      memset (&r->info, 0, sizeof r->info);
    }

  return casereader_create_sequential
    (NULL, r->proto,
     r->case_cnt == -1 ? CASENUMBER_MAX: r->case_cnt,
                                       &sys_file_casereader_class, r);

error:
  sfm_close (r);
  dict_destroy (dict);
  *dictp = NULL;
  return NULL;
}

/* Closes R, which should have been returned by sfm_open() but not already
   closed with sfm_decode() or this function.
   Returns true if an I/O error has occurred on READER, false
   otherwise. */
bool
sfm_close (struct sfm_reader *r)
{
  bool error;

  if (r == NULL)
    return true;

  if (r->file)
    {
      if (fn_close (fh_get_file_name (r->fh), r->file) == EOF)
        {
          msg (ME, _("Error closing system file `%s': %s."),
               fh_get_file_name (r->fh), strerror (errno));
          r->error = true;
        }
      r->file = NULL;
    }

  sfm_read_info_destroy (&r->info);
  fh_unlock (r->lock);
  fh_unref (r->fh);

  error = r->error;
  pool_destroy (r->pool);

  return !error;
}

/* Destroys READER. */
static void
sys_file_casereader_destroy (struct casereader *reader UNUSED, void *r_)
{
  struct sfm_reader *r = r_;
  sfm_close (r);
}

/* Returns true if FILE is an SPSS system file,
   false otherwise. */
bool
sfm_detect (FILE *file)
{
  char magic[5];

  if (fread (magic, 4, 1, file) != 1)
    return false;
  magic[4] = '\0';

  return (!strcmp (ASCII_MAGIC, magic)
          || !strcmp (ASCII_ZMAGIC, magic)
          || !strcmp (EBCDIC_MAGIC, magic));
}

/* Reads the global header of the system file.  Initializes *HEADER and *INFO,
   except for the string fields in *INFO, which parse_header() will initialize
   later once the file's encoding is known. */
static bool
read_header (struct sfm_reader *r, struct sfm_read_info *info,
             struct sfm_header_record *header)
{
  uint8_t raw_layout_code[4];
  uint8_t raw_bias[8];
  int compressed;
  bool zmagic;

  if (!read_string (r, header->magic, sizeof header->magic)
      || !read_string (r, header->eye_catcher, sizeof header->eye_catcher))
    return false;

  if (!strcmp (ASCII_MAGIC, header->magic)
      || !strcmp (EBCDIC_MAGIC, header->magic))
    zmagic = false;
  else if (!strcmp (ASCII_ZMAGIC, header->magic))
    zmagic = true;
  else
    {
      sys_error (r, 0, _("This is not an SPSS system file."));
      return false;
    }

  /* Identify integer format. */
  if (!read_bytes (r, raw_layout_code, sizeof raw_layout_code))
    return false;
  if ((!integer_identify (2, raw_layout_code, sizeof raw_layout_code,
                          &r->integer_format)
       && !integer_identify (3, raw_layout_code, sizeof raw_layout_code,
                             &r->integer_format))
      || (r->integer_format != INTEGER_MSB_FIRST
          && r->integer_format != INTEGER_LSB_FIRST))
    {
      sys_error (r, 64, _("This is not an SPSS system file."));
      return false;
    }

  if (!read_int (r, &header->nominal_case_size))
    return false;

  if (header->nominal_case_size < 0
      || header->nominal_case_size > INT_MAX / 16)
    header->nominal_case_size = -1;

  if (!read_int (r, &compressed))
    return false;
  if (!zmagic)
    {
      if (compressed == 0)
        r->compression = SFM_COMP_NONE;
      else if (compressed == 1)
        r->compression = SFM_COMP_SIMPLE;
      else if (compressed != 0)
        {
          sys_error (r, 0, "System file header has invalid compression "
                     "value %d.", compressed);
          return false;
        }
    }
  else
    {
      if (compressed == 2)
        r->compression = SFM_COMP_ZLIB;
      else
        {
          sys_error (r, 0, "ZLIB-compressed system file header has invalid "
                     "compression value %d.", compressed);
          return false;
        }
    }

  if (!read_int (r, &header->weight_idx))
    return false;

  if (!read_int (r, &r->case_cnt))
    return false;
  if ( r->case_cnt > INT_MAX / 2)
    r->case_cnt = -1;

  /* Identify floating-point format and obtain compression bias. */
  if (!read_bytes (r, raw_bias, sizeof raw_bias))
    return false;
  if (float_identify (100.0, raw_bias, sizeof raw_bias, &r->float_format) == 0)
    {
      uint8_t zero_bias[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

      if (memcmp (raw_bias, zero_bias, 8))
        sys_warn (r, r->pos - 8,
                  _("Compression bias is not the usual "
                    "value of 100, or system file uses unrecognized "
                    "floating-point format."));
      else
        {
          /* Some software is known to write all-zeros to this
             field.  Such software also writes floating-point
             numbers in the format that we expect by default
             (it seems that all software most likely does, in
             reality), so don't warn in this case. */
        }

      if (r->integer_format == INTEGER_MSB_FIRST)
        r->float_format = FLOAT_IEEE_DOUBLE_BE;
      else
        r->float_format = FLOAT_IEEE_DOUBLE_LE;
    }
  float_convert (r->float_format, raw_bias, FLOAT_NATIVE_DOUBLE, &r->bias);

  if (!read_string (r, header->creation_date, sizeof header->creation_date)
      || !read_string (r, header->creation_time, sizeof header->creation_time)
      || !read_string (r, header->file_label, sizeof header->file_label)
      || !skip_bytes (r, 3))
    return false;

  info->integer_format = r->integer_format;
  info->float_format = r->float_format;
  info->compression = r->compression;
  info->case_cnt = r->case_cnt;

  return true;
}

/* Reads a variable (type 2) record from R into RECORD. */
static bool
read_variable_record (struct sfm_reader *r, struct sfm_var_record *record)
{
  int has_variable_label;

  memset (record, 0, sizeof *record);

  record->pos = r->pos;
  if (!read_int (r, &record->width)
      || !read_int (r, &has_variable_label)
      || !read_int (r, &record->missing_value_code)
      || !read_int (r, &record->print_format)
      || !read_int (r, &record->write_format)
      || !read_bytes (r, record->name, sizeof record->name))
    return false;

  if (has_variable_label == 1)
    {
      enum { MAX_LABEL_LEN = 255 };
      unsigned int len, read_len;

      if (!read_uint (r, &len))
        return false;

      /* Read up to MAX_LABEL_LEN bytes of label. */
      read_len = MIN (MAX_LABEL_LEN, len);
      record->label = pool_malloc (r->pool, read_len + 1);
      if (!read_string (r, record->label, read_len + 1))
        return false;

      /* Skip unread label bytes. */
      if (!skip_bytes (r, len - read_len))
        return false;

      /* Skip label padding up to multiple of 4 bytes. */
      if (!skip_bytes (r, ROUND_UP (len, 4) - len))
        return false;
    }
  else if (has_variable_label != 0)
    {
      sys_error (r, record->pos,
                 _("Variable label indicator field is not 0 or 1."));
      return false;
    }

  /* Set missing values. */
  if (record->missing_value_code != 0)
    {
      int code = record->missing_value_code;
      if (record->width == 0)
        {
          if (code < -3 || code > 3 || code == -1)
            {
              sys_error (r, record->pos,
                         _("Numeric missing value indicator field is not "
                           "-3, -2, 0, 1, 2, or 3."));
              return false;
            }
        }
      else
        {
          if (code < 1 || code > 3)
            {
              sys_error (r, record->pos,
                         _("String missing value indicator field is not "
                           "0, 1, 2, or 3."));
              return false;
            }
        }

      if (!read_bytes (r, record->missing, 8 * abs (code)))
        return false;
    }

  return true;
}

/* Reads value labels from R into RECORD. */
static bool
read_value_label_record (struct sfm_reader *r,
                         struct sfm_value_label_record *record)
{
  size_t i;
  int type;

  /* Read type 3 record. */
  record->pos = r->pos;
  if (!read_uint (r, &record->n_labels))
    return false;
  if (record->n_labels > SIZE_MAX / sizeof *record->labels)
    {
      sys_error (r, r->pos - 4, _("Invalid number of labels %zu."),
                 record->n_labels);
      return false;
    }
  record->labels = pool_nmalloc (r->pool, record->n_labels,
                                 sizeof *record->labels);
  for (i = 0; i < record->n_labels; i++)
    {
      struct sfm_value_label *label = &record->labels[i];
      unsigned char label_len;
      size_t padded_len;

      if (!read_bytes (r, label->value, sizeof label->value))
        return false;

      /* Read label length. */
      if (!read_bytes (r, &label_len, sizeof label_len))
        return false;
      padded_len = ROUND_UP (label_len + 1, 8);

      /* Read label, padding. */
      label->label = pool_malloc (r->pool, padded_len + 1);
      if (!read_bytes (r, label->label, padded_len - 1))
        return false;
      label->label[label_len] = '\0';
    }

  /* Read record type of type 4 record. */
  if (!read_int (r, &type))
    return false;
  if (type != 4)
    {
      sys_error (r, r->pos - 4,
                 _("Variable index record (type 4) does not immediately "
                   "follow value label record (type 3) as it should."));
      return false;
    }

  /* Read number of variables associated with value label from type 4
     record. */
  if (!read_uint (r, &record->n_vars))
    return false;
  if (record->n_vars < 1 || record->n_vars > r->n_vars)
    {
      sys_error (r, r->pos - 4,
                 _("Number of variables associated with a value label (%zu) "
                   "is not between 1 and the number of variables (%zu)."),
                 record->n_vars, r->n_vars);
      return false;
    }

  record->vars = pool_nmalloc (r->pool, record->n_vars, sizeof *record->vars);
  for (i = 0; i < record->n_vars; i++)
    if (!read_int (r, &record->vars[i]))
      return false;

  return true;
}

/* Reads a document record from R and returns it. */
static struct sfm_document_record *
read_document_record (struct sfm_reader *r)
{
  struct sfm_document_record *record;
  int n_lines;

  record = pool_malloc (r->pool, sizeof *record);
  record->pos = r->pos;

  if (!read_int (r, &n_lines))
    return NULL;
  if (n_lines <= 0 || n_lines >= INT_MAX / DOC_LINE_LENGTH)
    {
      sys_error (r, record->pos,
                 _("Number of document lines (%d) "
                   "must be greater than 0 and less than %d."),
                 n_lines, INT_MAX / DOC_LINE_LENGTH);
      return NULL;
    }

  record->n_lines = n_lines;
  record->documents = pool_malloc (r->pool, DOC_LINE_LENGTH * n_lines);
  if (!read_bytes (r, record->documents, DOC_LINE_LENGTH * n_lines))
    return NULL;

  return record;
}

static bool
read_extension_record_header (struct sfm_reader *r, int subtype,
                              struct sfm_extension_record *record)
{
  record->subtype = subtype;
  record->pos = r->pos;
  if (!read_uint (r, &record->size) || !read_uint (r, &record->count))
    return false;

  /* Check that SIZE * COUNT + 1 doesn't overflow.  Adding 1
     allows an extra byte for a null terminator, used by some
     extension processing routines. */
  if (record->size != 0
      && xsum (1, xtimes (record->count, record->size)) >= UINT_MAX)
    {
      sys_error (r, record->pos, "Record type 7 subtype %d too large.",
                 subtype);
      return false;
    }

  return true;
}

/* Reads an extension record from R into RECORD. */
static bool
read_extension_record (struct sfm_reader *r, int subtype,
                       struct sfm_extension_record **recordp)
{
  struct extension_record_type
    {
      int subtype;
      int size;
      int count;
    };

  static const struct extension_record_type types[] =
    {
      /* Implemented record types. */
      { EXT_INTEGER,      4, 8 },
      { EXT_FLOAT,        8, 3 },
      { EXT_MRSETS,       1, 0 },
      { EXT_PRODUCT_INFO, 1, 0 },
      { EXT_DISPLAY,      4, 0 },
      { EXT_LONG_NAMES,   1, 0 },
      { EXT_LONG_STRINGS, 1, 0 },
      { EXT_NCASES,       8, 2 },
      { EXT_FILE_ATTRS,   1, 0 },
      { EXT_VAR_ATTRS,    1, 0 },
      { EXT_MRSETS2,      1, 0 },
      { EXT_ENCODING,     1, 0 },
      { EXT_LONG_LABELS,  1, 0 },
      { EXT_LONG_MISSING, 1, 0 },

      /* Ignored record types. */
      { EXT_VAR_SETS,     0, 0 },
      { EXT_DATE,         0, 0 },
      { EXT_DATA_ENTRY,   0, 0 },
      { EXT_DATAVIEW,     0, 0 },
    };

  const struct extension_record_type *type;
  struct sfm_extension_record *record;
  size_t n_bytes;

  *recordp = NULL;
  record = pool_malloc (r->pool, sizeof *record);
  if (!read_extension_record_header (r, subtype, record))
    return false;
  n_bytes = record->count * record->size;

  for (type = types; type < &types[sizeof types / sizeof *types]; type++)
    if (subtype == type->subtype)
      {
        if (type->size > 0 && record->size != type->size)
          sys_warn (r, record->pos,
                    _("Record type 7, subtype %d has bad size %zu "
                      "(expected %d)."), subtype, record->size, type->size);
        else if (type->count > 0 && record->count != type->count)
          sys_warn (r, record->pos,
                    _("Record type 7, subtype %d has bad count %zu "
                      "(expected %d)."), subtype, record->count, type->count);
        else if (type->count == 0 && type->size == 0)
          {
            /* Ignore this record. */
          }
        else
          {
            char *data = pool_malloc (r->pool, n_bytes + 1);
            data[n_bytes] = '\0';

            record->data = data;
            if (!read_bytes (r, record->data, n_bytes))
              return false;
            *recordp = record;
            return true;
          }

        goto skip;
      }

  sys_warn (r, record->pos,
            _("Unrecognized record type 7, subtype %d.  Please send a "
              "copy of this file, and the syntax which created it to %s."),
            subtype, PACKAGE_BUGREPORT);

skip:
  return skip_bytes (r, n_bytes);
}

static bool
skip_extension_record (struct sfm_reader *r, int subtype)
{
  struct sfm_extension_record record;

  return (read_extension_record_header (r, subtype, &record)
          && skip_bytes (r, record.count * record.size));
}

static void
parse_header (struct sfm_reader *r, const struct sfm_header_record *header,
              struct sfm_read_info *info, struct dictionary *dict)
{
  const char *dict_encoding = dict_get_encoding (dict);
  struct substring product;
  struct substring label;
  char *fixed_label;

  /* Convert file label to UTF-8 and put it into DICT. */
  label = recode_substring_pool ("UTF-8", dict_encoding,
                                 ss_cstr (header->file_label), r->pool);
  ss_trim (&label, ss_cstr (" "));
  label.string[label.length] = '\0';
  fixed_label = fix_line_ends (label.string);
  dict_set_label (dict, fixed_label);
  free (fixed_label);

  /* Put creation date and time in UTF-8 into INFO. */
  info->creation_date = recode_string ("UTF-8", dict_encoding,
                                       header->creation_date, -1);
  info->creation_time = recode_string ("UTF-8", dict_encoding,
                                       header->creation_time, -1);

  /* Put product name into INFO, dropping eye-catcher string if present. */
  product = recode_substring_pool ("UTF-8", dict_encoding,
                                   ss_cstr (header->eye_catcher), r->pool);
  ss_match_string (&product, ss_cstr ("@(#) SPSS DATA FILE"));
  ss_trim (&product, ss_cstr (" "));
  info->product = ss_xstrdup (product);
}

/* Reads a variable (type 2) record from R and adds the
   corresponding variable to DICT.
   Also skips past additional variable records for long string
   variables. */
static bool
parse_variable_records (struct sfm_reader *r, struct dictionary *dict,
                        struct sfm_var_record *var_recs, size_t n_var_recs)
{
  const char *dict_encoding = dict_get_encoding (dict);
  struct sfm_var_record *rec;
  int n_warnings = 0;

  for (rec = var_recs; rec < &var_recs[n_var_recs]; )
    {
      struct variable *var;
      size_t n_values;
      char *name;
      size_t i;

      name = recode_string_pool ("UTF-8", dict_encoding,
                                 rec->name, 8, r->pool);
      name[strcspn (name, " ")] = '\0';

      if (!dict_id_is_valid (dict, name, false)
          || name[0] == '$' || name[0] == '#')
        {
          sys_error (r, rec->pos, _("Invalid variable name `%s'."), name);
          return false;
        }

      if (rec->width < 0 || rec->width > 255)
        {
          sys_error (r, rec->pos,
                     _("Bad width %d for variable %s."), rec->width, name);
          return false;
        }

      var = rec->var = dict_create_var (dict, name, rec->width);
      if (var == NULL)
        {
          char *new_name = dict_make_unique_var_name (dict, NULL, NULL);
          sys_warn (r, rec->pos, _("Renaming variable with duplicate name "
                                   "`%s' to `%s'."),
                    name, new_name);
          var = rec->var = dict_create_var_assert (dict, new_name, rec->width);
          free (new_name);
        }

      /* Set the short name the same as the long name. */
      var_set_short_name (var, 0, name);

      /* Get variable label, if any. */
      if (rec->label)
        {
          char *utf8_label;

          utf8_label = recode_string_pool ("UTF-8", dict_encoding,
                                           rec->label, -1, r->pool);
          var_set_label (var, utf8_label, false);
        }

      /* Set missing values. */
      if (rec->missing_value_code != 0)
        {
          int width = var_get_width (var);
          struct missing_values mv;

          mv_init_pool (r->pool, &mv, width);
          if (var_is_numeric (var))
            {
              bool has_range = rec->missing_value_code < 0;
              int n_discrete = (has_range
                                ? rec->missing_value_code == -3
                                : rec->missing_value_code);
              int ofs = 0;

              if (has_range)
                {
                  double low = parse_float (r, rec->missing, 0);
                  double high = parse_float (r, rec->missing, 8);

                  /* Deal with SPSS 21 change in representation. */
                  if (low == SYSMIS)
                    low = LOWEST;

                  mv_add_range (&mv, low, high);
                  ofs += 16;
                }

              for (i = 0; i < n_discrete; i++)
                {
                  mv_add_num (&mv, parse_float (r, rec->missing, ofs));
                  ofs += 8;
                }
            }
          else
            {
              union value value;

              value_init_pool (r->pool, &value, width);
              value_set_missing (&value, width);
              for (i = 0; i < rec->missing_value_code; i++)
                mv_add_str (&mv, rec->missing + 8 * i, MIN (width, 8));
            }
          var_set_missing_values (var, &mv);
        }

      /* Set formats. */
      parse_format_spec (r, rec->pos + 12, rec->print_format,
                         PRINT_FORMAT, var, &n_warnings);
      parse_format_spec (r, rec->pos + 16, rec->write_format,
                         WRITE_FORMAT, var, &n_warnings);

      /* Account for values.
         Skip long string continuation records, if any. */
      n_values = rec->width == 0 ? 1 : DIV_RND_UP (rec->width, 8);
      for (i = 1; i < n_values; i++)
        if (i + (rec - var_recs) >= n_var_recs || rec[i].width != -1)
          {
            sys_error (r, rec->pos, _("Missing string continuation record."));
            return false;
          }
      rec += n_values;
    }

  return true;
}

/* Translates the format spec from sysfile format to internal
   format. */
static void
parse_format_spec (struct sfm_reader *r, off_t pos, unsigned int format,
                   enum which_format which, struct variable *v,
                   int *n_warnings)
{
  const int max_warnings = 8;
  uint8_t raw_type = format >> 16;
  uint8_t w = format >> 8;
  uint8_t d = format;
  struct fmt_spec f;
  bool ok;

  f.w = w;
  f.d = d;

  msg_disable ();
  ok = (fmt_from_io (raw_type, &f.type)
        && fmt_check_output (&f)
        && fmt_check_width_compat (&f, var_get_width (v)));
  msg_enable ();

  if (ok)
    {
      if (which == PRINT_FORMAT)
        var_set_print_format (v, &f);
      else
        var_set_write_format (v, &f);
    }
  else if (format == 0)
    {
      /* Actually observed in the wild.  No point in warning about it. */
    }
  else if (++*n_warnings <= max_warnings)
    {
      if (which == PRINT_FORMAT)
        sys_warn (r, pos, _("Variable %s with width %d has invalid print "
                            "format 0x%x."),
                  var_get_name (v), var_get_width (v), format);
      else
        sys_warn (r, pos, _("Variable %s with width %d has invalid write "
                            "format 0x%x."),
                  var_get_name (v), var_get_width (v), format);

      if (*n_warnings == max_warnings)
        sys_warn (r, -1, _("Suppressing further invalid format warnings."));
    }
}

static void
parse_document (struct dictionary *dict, struct sfm_document_record *record)
{
  const char *p;

  for (p = record->documents;
       p < record->documents + DOC_LINE_LENGTH * record->n_lines;
       p += DOC_LINE_LENGTH)
    {
      struct substring line;

      line = recode_substring_pool ("UTF-8", dict_get_encoding (dict),
                                    ss_buffer (p, DOC_LINE_LENGTH), NULL);
      ss_rtrim (&line, ss_cstr (" "));
      line.string[line.length] = '\0';

      dict_add_document_line (dict, line.string, false);

      ss_dealloc (&line);
    }
}

/* Parses record type 7, subtype 3. */
static bool
parse_machine_integer_info (struct sfm_reader *r,
                            const struct sfm_extension_record *record,
                            struct sfm_read_info *info)
{
  int float_representation, expected_float_format;
  int integer_representation, expected_integer_format;

  /* Save version info. */
  info->version_major = parse_int (r, record->data, 0);
  info->version_minor = parse_int (r, record->data, 4);
  info->version_revision = parse_int (r, record->data, 8);

  /* Check floating point format. */
  float_representation = parse_int (r, record->data, 16);
  if (r->float_format == FLOAT_IEEE_DOUBLE_BE
      || r->float_format == FLOAT_IEEE_DOUBLE_LE)
    expected_float_format = 1;
  else if (r->float_format == FLOAT_Z_LONG)
    expected_float_format = 2;
  else if (r->float_format == FLOAT_VAX_G || r->float_format == FLOAT_VAX_D)
    expected_float_format = 3;
  else
    NOT_REACHED ();
  if (float_representation != expected_float_format)
    {
      sys_error (r, record->pos,
                 _("Floating-point representation indicated by "
                   "system file (%d) differs from expected (%d)."),
                 float_representation, expected_float_format);
      return false;
    }

  /* Check integer format. */
  integer_representation = parse_int (r, record->data, 24);
  if (r->integer_format == INTEGER_MSB_FIRST)
    expected_integer_format = 1;
  else if (r->integer_format == INTEGER_LSB_FIRST)
    expected_integer_format = 2;
  else
    NOT_REACHED ();
  if (integer_representation != expected_integer_format)
    sys_warn (r, record->pos,
              _("Integer format indicated by system file (%d) "
                "differs from expected (%d)."),
              integer_representation, expected_integer_format);

  return true;
}

/* Parses record type 7, subtype 4. */
static void
parse_machine_float_info (struct sfm_reader *r,
                          const struct sfm_extension_record *record)
{
  double sysmis = parse_float (r, record->data, 0);
  double highest = parse_float (r, record->data, 8);
  double lowest = parse_float (r, record->data, 16);

  if (sysmis != SYSMIS)
    sys_warn (r, record->pos,
              _("File specifies unexpected value %g (%a) as %s, "
                "instead of %g (%a)."),
              sysmis, sysmis, "SYSMIS", SYSMIS, SYSMIS);

  if (highest != HIGHEST)
    sys_warn (r, record->pos,
              _("File specifies unexpected value %g (%a) as %s, "
                "instead of %g (%a)."),
              highest, highest, "HIGHEST", HIGHEST, HIGHEST);

  /* SPSS before version 21 used a unique value just bigger than SYSMIS as
     LOWEST.  SPSS 21 uses SYSMIS for LOWEST, which is OK because LOWEST only
     appears in a context (missing values) where SYSMIS cannot. */
  if (lowest != LOWEST && lowest != SYSMIS)
    sys_warn (r, record->pos,
              _("File specifies unexpected value %g (%a) as %s, "
                "instead of %g (%a) or %g (%a)."),
              lowest, lowest, "LOWEST", LOWEST, LOWEST, SYSMIS, SYSMIS);
}

/* Parses record type 7, subtype 10. */
static void
parse_extra_product_info (struct sfm_reader *r,
                          const struct sfm_extension_record *record,
                          struct sfm_read_info *info)
{
  struct text_record *text;

  text = open_text_record (r, record, true);
  info->product_ext = fix_line_ends (text_get_all (text));
  close_text_record (r, text);
}

/* Parses record type 7, subtype 7 or 19. */
static void
parse_mrsets (struct sfm_reader *r, const struct sfm_extension_record *record,
              struct dictionary *dict)
{
  struct text_record *text;
  struct mrset *mrset;

  text = open_text_record (r, record, false);
  for (;;)
    {
      const char *counted = NULL;
      const char *name;
      const char *label;
      struct stringi_set var_names;
      size_t allocated_vars;
      char delimiter;
      int width;

      /* Skip extra line feeds if present. */
      while (text_match (text, '\n'))
        continue;

      mrset = xzalloc (sizeof *mrset);

      name = text_get_token (text, ss_cstr ("="), NULL);
      if (name == NULL)
        break;
      mrset->name = recode_string ("UTF-8", r->encoding, name, -1);

      if (mrset->name[0] != '$')
        {
          sys_warn (r, record->pos,
                    _("`%s' does not begin with `$' at offset %zu "
                      "in MRSETS record."), mrset->name, text_pos (text));
          break;
        }

      if (text_match (text, 'C'))
        {
          mrset->type = MRSET_MC;
          if (!text_match (text, ' '))
            {
              sys_warn (r, record->pos,
                        _("Missing space following `%c' at offset %zu "
                          "in MRSETS record."), 'C', text_pos (text));
              break;
            }
        }
      else if (text_match (text, 'D'))
        {
          mrset->type = MRSET_MD;
          mrset->cat_source = MRSET_VARLABELS;
        }
      else if (text_match (text, 'E'))
        {
          char *number;

          mrset->type = MRSET_MD;
          mrset->cat_source = MRSET_COUNTEDVALUES;
          if (!text_match (text, ' '))
            {
              sys_warn (r, record->pos,
                        _("Missing space following `%c' at offset %zu "
                          "in MRSETS record."), 'E',  text_pos (text));
              break;
            }

          number = text_get_token (text, ss_cstr (" "), NULL);
          if (!strcmp (number, "11"))
            mrset->label_from_var_label = true;
          else if (strcmp (number, "1"))
            sys_warn (r, record->pos,
                      _("Unexpected label source value `%s' following `E' "
                        "at offset %zu in MRSETS record."),
                      number, text_pos (text));
        }
      else
        {
          sys_warn (r, record->pos,
                    _("Missing `C', `D', or `E' at offset %zu "
                      "in MRSETS record."),
                    text_pos (text));
          break;
        }

      if (mrset->type == MRSET_MD)
        {
          counted = text_parse_counted_string (r, text);
          if (counted == NULL)
            break;
        }

      label = text_parse_counted_string (r, text);
      if (label == NULL)
        break;
      if (label[0] != '\0')
        mrset->label = recode_string ("UTF-8", r->encoding, label, -1);

      stringi_set_init (&var_names);
      allocated_vars = 0;
      width = INT_MAX;
      do
        {
          const char *raw_var_name;
          struct variable *var;
          char *var_name;

          raw_var_name = text_get_token (text, ss_cstr (" \n"), &delimiter);
          if (raw_var_name == NULL)
            {
              if (delimiter != '\n')
                sys_warn (r, record->pos,
                          _("Missing new-line parsing variable names "
                            "at offset %zu in MRSETS record."),
                          text_pos (text));
              break;
            }
          var_name = recode_string ("UTF-8", r->encoding, raw_var_name, -1);

          var = dict_lookup_var (dict, var_name);
          if (var == NULL)
            {
              free (var_name);
              continue;
            }
          if (!stringi_set_insert (&var_names, var_name))
            {
              sys_warn (r, record->pos,
                        _("Duplicate variable name %s "
                          "at offset %zu in MRSETS record."),
                        var_name, text_pos (text));
              free (var_name);
              continue;
            }
          free (var_name);

          if (mrset->label == NULL && mrset->label_from_var_label
              && var_has_label (var))
            mrset->label = xstrdup (var_get_label (var));

          if (mrset->n_vars
              && var_get_type (var) != var_get_type (mrset->vars[0]))
            {
              sys_warn (r, record->pos,
                        _("MRSET %s contains both string and "
                          "numeric variables."), name);
              continue;
            }
          width = MIN (width, var_get_width (var));

          if (mrset->n_vars >= allocated_vars)
            mrset->vars = x2nrealloc (mrset->vars, &allocated_vars,
                                      sizeof *mrset->vars);
          mrset->vars[mrset->n_vars++] = var;
        }
      while (delimiter != '\n');

      if (mrset->n_vars < 2)
        {
          sys_warn (r, record->pos,
                    _("MRSET %s has only %zu variables."), mrset->name,
                    mrset->n_vars);
          mrset_destroy (mrset);
	  stringi_set_destroy (&var_names);
          continue;
        }

      if (mrset->type == MRSET_MD)
        {
          mrset->width = width;
          value_init (&mrset->counted, width);
          if (width == 0)
            mrset->counted.f = c_strtod (counted, NULL);
          else
            value_copy_str_rpad (&mrset->counted, width,
                                 (const uint8_t *) counted, ' ');
        }

      dict_add_mrset (dict, mrset);
      mrset = NULL;
      stringi_set_destroy (&var_names);
    }
  mrset_destroy (mrset);
  close_text_record (r, text);
}

/* Read record type 7, subtype 11, which specifies how variables
   should be displayed in GUI environments. */
static void
parse_display_parameters (struct sfm_reader *r,
                         const struct sfm_extension_record *record,
                         struct dictionary *dict)
{
  bool includes_width;
  bool warned = false;
  size_t n_vars;
  size_t ofs;
  size_t i;

  n_vars = dict_get_var_cnt (dict);
  if (record->count == 3 * n_vars)
    includes_width = true;
  else if (record->count == 2 * n_vars)
    includes_width = false;
  else
    {
      sys_warn (r, record->pos,
                _("Extension 11 has bad count %zu (for %zu variables)."),
                record->count, n_vars);
      return;
    }

  ofs = 0;
  for (i = 0; i < n_vars; ++i)
    {
      struct variable *v = dict_get_var (dict, i);
      int measure, width, align;

      measure = parse_int (r, record->data, ofs);
      ofs += 4;

      if (includes_width)
        {
          width = parse_int (r, record->data, ofs);
          ofs += 4;
        }
      else
        width = 0;

      align = parse_int (r, record->data, ofs);
      ofs += 4;

      /* SPSS sometimes seems to set variables' measure to zero. */
      if (0 == measure)
        measure = 1;

      if (measure < 1 || measure > 3 || align < 0 || align > 2)
        {
          if (!warned)
            sys_warn (r, record->pos,
                      _("Invalid variable display parameters for variable "
                        "%zu (%s).  Default parameters substituted."),
                      i, var_get_name (v));
          warned = true;
          continue;
        }

      var_set_measure (v, (measure == 1 ? MEASURE_NOMINAL
                           : measure == 2 ? MEASURE_ORDINAL
                           : MEASURE_SCALE));
      var_set_alignment (v, (align == 0 ? ALIGN_LEFT
                             : align == 1 ? ALIGN_RIGHT
                             : ALIGN_CENTRE));

      /* Older versions (SPSS 9.0) sometimes set the display
	 width to zero.  This causes confusion in the GUI, so
	 only set the width if it is nonzero. */
      if (width > 0)
        var_set_display_width (v, width);
    }
}

static void
rename_var_and_save_short_names (struct dictionary *dict, struct variable *var,
                                 const char *new_name)
{
  size_t n_short_names;
  char **short_names;
  size_t i;

  /* Renaming a variable may clear its short names, but we
     want to retain them, so we save them and re-set them
     afterward. */
  n_short_names = var_get_short_name_cnt (var);
  short_names = xnmalloc (n_short_names, sizeof *short_names);
  for (i = 0; i < n_short_names; i++)
    {
      const char *s = var_get_short_name (var, i);
      short_names[i] = s != NULL ? xstrdup (s) : NULL;
    }

  /* Set long name. */
  dict_rename_var (dict, var, new_name);

  /* Restore short names. */
  for (i = 0; i < n_short_names; i++)
    {
      var_set_short_name (var, i, short_names[i]);
      free (short_names[i]);
    }
  free (short_names);
}

/* Parses record type 7, subtype 13, which gives the long name that corresponds
   to each short name.  Modifies variable names in DICT accordingly.  */
static void
parse_long_var_name_map (struct sfm_reader *r,
                         const struct sfm_extension_record *record,
                         struct dictionary *dict)
{
  struct text_record *text;
  struct variable *var;
  char *long_name;

  if (record == NULL)
    {
      /* There are no long variable names.  Use the short variable names,
         converted to lowercase, as the long variable names. */
      size_t i;

      for (i = 0; i < dict_get_var_cnt (dict); i++)
	{
	  struct variable *var = dict_get_var (dict, i);
          char *new_name;

          new_name = utf8_to_lower (var_get_name (var));
          rename_var_and_save_short_names (dict, var, new_name);
          free (new_name);
	}

      return;
    }

  /* Rename each of the variables, one by one.  (In a correctly constructed
     system file, this cannot create any intermediate duplicate variable names,
     because all of the new variable names are longer than any of the old
     variable names and thus there cannot be any overlaps.) */
  text = open_text_record (r, record, true);
  while (read_variable_to_value_pair (r, dict, text, &var, &long_name))
    {
      /* Validate long name. */
      if (!dict_id_is_valid (dict, long_name, false))
        {
          sys_warn (r, record->pos,
                    _("Long variable mapping from %s to invalid "
                      "variable name `%s'."),
                    var_get_name (var), long_name);
          continue;
        }

      /* Identify any duplicates. */
      if (utf8_strcasecmp (var_get_short_name (var, 0), long_name)
          && dict_lookup_var (dict, long_name) != NULL)
        {
          sys_warn (r, record->pos,
                    _("Duplicate long variable name `%s'."), long_name);
          continue;
        }

      rename_var_and_save_short_names (dict, var, long_name);
    }
  close_text_record (r, text);
}

/* Reads record type 7, subtype 14, which gives the real length
   of each very long string.  Rearranges DICT accordingly. */
static bool
parse_long_string_map (struct sfm_reader *r,
                       const struct sfm_extension_record *record,
                       struct dictionary *dict)
{
  struct text_record *text;
  struct variable *var;
  char *length_s;

  text = open_text_record (r, record, true);
  while (read_variable_to_value_pair (r, dict, text, &var, &length_s))
    {
      size_t idx = var_get_dict_index (var);
      long int length;
      int segment_cnt;
      int i;

      /* Get length. */
      length = strtol (length_s, NULL, 10);
      if (length < 1 || length > MAX_STRING)
        {
          sys_warn (r, record->pos,
                    _("%s listed as string of invalid length %s "
                      "in very long string record."),
                    var_get_name (var), length_s);
          continue;
        }

      /* Check segments. */
      segment_cnt = sfm_width_to_segments (length);
      if (segment_cnt == 1)
        {
          sys_warn (r, record->pos,
                    _("%s listed in very long string record with width %s, "
                      "which requires only one segment."),
                    var_get_name (var), length_s);
          continue;
        }
      if (idx + segment_cnt > dict_get_var_cnt (dict))
        {
          sys_error (r, record->pos,
                     _("Very long string %s overflows dictionary."),
                     var_get_name (var));
          return false;
        }

      /* Get the short names from the segments and check their
         lengths. */
      for (i = 0; i < segment_cnt; i++)
        {
          struct variable *seg = dict_get_var (dict, idx + i);
          int alloc_width = sfm_segment_alloc_width (length, i);
          int width = var_get_width (seg);

          if (i > 0)
            var_set_short_name (var, i, var_get_short_name (seg, 0));
          if (ROUND_UP (width, 8) != ROUND_UP (alloc_width, 8))
            {
              sys_error (r, record->pos,
                         _("Very long string with width %ld has segment %d "
                           "of width %d (expected %d)."),
                         length, i, width, alloc_width);
              return false;
            }
        }
      dict_delete_consecutive_vars (dict, idx + 1, segment_cnt - 1);
      var_set_width (var, length);
    }
  close_text_record (r, text);
  dict_compact_values (dict);

  return true;
}

static bool
parse_value_labels (struct sfm_reader *r, struct dictionary *dict,
                    const struct sfm_var_record *var_recs, size_t n_var_recs,
                    const struct sfm_value_label_record *record)
{
  struct variable **vars;
  char **utf8_labels;
  size_t i;

  utf8_labels = pool_nmalloc (r->pool, record->n_labels, sizeof *utf8_labels);
  for (i = 0; i < record->n_labels; i++)
    utf8_labels[i] = recode_string_pool ("UTF-8", dict_get_encoding (dict),
                                         record->labels[i].label, -1,
                                         r->pool);

  vars = pool_nmalloc (r->pool, record->n_vars, sizeof *vars);
  for (i = 0; i < record->n_vars; i++)
    {
      vars[i] = lookup_var_by_index (r, record->pos,
                                     var_recs, n_var_recs, record->vars[i]);
      if (vars[i] == NULL)
        return false;
    }

  for (i = 1; i < record->n_vars; i++)
    if (var_get_type (vars[i]) != var_get_type (vars[0]))
      {
        sys_error (r, record->pos,
                   _("Variables associated with value label are not all of "
                     "identical type.  Variable %s is %s, but variable "
                     "%s is %s."),
                   var_get_name (vars[0]),
                   var_is_numeric (vars[0]) ? _("numeric") : _("string"),
                   var_get_name (vars[i]),
                   var_is_numeric (vars[i]) ? _("numeric") : _("string"));
        return false;
      }

  for (i = 0; i < record->n_vars; i++)
    {
      struct variable *var = vars[i];
      int width;
      size_t j;

      width = var_get_width (var);
      if (width > 8)
        {
          sys_error (r, record->pos,
                     _("Value labels may not be added to long string "
                       "variables (e.g. %s) using records types 3 and 4."),
                     var_get_name (var));
          return false;
        }

      for (j = 0; j < record->n_labels; j++)
        {
          struct sfm_value_label *label = &record->labels[j];
          union value value;

          value_init (&value, width);
          if (width == 0)
            value.f = parse_float (r, label->value, 0);
          else
            memcpy (value_str_rw (&value, width), label->value, width);

          if (!var_add_value_label (var, &value, utf8_labels[j]))
            {
              if (var_is_numeric (var))
                sys_warn (r, record->pos,
                          _("Duplicate value label for %g on %s."),
                          value.f, var_get_name (var));
              else
                sys_warn (r, record->pos,
                          _("Duplicate value label for `%.*s' on %s."),
                          width, value_str (&value, width),
                          var_get_name (var));
            }

          value_destroy (&value, width);
        }
    }

  pool_free (r->pool, vars);
  for (i = 0; i < record->n_labels; i++)
    pool_free (r->pool, utf8_labels[i]);
  pool_free (r->pool, utf8_labels);

  return true;
}

static struct variable *
lookup_var_by_index (struct sfm_reader *r, off_t offset,
                     const struct sfm_var_record *var_recs, size_t n_var_recs,
                     int idx)
{
  const struct sfm_var_record *rec;

  if (idx < 1 || idx > n_var_recs)
    {
      sys_error (r, offset,
                 _("Variable index %d not in valid range 1...%zu."),
                 idx, n_var_recs);
      return NULL;
    }

  rec = &var_recs[idx - 1];
  if (rec->var == NULL)
    {
      sys_error (r, offset,
                 _("Variable index %d refers to long string continuation."),
                 idx);
      return NULL;
    }

  return rec->var;
}

/* Parses a set of custom attributes from TEXT into ATTRS.
   ATTRS may be a null pointer, in which case the attributes are
   read but discarded. */
static void
parse_attributes (struct sfm_reader *r, struct text_record *text,
                  struct attrset *attrs)
{
  do
    {
      struct attribute *attr;
      char *key;
      int index;

      /* Parse the key. */
      key = text_get_token (text, ss_cstr ("("), NULL);
      if (key == NULL)
        return;

      attr = attribute_create (key);
      for (index = 1; ; index++)
        {
          /* Parse the value. */
          char *value;
          size_t length;

          value = text_get_token (text, ss_cstr ("\n"), NULL);
          if (value == NULL)
            {
              text_warn (r, text, _("Error parsing attribute value %s[%d]."),
                         key, index);
              break;
            }              

          length = strlen (value);
          if (length >= 2 && value[0] == '\'' && value[length - 1] == '\'') 
            {
              value[length - 1] = '\0';
              attribute_add_value (attr, value + 1); 
            }
          else 
            {
              text_warn (r, text,
                         _("Attribute value %s[%d] is not quoted: %s."),
                         key, index, value);
              attribute_add_value (attr, value); 
            }

          /* Was this the last value for this attribute? */
          if (text_match (text, ')'))
            break;
        }
      if (attrs != NULL)
        attrset_add (attrs, attr);
      else
        attribute_destroy (attr);
    }
  while (!text_match (text, '/'));
}

/* Reads record type 7, subtype 17, which lists custom
   attributes on the data file.  */
static void
parse_data_file_attributes (struct sfm_reader *r,
                            const struct sfm_extension_record *record,
                            struct dictionary *dict)
{
  struct text_record *text = open_text_record (r, record, true);
  parse_attributes (r, text, dict_get_attributes (dict));
  close_text_record (r, text);
}

/* Parses record type 7, subtype 18, which lists custom
   attributes on individual variables.  */
static void
parse_variable_attributes (struct sfm_reader *r,
                           const struct sfm_extension_record *record,
                           struct dictionary *dict)
{
  struct text_record *text;
  struct variable *var;

  text = open_text_record (r, record, true);
  while (text_read_variable_name (r, dict, text, ss_cstr (":"), &var))
    parse_attributes (r, text, var != NULL ? var_get_attributes (var) : NULL);
  close_text_record (r, text);
}

static void
assign_variable_roles (struct sfm_reader *r, struct dictionary *dict)
{
  size_t n_warnings = 0;
  size_t i;

  for (i = 0; i < dict_get_var_cnt (dict); i++)
    {
      struct variable *var = dict_get_var (dict, i);
      struct attrset *attrs = var_get_attributes (var);
      const struct attribute *attr = attrset_lookup (attrs, "$@Role");
      if (attr != NULL)
        {
          int value = atoi (attribute_get_value (attr, 0));
          enum var_role role;

          switch (value)
            {
            case 0:
              role = ROLE_INPUT;
              break;

            case 1:
              role = ROLE_TARGET;
              break;

            case 2:
              role = ROLE_BOTH;
              break;

            case 3:
              role = ROLE_NONE;
              break;

            case 4:
              role = ROLE_PARTITION;
              break;

            case 5:
              role = ROLE_SPLIT;
              break;

            default:
              role = ROLE_INPUT;
              if (n_warnings++ == 0)
                sys_warn (r, -1, _("Invalid role for variable %s."),
                          var_get_name (var));
            }

          var_set_role (var, role);
        }
    }

  if (n_warnings > 1)
    sys_warn (r, -1, _("%zu other variables had invalid roles."),
              n_warnings - 1);
}

static bool
check_overflow (struct sfm_reader *r,
                const struct sfm_extension_record *record,
                size_t ofs, size_t length)
{
  size_t end = record->size * record->count;
  if (length >= end || ofs + length > end)
    {
      sys_error (r, record->pos + end,
                 _("Extension record subtype %d ends unexpectedly."),
                 record->subtype);
      return false;
    }
  return true;
}

static bool
parse_long_string_value_labels (struct sfm_reader *r,
                                const struct sfm_extension_record *record,
                                struct dictionary *dict)
{
  const char *dict_encoding = dict_get_encoding (dict);
  size_t end = record->size * record->count;
  size_t ofs = 0;

  while (ofs < end)
    {
      char *var_name;
      size_t n_labels, i;
      struct variable *var;
      union value value;
      int var_name_len;
      int width;

      /* Parse variable name length. */
      if (!check_overflow (r, record, ofs, 4))
        return false;
      var_name_len = parse_int (r, record->data, ofs);
      ofs += 4;

      /* Parse variable name, width, and number of labels. */
      if (!check_overflow (r, record, ofs, var_name_len + 8))
        return false;
      var_name = recode_string_pool ("UTF-8", dict_encoding,
                                     (const char *) record->data + ofs,
                                     var_name_len, r->pool);
      width = parse_int (r, record->data, ofs + var_name_len);
      n_labels = parse_int (r, record->data, ofs + var_name_len + 4);
      ofs += var_name_len + 8;

      /* Look up 'var' and validate. */
      var = dict_lookup_var (dict, var_name);
      if (var == NULL)
        sys_warn (r, record->pos + ofs,
                  _("Ignoring long string value label record for "
                    "unknown variable %s."), var_name);
      else if (var_is_numeric (var))
        {
          sys_warn (r, record->pos + ofs,
                    _("Ignoring long string value label record for "
                      "numeric variable %s."), var_name);
          var = NULL;
        }
      else if (width != var_get_width (var))
        {
          sys_warn (r, record->pos + ofs,
                    _("Ignoring long string value label record for variable "
                      "%s because the record's width (%d) does not match the "
                      "variable's width (%d)."),
                    var_name, width, var_get_width (var));
          var = NULL;
        }

      /* Parse values. */
      value_init_pool (r->pool, &value, width);
      for (i = 0; i < n_labels; i++)
	{
          size_t value_length, label_length;
          bool skip = var == NULL;

          /* Parse value length. */
          if (!check_overflow (r, record, ofs, 4))
            return false;
          value_length = parse_int (r, record->data, ofs);
          ofs += 4;

          /* Parse value. */
          if (!check_overflow (r, record, ofs, value_length))
            return false;
          if (!skip)
            {
              if (value_length == width)
                memcpy (value_str_rw (&value, width),
                        (const uint8_t *) record->data + ofs, width);
              else
                {
                  sys_warn (r, record->pos + ofs,
                            _("Ignoring long string value label %zu for "
                              "variable %s, with width %d, that has bad value "
                              "width %zu."),
                            i, var_get_name (var), width, value_length);
                  skip = true;
                }
            }
          ofs += value_length;

          /* Parse label length. */
          if (!check_overflow (r, record, ofs, 4))
            return false;
          label_length = parse_int (r, record->data, ofs);
          ofs += 4;

          /* Parse label. */
          if (!check_overflow (r, record, ofs, label_length))
            return false;
          if (!skip)
            {
              char *label;

              label = recode_string_pool ("UTF-8", dict_encoding,
                                          (const char *) record->data + ofs,
                                          label_length, r->pool);
              if (!var_add_value_label (var, &value, label))
                sys_warn (r, record->pos + ofs,
                          _("Duplicate value label for `%.*s' on %s."),
                          width, value_str (&value, width),
                          var_get_name (var));
              pool_free (r->pool, label);
            }
          ofs += label_length;
        }
    }

  return true;
}

static bool
parse_long_string_missing_values (struct sfm_reader *r,
                                  const struct sfm_extension_record *record,
                                  struct dictionary *dict)
{
  const char *dict_encoding = dict_get_encoding (dict);
  size_t end = record->size * record->count;
  size_t ofs = 0;

  while (ofs < end)
    {
      struct missing_values mv;
      char *var_name;
      struct variable *var;
      int n_missing_values;
      int var_name_len;
      size_t i;

      /* Parse variable name length. */
      if (!check_overflow (r, record, ofs, 4))
        return false;
      var_name_len = parse_int (r, record->data, ofs);
      ofs += 4;

      /* Parse variable name. */
      if (!check_overflow (r, record, ofs, var_name_len + 1))
        return false;
      var_name = recode_string_pool ("UTF-8", dict_encoding,
                                     (const char *) record->data + ofs,
                                     var_name_len, r->pool);
      ofs += var_name_len;

      /* Parse number of missing values. */
      n_missing_values = ((const uint8_t *) record->data)[ofs];
      if (n_missing_values < 1 || n_missing_values > 3)
        sys_warn (r, record->pos + ofs,
                  _("Long string missing values record says variable %s "
                    "has %d missing values, but only 1 to 3 missing values "
                    "are allowed."),
                  var_name, n_missing_values);
      ofs++;

      /* Look up 'var' and validate. */
      var = dict_lookup_var (dict, var_name);
      if (var == NULL)
        sys_warn (r, record->pos + ofs,
                  _("Ignoring long string missing value record for "
                    "unknown variable %s."), var_name);
      else if (var_is_numeric (var))
        {
          sys_warn (r, record->pos + ofs,
                    _("Ignoring long string missing value record for "
                      "numeric variable %s."), var_name);
          var = NULL;
        }

      /* Parse values. */
      mv_init_pool (r->pool, &mv, var ? var_get_width (var) : 8);
      for (i = 0; i < n_missing_values; i++)
	{
          size_t value_length;

          /* Parse value length. */
          if (!check_overflow (r, record, ofs, 4))
            return false;
          value_length = parse_int (r, record->data, ofs);
          ofs += 4;

          /* Parse value. */
          if (!check_overflow (r, record, ofs, value_length))
            return false;
          if (var != NULL
              && i < 3
              && !mv_add_str (&mv, (const uint8_t *) record->data + ofs,
                              value_length))
            sys_warn (r, record->pos + ofs,
                      _("Ignoring long string missing value %zu for variable "
                        "%s, with width %d, that has bad value width %zu."),
                      i, var_get_name (var), var_get_width (var),
                      value_length);
          ofs += value_length;
        }
      if (var != NULL)
        var_set_missing_values (var, &mv);
    }

  return true;
}

/* Case reader. */

static void partial_record (struct sfm_reader *);

static void read_error (struct casereader *, const struct sfm_reader *);

static bool read_case_number (struct sfm_reader *, double *);
static int read_case_string (struct sfm_reader *, uint8_t *, size_t);
static int read_opcode (struct sfm_reader *);
static bool read_compressed_number (struct sfm_reader *, double *);
static int read_compressed_string (struct sfm_reader *, uint8_t *);
static int read_whole_strings (struct sfm_reader *, uint8_t *, size_t);
static bool skip_whole_strings (struct sfm_reader *, size_t);

/* Reads and returns one case from READER's file.  Returns a null
   pointer if not successful. */
static struct ccase *
sys_file_casereader_read (struct casereader *reader, void *r_)
{
  struct sfm_reader *r = r_;
  struct ccase *c;
  int retval;
  int i;

  if (r->error)
    return NULL;

  c = case_create (r->proto);

  for (i = 0; i < r->sfm_var_cnt; i++)
    {
      struct sfm_var *sv = &r->sfm_vars[i];
      union value *v = case_data_rw_idx (c, sv->case_index);

      if (sv->var_width == 0)
        retval = read_case_number (r, &v->f);
      else
        {
          uint8_t *s = value_str_rw (v, sv->var_width);
          retval = read_case_string (r, s + sv->offset, sv->segment_width);
          if (retval == 1)
            {
              retval = skip_whole_strings (r, ROUND_DOWN (sv->padding, 8));
              if (retval == 0)
                sys_error (r, r->pos, _("File ends in partial string value."));
            }
        }

      if (retval != 1)
        goto eof;
    }
  return c;

eof:
  if (i != 0)
    partial_record (r);
  if (r->case_cnt != -1)
    read_error (reader, r);
  case_unref (c);
  return NULL;
}

/* Issues an error that R ends in a partial record. */
static void
partial_record (struct sfm_reader *r)
{
  sys_error (r, r->pos, _("File ends in partial case."));
}

/* Issues an error that an unspecified error occurred SFM, and
   marks R tainted. */
static void
read_error (struct casereader *r, const struct sfm_reader *sfm)
{
  msg (ME, _("Error reading case from file %s."), fh_get_name (sfm->fh));
  casereader_force_error (r);
}

/* Reads a number from R and stores its value in *D.
   If R is compressed, reads a compressed number;
   otherwise, reads a number in the regular way.
   Returns true if successful, false if end of file is
   reached immediately. */
static bool
read_case_number (struct sfm_reader *r, double *d)
{
  if (r->compression == SFM_COMP_NONE)
    {
      uint8_t number[8];
      if (!try_read_bytes (r, number, sizeof number))
        return false;
      float_convert (r->float_format, number, FLOAT_NATIVE_DOUBLE, d);
      return true;
    }
  else
    return read_compressed_number (r, d);
}

/* Reads LENGTH string bytes from R into S.  Always reads a multiple of 8
   bytes; if LENGTH is not a multiple of 8, then extra bytes are read and
   discarded without being written to S.  Reads compressed strings if S is
   compressed.  Returns 1 if successful, 0 if end of file is reached
   immediately, or -1 for some kind of error. */
static int
read_case_string (struct sfm_reader *r, uint8_t *s, size_t length)
{
  size_t whole = ROUND_DOWN (length, 8);
  size_t partial = length % 8;

  if (whole)
    {
      int retval = read_whole_strings (r, s, whole);
      if (retval != 1)
        return retval;
    }

  if (partial)
    {
      uint8_t bounce[8];
      int retval = read_whole_strings (r, bounce, sizeof bounce);
      if (retval == -1)
        return -1;
      else if (!retval)
        {
          if (whole)
            {
              partial_record (r);
              return -1;
            }
          return 0;
        }
      memcpy (s + whole, bounce, partial);
    }

  return 1;
}

/* Reads and returns the next compression opcode from R. */
static int
read_opcode (struct sfm_reader *r)
{
  assert (r->compression != SFM_COMP_NONE);
  for (;;)
    {
      int opcode;
      if (r->opcode_idx >= sizeof r->opcodes)
        {

          int retval = try_read_compressed_bytes (r, r->opcodes,
                                                  sizeof r->opcodes);
          if (retval != 1)
            return -1;
          r->opcode_idx = 0;
        }
      opcode = r->opcodes[r->opcode_idx++];

      if (opcode != 0)
        return opcode;
    }
}

/* Reads a compressed number from R and stores its value in D.
   Returns true if successful, false if end of file is
   reached immediately. */
static bool
read_compressed_number (struct sfm_reader *r, double *d)
{
  int opcode = read_opcode (r);
  switch (opcode)
    {
    case -1:
    case 252:
      return false;

    case 253:
      return read_compressed_float (r, d);

    case 254:
      float_convert (r->float_format, "        ", FLOAT_NATIVE_DOUBLE, d);
      if (!r->corruption_warning)
        {
          r->corruption_warning = true;
          sys_warn (r, r->pos,
                    _("Possible compressed data corruption: "
                      "compressed spaces appear in numeric field."));
        }
      break;

    case 255:
      *d = SYSMIS;
      break;

    default:
      *d = opcode - r->bias;
      break;
    }

  return true;
}

/* Reads a compressed 8-byte string segment from R and stores it in DST. */
static int
read_compressed_string (struct sfm_reader *r, uint8_t *dst)
{
  int opcode;
  int retval;

  opcode = read_opcode (r);
  switch (opcode)
    {
    case -1:
    case 252:
      return 0;

    case 253:
      retval = read_compressed_bytes (r, dst, 8);
      return retval == 1 ? 1 : -1;

    case 254:
      memset (dst, ' ', 8);
      return 1;

    default:
      {
        double value = opcode - r->bias;
        float_convert (FLOAT_NATIVE_DOUBLE, &value, r->float_format, dst);
        if (value == 0.0)
          {
            /* This has actually been seen "in the wild".  The submitter of the
               file that showed that the contents decoded as spaces, but they
               were at the end of the field so it's possible that the null
               bytes just acted as null terminators. */
          }
        else if (!r->corruption_warning)
          {
            r->corruption_warning = true;
            sys_warn (r, r->pos,
                      _("Possible compressed data corruption: "
                        "string contains compressed integer (opcode %d)."),
                      opcode);
          }
      }
      return 1;
    }
}

/* Reads LENGTH string bytes from R into S.  LENGTH must be a multiple of 8.
   Reads compressed strings if S is compressed.  Returns 1 if successful, 0 if
   end of file is reached immediately, or -1 for some kind of error. */
static int
read_whole_strings (struct sfm_reader *r, uint8_t *s, size_t length)
{
  assert (length % 8 == 0);
  if (r->compression == SFM_COMP_NONE)
    return try_read_bytes (r, s, length);
  else
    {
      size_t ofs;

      for (ofs = 0; ofs < length; ofs += 8)
        {
          int retval = read_compressed_string (r, s + ofs);
          if (retval != 1)
            {
              if (ofs != 0)
                {
                  partial_record (r);
                  return -1;
                }
              return retval;
            }
          }
      return 1;
    }
}

/* Skips LENGTH string bytes from R.
   LENGTH must be a multiple of 8.
   (LENGTH is also limited to 1024, but that's only because the
   current caller never needs more than that many bytes.)
   Returns true if successful, false if end of file is
   reached immediately. */
static bool
skip_whole_strings (struct sfm_reader *r, size_t length)
{
  uint8_t buffer[1024];
  assert (length < sizeof buffer);
  return read_whole_strings (r, buffer, length);
}

/* Helpers for reading records that contain structured text
   strings. */

/* Maximum number of warnings to issue for a single text
   record. */
#define MAX_TEXT_WARNINGS 5

/* State. */
struct text_record
  {
    struct substring buffer;    /* Record contents. */
    off_t start;                /* Starting offset in file. */
    size_t pos;                 /* Current position in buffer. */
    int n_warnings;             /* Number of warnings issued or suppressed. */
    bool recoded;               /* Recoded into UTF-8? */
  };

static struct text_record *
open_text_record (struct sfm_reader *r,
                  const struct sfm_extension_record *record,
                  bool recode_to_utf8)
{
  struct text_record *text;
  struct substring raw;

  text = pool_alloc (r->pool, sizeof *text);
  raw = ss_buffer (record->data, record->size * record->count);
  text->start = record->pos;
  text->buffer = (recode_to_utf8
                  ? recode_substring_pool ("UTF-8", r->encoding, raw, r->pool)
                  : raw);
  text->pos = 0;
  text->n_warnings = 0;
  text->recoded = recode_to_utf8;

  return text;
}

/* Closes TEXT, frees its storage, and issues a final warning
   about suppressed warnings if necesary. */
static void
close_text_record (struct sfm_reader *r, struct text_record *text)
{
  if (text->n_warnings > MAX_TEXT_WARNINGS)
    sys_warn (r, -1, _("Suppressed %d additional related warnings."),
              text->n_warnings - MAX_TEXT_WARNINGS);
  if (text->recoded)
    pool_free (r->pool, ss_data (text->buffer));
}

/* Reads a variable=value pair from TEXT.
   Looks up the variable in DICT and stores it into *VAR.
   Stores a null-terminated value into *VALUE. */
static bool
read_variable_to_value_pair (struct sfm_reader *r, struct dictionary *dict,
                             struct text_record *text,
                             struct variable **var, char **value)
{
  for (;;)
    {
      if (!text_read_short_name (r, dict, text, ss_cstr ("="), var))
        return false;
      
      *value = text_get_token (text, ss_buffer ("\t\0", 2), NULL);
      if (*value == NULL)
        return false;

      text->pos += ss_span (ss_substr (text->buffer, text->pos, SIZE_MAX),
                            ss_buffer ("\t\0", 2));

      if (*var != NULL)
        return true;
    }
}

static bool
text_read_variable_name (struct sfm_reader *r, struct dictionary *dict,
                         struct text_record *text, struct substring delimiters,
                         struct variable **var)
{
  char *name;

  name = text_get_token (text, delimiters, NULL);
  if (name == NULL)
    return false;

  *var = dict_lookup_var (dict, name);
  if (*var != NULL)
    return true;

  text_warn (r, text, _("Dictionary record refers to unknown variable %s."),
             name);
  return false;
}


static bool
text_read_short_name (struct sfm_reader *r, struct dictionary *dict,
                      struct text_record *text, struct substring delimiters,
                      struct variable **var)
{
  char *short_name = text_get_token (text, delimiters, NULL);
  if (short_name == NULL)
    return false;

  *var = dict_lookup_var (dict, short_name);
  if (*var == NULL)
    text_warn (r, text, _("Dictionary record refers to unknown variable %s."),
               short_name);
  return true;
}

/* Displays a warning for the current file position, limiting the
   number to MAX_TEXT_WARNINGS for TEXT. */
static void
text_warn (struct sfm_reader *r, struct text_record *text,
           const char *format, ...)
{
  if (text->n_warnings++ < MAX_TEXT_WARNINGS) 
    {
      va_list args;

      va_start (args, format);
      sys_msg (r, text->start + text->pos, MW, format, args);
      va_end (args);
    }
}

static char *
text_get_token (struct text_record *text, struct substring delimiters,
                char *delimiter)
{
  struct substring token;
  char *end;

  if (!ss_tokenize (text->buffer, delimiters, &text->pos, &token))
    return NULL;

  end = &ss_data (token)[ss_length (token)];
  if (delimiter != NULL)
    *delimiter = *end;
  *end = '\0';
  return ss_data (token);
}

/* Reads a integer value expressed in decimal, then a space, then a string that
   consists of exactly as many bytes as specified by the integer, then a space,
   from TEXT.  Returns the string, null-terminated, as a subset of TEXT's
   buffer (so the caller should not free the string). */
static const char *
text_parse_counted_string (struct sfm_reader *r, struct text_record *text)
{
  size_t start;
  size_t n;
  char *s;

  start = text->pos;
  n = 0;
  while (text->pos < text->buffer.length)
    {
      int c = text->buffer.string[text->pos];
      if (c < '0' || c > '9')
        break;
      n = (n * 10) + (c - '0');
      text->pos++;
    }
  if (text->pos >= text->buffer.length || start == text->pos)
    {
      sys_warn (r, text->start,
                _("Expecting digit at offset %zu in MRSETS record."),
                text->pos);
      return NULL;
    }

  if (!text_match (text, ' '))
    {
      sys_warn (r, text->start,
                _("Expecting space at offset %zu in MRSETS record."),
                text->pos);
      return NULL;
    }

  if (text->pos + n > text->buffer.length)
    {
      sys_warn (r, text->start,
                _("%zu-byte string starting at offset %zu "
                  "exceeds record length %zu."),
                n, text->pos, text->buffer.length);
      return NULL;
    }

  s = &text->buffer.string[text->pos];
  if (s[n] != ' ')
    {
      sys_warn (r, text->start,
                _("Expecting space at offset %zu following %zu-byte string."),
                text->pos + n, n);
      return NULL;
    }
  s[n] = '\0';
  text->pos += n + 1;
  return s;
}

static bool
text_match (struct text_record *text, char c)
{
  if (text->buffer.string[text->pos] == c) 
    {
      text->pos++;
      return true;
    }
  else
    return false;
}

/* Returns the current byte offset (as converted to UTF-8, if it was converted)
   inside the TEXT's string. */
static size_t
text_pos (const struct text_record *text)
{
  return text->pos;
}

static const char *
text_get_all (const struct text_record *text)
{
  return text->buffer.string;
}

/* Messages. */

/* Displays a corruption message. */
static void
sys_msg (struct sfm_reader *r, off_t offset,
         int class, const char *format, va_list args)
{
  struct msg m;
  struct string text;

  ds_init_empty (&text);
  if (offset >= 0)
    ds_put_format (&text, _("`%s' near offset 0x%llx: "),
                   fh_get_file_name (r->fh), (long long int) offset);
  else
    ds_put_format (&text, _("`%s': "), fh_get_file_name (r->fh));
  ds_put_vformat (&text, format, args);

  m.category = msg_class_to_category (class);
  m.severity = msg_class_to_severity (class);
  m.file_name = NULL;
  m.first_line = 0;
  m.last_line = 0;
  m.first_column = 0;
  m.last_column = 0;
  m.text = ds_cstr (&text);

  msg_emit (&m);
}

/* Displays a warning for offset OFFSET in the file. */
static void
sys_warn (struct sfm_reader *r, off_t offset, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  sys_msg (r, offset, MW, format, args);
  va_end (args);
}

/* Displays an error for the current file position,
   marks it as in an error state,
   and aborts reading it using longjmp. */
static void
sys_error (struct sfm_reader *r, off_t offset, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  sys_msg (r, offset, ME, format, args);
  va_end (args);

  r->error = true;
}

/* Reads BYTE_CNT bytes into BUF.
   Returns 1 if exactly BYTE_CNT bytes are successfully read.
   Returns -1 if an I/O error or a partial read occurs.
   Returns 0 for an immediate end-of-file and, if EOF_IS_OK is false, reports
   an error. */
static inline int
read_bytes_internal (struct sfm_reader *r, bool eof_is_ok,
                     void *buf, size_t byte_cnt)
{
  size_t bytes_read = fread (buf, 1, byte_cnt, r->file);
  r->pos += bytes_read;
  if (bytes_read == byte_cnt)
    return 1;
  else if (ferror (r->file))
    {
      sys_error (r, r->pos, _("System error: %s."), strerror (errno));
      return -1;
    }
  else if (!eof_is_ok || bytes_read != 0)
    {
      sys_error (r, r->pos, _("Unexpected end of file."));
      return -1;
    }
  else
    return 0;
}

/* Reads BYTE_CNT into BUF.
   Returns true if successful.
   Returns false upon I/O error or if end-of-file is encountered. */
static bool
read_bytes (struct sfm_reader *r, void *buf, size_t byte_cnt)
{
  return read_bytes_internal (r, false, buf, byte_cnt) == 1;
}

/* Reads BYTE_CNT bytes into BUF.
   Returns 1 if exactly BYTE_CNT bytes are successfully read.
   Returns 0 if an immediate end-of-file is encountered.
   Returns -1 if an I/O error or a partial read occurs. */
static int
try_read_bytes (struct sfm_reader *r, void *buf, size_t byte_cnt)
{
  return read_bytes_internal (r, true, buf, byte_cnt);
}

/* Reads a 32-bit signed integer from R and stores its value in host format in
   *X.  Returns true if successful, otherwise false. */
static bool
read_int (struct sfm_reader *r, int *x)
{
  uint8_t integer[4];
  if (read_bytes (r, integer, sizeof integer) != 1)
    return false;
  *x = integer_get (r->integer_format, integer, sizeof integer);
  return true;
}

static bool
read_uint (struct sfm_reader *r, unsigned int *x)
{
  bool ok;
  int y;

  ok = read_int (r, &y);
  *x = y;
  return ok;
}

/* Reads a 64-bit signed integer from R and returns its value in
   host format. */
static bool
read_int64 (struct sfm_reader *r, long long int *x)
{
  uint8_t integer[8];
  if (read_bytes (r, integer, sizeof integer) != 1)
    return false;
  *x = integer_get (r->integer_format, integer, sizeof integer);
  return true;
}

/* Reads a 64-bit signed integer from R and returns its value in
   host format. */
static bool
read_uint64 (struct sfm_reader *r, unsigned long long int *x)
{
  long long int y;
  bool ok;

  ok = read_int64 (r, &y);
  *x = y;
  return ok;
}

static int
parse_int (const struct sfm_reader *r, const void *data, size_t ofs)
{
  return integer_get (r->integer_format, (const uint8_t *) data + ofs, 4);
}

static double
parse_float (const struct sfm_reader *r, const void *data, size_t ofs)
{
  return float_get_double (r->float_format, (const uint8_t *) data + ofs);
}

/* Reads exactly SIZE - 1 bytes into BUFFER
   and stores a null byte into BUFFER[SIZE - 1]. */
static bool
read_string (struct sfm_reader *r, char *buffer, size_t size)
{
  bool ok;

  assert (size > 0);
  ok = read_bytes (r, buffer, size - 1);
  if (ok)
    buffer[size - 1] = '\0';
  return ok;
}

/* Skips BYTES bytes forward in R. */
static bool
skip_bytes (struct sfm_reader *r, size_t bytes)
{
  while (bytes > 0)
    {
      char buffer[1024];
      size_t chunk = MIN (sizeof buffer, bytes);
      if (!read_bytes (r, buffer, chunk))
        return false;
      bytes -= chunk;
    }

  return true;
}

/* Returns a malloc()'d copy of S in which all lone CRs and CR LF pairs have
   been replaced by LFs.

   (A product that identifies itself as VOXCO INTERVIEWER 4.3 produces system
   files that use CR-only line ends in the file label and extra product
   info.) */
static char *
fix_line_ends (const char *s)
{
  char *dst, *d;

  d = dst = xmalloc (strlen (s) + 1);
  while (*s != '\0')
    {
      if (*s == '\r')
        {
          s++;
          if (*s == '\n')
            s++;
          *d++ = '\n';
        }
      else
        *d++ = *s++;
    }
  *d = '\0';

  return dst;
}

static bool
read_ztrailer (struct sfm_reader *r,
               long long int zheader_ofs,
               long long int ztrailer_len);

static void *
zalloc (voidpf pool_, uInt items, uInt size)
{
  struct pool *pool = pool_;

  return (!size || xalloc_oversized (items, size)
          ? Z_NULL
          : pool_malloc (pool, items * size));
}

static void
zfree (voidpf pool_, voidpf address)
{
  struct pool *pool = pool_;

  pool_free (pool, address);
}

static bool
read_zheader (struct sfm_reader *r)
{
  off_t pos = r->pos;
  long long int zheader_ofs;
  long long int ztrailer_ofs;
  long long int ztrailer_len;

  if (!read_int64 (r, &zheader_ofs)
      || !read_int64 (r, &ztrailer_ofs)
      || !read_int64 (r, &ztrailer_len))
    return false;

  if (zheader_ofs != pos)
    {
      sys_error (r, pos, _("Wrong ZLIB data header offset %#llx "
                           "(expected %#llx)."),
                 zheader_ofs, (long long int) pos);
      return false;
    }

  if (ztrailer_ofs < r->pos)
    {
      sys_error (r, pos, _("Impossible ZLIB trailer offset 0x%llx."),
                 ztrailer_ofs);
      return false;
    }

  if (ztrailer_len < 24 || ztrailer_len % 24)
    {
      sys_error (r, pos, _("Invalid ZLIB trailer length %lld."), ztrailer_len);
      return false;
    }

  r->ztrailer_ofs = ztrailer_ofs;
  if (!read_ztrailer (r, zheader_ofs, ztrailer_len))
    return false;

  if (r->zin_buf == NULL)
    {
      r->zin_buf = pool_malloc (r->pool, ZIN_BUF_SIZE);
      r->zout_buf = pool_malloc (r->pool, ZOUT_BUF_SIZE);
      r->zstream.next_in = NULL;
      r->zstream.avail_in = 0;
    }

  r->zstream.zalloc = zalloc;
  r->zstream.zfree = zfree;
  r->zstream.opaque = r->pool;

  return open_zstream (r);
}

static void
seek (struct sfm_reader *r, off_t offset)
{
  if (fseeko (r->file, offset, SEEK_SET))
    sys_error (r, 0, _("%s: seek failed (%s)."),
               fh_get_file_name (r->fh), strerror (errno));
  r->pos = offset;
}

/* Performs some additional consistency checks on the ZLIB compressed data
   trailer. */
static bool
read_ztrailer (struct sfm_reader *r,
               long long int zheader_ofs,
               long long int ztrailer_len)
{
  long long int expected_uncmp_ofs;
  long long int expected_cmp_ofs;
  long long int bias;
  long long int zero;
  unsigned int block_size;
  unsigned int n_blocks;
  unsigned int i;
  struct stat s;

  if (fstat (fileno (r->file), &s))
    {
      sys_error (ME, 0, _("%s: stat failed (%s)."),
                 fh_get_file_name (r->fh), strerror (errno));
      return false;
    }

  if (!S_ISREG (s.st_mode))
    {
      /* We can't seek to the trailer and then back to the data in this file,
         so skip doing extra checks. */
      return true;
    }

  if (r->ztrailer_ofs + ztrailer_len != s.st_size)
    sys_warn (r, r->pos,
              _("End of ZLIB trailer (0x%llx) is not file size (0x%llx)."),
              r->ztrailer_ofs + ztrailer_len, (long long int) s.st_size);

  seek (r, r->ztrailer_ofs);

  /* Read fixed header from ZLIB data trailer. */
  if (!read_int64 (r, &bias))
    return false;
  if (-bias != r->bias)
    {
      sys_error (r, r->pos, _("ZLIB trailer bias (%lld) differs from "
                              "file header bias (%.2f)."),
                 -bias, r->bias);
      return false;
    }

  if (!read_int64 (r, &zero))
    return false;
  if (zero != 0)
    sys_warn (r, r->pos,
              _("ZLIB trailer \"zero\" field has nonzero value %lld."), zero);

  if (!read_uint (r, &block_size))
    return false;
  if (block_size != ZBLOCK_SIZE)
    sys_warn (r, r->pos,
              _("ZLIB trailer specifies unexpected %u-byte block size."),
              block_size);

  if (!read_uint (r, &n_blocks))
    return false;
  if (n_blocks != (ztrailer_len - 24) / 24)
    {
      sys_error (r, r->pos,
                 _("%lld-byte ZLIB trailer specifies %u data blocks (expected "
                   "%lld)."),
                 ztrailer_len, n_blocks, (ztrailer_len - 24) / 24);
      return false;
    }

  expected_uncmp_ofs = zheader_ofs;
  expected_cmp_ofs = zheader_ofs + 24;
  for (i = 0; i < n_blocks; i++)
    {
      off_t desc_ofs = r->pos;
      unsigned long long int uncompressed_ofs;
      unsigned long long int compressed_ofs;
      unsigned int uncompressed_size;
      unsigned int compressed_size;

      if (!read_uint64 (r, &uncompressed_ofs)
          || !read_uint64 (r, &compressed_ofs)
          || !read_uint (r, &uncompressed_size)
          || !read_uint (r, &compressed_size))
        return false;

      if (uncompressed_ofs != expected_uncmp_ofs)
        {
          sys_error (r, desc_ofs,
                     _("ZLIB block descriptor %u reported uncompressed data "
                       "offset %#llx, when %#llx was expected."),
                     i, uncompressed_ofs, expected_uncmp_ofs);
          return false;
        }

      if (compressed_ofs != expected_cmp_ofs)
        {
          sys_error (r, desc_ofs,
                     _("ZLIB block descriptor %u reported compressed data "
                       "offset %#llx, when %#llx was expected."),
                     i, compressed_ofs, expected_cmp_ofs);
          return false;
        }

      if (i < n_blocks - 1)
        {
          if (uncompressed_size != block_size)
            sys_warn (r, desc_ofs,
                      _("ZLIB block descriptor %u reported block size %#x, "
                        "when %#x was expected."),
                      i, uncompressed_size, block_size);
        }
      else
        {
          if (uncompressed_size > block_size)
            sys_warn (r, desc_ofs,
                      _("ZLIB block descriptor %u reported block size %#x, "
                        "when at most %#x was expected."),
                      i, uncompressed_size, block_size);
        }

      /* http://www.zlib.net/zlib_tech.html says that the maximum expansion
         from compression, with worst-case parameters, is 13.5% plus 11 bytes.
         This code checks for an expansion of more than 14.3% plus 11
         bytes.  */
      if (compressed_size > uncompressed_size + uncompressed_size / 7 + 11)
        {
          sys_error (r, desc_ofs,
                     _("ZLIB block descriptor %u reports compressed size %u "
                       "and uncompressed size %u."),
                     i, compressed_size, uncompressed_size);
          return false;
        }

      expected_uncmp_ofs += uncompressed_size;
      expected_cmp_ofs += compressed_size;
    }

  if (expected_cmp_ofs != r->ztrailer_ofs)
    {
      sys_error (r, r->pos, _("ZLIB trailer is at offset %#llx but %#llx "
                              "would be expected from block descriptors."),
                 r->ztrailer_ofs, expected_cmp_ofs);
      return false;
    }

  seek (r, zheader_ofs + 24);
  return true;
}

static bool
open_zstream (struct sfm_reader *r)
{
  int error;

  r->zout_pos = r->zout_end = 0;
  error = inflateInit (&r->zstream);
  if (error != Z_OK)
    {
      sys_error (r, r->pos, _("ZLIB initialization failed (%s)."),
                 r->zstream.msg);
      return false;
    }
  return true;
}

static bool
close_zstream (struct sfm_reader *r)
{
  int error;

  error = inflateEnd (&r->zstream);
  if (error != Z_OK)
    {
      sys_error (r, r->pos, _("Inconsistency at end of ZLIB stream (%s)."),
                 r->zstream.msg);
      return false;
    }
  return true;
}

static int
read_bytes_zlib (struct sfm_reader *r, void *buf_, size_t byte_cnt)
{
  uint8_t *buf = buf_;

  if (byte_cnt == 0)
    return 1;

  for (;;)
    {
      int error;

      /* Use already inflated data if there is any. */
      if (r->zout_pos < r->zout_end)
        {
          unsigned int n = MIN (byte_cnt, r->zout_end - r->zout_pos);
          memcpy (buf, &r->zout_buf[r->zout_pos], n);
          r->zout_pos += n;
          byte_cnt -= n;
          buf += n;

          if (byte_cnt == 0)
            return 1;
        }

      /* We need to inflate some more data.
         Get some more input data if we don't have any. */
      if (r->zstream.avail_in == 0)
        {
          unsigned int n = MIN (ZIN_BUF_SIZE, r->ztrailer_ofs - r->pos);
          if (n == 0)
            return 0;
          else
            {
              int retval = try_read_bytes (r, r->zin_buf, n);
              if (retval != 1)
                return retval;
              r->zstream.avail_in = n;
              r->zstream.next_in = r->zin_buf;
            }
        }

      /* Inflate the (remaining) input data. */
      r->zstream.avail_out = ZOUT_BUF_SIZE;
      r->zstream.next_out = r->zout_buf;
      error = inflate (&r->zstream, Z_SYNC_FLUSH);
      r->zout_pos = 0;
      r->zout_end = r->zstream.next_out - r->zout_buf;
      if (r->zout_end == 0)
        {
          if (error != Z_STREAM_END)
            {
              sys_error (r, r->pos, _("ZLIB stream inconsistency (%s)."),
                         r->zstream.msg);
              return -1;
            }
          else if (!close_zstream (r) || !open_zstream (r))
            return -1;
        }
      else
        {
          /* Process the output data and ignore 'error' for now.  ZLIB will
             present it to us again on the next inflate() call. */
        }
    }
}

static int
read_compressed_bytes (struct sfm_reader *r, void *buf, size_t byte_cnt)
{
  if (r->compression == SFM_COMP_SIMPLE)
    return read_bytes (r, buf, byte_cnt);
  else
    {
      int retval = read_bytes_zlib (r, buf, byte_cnt);
      if (retval == 0)
        sys_error (r, r->pos, _("Unexpected end of ZLIB compressed data."));
      return retval;
    }
}

static int
try_read_compressed_bytes (struct sfm_reader *r, void *buf, size_t byte_cnt)
{
  if (r->compression == SFM_COMP_SIMPLE)
    return try_read_bytes (r, buf, byte_cnt);
  else
    return read_bytes_zlib (r, buf, byte_cnt);
}

/* Reads a 64-bit floating-point number from R and returns its
   value in host format. */
static bool
read_compressed_float (struct sfm_reader *r, double *d)
{
  uint8_t number[8];

  if (!read_compressed_bytes (r, number, sizeof number))
    return false;

  *d = float_get_double (r->float_format, number);
  return true;
}

static const struct casereader_class sys_file_casereader_class =
  {
    sys_file_casereader_read,
    sys_file_casereader_destroy,
    NULL,
    NULL,
  };
