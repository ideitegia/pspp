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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include "alloc.h"
#include "error.h"
#include "pool.h"
#include "str.h"

/* sprintf() wrapper functions for convenience. */

#if !__GNUC__
char *
spprintf (char *buf, const char *format,...)
{
#if HAVE_GOOD_SPRINTF
  int count;
#endif
  va_list args;

  va_start (args, format);
#if HAVE_GOOD_SPRINTF
  count =
#endif
    vsprintf (buf, format, args);
  va_end (args);

#if HAVE_GOOD_SPRINTF
  return &buf[count];
#else
  return strchr (buf, 0);
#endif
}
#endif /* !__GNUC__ */

#if !__GNUC__ && !HAVE_GOOD_SPRINTF
int
nsprintf (char *buf, const char *format,...)
{
  va_list args;

  va_start (args, format);
  vsprintf (buf, format, args);
  va_end (args);

  return strlen (buf);
}

int
nvsprintf (char *buf, const char *format, va_list args)
{
  vsprintf (buf, format, args);
  return strlen (buf);
}
#endif /* Not GNU C and not good sprintf(). */

/* Reverses the order of NBYTES bytes at address P, thus converting
   between little- and big-endian byte orders.  */
void
mm_reverse (void *p, size_t nbytes)
{
  unsigned char *h = p, *t = &h[nbytes - 1];
  unsigned char temp;

  nbytes /= 2;
  while (nbytes--)
    {
      temp = *h;
      *h++ = *t;
      *t-- = temp;
    }
}

/* Finds the last NEEDLE of length NEEDLE_LEN in a HAYSTACK of length
   HAYSTACK_LEN.  Returns a pointer to the needle found. */
char *
mm_find_reverse (const char *haystack, size_t haystack_len,
	 const char *needle, size_t needle_len)
{
  int i;
  for (i = haystack_len - needle_len; i >= 0; i--)
    if (!memcmp (needle, &haystack[i], needle_len))
      return (char *) &haystack[i];
  return 0;
}

/* Compares S0 of length S0L to S1 of length S1L.  The shorter string
   is considered to be padded with spaces to the length of the
   longer. */
int
st_compare_pad (const char *s0, int s0l, const char *s1, int s1l)
{
  /* 254 spaces. */
  static char blanks[254] =
  "                                                               "
  "                                                               "
  "                                                               "
  "                                                               "
  "  ";

  int diff = s0l - s1l;
  int r;

  if (diff == 0)
    {
      if (s0l == 0)
	return 0;
      return memcmp (s0, s1, s0l);
    }
  else if (diff > 0)
    {
      /* s0l > s1l */
      if (s1l)
	{
	  r = memcmp (s0, s1, s1l);
	  if (r)
	    return r;
	}
      return memcmp (&s0[s1l], blanks, diff);
    }
  else
    /* diff<0 */
    {
      /* s0l < s1l */
      if (s0l)
	{
	  r = memcmp (s0, s1, s0l);
	  if (r)
	    return r;
	}
      return memcmp (blanks, &s1[s0l], -diff);
    }
}

/* Copies SRC to DEST, truncating to N characters or right-padding
   with spaces to N characters as necessary.  Does not append a null
   character.  SRC must be null-terminated. */
void
st_bare_pad_copy (char *dest, const char *src, size_t n)
{
  size_t len;

  len = strlen (src);
  if (len >= n)
    memcpy (dest, src, n);
  else
    {
      memcpy (dest, src, len);
      memset (&dest[len], ' ', n - len);
    }
}

/* Copies SRC to DEST, truncating SRC to N characters or right-padding
   with spaces to N characters if necessary.  Does not append a null
   character.  SRC must be LEN characters long but does not need to be
   null-terminated. */
void
st_bare_pad_len_copy (char *dest, const char *src, size_t n, size_t len)
{
  if (len >= n)
    memcpy (dest, src, n);
  else
    {
      memcpy (dest, src, len);
      memset (&dest[len], ' ', n - len);
    }
}

/* Copies SRC to DEST, truncating SRC to N-1 characters or
   right-padding with spaces to N-1 characters if necessary.  Always
   appends a null character. */
void
st_pad_copy (char *dest, const char *src, size_t n)
{
  size_t len;

  len = strlen (src);
  if (len == n - 1)
    strcpy (dest, src);
  else if (len < n - 1)
    {
      memcpy (dest, src, len);
      memset (&dest[len], ' ', n - 1 - len);
      dest[n - 1] = 0;
    }
  else
    {
      memcpy (dest, src, n - 1);
      dest[n - 1] = 0;
    }
}

/* Initializes ST inside pool POOL (which may be a null pointer) with
   initial contents S. */
void
ds_create (struct pool *pool, struct string *st, const char *s)
{
  st->pool = pool;
  st->length = strlen (s);
  st->size = 8 + st->length * 2;
  st->string = pool_malloc (pool, st->size + 1);
  strcpy (st->string, s);
}

/* Initializes ST inside POOL (which may be null), making room for at
   least SIZE characters. */
void
ds_init (struct pool *pool, struct string *st, size_t size)
{
  st->pool = pool;
  st->length = 0;
  if (size > 8)
    st->size = size;
  else
    st->size = 8;
  st->string = pool_malloc (pool, st->size + 1);
}

/* Replaces the contents of ST with STRING.  STRING may overlap with
   ST. */
void
ds_replace (struct string *st, const char *string)
{
  char *s = st->string;
  st->string = NULL;
  ds_create (st->pool, st, string);
  pool_free (st->pool, s);
}

/* Frees ST. */
void
ds_destroy (struct string *st)
{
  pool_free (st->pool, st->string);
}

/* Truncates ST to zero length. */
void
ds_clear (struct string *st)
{
  st->length = 0;
}

/* Ensures that ST can hold at least MIN_SIZE characters plus a null
   terminator. */
void
ds_extend (struct string *st, size_t min_size)
{
  if (min_size > st->size)
    {
      st->size *= 2;
      if (st->size < min_size)
	st->size = min_size * 2;
      
      st->string = pool_realloc (st->pool, st->string, st->size + 1);
    }
}

/* Shrink ST to the minimum size need to contain its content. */
void
ds_shrink (struct string *st)
{
  if (st->size != st->length)
    {
      st->size = st->length;
      st->string = pool_realloc (st->pool, st->string, st->size + 1);
    }
}

/* Truncates ST to at most LENGTH characters long. */
void
ds_truncate (struct string *st, size_t length)
{
  if (length >= st->length)
    return;
  st->length = length;
}

/* Returns the length of ST. */
size_t
ds_length (const struct string *st)
{
  return st->length;
}

/* Returns the allocation size of ST. */
size_t
ds_size (const struct string *st)
{
  return st->size;
}

/* Returns the value of ST as a null-terminated string. */
char *
ds_value (const struct string *st)
{
  ((char *) st->string)[st->length] = '\0';
  return st->string;
}

/* Returns a pointer to the null terminator ST.
   This might not be an actual null character unless ds_value() has
   been called since the last modification to ST. */
char *
ds_end (const struct string *st)
{
  return st->string + st->length;
}

/* Concatenates S onto ST. */
void
ds_concat (struct string *st, const char *s)
{
  size_t s_len = strlen (s);
  ds_extend (st, st->length + s_len);
  strcpy (st->string + st->length, s);
  st->length += s_len;
}

/* Concatenates LEN characters from BUF onto ST. */
void
ds_concat_buffer (struct string *st, const char *buf, size_t len)
{
  ds_extend (st, st->length + len);
  memcpy (st->string + st->length, buf, len);
  st->length += len;
}

/* Formats FORMAT as a printf string and appends the result to ST. */
void
ds_printf (struct string *st, const char *format, ...)
{
  /* Fscking glibc silently changed behavior between 2.0 and 2.1.
     Fsck fsck fsck.  Before, it returned -1 on buffer overflow.  Now,
     it returns the number of characters (not bytes) that would have
     been written. */
  va_list args;

  int avail, needed;

  va_start (args, format);
  avail = st->size - st->length + 1;
  needed = vsnprintf (st->string + st->length, avail, format, args);
  va_end (args);

  if (needed >= avail)
    {
      ds_extend (st, st->length + needed);
      
      va_start (args, format);
      vsprintf (st->string + st->length, format, args);
      va_end (args);
    }
  else
    while (needed == -1)
      {
	ds_extend (st, (st->size + 1) * 2);
	avail = st->size - st->length + 1;
	
	va_start (args, format);
	needed = vsnprintf (st->string + st->length, avail, format, args);
	va_end (args);
      }

  st->length += needed;
}

/* Appends character CH to ST. */
void
ds_putchar (struct string *st, int ch)
{
  if (st->length == st->size)
    ds_extend (st, st->length + 1);
  st->string[st->length++] = ch;
}

/* Reads a newline-terminated line from STREAM into ST.
   Newline is the last character of ST on return, unless an I/O error
   or end of file is encountered after reading some characters.
   Returns 1 if a line is successfully read, or 0 if no characters at
   all were read before an I/O error or end of file was
   encountered. */
int
ds_getline (struct string *st, FILE *stream)
{
  int c;

  c = getc (stream);
  if (c == EOF)
    return 0;

  for (;;)
    {
      ds_putchar (st, c);
      if (c == '\n')
	return 1;

      c = getc (stream);
      if (c == EOF)
	return 1;
    }
}

/* Reads a line from STREAM into ST, then preprocesses as follows:

   - Splices lines terminated with `\'.

   - Deletes comments introduced by `#' outside of single or double
     quotes.

   - Trailing whitespace will be deleted.  

   Increments cust_ln as appropriate.

   Returns nonzero only if a line was successfully read. */
int
ds_get_config_line (FILE *stream, struct string *st, struct file_locator *where)
{
  /* Read the first line. */
  ds_clear (st);
  where->line_number++;
  if (!ds_getline (st, stream))
    return 0;

  /* Read additional lines, if any. */
  for (;;)
    {
      /* Remove trailing whitespace. */
      {
	char *s = ds_value (st);
	size_t len = ds_length (st);
      
	while (len > 0 && isspace ((unsigned char) s[len - 1]))
	  len--;
	ds_truncate (st, len);
      }

      /* Check for trailing \.  Remove if found, bail otherwise. */
      if (ds_length (st) == 0 || ds_value (st)[ds_length (st) - 1] != '\\')
	break;
      ds_truncate (st, ds_length (st) - 1);

      /* Append another line and go around again. */
      {
	int success = ds_getline (st, stream);
	where->line_number++;
	if (!success)
	  return 1;
      }
    }

  /* Find a comment and remove. */
  {
    char *cp;
    int quote = 0;
      
    for (cp = ds_value (st); *cp; cp++)
      if (quote)
	{
	  if (*cp == quote)
	    quote = 0;
	  else if (*cp == '\\')
	    cp++;
	}
      else if (*cp == '\'' || *cp == '"')
	quote = *cp;
      else if (*cp == '#')
	{
	  ds_truncate (st, cp - ds_value (st));
	  break;
	}
  }

  return 1;
}

/* Lengthed strings. */

/* Creates a new lengthed string LS in POOL with contents as a copy of
   S. */
void
ls_create (struct pool *pool, struct len_string *ls, const char *s)
{
  ls->length = strlen (s);
  ls->string = pool_alloc (pool, ls->length + 1);
  memcpy (ls->string, s, ls->length + 1);
}

/* Creates a new lengthed string LS in POOL with contents as a copy of
   BUFFER with length LEN. */
void
ls_create_buffer (struct pool *pool, struct len_string *ls,
		  const char *buffer, size_t len)
{
  ls->length = len;
  ls->string = pool_malloc (pool, len + 1);
  memcpy (ls->string, buffer, len);
  ls->string[len] = '\0';
}

/* Sets the fields of LS to the specified values. */
void
ls_init (struct len_string *ls, const char *string, size_t length)
{
  ls->string = (char *) string;
  ls->length = length;
}

/* Copies the fields of SRC to DST. */
void
ls_shallow_copy (struct len_string *dst, const struct len_string *src)
{
  *dst = *src;
}

/* Frees the memory in POOL backing LS. */
void
ls_destroy (struct pool *pool, struct len_string *ls)
{
  pool_free (pool, ls->string);
}

/* Sets LS to a null pointer value. */
void
ls_null (struct len_string *ls)
{
  ls->string = NULL;
}

/* Returns nonzero only if LS has a null pointer value. */
int
ls_null_p (const struct len_string *ls)
{
  return ls->string == NULL;
}

/* Returns nonzero only if LS is a null pointer or has length 0. */
int
ls_empty_p (const struct len_string *ls)
{
  return ls->string == NULL || ls->length == 0;
}

/* Returns the length of LS, which must not be null. */
size_t
ls_length (const struct len_string *ls)
{
  return ls->length;
}

/* Returns a pointer to the character string in LS. */
char *
ls_value (const struct len_string *ls)
{
  return (char *) ls->string;
}

/* Returns a pointer to the null terminator of the character string in
   LS. */
char *
ls_end (const struct len_string *ls)
{
  return (char *) (ls->string + ls->length);
}
