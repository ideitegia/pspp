/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
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
#include "str.h"
#include "message.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include "alloc.h"
#include "message.h"

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
buf_reverse (char *p, size_t nbytes)
{
  char *h = p, *t = &h[nbytes - 1];
  char temp;

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
buf_find_reverse (const char *haystack, size_t haystack_len,
                 const char *needle, size_t needle_len)
{
  int i;
  for (i = haystack_len - needle_len; i >= 0; i--)
    if (!memcmp (needle, &haystack[i], needle_len))
      return (char *) &haystack[i];
  return 0;
}

/* Compares the SIZE bytes in A to those in B, disregarding case,
   and returns a strcmp()-type result. */
int
buf_compare_case (const char *a_, const char *b_, size_t size)
{
  const unsigned char *a = (unsigned char *) a_;
  const unsigned char *b = (unsigned char *) b_;

  while (size-- > 0) 
    {
      unsigned char ac = toupper (*a++);
      unsigned char bc = toupper (*b++);

      if (ac != bc) 
        return ac > bc ? 1 : -1;
    }

  return 0;
}

/* Compares A of length A_LEN to B of length B_LEN.  The shorter
   string is considered to be padded with spaces to the length of
   the longer. */
int
buf_compare_rpad (const char *a, size_t a_len, const char *b, size_t b_len)
{
  size_t min_len;
  int result;

  min_len = a_len < b_len ? a_len : b_len;
  result = memcmp (a, b, min_len);
  if (result != 0)
    return result;
  else 
    {
      size_t idx;
      
      if (a_len < b_len) 
        {
          for (idx = min_len; idx < b_len; idx++)
            if (' ' != b[idx])
              return ' ' > b[idx] ? 1 : -1;
        }
      else 
        {
          for (idx = min_len; idx < a_len; idx++)
            if (a[idx] != ' ')
              return a[idx] > ' ' ? 1 : -1;
        }
      return 0;
    }
}

/* Compares strin A to string B.  The shorter string is
   considered to be padded with spaces to the length of the
   longer. */
int
str_compare_rpad (const char *a, const char *b)
{
  return buf_compare_rpad (a, strlen (a), b, strlen (b));
}

/* Copies string SRC to buffer DST, of size DST_SIZE bytes.
   DST is truncated to DST_SIZE bytes or padded on the right with
   spaces as needed. */
void
buf_copy_str_rpad (char *dst, size_t dst_size, const char *src)
{
  size_t src_len = strlen (src);
  if (src_len >= dst_size)
    memcpy (dst, src, dst_size);
  else
    {
      memcpy (dst, src, src_len);
      memset (&dst[src_len], ' ', dst_size - src_len);
    }
}

/* Copies string SRC to buffer DST, of size DST_SIZE bytes.
   DST is truncated to DST_SIZE bytes or padded on the left with
   spaces as needed. */
void
buf_copy_str_lpad (char *dst, size_t dst_size, const char *src)
{
  size_t src_len = strlen (src);
  if (src_len >= dst_size)
    memcpy (dst, src, dst_size);
  else
    {
      size_t pad_cnt = dst_size - src_len;
      memset (&dst[0], ' ', pad_cnt);
      memcpy (dst + pad_cnt, src, src_len);
    }
}

/* Copies buffer SRC, of SRC_SIZE bytes, to DST, of DST_SIZE bytes.
   DST is truncated to DST_SIZE bytes or padded on the right with
   spaces as needed. */
void
buf_copy_rpad (char *dst, size_t dst_size,
               const char *src, size_t src_size)
{
  if (src_size >= dst_size)
    memmove (dst, src, dst_size);
  else
    {
      memmove (dst, src, src_size);
      memset (&dst[src_size], ' ', dst_size - src_size);
    }
}

/* Copies string SRC to string DST, which is in a buffer DST_SIZE
   bytes long.
   Truncates DST to DST_SIZE - 1 characters or right-pads with
   spaces to DST_SIZE - 1 characters if necessary. */
void
str_copy_rpad (char *dst, size_t dst_size, const char *src)
{
  size_t src_len = strlen (src);
  if (src_len < dst_size - 1)
    {
      memcpy (dst, src, src_len);
      memset (&dst[src_len], ' ', dst_size - 1 - src_len);
    }
  else
    memcpy (dst, src, dst_size - 1);
  dst[dst_size - 1] = 0;
}

/* Copies SRC to DST, which is in a buffer DST_SIZE bytes long.
   Truncates DST to DST_SIZE - 1 characters, if necessary. */
void
str_copy_trunc (char *dst, size_t dst_size, const char *src) 
{
  size_t src_len = strlen (src);
  assert (dst_size > 0);
  if (src_len + 1 < dst_size)
    memcpy (dst, src, src_len + 1);
  else 
    {
      memcpy (dst, src, dst_size - 1);
      dst[dst_size - 1] = '\0';
    }
}

/* Copies buffer SRC, of SRC_LEN bytes,
   to DST, which is in a buffer DST_SIZE bytes long.
   Truncates DST to DST_SIZE - 1 characters, if necessary. */
void
str_copy_buf_trunc (char *dst, size_t dst_size,
                    const char *src, size_t src_size) 
{
  size_t dst_len;
  assert (dst_size > 0);

  dst_len = src_size < dst_size ? src_size : dst_size - 1;
  memcpy (dst, src, dst_len);
  dst[dst_len] = '\0';
}

/* Converts each character in S to uppercase. */
void
str_uppercase (char *s) 
{
  for (; *s != '\0'; s++)
    *s = toupper ((unsigned char) *s);
}

/* Converts each character in S to lowercase. */
void
str_lowercase (char *s) 
{
  for (; *s != '\0'; s++)
    *s = tolower ((unsigned char) *s);
}

/* Initializes ST with initial contents S. */
void
ds_create (struct string *st, const char *s)
{
  st->length = strlen (s);
  st->capacity = 8 + st->length * 2;
  st->string = xmalloc (st->capacity + 1);
  strcpy (st->string, s);
}

/* Initializes ST, making room for at least CAPACITY characters. */
void
ds_init (struct string *st, size_t capacity)
{
  st->length = 0;
  if (capacity > 8)
    st->capacity = capacity;
  else
    st->capacity = 8;
  st->string = xmalloc (st->capacity + 1);
}

/* Replaces the contents of ST with STRING.  STRING may overlap with
   ST. */
void
ds_replace (struct string *st, const char *string)
{
  size_t new_length = strlen (string);
  if (new_length > st->capacity) 
    {
      /* The new length is longer than the allocated length, so
         there can be no overlap. */
      st->length = 0;
      ds_concat (st, string, new_length);
    }
  else
    {
      /* Overlap is possible, but the new string will fit in the
         allocated space, so we can just copy data. */
      st->length = new_length;
      memmove (st->string, string, st->length);
    }
}

/* Frees ST. */
void
ds_destroy (struct string *st)
{
  free (st->string);
  st->string = NULL;
}

/* Swaps the contents of strings A and B. */
void
ds_swap (struct string *a, struct string *b) 
{
  struct string tmp = *a;
  *a = *b;
  *b = tmp;
}

/* Truncates ST to zero length. */
void
ds_clear (struct string *st)
{
  st->length = 0;
}

/* Pad ST on the right with copies of PAD until ST is at least
   LENGTH characters in size.  If ST is initially LENGTH
   characters or longer, this is a no-op. */
void
ds_rpad (struct string *st, size_t length, char pad) 
{
  assert (st != NULL);
  if (st->length < length) 
    {
      if (st->capacity < length)
        ds_extend (st, length);
      memset (&st->string[st->length], pad, length - st->length);
      st->length = length;
    }
}

/* Removes trailing spaces from ST.
   Returns number of spaces removed. */
int
ds_rtrim_spaces (struct string *st) 
{
  int cnt = 0;
  while (isspace (ds_last (st))) 
    {
      st->length--;
      cnt++;
    }
  return cnt;
}

/* If the last character in ST is C, removes it and returns true.
   Otherwise, returns false without modifying ST. */
bool
ds_chomp (struct string *st, char c_) 
{
  unsigned char c = c_;
  if (ds_last (st) == c)
    {
      st->length--;
      return true;
    }
  else
    return false;
}

/* Ensures that ST can hold at least MIN_CAPACITY characters plus a null
   terminator. */
void
ds_extend (struct string *st, size_t min_capacity)
{
  if (min_capacity > st->capacity)
    {
      st->capacity *= 2;
      if (st->capacity < min_capacity)
	st->capacity = min_capacity * 2;
      
      st->string = xrealloc (st->string, st->capacity + 1);
    }
}

/* Shrink ST to the minimum capacity need to contain its content. */
void
ds_shrink (struct string *st)
{
  if (st->capacity != st->length)
    {
      st->capacity = st->length;
      st->string = xrealloc (st->string, st->capacity + 1);
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

/* Returns true if ST is empty, false otherwise. */
bool
ds_is_empty (const struct string *st) 
{
  return st->length == 0;
}

/* Returns the length of ST. */
size_t
ds_length (const struct string *st)
{
  return st->length;
}

/* Returns the allocation size of ST. */
size_t
ds_capacity (const struct string *st)
{
  return st->capacity;
}

/* Returns the first character in ST as a value in the range of
   unsigned char.  Returns EOF if ST is the empty string. */
int
ds_first (const struct string *st) 
{
  return st->length > 0 ? (unsigned char) st->string[0] : EOF;
}

/* Returns the last character in ST as a value in the range of
   unsigned char.  Returns EOF if ST is the empty string. */
int
ds_last (const struct string *st) 
{
  return st->length > 0 ? (unsigned char) st->string[st->length - 1] : EOF;
}

/* Returns the value of ST as a null-terminated string. */
char *
ds_c_str (const struct string *st)
{
  ((char *) st->string)[st->length] = '\0';
  return st->string;
}

/* Returns the string data inside ST. */
char *
ds_data (const struct string *st)
{
  return st->string;
}

/* Returns a pointer to the null terminator ST.
   This might not be an actual null character unless ds_c_str() has
   been called since the last modification to ST. */
char *
ds_end (const struct string *st)
{
  return st->string + st->length;
}

/* Concatenates S onto ST. */
void
ds_puts (struct string *st, const char *s)
{
  size_t s_len;

  if (!s) return;

  s_len = strlen (s);
  ds_extend (st, st->length + s_len);
  strcpy (st->string + st->length, s);
  st->length += s_len;
}

/* Concatenates LEN characters from BUF onto ST. */
void
ds_concat (struct string *st, const char *buf, size_t len)
{
  ds_extend (st, st->length + len);
  memcpy (st->string + st->length, buf, len);
  st->length += len;
}

void ds_vprintf (struct string *st, const char *format, va_list args);


/* Formats FORMAT as a printf string and appends the result to ST. */
void
ds_printf (struct string *st, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  ds_vprintf(st,format,args);
  va_end (args);

}

/* Formats FORMAT as a printf string and appends the result to ST. */
void
ds_vprintf (struct string *st, const char *format, va_list args)
{
  /* Fscking glibc silently changed behavior between 2.0 and 2.1.
     Fsck fsck fsck.  Before, it returned -1 on buffer overflow.  Now,
     it returns the number of characters (not bytes) that would have
     been written. */

  int avail, needed;
  va_list a1;

  va_copy(a1, args);
  avail = st->capacity - st->length + 1;
  needed = vsnprintf (st->string + st->length, avail, format, args);


  if (needed >= avail)
    {
      ds_extend (st, st->length + needed);
      
      vsprintf (st->string + st->length, format, a1);
    }
  else
    while (needed == -1)
      {
	va_list a2;
	va_copy(a2, a1);

	ds_extend (st, (st->capacity + 1) * 2);
	avail = st->capacity - st->length + 1;

	needed = vsnprintf (st->string + st->length, avail, format, a2);
	va_end(a2);

      }
  va_end(a1);

  st->length += needed;
}

/* Appends character CH to ST. */
void
ds_putc (struct string *st, int ch)
{
  if (st->length == st->capacity)
    ds_extend (st, st->length + 1);
  st->string[st->length++] = ch;
}

/* Appends to ST a newline-terminated line read from STREAM.
   Newline is the last character of ST on return, unless an I/O error
   or end of file is encountered after reading some characters.
   Returns 1 if a line is successfully read, or 0 if no characters at
   all were read before an I/O error or end of file was
   encountered. */
int
ds_gets (struct string *st, FILE *stream)
{
  int c;

  c = getc (stream);
  if (c == EOF)
    return 0;

  for (;;)
    {
      ds_putc (st, c);
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
  if (!ds_gets (st, stream))
    return 0;

  /* Read additional lines, if any. */
  for (;;)
    {
      /* Remove trailing whitespace. */
      {
	char *s = ds_c_str (st);
	size_t len = ds_length (st);
      
	while (len > 0 && isspace ((unsigned char) s[len - 1]))
	  len--;
	ds_truncate (st, len);
      }

      /* Check for trailing \.  Remove if found, bail otherwise. */
      if (ds_length (st) == 0 || ds_c_str (st)[ds_length (st) - 1] != '\\')
	break;
      ds_truncate (st, ds_length (st) - 1);

      /* Append another line and go around again. */
      {
	int success = ds_gets (st, stream);
	where->line_number++;
	if (!success)
	  return 1;
      }
    }

  /* Find a comment and remove. */
  {
    char *cp;
    int quote = 0;
      
    for (cp = ds_c_str (st); *cp; cp++)
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
	  ds_truncate (st, cp - ds_c_str (st));
	  break;
	}
  }

  return 1;
}

/* Lengthed strings. */

/* Creates a new lengthed string LS with contents as a copy of
   S. */
void
ls_create (struct fixed_string *ls, const char *s)
{
  ls->length = strlen (s);
  ls->string = xmalloc (ls->length + 1);
  memcpy (ls->string, s, ls->length + 1);
}

/* Creates a new lengthed string LS with contents as a copy of
   BUFFER with length LEN. */
void
ls_create_buffer (struct fixed_string *ls,
		  const char *buffer, size_t len)
{
  ls->length = len;
  ls->string = xmalloc (len + 1);
  memcpy (ls->string, buffer, len);
  ls->string[len] = '\0';
}

/* Sets the fields of LS to the specified values. */
void
ls_init (struct fixed_string *ls, const char *string, size_t length)
{
  ls->string = (char *) string;
  ls->length = length;
}

/* Copies the fields of SRC to DST. */
void
ls_shallow_copy (struct fixed_string *dst, const struct fixed_string *src)
{
  *dst = *src;
}

/* Frees the memory backing LS. */
void
ls_destroy (struct fixed_string *ls)
{
  free (ls->string);
}

/* Sets LS to a null pointer value. */
void
ls_null (struct fixed_string *ls)
{
  ls->string = NULL;
}

/* Returns nonzero only if LS has a null pointer value. */
int
ls_null_p (const struct fixed_string *ls)
{
  return ls->string == NULL;
}

/* Returns nonzero only if LS is a null pointer or has length 0. */
int
ls_empty_p (const struct fixed_string *ls)
{
  return ls->string == NULL || ls->length == 0;
}

/* Returns the length of LS, which must not be null. */
size_t
ls_length (const struct fixed_string *ls)
{
  return ls->length;
}

/* Returns a pointer to the character string in LS. */
char *
ls_c_str (const struct fixed_string *ls)
{
  return (char *) ls->string;
}

/* Returns a pointer to the null terminator of the character string in
   LS. */
char *
ls_end (const struct fixed_string *ls)
{
  return (char *) (ls->string + ls->length);
}
