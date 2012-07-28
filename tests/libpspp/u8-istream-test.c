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

#include "libpspp/u8-istream.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libpspp/i18n.h"

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
          "  read FILE ENCODING [OUTBUF]\n"
          "    read FILE encoded in ENCODING (with output buffer size\n"
          "    OUTBUF) and print it on stdout in UTF-8\n",
          program_name);
  exit (0);
}

static void
cmd_read (int argc, char *argv[])
{
  struct u8_istream *is;
  const char *encoding;
  const char *filename;
  int outbufsize;
  char *buffer;

  if (argc < 4 || argc > 5)
    error (1, 0, "bad syntax for `%s' command; use `%s help' for help",
           argv[1], program_name);

  outbufsize = argc > 4 ? atoi (argv[4]) : 4096;
  buffer = xmalloc (outbufsize);

  filename = argv[2];
  encoding = *argv[3] ? argv[3] : NULL;

  is = (!strcmp(filename, "-")
        ? u8_istream_for_fd (encoding, STDIN_FILENO)
        : u8_istream_for_file (encoding, filename, O_RDONLY));
  if (is == NULL)
    error (1, errno, "u8_istream_open failed");

  if (u8_istream_is_auto (is))
    printf ("Auto mode\n");
  else if (u8_istream_is_utf8 (is))
    printf ("UTF-8 mode\n");

  for (;;)
    {
      ssize_t n;

      n = u8_istream_read (is, buffer, outbufsize);
      if (n > 0)
        fwrite (buffer, 1, n, stdout);
      else if (n < 0)
        error (1, errno, "u8_istream_read failed");
      else
        break;
    }
  free (buffer);

  if (u8_istream_is_auto (is))
    printf ("Auto mode\n");
  else if (u8_istream_is_utf8 (is))
    printf ("UTF-8 mode\n");

  if (!strcmp(filename, "-"))
    u8_istream_free (is);
  else
    {
      if (u8_istream_close (is) != 0)
        error (1, errno, "u8_istream_close failed");
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
    printf ("%d\n", U8_ISTREAM_BUFFER_SIZE);
  else if (!strcmp(argv[1], "read"))
    cmd_read (argc, argv);
  else
    error (1, 0, "unknown command `%s'; use `%s help' for help",
           argv[1], program_name);

  return 0;
}
