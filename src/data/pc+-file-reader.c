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

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "data/any-reader.h"
#include "data/case.h"
#include "data/casereader-provider.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "data/format.h"
#include "data/identifier.h"
#include "data/missing-values.h"
#include "data/value-labels.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/float-format.h"
#include "libpspp/i18n.h"
#include "libpspp/integer-format.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/localcharset.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xsize.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

struct pcp_dir_entry
  {
    unsigned int ofs;
    unsigned int len;
  };

struct pcp_directory
  {
    struct pcp_dir_entry main;
    struct pcp_dir_entry variables;
    struct pcp_dir_entry labels;
    struct pcp_dir_entry data;
  };

struct pcp_main_header
  {
    char product[63];           /* "PCSPSS SYSTEM FILE..." */
    unsigned int nominal_case_size; /* Number of var positions. */
    char creation_date[9];	/* "[m]m/dd/yy". */
    char creation_time[9];	/* "[H]H:MM:SS". */
    char file_label[65];        /* File label. */
  };

struct pcp_var_record
  {
    unsigned int pos;

    char name[9];
    int width;
    struct fmt_spec format;
    uint8_t missing[8];
    char *label;

    struct pcp_value_label *val_labs;
    size_t n_val_labs;

    struct variable *var;
  };

struct pcp_value_label
  {
    uint8_t value[8];
    char *label;
  };

/* System file reader. */
struct pcp_reader
  {
    struct any_reader any_reader;

    /* Resource tracking. */
    struct pool *pool;          /* All system file state. */

    /* File data. */
    unsigned int file_size;
    struct any_read_info info;
    struct pcp_directory directory;
    struct pcp_main_header header;
    struct pcp_var_record *vars;
    size_t n_vars;

    /* File state. */
    struct file_handle *fh;     /* File handle. */
    struct fh_lock *lock;       /* Mutual exclusion for file handle. */
    FILE *file;                 /* File stream. */
    unsigned int pos;           /* Position in file. */
    bool error;                 /* I/O or corruption error? */
    struct caseproto *proto;    /* Format of output cases. */

    /* File format. */
    unsigned int n_cases;       /* Number of cases */
    const char *encoding;       /* String encoding. */

    /* Decompression. */
    bool compressed;
    uint8_t opcodes[8];         /* Current block of opcodes. */
    size_t opcode_idx;          /* Next opcode to interpret, 8 if none left. */
    bool corruption_warning;    /* Warned about possible corruption? */
  };

static struct pcp_reader *
pcp_reader_cast (const struct any_reader *r_)
{
  assert (r_->klass == &pcp_file_reader_class);
  return UP_CAST (r_, struct pcp_reader, any_reader);
}

static const struct casereader_class pcp_file_casereader_class;

static bool pcp_close (struct any_reader *);

static bool read_variables_record (struct pcp_reader *);

static void pcp_msg (struct pcp_reader *r, off_t, int class,
                     const char *format, va_list args)
     PRINTF_FORMAT (4, 0);
static void pcp_warn (struct pcp_reader *, off_t, const char *, ...)
     PRINTF_FORMAT (3, 4);
static void pcp_error (struct pcp_reader *, off_t, const char *, ...)
     PRINTF_FORMAT (3, 4);

static bool read_bytes (struct pcp_reader *, void *, size_t)
  WARN_UNUSED_RESULT;
static int try_read_bytes (struct pcp_reader *, void *, size_t)
  WARN_UNUSED_RESULT;
static bool read_uint16 (struct pcp_reader *, unsigned int *)
  WARN_UNUSED_RESULT;
static bool read_uint32 (struct pcp_reader *, unsigned int *)
  WARN_UNUSED_RESULT;
static bool read_float (struct pcp_reader *, double *)
  WARN_UNUSED_RESULT;
static double parse_float (const uint8_t number[8]);
static bool read_string (struct pcp_reader *, char *, size_t)
  WARN_UNUSED_RESULT;
static bool skip_bytes (struct pcp_reader *, size_t) WARN_UNUSED_RESULT;

static bool pcp_seek (struct pcp_reader *, off_t);

static bool pcp_is_sysmis(const uint8_t *);

/* Dictionary reader. */

static bool read_dictionary (struct pcp_reader *);
static bool read_main_header (struct pcp_reader *, struct pcp_main_header *);
static void parse_header (struct pcp_reader *,
                          const struct pcp_main_header *,
                          struct any_read_info *, struct dictionary *);
static bool parse_variable_records (struct pcp_reader *, struct dictionary *,
                                    struct pcp_var_record *, size_t n);

/* Tries to open FH for reading as an SPSS/PC+ system file.  Returns a
   pcp_reader if successful, otherwise NULL. */
static struct any_reader *
pcp_open (struct file_handle *fh)
{
  struct pcp_reader *r;
  struct stat s;

  /* Create and initialize reader. */
  r = xzalloc (sizeof *r);
  r->any_reader.klass = &pcp_file_reader_class;
  r->pool = pool_create ();
  pool_register (r->pool, free, r);
  r->fh = fh_ref (fh);
  r->opcode_idx = sizeof r->opcodes;

  /* TRANSLATORS: this fragment will be interpolated into
     messages in fh_lock() that identify types of files. */
  r->lock = fh_lock (fh, FH_REF_FILE, N_("SPSS/PC+ system file"),
                     FH_ACC_READ, false);
  if (r->lock == NULL)
    goto error;

  /* Open file. */
  r->file = fn_open (fh_get_file_name (fh), "rb");
  if (r->file == NULL)
    {
      msg (ME, _("Error opening `%s' for reading as an SPSS/PC+ "
                 "system file: %s."),
           fh_get_file_name (r->fh), strerror (errno));
      goto error;
    }

  /* Fetch file size. */
  if (fstat (fileno (r->file), &s))
    {
      pcp_error (ME, 0, _("%s: stat failed (%s)."),
                 fh_get_file_name (r->fh), strerror (errno));
      goto error;
    }
  if (s.st_size > UINT_MAX)
    {
      pcp_error (ME, 0, _("%s: file too large."), fh_get_file_name (r->fh));
      goto error;
    }
  r->file_size = s.st_size;

  /* Read dictionary. */
  if (!read_dictionary (r))
    goto error;

  if (!pcp_seek (r, r->directory.data.ofs))
    goto error;

  return &r->any_reader;

error:
  pcp_close (&r->any_reader);
  return NULL;
}

static bool
pcp_read_dir_entry (struct pcp_reader *r, struct pcp_dir_entry *de)
{
  if (!read_uint32 (r, &de->ofs) || !read_uint32 (r, &de->len))
    return false;

  if (de->len > r->file_size || de->ofs > r->file_size - de->len)
    {
      pcp_error (r, r->pos - 8, _("Directory entry is for a %u-byte record "
                                  "starting at offset %u but file is only "
                                  "%u bytes long."),
                 de->ofs, de->len, r->file_size);
      return false;
    }

  return true;
}

static bool
read_dictionary (struct pcp_reader *r)
{
  unsigned int two, zero;

  if (!read_uint32 (r, &two) || !read_uint32 (r, &zero))
    return false;
  if (two != 2 || zero != 0)
    pcp_warn (r, 0, _("Directory fields have unexpected values "
                      "(%u,%u)."), two, zero);

  if (!pcp_read_dir_entry (r, &r->directory.main)
      || !pcp_read_dir_entry (r, &r->directory.variables)
      || !pcp_read_dir_entry (r, &r->directory.labels)
      || !pcp_read_dir_entry (r, &r->directory.data))
    return false;

  if (!read_main_header (r, &r->header))
    return false;

  read_variables_record (r);

  return true;
}

struct get_strings_aux
  {
    struct pool *pool;
    char **titles;
    char **strings;
    bool *ids;
    size_t allocated;
    size_t n;
  };

static void
add_string__ (struct get_strings_aux *aux,
              const char *string, bool id, char *title)
{
  if (aux->n >= aux->allocated)
    {
      aux->allocated = 2 * (aux->allocated + 1);
      aux->titles = pool_realloc (aux->pool, aux->titles,
                                  aux->allocated * sizeof *aux->titles);
      aux->strings = pool_realloc (aux->pool, aux->strings,
                                   aux->allocated * sizeof *aux->strings);
      aux->ids = pool_realloc (aux->pool, aux->ids,
                               aux->allocated * sizeof *aux->ids);
    }

  aux->titles[aux->n] = title;
  aux->strings[aux->n] = pool_strdup (aux->pool, string);
  aux->ids[aux->n] = id;
  aux->n++;
}

static void PRINTF_FORMAT (3, 4)
add_string (struct get_strings_aux *aux,
            const char *string, const char *title, ...)
{
  va_list args;

  va_start (args, title);
  add_string__ (aux, string, false, pool_vasprintf (aux->pool, title, args));
  va_end (args);
}

static void PRINTF_FORMAT (3, 4)
add_id (struct get_strings_aux *aux, const char *id, const char *title, ...)
{
  va_list args;

  va_start (args, title);
  add_string__ (aux, id, true, pool_vasprintf (aux->pool, title, args));
  va_end (args);
}

/* Retrieves significant string data from R in its raw format, to allow the
   caller to try to detect the encoding in use.

   Returns the number of strings retrieved N.  Sets each of *TITLESP, *IDSP,
   and *STRINGSP to an array of N elements allocated from POOL.  For each I in
   0...N-1, UTF-8 string *TITLESP[I] describes *STRINGSP[I], which is in
   whatever encoding system file R uses.  *IDS[I] is true if *STRINGSP[I] must
   be a valid PSPP language identifier, false if *STRINGSP[I] is free-form
   text. */
static size_t
pcp_get_strings (const struct any_reader *r_, struct pool *pool,
                 char ***titlesp, bool **idsp, char ***stringsp)
{
  struct pcp_reader *r = pcp_reader_cast (r_);
  struct get_strings_aux aux;
  size_t var_idx;
  size_t i, j;

  aux.pool = pool;
  aux.titles = NULL;
  aux.strings = NULL;
  aux.ids = NULL;
  aux.allocated = 0;
  aux.n = 0;

  var_idx = 0;
  for (i = 0; i < r->n_vars; i++)
    if (r->vars[i].width != -1)
      add_id (&aux, r->vars[i].name, _("Variable %zu"), ++var_idx);

  var_idx = 0;
  for (i = 0; i < r->n_vars; i++)
    if (r->vars[i].width != -1)
      {
        var_idx++;
        if (r->vars[i].label)
          add_string (&aux, r->vars[i].label, _("Variable %zu Label"),
                      var_idx);

        for (j = 0; j < r->vars[i].n_val_labs; j++)
          add_string (&aux, r->vars[i].label,
                      _("Variable %zu Value Label %zu"), var_idx, j);
      }

  add_string (&aux, r->header.creation_date, _("Creation Date"));
  add_string (&aux, r->header.creation_time, _("Creation Time"));
  add_string (&aux, r->header.product, _("Product"));
  add_string (&aux, r->header.file_label, _("File Label"));

  *titlesp = aux.titles;
  *idsp = aux.ids;
  *stringsp = aux.strings;
  return aux.n;
}

static void
find_and_delete_var (struct dictionary *dict, const char *name)
{
  struct variable *var = dict_lookup_var (dict, name);
  if (var)
    dict_delete_var (dict, var);
}

/* Decodes the dictionary read from R, saving it into into *DICT.  Character
   strings in R are decoded using ENCODING, or an encoding obtained from R if
   ENCODING is null, or the locale encoding if R specifies no encoding.

   If INFOP is non-null, then it receives additional info about the system
   file, which the caller must eventually free with any_read_info_destroy()
   when it is no longer needed.

   This function consumes R.  The caller must use it again later, even to
   destroy it with pcp_close(). */
static struct casereader *
pcp_decode (struct any_reader *r_, const char *encoding,
            struct dictionary **dictp, struct any_read_info *infop)
{
  struct pcp_reader *r = pcp_reader_cast (r_);
  struct dictionary *dict;

  if (encoding == NULL)
    {
      encoding = locale_charset ();
      pcp_warn (r, -1, _("Using default encoding %s to read this SPSS/PC+ "
                         "system file.  For best results, specify an "
                         "encoding explicitly.  Use SYSFILE INFO with "
                         "ENCODING=\"DETECT\" to analyze the possible "
                         "encodings."),
                encoding);
    }

  dict = dict_create (encoding);
  r->encoding = dict_get_encoding (dict);

  parse_header (r, &r->header, &r->info, dict);
  if (!parse_variable_records (r, dict, r->vars, r->n_vars))
    goto error;

  /* Create an index of dictionary variable widths for
     pcp_read_case to use.  We cannot use the `struct variable's
     from the dictionary we created, because the caller owns the
     dictionary and may destroy or modify its variables. */
  r->proto = caseproto_ref_pool (dict_get_proto (dict), r->pool);

  find_and_delete_var (dict, "CASENUM_");
  find_and_delete_var (dict, "DATE_");
  find_and_delete_var (dict, "WEIGHT_");

  *dictp = dict;
  if (infop)
    {
      *infop = r->info;
      memset (&r->info, 0, sizeof r->info);
    }

  return casereader_create_sequential
    (NULL, r->proto, r->n_cases, &pcp_file_casereader_class, r);

error:
  pcp_close (&r->any_reader);
  dict_destroy (dict);
  *dictp = NULL;
  return NULL;
}

/* Closes R, which should have been returned by pcp_open() but not already
   closed with pcp_decode() or this function.
   Returns true if an I/O error has occurred on READER, false
   otherwise. */
static bool
pcp_close (struct any_reader *r_)
{
  struct pcp_reader *r = pcp_reader_cast (r_);
  bool error;

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

  any_read_info_destroy (&r->info);
  fh_unlock (r->lock);
  fh_unref (r->fh);

  error = r->error;
  pool_destroy (r->pool);

  return !error;
}

/* Destroys READER. */
static void
pcp_file_casereader_destroy (struct casereader *reader UNUSED, void *r_)
{
  struct pcp_reader *r = r_;
  pcp_close (&r->any_reader);
}

/* Returns true if FILE is an SPSS/PC+ system file,
   false otherwise. */
static int
pcp_detect (FILE *file)
{
  static const char signature[4] = "SPSS";
  char buf[sizeof signature];

  if (fseek (file, 0x104, SEEK_SET)
      || (fread (buf, sizeof buf, 1, file) != 1 && !feof (file)))
    return -errno;

  return !memcmp (buf, signature, sizeof buf);
}

/* Reads the main header of the SPSS/PC+ system file.  Initializes *HEADER and
   *INFO, except for the string fields in *INFO, which parse_header() will
   initialize later once the file's encoding is known. */
static bool
read_main_header (struct pcp_reader *r, struct pcp_main_header *header)
{
  unsigned int base_ofs = r->directory.main.ofs;
  size_t min_values, min_data_size;
  unsigned int zero0, zero1, zero2;
  unsigned int one0, one1;
  unsigned int compressed;
  unsigned int n_cases1;
  uint8_t sysmis[8];

  if (!pcp_seek (r, base_ofs))
    return false;

  if (r->directory.main.len < 0xb0)
    {
      pcp_error (r, r->pos, _("This is not an SPSS/PC+ system file."));
      return false;
    }
  else if (r->directory.main.len > 0xb0)
    pcp_warn (r, r->pos, _("Record 0 has unexpected length %u."),
              r->directory.main.len);

  if (!read_uint16 (r, &one0)
      || !read_string (r, header->product, sizeof header->product)
      || !read_bytes (r, sysmis, sizeof sysmis)
      || !read_uint32 (r, &zero0)
      || !read_uint32 (r, &zero1)
      || !read_uint16 (r, &one1)
      || !read_uint16 (r, &compressed)
      || !read_uint16 (r, &header->nominal_case_size)
      || !read_uint32 (r, &r->n_cases)
      || !read_uint16 (r, &zero2)
      || !read_uint32 (r, &n_cases1)
      || !read_string (r, header->creation_date, sizeof header->creation_date)
      || !read_string (r, header->creation_time, sizeof header->creation_time)
      || !read_string (r, header->file_label, sizeof header->file_label))
    return false;

  if (!pcp_is_sysmis (sysmis))
    {
      double d = parse_float (sysmis);
      pcp_warn (r, base_ofs, _("Record 0 specifies unexpected system missing "
                               "value %g (%a)."), d, d);
    }
  if (one0 != 1 || one1 != 1 || zero0 != 0 || zero1 != 0 || zero2 != 0)
    pcp_warn (r, base_ofs, _("Record 0 reserved fields have unexpected values "
                             "(%u,%u,%u,%u,%u)."),
              one0, one1, zero0, zero1, zero2);
  if (n_cases1 != r->n_cases)
    pcp_warn (r, base_ofs, _("Record 0 case counts differ (%u versus %u)."),
              r->n_cases, n_cases1);
  if (compressed != 0 && compressed != 1)
    {
      pcp_error (r, base_ofs, _("Invalid compression type %u."), compressed);
      return false;
    }

  r->compressed = compressed != 0;

  min_values = xtimes (header->nominal_case_size, r->n_cases);
  min_data_size = xtimes (compressed ? 1 : 8, min_values);
  if (r->directory.data.len < min_data_size
      || size_overflow_p (min_data_size))
    {
      pcp_warn (r, base_ofs, _("Record 0 claims %u cases with %u values per "
                               "case (requiring at least %zu bytes) but data "
                               "record is only %u bytes long."),
                r->n_cases, header->nominal_case_size, min_data_size,
                r->directory.data.len);
      return true;
    }

  return true;
}

static bool
read_value_labels (struct pcp_reader *r, struct pcp_var_record *var,
                   unsigned int start, unsigned int end)
{
  size_t allocated_val_labs = 0;

  start += 7;
  end += 7;
  if (end > r->directory.labels.len)
    {
      pcp_warn (r, r->pos - 32,
                _("Value labels claimed to end at offset %u in labels record "
                  "but labels record is only %u bytes."),
                end, r->directory.labels.len);
      return true;
    }

  start += r->directory.labels.ofs;
  end += r->directory.labels.ofs;
  if (start > end || end > r->file_size)
    {
      pcp_warn (r, r->pos - 32,
                _("Value labels claimed to be at offset %u with length %u "
                  "but file size is only %u bytes."),
                start, end - start, r->file_size);
      return true;
    }

  if (!pcp_seek (r, start))
    return false;

  while (r->pos < end && end - r->pos > 8)
    {
      struct pcp_value_label *vl;
      uint8_t len;

      if (var->n_val_labs >= allocated_val_labs)
        var->val_labs = x2nrealloc (var->val_labs, &allocated_val_labs,
                                    sizeof *var->val_labs);
      vl = &var->val_labs[var->n_val_labs];

      if (!read_bytes (r, vl->value, sizeof vl->value)
          || !read_bytes (r, &len, 1))
        return false;

      if (end - r->pos < len)
        {
          pcp_warn (r, r->pos,
                    _("Value labels end with partial label (%u bytes left in "
                      "record, label length %"PRIu8")."),
                    end - r->pos, len);
          return true;
        }
      vl->label = pool_malloc (r->pool, len + 1);
      if (!read_bytes (r, vl->label, len))
        return false;

      vl->label[len] = '\0';
      var->n_val_labs++;
    }
  if (r->pos < end)
    pcp_warn (r, r->pos, _("%u leftover bytes following value labels."),
              end - r->pos);

  return true;
}

static bool
read_var_label (struct pcp_reader *r, struct pcp_var_record *var,
                unsigned int ofs)
{
  uint8_t len;

  ofs += 7;
  if (ofs >= r->directory.labels.len)
    {
      pcp_warn (r, r->pos - 32,
                _("Variable label claimed to start at offset %u in labels "
                  "record but labels record is only %u bytes."),
                ofs, r->directory.labels.len);
      return true;
    }

  if (!pcp_seek (r, ofs + r->directory.labels.ofs) || !read_bytes (r, &len, 1))
    return false;

  if (len >= r->directory.labels.len - ofs)
    {
      pcp_warn (r, r->pos - 1,
                _("Variable label with length %u starting at offset %u in "
                  "labels record overruns end of %u-byte labels record."),
                len, ofs + 1, r->directory.labels.len);
      return false;
    }

  var->label = pool_malloc (r->pool, len + 1);
  var->label[len] = '\0';
  return read_bytes (r, var->label, len);
}

/* Reads the variables record (record 1) into R. */
static bool
read_variables_record (struct pcp_reader *r)
{
  unsigned int i;

  if (!pcp_seek (r, r->directory.variables.ofs))
    return false;
  if (r->directory.variables.len != r->header.nominal_case_size * 32)
    {
      pcp_error (r, r->pos, _("Record 1 has length %u (expected %u)."),
                 r->directory.variables.len, r->header.nominal_case_size * 32);
      return false;
    }

  r->vars = pool_calloc (r->pool,
                         r->header.nominal_case_size, sizeof *r->vars);
  for (i = 0; i < r->header.nominal_case_size; i++)
    {
      struct pcp_var_record *var = &r->vars[r->n_vars++];
      unsigned int value_label_start, value_label_end;
      unsigned int var_label_ofs;
      unsigned int format;
      uint8_t raw_type;

      var->pos = r->pos;
      if (!read_uint32 (r, &value_label_start)
          || !read_uint32 (r, &value_label_end)
          || !read_uint32 (r, &var_label_ofs)
          || !read_uint32 (r, &format)
          || !read_string (r, var->name, sizeof var->name)
          || !read_bytes (r, var->missing, sizeof var->missing))
        return false;

      raw_type = format >> 16;
      if (!fmt_from_io (raw_type, &var->format.type))
        {
          pcp_error (r, var->pos, _("Variable %u has invalid type %"PRIu8"."),
                     i, raw_type);
          return false;
        }

      var->format.w = (format >> 8) & 0xff;
      var->format.d = format & 0xff;
      fmt_fix_output (&var->format);
      var->width = fmt_var_width (&var->format);

      if (var_label_ofs)
        {
          unsigned int save_pos = r->pos;
          if (!read_var_label (r, var, var_label_ofs)
              || !pcp_seek (r, save_pos))
            return false;
        }

      if (value_label_end > value_label_start && var->width <= 8)
        {
          unsigned int save_pos = r->pos;
          if (!read_value_labels (r, var, value_label_start, value_label_end)
              || !pcp_seek (r, save_pos))
            return false;
        }

      if (var->width > 8)
        {
          int extra = DIV_RND_UP (var->width - 8, 8);
          i += extra;
          if (!skip_bytes (r, 32 * extra))
            return false;
        }
    }

  return true;
}

static char *
recode_and_trim_string (struct pool *pool, const char *from, const char *in)
{
  struct substring out;

  out = recode_substring_pool ("UTF-8", from, ss_cstr (in), pool);
  ss_trim (&out, ss_cstr (" "));
  return ss_xstrdup (out);
}

static void
parse_header (struct pcp_reader *r, const struct pcp_main_header *header,
              struct any_read_info *info, struct dictionary *dict)
{
  const char *dict_encoding = dict_get_encoding (dict);
  char *label;

  memset (info, 0, sizeof *info);

  info->integer_format = INTEGER_LSB_FIRST;
  info->float_format = FLOAT_IEEE_DOUBLE_LE;
  info->compression = r->compressed ? ANY_COMP_SIMPLE : ANY_COMP_NONE;
  info->case_cnt = r->n_cases;

  /* Convert file label to UTF-8 and put it into DICT. */
  label = recode_and_trim_string (r->pool, dict_encoding, header->file_label);
  dict_set_label (dict, label);
  free (label);

  /* Put creation date, time, and product in UTF-8 into INFO. */
  info->creation_date = recode_and_trim_string (r->pool, dict_encoding,
                                                header->creation_date);
  info->creation_time = recode_and_trim_string (r->pool, dict_encoding,
                                                header->creation_time);
  info->product = recode_and_trim_string (r->pool, dict_encoding,
                                          header->product);
}

/* Reads a variable (type 2) record from R and adds the
   corresponding variable to DICT.
   Also skips past additional variable records for long string
   variables. */
static bool
parse_variable_records (struct pcp_reader *r, struct dictionary *dict,
                        struct pcp_var_record *var_recs, size_t n_var_recs)
{
  const char *dict_encoding = dict_get_encoding (dict);
  struct pcp_var_record *rec;

  for (rec = var_recs; rec < &var_recs[n_var_recs]; rec++)
    {
      struct variable *var;
      bool weight;
      char *name;
      size_t i;

      name = recode_string_pool ("UTF-8", dict_encoding,
                                 rec->name, -1, r->pool);
      name[strcspn (name, " ")] = '\0';
      weight = !strcmp (name, "$WEIGHT") && rec->width == 0;

      /* Transform $DATE => DATE_, $WEIGHT => WEIGHT_, $CASENUM => CASENUM_. */
      if (name[0] == '$')
        name = pool_asprintf (r->pool, "%s_", name + 1);

      if (!dict_id_is_valid (dict, name, false) || name[0] == '#')
        {
          pcp_error (r, rec->pos, _("Invalid variable name `%s'."), name);
          return false;
        }

      var = rec->var = dict_create_var (dict, name, rec->width);
      if (var == NULL)
        {
          char *new_name = dict_make_unique_var_name (dict, NULL, NULL);
          pcp_warn (r, rec->pos, _("Renaming variable with duplicate name "
                                   "`%s' to `%s'."),
                    name, new_name);
          var = rec->var = dict_create_var_assert (dict, new_name, rec->width);
          free (new_name);
        }
      if (weight)
        dict_set_weight (dict, var);

      /* Set the short name the same as the long name. */
      var_set_short_name (var, 0, name);

      /* Get variable label, if any. */
      if (rec->label)
        {
          char *utf8_label;

          utf8_label = recode_string ("UTF-8", dict_encoding, rec->label, -1);
          var_set_label (var, utf8_label);
          free (utf8_label);
        }

      /* Add value labels. */
      for (i = 0; i < rec->n_val_labs; i++)
        {
          union value value;
          char *utf8_label;

          value_init (&value, rec->width);
          if (var_is_numeric (var))
            value.f = parse_float (rec->val_labs[i].value);
          else
            memcpy (value_str_rw (&value, rec->width),
                    rec->val_labs[i].value, rec->width);

          utf8_label = recode_string ("UTF-8", dict_encoding,
                                      rec->val_labs[i].label, -1);
          var_add_value_label (var, &value, utf8_label);
          free (utf8_label);

          value_destroy (&value, rec->width);
        }

      /* Set missing values. */
      if (rec->width <= 8 && !pcp_is_sysmis (rec->missing))
        {
          int width = var_get_width (var);
          struct missing_values mv;

          mv_init_pool (r->pool, &mv, width);
          if (var_is_numeric (var))
            mv_add_num (&mv, parse_float (rec->missing));
          else
            mv_add_str (&mv, rec->missing, MIN (width, 8));
          var_set_missing_values (var, &mv);
        }

      /* Set formats. */
      var_set_both_formats (var, &rec->format);
    }

  return true;
}

/* Case reader. */

static void read_error (struct casereader *, const struct pcp_reader *);

static bool read_case_number (struct pcp_reader *, double *);
static int read_case_string (struct pcp_reader *, uint8_t *, size_t);
static int read_opcode (struct pcp_reader *);
static bool read_compressed_number (struct pcp_reader *, double *);
static int read_compressed_string (struct pcp_reader *, uint8_t *);
static int read_whole_strings (struct pcp_reader *, uint8_t *, size_t);

/* Reads and returns one case from READER's file.  Returns a null
   pointer if not successful. */
static struct ccase *
pcp_file_casereader_read (struct casereader *reader, void *r_)
{
  struct pcp_reader *r = r_;
  unsigned int start_pos = r->pos;
  struct ccase *c;
  int retval;
  int i;

  if (r->error || !r->n_cases)
    return NULL;
  r->n_cases--;

  c = case_create (r->proto);
  for (i = 0; i < r->n_vars; i++)
    {
      struct pcp_var_record *var = &r->vars[i];
      union value *v = case_data_rw_idx (c, i);

      if (var->width == 0)
        retval = read_case_number (r, &v->f);
      else
        retval = read_case_string (r, value_str_rw (v, var->width),
                                   var->width);

      if (retval != 1)
        {
          pcp_error (r, r->pos, _("File ends in partial case."));
          goto error;
        }
    }
  if (r->pos > r->directory.data.ofs + r->directory.data.len)
    {
      pcp_error (r, r->pos, _("Case beginning at offset 0x%08x extends past "
                              "end of data record at offset 0x%08x."),
                 start_pos, r->directory.data.ofs + r->directory.data.len);
      goto error;
    }

  return c;

error:
  read_error (reader, r);
  case_unref (c);
  return NULL;
}

/* Issues an error that an unspecified error occurred PCP, and
   marks R tainted. */
static void
read_error (struct casereader *r, const struct pcp_reader *pcp)
{
  msg (ME, _("Error reading case from file %s."), fh_get_name (pcp->fh));
  casereader_force_error (r);
}

/* Reads a number from R and stores its value in *D.
   If R is compressed, reads a compressed number;
   otherwise, reads a number in the regular way.
   Returns true if successful, false if end of file is
   reached immediately. */
static bool
read_case_number (struct pcp_reader *r, double *d)
{
  if (!r->compressed)
    {
      uint8_t number[8];
      if (!try_read_bytes (r, number, sizeof number))
        return false;
      *d = parse_float (number);
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
read_case_string (struct pcp_reader *r, uint8_t *s, size_t length)
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
      if (retval <= 0)
        return -1;
      memcpy (s + whole, bounce, partial);
    }

  return 1;
}

/* Reads and returns the next compression opcode from R. */
static int
read_opcode (struct pcp_reader *r)
{
  assert (r->compressed);
  if (r->opcode_idx >= sizeof r->opcodes)
    {
      int retval = try_read_bytes (r, r->opcodes, sizeof r->opcodes);
      if (retval != 1)
        return -1;
      r->opcode_idx = 0;
    }
  return r->opcodes[r->opcode_idx++];
}

/* Reads a compressed number from R and stores its value in D.
   Returns true if successful, false if end of file is
   reached immediately. */
static bool
read_compressed_number (struct pcp_reader *r, double *d)
{
  int opcode = read_opcode (r);
  switch (opcode)
    {
    case -1:
      return false;

    case 0:
      *d = SYSMIS;
      return true;

    case 1:
      return read_float (r, d);

    default:
      *d = opcode - 105.0;
      return true;
    }
}

/* Reads a compressed 8-byte string segment from R and stores it in DST. */
static int
read_compressed_string (struct pcp_reader *r, uint8_t *dst)
{
  int opcode;
  int retval;

  opcode = read_opcode (r);
  switch (opcode)
    {
    case -1:
      return 0;

    case 1:
      retval = read_bytes (r, dst, 8);
      return retval == 1 ? 1 : -1;

    default:
      if (!r->corruption_warning)
        {
          r->corruption_warning = true;
          pcp_warn (r, r->pos,
                    _("Possible compressed data corruption: "
                      "string contains compressed integer (opcode %d)."),
                    opcode);
      }
      memset (dst, ' ', 8);
      return 1;
    }
}

/* Reads LENGTH string bytes from R into S.  LENGTH must be a multiple of 8.
   Reads compressed strings if S is compressed.  Returns 1 if successful, 0 if
   end of file is reached immediately, or -1 for some kind of error. */
static int
read_whole_strings (struct pcp_reader *r, uint8_t *s, size_t length)
{
  assert (length % 8 == 0);
  if (!r->compressed)
    return try_read_bytes (r, s, length);
  else
    {
      size_t ofs;

      for (ofs = 0; ofs < length; ofs += 8)
        {
          int retval = read_compressed_string (r, s + ofs);
          if (retval != 1)
            return -1;
          }
      return 1;
    }
}

/* Messages. */

/* Displays a corruption message. */
static void
pcp_msg (struct pcp_reader *r, off_t offset,
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
pcp_warn (struct pcp_reader *r, off_t offset, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  pcp_msg (r, offset, MW, format, args);
  va_end (args);
}

/* Displays an error for the current file position,
   marks it as in an error state,
   and aborts reading it using longjmp. */
static void
pcp_error (struct pcp_reader *r, off_t offset, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  pcp_msg (r, offset, ME, format, args);
  va_end (args);

  r->error = true;
}

/* Reads BYTE_CNT bytes into BUF.
   Returns 1 if exactly BYTE_CNT bytes are successfully read.
   Returns -1 if an I/O error or a partial read occurs.
   Returns 0 for an immediate end-of-file and, if EOF_IS_OK is false, reports
   an error. */
static inline int
read_bytes_internal (struct pcp_reader *r, bool eof_is_ok,
                     void *buf, size_t byte_cnt)
{
  size_t bytes_read = fread (buf, 1, byte_cnt, r->file);
  r->pos += bytes_read;
  if (bytes_read == byte_cnt)
    return 1;
  else if (ferror (r->file))
    {
      pcp_error (r, r->pos, _("System error: %s."), strerror (errno));
      return -1;
    }
  else if (!eof_is_ok || bytes_read != 0)
    {
      pcp_error (r, r->pos, _("Unexpected end of file."));
      return -1;
    }
  else
    return 0;
}

/* Reads BYTE_CNT into BUF.
   Returns true if successful.
   Returns false upon I/O error or if end-of-file is encountered. */
static bool
read_bytes (struct pcp_reader *r, void *buf, size_t byte_cnt)
{
  return read_bytes_internal (r, false, buf, byte_cnt) == 1;
}

/* Reads BYTE_CNT bytes into BUF.
   Returns 1 if exactly BYTE_CNT bytes are successfully read.
   Returns 0 if an immediate end-of-file is encountered.
   Returns -1 if an I/O error or a partial read occurs. */
static int
try_read_bytes (struct pcp_reader *r, void *buf, size_t byte_cnt)
{
  return read_bytes_internal (r, true, buf, byte_cnt);
}

/* Reads a 16-bit signed integer from R and stores its value in host format in
   *X.  Returns true if successful, otherwise false. */
static bool
read_uint16 (struct pcp_reader *r, unsigned int *x)
{
  uint8_t integer[2];
  if (read_bytes (r, integer, sizeof integer) != 1)
    return false;
  *x = integer_get (INTEGER_LSB_FIRST, integer, sizeof integer);
  return true;
}

/* Reads a 32-bit signed integer from R and stores its value in host format in
   *X.  Returns true if successful, otherwise false. */
static bool
read_uint32 (struct pcp_reader *r, unsigned int *x)
{
  uint8_t integer[4];
  if (read_bytes (r, integer, sizeof integer) != 1)
    return false;
  *x = integer_get (INTEGER_LSB_FIRST, integer, sizeof integer);
  return true;
}

/* Reads exactly SIZE - 1 bytes into BUFFER
   and stores a null byte into BUFFER[SIZE - 1]. */
static bool
read_string (struct pcp_reader *r, char *buffer, size_t size)
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
skip_bytes (struct pcp_reader *r, size_t bytes)
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

static bool
pcp_seek (struct pcp_reader *r, off_t offset)
{
  if (fseeko (r->file, offset, SEEK_SET))
    {
      pcp_error (r, 0, _("%s: seek failed (%s)."),
                 fh_get_file_name (r->fh), strerror (errno));
      return false;
    }
  r->pos = offset;
  return true;
}

/* Reads a 64-bit floating-point number from R and returns its
   value in host format. */
static bool
read_float (struct pcp_reader *r, double *d)
{
  uint8_t number[8];

  if (!read_bytes (r, number, sizeof number))
    return false;
  else
    {
      *d = parse_float (number);
      return true;
    }
}

static double
parse_float (const uint8_t number[8])
{
  return (pcp_is_sysmis (number)
          ? SYSMIS
          : float_get_double (FLOAT_IEEE_DOUBLE_LE, number));
}

static bool
pcp_is_sysmis(const uint8_t *p)
{
  static const uint8_t sysmis[8]
    = { 0xf5, 0x1e, 0x26, 0x02, 0x8a, 0x8c, 0xed, 0xff };
  return !memcmp (p, sysmis, 8);
}

static const struct casereader_class pcp_file_casereader_class =
  {
    pcp_file_casereader_read,
    pcp_file_casereader_destroy,
    NULL,
    NULL,
  };

const struct any_reader_class pcp_file_reader_class =
  {
    N_("SPSS/PC+ System File"),
    pcp_detect,
    pcp_open,
    pcp_close,
    pcp_decode,
    pcp_get_strings,
  };
