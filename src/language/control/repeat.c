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
#include "message.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "dictionary.h"
#include "message.h"
#include "line-buffer.h"
#include "lexer.h"
#include "misc.h"
#include "pool.h"
#include "settings.h"
#include "str.h"
#include "variable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include "debug-print.h"

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
    struct pool *pool;                  /* Pool used for storage. */
    struct line_list *first_line;       /* First line in line buffer. */
    struct line_list *cur_line;         /* Current line in line buffer. */
    int loop_cnt;                       /* Number of loops. */
    int loop_idx;                       /* Number of loops so far. */
    struct repeat_entry *macros;        /* Pointer to macro table. */
    bool print;                         /* Print lines as executed? */
  };

static bool parse_specification (struct repeat_block *);
static bool parse_lines (struct repeat_block *);
static void create_vars (struct repeat_block *);

static int parse_ids (struct repeat_entry *);
static int parse_numbers (struct repeat_entry *);
static int parse_strings (struct repeat_entry *);

static void do_repeat_filter (struct string *line, void *block);
static bool do_repeat_read (struct string *line, char **file_name,
                            int *line_number, void *block);
static void do_repeat_close (void *block);

int
cmd_do_repeat (void)
{
  struct repeat_block *block;

  block = pool_create_container (struct repeat_block, pool);

  if (!parse_specification (block) || !parse_lines (block))
    goto error;
  
  create_vars (block);
  
  block->cur_line = NULL;
  block->loop_idx = -1;
  getl_include_filter (do_repeat_filter, do_repeat_close, block);
  getl_include_function (do_repeat_read, NULL, block);

  return CMD_SUCCESS;

 error:
  pool_destroy (block->pool);
  return CMD_CASCADING_FAILURE;
}

/* Parses the whole DO REPEAT command specification.
   Returns success. */
static bool
parse_specification (struct repeat_block *block) 
{
  char first_name[LONG_NAME_LEN + 1];

  block->loop_cnt = 0;
  block->macros = NULL;
  do
    {
      struct repeat_entry *e;
      struct repeat_entry *iter;
      int count;

      /* Get a stand-in variable name and make sure it's unique. */
      if (!lex_force_id ())
	return false;
      if (dict_lookup_var (default_dict, tokid))
        msg (SW, _("Dummy variable name \"%s\" hides dictionary "
                   "variable \"%s\"."),
             tokid, tokid);
      for (iter = block->macros; iter != NULL; iter = iter->next)
	if (!strcasecmp (iter->id, tokid))
	  {
	    msg (SE, _("Dummy variable name \"%s\" is given twice."), tokid);
	    return false;
	  }

      /* Make a new stand-in variable entry and link it into the
         list. */
      e = pool_alloc (block->pool, sizeof *e);
      e->next = block->macros;
      strcpy (e->id, tokid);
      block->macros = e;

      /* Skip equals sign. */
      lex_get ();
      if (!lex_force_match ('='))
	return false;

      /* Get the details of the variable's possible values. */
      if (token == T_ID)
	count = parse_ids (e);
      else if (lex_is_number ())
	count = parse_numbers (e);
      else if (token == T_STRING)
	count = parse_strings (e);
      else
	{
	  lex_error (NULL);
	  return false;
	}
      if (count == 0)
	return false;

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

      lex_match ('/');
    }
  while (token != '.');

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
parse_lines (struct repeat_block *block) 
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
      const char *cur_file_name;
      int cur_line_number;
      struct line_list *line;
      bool dot;

      if (!getl_read_line (NULL))
        return false;

      /* If the current file has changed then record the fact. */
      getl_location (&cur_file_name, &cur_line_number);
      if (previous_file_name == NULL 
          || !strcmp (cur_file_name, previous_file_name))
        previous_file_name = pool_strdup (block->pool, cur_file_name);

      ds_rtrim_spaces (&getl_buf);
      dot = ds_chomp (&getl_buf, get_endcmd ());
      if (recognize_do_repeat (ds_c_str (&getl_buf))) 
        nesting_level++; 
      else if (recognize_end_repeat (ds_c_str (&getl_buf), &block->print)) 
        {
        if (nesting_level-- == 0)
          {
            lex_discard_line ();
            return true;
          } 
        }
      if (dot)
        ds_putc (&getl_buf, get_endcmd ());
      
      line = *last_line = pool_alloc (block->pool, sizeof *line);
      line->next = NULL;
      line->file_name = previous_file_name;
      line->line_number = cur_line_number;
      line->line = pool_strdup (block->pool, ds_c_str (&getl_buf));
      last_line = &line->next;
    }

  lex_discard_line ();
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
            dict_create_var (default_dict, iter->replacement[i], 0);
          }
      }
}

/* Parses a set of ids for DO REPEAT. */
static int
parse_ids (struct repeat_entry *e)
{
  size_t i;
  size_t n = 0;

  e->type = VAR_NAMES;
  e->replacement = NULL;

  do
    {
      char **names;
      size_t nnames;

      if (!parse_mixed_vars (&names, &nnames, PV_NONE))
	return 0;

      e->replacement = xnrealloc (e->replacement,
                                  nnames + n, sizeof *e->replacement);
      for (i = 0; i < nnames; i++)
	{
	  e->replacement[n + i] = xstrdup (names[i]);
	  free (names[i]);
	}
      free (names);
      n += nnames;
    }
  while (token != '/' && token != '.');

  return n;
}

/* Stores VALUE into *REPL. */
static inline void
store_numeric (char **repl, long value)
{
  *repl = xmalloc (INT_DIGITS + 1);
  sprintf (*repl, "%ld", value);
}

/* Parses a list of numbers for DO REPEAT. */
static int
parse_numbers (struct repeat_entry *e)
{
  /* First and last numbers for TO, plus the step factor. */
  long a, b;

  /* Alias to e->replacement. */
  char **array;

  /* Number of entries in array; maximum number for this allocation
     size. */
  int n, m;

  n = m = 0;
  e->type = OTHER;
  e->replacement = array = NULL;

  do
    {
      /* Parse A TO B into a, b. */
      if (!lex_force_int ())
	return 0;
      a = lex_integer ();

      lex_get ();
      if (token == T_TO)
	{
	  lex_get ();
	  if (!lex_force_int ())
	    return 0;
	  b = lex_integer ();

	  lex_get ();
	}
      else b = a;

      if (n + (abs (b - a) + 1) > m)
	{
	  m = n + (abs (b - a) + 1) + 16;
	  e->replacement = array = xnrealloc (array,
                                              m, sizeof *e->replacement);
	}

      if (a == b)
	store_numeric (&array[n++], a);
      else
	{
	  long iter;

	  if (a < b)
	    for (iter = a; iter <= b; iter++)
	      store_numeric (&array[n++], iter);
	  else
	    for (iter = a; iter >= b; iter--)
	      store_numeric (&array[n++], iter);
	}

      lex_match (',');
    }
  while (token != '/' && token != '.');
  e->replacement = xrealloc (array, n * sizeof *e->replacement);

  return n;
}

/* Parses a list of strings for DO REPEAT. */
int
parse_strings (struct repeat_entry *e)
{
  char **string;
  int n, m;

  e->type = OTHER;
  string = e->replacement = NULL;
  n = m = 0;

  do
    {
      if (token != T_STRING)
	{
	  int i;
	  msg (SE, _("String expected."));
	  for (i = 0; i < n; i++)
	    free (string[i]);
	  free (string);
	  return 0;
	}

      if (n + 1 > m)
	{
	  m += 16;
	  e->replacement = string = xnrealloc (string,
                                               m, sizeof *e->replacement);
	}
      string[n++] = lex_token_representation ();
      lex_get ();

      lex_match (',');
    }
  while (token != '/' && token != '.');
  e->replacement = xnrealloc (string, n, sizeof *e->replacement);

  return n;
}

int
cmd_end_repeat (void)
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

/* Makes appropriate DO REPEAT macro substitutions within getl_buf. */
static void
do_repeat_filter (struct string *line, void *block_)
{
  struct repeat_block *block = block_;
  bool in_apos, in_quote;
  char *cp;
  struct string output;
  bool dot;

  ds_init (&output, ds_capacity (line));

  /* Strip trailing whitespace, check for & remove terminal dot. */
  while (isspace (ds_last (line)))
    ds_truncate (line, ds_length (line) - 1);
  dot = ds_chomp (line, get_endcmd ());

  in_apos = in_quote = false;
  for (cp = ds_c_str (line); cp < ds_end (line); )
    {
      if (*cp == '\'' && !in_quote)
	in_apos = !in_apos;
      else if (*cp == '"' && !in_apos)
	in_quote = !in_quote;
      
      if (in_quote || in_apos || !lex_is_id1 (*cp))
        ds_putc (&output, *cp++);
      else 
        {
          const char *start = cp;
          char *end = lex_skip_identifier (start);
          const char *substitution = find_substitution (block,
                                                        start, end - start);
          if (substitution != NULL) 
            ds_puts (&output, substitution);
          else
            ds_concat (&output, start, end - start);
          cp = end;
        }
    }
  if (dot)
    ds_putc (&output, get_endcmd ());

  ds_swap (line, &output);
  ds_destroy (&output);
}

/* Function called by getl to read a line.
   Puts the line in OUTPUT, sets the file name in *FILE_NAME and
   line number in *LINE_NUMBER.  Returns true if a line was
   obtained, false if the source is exhausted. */
static bool
do_repeat_read (struct string *output, char **file_name, int *line_number,
                void *block_) 
{
  struct repeat_block *block = block_;
  struct line_list *line;

  if (block->cur_line == NULL) 
    {
      block->loop_idx++;
      if (block->loop_idx >= block->loop_cnt)
        return false;
      block->cur_line = block->first_line;
    }
  line = block->cur_line;

  ds_replace (output, line->line);
  *file_name = line->file_name;
  *line_number = -line->line_number;
  block->cur_line = line->next;
  return true;
}

/* Frees a DO REPEAT block.
   Called by getl to close out the DO REPEAT block. */
static void
do_repeat_close (void *block_)
{
  struct repeat_block *block = block_;
  pool_destroy (block->pool);
}
