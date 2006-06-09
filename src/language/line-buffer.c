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

#include <language/line-buffer.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include <data/file-name.h>
#include <data/settings.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/str.h>
#include <libpspp/verbose-msg.h>
#include <libpspp/version.h>
#include <output/table.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Source file. */
struct getl_source
  {
    struct getl_source *included_from;	/* File that this is nested inside. */
    struct getl_source *includes;	/* File nested inside this file. */
    struct getl_source *next;		/* Next file in list. */

    /* Current location. */
    char *fn;				/* File name. */
    int ln;				/* Line number. */

    enum getl_source_type
      {
        SYNTAX_FILE,
        FILTER,
        FUNCTION,
        INTERACTIVE
      }
    type;

    union 
      {
        /* SYNTAX_FILE. */
        FILE *syntax_file;

        /* FILTER. */
        struct 
          {
            void (*filter) (struct string *line, void *aux);
            void (*close) (void *aux);
            void *aux;
          }
        filter;

        /* FUNCTION. */
        struct 
          {
            bool (*read) (struct string *line, char **fn, int *ln, void *aux);
            void (*close) (void *aux);
            void *aux;
          }
        function;

        /* INTERACTIVE. */
        bool (*interactive) (struct string *line, const char *prompt);
      }
    u;

  };

/* List of source files. */
static struct getl_source *cur_source;
static struct getl_source *last_source;

static struct string getl_include_path;

struct string getl_buf;

static void close_source (void);

static void init_prompts (void);
static void uninit_prompts (void);
static const char *get_prompt (void);

/* Initialize getl. */
void
getl_initialize (void)
{
  ds_init_cstr (&getl_include_path,
                fn_getenv_default ("STAT_INCLUDE_PATH", include_path));
  ds_init_empty (&getl_buf);
  init_prompts ();
}

/* Delete everything from the include path. */
void
getl_clear_include_path (void)
{
  ds_clear (&getl_include_path);
}

/* Add to the include path. */
void
getl_add_include_dir (const char *path)
{
  if (ds_length (&getl_include_path))
    ds_put_char (&getl_include_path, ':');

  ds_put_cstr (&getl_include_path, path);
}

/* Appends source S to the list of source files. */
static void
append_source (struct getl_source *s) 
{
  s->included_from = s->includes = s->next = NULL;
  if (last_source == NULL)
    cur_source = s;
  else
    last_source->next = s;
  last_source = s;
}

/* Nests source S within the current source file. */
static void
include_source (struct getl_source *s) 
{
  if (last_source == NULL)
    append_source (s);
  else 
    {
      s->included_from = cur_source;
      s->includes = s->next = NULL;
      s->next = NULL;
      cur_source->includes = s;
      cur_source = s;
    }
}

/* Creates a source of the given TYPE.
   Type-specific data must be initialized by the caller. */
static struct getl_source *
create_source (enum getl_source_type type) 
{
  struct getl_source *s = xmalloc (sizeof *s);
  s->fn = NULL;
  s->ln = 0;
  s->type = type;
  return s;
}

/* Creates a syntax file source with file name FN. */
static struct getl_source *
create_syntax_file_source (const char *fn) 
{
  struct getl_source *s = create_source (SYNTAX_FILE);
  s->fn = xstrdup (fn);
  s->u.syntax_file = NULL;
  return s;
}

/* Creates a filter source with the given FILTER and CLOSE
   functions that receive auxiliary data AUX. */
static struct getl_source *
create_filter_source (void (*filter) (struct string *, void *aux),
                      void (*close) (void *aux),
                      void *aux)
{
  struct getl_source *s = create_source (FILTER);
  s->u.filter.filter = filter;
  s->u.filter.close = close;
  s->u.filter.aux = aux;
  return s;
}

/* Creates a function source with the given READ and CLOSE
   functions that receive auxiliary data AUX. */
static struct getl_source *
create_function_source (bool (*read) (struct string *line,
                                      char **fn, int *ln, void *aux),
                        void (*close) (void *aux),
                        void *aux)
{
  struct getl_source *s = create_source (FUNCTION);
  s->u.function.read = read;
  s->u.function.close = close;
  s->u.function.aux = aux;
  return s;
}

/* Creates an interactive source with the given FUNCTION. */
static struct getl_source *
create_interactive_source (bool (*function) (struct string *line,
                                             const char *prompt)) 
{
  struct getl_source *s = xmalloc (sizeof *s);
  s->fn = NULL;
  s->ln = 0;
  s->type = INTERACTIVE;
  s->u.interactive = function;
  return s;
}

/* Adds FN to the tail end of the list of source files to
   execute. */
void
getl_append_syntax_file (const char *fn)
{
  append_source (create_syntax_file_source (fn));
}

/* Inserts the given file with name FN into the current file
   after the current line. */
void
getl_include_syntax_file (const char *fn)
{
  if (cur_source != NULL) 
    {
      char *found_fn = fn_search_path (fn, ds_cstr (&getl_include_path),
                                       fn_dir_name (cur_source->fn));
      if (found_fn != NULL) 
        {
          include_source (create_syntax_file_source (found_fn));
          free (found_fn); 
        }
      else
        msg (SE, _("Can't find `%s' in include file search path."), fn);
    }
  else 
    getl_append_syntax_file (fn); 
}

/* Inserts the given filter into the current file after the
   current line.  Each line read while the filter is in place
   will be passed through FILTER, which may modify it as
   necessary.  When the filter is closed, CLOSE will be called.
   AUX will be passed to both functions.

   The filter cannot itself output any new lines, and it will be
   closed as soon as any line would be read from it.  This means
   that, for a filter to be useful, another source must be nested
   inside it with, e.g., getl_include_syntax_file(). */
void
getl_include_filter (void (*filter) (struct string *, void *aux),
                     void (*close) (void *aux),
                     void *aux) 
{
  include_source (create_filter_source (filter, close, aux));
}

/* Inserts the given functional source into the current file
   after the current line.  Lines are read by calling READ, which
   should write the next line in LINE, store the file name and
   line number of the line in *FN and *LN, and return true.  The
   string stored in *FN will not be freed by getl.  When no lines
   are left, READ should return false.

   When the source is closed, CLOSE will be called.

   AUX will be passed to both READ and CLOSE. */
void
getl_include_function (bool (*read) (struct string *line,
                                     char **fn, int *ln, void *aux),
                       void (*close) (void *aux),
                       void *aux) 
{
  include_source (create_function_source (read, close, aux));
}

/* Adds an interactive source to the end of the list of sources.
   FUNCTION will be called to obtain a line.  It should store the
   line in LINE.  PROMPT is the prompt to be displayed to the
   user.  FUNCTION should return true when a line has been
   obtained or false at end of file. */
void
getl_append_interactive (bool (*function) (struct string *line,
                                           const char *prompt)) 
{
  append_source (create_interactive_source (function));
}

/* Closes all sources until an interactive source is
   encountered. */
void
getl_abort_noninteractive (void) 
{
  while (cur_source != NULL && cur_source->type != INTERACTIVE)
    close_source ();
}

/* Returns true if the current source is interactive,
   false otherwise. */
bool
getl_is_interactive (void) 
{
  return cur_source != NULL && cur_source->type == INTERACTIVE;
}

/* Closes the current file, whether it be a main file or included
   file, then moves cur_source to the next file in the chain. */
static void
close_source (void)
{
  struct getl_source *s;

  s = cur_source;
  switch (s->type) 
    {
    case SYNTAX_FILE:
      if (s->u.syntax_file && EOF == fn_close (s->fn, s->u.syntax_file))
        msg (MW, _("Closing `%s': %s."), s->fn, strerror (errno));
      free (s->fn);
      break;

    case FILTER:
      if (s->u.filter.close != NULL)
        s->u.filter.close (s->u.filter.aux);
      break;

    case FUNCTION:
      if (s->u.function.close != NULL)
        s->u.function.close (s->u.function.aux);
      break;

    case INTERACTIVE:
      break;
    }

  if (s->included_from != NULL)
    {
      cur_source = s->included_from;
      cur_source->includes = NULL;
    }
  else
    {
      cur_source = s->next;
      if (cur_source == NULL)
	last_source = NULL;
    }

  free (s);
}

/* Puts the current file and line number in *FN and *LN, respectively,
   or NULL and -1 if none. */
void
getl_location (const char **fn, int *ln)
{
  if (fn != NULL)
    *fn = cur_source ? cur_source->fn : "";
  if (ln != NULL)
    *ln = cur_source ? cur_source->ln : -1;
}

/* File locator stack. */
static const struct msg_locator **file_loc;
static int nfile_loc, mfile_loc;

/* Close getl. */
void
getl_uninitialize (void)
{
  while (cur_source != NULL)
    close_source ();
  ds_destroy (&getl_buf);
  ds_destroy (&getl_include_path);
  free(file_loc);
  file_loc = NULL;
  nfile_loc = mfile_loc = 0;
  uninit_prompts ();
}


/* File locator stack functions. */

/* Pushes F onto the stack of file locations. */
void
msg_push_msg_locator (const struct msg_locator *loc)
{
  if (nfile_loc >= mfile_loc)
    {
      if (mfile_loc == 0)
	mfile_loc = 8;
      else
	mfile_loc *= 2;

      file_loc = xnrealloc (file_loc, mfile_loc, sizeof *file_loc);
    }

  file_loc[nfile_loc++] = loc;
}

/* Pops F off the stack of file locations.
   Argument F is only used for verification that that is actually the
   item on top of the stack. */
void
msg_pop_msg_locator (const struct msg_locator *loc)
{
  assert (nfile_loc >= 0 && file_loc[nfile_loc - 1] == loc);
  nfile_loc--;
}

/* Puts the current file and line number in F, or NULL and -1 if
   none. */
void
msg_location (struct msg_locator *loc)
{
  if (nfile_loc)
    *loc = *file_loc[nfile_loc - 1];
  else
    getl_location (&loc->file_name, &loc->line_number);
}

/* Reads a line from syntax file source S into LINE.
   Returns true if successful, false at end of file. */
static bool
read_syntax_file (struct string *line, struct getl_source *s)
{
  /* Open file, if not yet opened. */
  if (s->u.syntax_file == NULL)
    {
      verbose_msg (1, _("opening \"%s\" as syntax file"), s->fn);
      s->u.syntax_file = fn_open (s->fn, "r");

      if (s->u.syntax_file == NULL)
        {
          msg (ME, _("Opening `%s': %s."), s->fn, strerror (errno));
          return false;
        }
    }

  /* Read line from file and remove new-line.
     Skip initial "#! /usr/bin/pspp" line. */
  do 
    {
      s->ln++;
      if (!ds_read_line (line, s->u.syntax_file))
        {
          if (ferror (s->u.syntax_file))
            msg (ME, _("Reading `%s': %s."), s->fn, strerror (errno));
          return false;
        }
      ds_chomp (line, '\n');
    }
  while (s->ln == 1 && !memcmp (ds_cstr (line), "#!", 2));

  /* Echo to listing file, if configured to do so. */
  if (get_echo ())
    tab_output_text (TAB_LEFT | TAB_FIX, ds_cstr (line));

  return true;
}

/* Reads a line from source S into LINE.
   Returns true if successful, false at end of file. */
static bool
read_line_from_source (struct string *line, struct getl_source *s)
{
  ds_clear (line);
  switch (s->type) 
    {
    case SYNTAX_FILE:
      return read_syntax_file (line, s);
    case FILTER:
      return false;
    case FUNCTION:
      return s->u.function.read (line, &s->fn, &s->ln, s->u.function.aux);
    case INTERACTIVE:
      return s->u.interactive (line, get_prompt ());
    }

  abort ();
}

/* Reads a single line into LINE.
   Returns true when a line has been read, false at end of input.
   If INTERACTIVE is non-null, then when true is returned
   *INTERACTIVE will be set to true if the line was obtained
   interactively, false otherwise. */
static bool
do_read_line (struct string *line, bool *interactive)
{
  while (cur_source != NULL)
    {
      struct getl_source *s = cur_source;
      if (read_line_from_source (line, s))
        {
          if (interactive != NULL)
            *interactive = s->type == INTERACTIVE;

          while ((s = s->included_from) != NULL)
            if (s->type == FILTER)
              s->u.filter.filter (line, s->u.filter.aux);

          return true;
        }
      close_source ();
    }

  return false;
}

/* Reads a single line into getl_buf.
   Returns true when a line has been read, false at end of input.
   If INTERACTIVE is non-null, then when true is returned
   *INTERACTIVE will be set to true if the line was obtained
   interactively, false otherwise. */
bool
getl_read_line (bool *interactive)
{
  return do_read_line (&getl_buf, interactive);
}

/* Current prompts in each style. */
static char *prompts[GETL_PROMPT_CNT];

/* Current prompting style. */
static enum getl_prompt_style current_style;

/* Initializes prompts. */
static void
init_prompts (void) 
{
  prompts[GETL_PROMPT_FIRST] = xstrdup ("PSPP> ");
  prompts[GETL_PROMPT_LATER] = xstrdup ("    > ");
  prompts[GETL_PROMPT_DATA] = xstrdup ("data> ");
  current_style = GETL_PROMPT_FIRST;
}

/* Frees prompts. */
static void
uninit_prompts (void) 
{
  int i;

  for (i = 0; i < GETL_PROMPT_CNT; i++) 
    {
      free (prompts[i]);
      prompts[i] = NULL;
    }
}

/* Gets the command prompt for the given STYLE. */
const char * 
getl_get_prompt (enum getl_prompt_style style)
{
  assert (style < GETL_PROMPT_CNT);
  return prompts[style];
}

/* Sets the given STYLE's prompt to STRING. */
void
getl_set_prompt (enum getl_prompt_style style, const char *string)
{
  assert (style < GETL_PROMPT_CNT);
  free (prompts[style]);
  prompts[style] = xstrdup (string);
}

/* Sets STYLE as the current prompt style. */
void
getl_set_prompt_style (enum getl_prompt_style style) 
{
  assert (style < GETL_PROMPT_CNT);
  current_style = style;
}

/* Returns the current prompt. */
static const char *
get_prompt (void) 
{
  return prompts[current_style];
}
