/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/data-in.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/settings.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/data-io/data-parser.h"
#include "language/data-io/data-reader.h"
#include "language/data-io/file-handle.h"
#include "language/data-io/inpt-pgm.h"
#include "language/data-io/placement-parser.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/xsize.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* DATA LIST transformation data. */
struct data_list_trns
  {
    struct data_parser *parser; /* Parser. */
    struct dfm_reader *reader;  /* Data file reader. */
    struct variable *end;	/* Variable specified on END subcommand. */
  };

static bool parse_fixed (struct lexer *, struct dictionary *,
                         struct pool *, struct data_parser *);
static bool parse_free (struct lexer *, struct dictionary *,
                        struct pool *, struct data_parser *);

static trns_free_func data_list_trns_free;
static trns_proc_func data_list_trns_proc;

int
cmd_data_list (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict;
  struct data_parser *parser;
  struct dfm_reader *reader;
  struct variable *end = NULL;
  struct file_handle *fh = NULL;
  char *encoding = NULL;

  int table;
  enum data_parser_type type;
  bool has_type;
  struct pool *tmp_pool;
  bool ok;

  dict = (in_input_program ()
          ? dataset_dict (ds)
          : dict_create (get_default_encoding ()));
  parser = data_parser_create (dict);
  reader = NULL;

  table = -1;                /* Print table if nonzero, -1=undecided. */
  has_type = false;

  while (lex_token (lexer) != T_SLASH)
    {
      if (lex_match_id (lexer, "FILE"))
	{
	  lex_match (lexer, T_EQUALS);
          fh_unref (fh);
	  fh = fh_parse (lexer, FH_REF_FILE | FH_REF_INLINE, NULL);
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
          data_parser_set_records (parser, lex_integer (lexer));
	  lex_get (lexer);
	  lex_match (lexer, T_RPAREN);
	}
      else if (lex_match_id (lexer, "SKIP"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (!lex_force_int (lexer))
	    goto error;
          data_parser_set_skip (parser, lex_integer (lexer));
	  lex_get (lexer);
	}
      else if (lex_match_id (lexer, "END"))
	{
          if (!in_input_program ())
            {
              msg (SE, _("The END subcommand may only be used within "
                         "INPUT PROGRAM."));
              goto error;
            }
	  if (end)
	    {
	      msg (SE, _("The END subcommand may only be specified once."));
	      goto error;
	    }

	  lex_match (lexer, T_EQUALS);
	  if (!lex_force_id (lexer))
	    goto error;
	  end = dict_lookup_var (dict, lex_tokcstr (lexer));
	  if (!end)
            end = dict_create_var_assert (dict, lex_tokcstr (lexer), 0);
	  lex_get (lexer);
	}
      else if (lex_match_id (lexer, "NOTABLE"))
        table = 0;
      else if (lex_match_id (lexer, "TABLE"))
        table = 1;
      else if (lex_token (lexer) == T_ID)
	{
          if (lex_match_id (lexer, "FIXED"))
            data_parser_set_type (parser, DP_FIXED);
          else if (lex_match_id (lexer, "FREE"))
            {
              data_parser_set_type (parser, DP_DELIMITED);
              data_parser_set_span (parser, true);
            }
          else if (lex_match_id (lexer, "LIST"))
            {
              data_parser_set_type (parser, DP_DELIMITED);
              data_parser_set_span (parser, false);
            }
          else
            {
              lex_error (lexer, NULL);
              goto error;
            }

          if (has_type)
            {
              msg (SE, _("Only one of FIXED, FREE, or LIST may "
                         "be specified."));
              goto error;
            }
          has_type = true;

          if (data_parser_get_type (parser) == DP_DELIMITED)
            {
              if (lex_match (lexer, T_LPAREN))
                {
                  struct string delims = DS_EMPTY_INITIALIZER;

                  while (!lex_match (lexer, T_RPAREN))
                    {
                      int delim;

                      if (lex_match_id (lexer, "TAB"))
                        delim = '\t';
                      else if (lex_is_string (lexer)
                               && ss_length (lex_tokss (lexer)) == 1)
                        {
                          delim = ss_first (lex_tokss (lexer));
                          lex_get (lexer);
                        }
                      else
                        {
                          /* XXX should support multibyte UTF-8 characters */
                          lex_error (lexer, NULL);
                          ds_destroy (&delims);
                          goto error;
                        }
                      ds_put_byte (&delims, delim);

                      lex_match (lexer, T_COMMA);
                    }

                  data_parser_set_empty_line_has_field (parser, true);
                  data_parser_set_quotes (parser, ss_empty ());
                  data_parser_set_soft_delimiters (parser, ss_empty ());
                  data_parser_set_hard_delimiters (parser, ds_ss (&delims));
                  ds_destroy (&delims);
                }
              else
                {
                  data_parser_set_empty_line_has_field (parser, false);
                  data_parser_set_quotes (parser, ss_cstr ("'\""));
                  data_parser_set_soft_delimiters (parser,
                                                   ss_cstr (CC_SPACES));
                  data_parser_set_hard_delimiters (parser, ss_cstr (","));
                }
            }
        }
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }
  type = data_parser_get_type (parser);

  if (encoding && NULL == fh)
    msg (MW, _("Encoding should not be specified for inline data. It will be "
               "ignored."));

  if (fh == NULL)
    fh = fh_inline_file ();
  fh_set_default_handle (fh);

  if (type != DP_FIXED && end != NULL)
    {
      msg (SE, _("The END subcommand may be used only with DATA LIST FIXED."));
      goto error;
    }

  tmp_pool = pool_create ();
  if (type == DP_FIXED)
    ok = parse_fixed (lexer, dict, tmp_pool, parser);
  else
    ok = parse_free (lexer, dict, tmp_pool, parser);
  pool_destroy (tmp_pool);
  if (!ok)
    goto error;

  if (!data_parser_any_fields (parser))
    {
      msg (SE, _("At least one variable must be specified."));
      goto error;
    }

  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto error;

  if (table == -1)
    table = type == DP_FIXED || !data_parser_get_span (parser);
  if (table)
    data_parser_output_description (parser, fh);

  reader = dfm_open_reader (fh, lexer, encoding);
  if (reader == NULL)
    goto error;

  if (in_input_program ())
    {
      struct data_list_trns *trns = xmalloc (sizeof *trns);
      trns->parser = parser;
      trns->reader = reader;
      trns->end = end;
      add_transformation (ds, data_list_trns_proc, data_list_trns_free, trns);
    }
  else
    data_parser_make_active_file (parser, ds, reader, dict);

  fh_unref (fh);
  free (encoding);

  return CMD_DATA_LIST;

 error:
  data_parser_destroy (parser);
  if (!in_input_program ())
    dict_destroy (dict);
  fh_unref (fh);
  free (encoding);
  return CMD_CASCADING_FAILURE;
}

/* Fixed-format parsing. */

/* Parses all the variable specifications for DATA LIST FIXED,
   storing them into DLS.  Uses TMP_POOL for temporary storage;
   the caller may destroy it.  Returns true only if
   successful. */
static bool
parse_fixed (struct lexer *lexer, struct dictionary *dict,
	     struct pool *tmp_pool, struct data_parser *parser)
{
  int max_records = data_parser_get_records (parser);
  int record = 0;
  int column = 1;

  while (lex_token (lexer) != T_ENDCMD)
    {
      char **names;
      size_t name_cnt, name_idx;
      struct fmt_spec *formats, *f;
      size_t format_cnt;

      /* Parse everything. */
      if (!parse_record_placement (lexer, &record, &column)
          || !parse_DATA_LIST_vars_pool (lexer, dict, tmp_pool,
					 &names, &name_cnt, PV_NONE)
          || !parse_var_placements (lexer, tmp_pool, name_cnt, FMT_FOR_INPUT,
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

            if (max_records && record > max_records)
              {
                msg (SE, _("Cannot place variable %s on record %d when "
                           "RECORDS=%d is specified."),
                     var_get_name (v), record,
                     data_parser_get_records (parser));
              }

            data_parser_add_fixed_field (parser, f,
                                         var_get_case_index (v),
                                         var_get_name (v), record, column);

            column += f->w;
          }
      assert (name_idx == name_cnt);
    }

  return true;
}

/* Free-format parsing. */

/* Parses variable specifications for DATA LIST FREE and adds
   them to DLS.  Uses TMP_POOL for temporary storage; the caller
   may destroy it.  Returns true only if successful. */
static bool
parse_free (struct lexer *lexer, struct dictionary *dict,
            struct pool *tmp_pool, struct data_parser *parser)
{
  lex_get (lexer);
  while (lex_token (lexer) != T_ENDCMD)
    {
      struct fmt_spec input, output;
      char **name;
      size_t name_cnt;
      size_t i;

      if (!parse_DATA_LIST_vars_pool (lexer, dict, tmp_pool,
				      &name, &name_cnt, PV_NONE))
	return false;

      if (lex_match (lexer, T_LPAREN))
	{
          char type[FMT_TYPE_LEN_MAX + 1];

	  if (!parse_abstract_format_specifier (lexer, type, &input.w,
                                                &input.d))
            return NULL;
          if (!fmt_from_name (type, &input.type))
            {
              msg (SE, _("Unknown format type `%s'."), type);
              return NULL;
            }

          /* If no width was included, use the minimum width for the type.
             This isn't quite right, because DATETIME by itself seems to become
             DATETIME20 (see bug #30690), whereas this will become
             DATETIME17.  The correct behavior is not documented. */
          if (input.w == 0)
            {
              input.w = fmt_min_input_width (input.type);
              input.d = 0;
            }

          if (!fmt_check_input (&input) || !lex_force_match (lexer, T_RPAREN))
            return NULL;

          /* As a special case, N format is treated as F format
             for free-field input. */
          if (input.type == FMT_N)
            input.type = FMT_F;

	  output = fmt_for_output_from_input (&input);
	}
      else
	{
	  lex_match (lexer, T_ASTERISK);
          input = fmt_for_input (FMT_F, 8, 0);
	  output = *settings_get_format ();
	}

      for (i = 0; i < name_cnt; i++)
	{
	  struct variable *v;

	  v = dict_create_var (dict, name[i], fmt_var_width (&input));
	  if (v == NULL)
	    {
	      msg (SE, _("%s is a duplicate variable name."), name[i]);
	      return false;
	    }
          var_set_both_formats (v, &output);

          data_parser_add_delimited_field (parser,
                                           &input, var_get_case_index (v),
                                           var_get_name (v));
	}
    }

  return true;
}

/* Input procedure. */

/* Destroys DATA LIST transformation TRNS.
   Returns true if successful, false if an I/O error occurred. */
static bool
data_list_trns_free (void *trns_)
{
  struct data_list_trns *trns = trns_;
  data_parser_destroy (trns->parser);
  dfm_close_reader (trns->reader);
  free (trns);
  return true;
}

/* Handle DATA LIST transformation TRNS, parsing data into *C. */
static int
data_list_trns_proc (void *trns_, struct ccase **c, casenumber case_num UNUSED)
{
  struct data_list_trns *trns = trns_;
  int retval;

  *c = case_unshare (*c);
  if (data_parser_parse (trns->parser, trns->reader, *c))
    retval = TRNS_CONTINUE;
  else if (dfm_reader_error (trns->reader) || dfm_eof (trns->reader) > 1)
    {
      /* An I/O error, or encountering end of file for a second
         time, should be escalated into a more serious error. */
      retval = TRNS_ERROR;
    }
  else
    retval = TRNS_END_FILE;

  /* If there was an END subcommand handle it. */
  if (trns->end != NULL)
    {
      double *end = &case_data_rw (*c, trns->end)->f;
      if (retval == TRNS_END_FILE)
        {
          *end = 1.0;
          retval = TRNS_CONTINUE;
        }
      else
        *end = 0.0;
    }

  return retval;
}

