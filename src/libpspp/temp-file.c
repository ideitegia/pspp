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
#include "libpspp/hmapx.h"
#include "libpspp/hash-functions.h"

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

*/

static void cleanup (void);

static struct temp_dir *temp_dir;
struct hmapx map;

static void
setup (void)
{
  hmapx_init (&map);
  temp_dir = create_temp_dir ("pspp", NULL, true);
}

static void
initialise (void)
{
  if (temp_dir == NULL)
    {
      setup ();
      if (temp_dir == NULL)
        return ;
      atexit (cleanup);
    }
}


const char *
temp_dir_name (void)
{
  initialise ();

  if (temp_dir)
    return temp_dir->dir_name;

  return NULL;
}

static void
cleanup (void)
{
  struct hmapx_node *node;
  char *fn;

  cleanup_temp_dir (temp_dir);

  HMAPX_FOR_EACH (fn, node, &map)
    {
      free (fn);
    }

  hmapx_destroy (&map);
}

FILE *
create_temp_file (void)
{
  static int idx = 0;
  char *file_name;
  FILE *stream;

  initialise ();
  if (temp_dir == NULL)
    return NULL;

  file_name = xasprintf ("%s/%d", temp_dir->dir_name, idx++);
  register_temp_file (temp_dir, file_name);
  stream = fopen_temp (file_name, "wb+");
  if (stream == NULL)
    unregister_temp_file (temp_dir, file_name);
  else
    setvbuf (stream, NULL, _IOFBF, 65536);

  hmapx_insert (&map, file_name, hash_pointer (stream, 0));

  return stream;
}

/* Closes and removes a temporary file created by create_temp_file(). */
void
close_temp_file (FILE *file)
{
  if (file != NULL)
    {
      struct hmapx_node *node = hmapx_first_with_hash (&map, hash_pointer (file, 0));
      char *fn = node->data;
      fclose_temp (file);
      cleanup_temp_file (temp_dir, fn); 
      hmapx_delete (&map, node);
      free (fn);
    }
}
