/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

#include <output/journal.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <data/file-name.h>
#include <libpspp/str.h>

#include "fwriteerror.h"
#include "error.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Journaling enabled? */
static bool journal_enabled = false;

/* Name of the journal file. */
static char *journal_file_name = NULL;

/* Journal file. */
static FILE *journal_file = NULL;

/* Enables journaling. */
void
journal_enable (void)
{
  journal_enabled = true;
}

/* Disables journaling. */
void
journal_disable (void)
{
  journal_enabled = false;
  if (journal_file != NULL)
    fflush (journal_file);
}

/* Sets the name of the journal file to FILE_NAME. */
void
journal_set_file_name (const char *file_name)
{
  assert (file_name != NULL);

  if (journal_file != NULL)
    {
      if (fwriteerror (journal_file))
        error (0, errno, _("error writing \"%s\""), journal_file_name);
    }

  free (journal_file_name);
  journal_file_name = xstrdup (file_name);
}

/* Writes LINE to the journal file (if journaling is enabled).
   If PREFIX is non-null, the line will be prefixed by "> ". */
void
journal_write (bool prefix, const char *line)
{
  if (!journal_enabled)
    return;

  if (journal_file == NULL)
    {
      if (journal_file_name == NULL)
	{
	  const char *output_path = default_output_path ();
	  journal_file_name = xasprintf ("%s%s", output_path, "pspp.jnl");
	}
      journal_file = fopen (journal_file_name, "w");
      if (journal_file == NULL)
        {
          error (0, errno, _("error creating \"%s\""), journal_file_name);
          journal_enabled = false;
          return;
        }
    }

  if (prefix)
    fputs ("> ", journal_file);
  fputs (line, journal_file);
  if (strchr (line, '\n') == NULL)
    putc ('\n', journal_file);
  fflush (journal_file);
}
