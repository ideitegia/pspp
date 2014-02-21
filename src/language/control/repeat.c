/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009-2012 Free Software Foundation, Inc.

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

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/settings.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/segment.h"
#include "language/lexer/token.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/misc.h"

#include "gl/ftoastr.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct dummy_var
  {
    struct hmap_node hmap_node;
    char *name;
    char **values;
    size_t n_values;
  };

static bool parse_specification (struct lexer *, struct dictionary *,
                                 struct hmap *dummies);
static bool parse_commands (struct lexer *, struct hmap *dummies);
static void destroy_dummies (struct hmap *dummies);

static bool parse_ids (struct lexer *, const struct dictionary *,
                       struct dummy_var *);
static bool parse_numbers (struct lexer *, struct dummy_var *);
static bool parse_strings (struct lexer *, struct dummy_var *);

int
cmd_do_repeat (struct lexer *lexer, struct dataset *ds)
{
  struct hmap dummies;
  bool ok;

  if (!parse_specification (lexer, dataset_dict (ds), &dummies))
    return CMD_CASCADING_FAILURE;

  ok = parse_commands (lexer, &dummies);

  destroy_dummies (&dummies);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

static unsigned int
hash_dummy (const char *name, size_t name_len)
{
  return utf8_hash_case_bytes (name, name_len, 0);
}

static const struct dummy_var *
find_dummy_var (struct hmap *hmap, const char *name, size_t name_len)
{
  const struct dummy_var *dv;

  HMAP_FOR_EACH_WITH_HASH (dv, struct dummy_var, hmap_node,
                           hash_dummy (name, name_len), hmap)
    if (utf8_strcasecmp (dv->name, name))
      return dv;

  return NULL;
}

/* Parses the whole DO REPEAT command specification.
   Returns success. */
static bool
parse_specification (struct lexer *lexer, struct dictionary *dict,
                     struct hmap *dummies)
{
  struct dummy_var *first_dv = NULL;

  hmap_init (dummies);
  do
    {
      struct dummy_var *dv;
      const char *name;
      bool ok;

      /* Get a stand-in variable name and make sure it's unique. */
      if (!lex_force_id (lexer))
	goto error;
      name = lex_tokcstr (lexer);
      if (dict_lookup_var (dict, name))
        msg (SW, _("Dummy variable name `%s' hides dictionary variable `%s'."),
             name, name);
      if (find_dummy_var (dummies, name, strlen (name)))
        {
          msg (SE, _("Dummy variable name `%s' is given twice."), name);
          goto error;
        }

      /* Make a new macro. */
      dv = xmalloc (sizeof *dv);
      dv->name = xstrdup (name);
      dv->values = NULL;
      dv->n_values = 0;
      hmap_insert (dummies, &dv->hmap_node, hash_dummy (name, strlen (name)));

      /* Skip equals sign. */
      lex_get (lexer);
      if (!lex_force_match (lexer, T_EQUALS))
	goto error;

      /* Get the details of the variable's possible values. */
      if (lex_token (lexer) == T_ID || lex_token (lexer) == T_ALL)
	ok = parse_ids (lexer, dict, dv);
      else if (lex_is_number (lexer))
	ok = parse_numbers (lexer, dv);
      else if (lex_is_string (lexer))
	ok = parse_strings (lexer, dv);
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
      if (!ok)
	goto error;
      assert (dv->n_values > 0);
      if (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
        {
          lex_error (lexer, NULL);
          goto error;
        }

      /* If this is the first variable then it defines how many replacements
	 there must be; otherwise enforce this number of replacements. */
      if (first_dv == NULL)
        first_dv = dv;
      else if (first_dv->n_values != dv->n_values)
	{
	  msg (SE, _("Dummy variable `%s' had %zu substitutions, so `%s' must "
                     "also, but %zu were specified."),
               first_dv->name, first_dv->n_values,
               dv->name, dv->n_values);
	  goto error;
	}

      lex_match (lexer, T_SLASH);
    }
  while (!lex_match (lexer, T_ENDCMD));

  while (lex_match (lexer, T_ENDCMD))
    continue;

  return true;

error:
  destroy_dummies (dummies);
  return false;
}

static size_t
count_values (struct hmap *dummies)
{
  const struct dummy_var *dv;
  dv = HMAP_FIRST (struct dummy_var, hmap_node, dummies);
  return dv->n_values;
}

static void
do_parse_commands (struct substring s, enum segmenter_mode mode,
                   struct hmap *dummies,
                   struct string *outputs, size_t n_outputs)
{
  struct segmenter segmenter;

  segmenter_init (&segmenter, mode);

  while (!ss_is_empty (s))
    {
      enum segment_type type;
      int n;

      n = segmenter_push (&segmenter, s.string, s.length, &type);
      assert (n >= 0);

      if (type == SEG_DO_REPEAT_COMMAND)
        {
          for (;;)
            {
              int k;

              k = segmenter_push (&segmenter, s.string + n, s.length - n,
                                  &type);
              if (type != SEG_NEWLINE && type != SEG_DO_REPEAT_COMMAND)
                break;

              n += k;
            }

          do_parse_commands (ss_head (s, n), mode, dummies,
                             outputs, n_outputs);
        }
      else if (type != SEG_END)
        {
          const struct dummy_var *dv;
          size_t i;

          dv = (type == SEG_IDENTIFIER
                ? find_dummy_var (dummies, s.string, n)
                : NULL);
          for (i = 0; i < n_outputs; i++)
            if (dv != NULL)
              ds_put_cstr (&outputs[i], dv->values[i]);
            else
              ds_put_substring (&outputs[i], ss_head (s, n));
        }

      ss_advance (&s, n);
    }
}

static bool
parse_commands (struct lexer *lexer, struct hmap *dummies)
{
  enum lex_syntax_mode syntax_mode;
  enum segmenter_mode mode;
  struct string *outputs;
  struct string input;
  size_t n_values;
  char *file_name;
  int line_number;
  bool ok;
  size_t i;

  if (lex_get_file_name (lexer) != NULL)
    file_name = xstrdup (lex_get_file_name (lexer));
  else
    file_name = NULL;
  line_number = lex_get_first_line_number (lexer, 0);

  ds_init_empty (&input);
  while (lex_is_string (lexer))
    {
      ds_put_substring (&input, lex_tokss (lexer));
      ds_put_byte (&input, '\n');
      lex_get (lexer);
    }
  if (ds_is_empty (&input))
    ds_put_byte (&input, '\n');
  ds_put_byte (&input, '\0');

  n_values = count_values (dummies);
  outputs = xmalloc (n_values * sizeof *outputs);
  for (i = 0; i < n_values; i++)
    ds_init_empty (&outputs[i]);

  syntax_mode = lex_get_syntax_mode (lexer);
  if (syntax_mode == LEX_SYNTAX_AUTO)
    mode = SEG_MODE_AUTO;
  else if (syntax_mode == LEX_SYNTAX_INTERACTIVE)
    mode = SEG_MODE_INTERACTIVE;
  else if (syntax_mode == LEX_SYNTAX_BATCH)
    mode = SEG_MODE_BATCH;
  else
    NOT_REACHED ();
  do_parse_commands (ds_ss (&input), mode, dummies, outputs, n_values);

  ds_destroy (&input);

  while (lex_match (lexer, T_ENDCMD))
    continue;

  ok = (lex_force_match_id (lexer, "END")
        && lex_force_match_id (lexer, "REPEAT"));
  if (ok)
    lex_match_id (lexer, "PRINT"); /* XXX */

  lex_discard_rest_of_command (lexer);

  for (i = 0; i < n_values; i++)
    {
      struct string *output = &outputs[n_values - i - 1];
      struct lex_reader *reader;

      reader = lex_reader_for_substring_nocopy (ds_ss (output));
      lex_reader_set_file_name (reader, file_name);
      reader->line_number = line_number;
      lex_include (lexer, reader);
    }
  free (file_name);
  free (outputs);

  return ok;
}

static void
destroy_dummies (struct hmap *dummies)
{
  struct dummy_var *dv, *next;

  HMAP_FOR_EACH_SAFE (dv, next, struct dummy_var, hmap_node, dummies)
    {
      size_t i;

      hmap_delete (dummies, &dv->hmap_node);

      free (dv->name);
      for (i = 0; i < dv->n_values; i++)
        free (dv->values[i]);
      free (dv->values);
      free (dv);
    }
  hmap_destroy (dummies);
}

/* Parses a set of ids for DO REPEAT. */
static bool
parse_ids (struct lexer *lexer, const struct dictionary *dict,
	   struct dummy_var *dv)
{
  return parse_mixed_vars (lexer, dict, &dv->values, &dv->n_values, PV_NONE);
}

/* Adds REPLACEMENT to MACRO's list of replacements, which has
   *USED elements and has room for *ALLOCATED.  Allocates memory
   from POOL. */
static void
add_replacement (struct dummy_var *dv, char *value, size_t *allocated)
{
  if (dv->n_values == *allocated)
    dv->values = x2nrealloc (dv->values, allocated, sizeof *dv->values);
  dv->values[dv->n_values++] = value;
}

/* Parses a list or range of numbers for DO REPEAT. */
static bool
parse_numbers (struct lexer *lexer, struct dummy_var *dv)
{
  size_t allocated = 0;

  do
    {
      if (!lex_force_num (lexer))
	return false;

      if (lex_next_token (lexer, 1) == T_TO)
        {
          long int a, b;
          long int i;

          if (!lex_is_integer (lexer))
	    {
	      msg (SE, _("Ranges may only have integer bounds."));
	      return false;
	    }

          a = lex_integer (lexer);
          lex_get (lexer);
          lex_get (lexer);

          if (!lex_force_int (lexer))
            return false;

	  b = lex_integer (lexer);
          if (b < a)
            {
              msg (SE, _("%ld TO %ld is an invalid range."), a, b);
              return false;
            }
	  lex_get (lexer);

          for (i = a; i <= b; i++)
            add_replacement (dv, xasprintf ("%ld", i), &allocated);
        }
      else
        {
          char s[DBL_BUFSIZE_BOUND];

          c_dtoastr (s, sizeof s, 0, 0, lex_number (lexer));
          add_replacement (dv, xstrdup (s), &allocated);
          lex_get (lexer);
        }

      lex_match (lexer, T_COMMA);
    }
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD);

  return true;
}

/* Parses a list of strings for DO REPEAT. */
static bool
parse_strings (struct lexer *lexer, struct dummy_var *dv)
{
  size_t allocated = 0;

  do
    {
      if (!lex_force_string (lexer))
	{
	  return false;
	}

      add_replacement (dv, token_to_string (lex_next (lexer, 0)), &allocated);

      lex_get (lexer);
      lex_match (lexer, T_COMMA);
    }
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD);

  return true;
}

int
cmd_end_repeat (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  msg (SE, _("No matching %s."), "DO REPEAT");
  return CMD_CASCADING_FAILURE;
}
