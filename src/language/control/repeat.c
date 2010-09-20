/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010 Free Software Foundation, Inc.

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

#include "repeat.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>

#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <libpspp/getl.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/cast.h>
#include <libpspp/ll.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>
#include <data/variable.h>

#include "intprops.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A line repeated by DO REPEAT. */
struct repeat_line
  {
    struct ll ll;               /* In struct repeat_block line_list. */
    const char *file_name;      /* File name. */
    int line_number;            /* Line number. */
    struct substring text;	/* Contents. */
  };

/* The type of substitution made for a DO REPEAT macro. */
enum repeat_macro_type
  {
    VAR_NAMES,
    OTHER
  };

/* Describes one DO REPEAT macro. */
struct repeat_macro
  {
    struct ll ll;                       /* In struct repeat_block macros. */
    enum repeat_macro_type type;        /* Types of replacements. */
    struct substring name;              /* Macro name. */
    struct substring *replacements;     /* Macro replacement. */
  };

/* A DO REPEAT...END REPEAT block. */
struct repeat_block
  {
    struct getl_interface parent;

    struct pool *pool;                  /* Pool used for storage. */
    struct dataset *ds;                 /* The dataset for this block */

    struct ll_list lines;               /* Lines in buffer. */
    struct ll *cur_line;                /* Last line output. */
    int loop_cnt;                       /* Number of loops. */
    int loop_idx;                       /* Number of loops so far. */

    struct ll_list macros;              /* Table of macros. */

    bool print;                         /* Print lines as executed? */
  };

static bool parse_specification (struct lexer *, struct repeat_block *);
static bool parse_lines (struct lexer *, struct repeat_block *);
static void create_vars (struct repeat_block *);

static struct repeat_macro *find_macro (struct repeat_block *,
                                        struct substring name);

static int parse_ids (struct lexer *, const struct dictionary *dict,
		      struct repeat_macro *, struct pool *);

static int parse_numbers (struct lexer *, struct repeat_macro *,
			  struct pool *);

static int parse_strings (struct lexer *, struct repeat_macro *,
			  struct pool *);

static void do_repeat_filter (struct getl_interface *,
                              struct string *);
static bool do_repeat_read (struct getl_interface *,
                            struct string *);
static void do_repeat_close (struct getl_interface *);
static bool always_false (const struct getl_interface *);
static const char *do_repeat_name (const struct getl_interface *);
static int do_repeat_location (const struct getl_interface *);

int
cmd_do_repeat (struct lexer *lexer, struct dataset *ds)
{
  struct repeat_block *block;

  block = pool_create_container (struct repeat_block, pool);
  block->ds = ds;
  ll_init (&block->lines);
  block->cur_line = ll_null (&block->lines);
  block->loop_idx = 0;
  ll_init (&block->macros);

  if (!parse_specification (lexer, block) || !parse_lines (lexer, block))
    goto error;

  create_vars (block);

  block->parent.read = do_repeat_read;
  block->parent.close = do_repeat_close;
  block->parent.filter = do_repeat_filter;
  block->parent.interactive = always_false;
  block->parent.name = do_repeat_name;
  block->parent.location = do_repeat_location;

  if (!ll_is_empty (&block->lines))
    getl_include_source (lex_get_source_stream (lexer),
			 &block->parent,
			 lex_current_syntax_mode (lexer),
			 lex_current_error_mode (lexer)
			 );
  else
    pool_destroy (block->pool);

  return CMD_SUCCESS;

 error:
  pool_destroy (block->pool);
  return CMD_CASCADING_FAILURE;
}

/* Parses the whole DO REPEAT command specification.
   Returns success. */
static bool
parse_specification (struct lexer *lexer, struct repeat_block *block)
{
  struct substring first_name;

  block->loop_cnt = 0;
  do
    {
      struct repeat_macro *macro;
      struct dictionary *dict = dataset_dict (block->ds);
      int count;

      /* Get a stand-in variable name and make sure it's unique. */
      if (!lex_force_id (lexer))
	return false;
      if (dict_lookup_var (dict, lex_tokid (lexer)))
        msg (SW, _("Dummy variable name `%s' hides dictionary "
                   "variable `%s'."),
             lex_tokid (lexer), lex_tokid (lexer));
      if (find_macro (block, ss_cstr (lex_tokid (lexer))))
	  {
	    msg (SE, _("Dummy variable name `%s' is given twice."),
		 lex_tokid (lexer));
	    return false;
	  }

      /* Make a new macro. */
      macro = pool_alloc (block->pool, sizeof *macro);
      ss_alloc_substring_pool (&macro->name, ss_cstr (lex_tokid (lexer)),
                               block->pool);
      ll_push_tail (&block->macros, &macro->ll);

      /* Skip equals sign. */
      lex_get (lexer);
      if (!lex_force_match (lexer, T_EQUALS))
	return false;

      /* Get the details of the variable's possible values. */
      if (lex_token (lexer) == T_ID)
	count = parse_ids (lexer, dict, macro, block->pool);
      else if (lex_is_number (lexer))
	count = parse_numbers (lexer, macro, block->pool);
      else if (lex_is_string (lexer))
	count = parse_strings (lexer, macro, block->pool);
      else
	{
	  lex_error (lexer, NULL);
	  return false;
	}
      if (count == 0)
	return false;
      if (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
        {
          lex_error (lexer, NULL);
          return false;
        }

      /* If this is the first variable then it defines how many
	 replacements there must be; otherwise enforce this number of
	 replacements. */
      if (block->loop_cnt == 0)
	{
	  block->loop_cnt = count;
	  first_name = macro->name;
	}
      else if (block->loop_cnt != count)
	{
	  msg (SE, _("Dummy variable `%.*s' had %d "
                     "substitutions, so `%.*s' must also, but %d "
                     "were specified."),
	       (int) ss_length (first_name), ss_data (first_name),
               block->loop_cnt,
               (int) ss_length (macro->name), ss_data (macro->name),
               count);
	  return false;
	}

      lex_match (lexer, T_SLASH);
    }
  while (lex_token (lexer) != T_ENDCMD);

  return true;
}

/* Finds and returns a DO REPEAT macro with the given NAME, or
   NULL if there is none */
static struct repeat_macro *
find_macro (struct repeat_block *block, struct substring name)
{
  struct repeat_macro *macro;

  ll_for_each (macro, struct repeat_macro, ll, &block->macros)
    if (ss_equals (macro->name, name))
      return macro;

  return NULL;
}

/* Advances LINE past white space and an identifier, if present.
   Returns true if KEYWORD matches the identifer, false
   otherwise. */
static bool
recognize_keyword (struct substring *line, const char *keyword)
{
  struct substring id;
  ss_ltrim (line, ss_cstr (CC_SPACES));
  ss_get_bytes (line, lex_id_get_length (*line), &id);
  return lex_id_match (ss_cstr (keyword), id);
}

/* Returns true if LINE contains a DO REPEAT command, false
   otherwise. */
static bool
recognize_do_repeat (struct substring line)
{
  return (recognize_keyword (&line, "do")
          && recognize_keyword (&line, "repeat"));
}

/* Returns true if LINE contains an END REPEAT command, false
   otherwise.  Sets *PRINT to true for END REPEAT PRINT, false
   otherwise. */
static bool
recognize_end_repeat (struct substring line, bool *print)
{
  if (!recognize_keyword (&line, "end")
      || !recognize_keyword (&line, "repeat"))
    return false;

  *print = recognize_keyword (&line, "print");
  return true;
}

/* Read all the lines we are going to substitute, inside the DO
   REPEAT...END REPEAT block. */
static bool
parse_lines (struct lexer *lexer, struct repeat_block *block)
{
  char *previous_file_name;
  int nesting_level;

  previous_file_name = NULL;
  nesting_level = 0;

  for (;;)
    {
      const char *cur_file_name;
      struct repeat_line *line;
      struct string text;
      bool command_ends_before_line, command_ends_after_line;

      /* Retrieve an input line and make a copy of it. */
      if (!lex_get_line_raw (lexer))
        {
          msg (SE, _("DO REPEAT without END REPEAT."));
          return false;
        }
      ds_init_string (&text, lex_entire_line_ds (lexer));

      /* Record file name. */
      cur_file_name = getl_source_name (lex_get_source_stream (lexer));
      if (cur_file_name != NULL &&
	  (previous_file_name == NULL
           || !strcmp (cur_file_name, previous_file_name)))
        previous_file_name = pool_strdup (block->pool, cur_file_name);

      /* Create a line structure. */
      line = pool_alloc (block->pool, sizeof *line);
      line->file_name = previous_file_name;
      line->line_number = getl_source_location (lex_get_source_stream (lexer));
      ss_alloc_substring_pool (&line->text, ds_ss (&text), block->pool);


      /* Check whether the line contains a DO REPEAT or END
         REPEAT command. */
      lex_preprocess_line (&text,
			   lex_current_syntax_mode (lexer),
                           &command_ends_before_line,
                           &command_ends_after_line);
      if (recognize_do_repeat (ds_ss (&text)))
        {
          if (settings_get_syntax () == COMPATIBLE)
            msg (SE, _("DO REPEAT may not nest in compatibility mode."));
          else
            nesting_level++;
        }
      else if (recognize_end_repeat (ds_ss (&text), &block->print)
               && nesting_level-- == 0)
        {
          lex_discard_line (lexer);
	  ds_destroy (&text);
          return true;
        }
      ds_destroy (&text);

      /* Add the line to the list. */
      ll_push_tail (&block->lines, &line->ll);
    }
}

/* Creates variables for the given DO REPEAT. */
static void
create_vars (struct repeat_block *block)
{
  struct repeat_macro *macro;

  ll_for_each (macro, struct repeat_macro, ll, &block->macros)
    if (macro->type == VAR_NAMES)
      {
        int i;

        for (i = 0; i < block->loop_cnt; i++)
          {
            /* Ignore return value: if the variable already
               exists there is no harm done. */
            char *var_name = ss_xstrdup (macro->replacements[i]);
            dict_create_var (dataset_dict (block->ds), var_name, 0);
            free (var_name);
          }
      }
}

/* Parses a set of ids for DO REPEAT. */
static int
parse_ids (struct lexer *lexer, const struct dictionary *dict,
	   struct repeat_macro *macro, struct pool *pool)
{
  char **replacements;
  size_t n, i;

  macro->type = VAR_NAMES;
  if (!parse_mixed_vars_pool (lexer, dict, pool, &replacements, &n, PV_NONE))
    return 0;

  macro->replacements = pool_nalloc (pool, n, sizeof *macro->replacements);
  for (i = 0; i < n; i++)
    macro->replacements[i] = ss_cstr (replacements[i]);
  return n;
}

/* Adds REPLACEMENT to MACRO's list of replacements, which has
   *USED elements and has room for *ALLOCATED.  Allocates memory
   from POOL. */
static void
add_replacement (struct substring replacement,
                 struct repeat_macro *macro, struct pool *pool,
                 size_t *used, size_t *allocated)
{
  if (*used == *allocated)
    macro->replacements = pool_2nrealloc (pool, macro->replacements, allocated,
                                          sizeof *macro->replacements);
  macro->replacements[(*used)++] = replacement;
}

/* Parses a list or range of numbers for DO REPEAT. */
static int
parse_numbers (struct lexer *lexer, struct repeat_macro *macro,
	       struct pool *pool)
{
  size_t used = 0;
  size_t allocated = 0;

  macro->type = OTHER;
  macro->replacements = NULL;

  do
    {
      bool integer_value_seen;
      double a, b, i;

      /* Parse A TO B into a, b. */
      if (!lex_force_num (lexer))
	return 0;

      if ( (integer_value_seen = lex_is_integer (lexer) ) )
	a = lex_integer (lexer);
      else
	a = lex_number (lexer);

      lex_get (lexer);
      if (lex_token (lexer) == T_TO)
	{
	  if ( !integer_value_seen )
	    {
	      msg (SE, _("Ranges may only have integer bounds"));
	      return 0;
	    }
	  lex_get (lexer);
	  if (!lex_force_int (lexer))
	    return 0;
	  b = lex_integer (lexer);
          if (b < a)
            {
              msg (SE, _("%g TO %g is an invalid range."), a, b);
              return 0;
            }
	  lex_get (lexer);
	}
      else
        b = a;

      for (i = a; i <= b; i++)
        add_replacement (ss_cstr (pool_asprintf (pool, "%g", i)),
                         macro, pool, &used, &allocated);

      lex_match (lexer, T_COMMA);
    }
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD);

  return used;
}

/* Parses a list of strings for DO REPEAT. */
int
parse_strings (struct lexer *lexer, struct repeat_macro *macro, struct pool *pool)
{
  size_t used = 0;
  size_t allocated = 0;

  macro->type = OTHER;
  macro->replacements = NULL;

  do
    {
      char *string;

      if (!lex_force_string (lexer))
	{
	  msg (SE, _("String expected."));
	  return 0;
	}

      string = lex_token_representation (lexer);
      pool_register (pool, free, string);
      add_replacement (ss_cstr (string), macro, pool, &used, &allocated);

      lex_get (lexer);
      lex_match (lexer, T_COMMA);
    }
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD);

  return used;
}

int
cmd_end_repeat (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  msg (SE, _("No matching DO REPEAT."));
  return CMD_CASCADING_FAILURE;
}

/* Finds a DO REPEAT macro with the given NAME and returns the
   appropriate substitution if found, or NAME otherwise. */
static struct substring
find_substitution (struct repeat_block *block, struct substring name)
{
  struct repeat_macro *macro = find_macro (block, name);
  return macro ? macro->replacements[block->loop_idx] : name;
}

/* Makes appropriate DO REPEAT macro substitutions within the
   repeated lines. */
static void
do_repeat_filter (struct getl_interface *interface, struct string *line)
{
  struct repeat_block *block
    = UP_CAST (interface, struct repeat_block, parent);
  bool in_apos, in_quote, dot;
  struct substring input;
  struct string output;
  int c;

  ds_init_empty (&output);

  /* Strip trailing whitespace, check for & remove terminal dot. */
  ds_rtrim (line, ss_cstr (CC_SPACES));
  dot = ds_chomp (line, settings_get_endcmd ());
  input = ds_ss (line);
  in_apos = in_quote = false;
  while ((c = ss_first (input)) != EOF)
    {
      if (c == '\'' && !in_quote)
	in_apos = !in_apos;
      else if (c == '"' && !in_apos)
	in_quote = !in_quote;

      if (in_quote || in_apos || !lex_is_id1 (c))
        {
          ds_put_byte (&output, c);
          ss_advance (&input, 1);
        }
      else
        {
          struct substring id;
          ss_get_bytes (&input, lex_id_get_length (input), &id);
          ds_put_substring (&output, find_substitution (block, id));
        }
    }
  if (dot)
    ds_put_byte (&output, settings_get_endcmd ());

  ds_swap (line, &output);
  ds_destroy (&output);
}

static struct repeat_line *
current_line (const struct getl_interface *interface)
{
  struct repeat_block *block
    = UP_CAST (interface, struct repeat_block, parent);
  return (block->cur_line != ll_null (&block->lines)
          ? ll_data (block->cur_line, struct repeat_line, ll)
          : NULL);
}

/* Function called by getl to read a line.  Puts the line in
   OUTPUT and its syntax mode in *SYNTAX.  Returns true if a line
   was obtained, false if the source is exhausted. */
static bool
do_repeat_read  (struct getl_interface *interface,
                 struct string *output)
{
  struct repeat_block *block
    = UP_CAST (interface, struct repeat_block, parent);
  struct repeat_line *line;

  block->cur_line = ll_next (block->cur_line);
  if (block->cur_line == ll_null (&block->lines))
    {
      block->loop_idx++;
      if (block->loop_idx >= block->loop_cnt)
        return false;

      block->cur_line = ll_head (&block->lines);
    }

  line = current_line (interface);
  ds_assign_substring (output, line->text);
  return true;
}

/* Frees a DO REPEAT block.
   Called by getl to close out the DO REPEAT block. */
static void
do_repeat_close (struct getl_interface *interface)
{
  struct repeat_block *block
    = UP_CAST (interface, struct repeat_block, parent);
  pool_destroy (block->pool);
}


static bool
always_false (const struct getl_interface *i UNUSED)
{
  return false;
}

/* Returns the name of the source file from which the previous
   line was originally obtained, or a null pointer if none. */
static const char *
do_repeat_name (const struct getl_interface *interface)
{
  struct repeat_line *line = current_line (interface);
  return line ? line->file_name : NULL;
}

/* Returns the line number in the source file from which the
   previous line was originally obtained, or 0 if none. */
static int
do_repeat_location (const struct getl_interface *interface)
{
  struct repeat_line *line = current_line (interface);
  return line ? line->line_number : 0;
}
