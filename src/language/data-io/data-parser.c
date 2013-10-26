/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include "language/data-io/data-parser.h"

#include <stdint.h>
#include <stdlib.h>

#include "data/casereader-provider.h"
#include "data/data-in.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/file-handle-def.h"
#include "data/settings.h"
#include "language/data-io/data-reader.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "output/tab.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Data parser for textual data like that read by DATA LIST. */
struct data_parser
  {
    const struct dictionary *dict; /*Dictionary of destination */
    enum data_parser_type type; /* Type of data to parse. */
    int skip_records;           /* Records to skip before first real data. */
    casenumber max_cases;       /* Max number of cases to read. */
    int percent_cases;          /* Approximate percent of cases to read. */

    struct field *fields;       /* Fields to parse. */
    size_t field_cnt;           /* Number of fields. */
    size_t field_allocated;     /* Number of fields spaced allocated for. */

    /* DP_DELIMITED parsers only. */
    bool span;                  /* May cases span multiple records? */
    bool empty_line_has_field;  /* Does an empty line have an (empty) field? */
    struct substring quotes;    /* Characters that can quote separators. */
    bool quote_escape;          /* Doubled quote acts as escape? */
    struct substring soft_seps; /* Two soft separators act like just one. */
    struct substring hard_seps; /* Two hard separators yield empty fields. */
    struct string any_sep;      /* Concatenation of soft_seps and hard_seps. */

    /* DP_FIXED parsers only. */
    int records_per_case;       /* Number of records in each case. */
  };

/* How to parse one variable. */
struct field
  {
    struct fmt_spec format;	/* Input format of this field. */
    int case_idx;               /* First value in case. */
    char *name;                 /* Var name for error messages and tables. */

    /* DP_FIXED only. */
    int record;			/* Record number (1-based). */
    int first_column;           /* First column in record (1-based). */
  };

static void set_any_sep (struct data_parser *parser);

/* Creates and returns a new data parser. */
struct data_parser *
data_parser_create (const struct dictionary *dict)
{
  struct data_parser *parser = xmalloc (sizeof *parser);

  parser->type = DP_FIXED;
  parser->skip_records = 0;
  parser->max_cases = -1;
  parser->percent_cases = 100;

  parser->fields = NULL;
  parser->field_cnt = 0;
  parser->field_allocated = 0;
  parser->dict = dict;

  parser->span = true;
  parser->empty_line_has_field = false;
  ss_alloc_substring (&parser->quotes, ss_cstr ("\"'"));
  parser->quote_escape = false;
  ss_alloc_substring (&parser->soft_seps, ss_cstr (CC_SPACES));
  ss_alloc_substring (&parser->hard_seps, ss_cstr (","));
  ds_init_empty (&parser->any_sep);
  set_any_sep (parser);

  parser->records_per_case = 0;

  return parser;
}

/* Destroys PARSER. */
void
data_parser_destroy (struct data_parser *parser)
{
  if (parser != NULL)
    {
      size_t i;

      for (i = 0; i < parser->field_cnt; i++)
        free (parser->fields[i].name);
      free (parser->fields);
      ss_dealloc (&parser->quotes);
      ss_dealloc (&parser->soft_seps);
      ss_dealloc (&parser->hard_seps);
      ds_destroy (&parser->any_sep);
      free (parser);
    }
}

/* Returns the type of PARSER (either DP_DELIMITED or DP_FIXED). */
enum data_parser_type
data_parser_get_type (const struct data_parser *parser)
{
  return parser->type;
}

/* Sets the type of PARSER to TYPE (either DP_DELIMITED or
   DP_FIXED). */
void
data_parser_set_type (struct data_parser *parser, enum data_parser_type type)
{
  assert (parser->field_cnt == 0);
  assert (type == DP_FIXED || type == DP_DELIMITED);
  parser->type = type;
}

/* Configures PARSER to skip the specified number of
   INITIAL_RECORDS_TO_SKIP before parsing any data.  By default,
   no records are skipped. */
void
data_parser_set_skip (struct data_parser *parser, int initial_records_to_skip)
{
  assert (initial_records_to_skip >= 0);
  parser->skip_records = initial_records_to_skip;
}

/* Sets the maximum number of cases parsed by PARSER to
   MAX_CASES.  The default is -1, meaning no limit. */
void
data_parser_set_case_limit (struct data_parser *parser, casenumber max_cases)
{
  parser->max_cases = max_cases;
}

/* Sets the percentage of cases that PARSER should read from the
   input file to PERCENT_CASES.  By default, all cases are
   read. */
void
data_parser_set_case_percent (struct data_parser *parser, int percent_cases)
{
  assert (percent_cases >= 0 && percent_cases <= 100);
  parser->percent_cases = percent_cases;
}

/* Returns true if PARSER is configured to allow cases to span
   multiple records. */
bool
data_parser_get_span (const struct data_parser *parser)
{
  return parser->span;
}

/* If MAY_CASES_SPAN_RECORDS is true, configures PARSER to allow
   a single case to span multiple records and multiple cases to
   occupy a single record.  If MAY_CASES_SPAN_RECORDS is false,
   configures PARSER to require each record to contain exactly
   one case.

   This setting affects parsing of DP_DELIMITED files only. */
void
data_parser_set_span (struct data_parser *parser, bool may_cases_span_records)
{
  parser->span = may_cases_span_records;
}

/* If EMPTY_LINE_HAS_FIELD is true, configures PARSER to parse an
   empty line as an empty field and to treat a hard delimiter
   followed by end-of-line as an empty field.  If
   EMPTY_LINE_HAS_FIELD is false, PARSER will skip empty lines
   and hard delimiters at the end of lines without emitting empty
   fields.

   This setting affects parsing of DP_DELIMITED files only. */
void
data_parser_set_empty_line_has_field (struct data_parser *parser,
                                      bool empty_line_has_field)
{
  parser->empty_line_has_field = empty_line_has_field;
}

/* Sets the characters that may be used for quoting field
   contents to QUOTES.  If QUOTES is empty, quoting will be
   disabled.

   The caller retains ownership of QUOTES.

   This setting affects parsing of DP_DELIMITED files only. */
void
data_parser_set_quotes (struct data_parser *parser, struct substring quotes)
{
  ss_dealloc (&parser->quotes);
  ss_alloc_substring (&parser->quotes, quotes);
}

/* If ESCAPE is false (the default setting), a character used for
   quoting cannot itself be embedded within a quoted field.  If
   ESCAPE is true, then a quote character can be embedded within
   a quoted field by doubling it.

   This setting affects parsing of DP_DELIMITED files only, and
   only when at least one quote character has been set (with
   data_parser_set_quotes). */
void
data_parser_set_quote_escape (struct data_parser *parser, bool escape)
{
  parser->quote_escape = escape;
}

/* Sets PARSER's soft delimiters to DELIMITERS.  Soft delimiters
   separate fields, but consecutive soft delimiters do not yield
   empty fields.  (Ordinarily, only white space characters are
   appropriate soft delimiters.)

   The caller retains ownership of DELIMITERS.

   This setting affects parsing of DP_DELIMITED files only. */
void
data_parser_set_soft_delimiters (struct data_parser *parser,
                                 struct substring delimiters)
{
  ss_dealloc (&parser->soft_seps);
  ss_alloc_substring (&parser->soft_seps, delimiters);
  set_any_sep (parser);
}

/* Sets PARSER's hard delimiters to DELIMITERS.  Hard delimiters
   separate fields.  A consecutive pair of hard delimiters yield
   an empty field.

   The caller retains ownership of DELIMITERS.

   This setting affects parsing of DP_DELIMITED files only. */
void
data_parser_set_hard_delimiters (struct data_parser *parser,
                                 struct substring delimiters)
{
  ss_dealloc (&parser->hard_seps);
  ss_alloc_substring (&parser->hard_seps, delimiters);
  set_any_sep (parser);
}

/* Returns the number of records per case. */
int
data_parser_get_records (const struct data_parser *parser)
{
  return parser->records_per_case;
}

/* Sets the number of records per case to RECORDS_PER_CASE.

   This setting affects parsing of DP_FIXED files only. */
void
data_parser_set_records (struct data_parser *parser, int records_per_case)
{
  assert (records_per_case >= 0);
  assert (records_per_case >= parser->records_per_case);
  parser->records_per_case = records_per_case;
}

static void
add_field (struct data_parser *p, const struct fmt_spec *format, int case_idx,
           const char *name, int record, int first_column)
{
  struct field *field;

  if (p->field_cnt == p->field_allocated)
    p->fields = x2nrealloc (p->fields, &p->field_allocated, sizeof *p->fields);
  field = &p->fields[p->field_cnt++];
  field->format = *format;
  field->case_idx = case_idx;
  field->name = xstrdup (name);
  field->record = record;
  field->first_column = first_column;
}

/* Adds a delimited field to the field parsed by PARSER, which
   must be configured as a DP_DELIMITED parser.  The field is
   parsed as input format FORMAT.  Its data will be stored into case
   index CASE_INDEX.  Errors in input data will be reported
   against variable NAME. */
void
data_parser_add_delimited_field (struct data_parser *parser,
                                 const struct fmt_spec *format, int case_idx,
                                 const char *name)
{
  assert (parser->type == DP_DELIMITED);
  add_field (parser, format, case_idx, name, 0, 0);
}

/* Adds a fixed field to the field parsed by PARSER, which
   must be configured as a DP_FIXED parser.  The field is
   parsed as input format FORMAT.  Its data will be stored into case
   index CASE_INDEX.  Errors in input data will be reported
   against variable NAME.  The field will be drawn from the
   FORMAT->w columns in 1-based RECORD starting at 1-based
   column FIRST_COLUMN.

   RECORD must be at least as great as that of any field already
   added; that is, fields must be added in increasing order of
   record number.  If RECORD is greater than the current number
   of records per case, the number of records per case are
   increased as needed.  */
void
data_parser_add_fixed_field (struct data_parser *parser,
                             const struct fmt_spec *format, int case_idx,
                             const char *name,
                             int record, int first_column)
{
  assert (parser->type == DP_FIXED);
  assert (parser->field_cnt == 0
          || record >= parser->fields[parser->field_cnt - 1].record);
  if (record > parser->records_per_case)
    parser->records_per_case = record;
  add_field (parser, format, case_idx, name, record, first_column);
}

/* Returns true if any fields have been added to PARSER, false
   otherwise. */
bool
data_parser_any_fields (const struct data_parser *parser)
{
  return parser->field_cnt > 0;
}

static void
set_any_sep (struct data_parser *parser)
{
  ds_assign_substring (&parser->any_sep, parser->soft_seps);
  ds_put_substring (&parser->any_sep, parser->hard_seps);
}

static bool parse_delimited_span (const struct data_parser *,
                                  struct dfm_reader *, struct ccase *);
static bool parse_delimited_no_span (const struct data_parser *,
                                     struct dfm_reader *, struct ccase *);
static bool parse_fixed (const struct data_parser *,
                         struct dfm_reader *, struct ccase *);

/* Reads a case from DFM into C, parsing it with PARSER.  Returns
   true if successful, false at end of file or on I/O error.

   Case C must not be shared. */
bool
data_parser_parse (struct data_parser *parser, struct dfm_reader *reader,
                   struct ccase *c)
{
  bool retval;

  assert (!case_is_shared (c));
  assert (data_parser_any_fields (parser));

  /* Skip the requested number of records before reading the
     first case. */
  for (; parser->skip_records > 0; parser->skip_records--)
    {
      if (dfm_eof (reader))
        return false;
      dfm_forward_record (reader);
    }

  /* Limit cases. */
  if (parser->max_cases != -1 && parser->max_cases-- == 0)
    return false;
  if (parser->percent_cases < 100
      && dfm_get_percent_read (reader) >= parser->percent_cases)
    return false;

  if (parser->type == DP_DELIMITED)
    {
      if (parser->span)
        retval = parse_delimited_span (parser, reader, c);
      else
        retval = parse_delimited_no_span (parser, reader, c);
    }
  else
    retval = parse_fixed (parser, reader, c);

  return retval;
}

/* Extracts a delimited field from the current position in the
   current record according to PARSER, reading data from READER.

   *FIELD is set to the field content.  The caller must not or
   destroy this constant string.

   After parsing the field, sets the current position in the
   record to just past the field and any trailing delimiter.
   Returns 0 on failure or a 1-based column number indicating the
   beginning of the field on success. */
static bool
cut_field (const struct data_parser *parser, struct dfm_reader *reader,
           int *first_column, int *last_column, struct string *tmp,
           struct substring *field)
{
  size_t length_before_separators;
  struct substring line, p;
  bool quoted;

  if (dfm_eof (reader))
    return false;
  if (ss_is_empty (parser->hard_seps))
    dfm_expand_tabs (reader);
  line = p = dfm_get_record (reader);

  /* Skip leading soft separators. */
  ss_ltrim (&p, parser->soft_seps);

  /* Handle empty or completely consumed lines. */
  if (ss_is_empty (p))
    {
      if (!parser->empty_line_has_field || dfm_columns_past_end (reader) > 0)
        return false;
      else
        {
          *field = p;
          *first_column = dfm_column_start (reader);
          *last_column = *first_column + 1;
          dfm_forward_columns (reader, 1);
          return true;
        }
    }

  *first_column = dfm_column_start (reader);
  quoted = ss_find_byte (parser->quotes, ss_first (p)) != SIZE_MAX;
  if (quoted)
    {
      /* Quoted field. */
      int quote = ss_get_byte (&p);
      if (!ss_get_until (&p, quote, field))
        msg (DW, _("Quoted string extends beyond end of line."));
      if (parser->quote_escape && ss_first (p) == quote)
        {
          ds_assign_substring (tmp, *field);
          while (ss_match_byte (&p, quote))
            {
              struct substring ss;
              ds_put_byte (tmp, quote);
              if (!ss_get_until (&p, quote, &ss))
                msg (DW, _("Quoted string extends beyond end of line."));
              ds_put_substring (tmp, ss);
            }
          *field = ds_ss (tmp);
        }
      *last_column = *first_column + (ss_length (line) - ss_length (p));
    }
  else
    {
      /* Regular field. */
      ss_get_bytes (&p, ss_cspan (p, ds_ss (&parser->any_sep)), field);
      *last_column = *first_column + ss_length (*field);
    }

  /* Skip trailing soft separator and a single hard separator if present. */
  length_before_separators = ss_length (p);
  ss_ltrim (&p, parser->soft_seps);
  if (!ss_is_empty (p)
      && ss_find_byte (parser->hard_seps, ss_first (p)) != SIZE_MAX)
    {
      ss_advance (&p, 1);
      ss_ltrim (&p, parser->soft_seps);
    }
  if (ss_is_empty (p))
    dfm_forward_columns (reader, 1);
  else if (quoted && length_before_separators == ss_length (p))
    msg (DW, _("Missing delimiter following quoted string."));
  dfm_forward_columns (reader, ss_length (line) - ss_length (p));

  return true;
}

static void
parse_error (const struct dfm_reader *reader, const struct field *field,
             int first_column, int last_column, char *error)
{
  struct msg m;

  m.category = MSG_C_DATA;
  m.severity = MSG_S_WARNING;
  m.file_name = CONST_CAST (char *, dfm_get_file_name (reader));
  m.first_line = dfm_get_line_number (reader);
  m.last_line = m.first_line + 1;
  m.first_column = first_column;
  m.last_column = last_column;
  m.text = xasprintf (_("Data for variable %s is not valid as format %s: %s"),
                      field->name, fmt_name (field->format.type), error);
  msg_emit (&m);

  free (error);
}

/* Reads a case from READER into C, parsing it according to
   fixed-format syntax rules in PARSER.
   Returns true if successful, false at end of file or on I/O error. */
static bool
parse_fixed (const struct data_parser *parser, struct dfm_reader *reader,
             struct ccase *c)
{
  const char *input_encoding = dfm_reader_get_encoding (reader);
  const char *output_encoding = dict_get_encoding (parser->dict);
  struct field *f;
  int row;

  if (dfm_eof (reader))
    return false;

  f = parser->fields;
  for (row = 1; row <= parser->records_per_case; row++)
    {
      struct substring line;

      if (dfm_eof (reader))
        {
          msg (DW, _("Partial case of %d of %d records discarded."),
               row - 1, parser->records_per_case);
          return false;
        }
      dfm_expand_tabs (reader);
      line = dfm_get_record (reader);

      for (; f < &parser->fields[parser->field_cnt] && f->record == row; f++)
        {
          struct substring s = ss_substr (line, f->first_column - 1,
                                          f->format.w);
          union value *value = case_data_rw_idx (c, f->case_idx);
          char *error = data_in (s, input_encoding, f->format.type,
                                 value, fmt_var_width (&f->format),
                                 output_encoding);

          if (error == NULL)
            data_in_imply_decimals (s, input_encoding, f->format.type,
                                    f->format.d, value);
          else
            parse_error (reader, f, f->first_column,
                         f->first_column + f->format.w, error);
        }

      dfm_forward_record (reader);
    }

  return true;
}

/* Reads a case from READER into C, parsing it according to
   free-format syntax rules in PARSER.
   Returns true if successful, false at end of file or on I/O error. */
static bool
parse_delimited_span (const struct data_parser *parser,
                      struct dfm_reader *reader, struct ccase *c)
{
  const char *input_encoding = dfm_reader_get_encoding (reader);
  const char *output_encoding = dict_get_encoding (parser->dict);
  struct string tmp = DS_EMPTY_INITIALIZER;
  struct field *f;

  for (f = parser->fields; f < &parser->fields[parser->field_cnt]; f++)
    {
      struct substring s;
      int first_column, last_column;
      char *error;

      /* Cut out a field and read in a new record if necessary. */
      while (!cut_field (parser, reader,
                         &first_column, &last_column, &tmp, &s))
	{
	  if (!dfm_eof (reader))
            dfm_forward_record (reader);
	  if (dfm_eof (reader))
	    {
	      if (f > parser->fields)
		msg (DW, _("Partial case discarded.  The first variable "
                           "missing was %s."), f->name);
              ds_destroy (&tmp);
	      return false;
	    }
	}

      error = data_in (s, input_encoding, f->format.type,
                       case_data_rw_idx (c, f->case_idx),
                       fmt_var_width (&f->format), output_encoding);
      if (error != NULL)
        parse_error (reader, f, first_column, last_column, error);
    }
  ds_destroy (&tmp);
  return true;
}

/* Reads a case from READER into C, parsing it according to
   delimited syntax rules with one case per record in PARSER.
   Returns true if successful, false at end of file or on I/O error. */
static bool
parse_delimited_no_span (const struct data_parser *parser,
                         struct dfm_reader *reader, struct ccase *c)
{
  const char *input_encoding = dfm_reader_get_encoding (reader);
  const char *output_encoding = dict_get_encoding (parser->dict);
  struct string tmp = DS_EMPTY_INITIALIZER;
  struct substring s;
  struct field *f, *end;

  if (dfm_eof (reader))
    return false;

  end = &parser->fields[parser->field_cnt];
  for (f = parser->fields; f < end; f++)
    {
      int first_column, last_column;
      char *error;

      if (!cut_field (parser, reader, &first_column, &last_column, &tmp, &s))
	{
	  if (f < end - 1 && settings_get_undefined ())
	    msg (DW, _("Missing value(s) for all variables from %s onward.  "
                       "These will be filled with the system-missing value "
                       "or blanks, as appropriate."),
		 f->name);
          for (; f < end; f++)
            value_set_missing (case_data_rw_idx (c, f->case_idx),
                               fmt_var_width (&f->format));
          goto exit;
	}

      error = data_in (s, input_encoding, f->format.type,
                       case_data_rw_idx (c, f->case_idx),
                       fmt_var_width (&f->format), output_encoding);
      if (error != NULL)
        parse_error (reader, f, first_column, last_column, error);
    }

  s = dfm_get_record (reader);
  ss_ltrim (&s, parser->soft_seps);
  if (!ss_is_empty (s))
    msg (DW, _("Record ends in data not part of any field."));

exit:
  dfm_forward_record (reader);
  ds_destroy (&tmp);
  return true;
}

/* Displays a table giving information on fixed-format variable
   parsing on DATA LIST. */
static void
dump_fixed_table (const struct data_parser *parser,
                  const struct file_handle *fh)
{
  struct tab_table *t;
  size_t i;

  t = tab_create (4, parser->field_cnt + 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Variable"));
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Record"));
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Columns"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Format"));
  tab_box (t, TAL_1, TAL_1, TAL_0, TAL_1, 0, 0, 3, parser->field_cnt);
  tab_hline (t, TAL_2, 0, 3, 1);

  for (i = 0; i < parser->field_cnt; i++)
    {
      struct field *f = &parser->fields[i];
      char fmt_string[FMT_STRING_LEN_MAX + 1];
      int row = i + 1;

      tab_text (t, 0, row, TAB_LEFT, f->name);
      tab_text_format (t, 1, row, 0, "%d", f->record);
      tab_text_format (t, 2, row, 0, "%3d-%3d",
                       f->first_column, f->first_column + f->format.w - 1);
      tab_text (t, 3, row, TAB_LEFT | TAB_FIX,
                fmt_to_string (&f->format, fmt_string));
    }

  tab_title (t, ngettext ("Reading %d record from %s.",
                          "Reading %d records from %s.",
                          parser->records_per_case),
             parser->records_per_case, fh_get_name (fh));
  tab_submit (t);
}

/* Displays a table giving information on free-format variable parsing
   on DATA LIST. */
static void
dump_delimited_table (const struct data_parser *parser,
                      const struct file_handle *fh)
{
  struct tab_table *t;
  size_t i;

  t = tab_create (2, parser->field_cnt + 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Variable"));
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Format"));
  tab_box (t, TAL_1, TAL_1, TAL_0, TAL_1, 0, 0, 1, parser->field_cnt);
  tab_hline (t, TAL_2, 0, 1, 1);

  for (i = 0; i < parser->field_cnt; i++)
    {
      struct field *f = &parser->fields[i];
      char str[FMT_STRING_LEN_MAX + 1];
      int row = i + 1;

      tab_text (t, 0, row, TAB_LEFT, f->name);
      tab_text (t, 1, row, TAB_LEFT | TAB_FIX,
                fmt_to_string (&f->format, str));
    }

  tab_title (t, _("Reading free-form data from %s."), fh_get_name (fh));

  tab_submit (t);
}

/* Displays a table giving information on how PARSER will read
   data from FH. */
void
data_parser_output_description (struct data_parser *parser,
                                const struct file_handle *fh)
{
  if (parser->type == DP_FIXED)
    dump_fixed_table (parser, fh);
  else
    dump_delimited_table (parser, fh);
}

/* Data parser input program. */
struct data_parser_casereader
  {
    struct data_parser *parser; /* Parser. */
    struct dfm_reader *reader;  /* Data file reader. */
    struct caseproto *proto;    /* Format of cases. */
  };

static const struct casereader_class data_parser_casereader_class;

/* Replaces DS's active dataset by an input program that reads data
   from READER according to the rules in PARSER, using DICT as
   the underlying dictionary.  Ownership of PARSER and READER is
   transferred to the input program, and ownership of DICT is
   transferred to the dataset. */
void
data_parser_make_active_file (struct data_parser *parser, struct dataset *ds,
                              struct dfm_reader *reader,
                              struct dictionary *dict)
{
  struct data_parser_casereader *r;
  struct casereader *casereader;

  r = xmalloc (sizeof *r);
  r->parser = parser;
  r->reader = reader;
  r->proto = caseproto_ref (dict_get_proto (dict));
  casereader = casereader_create_sequential (NULL, r->proto,
                                             CASENUMBER_MAX,
                                             &data_parser_casereader_class, r);
  dataset_set_dict (ds, dict);
  dataset_set_source (ds, casereader);
}

static struct ccase *
data_parser_casereader_read (struct casereader *reader UNUSED, void *r_)
{
  struct data_parser_casereader *r = r_;
  struct ccase *c = case_create (r->proto);
  if (data_parser_parse (r->parser, r->reader, c))
    return c;
  else
    {
      case_unref (c);
      return NULL;
    }
}

static void
data_parser_casereader_destroy (struct casereader *reader UNUSED, void *r_)
{
  struct data_parser_casereader *r = r_;
  if (dfm_reader_error (r->reader))
    casereader_force_error (reader);
  data_parser_destroy (r->parser);
  dfm_close_reader (r->reader);
  caseproto_unref (r->proto);
  free (r);
}

static const struct casereader_class data_parser_casereader_class =
  {
    data_parser_casereader_read,
    data_parser_casereader_destroy,
    NULL,
    NULL,
  };
