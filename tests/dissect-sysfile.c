/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2008 Free Software Foundation, Inc.

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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <data/val-type.h>
#include <libpspp/compiler.h>
#include <libpspp/float-format.h>
#include <libpspp/integer-format.h>
#include <libpspp/misc.h>

#include "error.h"
#include "minmax.h"
#include "progname.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct sfm_reader
  {
    const char *file_name;
    FILE *file;

    int n_variable_records, n_variables;

    enum integer_format integer_format;
    enum float_format float_format;
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
static void read_character_encoding (struct sfm_reader *r,
				       size_t size, size_t count);


static struct text_record *open_text_record (
  struct sfm_reader *, size_t size);
static void close_text_record (struct text_record *);
static bool read_variable_to_value_pair (struct text_record *,
                                         char **key, char **value);
static char *text_tokenize (struct text_record *, int delimiter);
static bool text_match (struct text_record *text, int c);

static void usage (int exit_code);
static void sys_warn (struct sfm_reader *, const char *, ...)
     PRINTF_FORMAT (2, 3);
static void sys_error (struct sfm_reader *, const char *, ...)
     PRINTF_FORMAT (2, 3)
     NO_RETURN;

static void read_bytes (struct sfm_reader *, void *, size_t);
static int read_int (struct sfm_reader *);
static double read_float (struct sfm_reader *);
static void read_string (struct sfm_reader *, char *, size_t);
static void skip_bytes (struct sfm_reader *, size_t);
static void trim_spaces (char *);

int
main (int argc, char *argv[])
{
  struct sfm_reader r;
  int i;

  set_program_name (argv[0]);
  if (argc < 2)
    usage (EXIT_FAILURE);

  for (i = 1; i < argc; i++) 
    {
      int rec_type;

      r.file_name = argv[i];
      r.file = fopen (r.file_name, "rb");
      if (r.file == NULL)
        error (EXIT_FAILURE, errno, "error opening \"%s\"", r.file_name);
      r.n_variable_records = 0;
      r.n_variables = 0;

      if (argc > 2)
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
              sys_error (&r, _("Misplaced type 4 record."));

            case 6:
              read_document_record (&r);
              break;

            case 7:
              read_extension_record (&r);
              break;

            default:
              sys_error (&r, _("Unrecognized record type %d."), rec_type);
            }
        }
      printf ("%08lx: end-of-dictionary record "
              "(first byte of data at %08lx)\n",
              ftell (r.file), ftell (r.file) + 4);

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
  int32_t nominal_case_size;
  int32_t compressed;
  int32_t weight_index;
  int32_t ncases;
  uint8_t raw_bias[8];
  double bias;
  char creation_date[10];
  char creation_time[9];
  char file_label[65];

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
  layout_code = integer_get (r->integer_format,
                             raw_layout_code, sizeof raw_layout_code);

  nominal_case_size = read_int (r);
  compressed = read_int (r) != 0;
  weight_index = read_int (r);
  ncases = read_int (r);

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
  bias = float_get_double (r->float_format, raw_bias);

  read_string (r, creation_date, sizeof creation_date);
  read_string (r, creation_time, sizeof creation_time);
  read_string (r, file_label, sizeof file_label);
  trim_spaces (file_label);
  skip_bytes (r, 3);

  printf ("File header record:\n");
  printf ("\t%17s: %s\n", "Product name", eye_catcher);
  printf ("\t%17s: %"PRId32"\n", "Layout code", layout_code);
  printf ("\t%17s: %"PRId32"\n", "Compressed", compressed);
  printf ("\t%17s: %"PRId32"\n", "Weight index", weight_index);
  printf ("\t%17s: %"PRId32"\n", "Number of cases", ncases);
  printf ("\t%17s: %g\n", "Compression bias", bias);
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

  printf ("%08lx: variable record #%d\n",
          ftell (r->file), r->n_variable_records++);

  width = read_int (r);
  has_variable_label = read_int (r);
  missing_value_code = read_int (r);
  print_format = read_int (r);
  write_format = read_int (r);
  read_string (r, name, sizeof name);
  name[strcspn (name, " ")] = '\0';

  if (width >= 0)
    r->n_variables++;

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
    sys_error (r, _("Variable label indicator field is not 0 or 1."));
  if (has_variable_label == 1)
    {
      long int offset = ftell (r->file);
      size_t len;
      char label[255 + 1];

      len = read_int (r);
      if (len >= sizeof label)
        sys_error (r, _("Variable %s has label of invalid length %zu."),
                   name, len);
      read_string (r, label, len + 1);
      printf("\t%08lx Variable label: \"%s\"\n", offset, label);

      skip_bytes (r, ROUND_UP (len, 4) - len);
    }

  /* Set missing values. */
  if (missing_value_code != 0)
    {
      int i;

      printf ("\t%08lx Missing values:", ftell (r->file));
      if (!width)
        {
          if (missing_value_code < -3 || missing_value_code > 3
              || missing_value_code == -1)
            sys_error (r, _("Numeric missing value indicator field is not "
                            "-3, -2, 0, 1, 2, or 3."));
          if (missing_value_code < 0)
            {
              double low = read_float (r);
              double high = read_float (r);
              printf (" %g...%g", low, high);
              missing_value_code = -missing_value_code - 2;
            }
          for (i = 0; i < missing_value_code; i++)
            printf (" %g", read_float (r));
        }
      else if (width > 0)
        {
          if (missing_value_code < 1 || missing_value_code > 3)
            sys_error (r, _("String missing value indicator field is not "
                            "0, 1, 2, or 3."));
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

/* Reads value labels from sysfile R and inserts them into the
   associated dictionary. */
static void
read_value_label_record (struct sfm_reader *r)
{
  int label_cnt, var_cnt;
  int i;

  printf ("%08lx: value labels record\n", ftell (r->file));

  /* Read number of labels. */
  label_cnt = read_int (r);
  for (i = 0; i < label_cnt; i++)
    {
      char raw_value[8];
      double value;
      int n_printable;
      unsigned char label_len;
      size_t padded_len;
      char label[256];

      read_bytes (r, raw_value, sizeof raw_value);
      value = float_get_double (r->float_format, raw_value);
      for (n_printable = 0; n_printable < sizeof raw_value; n_printable++)
        if (!isprint (raw_value[n_printable]))
          break;

      /* Read label length. */
      read_bytes (r, &label_len, sizeof label_len);
      padded_len = ROUND_UP (label_len + 1, 8);

      /* Read label, padding. */
      read_bytes (r, label, padded_len - 1);
      label[label_len] = 0;

      printf ("\t%g/\"%.*s\": \"%s\"\n", value, n_printable, raw_value, label);
    }

  /* Now, read the type 4 record that has the list of variables
     to which the value labels are to be applied. */

  /* Read record type of type 4 record. */
  if (read_int (r) != 4)
    sys_error (r, _("Variable index record (type 4) does not immediately "
                    "follow value label record (type 3) as it should."));

  /* Read number of variables associated with value label from type 4
     record. */
  printf ("\t%08lx: apply to variables", ftell (r->file));
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

  printf ("%08lx: document record\n", ftell (r->file));
  n_lines = read_int (r);
  printf ("\t%d lines of documents\n", n_lines);

  for (i = 0; i < n_lines; i++)
    {
      char line[81];
      printf ("\t%08lx: ", ftell (r->file));
      read_string (r, line, sizeof line);
      trim_spaces (line);
      printf ("line %d: \"%s\"\n", i, line);
    }
}

static void
read_extension_record (struct sfm_reader *r)
{
  long int offset = ftell (r->file);
  int subtype = read_int (r);
  size_t size = read_int (r);
  size_t count = read_int (r);
  size_t bytes = size * count;

  printf ("%08lx: Record 7, subtype %d, size=%zu, count=%zu\n",
          offset, subtype, size, count);

  switch (subtype)
    {
    case 3:
      read_machine_integer_info (r, size, count);
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
      /* Unknown purpose. */
      break;

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
      /* New in SPSS v14?  Unknown purpose.  */
      break;

    case 17:
      read_datafile_attributes (r, size, count);
      return;

    case 18:
      read_variable_attributes (r, size, count);
      return;

    case 20:
      read_character_encoding (r, size, count);
      return;

    default:
      sys_warn (r, _("Unrecognized record type 7, subtype %d."), subtype);
      break;
    }

  skip_bytes (r, bytes);
}

static void
read_machine_integer_info (struct sfm_reader *r, size_t size, size_t count)
{
  long int offset = ftell (r->file);
  int version_major = read_int (r);
  int version_minor = read_int (r);
  int version_revision = read_int (r);
  int machine_code = read_int (r);
  int float_representation = read_int (r);
  int compression_code = read_int (r);
  int integer_representation = read_int (r);
  int character_code = read_int (r);

  printf ("%08lx: machine integer info\n", offset);
  if (size != 4 || count != 8)
    sys_error (r, _("Bad size (%zu) or count (%zu) field on record type 7, "
                    "subtype 3."),
                size, count);

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
  long int offset = ftell (r->file);
  double sysmis = read_float (r);
  double highest = read_float (r);
  double lowest = read_float (r);

  printf ("%08lx: machine float info\n", offset);
  if (size != 8 || count != 3)
    sys_error (r, _("Bad size (%zu) or count (%zu) on extension 4."),
               size, count);

  printf ("\tsysmis: %g\n", sysmis);
  if (sysmis != SYSMIS)
    sys_warn (r, _("File specifies unexpected value %g as SYSMIS."), sysmis);
  printf ("\thighest: %g\n", highest);
  if (highest != HIGHEST)
    sys_warn (r, _("File specifies unexpected value %g as HIGHEST."), highest);
  printf ("\tlowest: %g\n", lowest);
  if (lowest != LOWEST)
    sys_warn (r, _("File specifies unexpected value %g as LOWEST."), lowest);
}

/* Read record type 7, subtype 11. */
static void
read_display_parameters (struct sfm_reader *r, size_t size, size_t count)
{
  size_t n_vars;
  bool includes_width;
  size_t i;

  printf ("%08lx: variable display parameters\n", ftell (r->file));
  if (size != 4)
    {
      sys_warn (r, _("Bad size %zu on extension 11."), size);
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
      sys_warn (r, _("Extension 11 has bad count %zu (for %zu variables)."),
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

  printf ("%08lx: long variable names (short => long)\n", ftell (r->file));
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

  printf ("%08lx: very long strings (variable => length)\n", ftell (r->file));
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
              sys_warn (r, _("%s: Error parsing attribute value %s[%d]"),
                        variable, key, index);
              return false;
            }
          if (strlen (value) < 2
              || value[0] != '\'' || value[strlen (value) - 1] != '\'')
            sys_warn (r, _("%s: Attribute value %s[%d] is not quoted: %s"),
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

static void
read_datafile_attributes (struct sfm_reader *r, size_t size, size_t count) 
{
  struct text_record *text;
  
  printf ("%08lx: datafile attributes\n", ftell (r->file));
  text = open_text_record (r, size * count);
  read_attributes (r, text, "datafile");
  close_text_record (text);
}

static void
read_character_encoding (struct sfm_reader *r, size_t size, size_t count)
{
  const unsigned long int posn =  ftell (r->file);
  char *encoding = calloc (size, count + 1);
  read_string (r, encoding, count + 1);

  printf ("%08lx: Character Encoding: %s\n", posn, encoding);
}


static void
read_variable_attributes (struct sfm_reader *r, size_t size, size_t count) 
{
  struct text_record *text;
  
  printf ("%08lx: variable attributes\n", ftell (r->file));
  text = open_text_record (r, size * count);
  for (;;) 
    {
      const char *variable = text_tokenize (text, ':');
      if (variable == NULL || !read_attributes (r, text, variable))
        break; 
    }
  close_text_record (text);
}

/* Helpers for reading records that consist of structured text
   strings. */

/* State. */
struct text_record
  {
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
  if (text->pos == text->size)
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

static void
usage (int exit_code)
{
  printf ("usage: %s SYSFILE...\n"
          "where each SYSFILE is the name of a system file\n",
          program_name);
  exit (exit_code);
}

/* Displays a corruption message. */
static void
sys_msg (struct sfm_reader *r, const char *format, va_list args)
{
  printf ("\"%s\" near offset 0x%lx: ",
          r->file_name, (unsigned long) ftell (r->file));
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

static void
trim_spaces (char *s)
{
  char *end = strchr (s, '\0');
  while (end > s && end[-1] == ' ')
    end--;
  *end = '\0';
}
