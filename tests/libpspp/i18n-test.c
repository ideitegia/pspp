/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/i18n.h"

#undef NDEBUG
#include <assert.h>

int
main (int argc, char *argv[])
{
  i18n_init ();

  if (argc > 1 && !strcmp (argv[1], "supports_encodings"))
    {
      int status = 0;
      int i;

      for (i = 2; i < argc; i++)
        if (!is_encoding_supported (argv[i]))
          {
            printf ("encoding \"%s\" is NOT supported\n", argv[i]);
            status = 77;
          }
      i18n_done ();
      exit (status);
    }
  if (argc == 5 && !strcmp (argv[1], "recode"))
    {
      const char *from = argv[2];
      const char *to = argv[3];
      const char *string = argv[4];
      char *result = recode_string (to, from, string, -1);
      puts (result);
      assert (strlen (result) == recode_string_len (to, from, string, -1));
      free (result);
    }
  else if (argc == 6 && !strcmp (argv[1], "concat"))
    {
      const char *head = argv[2];
      const char *tail = argv[3];
      const char *encoding = argv[4];
      int max_len = atoi (argv[5]);
      char *result;

      result = utf8_encoding_concat (head, tail, encoding, max_len);
      puts (result);

      assert (strlen (result)
              == utf8_encoding_concat_len (head, tail, encoding, max_len));

      if (tail[0] == '\0')
        {
          char *result2 = utf8_encoding_trunc (head, encoding, max_len);
          assert (!strcmp (result, result2));
          assert (strlen (result2)
                  == utf8_encoding_trunc_len (head, encoding, max_len));
          free (result2);
        }

      free (result);
    }
  else
    {
      fprintf (stderr, "\
usage: %s supports_encodings ENCODING...\n\
where ENCODING is the name of an encoding.\n\
Exits with status 0 if all the encodings are supported, 77 otherwise.\n\
\n\
usage: %s recode FROM TO STRING\n\
where FROM is the source encoding,\n\
      TO is the target encoding,\n\
      and STRING is the text to recode.\n\
\n\
usage: %s concat HEAD TAIL ENCODING MAX_LEN\n\
where HEAD is the first string to concatenate\n\
      TAIL is the second string to concatenate\n\
      ENCODING is the encoding in which to measure the result's length\n\
      MAX_LEN is the maximum length of the result in ENCODING.\n",
               argv[0], argv[0], argv[0]);
      return EXIT_FAILURE;
    }

  i18n_done ();

  return 0;
}
