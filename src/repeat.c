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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include "repeat.h"
#include "error.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "getline.h"
#include "lexer.h"
#include "misc.h"
#include "settings.h"
#include "str.h"
#include "var.h"

#include "debug-print.h"

/* Describes one DO REPEAT macro. */
struct repeat_entry
  {
    int type;			/* 1=variable names, 0=any other. */
    char id[9];			/* Macro identifier. */
    char **replacement;		/* Macro replacement. */
    struct repeat_entry *next;
  };

/* List of macro identifiers. */
static struct repeat_entry *repeat_tab;

/* Number of substitutions for each macro. */
static int count;

/* List of lines before it's actually assigned to a file. */
static struct getl_line_list *line_buf_head;
static struct getl_line_list *line_buf_tail;

static int parse_ids (struct repeat_entry *);
static int parse_numbers (struct repeat_entry *);
static int parse_strings (struct repeat_entry *);
static void clean_up (void);
static int internal_cmd_do_repeat (void);

int
cmd_do_repeat (void)
{
  if (internal_cmd_do_repeat ())
    return CMD_SUCCESS;

  clean_up ();
  return CMD_FAILURE;
}

/* Garbage collects all the allocated memory that's no longer
   needed. */
static void
clean_up (void)
{
  struct repeat_entry *iter, *next;
  int i;

  iter = repeat_tab;
  repeat_tab = NULL;

  while (iter)
    {
      if (iter->replacement)
	{
	  for (i = 0; i < count; i++)
	    free (iter->replacement[i]);
	  free (iter->replacement);
	}
      next = iter->next;
      free (iter);
      iter = next;
    }
}

/* Allocates & appends another record at the end of the line_buf_tail
   chain. */
static inline void
append_record (void)
{
  struct getl_line_list *new = xmalloc (sizeof *new);
  
  if (line_buf_head == NULL)
    line_buf_head = line_buf_tail = new;
  else
    line_buf_tail = line_buf_tail->next = new;
}

/* Returns nonzero if KEYWORD appears beginning at CONTEXT. */
static int
recognize_keyword (const char *context, const char *keyword)
{
  const char *end = context;
  while (isalpha ((unsigned char) *end))
    end++;
  return lex_id_match_len (keyword, strlen (keyword), context, end - context);
}

/* Does the real work of parsing the DO REPEAT command and its nested
   commands. */
static int
internal_cmd_do_repeat (void)
{
  /* Name of first DO REPEAT macro. */
  char first_name[9];

  /* Current filename. */
  const char *current_filename = NULL;

  /* 1=Print lines after preprocessing. */
  int print;

  /* The first step is parsing the DO REPEAT command itself. */
  count = 0;
  line_buf_head = NULL;
  do
    {
      struct repeat_entry *e;
      struct repeat_entry *iter;
      int result;

      /* Get a stand-in variable name and make sure it's unique. */
      if (!lex_force_id ())
	return 0;
      for (iter = repeat_tab; iter; iter = iter->next)
	if (!strcmp (iter->id, tokid))
	  {
	    msg (SE, _("Identifier %s is given twice."), tokid);
	    return 0;
	  }

      /* Make a new stand-in variable entry and link it into the
         list. */
      e = xmalloc (sizeof *e);
      e->type = 0;
      e->next = repeat_tab;
      strcpy (e->id, tokid);
      repeat_tab = e;

      /* Skip equals sign. */
      lex_get ();
      if (!lex_force_match ('='))
	return 0;

      /* Get the details of the variable's possible values. */
      
      if (token == T_ID)
	result = parse_ids (e);
      else if (token == T_NUM)
	result = parse_numbers (e);
      else if (token == T_STRING)
	result = parse_strings (e);
      else
	{
	  lex_error (NULL);
	  return 0;
	}
      if (!result)
	return 0;

      /* If this is the first variable then it defines how many
	 replacements there must be; otherwise enforce this number of
	 replacements. */
      if (!count)
	{
	  count = result;
	  strcpy (first_name, e->id);
	}
      else if (count != result)
	{
	  msg (SE, _("There must be the same number of substitutions "
		     "for each dummy variable specified.  Since there "
		     "were %d substitutions for %s, there must be %d "
		     "for %s as well, but %d were specified."),
	       count, first_name, count, e->id, result);
	  return 0;
	}

      /* Next! */
      lex_match ('/');
    }
  while (token != '.');

  /* Read all the lines inside the DO REPEAT ... END REPEAT. */
  {
    int nest = 1;

    for (;;)
      {
	if (!getl_read_line ())
	  msg (FE, _("Unexpected end of file."));

	/* If the current file has changed then record the fact. */
	{
	  const char *curfn;
	  int curln;

	  getl_location (&curfn, &curln);
	  if (current_filename != curfn)
	    {
	      assert (curln > 0 && curfn != NULL);
	    
	      append_record ();
	      line_buf_tail->len = -curln;
	      line_buf_tail->line = xstrdup (curfn);
	      current_filename = curfn;
	    }
	}
	
	/* FIXME?  This code is not strictly correct, however if you
	   have begun a line with DO REPEAT or END REPEAT and it's
	   *not* a command name, then you are obviously *trying* to
	   break this mechanism.  And you will.  Also, the entire
	   command names must appear on a single line--they can't be
	   spread out. */
	{
	  char *cp = ds_c_str (&getl_buf);

	  /* Skip leading indentors and any whitespace. */
	  if (*cp == '+' || *cp == '-' || *cp == '.')
	    cp++;
	  while (isspace ((unsigned char) *cp))
	    cp++;

	  /* Find END REPEAT. */
	  if (recognize_keyword (cp, "end"))
	    {
	      while (isalpha ((unsigned char) *cp))
		cp++;
	      while (isspace ((unsigned char) *cp))
		cp++;
	      if (recognize_keyword (cp, "repeat"))
		{
		  nest--;

		  if (!nest)
		  {
		    while (isalpha ((unsigned char) *cp))
		      cp++;
		    while (isspace ((unsigned char) *cp))
		      cp++;

		    print = recognize_keyword (cp, "print");
		    break;
		  }
		}
	    }
	  else /* Find DO REPEAT. */
	    if (!strncasecmp (cp, "do", 2))
	      {
		cp += 2;
		while (isspace ((unsigned char) *cp))
		  cp++;
		if (!strncasecmp (cp, "rep", 3))
		  nest++;
	      }
	}

	append_record ();
	line_buf_tail->len = ds_length (&getl_buf);
	line_buf_tail->line = xmalloc (ds_length (&getl_buf) + 1);
	memcpy (line_buf_tail->line,
		ds_c_str (&getl_buf), ds_length (&getl_buf) + 1);
      }
  }

  /* FIXME: For the moment we simply discard the contents of the END
     REPEAT line.  We should actually check for the PRINT specifier.
     This can be done easier when we buffer entire commands instead of
     doing it token by token; see TODO. */
  lex_discard_line ();	
  
  /* Tie up the loose end of the chain. */
  if (line_buf_head == NULL)
    {
      msg (SW, _("No commands in scope."));
      return 1;
    }
  line_buf_tail->next = NULL;

  /* Make new variables. */
  {
    struct repeat_entry *iter;
    for (iter = repeat_tab; iter; iter = iter->next)
      if (iter->type == 1)
	{
	  int i;
	  for (i = 0; i < count; i++)
	    {
	      /* Note that if the variable already exists there is no
		 harm done. */
	      dict_create_var (default_dict, iter->replacement[i], 0);
	    }
	}
  }

  /* Create the DO REPEAT virtual input file. */
  {
    struct getl_script *script = xmalloc (sizeof *script);

    script->first_line = line_buf_head;
    script->cur_line = NULL;
    script->remaining_loops = count;
    script->loop_index = -1;
    script->macros = repeat_tab;
    script->print = print;

    getl_add_DO_REPEAT_file (script);
  }

  return 1;
}

/* Parses a set of ids for DO REPEAT. */
static int
parse_ids (struct repeat_entry * e)
{
  int i;
  int n = 0;

  e->type = 1;
  e->replacement = NULL;

  do
    {
      char **names;
      int nnames;

      if (!parse_mixed_vars (&names, &nnames, PV_NONE))
	return 0;

      e->replacement = xrealloc (e->replacement,
				 (nnames + n) * sizeof *e->replacement);
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
  e->type = 0;
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
	  e->replacement = array = xrealloc (array,
					     m * sizeof *e->replacement);
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
parse_strings (struct repeat_entry * e)
{
  char **string;
  int n, m;

  e->type = 0;
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
	  e->replacement = string = xrealloc (string,
					      m * sizeof *e->replacement);
	}
      string[n++] = lex_token_representation ();
      lex_get ();

      lex_match (',');
    }
  while (token != '/' && token != '.');
  e->replacement = xrealloc (string, n * sizeof *e->replacement);

  return n;
}

int
cmd_end_repeat (void)
{
  msg (SE, _("No matching DO REPEAT."));
  return CMD_FAILURE;
}

/* Finds a DO REPEAT macro with name MACRO_NAME and returns the
   appropriate subsitution if found, or NULL if not. */
static char *
find_DO_REPEAT_substitution (char *macro_name)
{
  struct getl_script *s;
	    
  for (s = getl_head; s; s = s->included_from)
    {
      struct repeat_entry *e;
      
      if (s->first_line == NULL)
	continue;

      for (e = s->macros; e; e = e->next)
	if (!strcasecmp (e->id, macro_name))
	  return e->replacement[s->loop_index];
    }
  
  return NULL;
}

/* Makes appropriate DO REPEAT macro substitutions within getl_buf. */
void
perform_DO_REPEAT_substitutions (void)
{
  /* Are we in an apostrophized string or a quoted string? */
  int in_apos = 0, in_quote = 0;

  /* Source pointer. */
  char *cp;

  /* Output buffer, size, pointer. */
  struct string output;

  /* Terminal dot. */
  int dot = 0;

  ds_init (&output, ds_capacity (&getl_buf));

  /* Strip trailing whitespace, check for & remove terminal dot. */
  while (ds_length (&getl_buf) > 0
	 && isspace ((unsigned char) ds_end (&getl_buf)[-1]))
    ds_truncate (&getl_buf, ds_length (&getl_buf) - 1);
  if (ds_length (&getl_buf) > 0 && ds_end (&getl_buf)[-1] == get_endcmd() )
    {
      dot = 1;
      ds_truncate (&getl_buf, ds_length (&getl_buf) - 1);
    }
  
  for (cp = ds_c_str (&getl_buf); cp < ds_end (&getl_buf); )
    {
      if (*cp == '\'' && !in_quote)
	in_apos ^= 1;
      else if (*cp == '"' && !in_apos)
	in_quote ^= 1;
      
      if (in_quote || in_apos || !CHAR_IS_ID1 (*cp))
	{
	  ds_putc (&output, *cp++);
	  continue;
	}

      /* Collect an identifier. */
      {
	char name[9];
	char *start = cp;
	char *np = name;
	char *substitution;

	while (CHAR_IS_IDN (*cp) && np < &name[8])
	  *np++ = *cp++;
	while (CHAR_IS_IDN (*cp))
	  cp++;
	*np = 0;

	substitution = find_DO_REPEAT_substitution (name);
	if (!substitution)
	  {
	    ds_concat (&output, start, cp - start);
	    continue;
	  }

	/* Force output buffer size, copy substitution. */
	ds_puts (&output, substitution);
      }
    }
  if (dot)
    ds_putc (&output, get_endcmd() );

  ds_destroy (&getl_buf);
  getl_buf = output;
}
