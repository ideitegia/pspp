/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
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
#include <libpspp/alloc.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>
#include <data/variable.h>

#include "intprops.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Defines a list of lines used by DO REPEAT. */
struct line_list
  {
    struct line_list *next;	/* Next line. */
    char *file_name;            /* File name. */
    int line_number;            /* Line number. */
    char *line;			/* Contents. */
  };

/* The type of substitution made for a DO REPEAT macro. */
enum repeat_entry_type 
  {
    VAR_NAMES,
    OTHER
  };

/* Describes one DO REPEAT macro. */
struct repeat_entry
  {
    struct repeat_entry *next;          /* Next entry. */
    enum repeat_entry_type type;        /* Types of replacements. */
    char id[LONG_NAME_LEN + 1];         /* Macro identifier. */
    char **replacement;                 /* Macro replacement. */
  };

/* A DO REPEAT...END REPEAT block. */
struct repeat_block 
  {
    struct getl_interface parent ;

    struct pool *pool;                  /* Pool used for storage. */
    struct dataset *ds;                 /* The dataset for this block */
    struct line_list *first_line;       /* First line in line buffer. */
    struct line_list *cur_line;         /* Current line in line buffer. */
    int loop_cnt;                       /* Number of loops. */
    int loop_idx;                       /* Number of loops so far. */
    struct repeat_entry *macros;        /* Pointer to macro table. */
    bool print;                         /* Print lines as executed? */
  };

static bool parse_specification (struct lexer *, struct repeat_block *);
static bool parse_lines (struct lexer *, struct repeat_block *);
static void create_vars (struct repeat_block *);

static int parse_ids (struct lexer *, const struct dictionary *dict, 
		      struct repeat_entry *, struct pool *);

static int parse_numbers (struct lexer *, struct repeat_entry *, 
			  struct pool *);

static int parse_strings (struct lexer *, struct repeat_entry *, 
			  struct pool *);

static void do_repeat_filter (struct getl_interface *, struct string *);
static bool do_repeat_read (struct getl_interface *, struct string *);
static void do_repeat_close (struct getl_interface *);
static bool always_false (const struct getl_interface *i UNUSED);


int
cmd_do_repeat (struct lexer *lexer, struct dataset *ds)
{
  struct repeat_block *block;

  block = pool_create_container (struct repeat_block, pool);
  block->ds = ds;

  if (!parse_specification (lexer, block) || !parse_lines (lexer, block))
    goto error;
  
  create_vars (block);
  
  block->cur_line = NULL;
  block->loop_idx = -1;

  block->parent.read = do_repeat_read;
  block->parent.close = do_repeat_close;
  block->parent.filter = do_repeat_filter;
  block->parent.interactive = always_false;

  getl_include_source ( (struct getl_interface *) block);

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
  char first_name[LONG_NAME_LEN + 1];

  block->loop_cnt = 0;
  block->macros = NULL;
  do
    {
      struct repeat_entry *e;
      struct repeat_entry *iter;
      struct dictionary *dict = dataset_dict (block->ds);
      int count;

      /* Get a stand-in variable name and make sure it's unique. */
      if (!lex_force_id (lexer))
	return false;
      if (dict_lookup_var (dict, lex_tokid (lexer)))
        msg (SW, _("Dummy variable name \"%s\" hides dictionary "
                   "variable \"%s\"."),
             lex_tokid (lexer), lex_tokid (lexer));
      for (iter = block->macros; iter != NULL; iter = iter->next)
	if (!strcasecmp (iter->id, lex_tokid (lexer)))
	  {
	    msg (SE, _("Dummy variable name \"%s\" is given twice."), 
		 lex_tokid (lexer));
	    return false;
	  }

      /* Make a new stand-in variable entry and link it into the
         list. */
      e = pool_alloc (block->pool, sizeof *e);
      e->next = block->macros;
      strcpy (e->id, lex_tokid (lexer));
      block->macros = e;

      /* Skip equals sign. */
      lex_get (lexer);
      if (!lex_force_match (lexer, '='))
	return false;

      /* Get the details of the variable's possible values. */
      if (lex_token (lexer) == T_ID)
	count = parse_ids (lexer, dict, e, block->pool);
      else if (lex_is_number (lexer))
	count = parse_numbers (lexer, e, block->pool);
      else if (lex_token (lexer) == T_STRING)
	count = parse_strings (lexer, e, block->pool);
      else
	{
	  lex_error (lexer, NULL);
	  return false;
	}
      if (count == 0)
	return false;
      if (lex_token (lexer) != '/' && lex_token (lexer) != '.') 
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
	  strcpy (first_name, e->id);
	}
      else if (block->loop_cnt != count)
	{
	  msg (SE, _("Dummy variable \"%s\" had %d "
                     "substitutions, so \"%s\" must also, but %d "
                     "were specified."),
	       first_name, block->loop_cnt, e->id, count);
	  return false;
	}

      lex_match (lexer, '/');
    }
  while (lex_token (lexer) != '.');

  return true;
}

/* If KEYWORD appears beginning at CP, possibly preceded by white
   space, returns a pointer to the character just after the
   keyword.  Otherwise, returns a null pointer. */
static const char *
recognize_keyword (const char *cp, const char *keyword)
{
  const char *end;

  while (isspace ((unsigned char) *cp))
    cp++;

  end = lex_skip_identifier (cp);
  if (end != cp
      && lex_id_match_len (keyword, strlen (keyword), cp, end - cp))
    return end;
  else
    return NULL;
}

/* Returns CP, advanced past a '+' or '-' if present. */
static const char *
skip_indentor (const char *cp) 
{
  if (*cp == '+' || *cp == '-')
    cp++;
  return cp;
}

/* Returns true if LINE contains a DO REPEAT command, false
   otherwise. */
static bool
recognize_do_repeat (const char *line) 
{
  const char *cp = recognize_keyword (skip_indentor (line), "do");
  return cp != NULL && recognize_keyword (cp, "repeat") != NULL;
}

/* Returns true if LINE contains an END REPEAT command, false
   otherwise.  Sets *PRINT to true for END REPEAT PRINT, false
   otherwise. */
static bool
recognize_end_repeat (const char *line, bool *print)
{
  const char *cp = recognize_keyword (skip_indentor (line), "end");
  if (cp == NULL)
    return false;

  cp = recognize_keyword (cp, "repeat");
  if (cp == NULL) 
    return false; 

  *print = recognize_keyword (cp, "print");
  return true; 
}

/* Read all the lines we are going to substitute, inside the DO
   REPEAT...END REPEAT block. */
static bool
parse_lines (struct lexer *lexer, struct repeat_block *block) 
{
  char *previous_file_name;
  struct line_list **last_line;
  int nesting_level;

  previous_file_name = NULL;
  block->first_line = NULL;
  last_line = &block->first_line;
  nesting_level = 0;

  for (;;)
    {
      const char *cur_file_name = getl_source_name ();
      int cur_line_number = getl_source_location ();
      struct line_list *line;
      struct string cur_line_copy;
      bool dot;

      if (! lex_get_line_raw (lexer))
        return false;

      /* If the current file has changed then record the fact. */
      if (cur_file_name && 
	  (previous_file_name == NULL 
          || !strcmp (cur_file_name, previous_file_name))
	  )
        previous_file_name = pool_strdup (block->pool, cur_file_name);

      ds_init_string (&cur_line_copy, lex_entire_line_ds (lexer) );
      ds_rtrim (&cur_line_copy, ss_cstr (CC_SPACES));
      dot = ds_chomp (&cur_line_copy, get_endcmd ());

      if (recognize_do_repeat (ds_cstr (&cur_line_copy)))
        nesting_level++; 
      else if (recognize_end_repeat (ds_cstr (&cur_line_copy), &block->print)) 
        {
        if (nesting_level-- == 0)
          {
            lex_discard_line (lexer);
	    ds_destroy (&cur_line_copy);
            return true;
          } 
        }
      if (dot)
        ds_put_char (&cur_line_copy, get_endcmd ());
      
      line = *last_line = pool_alloc (block->pool, sizeof *line);
      line->next = NULL;
      line->file_name = previous_file_name;
      line->line_number = cur_line_number;
      line->line = pool_strdup (block->pool, ds_cstr (&cur_line_copy) );
      last_line = &line->next;

      ds_destroy (&cur_line_copy);
    }

  lex_discard_line (lexer);
  return true;
}

/* Creates variables for the given DO REPEAT. */
static void
create_vars (struct repeat_block *block)
{
  struct repeat_entry *iter;
 
  for (iter = block->macros; iter; iter = iter->next)
    if (iter->type == VAR_NAMES)
      {
        int i;

        for (i = 0; i < block->loop_cnt; i++)
          {
            /* Ignore return value: if the variable already
               exists there is no harm done. */
            dict_create_var (dataset_dict (block->ds), iter->replacement[i], 0);
          }
      }
}

/* Parses a set of ids for DO REPEAT. */
static int
parse_ids (struct lexer *lexer, const struct dictionary *dict, 
	   struct repeat_entry *e, struct pool *pool)
{
  size_t n = 0;
  e->type = VAR_NAMES;
  return parse_mixed_vars_pool (lexer, dict, pool, 
				&e->replacement, &n, PV_NONE) ? n : 0;
}

/* Adds STRING to E's list of replacements, which has *USED
   elements and has room for *ALLOCATED.  Allocates memory from
   POOL. */
static void
add_replacement (char *string,
                 struct repeat_entry *e, struct pool *pool,
                 size_t *used, size_t *allocated) 
{
  if (*used == *allocated)
    e->replacement = pool_2nrealloc (pool, e->replacement, allocated,
                                     sizeof *e->replacement);
  e->replacement[(*used)++] = string;
}

/* Parses a list of numbers for DO REPEAT. */
static int
parse_numbers (struct lexer *lexer, struct repeat_entry *e, struct pool *pool)
{
  size_t used = 0;
  size_t allocated = 0;
  
  e->type = OTHER;
  e->replacement = NULL;

  do
    {
      long a, b, i;

      /* Parse A TO B into a, b. */
      if (!lex_force_int (lexer))
	return 0;
      a = lex_integer (lexer);

      lex_get (lexer);
      if (lex_token (lexer) == T_TO)
	{
	  lex_get (lexer);
	  if (!lex_force_int (lexer))
	    return 0;
	  b = lex_integer (lexer);
          if (b < a) 
            {
              msg (SE, _("%ld TO %ld is an invalid range."), a, b);
              return 0;
            }
	  lex_get (lexer);
	}
      else
        b = a;

      for (i = a; i <= b; i++)
        add_replacement (pool_asprintf (pool, "%ld", i),
                         e, pool, &used, &allocated);


      lex_match (lexer, ',');
    }
  while (lex_token (lexer) != '/' && lex_token (lexer) != '.');

  return used;
}

/* Parses a list of strings for DO REPEAT. */
int
parse_strings (struct lexer *lexer, struct repeat_entry *e, struct pool *pool)
{
  size_t used = 0;
  size_t allocated = 0;
  
  e->type = OTHER;
  e->replacement = NULL;

  do
    {
      char *string;
      
      if (lex_token (lexer) != T_STRING)
	{
	  msg (SE, _("String expected."));
	  return 0;
	}

      string = lex_token_representation (lexer);
      pool_register (pool, free, string);
      add_replacement (string, e, pool, &used, &allocated);

      lex_get (lexer);
      lex_match (lexer, ',');
    }
  while (lex_token (lexer) != '/' && lex_token (lexer) != '.');

  return used;
}

int
cmd_end_repeat (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  msg (SE, _("No matching DO REPEAT."));
  return CMD_CASCADING_FAILURE;
}

/* Finds a DO REPEAT macro with name MACRO_NAME and returns the
   appropriate subsitution if found, or NULL if not. */
static char *
find_substitution (struct repeat_block *block, const char *name, size_t length)
{
  struct repeat_entry *e;

  for (e = block->macros; e; e = e->next)
    if (!memcasecmp (e->id, name, length) && strlen (e->id) == length)
      return e->replacement[block->loop_idx];
  
  return NULL;
}

/* Makes appropriate DO REPEAT macro substitutions within the 
   repeated lines. */
static void
do_repeat_filter (struct getl_interface *block_, struct string *line)
{
  struct repeat_block *block = (struct repeat_block *) block_;
  bool in_apos, in_quote;
  char *cp;
  struct string output;
  bool dot;

  ds_init_empty (&output);

  /* Strip trailing whitespace, check for & remove terminal dot. */
  while (isspace (ds_last (line)))
    ds_truncate (line, ds_length (line) - 1);
  dot = ds_chomp (line, get_endcmd ());

  in_apos = in_quote = false;
  for (cp = ds_cstr (line); cp < ds_end (line); )
    {
      if (*cp == '\'' && !in_quote)
	in_apos = !in_apos;
      else if (*cp == '"' && !in_apos)
	in_quote = !in_quote;
      
      if (in_quote || in_apos || !lex_is_id1 (*cp))
        ds_put_char (&output, *cp++);
      else 
        {
          const char *start = cp;
          char *end = lex_skip_identifier (start);
          const char *substitution = find_substitution (block,
                                                        start, end - start);
          if (substitution != NULL) 
            ds_put_cstr (&output, substitution);
          else
            ds_put_substring (&output, ss_buffer (start, end - start));
          cp = end;
        }
    }
  if (dot)
    ds_put_char (&output, get_endcmd ());

  ds_swap (line, &output);
  ds_destroy (&output);
}

/* Function called by getl to read a line.
   Puts the line in OUTPUT, sets the file name in *FILE_NAME and
   line number in *LINE_NUMBER.  Returns true if a line was
   obtained, false if the source is exhausted. */
static bool  
do_repeat_read  (struct getl_interface *b, struct string *output)
{
  struct repeat_block *block = (struct repeat_block *) b;
  struct line_list *line;

  if (block->cur_line == NULL) 
    {
      block->loop_idx++;
      if (block->loop_idx >= block->loop_cnt)
        return false;
      block->cur_line = block->first_line;
      if (block->cur_line == NULL)
        return false;
    }
  line = block->cur_line;

  ds_assign_cstr (output, line->line);
  block->cur_line = line->next;
  return true;
}

/* Frees a DO REPEAT block.
   Called by getl to close out the DO REPEAT block. */
static void
do_repeat_close (struct getl_interface *block_)
{
  struct repeat_block *block = (struct repeat_block *) block_;
  pool_destroy (block->pool);
}


static bool 
always_false (const struct getl_interface *i UNUSED)
{
  return false;
}
