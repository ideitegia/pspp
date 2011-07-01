/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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

/* Functions for temporary files that honor $TMPDIR. */

#include <config.h>

#include "libpspp/temp-file.h"

#include <stdlib.h>

#include "gl/clean-temp.h"
#include "gl/xvasprintf.h"

/* Creates and returns a new temporary file that will be removed automatically
   when the process exits.  The file is opened in mode "wb+".  To close the
   file before the process exits, use close_temp_file() to ensure that it gets
   deleted early.

   Returns NULL if creating the temporary file fails.

   This is similar to tmpfile(), except:

     - It honors the $TMPDIR environment variable.

     - The file will not be automatically deleted upon close.  You have to call
       close_temp_file() if you want it to be deleted before the process exits.
*/
FILE *
create_temp_file (void)
{
  static int idx = 0;
  static struct temp_dir *temp_dir;
  char *file_name;
  FILE *stream;

  if (temp_dir == NULL)
    {
      temp_dir = create_temp_dir ("pspp", NULL, true);
      if (temp_dir == NULL)
        return NULL;
    }

  file_name = xasprintf ("%s/%d", temp_dir->dir_name, idx++);
  stream = fopen_temp (file_name, "wb+");
  if (stream != NULL)
    setvbuf (stream, NULL, _IOFBF, 65536);
  free (file_name);

  return stream;
}

/* Closes and removes a temporary file created by create_temp_file(). */
void
close_temp_file (FILE *file)
{
  if (file != NULL)
    fclose_temp (file);
}
