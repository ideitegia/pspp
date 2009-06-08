/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009 Free Software Foundation, Inc.

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

#include "syntax-file.h"

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <data/file-name.h>
#include <data/settings.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/str.h>
#include <libpspp/verbose-msg.h>
#include <libpspp/version.h>
#include <output/table.h>

#include <libpspp/ll.h>

#include "prompt.h"

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include <libpspp/getl.h>


struct syntax_file_source
  {
    struct getl_interface parent ;

    FILE *syntax_file;

    /* Current location. */
    char *fn;				/* File name. */
    int ln;				/* Line number. */
  };

static const char *
name (const struct getl_interface *s)
{
  const struct syntax_file_source *sfs = (const struct syntax_file_source *) s;
  return sfs->fn;
}

static int
line_number (const struct getl_interface *s)
{
  const struct syntax_file_source *sfs = (const struct syntax_file_source *) s;
  return sfs->ln;
}


/* Reads a line from syntax file source S into LINE.
   Returns true if successful, false at end of file. */
static bool
read_syntax_file (struct getl_interface *s,
                  struct string *line)
{
  struct syntax_file_source *sfs = (struct syntax_file_source *) s;

  /* Open file, if not yet opened. */
  if (sfs->syntax_file == NULL)
    {
      verbose_msg (1, _("opening \"%s\" as syntax file"), sfs->fn);
      sfs->syntax_file = fn_open (sfs->fn, "r");

      if (sfs->syntax_file == NULL)
        {
          msg (ME, _("Opening `%s': %s."), sfs->fn, strerror (errno));
          return false;
        }
    }

  /* Read line from file and remove new-line.
     Skip initial "#! /usr/bin/pspp" line. */
  do
    {
      sfs->ln++;
      ds_clear (line);
      if (!ds_read_line (line, sfs->syntax_file, SIZE_MAX))
        {
          if (ferror (sfs->syntax_file))
            msg (ME, _("Reading `%s': %s."), sfs->fn, strerror (errno));
          return false;
        }
      ds_chomp (line, '\n');
    }
  while (sfs->ln == 1 && !memcmp (ds_cstr (line), "#!", 2));

  /* Echo to listing file, if configured to do so. */
  if (settings_get_echo ())
    tab_output_text (TAB_LEFT | TAB_FIX, ds_cstr (line));

  return true;
}

static void
syntax_close (struct getl_interface *s)
{
  struct syntax_file_source *sfs = (struct syntax_file_source *) s;

  if (sfs->syntax_file && EOF == fn_close (sfs->fn, sfs->syntax_file))
    msg (MW, _("Closing `%s': %s."), sfs->fn, strerror (errno));
  free (sfs->fn);
  free (sfs);
}

static bool
always_false (const struct getl_interface *s UNUSED)
{
  return false;
}


/* Creates a syntax file source with file name FN. */
struct getl_interface *
create_syntax_file_source (const char *fn)
{
  struct syntax_file_source *ss = xzalloc (sizeof (*ss));

  ss->fn = xstrdup (fn);

  ss->parent.interactive = always_false;
  ss->parent.read = read_syntax_file ;
  ss->parent.filter = NULL;
  ss->parent.close = syntax_close ;
  ss->parent.name = name ;
  ss->parent.location = line_number;

  return (struct getl_interface *) ss;
}

