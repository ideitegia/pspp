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

#include <config.h>

#include "libpspp/line-reader.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libpspp/i18n.h"
#include "libpspp/str.h"

#include "gl/error.h"
#include "gl/progname.h"
#include "gl/xalloc.h"

static void
usage (void)
{
  printf ("usage: %s COMMAND [ARG]...\n"
          "The available commands are:\n"
          "  help\n"
          "    print this usage message\n"
          "  buffer-size\n"
          "    print the buffer size, in bytes, on stdout\n"
          "  read FILE ENCODING\n"
          "    read FILE encoded in ENCODING and print it in UTF-8\n",
          program_name);
  exit (0);
}

static void
cmd_read (int argc, char *argv[])
{
  struct line_reader *r;
  const char *filename;
  struct string line;
  char *encoding;

  if (argc != 4)
    error (1, 0, "bad syntax for `%s' command; use `%s help' for help",
           argv[1], program_name);

  filename = argv[2];

  r = (!strcmp(filename, "-")
       ? line_reader_for_fd (argv[3], STDIN_FILENO)
       : line_reader_for_file (argv[3], filename, O_RDONLY));
  if (r == NULL)
    error (1, errno, "line_reader_open failed");

  encoding = xstrdup (line_reader_get_encoding (r));
  printf ("encoded in %s", encoding);
  if (line_reader_is_auto (r))
    printf (" (auto)");
  printf ("\n");

  ds_init_empty (&line);
  while (line_reader_read (r, &line, SIZE_MAX))
    {
      const char *new_encoding;
      char *utf8_line;

      new_encoding = line_reader_get_encoding (r);
      if (strcmp (encoding, new_encoding))
        {
          free (encoding);
          encoding = xstrdup (new_encoding);

          printf ("encoded in %s", encoding);
          if (line_reader_is_auto (r))
            printf (" (auto)");
          printf ("\n");
        }

      utf8_line = recode_string ("UTF-8", encoding,
                                 ds_data (&line), ds_length (&line));
      printf ("\"%s\"\n", utf8_line);
      free (utf8_line);

      ds_clear (&line);
    }

  if (!strcmp(filename, "-"))
    line_reader_free (r);
  else
    {
      if (line_reader_close (r) != 0)
        error (1, errno, "line_reader_close failed");
    }
}

int
main (int argc, char *argv[])
{
  set_program_name (argv[0]);
  i18n_init ();

  if (argc < 2)
    error (1, 0, "missing command name; use `%s help' for help", program_name);
  else if (!strcmp(argv[1], "help") || !strcmp(argv[1], "--help"))
    usage ();
  else if (!strcmp(argv[1], "buffer-size"))
    printf ("%d\n", LINE_READER_BUFFER_SIZE);
  else if (!strcmp(argv[1], "read"))
    cmd_read (argc, argv);
  else
    error (1, 0, "unknown command `%s'; use `%s help' for help",
           argv[1], program_name);

  return 0;
}
