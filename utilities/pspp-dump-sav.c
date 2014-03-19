/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "data/val-type.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/float-format.h"
#include "libpspp/integer-format.h"
#include "libpspp/misc.h"

#include "gl/error.h"
#include "gl/minmax.h"
#include "gl/progname.h"
#include "gl/version-etc.h"
#include "gl/xalloc.h"

#define ID_MAX_LEN 64

enum compression
  {
    COMP_NONE,
    COMP_SIMPLE,
    COMP_ZLIB
  };

struct sfm_reader
  {
    const char *file_name;
    FILE *file;

    int n_variable_records, n_variables;

    int *var_widths;
    size_t n_var_widths, allocated_var_widths;

    enum integer_format integer_format;
    enum float_format float_format;

    enum compression compression;
    double bias;
  };

static void read_header (struct sfm_reader *);
static void read_variable_record (struct sfm_reader *);
static void read_value_label_record (struct sfm_reader *);
static void read_document_record (struct sfm_reader *);
static void read_extension_record (struct sfm_reader *);
static void read_machine_integer_info (struct sfm_reader *,
                                       size_t size, size_t count);
static void read_machine_float_info (struct sfm_reader *,
                                     size_t size, size_t count);
static void read_extra_product_info (struct sfm_reader *,
                                     size_t size, size_t count);
static void read_mrsets (struct sfm_reader *, size_t size, size_t count);
static void read_display_parameters (struct sfm_reader *,
                                     size_t size, size_t count);
static void read_long_var_name_map (struct sfm_reader *r,
                                    size_t size, size_t count);
static void read_long_string_map (struct sfm_reader *r,
                                  size_t size, size_t count);
static void read_datafile_attributes (struct sfm_reader *r,
                                      size_t size, size_t count);
static void read_variable_attributes (struct sfm_reader *r,
                                      size_t size, size_t count);
static void read_ncases64 (struct sfm_reader *, size_t size, size_t count);
static void read_character_encoding (struct sfm_reader *r,
				       size_t size, size_t count);
static void read_long_string_value_labels (struct sfm_reader *r,
                                           size_t size, size_t count);
static void read_long_string_missing_values (struct sfm_reader *r,
                                             size_t size, size_t count);
static void read_unknown_extension (struct sfm_reader *,
                                    size_t size, size_t count);
static void read_simple_compressed_data (struct sfm_reader *, int max_cases);
static void read_zlib_compressed_data (struct sfm_reader *);

static struct text_record *open_text_record (
  struct sfm_reader *, size_t size);
static void close_text_record (struct text_record *);
static bool read_variable_to_value_pair (struct text_record *,
                                         char **key, char **value);
static char *text_tokenize (struct text_record *, int delimiter);
static bool text_match (struct text_record *text, int c);
static const char *text_parse_counted_string (struct text_record *);
static size_t text_pos (const struct text_record *);
static const char *text_get_all (const struct text_record *);

static void usage (void);
static void sys_warn (struct sfm_reader *, const char *, ...)
     PRINTF_FORMAT (2, 3);
static void sys_error (struct sfm_reader *, const char *, ...)
     PRINTF_FORMAT (2, 3)
     NO_RETURN;

static void read_bytes (struct sfm_reader *, void *, size_t);
static bool try_read_bytes (struct sfm_reader *, void *, size_t);
static int read_int (struct sfm_reader *);
static int64_t read_int64 (struct sfm_reader *);
static double read_float (struct sfm_reader *);
static void read_string (struct sfm_reader *, char *, size_t);
static void skip_bytes (struct sfm_reader *, size_t);
static void trim_spaces (char *);

static void print_string (const char *s, size_t len);

int
main (int argc, char *argv[])
{
  int max_cases = 0;
  struct sfm_reader r;
  int i;

  set_program_name (argv[0]);

  for (;;)
    {
      static const struct option long_options[] =
        {
          { "data",    optional_argument, NULL, 'd' },
          { "help",    no_argument,       NULL, 'h' },
          { "version", no_argument,       NULL, 'v' },
          { NULL,      0,                 NULL, 0 },
        };

      int c;

      c = getopt_long (argc, argv, "d::hv", long_options, NULL);
      if (c == -1)
        break;

      switch (c)
        {
        case 'd':
          max_cases = optarg ? atoi (optarg) : INT_MAX;
          break;

        case 'v':
          version_etc (stdout, "pspp-dump-sav", PACKAGE_NAME, PACKAGE_VERSION,
                       "Ben Pfaff", "John Darrington", NULL_SENTINEL);
          exit (EXIT_SUCCESS);

        case 'h':
          usage ();
          exit (EXIT_SUCCESS);

        default:
          exit (EXIT_FAILURE);
        }
    }

  if (optind == argc)
    error (1, 0, "at least one non-option argument is required; "
           "use --help for help");

  for (i = optind; i < argc; i++)
    {
      int rec_type;

      r.file_name = argv[i];
      r.file = fopen (r.file_name, "rb");
      if (r.file == NULL)
        error (EXIT_FAILURE, errno, "error opening `%s'", r.file_name);
      r.n_variable_records = 0;
      r.n_variables = 0;
      r.n_var_widths = 0;
      r.allocated_var_widths = 0;
      r.var_widths = 0;
      r.compression = COMP_NONE;

      if (argc - optind > 1)
        printf ("Reading \"%s\":\n", r.file_name);
      
      read_header (&r);
      while ((rec_type = read_int (&r)) != 999)
        {
          switch (rec_type)
            {
            case 2:
              read_variable_record (&r);
              break;

            case 3:
              read_value_label_record (&r);
              break;

            case 4:
              sys_error (&r, "Misplaced type 4 record.");

            case 6:
              read_document_record (&r);
              break;

            case 7:
              read_extension_record (&r);
              break;

            default:
              sys_error (&r, "Unrecognized record type %d.", rec_type);
            }
        }
      printf ("%08llx: end-of-dictionary record "
              "(first byte of data at %08llx)\n",
              (long long int) ftello (r.file),
              (long long int) ftello (r.file) + 4);

      if (r.compression == COMP_SIMPLE)
        {
          if (max_cases > 0)
            read_simple_compressed_data (&r, max_cases);
        }
      else if (r.compression == COMP_ZLIB)
        read_zlib_compressed_data (&r);

      fclose (r.file);
    }
  
  return 0;
}

static void
read_header (struct sfm_reader *r)
{
  char rec_type[5];
  char eye_catcher[61];
  uint8_t raw_layout_code[4];
  int32_t layout_code;
  int32_t compressed;
  int32_t weight_index;
  int32_t ncases;
  uint8_t raw_bias[8];
  char creation_date[10];
  char creation_time[9];
  char file_label[65];
  bool zmagic;

  read_string (r, rec_type, sizeof rec_type);
  read_string (r, eye_catcher, sizeof eye_catcher);

  if (!strcmp ("$FL2", rec_type))
    zmagic = false;
  else if (!strcmp ("$FL3", rec_type))
    zmagic = true;
  else
    sys_error (r, "This is not an SPSS system file.");

  /* Identify integer format. */
  read_bytes (r, raw_layout_code, sizeof raw_layout_code);
  if ((!integer_identify (2, raw_layout_code, sizeof raw_layout_code,
                          &r->integer_format)
       && !integer_identify (3, raw_layout_code, sizeof raw_layout_code,
                             &r->integer_format))
      || (r->integer_format != INTEGER_MSB_FIRST
          && r->integer_format != INTEGER_LSB_FIRST))
    sys_error (r, "This is not an SPSS system file.");
  layout_code = integer_get (r->integer_format,
                             raw_layout_code, sizeof raw_layout_code);

  read_int (r);                 /* Nominal case size (not actually useful). */
  compressed = read_int (r);
  weight_index = read_int (r);
  ncases = read_int (r);

  if (!zmagic)
    {
      if (compressed == 0)
        r->compression = COMP_NONE;
      else if (compressed == 1)
        r->compression = COMP_SIMPLE;
      else if (compressed != 0)
        sys_error (r, "SAV file header has invalid compression value "
                   "%"PRId32".", compressed);
    }
  else
    {
      if (compressed == 2)
        r->compression = COMP_ZLIB;
      else
        sys_error (r, "ZSAV file header has invalid compression value "
                   "%"PRId32".", compressed);
    }

  /* Identify floating-point format and obtain compression bias. */
  read_bytes (r, raw_bias, sizeof raw_bias);
  if (float_identify (100.0, raw_bias, sizeof raw_bias, &r->float_format) == 0)
    {
      sys_warn (r, "Compression bias is not the usual value of 100, or system "
                "file uses unrecognized floating-point format.");
      if (r->integer_format == INTEGER_MSB_FIRST)
        r->float_format = FLOAT_IEEE_DOUBLE_BE;
      else
        r->float_format = FLOAT_IEEE_DOUBLE_LE;
    }
  r->bias = float_get_double (r->float_format, raw_bias);

  read_string (r, creation_date, sizeof creation_date);
  read_string (r, creation_time, sizeof creation_time);
  read_string (r, file_label, sizeof file_label);
  trim_spaces (file_label);
  skip_bytes (r, 3);

  printf ("File header record:\n");
  printf ("\t%17s: %s\n", "Product name", eye_catcher);
  printf ("\t%17s: %"PRId32"\n", "Layout code", layout_code);
  printf ("\t%17s: %"PRId32" (%s)\n", "Compressed",
          compressed,
          r->compression == COMP_NONE ? "no compression"
          : r->compression == COMP_SIMPLE ? "simple compression"
          : r->compression == COMP_ZLIB ? "ZLIB compression"
          : "<error>");
  printf ("\t%17s: %"PRId32"\n", "Weight index", weight_index);
  printf ("\t%17s: %"PRId32"\n", "Number of cases", ncases);
  printf ("\t%17s: %.*g\n", "Compression bias", DBL_DIG + 1, r->bias);
  printf ("\t%17s: %s\n", "Creation date", creation_date);
  printf ("\t%17s: %s\n", "Creation time", creation_time);
  printf ("\t%17s: \"%s\"\n", "File label", file_label);
}

static const char *
format_name (int format)
{
  switch ((format >> 16) & 0xff)
    {
    case 1: return "A";
    case 2: return "AHEX";
    case 3: return "COMMA";
    case 4: return "DOLLAR";
    case 5: return "F";
    case 6: return "IB";
    case 7: return "PIBHEX";
    case 8: return "P";
    case 9: return "PIB";
    case 10: return "PK";
    case 11: return "RB";
    case 12: return "RBHEX";
    case 15: return "Z";
    case 16: return "N";
    case 17: return "E";
    case 20: return "DATE";
    case 21: return "TIME";
    case 22: return "DATETIME";
    case 23: return "ADATE";
    case 24: return "JDATE";
    case 25: return "DTIME";
    case 26: return "WKDAY";
    case 27: return "MONTH";
    case 28: return "MOYR";
    case 29: return "QYR";
    case 30: return "WKYR";
    case 31: return "PCT";
    case 32: return "DOT";
    case 33: return "CCA";
    case 34: return "CCB";
    case 35: return "CCC";
    case 36: return "CCD";
    case 37: return "CCE";
    case 38: return "EDATE";
    case 39: return "SDATE";
    default: return "invalid";
    }
}

/* Reads a variable (type 2) record from R and adds the
   corresponding variable to DICT.
   Also skips past additional variable records for long string
   variables. */
static void
read_variable_record (struct sfm_reader *r)
{
  int width;
  int has_variable_label;
  int missing_value_code;
  int print_format;
  int write_format;
  char name[9];

  printf ("%08llx: variable record #%d\n",
          (long long int) ftello (r->file), r->n_variable_records++);

  width = read_int (r);
  has_variable_label = read_int (r);
  missing_value_code = read_int (r);
  print_format = read_int (r);
  write_format = read_int (r);
  read_string (r, name, sizeof name);
  name[strcspn (name, " ")] = '\0';

  if (width >= 0)
    r->n_variables++;

  if (r->n_var_widths >= r->allocated_var_widths)
    r->var_widths = x2nrealloc (r->var_widths, &r->allocated_var_widths,
                                sizeof *r->var_widths);
  r->var_widths[r->n_var_widths++] = width;

  printf ("\tWidth: %d (%s)\n",
          width,
          width > 0 ? "string"
          : width == 0 ? "numeric"
          : "long string continuation record");
  printf ("\tVariable label: %d\n", has_variable_label);
  printf ("\tMissing values code: %d (%s)\n", missing_value_code,
          (missing_value_code == 0 ? "no missing values"
           : missing_value_code == 1 ? "one missing value"
           : missing_value_code == 2 ? "two missing values"
           : missing_value_code == 3 ? "three missing values"
           : missing_value_code == -2 ? "one missing value range"
           : missing_value_code == -3 ? "one missing value, one range"
           : "bad value"));
  printf ("\tPrint format: %06x (%s%d.%d)\n",
          print_format, format_name (print_format),
          (print_format >> 8) & 0xff, print_format & 0xff);
  printf ("\tWrite format: %06x (%s%d.%d)\n",
          write_format, format_name (write_format),
          (write_format >> 8) & 0xff, write_format & 0xff);
  printf ("\tName: %s\n", name);

  /* Get variable label, if any. */
  if (has_variable_label != 0 && has_variable_label != 1)
    sys_error (r, "Variable label indicator field is not 0 or 1.");
  if (has_variable_label == 1)
    {
      long long int offset = ftello (r->file);
      size_t len;
      char *label;

      len = read_int (r);

      /* Read up to 255 bytes of label. */
      label = xmalloc (len + 1);
      read_string (r, label, len + 1);
      printf("\t%08llx Variable label: \"%s\"\n", offset, label);
      free (label);

      /* Skip label padding up to multiple of 4 bytes. */
      skip_bytes (r, ROUND_UP (len, 4) - len);
    }

  /* Set missing values. */
  if (missing_value_code != 0)
    {
      int i;

      printf ("\t%08llx Missing values:", (long long int) ftello (r->file));
      if (!width)
        {
          if (missing_value_code < -3 || missing_value_code > 3
              || missing_value_code == -1)
            sys_error (r, "Numeric missing value indicator field is not "
                       "-3, -2, 0, 1, 2, or 3.");
          if (missing_value_code < 0)
            {
              double low = read_float (r);
              double high = read_float (r);
              printf (" %.*g...%.*g", DBL_DIG + 1, low, DBL_DIG + 1, high);
              missing_value_code = -missing_value_code - 2;
            }
          for (i = 0; i < missing_value_code; i++)
            printf (" %.*g", DBL_DIG + 1, read_float (r));
        }
      else if (width > 0)
        {
          if (missing_value_code < 1 || missing_value_code > 3)
            sys_error (r, "String missing value indicator field is not "
                       "0, 1, 2, or 3.");
          for (i = 0; i < missing_value_code; i++)
            {
              char string[9];
              read_string (r, string, sizeof string);
              printf (" \"%s\"", string);
            }
        }
      putchar ('\n');
    }
}

static void
print_untyped_value (struct sfm_reader *r, char raw_value[8])
{
  int n_printable;
  double value;

  value = float_get_double (r->float_format, raw_value);
  for (n_printable = 0; n_printable < 8; n_printable++)
    if (!isprint (raw_value[n_printable]))
      break;

  printf ("%.*g/\"%.*s\"", DBL_DIG + 1, value, n_printable, raw_value);
}

/* Reads value labels from sysfile R and inserts them into the
   associated dictionary. */
static void
read_value_label_record (struct sfm_reader *r)
{
  int label_cnt, var_cnt;
  int i;

  printf ("%08llx: value labels record\n", (long long int) ftello (r->file));

  /* Read number of labels. */
  label_cnt = read_int (r);
  for (i = 0; i < label_cnt; i++)
    {
      char raw_value[8];
      unsigned char label_len;
      size_t padded_len;
      char label[256];

      read_bytes (r, raw_value, sizeof raw_value);

      /* Read label length. */
      read_bytes (r, &label_len, sizeof label_len);
      padded_len = ROUND_UP (label_len + 1, 8);

      /* Read label, padding. */
      read_bytes (r, label, padded_len - 1);
      label[label_len] = 0;

      printf ("\t");
      print_untyped_value (r, raw_value);
      printf (": \"%s\"\n", label);
    }

  /* Now, read the type 4 record that has the list of variables
     to which the value labels are to be applied. */

  /* Read record type of type 4 record. */
  if (read_int (r) != 4)
    sys_error (r, "Variable index record (type 4) does not immediately "
               "follow value label record (type 3) as it should.");

  /* Read number of variables associated with value label from type 4
     record. */
  printf ("\t%08llx: apply to variables", (long long int) ftello (r->file));
  var_cnt = read_int (r);
  for (i = 0; i < var_cnt; i++)
    printf (" #%d", read_int (r));
  putchar ('\n');
}

static void
read_document_record (struct sfm_reader *r)
{
  int n_lines;
  int i;

  printf ("%08llx: document record\n", (long long int) ftello (r->file));
  n_lines = read_int (r);
  printf ("\t%d lines of documents\n", n_lines);

  for (i = 0; i < n_lines; i++)
    {
      char line[81];
      printf ("\t%08llx: ", (long long int) ftello (r->file));
      read_string (r, line, sizeof line);
      trim_spaces (line);
      printf ("line %d: \"%s\"\n", i, line);
    }
}

static void
read_extension_record (struct sfm_reader *r)
{
  long long int offset = ftello (r->file);
  int subtype = read_int (r);
  size_t size = read_int (r);
  size_t count = read_int (r);
  size_t bytes = size * count;

  printf ("%08llx: Record 7, subtype %d, size=%zu, count=%zu\n",
          offset, subtype, size, count);

  switch (subtype)
    {
    case 3:
      read_machine_integer_info (r, size, count);
      return;

    case 4:
      read_machine_float_info (r, size, count);
      return;

    case 6:
      /* DATE variable information.  We don't use it yet, but we
         should. */
      break;

    case 7:
    case 19:
      read_mrsets (r, size, count);
      return;

    case 10:
      read_extra_product_info (r, size, count);
      return;

    case 11:
      read_display_parameters (r, size, count);
      return;

    case 13:
      read_long_var_name_map (r, size, count);
      return;

    case 14:
      read_long_string_map (r, size, count);
      return;

    case 16:
      read_ncases64 (r, size, count);
      return;

    case 17:
      read_datafile_attributes (r, size, count);
      return;

    case 18:
      read_variable_attributes (r, size, count);
      return;

    case 20:
      read_character_encoding (r, size, count);
      return;

    case 21:
      read_long_string_value_labels (r, size, count);
      return;

    case 22:
      read_long_string_missing_values (r, size, count);
      return;

    default:
      sys_warn (r, "Unrecognized record type 7, subtype %d.", subtype);
      read_unknown_extension (r, size, count);
      return;
    }

  skip_bytes (r, bytes);
}

static void
read_machine_integer_info (struct sfm_reader *r, size_t size, size_t count)
{
  long long int offset = ftello (r->file);
  int version_major = read_int (r);
  int version_minor = read_int (r);
  int version_revision = read_int (r);
  int machine_code = read_int (r);
  int float_representation = read_int (r);
  int compression_code = read_int (r);
  int integer_representation = read_int (r);
  int character_code = read_int (r);

  printf ("%08llx: machine integer info\n", offset);
  if (size != 4 || count != 8)
    sys_error (r, "Bad size (%zu) or count (%zu) field on record type 7, "
               "subtype 3.", size, count);

  printf ("\tVersion: %d.%d.%d\n",
          version_major, version_minor, version_revision);
  printf ("\tMachine code: %d\n", machine_code);
  printf ("\tFloating point representation: %d (%s)\n",
          float_representation,
          float_representation == 1 ? "IEEE 754"
          : float_representation == 2 ? "IBM 370"
          : float_representation == 3 ? "DEC VAX"
          : "unknown");
  printf ("\tCompression code: %d\n", compression_code);
  printf ("\tEndianness: %d (%s)\n", integer_representation,
          integer_representation == 1 ? "big"
          : integer_representation == 2 ? "little" : "unknown");
  printf ("\tCharacter code: %d\n", character_code);
}

/* Read record type 7, subtype 4. */
static void
read_machine_float_info (struct sfm_reader *r, size_t size, size_t count)
{
  long long int offset = ftello (r->file);
  double sysmis = read_float (r);
  double highest = read_float (r);
  double lowest = read_float (r);

  printf ("%08llx: machine float info\n", offset);
  if (size != 8 || count != 3)
    sys_error (r, "Bad size (%zu) or count (%zu) on extension 4.",
               size, count);

  printf ("\tsysmis: %.*g (%a)\n", DBL_DIG + 1, sysmis, sysmis);
  if (sysmis != SYSMIS)
    sys_warn (r, "File specifies unexpected value %.*g (%a) as %s.",
              DBL_DIG + 1, sysmis, sysmis, "SYSMIS");

  printf ("\thighest: %.*g (%a)\n", DBL_DIG + 1, highest, highest);
  if (highest != HIGHEST)
    sys_warn (r, "File specifies unexpected value %.*g (%a) as %s.",
              DBL_DIG + 1, highest, highest, "HIGHEST");

  printf ("\tlowest: %.*g (%a)\n", DBL_DIG + 1, lowest, lowest);
  if (lowest != LOWEST && lowest != SYSMIS)
    sys_warn (r, "File specifies unexpected value %.*g (%a) as %s.",
              DBL_DIG + 1, lowest, lowest, "LOWEST");
}

static void
read_extra_product_info (struct sfm_reader *r,
                         size_t size, size_t count)
{
  struct text_record *text;
  const char *s;

  printf ("%08llx: extra product info\n", (long long int) ftello (r->file));
  text = open_text_record (r, size * count);
  s = text_get_all (text);
  print_string (s, strlen (s));
  close_text_record (text);
}

/* Read record type 7, subtype 7. */
static void
read_mrsets (struct sfm_reader *r, size_t size, size_t count)
{
  struct text_record *text;

  printf ("%08llx: multiple response sets\n",
          (long long int) ftello (r->file));
  text = open_text_record (r, size * count);
  for (;;)
    {
      const char *name;
      enum { MRSET_MC, MRSET_MD } type;
      bool cat_label_from_counted_values = false;
      bool label_from_var_label = false;
      const char *counted;
      const char *label;
      const char *variables;

      while (text_match (text, '\n'))
        continue;

      name = text_tokenize (text, '=');
      if (name == NULL)
        break;

      if (text_match (text, 'C'))
        {
          type = MRSET_MC;
          counted = NULL;
          if (!text_match (text, ' '))
            {
              sys_warn (r, "missing space following 'C' at offset %zu "
                        "in mrsets record", text_pos (text));
              break;
            }
        }
      else if (text_match (text, 'D'))
        {
          type = MRSET_MD;
        }
      else if (text_match (text, 'E'))
        {
          char *number;

          type = MRSET_MD;
          cat_label_from_counted_values = true;

          if (!text_match (text, ' '))
            {
              sys_warn (r, "Missing space following `%c' at offset %zu "
                        "in MRSETS record", 'E', text_pos (text));
              break;
            }

          number = text_tokenize (text, ' ');
          if (!strcmp (number, "11"))
            label_from_var_label = true;
          else if (strcmp (number, "1"))
            sys_warn (r, "Unexpected label source value `%s' "
                      "following `E' at offset %zu in MRSETS record",
                      number, text_pos (text));

        }
      else
        {
          sys_warn (r, "missing `C', `D', or `E' at offset %zu "
                    "in mrsets record", text_pos (text));
          break;
        }

      if (type == MRSET_MD)
        {
          counted = text_parse_counted_string (text);
          if (counted == NULL)
            break;
        }

      label = text_parse_counted_string (text);
      if (label == NULL)
        break;

      variables = text_tokenize (text, '\n');

      printf ("\t\"%s\": multiple %s set",
              name, type == MRSET_MC ? "category" : "dichotomy");
      if (counted != NULL)
        printf (", counted value \"%s\"", counted);
      if (cat_label_from_counted_values)
        printf (", category labels from counted values");
      if (label[0] != '\0')
        printf (", label \"%s\"", label);
      if (label_from_var_label)
        printf (", label from variable label");
      if (variables != NULL)
        printf(", variables \"%s\"\n", variables);
      else
        printf(", no variables\n");
    }
  close_text_record (text);
}

/* Read record type 7, subtype 11. */
static void
read_display_parameters (struct sfm_reader *r, size_t size, size_t count)
{
  size_t n_vars;
  bool includes_width;
  size_t i;

  printf ("%08llx: variable display parameters\n",
          (long long int) ftello (r->file));
  if (size != 4)
    {
      sys_warn (r, "Bad size %zu on extension 11.", size);
      skip_bytes (r, size * count);
      return;
    }

  n_vars = r->n_variables;
  if (count == 3 * n_vars)
    includes_width = true;
  else if (count == 2 * n_vars)
    includes_width = false;
  else
    {
      sys_warn (r, "Extension 11 has bad count %zu (for %zu variables.",
                count, n_vars);
      skip_bytes (r, size * count);
      return;
    }

  for (i = 0; i < n_vars; ++i)
    {
      int measure = read_int (r);
      int width = includes_width ? read_int (r) : 0;
      int align = read_int (r);

      printf ("\tVar #%zu: measure=%d (%s)", i, measure,
              (measure == 1 ? "nominal"
               : measure == 2 ? "ordinal"
               : measure == 3 ? "scale"
               : "invalid"));
      if (includes_width)
        printf (", width=%d", width);
      printf (", align=%d (%s)\n", align,
              (align == 0 ? "left"
               : align == 1 ? "right"
               : align == 2 ? "centre"
               : "invalid"));
    }
}

/* Reads record type 7, subtype 13, which gives the long name
   that corresponds to each short name.  */
static void
read_long_var_name_map (struct sfm_reader *r, size_t size, size_t count)
{
  struct text_record *text;
  char *var;
  char *long_name;

  printf ("%08llx: long variable names (short => long)\n",
          (long long int) ftello (r->file));
  text = open_text_record (r, size * count);
  while (read_variable_to_value_pair (text, &var, &long_name))
    printf ("\t%s => %s\n", var, long_name);
  close_text_record (text);
}

/* Reads record type 7, subtype 14, which gives the real length
   of each very long string.  Rearranges DICT accordingly. */
static void
read_long_string_map (struct sfm_reader *r, size_t size, size_t count)
{
  struct text_record *text;
  char *var;
  char *length_s;

  printf ("%08llx: very long strings (variable => length)\n",
          (long long int) ftello (r->file));
  text = open_text_record (r, size * count);
  while (read_variable_to_value_pair (text, &var, &length_s))
    printf ("\t%s => %d\n", var, atoi (length_s));
  close_text_record (text);
}

static bool
read_attributes (struct sfm_reader *r, struct text_record *text,
                 const char *variable)
{
  const char *key;
  int index;

  for (;;) 
    {
      key = text_tokenize (text, '(');
      if (key == NULL)
        return true;
  
      for (index = 1; ; index++)
        {
          /* Parse the value. */
          const char *value = text_tokenize (text, '\n');
          if (value == NULL) 
            {
              sys_warn (r, "%s: Error parsing attribute value %s[%d]",
                        variable, key, index);
              return false;
            }
          if (strlen (value) < 2
              || value[0] != '\'' || value[strlen (value) - 1] != '\'')
            sys_warn (r, "%s: Attribute value %s[%d] is not quoted: %s",
                      variable, key, index, value);
          else
            printf ("\t%s: %s[%d] = \"%.*s\"\n",
                    variable, key, index, (int) strlen (value) - 2, value + 1);

          /* Was this the last value for this attribute? */
          if (text_match (text, ')'))
            break;
        }

      if (text_match (text, '/'))
        return true; 
    }
}

/* Read extended number of cases record. */
static void
read_ncases64 (struct sfm_reader *r, size_t size, size_t count)
{
  int64_t unknown, ncases64;

  if (size != 8)
    {
      sys_warn (r, "Bad size %zu for extended number of cases.", size);
      skip_bytes (r, size * count);
      return;
    }
  if (count != 2)
    {
      sys_warn (r, "Bad count %zu for extended number of cases.", size);
      skip_bytes (r, size * count);
      return;
    }
  unknown = read_int64 (r);
  ncases64 = read_int64 (r);
  printf ("%08llx: extended number of cases: "
          "unknown=%"PRId64", ncases64=%"PRId64"\n",
          (long long int) ftello (r->file), unknown, ncases64);
}

static void
read_datafile_attributes (struct sfm_reader *r, size_t size, size_t count) 
{
  struct text_record *text;
  
  printf ("%08llx: datafile attributes\n", (long long int) ftello (r->file));
  text = open_text_record (r, size * count);
  read_attributes (r, text, "datafile");
  close_text_record (text);
}

static void
read_character_encoding (struct sfm_reader *r, size_t size, size_t count)
{
  long long int posn =  ftello (r->file);
  char *encoding = xcalloc (size, count + 1);
  read_string (r, encoding, count + 1);

  printf ("%08llx: Character Encoding: %s\n", posn, encoding);
}

static void
read_long_string_value_labels (struct sfm_reader *r, size_t size, size_t count)
{
  long long int start = ftello (r->file);

  printf ("%08llx: long string value labels\n", start);
  while (ftello (r->file) - start < size * count)
    {
      long long posn = ftello (r->file);
      char var_name[ID_MAX_LEN + 1];
      int var_name_len;
      int n_values;
      int width;
      int i;

      /* Read variable name. */
      var_name_len = read_int (r);
      if (var_name_len > ID_MAX_LEN)
        sys_error (r, "Variable name length in long string value label "
                   "record (%d) exceeds %d-byte limit.",
                   var_name_len, ID_MAX_LEN);
      read_string (r, var_name, var_name_len + 1);

      /* Read width, number of values. */
      width = read_int (r);
      n_values = read_int (r);

      printf ("\t%08llx: %s, width %d, %d values\n",
              posn, var_name, width, n_values);

      /* Read values. */
      for (i = 0; i < n_values; i++)
	{
          char *value;
          int value_length;

          char *label;
	  int label_length;

          posn = ftello (r->file);

          /* Read value. */
          value_length = read_int (r);
          value = xmalloc (value_length + 1);
          read_string (r, value, value_length + 1);

          /* Read label. */
          label_length = read_int (r);
          label = xmalloc (label_length + 1);
          read_string (r, label, label_length + 1);

          printf ("\t\t%08llx: \"%s\" (%d bytes) => \"%s\" (%d bytes)\n",
                  posn, value, value_length, label, label_length);

          free (value);
          free (label);
	}
    }
}

static void
read_long_string_missing_values (struct sfm_reader *r,
                                 size_t size, size_t count)
{
  long long int start = ftello (r->file);

  printf ("%08llx: long string missing values\n", start);
  while (ftello (r->file) - start < size * count)
    {
      long long posn = ftello (r->file);
      char var_name[ID_MAX_LEN + 1];
      uint8_t n_missing_values;
      int var_name_len;
      int i;

      /* Read variable name. */
      var_name_len = read_int (r);
      if (var_name_len > ID_MAX_LEN)
        sys_error (r, "Variable name length in long string value label "
                   "record (%d) exceeds %d-byte limit.",
                   var_name_len, ID_MAX_LEN);
      read_string (r, var_name, var_name_len + 1);

      /* Read number of values. */
      read_bytes (r, &n_missing_values, 1);

      printf ("\t%08llx: %s, %d missing values:",
              posn, var_name, n_missing_values);

      /* Read values. */
      for (i = 0; i < n_missing_values; i++)
	{
          char *value;
          int value_length;

          posn = ftello (r->file);

          /* Read value. */
          value_length = read_int (r);
          value = xmalloc (value_length + 1);
          read_string (r, value, value_length + 1);

          printf (" \"%s\"", value);

          free (value);
	}
      printf ("\n");
    }
}

static void
hex_dump (size_t offset, const void *buffer_, size_t buffer_size)
{
  const uint8_t *buffer = buffer_;

  while (buffer_size > 0)
    {
      size_t n = MIN (buffer_size, 16);
      size_t i;

      printf ("%04zx", offset);
      for (i = 0; i < 16; i++)
        {
          if (i < n)
            printf ("%c%02x", i == 8 ? '-' : ' ', buffer[i]);
          else
            printf ("   ");
        }

      printf (" |");
      for (i = 0; i < 16; i++)
        {
          unsigned char c = i < n ? buffer[i] : ' ';
          putchar (isprint (c) ? c : '.');
        }
      printf ("|\n");

      offset += n;
      buffer += n;
      buffer_size -= n;
    }
}

/* Reads and prints any type 7 record that we don't understand. */
static void
read_unknown_extension (struct sfm_reader *r, size_t size, size_t count)
{
  unsigned char *buffer;
  size_t i;

  if (size == 0 || count > 65536 / size)
    skip_bytes (r, size * count);
  else if (size != 1)
    {
      buffer = xmalloc (size);
      for (i = 0; i < count; i++)
        {
          read_bytes (r, buffer, size);
          hex_dump (i * size, buffer, size);
        }
      free (buffer);
    }
  else
    {
      buffer = xmalloc (count);
      read_bytes (r, buffer, count);
      print_string (CHAR_CAST (char *, buffer), count);
      free (buffer);
    }
}

static void
read_variable_attributes (struct sfm_reader *r, size_t size, size_t count) 
{
  struct text_record *text;
  
  printf ("%08llx: variable attributes\n", (long long int) ftello (r->file));
  text = open_text_record (r, size * count);
  for (;;) 
    {
      const char *variable = text_tokenize (text, ':');
      if (variable == NULL || !read_attributes (r, text, variable))
        break; 
    }
  close_text_record (text);
}

static void
read_simple_compressed_data (struct sfm_reader *r, int max_cases)
{
  enum { N_OPCODES = 8 };
  uint8_t opcodes[N_OPCODES];
  long long int opcode_ofs;
  int opcode_idx;
  int case_num;
  int i;

  read_int (r);
  printf ("\n%08llx: compressed data:\n", (long long int) ftello (r->file));

  opcode_idx = N_OPCODES;
  opcode_ofs = 0;
  case_num = 0;
  for (case_num = 0; case_num < max_cases; case_num++)
    {
      printf ("%08llx: case %d's uncompressible data begins\n",
              (long long int) ftello (r->file), case_num);
      for (i = 0; i < r->n_var_widths; )
        {
          int width = r->var_widths[i];
          char raw_value[8];
          int opcode;

          if (opcode_idx >= N_OPCODES)
            {
              opcode_ofs = ftello (r->file);
              if (i == 0)
                {
                  if (!try_read_bytes (r, opcodes, 8))
                    return;
                }
              else
                read_bytes (r, opcodes, 8);
              opcode_idx = 0;
            }
          opcode = opcodes[opcode_idx];
          printf ("%08llx: variable %d: opcode %d: ",
                  opcode_ofs + opcode_idx, i, opcode);

          switch (opcode)
            {
            default:
              printf ("%.*g", DBL_DIG + 1, opcode - r->bias);
              if (width != 0)
                printf (", but this is a string variable (width=%d)", width);
              printf ("\n");
              i++;
              break;

            case 0:
              printf ("ignored padding\n");
              break;

            case 252:
              printf ("end of data\n");
              return;

            case 253:
              read_bytes (r, raw_value, 8);
              printf ("uncompressible data: ");
              print_untyped_value (r, raw_value);
              printf ("\n");
              i++;
              break;

            case 254:
              printf ("spaces");
              if (width == 0)
                printf (", but this is a numeric variable");
              printf ("\n");
              i++;
              break;

            case 255:
              printf ("SYSMIS");
              if (width != 0)
                printf (", but this is a string variable (width=%d)", width);
              printf ("\n");
              i++;
              break;
            }

          opcode_idx++;
        }
    }
}

static void
read_zlib_compressed_data (struct sfm_reader *r)
{
  long long int ofs;
  long long int this_ofs, next_ofs, next_len;
  long long int bias, zero;
  long long int expected_uncmp_ofs, expected_cmp_ofs;
  unsigned int block_size, n_blocks;
  unsigned int i;

  read_int (r);
  ofs = ftello (r->file);
  printf ("\n%08llx: ZLIB compressed data header:\n", ofs);

  this_ofs = read_int64 (r);
  next_ofs = read_int64 (r);
  next_len = read_int64 (r);

  printf ("\tzheader_ofs: 0x%llx\n", this_ofs);
  if (this_ofs != ofs)
    printf ("\t\t(Expected 0x%llx.)\n", ofs);
  printf ("\tztrailer_ofs: 0x%llx\n", next_ofs);
  printf ("\tztrailer_len: %lld\n", next_len);
  if (next_len < 24 || next_len % 24)
    printf ("\t\t(Trailer length is not a positive multiple of 24.)\n");

  printf ("\n%08llx: 0x%llx bytes of ZLIB compressed data\n",
          ofs + 8 * 3, next_ofs - (ofs + 8 * 3));

  skip_bytes (r, next_ofs - (ofs + 8 * 3));

  printf ("\n%08llx: ZLIB trailer fixed header:\n", next_ofs);
  bias = read_int64 (r);
  zero = read_int64 (r);
  block_size = read_int (r);
  n_blocks = read_int (r);
  printf ("\tbias: %lld\n", bias);
  printf ("\tzero: 0x%llx\n", zero);
  if (zero != 0)
    printf ("\t\t(Expected 0.)\n");
  printf ("\tblock_size: 0x%x\n", block_size);
  if (block_size != 0x3ff000)
    printf ("\t\t(Expected 0x3ff000.)\n");
  printf ("\tn_blocks: %u\n", n_blocks);
  if (n_blocks != next_len / 24 - 1)
    printf ("\t\t(Expected %llu.)\n", next_len / 24 - 1);

  expected_uncmp_ofs = ofs;
  expected_cmp_ofs = ofs + 24;
  for (i = 0; i < n_blocks; i++)
    {
      long long int blockinfo_ofs = ftello (r->file);
      unsigned long long int uncompressed_ofs = read_int64 (r);
      unsigned long long int compressed_ofs = read_int64 (r);
      unsigned int uncompressed_size = read_int (r);
      unsigned int compressed_size = read_int (r);

      printf ("\n%08llx: ZLIB block descriptor %d\n", blockinfo_ofs, i + 1);

      printf ("\tuncompressed_ofs: 0x%llx\n", uncompressed_ofs);
      if (uncompressed_ofs != expected_uncmp_ofs)
        printf ("\t\t(Expected 0x%llx.)\n", ofs);

      printf ("\tcompressed_ofs: 0x%llx\n", compressed_ofs);
      if (compressed_ofs != expected_cmp_ofs)
        printf ("\t\t(Expected 0x%llx.)\n", ofs + 24);

      printf ("\tuncompressed_size: 0x%x\n", uncompressed_size);
      if (i < n_blocks - 1 && uncompressed_size != block_size)
        printf ("\t\t(Expected 0x%x.)\n", block_size);

      printf ("\tcompressed_size: 0x%x\n", compressed_size);
      if (i == n_blocks - 1 && compressed_ofs + compressed_size != next_ofs)
        printf ("\t\t(This was expected to be 0x%llx.)\n",
                next_ofs - compressed_size);

      expected_uncmp_ofs += uncompressed_size;
      expected_cmp_ofs += compressed_size;
    }
}

/* Helpers for reading records that consist of structured text
   strings. */

/* State. */
struct text_record
  {
    struct sfm_reader *reader;  /* Reader. */
    char *buffer;               /* Record contents. */
    size_t size;                /* Size of buffer. */
    size_t pos;                 /* Current position in buffer. */
  };

/* Reads SIZE bytes into a text record for R,
   and returns the new text record. */
static struct text_record *
open_text_record (struct sfm_reader *r, size_t size)
{
  struct text_record *text = xmalloc (sizeof *text);
  char *buffer = xmalloc (size + 1);
  read_bytes (r, buffer, size);
  buffer[size] = '\0';
  text->reader = r;
  text->buffer = buffer;
  text->size = size;
  text->pos = 0;
  return text;
}

/* Closes TEXT and frees its storage.
   Not really needed, because the pool will free the text record anyway,
   but can be used to free it earlier. */
static void
close_text_record (struct text_record *text)
{
  free (text->buffer);
  free (text);
}

static char *
text_tokenize (struct text_record *text, int delimiter)
{
  size_t start = text->pos;
  while (text->pos < text->size
         && text->buffer[text->pos] != delimiter
         && text->buffer[text->pos] != '\0')
    text->pos++;
  if (start == text->pos)
    return NULL;
  text->buffer[text->pos++] = '\0';
  return &text->buffer[start];
}

static bool
text_match (struct text_record *text, int c) 
{
  if (text->pos < text->size && text->buffer[text->pos] == c) 
    {
      text->pos++;
      return true;
    }
  else
    return false;
}

/* Reads a integer value expressed in decimal, then a space, then a string that
   consists of exactly as many bytes as specified by the integer, then a space,
   from TEXT.  Returns the string, null-terminated, as a subset of TEXT's
   buffer (so the caller should not free the string). */
static const char *
text_parse_counted_string (struct text_record *text)
{
  size_t start;
  size_t n;
  char *s;

  start = text->pos;
  n = 0;
  while (isdigit ((unsigned char) text->buffer[text->pos]))
    n = (n * 10) + (text->buffer[text->pos++] - '0');
  if (start == text->pos)
    {
      sys_error (text->reader, "expecting digit at offset %zu in record",
                 text->pos);
      return NULL;
    }

  if (!text_match (text, ' '))
    {
      sys_error (text->reader, "expecting space at offset %zu in record",
                 text->pos);
      return NULL;
    }

  if (text->pos + n > text->size)
    {
      sys_error (text->reader, "%zu-byte string starting at offset %zu "
                 "exceeds record length %zu", n, text->pos, text->size);
      return NULL;
    }

  s = &text->buffer[text->pos];
  if (s[n] != ' ')
    {
      sys_error (text->reader, "expecting space at offset %zu following "
                 "%zu-byte string", text->pos + n, n);
      return NULL;
    }
  s[n] = '\0';
  text->pos += n + 1;
  return s;
}

/* Reads a variable=value pair from TEXT.
   Looks up the variable in DICT and stores it into *VAR.
   Stores a null-terminated value into *VALUE. */
static bool
read_variable_to_value_pair (struct text_record *text,
                             char **key, char **value)
{
  *key = text_tokenize (text, '=');
  *value = text_tokenize (text, '\t');
  if (!*key || !*value)
    return false;

  while (text->pos < text->size
         && (text->buffer[text->pos] == '\t'
             || text->buffer[text->pos] == '\0'))
    text->pos++;
  return true;
}

/* Returns the current byte offset inside the TEXT's string. */
static size_t
text_pos (const struct text_record *text)
{
  return text->pos;
}

static const char *
text_get_all (const struct text_record *text)
{
  return text->buffer;
}

static void
usage (void)
{
  printf ("\
%s, a utility for dissecting system files.\n\
Usage: %s [OPTION]... SYSFILE...\n\
where each SYSFILE is the name of a system file.\n\
\n\
Options:\n\
  --data[=MAXCASES]   print (up to MAXCASES cases of) compressed data\n\
  --help              display this help and exit\n\
  --version           output version information and exit\n",
          program_name, program_name);
}

/* Displays a corruption message. */
static void
sys_msg (struct sfm_reader *r, const char *format, va_list args)
{
  printf ("\"%s\" near offset 0x%llx: ",
          r->file_name, (long long int) ftello (r->file));
  vprintf (format, args);
  putchar ('\n');
}

/* Displays a warning for the current file position. */
static void
sys_warn (struct sfm_reader *r, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  sys_msg (r, format, args);
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
  sys_msg (r, format, args);
  va_end (args);

  exit (EXIT_FAILURE);
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
    sys_error (r, "System error: %s.", strerror (errno));
  else if (!eof_is_ok || bytes_read != 0)
    sys_error (r, "Unexpected end of file.");
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

/* Reads a 64-bit signed integer from R and returns its value in
   host format. */
static int64_t
read_int64 (struct sfm_reader *r)
{
  uint8_t integer[8];
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

static void
trim_spaces (char *s)
{
  char *end = strchr (s, '\0');
  while (end > s && end[-1] == ' ')
    end--;
  *end = '\0';
}

static void
print_string (const char *s, size_t len)
{
  if (memchr (s, 0, len) == 0)
    {
      size_t i;

      for (i = 0; i < len; i++)
        {
          unsigned char c = s[i];

          if (c == '\\')
            printf ("\\\\");
          else if (c == '\n' || isprint (c))
            putchar (c);
          else
            printf ("\\%02x", c);
        }
      putchar ('\n');
    }
  else
    hex_dump (0, s, len);
}
