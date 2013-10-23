/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2013 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/float-format.h"
#include "libpspp/integer-format.h"

#include "gl/c-ctype.h"
#include "gl/error.h"
#include "gl/md5.h"
#include "gl/intprops.h"
#include "gl/progname.h"
#include "gl/xalloc.h"

struct buffer
  {
    uint8_t *data;
    size_t size;
    size_t allocated;
  };

static void buffer_put (struct buffer *, const void *, size_t);
static void *buffer_put_uninit (struct buffer *, size_t);

enum token_type
  {
    T_EOF,
    T_INTEGER,
    T_FLOAT,
    T_STRING,
    T_SEMICOLON,
    T_ASTERISK,
    T_LPAREN,
    T_RPAREN,
    T_I8,
    T_I64,
    T_S,
    T_COUNT,
    T_HEX
  };

static enum token_type token;
static unsigned long long int tok_integer;
static double tok_float;
static char *tok_string;
static size_t tok_strlen, tok_allocated;

/* --be, --le: Integer and floating-point formats. */
static enum float_format float_format = FLOAT_IEEE_DOUBLE_BE;
static enum integer_format integer_format = INTEGER_MSB_FIRST;

/* Input file and current position. */
static FILE *input;
static const char *input_file_name;
static int line_number;

static void PRINTF_FORMAT (1, 2)
fatal (const char *message, ...)
{
  va_list args;

  fprintf (stderr, "%s:%d: ", input_file_name, line_number);
  va_start (args, message);
  vfprintf (stderr, message, args);
  va_end (args);
  putc ('\n', stderr);

  exit (EXIT_FAILURE);
}

static void
add_char__ (int c)
{
  if (tok_strlen >= tok_allocated)
    tok_string = x2realloc (tok_string, &tok_allocated);

  tok_string[tok_strlen] = c;
}

static void
add_char (int c)
{
  add_char__ (c);
  tok_strlen++;
}

static void
get_token (void)
{
  int c;

  do
    {
      c = getc (input);
      if (c == '#')
        {
          while ((c = getc (input)) != '\n' && c != EOF)
            continue;
        }
      if (c == '\n')
        line_number++;
    }
  while (isspace (c) || c == '<' || c == '>');

  tok_strlen = 0;
  if (c == EOF)
    {
      if (token == T_EOF)
        fatal ("unexpected end of input");
      token = T_EOF;
    }
  else if (isdigit (c) || c == '-')
    {
      char *tail;

      do
        {
          add_char (c);
          c = getc (input);
        }
      while (isdigit (c) || isalpha (c) || c == '.');
      add_char__ ('\0');
      ungetc (c, input);

      errno = 0;
      if (strchr (tok_string, '.') == NULL)
        {
          token = T_INTEGER;
          tok_integer = strtoull (tok_string, &tail, 0);
        }
      else
        {
          token = T_FLOAT;
          tok_float = strtod (tok_string, &tail);
        }
      if (errno || *tail)
        fatal ("invalid numeric syntax");
    }
  else if (c == '"')
    {
      token = T_STRING;
      while ((c = getc (input)) != '"')
        {
          if (c == '\n')
            fatal ("new-line inside string");
          add_char (c);
        }
      add_char__ ('\0');
    }
  else if (c == ';')
    token = T_SEMICOLON;
  else if (c == '*')
    token = T_ASTERISK;
  else if (c == '(')
    token = T_LPAREN;
  else if (c == ')')
    token = T_RPAREN;
  else if (isalpha (c))
    {
      do
        {
          add_char (c);
          c = getc (input);
        }
      while (isdigit (c) || isalpha (c) || c == '.');
      add_char ('\0');
      ungetc (c, input);

      if (!strcmp (tok_string, "i8"))
        token = T_I8;
      else if (!strcmp (tok_string, "i64"))
        token = T_I64;
      else if (tok_string[0] == 's')
        {
          token = T_S;
          tok_integer = atoi (tok_string + 1);
        }
      else if (!strcmp (tok_string, "SYSMIS"))
        {
          token = T_FLOAT;
          tok_float = -DBL_MAX;
        }
      else if (!strcmp (tok_string, "LOWEST"))
        {
          token = T_FLOAT;
          tok_float = float_get_lowest ();
        }
      else if (!strcmp (tok_string, "HIGHEST"))
        {
          token = T_FLOAT;
          tok_float = DBL_MAX;
        }
      else if (!strcmp (tok_string, "ENDIAN"))
        {
          token = T_INTEGER;
          tok_integer = integer_format == INTEGER_MSB_FIRST ? 1 : 2;
        }
      else if (!strcmp (tok_string, "COUNT"))
        token = T_COUNT;
      else if (!strcmp (tok_string, "hex"))
        token = T_HEX;
      else
        fatal ("invalid token `%s'", tok_string);
    }
  else
    fatal ("invalid input byte `%c'", c);
}

static void
buffer_put (struct buffer *buffer, const void *data, size_t n)
{
  memcpy (buffer_put_uninit (buffer, n), data, n);
}

static void *
buffer_put_uninit (struct buffer *buffer, size_t n)
{
  buffer->size += n;
  if (buffer->size > buffer->allocated)
    {
      buffer->allocated = buffer->size * 2;
      buffer->data = xrealloc (buffer->data, buffer->allocated);
    }
  return &buffer->data[buffer->size - n];
}

/* Returns the integer value of hex digit C. */
static int
hexit_value (int c)
{
  const char s[] = "0123456789abcdef";
  const char *cp = strchr (s, c_tolower ((unsigned char) c));

  assert (cp != NULL);
  return cp - s;
}

static void
usage (void)
{
  printf ("\
%s, SAv Construction Kit\n\
usage: %s [OPTIONS] INPUT\n\
\nOptions:\n\
  --be     big-endian output format (default)\n\
  --le     little-endian output format\n\
  --help   print this help message and exit\n\
\n\
The input is a sequence of data items, each followed by a semicolon.\n\
Each data item is converted to the output format and written on\n\
stdout.  A data item is one of the following\n\
\n\
  - An integer in decimal, in hexadecimal prefixed by 0x, or in octal\n\
    prefixed by 0.  Output as a 32-bit binary integer.\n\
\n\
  - A floating-point number.  Output in 64-bit IEEE 754 format.\n\
\n\
  - A string enclosed in double quotes.  Output literally.  There is\n\
    no syntax for \"escapes\".  Strings may not contain new-lines.\n\
\n\
  - A literal of the form s<number> followed by a quoted string as\n\
    above.  Output as the string's contents followed by enough spaces\n\
    to fill up <number> bytes.  For example, s8 \"foo\" is output as\n\
    the \"foo\" followed by 5 spaces.\n\
\n\
  - The literal \"i8\" followed by an integer.  Output as a single\n\
    byte with the specified value.\n\
\n\
  - The literal \"i64\" followed by an integer.  Output as a 64-bit\n\
    binary integer.\n\
\n\
  - One of the literals SYSMIS, LOWEST, or HIGHEST.  Output as a\n\
    64-bit IEEE 754 float of the appropriate PSPP value.\n\
\n\
  - The literal ENDIAN.  Output as a 32-bit binary integer, either\n\
    with value 1 if --be is in effect or 2 if --le is in effect.\n\
\n\
  - A pair of parentheses enclosing a sequence of data items, each\n\
    followed by a semicolon (the last semicolon is optional).\n\
    Output as the enclosed data items in sequence.\n\
\n\
  - The literal COUNT followed by a sequence of parenthesized data\n\
    items, as above.  Output as a 32-bit binary integer whose value\n\
    is the number of bytes enclosed within the parentheses, followed\n\
    by the enclosed data items themselves.\n\
\n\
optionally followed by an asterisk and a positive integer, which\n\
specifies a repeat count for the data item.\n\
\n\
The md5sum of the data written to stdout is written to stderr as\n\
16 hexadecimal digits followed by a new-line.\n",
          program_name, program_name);
  exit (EXIT_SUCCESS);
}

static const char *
parse_options (int argc, char **argv)
{
  for (;;)
    {
      enum {
        OPT_BE = UCHAR_MAX + 1,
        OPT_LE,
        OPT_HELP
      };
      static const struct option options[] =
        {
          {"be", no_argument, NULL, OPT_BE},
          {"le", no_argument, NULL, OPT_LE},
          {"help", no_argument, NULL, OPT_HELP},
          {NULL, 0, NULL, 0},
        };

      int c = getopt_long (argc, argv, "", options, NULL);
      if (c == -1)
        break;

      switch (c)
        {
        case OPT_BE:
          float_format = FLOAT_IEEE_DOUBLE_BE;
          integer_format = INTEGER_MSB_FIRST;
          break;

        case OPT_LE:
          float_format = FLOAT_IEEE_DOUBLE_LE;
          integer_format = INTEGER_LSB_FIRST;
          break;

        case OPT_HELP:
          usage ();

        case 0:
          break;

        case '?':
          exit (EXIT_FAILURE);
          break;

        default:
          NOT_REACHED ();
        }

    }

  if (optind + 1 != argc)
    error (1, 0, "exactly one non-option argument required; "
           "use --help for help");
  return argv[optind];
}

static void
parse_data_item (struct buffer *output)
{
  size_t old_size = output->size;

  if (token == T_INTEGER)
    {
      integer_put (tok_integer, integer_format,
                   buffer_put_uninit (output, 4), 4);
      get_token ();
    }
  else if (token == T_FLOAT)
    {
      float_convert (FLOAT_NATIVE_DOUBLE, &tok_float,
                     float_format, buffer_put_uninit (output, 8));
      get_token ();
    }
  else if (token == T_I8)
    {
      uint8_t byte;

      get_token ();
      do
        {
          if (token != T_INTEGER)
            fatal ("integer expected after `i8'");
          byte = tok_integer;
          buffer_put (output, &byte, 1);
          get_token ();
        }
      while (token == T_INTEGER);
    }
  else if (token == T_I64)
    {
      get_token ();
      do
        {
          if (token != T_INTEGER)
            fatal ("integer expected after `i64'");
          integer_put (tok_integer, integer_format,
                       buffer_put_uninit (output, 8), 8);
          get_token ();
        }
      while (token == T_INTEGER);
    }
  else if (token == T_STRING)
    {
      buffer_put (output, tok_string, tok_strlen);
      get_token ();
    }
  else if (token == T_S)
    {
      int n;

      n = tok_integer;
      get_token ();

      if (token != T_STRING)
        fatal ("string expected");
      if (tok_strlen > n)
        fatal ("%zu-byte string is longer than pad length %d",
               tok_strlen, n);

      buffer_put (output, tok_string, tok_strlen);
      memset (buffer_put_uninit (output, n - tok_strlen), ' ',
              n - tok_strlen);
      get_token ();
    }
  else if (token == T_LPAREN)
    {
      get_token ();

      while (token != T_RPAREN)
        parse_data_item (output);

      get_token ();
    }
  else if (token == T_COUNT)
    {
      buffer_put_uninit (output, 4);

      get_token ();
      if (token != T_LPAREN)
        fatal ("`(' expected after COUNT");
      get_token ();

      while (token != T_RPAREN)
        parse_data_item (output);
      get_token ();

      integer_put (output->size - old_size - 4, integer_format,
                   output->data + old_size, 4);
    }
  else if (token == T_HEX)
    {
      const char *p;

      get_token ();

      if (token != T_STRING)
        fatal ("string expected");

      for (p = tok_string; *p; p++)
        {
          if (isspace ((unsigned char) *p))
            continue;
          else if (isxdigit ((unsigned char) p[0])
                   && isxdigit ((unsigned char) p[1]))
            {
              int high = hexit_value (p[0]);
              int low = hexit_value (p[1]);
              uint8_t byte = high * 16 + low;
              buffer_put (output, &byte, 1);
              p++;
            }
          else
            fatal ("invalid format in hex string");
        }
      get_token ();
    }
  else
    fatal ("syntax error");

  if (token == T_ASTERISK)
    {
      size_t n = output->size - old_size;
      char *p;

      get_token ();

      if (token != T_INTEGER || tok_integer < 1)
        fatal ("positive integer expected after `*'");
      p = buffer_put_uninit (output, (tok_integer - 1) * n);
      while (--tok_integer > 0)
        {
          memcpy (p, output->data + old_size, n);
          p += n;
        }

      get_token ();
    }

  if (token == T_SEMICOLON)
    get_token ();
  else if (token != T_RPAREN)
    fatal ("`;' expected");
}

int
main (int argc, char **argv)
{
  struct buffer output;
  uint8_t digest[16];
  int i;

  set_program_name (argv[0]);
  input_file_name = parse_options (argc, argv);

  if (!strcmp (input_file_name, "-"))
    input = stdin;
  else
    {
      input = fopen (input_file_name, "r");
      if (input == NULL)
        error (1, errno, "%s: open failed", input_file_name);
    }

  if (isatty (STDOUT_FILENO))
    error (1, 0, "not writing binary data to a terminal; redirect to a file");

  output.data = NULL;
  output.size = 0;
  output.allocated = 0;

  line_number = 1;
  get_token ();
  while (token != T_EOF)
    parse_data_item (&output);

  if (input != stdin)
    fclose (input);

  fwrite (output.data, output.size, 1, stdout);

  md5_buffer ((const char *) output.data, output.size, digest);
  for (i = 0; i < sizeof digest; i++)
    fprintf (stderr, "%02x", digest[i]);
  putc ('\n', stderr);

  return 0;
}
