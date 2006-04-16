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
#include <libpspp/message.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <data/filename.h>
#include <language/line-buffer.h>
#include <language/lexer/lexer.h>
#include <data/settings.h>
#include <ui/terminal/read-line.h>
#include <libpspp/version.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

int err_error_count;
int err_warning_count;

int err_already_flagged;

int err_verbosity;

static char *command_name;

/* Fairly common public functions. */

/* Writes error message in CLASS, with title TITLE and text FORMAT,
   formatted with printf, to the standard places. */
void
tmsg (int class, const char *title, const char *format, ...)
{
  struct error e;
  va_list args;

  e.class = class;
  err_location (&e.where);
  e.title = title;

  va_start (args, format);
  err_vmsg (&e, format, args);
  va_end (args);
}

/* Writes error message in CLASS, with text FORMAT, formatted with
   printf, to the standard places. */
void
msg (int class, const char *format, ...)
{
  struct error e;
  va_list args;

  e.class = class;
  err_location (&e.where);
  e.title = NULL;

  va_start (args, format);
  err_vmsg (&e, format, args);
  va_end (args);
}

/* Checks whether we've had so many errors that it's time to quit
   processing this syntax file. */
void
err_check_count (void)
{
  if (get_errorbreak() && err_error_count)
    msg (MM, _("Terminating execution of syntax file due to error."));
  else if (err_error_count > get_mxerrs() )
    msg (MM, _("Errors (%d) exceeds limit (%d)."),
	 err_error_count, get_mxerrs());
  else if (err_error_count + err_warning_count > get_mxwarns() )
    msg (MM, _("Warnings (%d) exceed limit (%d)."),
	 err_error_count + err_warning_count, get_mxwarns() );
  else
    return;

  getl_abort_noninteractive ();
}

/* Some machines are broken.  Compensate. */
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

static void puts_stdout (const char *s);
static void dump_message (char *errbuf, unsigned indent,
			  void (*func) (const char *), unsigned width);

void
err_done (void) 
{
  lex_done();
  getl_uninitialize ();
  readln_uninitialize();
}

void
err_vmsg (const struct error *e, const char *format, va_list args)
{
  /* Class flags. */
  enum
    {
      ERR_IN_PROCEDURE = 01,	/* 1=Display name of current procedure. */
      ERR_WITH_FILE = 02,	/* 1=Display filename and line number. */
    };

  /* Describes one class of error. */
  struct error_class
    {
      int flags;		/* Zero or more of ERR_*. */
      int *count;		/* Counting category. */
      const char *banner;	/* Banner. */
    };

  static const struct error_class error_classes[ERR_CLASS_COUNT] =
    {
      {3, &err_error_count, N_("error")},	/* SE */
      {3, &err_warning_count, N_("warning")},	/* SW */
      {3, NULL, N_("note")},			/* SM */

      {2, &err_error_count, N_("error")},	/* DE */
      {2, &err_warning_count, N_("warning")},	/* DW */

      {0, &err_error_count, N_("error")},	/* ME */
      {0, &err_warning_count, N_("warning")},	/* MW */
      {0, NULL, N_("note")},			/* MM */
    };

  struct string msg;
  int class;

  /* Check verbosity level. */
  class = e->class;
  if (((class >> ERR_VERBOSITY_SHIFT) & ERR_VERBOSITY_MASK) > err_verbosity)
    return;
  class &= ERR_CLASS_MASK;
  
  assert (class >= 0 && class < ERR_CLASS_COUNT);
  assert (format != NULL);
  
  ds_init (&msg, 64);
  if (e->where.filename && (error_classes[class].flags & ERR_WITH_FILE))
    {
      ds_printf (&msg, "%s:", e->where.filename);
      if (e->where.line_number != -1)
	ds_printf (&msg, "%d:", e->where.line_number);
      ds_putc (&msg, ' ');
    }

  ds_printf (&msg, "%s: ", gettext (error_classes[class].banner));
  
  {
    int *count = error_classes[class].count;
    if (count)
      (*count)++;
  }
  
  if (command_name != NULL && (error_classes[class].flags & ERR_IN_PROCEDURE))
    ds_printf (&msg, "%s: ", command_name);

  if (e->title)
    ds_puts (&msg, e->title);

  ds_vprintf (&msg, format, args);

  /* FIXME: Check set_messages and set_errors to determine where to
     send errors and messages.

     Please note that this is not trivial.  We have to avoid an
     infinite loop in reporting errors that originate in the output
     section. */
  dump_message (ds_c_str (&msg), 8, puts_stdout, get_viewwidth());

  ds_destroy (&msg);
}

/* Private functions. */

#if 0
/* Write S followed by a newline to stderr. */
static void
puts_stderr (const char *s)
{
  fputs (s, stderr);
  fputc ('\n', stderr);
}
#endif

/* Write S followed by a newline to stdout. */
static void
puts_stdout (const char *s)
{
  puts (s);
}

/* Returns 1 if the line must be broken here */
static int
compulsory_break(int c)
{
  return ( c == '\n' );
}

/* Returns 1 if C is a `break character', that is, if it is a good
   place to break a message into lines. */
static inline int
char_is_break (int quote, int c)
{
  return ((quote && c == DIR_SEPARATOR)
	  || (!quote && (isspace (c) || c == '-' || c == '/'))); 
}

/* Returns 1 if C is a break character where the break should be made
   BEFORE the character. */
static inline int
break_before (int quote, int c)
{
  return !quote && isspace (c);
}

/* If C is a break character, returns 1 if the break should be made
   AFTER the character.  Does not return a meaningful result if C is
   not a break character. */
static inline int
break_after (int quote, int c)
{
  return !break_before (quote, c);
}

/* If you want very long words that occur at a bad break point to be
   broken into two lines even if they're shorter than a whole line by
   themselves, define as 2/3, or 4/5, or whatever fraction of a whole
   line you think is necessary in order to consider a word long enough
   to break into pieces.  Otherwise, define as 0.  See code to grok
   the details.  Do NOT parenthesize the expression!  */
#define BREAK_LONG_WORD 0
/* #define BREAK_LONG_WORD 2/3 */
/* #define BREAK_LONG_WORD 4/5 */

/* Divides MSG into lines of WIDTH width for the first line and WIDTH
   - INDENT width for each succeeding line.  Each line is dumped
   through FUNC, which may do with the string what it will. */
static void
dump_message (char *msg, unsigned indent, void (*func) (const char *),
	      unsigned width)
{
  char *cp;

  /* 1 when at a position inside double quotes ("). */
  int quote = 0;

  /* Buffer for a single line. */
  char *buf;

  /* If the message is short, just print the full thing. */
  if (strlen (msg) < width)
    {
      func (msg);
      return;
    }

  /* Make sure the indent isn't too big relative to the page width. */
  if (indent > width / 3)
    indent = width / 3;
  
  buf = local_alloc (width + 2);

  /* Advance WIDTH characters into MSG.
     If that's a valid breakpoint, keep it; otherwise, back up.
     Output the line. */
  for (cp = msg; (unsigned) (cp - msg) < width - 1 && 
	 ! compulsory_break(*cp); cp++)
    if (*cp == '"')
      quote ^= 1;

  if (break_after (quote, (unsigned char) *cp))
    {
      for (cp--; !char_is_break (quote, (unsigned char) *cp) && cp > msg; cp--)
	if (*cp == '"')
	  quote ^= 1;
      
      if (break_after (quote, (unsigned char) *cp))
	cp++;
    }

  if (cp <= msg + width * BREAK_LONG_WORD)
    for (; cp < msg + width - 1; cp++)
      if (*cp == '"')
	quote ^= 1;
  
  {
    int c = *cp;
    *cp = '\0';
    func (msg);
    *cp = c;
  }


  /* Repeat above procedure for remaining lines. */
  for (;;)
    {
      static int hard_break=0;

      int idx=0;
      char *cp2;

      /* Advance past whitespace. */
      if (! hard_break ) 
	while ( isspace ((unsigned char) *cp) )
	  cp++;
      else
	cp++;

      if (*cp == 0)
	  break; 


      /* Advance WIDTH - INDENT characters. */
      for (cp2 = cp; (unsigned) (cp2 - cp) < width - indent && 
	     *cp2 && !compulsory_break(*cp2);  cp2++)
	if (*cp2 == '"')
	  quote ^= 1;
      
      if ( compulsory_break(*cp2) )
	hard_break = 1;
      else
	hard_break = 0;


      /* Back up if this isn't a breakpoint. */
      {
	unsigned w = cp2 - cp;
	if (*cp2 && ! compulsory_break(*cp2) )
	for (cp2--; !char_is_break (quote, (unsigned char) *cp2) && 
	       cp2 > cp;
	       cp2--)
	  {

	    if (*cp2 == '"')
	      quote ^= 1;
	  }

	if (w == width - indent
	    && (unsigned) (cp2 - cp) <= (width - indent) * BREAK_LONG_WORD)
	  for (; (unsigned) (cp2 - cp) < width - indent && *cp2 ; cp2++)
	    if (*cp2 == '"')
	      quote ^= 1;
      }

      
      /* Write out the line. */

      memset (buf, ' ', indent);
      memcpy (&buf[indent], cp, cp2 - cp);

      buf[indent + idx + cp2 - cp] = '\0';
      func (buf);
      cp = cp2;
    }

  local_free (buf);
}

/* Sets COMMAND_NAME as the command name included in some kinds
   of error messages. */
void
err_set_command_name (const char *command_name_) 
{
  free (command_name);
  command_name = command_name_ ? xstrdup (command_name_) : NULL;
}

void 
request_bug_report_and_abort(const char *msg )
{
  fprintf(stderr,
	  "******************************************************************\n"
	  "You have discovered a bug in PSPP.\n\n"
	  "  Please report this, by sending "
	  "an email to " PACKAGE_BUGREPORT ",\n"
	  "explaining what you were doing when this happened, and including\n"
	  "a sample of your input file which caused it.\n");

  fprintf(stderr,
	  "Also, please copy the following lines into your bug report:\n\n"
	  "bare_version:        %s\n" 
	  "version:             %s\n"
	  "stat_version:        %s\n"
	  "host_system:         %s\n"
	  "build_system:        %s\n"
	  "default_config_path: %s\n"
	  "include_path:        %s\n"
	  "groff_font_path:     %s\n"
	  "locale_dir:          %s\n"
	  "compiler version:    %s\n"
	  ,

	  bare_version,         
	  version,
	  stat_version,
	  host_system,        
	  build_system,
	  default_config_path,
	  include_path, 
	  groff_font_path,
	  locale_dir,
#ifdef __VERSION__
	  __VERSION__
#else
	  "Unknown"
#endif
	  );     

  if ( msg )
    fprintf(stderr,"Diagnosis: %s\n",msg);

  fprintf(stderr,
    "******************************************************************\n");

  abort();
}

void 
err_assert_fail(const char *expr, const char *file, int line)
{
  char msg[256];
  snprintf(msg,256,"Assertion failed: %s:%d; (%s)",file,line,expr);
  request_bug_report_and_abort( msg );
}

