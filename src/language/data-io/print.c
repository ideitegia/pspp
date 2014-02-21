/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include <stdlib.h>
#include <uniwidth.h>

#include "data/case.h"
#include "data/dataset.h"
#include "data/data-out.h"
#include "data/format.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/data-io/data-writer.h"
#include "language/data-io/file-handle.h"
#include "language/data-io/placement-parser.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/ll.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/u8-line.h"
#include "output/tab.h"
#include "output/text-item.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Describes what to do when an output field is encountered. */
enum field_type
  {
    PRT_LITERAL,		/* Literal string. */
    PRT_VAR			/* Variable. */
  };

/* Describes how to output one field. */
struct prt_out_spec
  {
    /* All fields. */
    struct ll ll;               /* In struct print_trns `specs' list. */
    enum field_type type;	/* What type of field this is. */
    int record;                 /* 1-based record number. */
    int first_column;		/* 0-based first column. */

    /* PRT_VAR only. */
    const struct variable *var;	/* Associated variable. */
    struct fmt_spec format;	/* Output spec. */
    bool add_space;             /* Add trailing space? */
    bool sysmis_as_spaces;      /* Output SYSMIS as spaces? */

    /* PRT_LITERAL only. */
    struct string string;       /* String to output. */
    int width;                  /* Width of 'string', in display columns. */
  };

static inline struct prt_out_spec *
ll_to_prt_out_spec (struct ll *ll)
{
  return ll_data (ll, struct prt_out_spec, ll);
}

/* PRINT, PRINT EJECT, WRITE private data structure. */
struct print_trns
  {
    struct pool *pool;          /* Stores related data. */
    bool eject;                 /* Eject page before printing? */
    bool include_prefix;        /* Prefix lines with space? */
    const char *encoding;       /* Encoding to use for output. */
    struct dfm_writer *writer;	/* Output file, NULL=listing file. */
    struct ll_list specs;       /* List of struct prt_out_specs. */
    size_t record_cnt;          /* Number of records to write. */
  };

enum which_formats
  {
    PRINT,
    WRITE
  };

static int internal_cmd_print (struct lexer *, struct dataset *ds,
			       enum which_formats, bool eject);
static trns_proc_func print_text_trns_proc, print_binary_trns_proc;
static trns_free_func print_trns_free;
static bool parse_specs (struct lexer *, struct pool *tmp_pool, struct print_trns *,
			 struct dictionary *dict, enum which_formats);
static void dump_table (struct print_trns *, const struct file_handle *);

/* Basic parsing. */

/* Parses PRINT command. */
int
cmd_print (struct lexer *lexer, struct dataset *ds)
{
  return internal_cmd_print (lexer, ds, PRINT, false);
}

/* Parses PRINT EJECT command. */
int
cmd_print_eject (struct lexer *lexer, struct dataset *ds)
{
  return internal_cmd_print (lexer, ds, PRINT, true);
}

/* Parses WRITE command. */
int
cmd_write (struct lexer *lexer, struct dataset *ds)
{
  return internal_cmd_print (lexer, ds, WRITE, false);
}

/* Parses the output commands. */
static int
internal_cmd_print (struct lexer *lexer, struct dataset *ds,
		    enum which_formats which_formats, bool eject)
{
  bool print_table = false;
  const struct prt_out_spec *spec;
  struct print_trns *trns;
  struct file_handle *fh = NULL;
  char *encoding = NULL;
  struct pool *tmp_pool;
  bool binary;

  /* Fill in prt to facilitate error-handling. */
  trns = pool_create_container (struct print_trns, pool);
  trns->eject = eject;
  trns->writer = NULL;
  trns->record_cnt = 0;
  ll_init (&trns->specs);

  tmp_pool = pool_create_subpool (trns->pool);

  /* Parse the command options. */
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
    {
      if (lex_match_id (lexer, "OUTFILE"))
	{
	  lex_match (lexer, T_EQUALS);

	  fh = fh_parse (lexer, FH_REF_FILE, NULL);
	  if (fh == NULL)
	    goto error;
	}
      else if (lex_match_id (lexer, "ENCODING"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (!lex_force_string (lexer))
	    goto error;

          free (encoding);
          encoding = ss_xstrdup (lex_tokss (lexer));

	  lex_get (lexer);
	}
      else if (lex_match_id (lexer, "RECORDS"))
	{
	  lex_match (lexer, T_EQUALS);
	  lex_match (lexer, T_LPAREN);
	  if (!lex_force_int (lexer))
	    goto error;
	  trns->record_cnt = lex_integer (lexer);
	  lex_get (lexer);
	  lex_match (lexer, T_RPAREN);
	}
      else if (lex_match_id (lexer, "TABLE"))
	print_table = true;
      else if (lex_match_id (lexer, "NOTABLE"))
	print_table = false;
      else
	{
	  lex_error (lexer, _("expecting a valid subcommand"));
	  goto error;
	}
    }

  /* When PRINT or PRINT EJECT writes to an external file, we
     prefix each line with a space for compatibility. */
  trns->include_prefix = which_formats == PRINT && fh != NULL;

  /* Parse variables and strings. */
  if (!parse_specs (lexer, tmp_pool, trns, dataset_dict (ds), which_formats))
    goto error;

  /* Are there any binary formats?

     There are real difficulties figuring out what to do when both binary
     formats and nontrivial encodings enter the picture.  So when binary
     formats are present we fall back to much simpler handling. */
  binary = false;
  ll_for_each (spec, struct prt_out_spec, ll, &trns->specs)
    {
      if (spec->type == PRT_VAR
          && fmt_get_category (spec->format.type) == FMT_CAT_BINARY)
        {
          binary = true;
          break;
        }
    }
  if (binary && fh == NULL)
    {
      msg (SE, _("%s is required when binary formats are specified."), "OUTFILE");
      goto error;
    }

  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto error;

  if (fh != NULL)
    {
      trns->writer = dfm_open_writer (fh, encoding);
      if (trns->writer == NULL)
        goto error;
      trns->encoding = dfm_writer_get_encoding (trns->writer);
    }
  else
    trns->encoding = UTF8;

  /* Output the variable table if requested. */
  if (print_table)
    dump_table (trns, fh);

  /* Put the transformation in the queue. */
  add_transformation (ds,
                      (binary
                       ? print_binary_trns_proc
                       : print_text_trns_proc),
                      print_trns_free, trns);

  pool_destroy (tmp_pool);
  fh_unref (fh);

  return CMD_SUCCESS;

 error:
  print_trns_free (trns);
  fh_unref (fh);
  return CMD_FAILURE;
}

static bool parse_string_argument (struct lexer *, struct print_trns *,
                                   int record, int *column);
static bool parse_variable_argument (struct lexer *, const struct dictionary *,
				     struct print_trns *,
                                     struct pool *tmp_pool,
                                     int *record, int *column,
                                     enum which_formats);

/* Parses all the variable and string specifications on a single
   PRINT, PRINT EJECT, or WRITE command into the prt structure.
   Returns success. */
static bool
parse_specs (struct lexer *lexer, struct pool *tmp_pool, struct print_trns *trns,
	     struct dictionary *dict,
             enum which_formats which_formats)
{
  int record = 0;
  int column = 1;

  if (lex_token (lexer) == T_ENDCMD)
    {
      trns->record_cnt = 1;
      return true;
    }

  while (lex_token (lexer) != T_ENDCMD)
    {
      bool ok;

      if (!parse_record_placement (lexer, &record, &column))
        return false;

      if (lex_is_string (lexer))
	ok = parse_string_argument (lexer, trns, record, &column);
      else
	ok = parse_variable_argument (lexer, dict, trns, tmp_pool, &record,
                                      &column, which_formats);
      if (!ok)
	return 0;

      lex_match (lexer, T_COMMA);
    }

  if (trns->record_cnt != 0 && trns->record_cnt != record)
    msg (SW, _("Output calls for %d records but %zu specified on RECORDS "
               "subcommand."),
         record, trns->record_cnt);
  trns->record_cnt = record;

  return true;
}

/* Parses a string argument to the PRINT commands.  Returns success. */
static bool
parse_string_argument (struct lexer *lexer, struct print_trns *trns, int record, int *column)
{
  struct prt_out_spec *spec = pool_alloc (trns->pool, sizeof *spec);
  spec->type = PRT_LITERAL;
  spec->record = record;
  spec->first_column = *column;
  ds_init_substring (&spec->string, lex_tokss (lexer));
  ds_register_pool (&spec->string, trns->pool);
  lex_get (lexer);

  /* Parse the included column range. */
  if (lex_is_number (lexer))
    {
      int first_column, last_column;
      bool range_specified;

      if (!parse_column_range (lexer, 1,
                               &first_column, &last_column, &range_specified))
        return false;

      spec->first_column = first_column;
      if (range_specified)
        ds_set_length (&spec->string, last_column - first_column + 1, ' ');
    }

  spec->width = u8_strwidth (CHAR_CAST (const uint8_t *,
                                        ds_cstr (&spec->string)),
                             UTF8);
  *column = spec->first_column + spec->width;

  ll_push_tail (&trns->specs, &spec->ll);
  return true;
}

/* Parses a variable argument to the PRINT commands by passing it off
   to fixed_parse_compatible() or fixed_parse_fortran() as appropriate.
   Returns success. */
static bool
parse_variable_argument (struct lexer *lexer, const struct dictionary *dict,
			 struct print_trns *trns, struct pool *tmp_pool,
                         int *record, int *column,
                         enum which_formats which_formats)
{
  const struct variable **vars;
  size_t var_cnt, var_idx;
  struct fmt_spec *formats, *f;
  size_t format_cnt;
  bool add_space;

  if (!parse_variables_const_pool (lexer, tmp_pool, dict,
			     &vars, &var_cnt, PV_DUPLICATE))
    return false;

  if (lex_is_number (lexer) || lex_token (lexer) == T_LPAREN)
    {
      if (!parse_var_placements (lexer, tmp_pool, var_cnt, FMT_FOR_OUTPUT,
                                 &formats, &format_cnt))
        return false;
      add_space = false;
    }
  else
    {
      size_t i;

      lex_match (lexer, T_ASTERISK);

      formats = pool_nmalloc (tmp_pool, var_cnt, sizeof *formats);
      format_cnt = var_cnt;
      for (i = 0; i < var_cnt; i++)
        {
          const struct variable *v = vars[i];
          formats[i] = (which_formats == PRINT
                        ? *var_get_print_format (v)
                        : *var_get_write_format (v));
        }
      add_space = which_formats == PRINT;
    }

  var_idx = 0;
  for (f = formats; f < &formats[format_cnt]; f++)
    if (!execute_placement_format (f, record, column))
      {
        const struct variable *var;
        struct prt_out_spec *spec;

        var = vars[var_idx++];
        if (!fmt_check_width_compat (f, var_get_width (var)))
          return false;

        spec = pool_alloc (trns->pool, sizeof *spec);
        spec->type = PRT_VAR;
        spec->record = *record;
        spec->first_column = *column;
        spec->var = var;
        spec->format = *f;
        spec->add_space = add_space;

        /* This is a completely bizarre twist for compatibility:
           WRITE outputs the system-missing value as a field
           filled with spaces, instead of using the normal format
           that usually contains a period. */
        spec->sysmis_as_spaces = (which_formats == WRITE
                                  && var_is_numeric (var)
                                  && (fmt_get_category (spec->format.type)
                                      != FMT_CAT_BINARY));

        ll_push_tail (&trns->specs, &spec->ll);

        *column += f->w + add_space;
      }
  assert (var_idx == var_cnt);

  return true;
}

/* Prints the table produced by the TABLE subcommand to the listing
   file. */
static void
dump_table (struct print_trns *trns, const struct file_handle *fh)
{
  struct prt_out_spec *spec;
  struct tab_table *t;
  int spec_cnt;
  int row;

  spec_cnt = ll_count (&trns->specs);
  t = tab_create (4, spec_cnt + 1);
  tab_box (t, TAL_1, TAL_1, TAL_0, TAL_1, 0, 0, 3, spec_cnt);
  tab_hline (t, TAL_2, 0, 3, 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Variable"));
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Record"));
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Columns"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Format"));
  row = 1;
  ll_for_each (spec, struct prt_out_spec, ll, &trns->specs)
    {
      char fmt_string[FMT_STRING_LEN_MAX + 1];
      int width;
      switch (spec->type)
        {
        case PRT_LITERAL:
          tab_text_format (t, 0, row, TAB_LEFT | TAB_FIX, "`%.*s'",
                           (int) ds_length (&spec->string),
                           ds_data (&spec->string));
          width = ds_length (&spec->string);
          break;
        case PRT_VAR:
          tab_text (t, 0, row, TAB_LEFT, var_get_name (spec->var));
          tab_text (t, 3, row, TAB_LEFT | TAB_FIX,
                    fmt_to_string (&spec->format, fmt_string));
          width = spec->format.w;
          break;
        default:
          NOT_REACHED ();
	}
      tab_text_format (t, 1, row, 0, "%d", spec->record);
      tab_text_format (t, 2, row, 0, "%3d-%3d",
                       spec->first_column, spec->first_column + width - 1);
      row++;
    }

  if (fh != NULL)
    tab_title (t, ngettext ("Writing %zu record to %s.",
                            "Writing %zu records to %s.", trns->record_cnt),
               trns->record_cnt, fh_get_name (fh));
  else
    tab_title (t, ngettext ("Writing %zu record.",
                            "Writing %zu records.", trns->record_cnt),
               trns->record_cnt);
  tab_submit (t);
}

/* Transformation, for all-text output. */

static void print_text_flush_records (struct print_trns *, struct u8_line *,
                                      int target_record,
                                      bool *eject, int *record);

/* Performs the transformation inside print_trns T on case C. */
static int
print_text_trns_proc (void *trns_, struct ccase **c,
                      casenumber case_num UNUSED)
{
  struct print_trns *trns = trns_;
  struct prt_out_spec *spec;
  struct u8_line line;

  bool eject = trns->eject;
  int record = 1;

  u8_line_init (&line);
  ll_for_each (spec, struct prt_out_spec, ll, &trns->specs)
    {
      int x0 = spec->first_column;

      print_text_flush_records (trns, &line, spec->record, &eject, &record);

      u8_line_set_length (&line, spec->first_column);
      if (spec->type == PRT_VAR)
        {
          const union value *input = case_data (*c, spec->var);
          int x1;

          if (!spec->sysmis_as_spaces || input->f != SYSMIS)
            {
              size_t len;
              int width;
              char *s;

              s = data_out (input, var_get_encoding (spec->var),
                            &spec->format);
              len = strlen (s);
              width = u8_width (CHAR_CAST (const uint8_t *, s), len, UTF8);
              x1 = x0 + width;
              u8_line_put (&line, x0, x1, s, len);
              free (s);
            }
          else
            {
              int n = spec->format.w;

              x1 = x0 + n;
              memset (u8_line_reserve (&line, x0, x1, n), ' ', n);
            }

          if (spec->add_space)
            *u8_line_reserve (&line, x1, x1 + 1, 1) = ' ';
        }
      else
        {
          const struct string *s = &spec->string;

          u8_line_put (&line, x0, x0 + spec->width,
                       ds_data (s), ds_length (s));
        }
    }
  print_text_flush_records (trns, &line, trns->record_cnt + 1,
                            &eject, &record);
  u8_line_destroy (&line);

  if (trns->writer != NULL && dfm_write_error (trns->writer))
    return TRNS_ERROR;
  return TRNS_CONTINUE;
}

/* Advance from *RECORD to TARGET_RECORD, outputting records
   along the way.  If *EJECT is true, then the first record
   output is preceded by ejecting the page (and *EJECT is set
   false). */
static void
print_text_flush_records (struct print_trns *trns, struct u8_line *line,
                          int target_record, bool *eject, int *record)
{
  for (; target_record > *record; (*record)++)
    {
      char leader = ' ';

      if (*eject)
        {
          *eject = false;
          if (trns->writer == NULL)
            text_item_submit (text_item_create (TEXT_ITEM_EJECT_PAGE, ""));
          else
            leader = '1';
        }
      *u8_line_reserve (line, 0, 1, 1) = leader;

      if (trns->writer == NULL)
        tab_output_text (TAB_FIX, ds_cstr (&line->s) + 1);
      else
        {
          size_t len = ds_length (&line->s);
          char *s = ds_cstr (&line->s);

          if (!trns->include_prefix)
            {
              s++;
              len--;
            }

          if (is_encoding_utf8 (trns->encoding))
            dfm_put_record (trns->writer, s, len);
          else
            {
              char *recoded = recode_string (trns->encoding, UTF8, s, len);
              dfm_put_record (trns->writer, recoded, strlen (recoded));
              free (recoded);
            }
        }
    }
}

/* Transformation, for output involving binary. */

static void print_binary_flush_records (struct print_trns *,
                                        struct string *line, int target_record,
                                        bool *eject, int *record);

/* Performs the transformation inside print_trns T on case C. */
static int
print_binary_trns_proc (void *trns_, struct ccase **c,
                        casenumber case_num UNUSED)
{
  struct print_trns *trns = trns_;
  bool eject = trns->eject;
  char encoded_space = recode_byte (trns->encoding, C_ENCODING, ' ');
  int record = 1;
  struct prt_out_spec *spec;
  struct string line;

  ds_init_empty (&line);
  ds_put_byte (&line, ' ');
  ll_for_each (spec, struct prt_out_spec, ll, &trns->specs)
    {
      print_binary_flush_records (trns, &line, spec->record, &eject, &record);

      ds_set_length (&line, spec->first_column, encoded_space);
      if (spec->type == PRT_VAR)
        {
          const union value *input = case_data (*c, spec->var);
          if (!spec->sysmis_as_spaces || input->f != SYSMIS)
            data_out_recode (input, var_get_encoding (spec->var),
                             &spec->format, &line, trns->encoding);
          else
            ds_put_byte_multiple (&line, encoded_space, spec->format.w);
          if (spec->add_space)
            ds_put_byte (&line, encoded_space);
        }
      else
        {
          ds_put_substring (&line, ds_ss (&spec->string));
          if (0 != strcmp (trns->encoding, UTF8))
            {
              size_t length = ds_length (&spec->string);
              char *data = ss_data (ds_tail (&line, length));
	      char *s = recode_string (trns->encoding, UTF8, data, length);
	      memcpy (data, s, length);
	      free (s);
            }
        }
    }
  print_binary_flush_records (trns, &line, trns->record_cnt + 1,
                              &eject, &record);
  ds_destroy (&line);

  if (trns->writer != NULL && dfm_write_error (trns->writer))
    return TRNS_ERROR;
  return TRNS_CONTINUE;
}

/* Advance from *RECORD to TARGET_RECORD, outputting records
   along the way.  If *EJECT is true, then the first record
   output is preceded by ejecting the page (and *EJECT is set
   false). */
static void
print_binary_flush_records (struct print_trns *trns, struct string *line,
                            int target_record, bool *eject, int *record)
{
  for (; target_record > *record; (*record)++)
    {
      char *s = ds_cstr (line);
      size_t length = ds_length (line);
      char leader = ' ';

      if (*eject)
        {
          *eject = false;
          leader = '1';
        }
      s[0] = recode_byte (trns->encoding, C_ENCODING, leader);

      if (!trns->include_prefix)
        {
          s++;
          length--;
        }
      dfm_put_record (trns->writer, s, length);

      ds_truncate (line, 1);
    }
}

/* Frees TRNS. */
static bool
print_trns_free (void *trns_)
{
  struct print_trns *trns = trns_;
  bool ok = true;

  if (trns->writer != NULL)
    ok = dfm_close_writer (trns->writer);
  pool_destroy (trns->pool);

  return ok;
}

