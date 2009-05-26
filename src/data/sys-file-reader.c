/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009 Free Software Foundation, Inc.

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

#include <data/sys-file-reader.h>
#include <data/sys-file-private.h>

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdlib.h>

#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/compiler.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>
#include <libpspp/hash.h>
#include <libpspp/array.h>

#include <data/attributes.h>
#include <data/case.h>
#include <data/casereader-provider.h>
#include <data/casereader.h>
#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/file-name.h>
#include <data/format.h>
#include <data/missing-values.h>
#include <data/short-names.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <data/value.h>

#include "c-ctype.h"
#include "inttostr.h"
#include "minmax.h"
#include "unlocked-io.h"
#include "xalloc.h"
#include "xsize.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* System file reader. */
struct sfm_reader
  {
    /* Resource tracking. */
    struct pool *pool;          /* All system file state. */
    jmp_buf bail_out;           /* longjmp() target for error handling. */

    /* File state. */
    struct file_handle *fh;     /* File handle. */
    struct fh_lock *lock;       /* Mutual exclusion for file handle. */
    FILE *file;                 /* File stream. */
    bool error;                 /* I/O or corruption error? */
    struct caseproto *proto;    /* Format of output cases. */

    /* File format. */
    enum integer_format integer_format; /* On-disk integer format. */
    enum float_format float_format; /* On-disk floating point format. */
    int oct_cnt;		/* Number of 8-byte units per case. */
    struct sfm_var *sfm_vars;   /* Variables. */
    size_t sfm_var_cnt;         /* Number of variables. */
    casenumber case_cnt;        /* Number of cases */
    bool has_long_var_names;    /* File has a long variable name map */

    /* Decompression. */
    bool compressed;		/* File is compressed? */
    double bias;		/* Compression bias, usually 100.0. */
    uint8_t opcodes[8];         /* Current block of opcodes. */
    size_t opcode_idx;          /* Next opcode to interpret, 8 if none left. */
  };

static const struct casereader_class sys_file_casereader_class;

static bool close_reader (struct sfm_reader *);

static struct variable **make_var_by_value_idx (struct sfm_reader *,
                                                struct dictionary *);
static struct variable *lookup_var_by_value_idx (struct sfm_reader *,
                                                 struct variable **,
                                                 int value_idx);

static void sys_msg (struct sfm_reader *r, int class,
                     const char *format, va_list args)
     PRINTF_FORMAT (3, 0);
static void sys_warn (struct sfm_reader *, const char *, ...)
     PRINTF_FORMAT (2, 3);
static void sys_error (struct sfm_reader *, const char *, ...)
     PRINTF_FORMAT (2, 3)
     NO_RETURN;

static void read_bytes (struct sfm_reader *, void *, size_t);
static bool try_read_bytes (struct sfm_reader *, void *, size_t);
static int read_int (struct sfm_reader *);
static double read_float (struct sfm_reader *);
static void read_string (struct sfm_reader *, char *, size_t);
static void skip_bytes (struct sfm_reader *, size_t);

static struct text_record *open_text_record (struct sfm_reader *, size_t size);
static void close_text_record (struct sfm_reader *r,
                               struct text_record *);
static bool read_variable_to_value_pair (struct sfm_reader *,
                                         struct dictionary *,
                                         struct text_record *,
                                         struct variable **var, char **value);
static void text_warn (struct sfm_reader *r, struct text_record *text,
                       const char *format, ...)
  PRINTF_FORMAT (3, 4);
static char *text_get_token (struct text_record *,
                             struct substring delimiters);
static bool text_match (struct text_record *, char c);
static bool text_read_short_name (struct sfm_reader *, struct dictionary *,
                                  struct text_record *,
                                  struct substring delimiters,
                                  struct variable **);

static bool close_reader (struct sfm_reader *r);

/* Dictionary reader. */

enum which_format
  {
    PRINT_FORMAT,
    WRITE_FORMAT
  };

static void read_header (struct sfm_reader *, struct dictionary *,
                         int *weight_idx, int *claimed_oct_cnt,
                         struct sfm_read_info *);
static void read_variable_record (struct sfm_reader *, struct dictionary *,
                                  int *format_warning_cnt);
static void parse_format_spec (struct sfm_reader *, unsigned int,
                               enum which_format, struct variable *,
                               int *format_warning_cnt);
static void setup_weight (struct sfm_reader *, int weight_idx,
                          struct variable **var_by_value_idx,
                          struct dictionary *);
static void read_documents (struct sfm_reader *, struct dictionary *);
static void read_value_labels (struct sfm_reader *, struct dictionary *,
                               struct variable **var_by_value_idx);

static void read_extension_record (struct sfm_reader *, struct dictionary *,
                                   struct sfm_read_info *);
static void read_machine_integer_info (struct sfm_reader *,
                                       size_t size, size_t count,
                                       struct sfm_read_info *,
				       struct dictionary *
				       );
static void read_machine_float_info (struct sfm_reader *,
                                     size_t size, size_t count);
static void read_display_parameters (struct sfm_reader *,
                                     size_t size, size_t count,
                                     struct dictionary *);
static void read_long_var_name_map (struct sfm_reader *,
                                    size_t size, size_t count,
                                    struct dictionary *);
static void read_long_string_map (struct sfm_reader *,
                                  size_t size, size_t count,
                                  struct dictionary *);
static void read_data_file_attributes (struct sfm_reader *,
                                       size_t size, size_t count,
                                       struct dictionary *);
static void read_variable_attributes (struct sfm_reader *,
                                      size_t size, size_t count,
                                      struct dictionary *);
static void read_long_string_value_labels (struct sfm_reader *,
					   size_t size, size_t count,
					   struct dictionary *);

/* Opens the system file designated by file handle FH for
   reading.  Reads the system file's dictionary into *DICT.
   If INFO is non-null, then it receives additional info about the
   system file. */
struct casereader *
sfm_open_reader (struct file_handle *fh, struct dictionary **dict,
                 struct sfm_read_info *volatile info)
{
  struct sfm_reader *volatile r = NULL;
  struct variable **var_by_value_idx;
  struct sfm_read_info local_info;
  int format_warning_cnt = 0;
  int weight_idx;
  int claimed_oct_cnt;
  int rec_type;

  *dict = dict_create ();

  /* Create and initialize reader. */
  r = pool_create_container (struct sfm_reader, pool);
  r->fh = fh_ref (fh);
  r->lock = NULL;
  r->file = NULL;
  r->error = false;
  r->oct_cnt = 0;
  r->has_long_var_names = false;
  r->opcode_idx = sizeof r->opcodes;

  /* TRANSLATORS: this fragment will be interpolated into
     messages in fh_lock() that identify types of files. */
  r->lock = fh_lock (fh, FH_REF_FILE, N_("system file"), FH_ACC_READ, false);
  if (r->lock == NULL)
    goto error;

  r->file = fn_open (fh_get_file_name (fh), "rb");
  if (r->file == NULL)
    {
      msg (ME, _("Error opening \"%s\" for reading as a system file: %s."),
           fh_get_file_name (r->fh), strerror (errno));
      goto error;
    }

  /* Initialize info. */
  if (info == NULL)
    info = &local_info;
  memset (info, 0, sizeof *info);

  if (setjmp (r->bail_out))
    goto error;


  /* Read header. */
  read_header (r, *dict, &weight_idx, &claimed_oct_cnt, info);

  /* Read all the variable definition records. */
  rec_type = read_int (r);
  while (rec_type == 2)
    {
      read_variable_record (r, *dict, &format_warning_cnt);
      rec_type = read_int (r);
    }

  /* Figure out the case format. */
  var_by_value_idx = make_var_by_value_idx (r, *dict);
  setup_weight (r, weight_idx, var_by_value_idx, *dict);

  /* Read all the rest of the dictionary records. */
  while (rec_type != 999)
    {
      switch (rec_type)
        {
        case 3:
          read_value_labels (r, *dict, var_by_value_idx);
          break;

        case 4:
          sys_error (r, _("Misplaced type 4 record."));

        case 6:
          read_documents (r, *dict);
          break;

        case 7:
          read_extension_record (r, *dict, info);
          break;

        default:
          sys_error (r, _("Unrecognized record type %d."), rec_type);
        }
      rec_type = read_int (r);
    }


  if ( ! r->has_long_var_names )
    {
      int i;
      for (i = 0; i < dict_get_var_cnt (*dict); i++)
	{
	  struct variable *var = dict_get_var (*dict, i);
	  char short_name[SHORT_NAME_LEN + 1];
	  char long_name[SHORT_NAME_LEN + 1];

	  strcpy (short_name, var_get_name (var));

	  strcpy (long_name, short_name);
	  str_lowercase (long_name);

	  /* Set long name.  Renaming a variable may clear the short
	     name, but we want to retain it, so re-set it
	     explicitly. */
	  dict_rename_var (*dict, var, long_name);
	  var_set_short_name (var, 0, short_name);
	}

      r->has_long_var_names = true;
    }

  /* Read record 999 data, which is just filler. */
  read_int (r);

  /* Warn if the actual amount of data per case differs from the
     amount that the header claims.  SPSS version 13 gets this
     wrong when very long strings are involved, so don't warn in
     that case. */
  if (claimed_oct_cnt != -1 && claimed_oct_cnt != r->oct_cnt
      && info->version_major != 13)
    sys_warn (r, _("File header claims %d variable positions but "
                   "%d were read from file."),
              claimed_oct_cnt, r->oct_cnt);

  /* Create an index of dictionary variable widths for
     sfm_read_case to use.  We cannot use the `struct variable's
     from the dictionary we created, because the caller owns the
     dictionary and may destroy or modify its variables. */
  sfm_dictionary_to_sfm_vars (*dict, &r->sfm_vars, &r->sfm_var_cnt);
  pool_register (r->pool, free, r->sfm_vars);
  r->proto = caseproto_ref_pool (dict_get_proto (*dict), r->pool);

  pool_free (r->pool, var_by_value_idx);
  return casereader_create_sequential
    (NULL, r->proto,
     r->case_cnt == -1 ? CASENUMBER_MAX: r->case_cnt,
                                       &sys_file_casereader_class, r);

error:
  close_reader (r);
  dict_destroy (*dict);
  *dict = NULL;
  return NULL;
}

/* Closes a system file after we're done with it.
   Returns true if an I/O error has occurred on READER, false
   otherwise. */
static bool
close_reader (struct sfm_reader *r)
{
  bool error;

  if (r == NULL)
    return true;

  if (r->file)
    {
      if (fn_close (fh_get_file_name (r->fh), r->file) == EOF)
        {
          msg (ME, _("Error closing system file \"%s\": %s."),
               fh_get_file_name (r->fh), strerror (errno));
          r->error = true;
        }
      r->file = NULL;
    }

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
  close_reader (r);
}

/* Returns true if FILE is an SPSS system file,
   false otherwise. */
bool
sfm_detect (FILE *file)
{
  char rec_type[5];

  if (fread (rec_type, 4, 1, file) != 1)
    return false;
  rec_type[4] = '\0';

  return !strcmp ("$FL2", rec_type);
}

/* Reads the global header of the system file.
   Sets DICT's file label to the system file's label.
   Sets *WEIGHT_IDX to 0 if the system file is unweighted,
   or to the value index of the weight variable otherwise.
   Sets *CLAIMED_OCT_CNT to the number of "octs" (8-byte units)
   per case that the file claims to have (although it is not
   always correct).
   Initializes INFO with header information. */
static void
read_header (struct sfm_reader *r, struct dictionary *dict,
             int *weight_idx, int *claimed_oct_cnt,
             struct sfm_read_info *info)
{
  char rec_type[5];
  char eye_catcher[61];
  uint8_t raw_layout_code[4];
  uint8_t raw_bias[8];
  char creation_date[10];
  char creation_time[9];
  char file_label[65];
  struct substring file_label_ss;
  struct substring product;

  read_string (r, rec_type, sizeof rec_type);
  read_string (r, eye_catcher, sizeof eye_catcher);

  if (strcmp ("$FL2", rec_type) != 0)
    sys_error (r, _("This is not an SPSS system file."));

  /* Identify integer format. */
  read_bytes (r, raw_layout_code, sizeof raw_layout_code);
  if ((!integer_identify (2, raw_layout_code, sizeof raw_layout_code,
                          &r->integer_format)
       && !integer_identify (3, raw_layout_code, sizeof raw_layout_code,
                             &r->integer_format))
      || (r->integer_format != INTEGER_MSB_FIRST
          && r->integer_format != INTEGER_LSB_FIRST))
    sys_error (r, _("This is not an SPSS system file."));

  *claimed_oct_cnt = read_int (r);
  if (*claimed_oct_cnt < 0 || *claimed_oct_cnt > INT_MAX / 16)
    *claimed_oct_cnt = -1;

  r->compressed = read_int (r) != 0;

  *weight_idx = read_int (r);

  r->case_cnt = read_int (r);
  if ( r->case_cnt > INT_MAX / 2)
    r->case_cnt = -1;


  /* Identify floating-point format and obtain compression bias. */
  read_bytes (r, raw_bias, sizeof raw_bias);
  if (float_identify (100.0, raw_bias, sizeof raw_bias, &r->float_format) == 0)
    {
      sys_warn (r, _("Compression bias is not the usual "
                     "value of 100, or system file uses unrecognized "
                     "floating-point format."));
      if (r->integer_format == INTEGER_MSB_FIRST)
        r->float_format = FLOAT_IEEE_DOUBLE_BE;
      else
        r->float_format = FLOAT_IEEE_DOUBLE_LE;
    }
  float_convert (r->float_format, raw_bias, FLOAT_NATIVE_DOUBLE, &r->bias);

  read_string (r, creation_date, sizeof creation_date);
  read_string (r, creation_time, sizeof creation_time);
  read_string (r, file_label, sizeof file_label);
  skip_bytes (r, 3);

  file_label_ss = ss_cstr (file_label);
  ss_trim (&file_label_ss, ss_cstr (" "));
  if (!ss_is_empty (file_label_ss))
    {
      ss_data (file_label_ss)[ss_length (file_label_ss)] = '\0';
      dict_set_label (dict, ss_data (file_label_ss));
    }

  strcpy (info->creation_date, creation_date);
  strcpy (info->creation_time, creation_time);
  info->integer_format = r->integer_format;
  info->float_format = r->float_format;
  info->compressed = r->compressed;
  info->case_cnt = r->case_cnt;

  product = ss_cstr (eye_catcher);
  ss_match_string (&product, ss_cstr ("@(#) SPSS DATA FILE"));
  ss_trim (&product, ss_cstr (" "));
  str_copy_buf_trunc (info->product, sizeof info->product,
                      ss_data (product), ss_length (product));
}

/* Reads a variable (type 2) record from R and adds the
   corresponding variable to DICT.
   Also skips past additional variable records for long string
   variables. */
static void
read_variable_record (struct sfm_reader *r, struct dictionary *dict,
                      int *format_warning_cnt)
{
  int width;
  int has_variable_label;
  int missing_value_code;
  int print_format;
  int write_format;
  char name[9];

  struct variable *var;
  int nv;

  width = read_int (r);
  has_variable_label = read_int (r);
  missing_value_code = read_int (r);
  print_format = read_int (r);
  write_format = read_int (r);
  read_string (r, name, sizeof name);
  name[strcspn (name, " ")] = '\0';

  /* Check variable name. */
  if (name[0] == '$' || name[0] == '#')
    sys_error (r, "Variable name begins with invalid character `%c'.",
               name[0]);
  if (!var_is_plausible_name (name, false))
    sys_error (r, _("Invalid variable name `%s'."), name);

  /* Create variable. */
  if (width < 0 || width > 255)
    sys_error (r, _("Bad variable width %d."), width);
  var = dict_create_var (dict, name, width);
  if (var == NULL)
    sys_error (r,
               _("Duplicate variable name `%s' within system file."),
               name);

  /* Set the short name the same as the long name. */
  var_set_short_name (var, 0, var_get_name (var));

  /* Get variable label, if any. */
  if (has_variable_label != 0 && has_variable_label != 1)
    sys_error (r, _("Variable label indicator field is not 0 or 1."));
  if (has_variable_label == 1)
    {
      size_t len;
      char label[255 + 1];

      len = read_int (r);
      if (len >= sizeof label)
        sys_error (r, _("Variable %s has label of invalid length %zu."),
                   name, len);
      read_string (r, label, len + 1);
      var_set_label (var, label);

      skip_bytes (r, ROUND_UP (len, 4) - len);
    }

  /* Set missing values. */
  if (missing_value_code != 0)
    {
      struct missing_values mv;
      int i;

      mv_init_pool (r->pool, &mv, var_get_width (var));
      if (var_is_numeric (var))
        {
          if (missing_value_code < -3 || missing_value_code > 3
              || missing_value_code == -1)
            sys_error (r, _("Numeric missing value indicator field is not "
                            "-3, -2, 0, 1, 2, or 3."));
          if (missing_value_code < 0)
            {
              double low = read_float (r);
              double high = read_float (r);
              mv_add_range (&mv, low, high);
              missing_value_code = -missing_value_code - 2;
            }
          for (i = 0; i < missing_value_code; i++)
            mv_add_num (&mv, read_float (r));
        }
      else
        {
          int mv_width = MAX (width, 8);
          union value value;

          if (missing_value_code < 1 || missing_value_code > 3)
            sys_error (r, _("String missing value indicator field is not "
                            "0, 1, 2, or 3."));

          value_init (&value, mv_width);
          value_set_missing (&value, mv_width);
          for (i = 0; i < missing_value_code; i++)
            {
              char *s = value_str_rw (&value, mv_width);
              read_bytes (r, s, 8);
              mv_add_str (&mv, s);
            }
          value_destroy (&value, mv_width);
        }
      var_set_missing_values (var, &mv);
    }

  /* Set formats. */
  parse_format_spec (r, print_format, PRINT_FORMAT, var, format_warning_cnt);
  parse_format_spec (r, write_format, WRITE_FORMAT, var, format_warning_cnt);

  /* Account for values.
     Skip long string continuation records, if any. */
  nv = width == 0 ? 1 : DIV_RND_UP (width, 8);
  r->oct_cnt += nv;
  if (width > 8)
    {
      int i;

      for (i = 1; i < nv; i++)
        {
          /* Check for record type 2 and width -1. */
          if (read_int (r) != 2 || read_int (r) != -1)
            sys_error (r, _("Missing string continuation record."));

          /* Skip and ignore remaining continuation data. */
          has_variable_label = read_int (r);
          missing_value_code = read_int (r);
          print_format = read_int (r);
          write_format = read_int (r);
          read_string (r, name, sizeof name);

          /* Variable label fields on continuation records have
             been spotted in system files created by "SPSS Power
             Macintosh Release 6.1". */
          if (has_variable_label)
            skip_bytes (r, ROUND_UP (read_int (r), 4));
        }
    }
}

/* Translates the format spec from sysfile format to internal
   format. */
static void
parse_format_spec (struct sfm_reader *r, unsigned int s,
                   enum which_format which, struct variable *v,
                   int *format_warning_cnt)
{
  const int max_format_warnings = 8;
  struct fmt_spec f;
  uint8_t raw_type = s >> 16;
  uint8_t w = s >> 8;
  uint8_t d = s;

  bool ok;

  if (!fmt_from_io (raw_type, &f.type))
    sys_error (r, _("Unknown variable format %"PRIu8"."), raw_type);
  f.w = w;
  f.d = d;

  msg_disable ();
  ok = fmt_check_output (&f) && fmt_check_width_compat (&f, var_get_width (v));
  msg_enable ();

  if (ok)
    {
      if (which == PRINT_FORMAT)
        var_set_print_format (v, &f);
      else
        var_set_write_format (v, &f);
    }
  else if (*++format_warning_cnt <= max_format_warnings)
    {
      char fmt_string[FMT_STRING_LEN_MAX + 1];
      sys_warn (r, _("%s variable %s has invalid %s format %s."),
                var_is_numeric (v) ? _("Numeric") : _("String"),
                var_get_name (v),
                which == PRINT_FORMAT ? _("print") : _("write"),
                fmt_to_string (&f, fmt_string));

      if (*format_warning_cnt == max_format_warnings)
        sys_warn (r, _("Suppressing further invalid format warnings."));
    }
}

/* Sets the weighting variable in DICT to the variable
   corresponding to the given 1-based VALUE_IDX, if VALUE_IDX is
   nonzero. */
static void
setup_weight (struct sfm_reader *r, int weight_idx,
              struct variable **var_by_value_idx, struct dictionary *dict)
{
  if (weight_idx != 0)
    {
      struct variable *weight_var
        = lookup_var_by_value_idx (r, var_by_value_idx, weight_idx);
      if (var_is_numeric (weight_var))
        dict_set_weight (dict, weight_var);
      else
        sys_error (r, _("Weighting variable must be numeric."));
    }
}

/* Reads a document record, type 6, from system file R, and sets up
   the documents and n_documents fields in the associated
   dictionary. */
static void
read_documents (struct sfm_reader *r, struct dictionary *dict)
{
  int line_cnt;
  char *documents;

  if (dict_get_documents (dict) != NULL)
    sys_error (r, _("Multiple type 6 (document) records."));

  line_cnt = read_int (r);
  if (line_cnt <= 0)
    sys_error (r, _("Number of document lines (%d) "
                    "must be greater than 0."), line_cnt);

  documents = pool_nmalloc (r->pool, line_cnt + 1, DOC_LINE_LENGTH);
  read_string (r, documents, DOC_LINE_LENGTH * line_cnt + 1);
  if (strlen (documents) == DOC_LINE_LENGTH * line_cnt)
    dict_set_documents (dict, documents);
  else
    sys_error (r, _("Document line contains null byte."));
  pool_free (r->pool, documents);
}

/* Read a type 7 extension record. */
static void
read_extension_record (struct sfm_reader *r, struct dictionary *dict,
                       struct sfm_read_info *info)
{
  int subtype = read_int (r);
  size_t size = read_int (r);
  size_t count = read_int (r);
  size_t bytes = size * count;

  /* Check that SIZE * COUNT + 1 doesn't overflow.  Adding 1
     allows an extra byte for a null terminator, used by some
     extension processing routines. */
  if (size != 0 && size_overflow_p (xsum (1, xtimes (count, size))))
    sys_error (r, "Record type 7 subtype %d too large.", subtype);

  switch (subtype)
    {
    case 3:
      read_machine_integer_info (r, size, count, info, dict);
      return;

    case 4:
      read_machine_float_info (r, size, count);
      return;

    case 5:
      /* Variable sets information.  We don't use these yet.
         They only apply to GUIs; see VARSETS on the APPLY
         DICTIONARY command in SPSS documentation. */
      break;

    case 6:
      /* DATE variable information.  We don't use it yet, but we
         should. */
      break;

    case 7:
      /* Used by the MRSETS command. */
      break;

    case 8:
      /* Used by the SPSS Data Entry software. */
      break;

    case 11:
      read_display_parameters (r, size, count, dict);
      return;

    case 13:
      read_long_var_name_map (r, size, count, dict);
      return;

    case 14:
      read_long_string_map (r, size, count, dict);
      return;

    case 16:
      /* New in SPSS v14?  Unknown purpose.  */
      break;

    case 17:
      read_data_file_attributes (r, size, count, dict);
      return;

    case 18:
      read_variable_attributes (r, size, count, dict);
      return;

    case 20:
      /* New in SPSS 16.  Contains a single string that describes
         the character encoding, e.g. "windows-1252". */
      {
	char *encoding = pool_calloc (r->pool, size, count + 1);
	read_string (r, encoding, count + 1);
	dict_set_encoding (dict, encoding);
	return;
      }

    case 21:
      /* New in SPSS 16.  Encodes value labels for long string
         variables. */
      read_long_string_value_labels (r, size, count, dict);
      return;

    default:
      sys_warn (r, _("Unrecognized record type 7, subtype %d.  Please send a copy of this file, and the syntax which created it to %s"),
		subtype, PACKAGE_BUGREPORT);
      break;
    }

  skip_bytes (r, bytes);
}

/* Read record type 7, subtype 3. */
static void
read_machine_integer_info (struct sfm_reader *r, size_t size, size_t count,
                           struct sfm_read_info *info,
			   struct dictionary *dict)
{
  int version_major = read_int (r);
  int version_minor = read_int (r);
  int version_revision = read_int (r);
  int machine_code UNUSED = read_int (r);
  int float_representation = read_int (r);
  int compression_code UNUSED = read_int (r);
  int integer_representation = read_int (r);
  int character_code = read_int (r);

  int expected_float_format;
  int expected_integer_format;

  if (size != 4 || count != 8)
    sys_error (r, _("Bad size (%zu) or count (%zu) field on record type 7, "
                    "subtype 3."),
                size, count);

  /* Save version info. */
  info->version_major = version_major;
  info->version_minor = version_minor;
  info->version_revision = version_revision;

  /* Check floating point format. */
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
    sys_error (r, _("Floating-point representation indicated by "
                    "system file (%d) differs from expected (%d)."),
               r->float_format, expected_float_format);

  /* Check integer format. */
  if (r->integer_format == INTEGER_MSB_FIRST)
    expected_integer_format = 1;
  else if (r->integer_format == INTEGER_LSB_FIRST)
    expected_integer_format = 2;
  else
    NOT_REACHED ();
  if (integer_representation != expected_integer_format)
    {
      static const char *const endian[] = {N_("little-endian"), N_("big-endian")};
      sys_warn (r, _("Integer format indicated by system file (%s) "
                     "differs from expected (%s)."),
                gettext (endian[integer_representation == 1]),
                gettext (endian[expected_integer_format == 1]));
    }


  /*
    Record 7 (20) provides a much more reliable way of
    setting the encoding.
    The character_code is used as a fallback only.
  */
  if ( NULL == dict_get_encoding (dict))
    {
      switch (character_code)
	{
	case 1:
	  dict_set_encoding (dict, "EBCDIC-US");
	  break;
	case 2:
	case 3:
	  /* These ostensibly mean "7-bit ASCII" and "8-bit ASCII"[sic]
	     respectively.   However, there are known to be many files
	     in the wild with character code 2, yet have data which are
	     clearly not ascii.
	     Therefore we ignore these values.
	  */
	  return;
	case 4:
	  dict_set_encoding (dict, "MS_KANJI");
	  break;
	case 65000:
	  dict_set_encoding (dict, "UTF-7");
	  break;
	case 65001:
	  dict_set_encoding (dict, "UTF-8");
	  break;
	default:
	  {
	    char enc[100];
	    snprintf (enc, 100, "CP%d", character_code);
	    dict_set_encoding (dict, enc);
	  }
	  break;
	};
    }
}

/* Read record type 7, subtype 4. */
static void
read_machine_float_info (struct sfm_reader *r, size_t size, size_t count)
{
  double sysmis = read_float (r);
  double highest = read_float (r);
  double lowest = read_float (r);

  if (size != 8 || count != 3)
    sys_error (r, _("Bad size (%zu) or count (%zu) on extension 4."),
               size, count);

  if (sysmis != SYSMIS)
    sys_warn (r, _("File specifies unexpected value %g as SYSMIS."), sysmis);
  if (highest != HIGHEST)
    sys_warn (r, _("File specifies unexpected value %g as HIGHEST."), highest);
  if (lowest != LOWEST)
    sys_warn (r, _("File specifies unexpected value %g as LOWEST."), lowest);
}

/* Read record type 7, subtype 11, which specifies how variables
   should be displayed in GUI environments. */
static void
read_display_parameters (struct sfm_reader *r, size_t size, size_t count,
                         struct dictionary *dict)
{
  size_t n_vars;
  bool includes_width;
  bool warned = false;
  size_t i;

  if (size != 4)
    {
      sys_warn (r, _("Bad size %zu on extension 11."), size);
      skip_bytes (r, size * count);
      return;
    }

  n_vars = dict_get_var_cnt (dict);
  if (count == 3 * n_vars)
    includes_width = true;
  else if (count == 2 * n_vars)
    includes_width = false;
  else
    {
      sys_warn (r, _("Extension 11 has bad count %zu (for %zu variables)."),
                count, n_vars);
      skip_bytes (r, size * count);
      return;
    }

  for (i = 0; i < n_vars; ++i)
    {
      struct variable *v = dict_get_var (dict, i);
      int measure = read_int (r);
      int width = includes_width ? read_int (r) : 0;
      int align = read_int (r);

      /* SPSS 14 sometimes seems to set string variables' measure
         to zero. */
      if (0 == measure && var_is_alpha (v))
        measure = 1;

      if (measure < 1 || measure > 3 || align < 0 || align > 2)
        {
          if (!warned)
            sys_warn (r, _("Invalid variable display parameters "
                           "for variable %zu (%s).  "
                           "Default parameters substituted."),
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

/* Reads record type 7, subtype 13, which gives the long name
   that corresponds to each short name.  Modifies variable names
   in DICT accordingly.  */
static void
read_long_var_name_map (struct sfm_reader *r, size_t size, size_t count,
                        struct dictionary *dict)
{
  struct text_record *text;
  struct variable *var;
  char *long_name;

  text = open_text_record (r, size * count);
  while (read_variable_to_value_pair (r, dict, text, &var, &long_name))
    {
      char **short_names;
      size_t short_name_cnt;
      size_t i;

      /* Validate long name. */
      if (!var_is_valid_name (long_name, false))
        {
          sys_warn (r, _("Long variable mapping from %s to invalid "
                         "variable name `%s'."),
                    var_get_name (var), long_name);
          continue;
        }

      /* Identify any duplicates. */
      if (strcasecmp (var_get_short_name (var, 0), long_name)
          && dict_lookup_var (dict, long_name) != NULL)
        {
          sys_warn (r, _("Duplicate long variable name `%s' "
                         "within system file."), long_name);
          continue;
        }

      /* Renaming a variable may clear its short names, but we
         want to retain them, so we save them and re-set them
         afterward. */
      short_name_cnt = var_get_short_name_cnt (var);
      short_names = xnmalloc (short_name_cnt, sizeof *short_names);
      for (i = 0; i < short_name_cnt; i++)
        {
          const char *s = var_get_short_name (var, i);
          short_names[i] = s != NULL ? xstrdup (s) : NULL;
        }

      /* Set long name. */
      dict_rename_var (dict, var, long_name);

      /* Restore short names. */
      for (i = 0; i < short_name_cnt; i++)
        {
          var_set_short_name (var, i, short_names[i]);
          free (short_names[i]);
        }
      free (short_names);
    }
  close_text_record (r, text);
  r->has_long_var_names = true;
}

/* Reads record type 7, subtype 14, which gives the real length
   of each very long string.  Rearranges DICT accordingly. */
static void
read_long_string_map (struct sfm_reader *r, size_t size, size_t count,
                      struct dictionary *dict)
{
  struct text_record *text;
  struct variable *var;
  char *length_s;

  text = open_text_record (r, size * count);
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
          sys_warn (r, _("%s listed as string of invalid length %s "
                         "in very length string record."),
                    var_get_name (var), length_s);
          continue;
        }

      /* Check segments. */
      segment_cnt = sfm_width_to_segments (length);
      if (segment_cnt == 1)
        {
          sys_warn (r, _("%s listed in very long string record with width %s, "
                         "which requires only one segment."),
                    var_get_name (var), length_s);
          continue;
        }
      if (idx + segment_cnt > dict_get_var_cnt (dict))
        sys_error (r, _("Very long string %s overflows dictionary."),
                   var_get_name (var));

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
            sys_error (r, _("Very long string with width %ld has segment %d "
                            "of width %d (expected %d)"),
                       length, i, width, alloc_width);
        }
      dict_delete_consecutive_vars (dict, idx + 1, segment_cnt - 1);
      var_set_width (var, length);
    }
  close_text_record (r, text);
  dict_compact_values (dict);
}

/* Reads value labels from sysfile H and inserts them into the
   associated dictionary. */
static void
read_value_labels (struct sfm_reader *r,
                   struct dictionary *dict, struct variable **var_by_value_idx)
{
  struct pool *subpool;

  struct label
    {
      char raw_value[8];        /* Value as uninterpreted bytes. */
      union value value;        /* Value. */
      char *label;              /* Null-terminated label string. */
    };

  struct label *labels = NULL;
  int label_cnt;		/* Number of labels. */

  struct variable **var = NULL;	/* Associated variables. */
  int var_cnt;			/* Number of associated variables. */
  int max_width;                /* Maximum width of string variables. */

  int i;

  subpool = pool_create_subpool (r->pool);

  /* Read the type 3 record and record its contents.  We can't do
     much with the data yet because we don't know whether it is
     of numeric or string type. */

  /* Read number of labels. */
  label_cnt = read_int (r);

  if (size_overflow_p (xtimes (label_cnt, sizeof *labels)))
    {
      sys_warn (r, _("Invalid number of labels: %d.  Ignoring labels."),
                label_cnt);
      label_cnt = 0;
    }

  /* Read each value/label tuple into labels[]. */
  labels = pool_nalloc (subpool, label_cnt, sizeof *labels);
  for (i = 0; i < label_cnt; i++)
    {
      struct label *label = labels + i;
      unsigned char label_len;
      size_t padded_len;

      /* Read value. */
      read_bytes (r, label->raw_value, sizeof label->raw_value);

      /* Read label length. */
      read_bytes (r, &label_len, sizeof label_len);
      padded_len = ROUND_UP (label_len + 1, 8);

      /* Read label, padding. */
      label->label = pool_alloc (subpool, padded_len + 1);
      read_bytes (r, label->label, padded_len - 1);
      label->label[label_len] = 0;
    }

  /* Now, read the type 4 record that has the list of variables
     to which the value labels are to be applied. */

  /* Read record type of type 4 record. */
  if (read_int (r) != 4)
    sys_error (r, _("Variable index record (type 4) does not immediately "
                    "follow value label record (type 3) as it should."));

  /* Read number of variables associated with value label from type 4
     record. */
  var_cnt = read_int (r);
  if (var_cnt < 1 || var_cnt > dict_get_var_cnt (dict))
    sys_error (r, _("Number of variables associated with a value label (%d) "
                    "is not between 1 and the number of variables (%zu)."),
               var_cnt, dict_get_var_cnt (dict));

  /* Read the list of variables. */
  var = pool_nalloc (subpool, var_cnt, sizeof *var);
  max_width = 0;
  for (i = 0; i < var_cnt; i++)
    {
      var[i] = lookup_var_by_value_idx (r, var_by_value_idx, read_int (r));
      if (var_get_width (var[i]) > 8)
        sys_error (r, _("Value labels may not be added to long string "
                        "variables (e.g. %s) using records types 3 and 4."),
                   var_get_name (var[i]));
      max_width = MAX (max_width, var_get_width (var[i]));
    }

  /* Type check the variables. */
  for (i = 1; i < var_cnt; i++)
    if (var_get_type (var[i]) != var_get_type (var[0]))
      sys_error (r, _("Variables associated with value label are not all of "
                      "identical type.  Variable %s is %s, but variable "
                      "%s is %s."),
                 var_get_name (var[0]),
                 var_is_numeric (var[0]) ? _("numeric") : _("string"),
                 var_get_name (var[i]),
                 var_is_numeric (var[i]) ? _("numeric") : _("string"));

  /* Fill in labels[].value, now that we know the desired type. */
  for (i = 0; i < label_cnt; i++)
    {
      struct label *label = labels + i;

      value_init_pool (subpool, &label->value, max_width);
      if (var_is_alpha (var[0]))
        buf_copy_rpad (value_str_rw (&label->value, max_width), max_width,
                       label->raw_value, sizeof label->raw_value, ' ');
      else
        label->value.f = float_get_double (r->float_format, label->raw_value);
    }

  /* Assign the `value_label's to each variable. */
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = var[i];
      int j;

      /* Add each label to the variable. */
      for (j = 0; j < label_cnt; j++)
	{
          struct label *label = &labels[j];
          if (!var_add_value_label (v, &label->value, label->label))
            {
              if (var_is_numeric (var[0]))
                sys_warn (r, _("Duplicate value label for %g on %s."),
                          label->value.f, var_get_name (v));
              else
                sys_warn (r, _("Duplicate value label for \"%.*s\" on %s."),
                          max_width, value_str (&label->value, max_width),
                          var_get_name (v));
            }
	}
    }

  pool_destroy (subpool);
}

/* Reads a set of custom attributes from TEXT into ATTRS.
   ATTRS may be a null pointer, in which case the attributes are
   read but discarded. */
static void
read_attributes (struct sfm_reader *r, struct text_record *text,
                 struct attrset *attrs)
{
  do
    {
      struct attribute *attr;
      char *key;
      int index;

      /* Parse the key. */
      key = text_get_token (text, ss_cstr ("("));
      if (key == NULL)
        return;

      attr = attribute_create (key);
      for (index = 1; ; index++)
        {
          /* Parse the value. */
          char *value;
          size_t length;

          value = text_get_token (text, ss_cstr ("\n"));
          if (value == NULL)
            {
              text_warn (r, text, _("Error parsing attribute value %s[%d]"),
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
                         _("Attribute value %s[%d] is not quoted: %s"),
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
read_data_file_attributes (struct sfm_reader *r,
                           size_t size, size_t count,
                           struct dictionary *dict)
{
  struct text_record *text = open_text_record (r, size * count);
  read_attributes (r, text, dict_get_attributes (dict));
  close_text_record (r, text);
}

static void
skip_long_string_value_labels (struct sfm_reader *r, size_t n_labels)
{
  size_t i;

  for (i = 0; i < n_labels; i++)
    {
      size_t value_length, label_length;

      value_length = read_int (r);
      skip_bytes (r, value_length);
      label_length = read_int (r);
      skip_bytes (r, label_length);
    }
}

static void
read_long_string_value_labels (struct sfm_reader *r,
			       size_t size, size_t count,
			       struct dictionary *d)
{
  const off_t start = ftello (r->file);
  while (ftello (r->file) - start < size * count)
    {
      char var_name[VAR_NAME_LEN + 1];
      size_t n_labels, i;
      struct variable *v;
      union value value;
      int var_name_len;
      int width;

      /* Read header. */
      var_name_len = read_int (r);
      if (var_name_len > VAR_NAME_LEN)
        sys_error (r, _("Variable name length in long string value label "
                        "record (%d) exceeds %d-byte limit."),
                   var_name_len, VAR_NAME_LEN);
      read_string (r, var_name, var_name_len + 1);
      width = read_int (r);
      n_labels = read_int (r);

      v = dict_lookup_var (d, var_name);
      if (v == NULL)
        {
          sys_warn (r, _("Ignoring long string value record for "
                         "unknown variable %s."), var_name);
          skip_long_string_value_labels (r, n_labels);
          continue;
        }
      if (var_is_numeric (v))
        {
          sys_warn (r, _("Ignoring long string value record for "
                         "numeric variable %s."), var_name);
          skip_long_string_value_labels (r, n_labels);
          continue;
        }
      if (width != var_get_width (v))
        {
          sys_warn (r, _("Ignoring long string value record for variable %s "
                         "because the record's width (%d) does not match the "
                         "variable's width (%d)"),
                    var_name, width, var_get_width (v));
          skip_long_string_value_labels (r, n_labels);
          continue;
        }

      /* Read values. */
      value_init_pool (r->pool, &value, width);
      for (i = 0; i < n_labels; i++)
	{
          size_t value_length, label_length;
          char label[256];
          bool skip = false;

          /* Read value. */
          value_length = read_int (r);
          if (value_length == width)
            read_string (r, value_str_rw (&value, width), width + 1);
          else
            {
              sys_warn (r, _("Ignoring long string value %zu for variable %s, "
                             "with width %d, that has bad value width %zu."),
                        i, var_get_name (v), width, value_length);
              skip_bytes (r, value_length);
              skip = true;
            }

          /* Read label. */
          label_length = read_int (r);
          read_string (r, label, MIN (sizeof label, label_length + 1));
          if (label_length >= sizeof label)
            {
              /* Skip and silently ignore label text after the
                 first 255 bytes.  The maximum documented length
                 of a label is 120 bytes so this is more than
                 generous. */
              skip_bytes (r, sizeof label - (label_length + 1));
            }

          if (!skip && !var_add_value_label (v, &value, label))
            sys_warn (r, _("Duplicate value label for \"%.*s\" on %s."),
                      width, value_str (&value, width), var_get_name (v));
        }
    }
}


/* Reads record type 7, subtype 18, which lists custom
   attributes on individual variables.  */
static void
read_variable_attributes (struct sfm_reader *r,
                          size_t size, size_t count,
                          struct dictionary *dict)
{
  struct text_record *text = open_text_record (r, size * count);
  for (;;) 
    {
      struct variable *var;
      if (!text_read_short_name (r, dict, text, ss_cstr (":"), &var))
        break;
      read_attributes (r, text, var != NULL ? var_get_attributes (var) : NULL);
    }
  close_text_record (r, text);
}


/* Case reader. */

static void partial_record (struct sfm_reader *r)
     NO_RETURN;

static void read_error (struct casereader *, const struct sfm_reader *);

static bool read_case_number (struct sfm_reader *, double *);
static bool read_case_string (struct sfm_reader *, char *, size_t);
static int read_opcode (struct sfm_reader *);
static bool read_compressed_number (struct sfm_reader *, double *);
static bool read_compressed_string (struct sfm_reader *, char *);
static bool read_whole_strings (struct sfm_reader *, char *, size_t);
static bool skip_whole_strings (struct sfm_reader *, size_t);

/* Reads and returns one case from READER's file.  Returns a null
   pointer if not successful. */
static struct ccase *
sys_file_casereader_read (struct casereader *reader, void *r_)
{
  struct sfm_reader *r = r_;
  struct ccase *volatile c;
  int i;

  if (r->error)
    return NULL;

  c = case_create (r->proto);
  if (setjmp (r->bail_out))
    {
      casereader_force_error (reader);
      case_unref (c);
      return NULL;
    }

  for (i = 0; i < r->sfm_var_cnt; i++)
    {
      struct sfm_var *sv = &r->sfm_vars[i];
      union value *v = case_data_rw_idx (c, sv->case_index);

      if (sv->var_width == 0)
        {
          if (!read_case_number (r, &v->f))
            goto eof;
        }
      else
        {
          char *s = value_str_rw (v, sv->var_width);
          if (!read_case_string (r, s + sv->offset, sv->segment_width))
            goto eof;
          if (!skip_whole_strings (r, ROUND_DOWN (sv->padding, 8)))
            partial_record (r);
        }
    }
  return c;

eof:
  case_unref (c);
  if (i != 0)
    partial_record (r);
  if (r->case_cnt != -1)
    read_error (reader, r);
  return NULL;
}

/* Issues an error that R ends in a partial record. */
static void
partial_record (struct sfm_reader *r)
{
  sys_error (r, _("File ends in partial case."));
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
  if (!r->compressed)
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

/* Reads LENGTH string bytes from R into S.
   Always reads a multiple of 8 bytes; if LENGTH is not a
   multiple of 8, then extra bytes are read and discarded without
   being written to S.
   Reads compressed strings if S is compressed.
   Returns true if successful, false if end of file is
   reached immediately. */
static bool
read_case_string (struct sfm_reader *r, char *s, size_t length)
{
  size_t whole = ROUND_DOWN (length, 8);
  size_t partial = length % 8;

  if (whole)
    {
      if (!read_whole_strings (r, s, whole))
        return false;
    }

  if (partial)
    {
      char bounce[8];
      if (!read_whole_strings (r, bounce, sizeof bounce))
        {
          if (whole)
            partial_record (r);
          return false;
        }
      memcpy (s + whole, bounce, partial);
    }

  return true;
}

/* Reads and returns the next compression opcode from R. */
static int
read_opcode (struct sfm_reader *r)
{
  assert (r->compressed);
  for (;;)
    {
      int opcode;
      if (r->opcode_idx >= sizeof r->opcodes)
        {
          if (!try_read_bytes (r, r->opcodes, sizeof r->opcodes))
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
      *d = read_float (r);
      break;

    case 254:
      sys_error (r, _("Compressed data is corrupt."));

    case 255:
      *d = SYSMIS;
      break;

    default:
      *d = opcode - r->bias;
      break;
    }

  return true;
}

/* Reads a compressed 8-byte string segment from R and stores it
   in DST.
   Returns true if successful, false if end of file is
   reached immediately. */
static bool
read_compressed_string (struct sfm_reader *r, char *dst)
{
  switch (read_opcode (r))
    {
    case -1:
    case 252:
      return false;

    case 253:
      read_bytes (r, dst, 8);
      break;

    case 254:
      memset (dst, ' ', 8);
      break;

    default:
      sys_error (r, _("Compressed data is corrupt."));
    }

  return true;
}

/* Reads LENGTH string bytes from R into S.
   LENGTH must be a multiple of 8.
   Reads compressed strings if S is compressed.
   Returns true if successful, false if end of file is
   reached immediately. */
static bool
read_whole_strings (struct sfm_reader *r, char *s, size_t length)
{
  assert (length % 8 == 0);
  if (!r->compressed)
    return try_read_bytes (r, s, length);
  else
    {
      size_t ofs;
      for (ofs = 0; ofs < length; ofs += 8)
        if (!read_compressed_string (r, s + ofs))
          {
            if (ofs != 0)
              partial_record (r);
            return false;
          }
      return true;
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
  char buffer[1024];
  assert (length < sizeof buffer);
  return read_whole_strings (r, buffer, length);
}

/* Creates and returns a table that can be used for translating a value
   index into a case to a "struct variable *" for DICT.  Multiple
   system file fields reference variables this way.

   This table must be created before processing the very long
   string extension record, because that record causes some
   values to be deleted from the case and the dictionary to be
   compacted. */
static struct variable **
make_var_by_value_idx (struct sfm_reader *r, struct dictionary *dict)
{
  struct variable **var_by_value_idx;
  int value_idx = 0;
  int i;

  var_by_value_idx = pool_nmalloc (r->pool,
                                   r->oct_cnt, sizeof *var_by_value_idx);
  for (i = 0; i < dict_get_var_cnt (dict); i++)
    {
      struct variable *v = dict_get_var (dict, i);
      int nv = var_is_numeric (v) ? 1 : DIV_RND_UP (var_get_width (v), 8);
      int j;

      var_by_value_idx[value_idx++] = v;
      for (j = 1; j < nv; j++)
        var_by_value_idx[value_idx++] = NULL;
    }
  assert (value_idx == r->oct_cnt);

  return var_by_value_idx;
}

/* Returns the "struct variable" corresponding to the given
   1-basd VALUE_IDX in VAR_BY_VALUE_IDX.  Verifies that the index
   is valid. */
static struct variable *
lookup_var_by_value_idx (struct sfm_reader *r,
                         struct variable **var_by_value_idx, int value_idx)
{
  struct variable *var;

  if (value_idx < 1 || value_idx > r->oct_cnt)
    sys_error (r, _("Variable index %d not in valid range 1...%d."),
               value_idx, r->oct_cnt);

  var = var_by_value_idx[value_idx - 1];
  if (var == NULL)
    sys_error (r, _("Variable index %d refers to long string "
                    "continuation."),
               value_idx);

  return var;
}

/* Returns the variable in D with the given SHORT_NAME,
   or a null pointer if there is none. */
static struct variable *
lookup_var_by_short_name (struct dictionary *d, const char *short_name)
{
  struct variable *var;
  size_t var_cnt;
  size_t i;

  /* First try looking up by full name.  This often succeeds. */
  var = dict_lookup_var (d, short_name);
  if (var != NULL && !strcasecmp (var_get_short_name (var, 0), short_name))
    return var;

  /* Iterate through the whole dictionary as a fallback. */
  var_cnt = dict_get_var_cnt (d);
  for (i = 0; i < var_cnt; i++)
    {
      var = dict_get_var (d, i);
      if (!strcasecmp (var_get_short_name (var, 0), short_name))
        return var;
    }

  return NULL;
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
    size_t pos;                 /* Current position in buffer. */
    int n_warnings;             /* Number of warnings issued or suppressed. */
  };

/* Reads SIZE bytes into a text record for R,
   and returns the new text record. */
static struct text_record *
open_text_record (struct sfm_reader *r, size_t size)
{
  struct text_record *text = pool_alloc (r->pool, sizeof *text);
  char *buffer = pool_malloc (r->pool, size + 1);
  read_bytes (r, buffer, size);
  text->buffer = ss_buffer (buffer, size);
  text->pos = 0;
  text->n_warnings = 0;
  return text;
}

/* Closes TEXT, frees its storage, and issues a final warning
   about suppressed warnings if necesary. */
static void
close_text_record (struct sfm_reader *r, struct text_record *text)
{
  if (text->n_warnings > MAX_TEXT_WARNINGS)
    sys_warn (r, _("Suppressed %d additional related warnings."),
              text->n_warnings - MAX_TEXT_WARNINGS);
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
      
      *value = text_get_token (text, ss_buffer ("\t\0", 2));
      if (*value == NULL)
        return false;

      text->pos += ss_span (ss_substr (text->buffer, text->pos, SIZE_MAX),
                            ss_buffer ("\t\0", 2));

      if (*var != NULL)
        return true;
    }
}

static bool
text_read_short_name (struct sfm_reader *r, struct dictionary *dict,
                      struct text_record *text, struct substring delimiters,
                      struct variable **var)
{
  char *short_name = text_get_token (text, delimiters);
  if (short_name == NULL)
    return false;

  *var = lookup_var_by_short_name (dict, short_name);
  if (*var == NULL)
    text_warn (r, text, _("Variable map refers to unknown variable %s."),
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
      sys_msg (r, MW, format, args);
      va_end (args);
    }
}

static char *
text_get_token (struct text_record *text, struct substring delimiters)
{
  struct substring token;

  if (!ss_tokenize (text->buffer, delimiters, &text->pos, &token))
    return NULL;
  ss_data (token)[ss_length (token)] = '\0';
  return ss_data (token);
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

/* Messages. */

/* Displays a corruption message. */
static void
sys_msg (struct sfm_reader *r, int class, const char *format, va_list args)
{
  struct msg m;
  struct string text;

  ds_init_empty (&text);
  ds_put_format (&text, "\"%s\" near offset 0x%lx: ",
                 fh_get_file_name (r->fh), (unsigned long) ftell (r->file));
  ds_put_vformat (&text, format, args);

  m.category = msg_class_to_category (class);
  m.severity = msg_class_to_severity (class);
  m.where.file_name = NULL;
  m.where.line_number = 0;
  m.text = ds_cstr (&text);

  msg_emit (&m);
}

/* Displays a warning for the current file position. */
static void
sys_warn (struct sfm_reader *r, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  sys_msg (r, MW, format, args);
  va_end (args);
}

/* Displays an error for the current file position,
   marks it as in an error state,
   and aborts reading it using longjmp. */
static void
sys_error (struct sfm_reader *r, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  sys_msg (r, ME, format, args);
  va_end (args);

  r->error = true;
  longjmp (r->bail_out, 1);
}

/* Reads BYTE_CNT bytes into BUF.
   Returns true if exactly BYTE_CNT bytes are successfully read.
   Aborts if an I/O error or a partial read occurs.
   If EOF_IS_OK, then an immediate end-of-file causes false to be
   returned; otherwise, immediate end-of-file causes an abort
   too. */
static inline bool
read_bytes_internal (struct sfm_reader *r, bool eof_is_ok,
                   void *buf, size_t byte_cnt)
{
  size_t bytes_read = fread (buf, 1, byte_cnt, r->file);
  if (bytes_read == byte_cnt)
    return true;
  else if (ferror (r->file))
    sys_error (r, _("System error: %s."), strerror (errno));
  else if (!eof_is_ok || bytes_read != 0)
    sys_error (r, _("Unexpected end of file."));
  else
    return false;
}

/* Reads BYTE_CNT into BUF.
   Aborts upon I/O error or if end-of-file is encountered. */
static void
read_bytes (struct sfm_reader *r, void *buf, size_t byte_cnt)
{
  read_bytes_internal (r, false, buf, byte_cnt);
}

/* Reads BYTE_CNT bytes into BUF.
   Returns true if exactly BYTE_CNT bytes are successfully read.
   Returns false if an immediate end-of-file is encountered.
   Aborts if an I/O error or a partial read occurs. */
static bool
try_read_bytes (struct sfm_reader *r, void *buf, size_t byte_cnt)
{
  return read_bytes_internal (r, true, buf, byte_cnt);
}

/* Reads a 32-bit signed integer from R and returns its value in
   host format. */
static int
read_int (struct sfm_reader *r)
{
  uint8_t integer[4];
  read_bytes (r, integer, sizeof integer);
  return integer_get (r->integer_format, integer, sizeof integer);
}

/* Reads a 64-bit floating-point number from R and returns its
   value in host format. */
static double
read_float (struct sfm_reader *r)
{
  uint8_t number[8];
  read_bytes (r, number, sizeof number);
  return float_get_double (r->float_format, number);
}

/* Reads exactly SIZE - 1 bytes into BUFFER
   and stores a null byte into BUFFER[SIZE - 1]. */
static void
read_string (struct sfm_reader *r, char *buffer, size_t size)
{
  assert (size > 0);
  read_bytes (r, buffer, size - 1);
  buffer[size - 1] = '\0';
}

/* Skips BYTES bytes forward in R. */
static void
skip_bytes (struct sfm_reader *r, size_t bytes)
{
  while (bytes > 0)
    {
      char buffer[1024];
      size_t chunk = MIN (sizeof buffer, bytes);
      read_bytes (r, buffer, chunk);
      bytes -= chunk;
    }
}

static const struct casereader_class sys_file_casereader_class =
  {
    sys_file_casereader_read,
    sys_file_casereader_destroy,
    NULL,
    NULL,
  };
