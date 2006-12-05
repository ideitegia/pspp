/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
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

#include <ctype.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include <data/case-source.h>
#include <data/case.h>
#include <data/case-source.h>
#include <data/data-in.h>
#include <data/dictionary.h>
#include <data/format.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <data/transformations.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/data-reader.h>
#include <language/data-io/file-handle.h>
#include <language/data-io/inpt-pgm.h>
#include <language/data-io/placement-parser.h>
#include <language/lexer/format-parser.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/ll.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>
#include <output/table.h>

#include "size_max.h"
#include "xsize.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Utility function. */

/* Describes how to parse one variable. */
struct dls_var_spec
  {
    struct ll ll;               /* List element. */

    /* All parsers. */
    struct fmt_spec input;	/* Input format of this field. */
    int fv;			/* First value in case. */
    char name[LONG_NAME_LEN + 1]; /* Var name for error messages and tables. */

    /* Fixed format only. */
    int record;			/* Record number (1-based). */
    int first_column;           /* Column numbers in record. */
  };

static struct dls_var_spec *
ll_to_dls_var_spec (struct ll *ll) 
{
  return ll_data (ll, struct dls_var_spec, ll);
}

/* Constants for DATA LIST type. */
enum dls_type
  {
    DLS_FIXED,
    DLS_FREE,
    DLS_LIST
  };

/* DATA LIST private data structure. */
struct data_list_pgm
  {
    struct pool *pool;          /* Used for all DATA LIST storage. */
    struct ll_list specs;       /* List of dls_var_specs. */
    struct dfm_reader *reader;  /* Data file reader. */
    enum dls_type type;		/* Type of DATA LIST construct. */
    struct variable *end;	/* Variable specified on END subcommand. */
    int record_cnt;             /* Number of records. */
    struct string delims;       /* Field delimiters. */
    int skip_records;           /* Records to skip before first case. */
  };

static const struct case_source_class data_list_source_class;

static bool parse_fixed (struct lexer *, struct dictionary *dict, 
			 struct pool *tmp_pool, struct data_list_pgm *);
static bool parse_free (struct lexer *, struct dictionary *dict, 
			struct pool *tmp_pool, struct data_list_pgm *);
static void dump_fixed_table (const struct ll_list *,
                              const struct file_handle *, int record_cnt);
static void dump_free_table (const struct data_list_pgm *,
                             const struct file_handle *);

static trns_free_func data_list_trns_free;
static trns_proc_func data_list_trns_proc;

int
cmd_data_list (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct data_list_pgm *dls;
  int table = -1;                /* Print table if nonzero, -1=undecided. */
  struct file_handle *fh = fh_inline_file ();
  struct pool *tmp_pool;
  bool ok;

  if (!in_input_program ())
    discard_variables (ds);

  dls = pool_create_container (struct data_list_pgm, pool);
  ll_init (&dls->specs);
  dls->reader = NULL;
  dls->type = -1;
  dls->end = NULL;
  dls->record_cnt = 0;
  dls->skip_records = 0;
  ds_init_empty (&dls->delims);
  ds_register_pool (&dls->delims, dls->pool);

  tmp_pool = pool_create_subpool (dls->pool);

  while (lex_token (lexer) != '/')
    {
      if (lex_match_id (lexer, "FILE"))
	{
	  lex_match (lexer, '=');
	  fh = fh_parse (lexer, FH_REF_FILE | FH_REF_INLINE);
	  if (fh == NULL)
	    goto error;
	}
      else if (lex_match_id (lexer, "RECORDS"))
	{
	  lex_match (lexer, '=');
	  lex_match (lexer, '(');
	  if (!lex_force_int (lexer))
	    goto error;
	  dls->record_cnt = lex_integer (lexer);
	  lex_get (lexer);
	  lex_match (lexer, ')');
	}
      else if (lex_match_id (lexer, "SKIP"))
	{
	  lex_match (lexer, '=');
	  if (!lex_force_int (lexer))
	    goto error;
	  dls->skip_records = lex_integer (lexer);
	  lex_get (lexer);
	}
      else if (lex_match_id (lexer, "END"))
	{
	  if (dls->end)
	    {
	      msg (SE, _("The END subcommand may only be specified once."));
	      goto error;
	    }
	  
	  lex_match (lexer, '=');
	  if (!lex_force_id (lexer))
	    goto error;
	  dls->end = dict_lookup_var (dataset_dict (ds), lex_tokid (lexer));
	  if (!dls->end) 
            dls->end = dict_create_var_assert (dataset_dict (ds), lex_tokid (lexer), 0);
	  lex_get (lexer);
	}
      else if (lex_token (lexer) == T_ID)
	{
          if (lex_match_id (lexer, "NOTABLE"))
            table = 0;
          else if (lex_match_id (lexer, "TABLE"))
            table = 1;
          else 
            {
              int type;
              if (lex_match_id (lexer, "FIXED"))
                type = DLS_FIXED;
              else if (lex_match_id (lexer, "FREE"))
                type = DLS_FREE;
              else if (lex_match_id (lexer, "LIST"))
                type = DLS_LIST;
              else 
                {
                  lex_error (lexer, NULL);
                  goto error;
                }

	      if (dls->type != -1)
		{
		  msg (SE, _("Only one of FIXED, FREE, or LIST may "
                             "be specified."));
		  goto error;
		}
	      dls->type = type;

              if ((dls->type == DLS_FREE || dls->type == DLS_LIST)
                  && lex_match (lexer, '(')) 
                {
                  while (!lex_match (lexer, ')'))
                    {
                      int delim;

                      if (lex_match_id (lexer, "TAB"))
                        delim = '\t';
                      else if (lex_token (lexer) == T_STRING && ds_length (lex_tokstr (lexer)) == 1)
			{
			  delim = ds_first (lex_tokstr (lexer));
			  lex_get (lexer);
			}
                      else 
                        {
                          lex_error (lexer, NULL);
                          goto error;
                        }

                      ds_put_char (&dls->delims, delim);

                      lex_match (lexer, ',');
                    }
                }
            }
        }
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }

  fh_set_default_handle (fh);

  if (dls->type == -1)
    dls->type = DLS_FIXED;

  if (table == -1)
    table = dls->type != DLS_FREE;

  ok = (dls->type == DLS_FIXED ? parse_fixed : parse_free) (lexer, dict, tmp_pool, dls);
  if (!ok)
    goto error;

  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto error;

  if (table)
    {
      if (dls->type == DLS_FIXED)
	dump_fixed_table (&dls->specs, fh, dls->record_cnt);
      else
	dump_free_table (dls, fh);
    }

  dls->reader = dfm_open_reader (fh, lexer);
  if (dls->reader == NULL)
    goto error;

  if (in_input_program ())
    add_transformation (ds, data_list_trns_proc, data_list_trns_free, dls);
  else 
    proc_set_source (ds, create_case_source (&data_list_source_class, dls));

  pool_destroy (tmp_pool);

  return CMD_SUCCESS;

 error:
  data_list_trns_free (dls);
  return CMD_CASCADING_FAILURE;
}

/* Fixed-format parsing. */

/* Parses all the variable specifications for DATA LIST FIXED,
   storing them into DLS.  Uses TMP_POOL for data that is not
   needed once parsing is complete.  Returns true only if
   successful. */
static bool
parse_fixed (struct lexer *lexer, struct dictionary *dict, 
	     struct pool *tmp_pool, struct data_list_pgm *dls)
{
  int last_nonempty_record;
  int record = 0;
  int column = 1;

  while (lex_token (lexer) != '.')
    {
      char **names;
      size_t name_cnt, name_idx;
      struct fmt_spec *formats, *f;
      size_t format_cnt;

      /* Parse everything. */
      if (!parse_record_placement (lexer, &record, &column)
          || !parse_DATA_LIST_vars_pool (lexer, tmp_pool, 
					 &names, &name_cnt, PV_NONE)
          || !parse_var_placements (lexer, tmp_pool, name_cnt, true,
                                    &formats, &format_cnt))
        return false;

      /* Create variables and var specs. */
      name_idx = 0;
      for (f = formats; f < &formats[format_cnt]; f++)
        if (!execute_placement_format (f, &record, &column))
          {
            char *name;
            int width;
            struct variable *v;
            struct dls_var_spec *spec;
              
            name = names[name_idx++];

            /* Create variable. */
            width = fmt_var_width (f);
            v = dict_create_var (dict, name, width);
            if (v != NULL)
              {
                /* Success. */
                struct fmt_spec output = fmt_for_output_from_input (f);
                var_set_both_formats (v, &output);
              }
            else
              {
                /* Failure.
                   This can be acceptable if we're in INPUT
                   PROGRAM, but only if the existing variable has
                   the same width as the one we would have
                   created. */ 
                if (!in_input_program ())
                  {
                    msg (SE, _("%s is a duplicate variable name."), name);
                    return false;
                  }

                v = dict_lookup_var_assert (dict, name);
                if ((width != 0) != (var_get_width (v) != 0))
                  {
                    msg (SE, _("There is already a variable %s of a "
                               "different type."),
                         name);
                    return false;
                  }
                if (width != 0 && width != var_get_width (v))
                  {
                    msg (SE, _("There is already a string variable %s of a "
                               "different width."), name);
                    return false;
                  }
              }

            /* Create specifier for parsing the variable. */
            spec = pool_alloc (dls->pool, sizeof *spec);
            spec->input = *f;
            spec->fv = v->fv;
            spec->record = record;
            spec->first_column = column;
            strcpy (spec->name, var_get_name (v));
            ll_push_tail (&dls->specs, &spec->ll);

            column += f->w;
          }
      assert (name_idx == name_cnt);
    }
  if (ll_is_empty (&dls->specs)) 
    {
      msg (SE, _("At least one variable must be specified."));
      return false;
    }

  last_nonempty_record = ll_to_dls_var_spec (ll_tail (&dls->specs))->record;
  if (dls->record_cnt && last_nonempty_record > dls->record_cnt)
    {
      msg (SE, _("Variables are specified on records that "
		 "should not exist according to RECORDS subcommand."));
      return false;
    }
  else if (!dls->record_cnt) 
    dls->record_cnt = last_nonempty_record;

  return true;
}

/* Displays a table giving information on fixed-format variable
   parsing on DATA LIST. */
static void
dump_fixed_table (const struct ll_list *specs,
                  const struct file_handle *fh, int record_cnt)
{
  size_t spec_cnt;
  struct tab_table *t;
  struct dls_var_spec *spec;
  int row;

  spec_cnt = ll_count (specs);
  t = tab_create (4, spec_cnt + 1, 0);
  tab_columns (t, TAB_COL_DOWN, 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Variable"));
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Record"));
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Columns"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Format"));
  tab_box (t, TAL_1, TAL_1, TAL_0, TAL_1, 0, 0, 3, spec_cnt);
  tab_hline (t, TAL_2, 0, 3, 1);
  tab_dim (t, tab_natural_dimensions);

  row = 1;
  ll_for_each (spec, struct dls_var_spec, ll, specs)
    {
      char fmt_string[FMT_STRING_LEN_MAX + 1];
      tab_text (t, 0, row, TAB_LEFT, spec->name);
      tab_text (t, 1, row, TAT_PRINTF, "%d", spec->record);
      tab_text (t, 2, row, TAT_PRINTF, "%3d-%3d",
                spec->first_column, spec->first_column + spec->input.w - 1);
      tab_text (t, 3, row, TAB_LEFT | TAB_FIX,
                fmt_to_string (&spec->input, fmt_string));
      row++;
    }

  tab_title (t, ngettext ("Reading %d record from %s.",
                          "Reading %d records from %s.", record_cnt),
             record_cnt, fh_get_name (fh));
  tab_submit (t);
}

/* Free-format parsing. */

/* Parses variable specifications for DATA LIST FREE and adds
   them to DLS.  Uses TMP_POOL for data that is not needed once
   parsing is complete.  Returns true only if successful. */
static bool
parse_free (struct lexer *lexer, struct dictionary *dict, struct pool *tmp_pool, 
		struct data_list_pgm *dls)
{
  lex_get (lexer);
  while (lex_token (lexer) != '.')
    {
      struct fmt_spec input, output;
      char **name;
      size_t name_cnt;
      size_t i;

      if (!parse_DATA_LIST_vars_pool (lexer, tmp_pool, 
				      &name, &name_cnt, PV_NONE))
	return 0;

      if (lex_match (lexer, '('))
	{
	  if (!parse_format_specifier (lexer, &input)
              || !fmt_check_input (&input)
              || !lex_force_match (lexer, ')')) 
            return NULL;

          /* As a special case, N format is treated as F format
             for free-field input. */
          if (input.type == FMT_N)
            input.type = FMT_F;
          
	  output = fmt_for_output_from_input (&input);
	}
      else
	{
	  lex_match (lexer, '*');
          input = fmt_for_input (FMT_F, 8, 0);
	  output = *get_format ();
	}

      for (i = 0; i < name_cnt; i++)
	{
          struct dls_var_spec *spec;
	  struct variable *v;

	  v = dict_create_var (dict, name[i], fmt_var_width (&input));
	  if (v == NULL)
	    {
	      msg (SE, _("%s is a duplicate variable name."), name[i]);
	      return 0;
	    }
          var_set_both_formats (v, &output);

          spec = pool_alloc (dls->pool, sizeof *spec);
          spec->input = input;
	  spec->fv = v->fv;
	  strcpy (spec->name, var_get_name (v));
          ll_push_tail (&dls->specs, &spec->ll);
	}
    }

  return true;
}

/* Displays a table giving information on free-format variable parsing
   on DATA LIST. */
static void
dump_free_table (const struct data_list_pgm *dls,
                 const struct file_handle *fh)
{
  struct tab_table *t;
  struct dls_var_spec *spec;
  size_t spec_cnt;
  int row;

  spec_cnt = ll_count (&dls->specs);
  
  t = tab_create (2, spec_cnt + 1, 0);
  tab_columns (t, TAB_COL_DOWN, 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Variable"));
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Format"));
  tab_box (t, TAL_1, TAL_1, TAL_0, TAL_1, 0, 0, 1, spec_cnt);
  tab_hline (t, TAL_2, 0, 1, 1);
  tab_dim (t, tab_natural_dimensions);
  row = 1;
  ll_for_each (spec, struct dls_var_spec, ll, &dls->specs)
    {
      char str[FMT_STRING_LEN_MAX + 1];
      tab_text (t, 0, row, TAB_LEFT, spec->name);
      tab_text (t, 1, row, TAB_LEFT | TAB_FIX,
                fmt_to_string (&spec->input, str));
      row++;
    }

  tab_title (t, _("Reading free-form data from %s."), fh_get_name (fh));
  
  tab_submit (t);
}

/* Input procedure. */ 

/* Extracts a field from the current position in the current
   record.  Fields can be unquoted or quoted with single- or
   double-quote characters.

   *FIELD is set to the field content.  The caller must not
   or destroy this constant string.
   
   After parsing the field, sets the current position in the
   record to just past the field and any trailing delimiter.
   Returns 0 on failure or a 1-based column number indicating the
   beginning of the field on success. */
static bool
cut_field (const struct data_list_pgm *dls, struct substring *field)
{
  struct substring line, p;

  if (dfm_eof (dls->reader))
    return false;
  if (ds_is_empty (&dls->delims))
    dfm_expand_tabs (dls->reader);
  line = p = dfm_get_record (dls->reader);

  if (ds_is_empty (&dls->delims)) 
    {
      bool missing_quote = false;
      
      /* Skip leading whitespace. */
      ss_ltrim (&p, ss_cstr (CC_SPACES));
      if (ss_is_empty (p))
        return false;
      
      /* Handle actual data, whether quoted or unquoted. */
      if (ss_match_char (&p, '\''))
        missing_quote = !ss_get_until (&p, '\'', field);
      else if (ss_match_char (&p, '"'))
        missing_quote = !ss_get_until (&p, '"', field);
      else
        ss_get_chars (&p, ss_cspan (p, ss_cstr ("," CC_SPACES)), field);
      if (missing_quote)
        msg (SW, _("Quoted string extends beyond end of line."));

      /* Skip trailing whitespace and a single comma if present. */
      ss_ltrim (&p, ss_cstr (CC_SPACES));
      ss_match_char (&p, ',');

      dfm_forward_columns (dls->reader, ss_length (line) - ss_length (p));
    }
  else 
    {
      if (!ss_is_empty (p))
        ss_get_chars (&p, ss_cspan (p, ds_ss (&dls->delims)), field);
      else if (dfm_columns_past_end (dls->reader) == 0)
        {
          /* A blank line or a line that ends in a delimiter has a
             trailing blank field. */
          *field = p;
        }
      else 
        return false;

      /* Advance past the field.
         
         Also advance past a trailing delimiter, regardless of
         whether one actually existed.  If we "skip" a delimiter
         that was not actually there, then we will return
         end-of-line on our next call, which is what we want. */
      dfm_forward_columns (dls->reader, ss_length (line) - ss_length (p) + 1);
    }
  return true;
}

static bool read_from_data_list_fixed (const struct data_list_pgm *,
                                       struct ccase *);
static bool read_from_data_list_free (const struct data_list_pgm *,
                                      struct ccase *);
static bool read_from_data_list_list (const struct data_list_pgm *,
                                      struct ccase *);

/* Reads a case from DLS into C.
   Returns true if successful, false at end of file or on I/O error. */
static bool
read_from_data_list (const struct data_list_pgm *dls, struct ccase *c) 
{
  bool retval;

  dfm_push (dls->reader);
  switch (dls->type)
    {
    case DLS_FIXED:
      retval = read_from_data_list_fixed (dls, c);
      break;
    case DLS_FREE:
      retval = read_from_data_list_free (dls, c);
      break;
    case DLS_LIST:
      retval = read_from_data_list_list (dls, c);
      break;
    default:
      NOT_REACHED ();
    }
  dfm_pop (dls->reader);

  return retval;
}

/* Reads a case from the data file into C, parsing it according
   to fixed-format syntax rules in DLS.  
   Returns true if successful, false at end of file or on I/O error. */
static bool
read_from_data_list_fixed (const struct data_list_pgm *dls, struct ccase *c)
{
  struct dls_var_spec *spec;
  int row;

  if (dfm_eof (dls->reader)) 
    return false; 

  spec = ll_to_dls_var_spec (ll_head (&dls->specs));
  for (row = 1; row <= dls->record_cnt; row++)
    {
      struct substring line;

      if (dfm_eof (dls->reader))
        {
          msg (SW, _("Partial case of %d of %d records discarded."),
               row - 1, dls->record_cnt);
          return false;
        } 
      dfm_expand_tabs (dls->reader);
      line = dfm_get_record (dls->reader);

      ll_for_each_continue (spec, struct dls_var_spec, ll, &dls->specs) 
        data_in (ss_substr (line, spec->first_column - 1, spec->input.w),
                 spec->input.type, spec->input.d, spec->first_column,
                 case_data_rw (c, spec->fv), fmt_var_width (&spec->input));

      dfm_forward_record (dls->reader);
    }

  return true;
}

/* Reads a case from the data file into C, parsing it according
   to free-format syntax rules in DLS.  
   Returns true if successful, false at end of file or on I/O error. */
static bool
read_from_data_list_free (const struct data_list_pgm *dls, struct ccase *c)
{
  struct dls_var_spec *spec;

  ll_for_each (spec, struct dls_var_spec, ll, &dls->specs)
    {
      struct substring field;
      
      /* Cut out a field and read in a new record if necessary. */
      while (!cut_field (dls, &field))
	{
	  if (!dfm_eof (dls->reader)) 
            dfm_forward_record (dls->reader);
	  if (dfm_eof (dls->reader))
	    {
	      if (&spec->ll != ll_head (&dls->specs))
		msg (SW, _("Partial case discarded.  The first variable "
                           "missing was %s."), spec->name);
	      return false;
	    }
	}
      
      data_in (field, spec->input.type, 0,
               dfm_get_column (dls->reader, ss_data (field)),
               case_data_rw (c, spec->fv), fmt_var_width (&spec->input));
    }
  return true;
}

/* Reads a case from the data file and parses it according to
   list-format syntax rules.  
   Returns true if successful, false at end of file or on I/O error. */
static bool
read_from_data_list_list (const struct data_list_pgm *dls, struct ccase *c)
{
  struct dls_var_spec *spec;

  if (dfm_eof (dls->reader))
    return false;

  ll_for_each (spec, struct dls_var_spec, ll, &dls->specs)
    {
      struct substring field;

      if (!cut_field (dls, &field))
	{
	  if (get_undefined ())
	    msg (SW, _("Missing value(s) for all variables from %s onward.  "
                       "These will be filled with the system-missing value "
                       "or blanks, as appropriate."),
		 spec->name);
          ll_for_each_continue (spec, struct dls_var_spec, ll, &dls->specs)
            {
              int width = fmt_var_width (&spec->input);
              if (width == 0)
                case_data_rw (c, spec->fv)->f = SYSMIS;
              else
                memset (case_data_rw (c, spec->fv)->s, ' ', width); 
            }
	  break;
	}
      
      data_in (field, spec->input.type, 0,
               dfm_get_column (dls->reader, ss_data (field)),
               case_data_rw (c, spec->fv), fmt_var_width (&spec->input));
    }

  dfm_forward_record (dls->reader);
  return true;
}

/* Destroys DATA LIST transformation DLS.
   Returns true if successful, false if an I/O error occurred. */
static bool
data_list_trns_free (void *dls_)
{
  struct data_list_pgm *dls = dls_;
  dfm_close_reader (dls->reader);
  pool_destroy (dls->pool);
  return true;
}

/* Handle DATA LIST transformation DLS, parsing data into C. */
static int
data_list_trns_proc (void *dls_, struct ccase *c, casenumber case_num UNUSED)
{
  struct data_list_pgm *dls = dls_;
  int retval;

  if (read_from_data_list (dls, c))
    retval = TRNS_CONTINUE;
  else if (dfm_reader_error (dls->reader) || dfm_eof (dls->reader) > 1) 
    {
      /* An I/O error, or encountering end of file for a second
         time, should be escalated into a more serious error. */
      retval = TRNS_ERROR;
    }
  else
    retval = TRNS_END_FILE;
  
  /* If there was an END subcommand handle it. */
  if (dls->end != NULL) 
    {
      double *end = &case_data_rw (c, dls->end->fv)->f;
      if (retval == TRNS_DROP_CASE)
        {
          *end = 1.0;
          retval = TRNS_END_FILE;
        }
      else
        *end = 0.0;
    }

  return retval;
}

/* Reads all the records from the data file and passes them to
   write_case().
   Returns true if successful, false if an I/O error occurred. */
static bool
data_list_source_read (struct case_source *source,
                       struct ccase *c,
                       write_case_func *write_case, write_case_data wc_data)
{
  struct data_list_pgm *dls = source->aux;

  /* Skip the requested number of records before reading the
     first case. */
  while (dls->skip_records > 0) 
    {
      if (dfm_eof (dls->reader))
        return false;
      dfm_forward_record (dls->reader);
      dls->skip_records--;
    }
  
  for (;;) 
    {
      bool ok;

      if (!read_from_data_list (dls, c)) 
        return !dfm_reader_error (dls->reader);

      dfm_push (dls->reader);
      ok = write_case (wc_data);
      dfm_pop (dls->reader);
      if (!ok)
        return false;
    }
}

/* Destroys the source's internal data. */
static void
data_list_source_destroy (struct case_source *source)
{
  data_list_trns_free (source->aux);
}

static const struct case_source_class data_list_source_class = 
  {
    "DATA LIST",
    NULL,
    data_list_source_read,
    data_list_source_destroy,
  };
