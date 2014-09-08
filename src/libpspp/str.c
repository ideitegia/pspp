/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2014 Free Software Foundation, Inc.

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

#include "str.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistr.h>

#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"

#include "gl/c-ctype.h"
#include "gl/c-vasnprintf.h"
#include "gl/relocatable.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"
#include "gl/xsize.h"

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
   copies of PAD as needed. */
void
buf_copy_str_rpad (char *dst, size_t dst_size, const char *src, char pad)
{
  size_t src_len = strlen (src);
  if (src_len >= dst_size)
    memcpy (dst, src, dst_size);
  else
    {
      memcpy (dst, src, src_len);
      memset (&dst[src_len], pad, dst_size - src_len);
    }
}

/* Copies string SRC to buffer DST, of size DST_SIZE bytes.
   DST is truncated to DST_SIZE bytes or padded on the left with
   copies of PAD as needed. */
void
buf_copy_str_lpad (char *dst, size_t dst_size, const char *src, char pad)
{
  size_t src_len = strlen (src);
  if (src_len >= dst_size)
    memcpy (dst, src, dst_size);
  else
    {
      size_t pad_cnt = dst_size - src_len;
      memset (&dst[0], pad, pad_cnt);
      memcpy (dst + pad_cnt, src, src_len);
    }
}

/* Copies buffer SRC, of SRC_SIZE bytes, to DST, of DST_SIZE bytes.
   DST is truncated to DST_SIZE bytes or padded on the left with
   copies of PAD as needed. */
void
buf_copy_lpad (char *dst, size_t dst_size,
               const char *src, size_t src_size,
               char pad)
{
  if (src_size >= dst_size)
    memmove (dst, src, dst_size);
  else
    {
      memset (dst, pad, dst_size - src_size);
      memmove (&dst[dst_size - src_size], src, src_size);
    }
}

/* Copies buffer SRC, of SRC_SIZE bytes, to DST, of DST_SIZE bytes.
   DST is truncated to DST_SIZE bytes or padded on the right with
   copies of PAD as needed. */
void
buf_copy_rpad (char *dst, size_t dst_size,
               const char *src, size_t src_size,
               char pad)
{
  if (src_size >= dst_size)
    memmove (dst, src, dst_size);
  else
    {
      memmove (dst, src, src_size);
      memset (&dst[src_size], pad, dst_size - src_size);
    }
}

/* Copies string SRC to string DST, which is in a buffer DST_SIZE
   bytes long.
   Truncates DST to DST_SIZE - 1 bytes or right-pads with
   spaces to DST_SIZE - 1 bytes if necessary. */
void
str_copy_rpad (char *dst, size_t dst_size, const char *src)
{
  if (dst_size > 0) 
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
}

/* Copies SRC to DST, which is in a buffer DST_SIZE bytes long.
   Truncates DST to DST_SIZE - 1 bytes, if necessary. */
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
   Truncates DST to DST_SIZE - 1 bytes, if necessary. */
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

/* Converts each byte in S to uppercase.

   This is suitable only for ASCII strings.  Use utf8_to_upper() for UTF-8
   strings.*/
void
str_uppercase (char *s)
{
  for (; *s != '\0'; s++)
    *s = c_toupper ((unsigned char) *s);
}

/* Converts each byte in S to lowercase.

   This is suitable only for ASCII strings.  Use utf8_to_lower() for UTF-8
   strings.*/
void
str_lowercase (char *s)
{
  for (; *s != '\0'; s++)
    *s = c_tolower ((unsigned char) *s);
}

/* Converts NUMBER into a string in 26-adic notation in BUFFER,
   which has room for SIZE bytes.  Uses uppercase if UPPERCASE is
   true, otherwise lowercase, Returns true if successful, false
   if NUMBER, plus a trailing null, is too large to fit in the
   available space.

   26-adic notation is "spreadsheet column numbering": 1 = A, 2 =
   B, 3 = C, ... 26 = Z, 27 = AA, 28 = AB, 29 = AC, ...

   26-adic notation is the special case of a k-adic numeration
   system (aka bijective base-k numeration) with k=26.  In k-adic
   numeration, the digits are {1, 2, 3, ..., k} (there is no
   digit 0), and integer 0 is represented by the empty string.
   For more information, see
   http://en.wikipedia.org/wiki/Bijective_numeration. */
bool
str_format_26adic (unsigned long int number, bool uppercase,
                   char buffer[], size_t size)
{
  const char *alphabet
    = uppercase ? "ABCDEFGHIJKLMNOPQRSTUVWXYZ" : "abcdefghijklmnopqrstuvwxyz";
  size_t length = 0;

  while (number-- > 0)
    {
      if (length >= size)
        goto overflow;
      buffer[length++] = alphabet[number % 26];
      number /= 26;
    }

  if (length >= size)
    goto overflow;
  buffer[length] = '\0';

  buf_reverse (buffer, length);
  return true;

overflow:
  if (length > 0)
    buffer[0] = '\0';
  return false;
}

/* Sets the SIZE bytes starting at BLOCK to C,
   and returns the byte following BLOCK. */
void *
mempset (void *block, int c, size_t size)
{
  memset (block, c, size);
  return (char *) block + size;
}

/* Substrings. */

/* Returns a substring whose contents are the CNT bytes
   starting at the (0-based) position START in SS. */
struct substring
ss_substr (struct substring ss, size_t start, size_t cnt)
{
  if (start < ss.length)
    return ss_buffer (ss.string + start, MIN (cnt, ss.length - start));
  else
    return ss_buffer (ss.string + ss.length, 0);
}

/* Returns a substring whose contents are the first CNT
   bytes in SS. */
struct substring
ss_head (struct substring ss, size_t cnt)
{
  return ss_buffer (ss.string, MIN (cnt, ss.length));
}

/* Returns a substring whose contents are the last CNT bytes
   in SS. */
struct substring
ss_tail (struct substring ss, size_t cnt)
{
  if (cnt < ss.length)
    return ss_buffer (ss.string + (ss.length - cnt), cnt);
  else
    return ss;
}

/* Makes a malloc()'d, null-terminated copy of the contents of OLD
   and stores it in NEW. */
void
ss_alloc_substring (struct substring *new, struct substring old)
{
  new->string = xmemdup0 (old.string, old.length);
  new->length = old.length;
}

/* Allocates room for a CNT-byte string in NEW. */
void
ss_alloc_uninit (struct substring *new, size_t cnt)
{
  new->string = xmalloc (cnt);
  new->length = cnt;
}

void
ss_realloc (struct substring *ss, size_t size)
{
  ss->string = xrealloc (ss->string, size);
}

/* Makes a pool_alloc_unaligned()'d, null-terminated copy of the contents of
   OLD in POOL, and stores it in NEW. */
void
ss_alloc_substring_pool (struct substring *new, struct substring old,
                         struct pool *pool)
{
  new->string = pool_alloc_unaligned (pool, old.length + 1);
  new->length = old.length;
  memcpy (new->string, old.string, old.length);
  new->string[old.length] = '\0';
}

/* Allocates room for a CNT-byte string in NEW in POOL. */
void
ss_alloc_uninit_pool (struct substring *new, size_t cnt, struct pool *pool)
{
  new->string = pool_alloc_unaligned (pool, cnt);
  new->length = cnt;
}

/* Frees the string that SS points to. */
void
ss_dealloc (struct substring *ss)
{
  free (ss->string);
}

/* Truncates SS to at most CNT bytes in length. */
void
ss_truncate (struct substring *ss, size_t cnt)
{
  if (ss->length > cnt)
    ss->length = cnt;
}

/* Removes trailing bytes in TRIM_SET from SS.
   Returns number of bytes removed. */
size_t
ss_rtrim (struct substring *ss, struct substring trim_set)
{
  size_t cnt = 0;
  while (cnt < ss->length
         && ss_find_byte (trim_set,
                          ss->string[ss->length - cnt - 1]) != SIZE_MAX)
    cnt++;
  ss->length -= cnt;
  return cnt;
}

/* Removes leading bytes in TRIM_SET from SS.
   Returns number of bytes removed. */
size_t
ss_ltrim (struct substring *ss, struct substring trim_set)
{
  size_t cnt = ss_span (*ss, trim_set);
  ss_advance (ss, cnt);
  return cnt;
}

/* Trims leading and trailing bytes in TRIM_SET from SS. */
void
ss_trim (struct substring *ss, struct substring trim_set)
{
  ss_ltrim (ss, trim_set);
  ss_rtrim (ss, trim_set);
}

/* If the last byte in SS is C, removes it and returns true.
   Otherwise, returns false without changing the string. */
bool
ss_chomp_byte (struct substring *ss, char c)
{
  if (ss_last (*ss) == c)
    {
      ss->length--;
      return true;
    }
  else
    return false;
}

/* If SS ends with SUFFIX, removes it and returns true.
   Otherwise, returns false without changing the string. */
bool
ss_chomp (struct substring *ss, struct substring suffix)
{
  if (ss_ends_with (*ss, suffix))
    {
      ss->length -= suffix.length;
      return true;
    }
  else
    return false;
}

/* Divides SS into tokens separated by any of the DELIMITERS.
   Each call replaces TOKEN by the next token in SS, or by an
   empty string if no tokens remain.  Returns true if a token was
   obtained, false otherwise.

   Before the first call, initialize *SAVE_IDX to 0.  Do not
   modify *SAVE_IDX between calls.

   SS divides into exactly one more tokens than it contains
   delimiters.  That is, a delimiter at the start or end of SS or
   a pair of adjacent delimiters yields an empty token, and the
   empty string contains a single token. */
bool
ss_separate (struct substring ss, struct substring delimiters,
             size_t *save_idx, struct substring *token)
{
  if (*save_idx <= ss_length (ss))
    {
      struct substring tmp = ss_substr (ss, *save_idx, SIZE_MAX);
      size_t length = ss_cspan (tmp, delimiters);
      *token = ss_head (tmp, length);
      *save_idx += length + 1;
      return true;
    }
  else
    {
      *token = ss_empty ();
      return false;
    }
}

/* Divides SS into tokens separated by any of the DELIMITERS,
   merging adjacent delimiters so that the empty string is never
   produced as a token.  Each call replaces TOKEN by the next
   token in SS, or by an empty string if no tokens remain, and
   then skips past the first delimiter following the token.
   Returns true if a token was obtained, false otherwise.

   Before the first call, initialize *SAVE_IDX to 0.  Do not
   modify *SAVE_IDX between calls. */
bool
ss_tokenize (struct substring ss, struct substring delimiters,
             size_t *save_idx, struct substring *token)
{
  bool found_token;

  ss_advance (&ss, *save_idx);
  *save_idx += ss_ltrim (&ss, delimiters);
  ss_get_bytes (&ss, ss_cspan (ss, delimiters), token);

  found_token = ss_length (*token) > 0;
  *save_idx += ss_length (*token) + found_token;
  return found_token;
}

/* Removes the first CNT bytes from SS. */
void
ss_advance (struct substring *ss, size_t cnt)
{
  if (cnt > ss->length)
    cnt = ss->length;
  ss->string += cnt;
  ss->length -= cnt;
}

/* If the first byte in SS is C, removes it and returns true.
   Otherwise, returns false without changing the string. */
bool
ss_match_byte (struct substring *ss, char c)
{
  if (ss_first (*ss) == c)
    {
      ss->string++;
      ss->length--;
      return true;
    }
  else
    return false;
}

/* If the first byte in SS is in MATCH, removes it and
   returns the byte that was removed.
   Otherwise, returns EOF without changing the string. */
int
ss_match_byte_in (struct substring *ss, struct substring match)
{
  int c = EOF;
  if (ss->length > 0
      && memchr (match.string, ss->string[0], match.length) != NULL)
    {
      c = ss->string[0];
      ss->string++;
      ss->length--;
    }
  return c;
}

/* If SS begins with TARGET, removes it and returns true.
   Otherwise, returns false without changing SS. */
bool
ss_match_string (struct substring *ss, const struct substring target)
{
  size_t length = ss_length (target);
  if (ss_equals (ss_head (*ss, length), target))
    {
      ss_advance (ss, length);
      return true;
    }
  else
    return false;
}

/* Removes the first byte from SS and returns it.
   If SS is empty, returns EOF without modifying SS. */
int
ss_get_byte (struct substring *ss)
{
  int c = ss_first (*ss);
  if (c != EOF)
    {
      ss->string++;
      ss->length--;
    }
  return c;
}

/* Stores the prefix of SS up to the first DELIMITER in OUT (if
   any).  Trims those same bytes from SS.  DELIMITER is
   removed from SS but not made part of OUT.  Returns true if
   DELIMITER was found (and removed), false otherwise. */
bool
ss_get_until (struct substring *ss, char delimiter, struct substring *out)
{
  ss_get_bytes (ss, ss_cspan (*ss, ss_buffer (&delimiter, 1)), out);
  return ss_match_byte (ss, delimiter);
}

/* Stores the first CNT bytes in SS in OUT (or fewer, if SS
   is shorter than CNT bytes).  Trims the same bytes
   from the beginning of SS.  Returns CNT. */
size_t
ss_get_bytes (struct substring *ss, size_t cnt, struct substring *out)
{
  *out = ss_head (*ss, cnt);
  ss_advance (ss, cnt);
  return cnt;
}

/* Parses and removes an optionally signed decimal integer from
   the beginning of SS.  Returns 0 if an error occurred,
   otherwise the number of bytes removed from SS.  Stores
   the integer's value into *VALUE. */
size_t
ss_get_long (struct substring *ss, long *value)
{
  char tmp[64];
  size_t length;

  length = ss_span (*ss, ss_cstr ("+-"));
  length += ss_span (ss_substr (*ss, length, SIZE_MAX), ss_cstr (CC_DIGITS));
  if (length > 0 && length < sizeof tmp)
    {
      char *tail;

      memcpy (tmp, ss_data (*ss), length);
      tmp[length] = '\0';

      *value = strtol (tmp, &tail, 10);
      if (tail - tmp == length)
        {
          ss_advance (ss, length);
          return length;
        }
    }
  *value = 0;
  return 0;
}

/* Returns true if SS is empty (has length 0 bytes),
   false otherwise. */
bool
ss_is_empty (struct substring ss)
{
  return ss.length == 0;
}

/* Returns the number of bytes in SS. */
size_t
ss_length (struct substring ss)
{
  return ss.length;
}

/* Returns a pointer to the bytes in SS. */
char *
ss_data (struct substring ss)
{
  return ss.string;
}

/* Returns a pointer just past the last byte in SS. */
char *
ss_end (struct substring ss)
{
  return ss.string + ss.length;
}

/* Returns the byte in position IDX in SS, as a value in the
   range of unsigned char.  Returns EOF if IDX is out of the
   range of indexes for SS. */
int
ss_at (struct substring ss, size_t idx)
{
  return idx < ss.length ? (unsigned char) ss.string[idx] : EOF;
}

/* Returns the first byte in SS as a value in the range of
   unsigned char.  Returns EOF if SS is the empty string. */
int
ss_first (struct substring ss)
{
  return ss_at (ss, 0);
}

/* Returns the last byte in SS as a value in the range of
   unsigned char.  Returns EOF if SS is the empty string. */
int
ss_last (struct substring ss)
{
  return ss.length > 0 ? (unsigned char) ss.string[ss.length - 1] : EOF;
}

/* Returns true if SS ends with SUFFIX, false otherwise. */
bool
ss_ends_with (struct substring ss, struct substring suffix)
{
  return (ss.length >= suffix.length
          && !memcmp (&ss.string[ss.length - suffix.length], suffix.string,
                      suffix.length));
}

/* Returns the number of contiguous bytes at the beginning
   of SS that are in SKIP_SET. */
size_t
ss_span (struct substring ss, struct substring skip_set)
{
  size_t i;
  for (i = 0; i < ss.length; i++)
    if (ss_find_byte (skip_set, ss.string[i]) == SIZE_MAX)
      break;
  return i;
}

/* Returns the number of contiguous bytes at the beginning
   of SS that are not in SKIP_SET. */
size_t
ss_cspan (struct substring ss, struct substring stop_set)
{
  size_t i;
  for (i = 0; i < ss.length; i++)
    if (ss_find_byte (stop_set, ss.string[i]) != SIZE_MAX)
      break;
  return i;
}

/* Returns the offset in SS of the first instance of C,
   or SIZE_MAX if C does not occur in SS. */
size_t
ss_find_byte (struct substring ss, char c)
{
  const char *p = memchr (ss.string, c, ss.length);
  return p != NULL ? p - ss.string : SIZE_MAX;
}

/* Compares A and B and returns a strcmp()-type comparison
   result. */
int
ss_compare (struct substring a, struct substring b)
{
  int retval = memcmp (a.string, b.string, MIN (a.length, b.length));
  if (retval == 0)
    retval = a.length < b.length ? -1 : a.length > b.length;
  return retval;
}

/* Compares A and B case-insensitively and returns a
   strcmp()-type comparison result. */
int
ss_compare_case (struct substring a, struct substring b)
{
  int retval = memcasecmp (a.string, b.string, MIN (a.length, b.length));
  if (retval == 0)
    retval = a.length < b.length ? -1 : a.length > b.length;
  return retval;
}

/* Compares A and B and returns true if their contents are
   identical, false otherwise. */
int
ss_equals (struct substring a, struct substring b)
{
  return a.length == b.length && !memcmp (a.string, b.string, a.length);
}

/* Compares A and B and returns true if their contents are
   identical except possibly for case differences, false
   otherwise. */
int
ss_equals_case (struct substring a, struct substring b)
{
  return a.length == b.length && !memcasecmp (a.string, b.string, a.length);
}

/* Returns the position in SS that the byte at P occupies.
   P must point within SS or one past its end. */
size_t
ss_pointer_to_position (struct substring ss, const char *p)
{
  size_t pos = p - ss.string;
  assert (pos <= ss.length);
  return pos;
}

/* Allocates and returns a null-terminated string that contains
   SS. */
char *
ss_xstrdup (struct substring ss)
{
  char *s = xmalloc (ss.length + 1);
  memcpy (s, ss.string, ss.length);
  s[ss.length] = '\0';
  return s;
}
/* UTF-8. */

/* Returns the character represented by the UTF-8 sequence at the start of S.
   The return value is either a Unicode code point in the range 0 to 0x10ffff,
   or UINT32_MAX if S is empty. */
ucs4_t
ss_first_mb (struct substring s)
{
  return ss_at_mb (s, 0);
}

/* Returns the number of bytes in the UTF-8 character at the beginning of S.

   The return value is 0 if S is empty, otherwise between 1 and 4. */
int
ss_first_mblen (struct substring s)
{
  return ss_at_mblen (s, 0);
}

/* Advances S past the UTF-8 character at its beginning.  Returns the Unicode
   code point that was skipped (in the range 0 to 0x10ffff), or UINT32_MAX if S
   was not modified because it was initially empty. */
ucs4_t
ss_get_mb (struct substring *s)
{
  if (s->length > 0)
    {
      ucs4_t uc;
      int n;

      n = u8_mbtouc (&uc, CHAR_CAST (const uint8_t *, s->string), s->length);
      s->string += n;
      s->length -= n;
      return uc;
    }
  else
    return UINT32_MAX;
}

/* Returns the character represented by the UTF-8 sequence starting OFS bytes
   into S.  The return value is either a Unicode code point in the range 0 to
   0x10ffff, or UINT32_MAX if OFS is past the last byte in S.

   (Returns 0xfffd if OFS points into the middle, not the beginning, of a UTF-8
   sequence.)  */
ucs4_t
ss_at_mb (struct substring s, size_t ofs)
{
  if (s.length > ofs)
    {
      ucs4_t uc;
      u8_mbtouc (&uc, CHAR_CAST (const uint8_t *, s.string + ofs),
                 s.length - ofs);
      return uc;
    }
  else
    return UINT32_MAX;
}

/* Returns the number of bytes represented by the UTF-8 sequence starting OFS
   bytes into S.  The return value is 0 if OFS is past the last byte in S,
   otherwise between 1 and 4. */
int
ss_at_mblen (struct substring s, size_t ofs)
{
  if (s.length > ofs)
    {
      ucs4_t uc;
      return u8_mbtouc (&uc, CHAR_CAST (const uint8_t *, s.string + ofs),
                        s.length - ofs);
    }
  else
    return 0;
}

/* Initializes ST as an empty string. */
void
ds_init_empty (struct string *st)
{
  st->ss = ss_empty ();
  st->capacity = 0;
}

/* Initializes ST with initial contents S. */
void
ds_init_string (struct string *st, const struct string *s)
{
  ds_init_substring (st, ds_ss (s));
}

/* Initializes ST with initial contents SS. */
void
ds_init_substring (struct string *st, struct substring ss)
{
  st->capacity = MAX (8, ss.length * 2);
  st->ss.string = xmalloc (st->capacity + 1);
  memcpy (st->ss.string, ss.string, ss.length);
  st->ss.length = ss.length;
}

/* Initializes ST with initial contents S. */
void
ds_init_cstr (struct string *st, const char *s)
{
  ds_init_substring (st, ss_cstr (s));
}

/* Frees ST. */
void
ds_destroy (struct string *st)
{
  if (st != NULL)
    {
      ss_dealloc (&st->ss);
      st->ss.string = NULL;
      st->ss.length = 0;
      st->capacity = 0;
    }
}

/* Swaps the contents of strings A and B. */
void
ds_swap (struct string *a, struct string *b)
{
  struct string tmp = *a;
  *a = *b;
  *b = tmp;
}

/* Helper function for ds_register_pool. */
static void
free_string (void *st_)
{
  struct string *st = st_;
  ds_destroy (st);
}

/* Arranges for ST to be destroyed automatically as part of
   POOL. */
void
ds_register_pool (struct string *st, struct pool *pool)
{
  pool_register (pool, free_string, st);
}

/* Cancels the arrangement for ST to be destroyed automatically
   as part of POOL. */
void
ds_unregister_pool (struct string *st, struct pool *pool)
{
  pool_unregister (pool, st);
}

/* Copies SRC into DST.
   DST and SRC may be the same string. */
void
ds_assign_string (struct string *dst, const struct string *src)
{
  ds_assign_substring (dst, ds_ss (src));
}

/* Replaces DST by SS.
   SS may be a substring of DST. */
void
ds_assign_substring (struct string *dst, struct substring ss)
{
  dst->ss.length = ss.length;
  ds_extend (dst, ss.length);
  memmove (dst->ss.string, ss.string, ss.length);
}

/* Replaces DST by null-terminated string SRC.  SRC may overlap
   with DST. */
void
ds_assign_cstr (struct string *dst, const char *src)
{
  ds_assign_substring (dst, ss_cstr (src));
}

/* Truncates ST to zero length. */
void
ds_clear (struct string *st)
{
  st->ss.length = 0;
}

/* Returns a substring that contains ST. */
struct substring
ds_ss (const struct string *st)
{
  return st->ss;
}

/* Returns a substring that contains CNT bytes from ST
   starting at position START.

   If START is greater than or equal to the length of ST, then
   the substring will be the empty string.  If START + CNT
   exceeds the length of ST, then the substring will only be
   ds_length(ST) - START bytes long. */
struct substring
ds_substr (const struct string *st, size_t start, size_t cnt)
{
  return ss_substr (ds_ss (st), start, cnt);
}

/* Returns a substring that contains the first CNT bytes in
   ST.  If CNT exceeds the length of ST, then the substring will
   contain all of ST. */
struct substring
ds_head (const struct string *st, size_t cnt)
{
  return ss_head (ds_ss (st), cnt);
}

/* Returns a substring that contains the last CNT bytes in
   ST.  If CNT exceeds the length of ST, then the substring will
   contain all of ST. */
struct substring
ds_tail (const struct string *st, size_t cnt)
{
  return ss_tail (ds_ss (st), cnt);
}

/* Ensures that ST can hold at least MIN_CAPACITY bytes plus a null
   terminator. */
void
ds_extend (struct string *st, size_t min_capacity)
{
  if (min_capacity > st->capacity)
    {
      st->capacity *= 2;
      if (st->capacity < min_capacity)
	st->capacity = 2 * min_capacity;

      st->ss.string = xrealloc (st->ss.string, st->capacity + 1);
    }
}

/* Shrink ST to the minimum capacity need to contain its content. */
void
ds_shrink (struct string *st)
{
  if (st->capacity != st->ss.length)
    {
      st->capacity = st->ss.length;
      st->ss.string = xrealloc (st->ss.string, st->capacity + 1);
    }
}

/* Truncates ST to at most LENGTH bytes long. */
void
ds_truncate (struct string *st, size_t length)
{
  ss_truncate (&st->ss, length);
}

/* Removes trailing bytes in TRIM_SET from ST.
   Returns number of bytes removed. */
size_t
ds_rtrim (struct string *st, struct substring trim_set)
{
  return ss_rtrim (&st->ss, trim_set);
}

/* Removes leading bytes in TRIM_SET from ST.
   Returns number of bytes removed. */
size_t
ds_ltrim (struct string *st, struct substring trim_set)
{
  size_t cnt = ds_span (st, trim_set);
  if (cnt > 0)
    ds_assign_substring (st, ds_substr (st, cnt, SIZE_MAX));
  return cnt;
}

/* Trims leading and trailing bytes in TRIM_SET from ST.
   Returns number of bytes removed. */
size_t
ds_trim (struct string *st, struct substring trim_set)
{
  size_t cnt = ds_rtrim (st, trim_set);
  return cnt + ds_ltrim (st, trim_set);
}

/* If the last byte in ST is C, removes it and returns true.
   Otherwise, returns false without modifying ST. */
bool
ds_chomp_byte (struct string *st, char c)
{
  return ss_chomp_byte (&st->ss, c);
}

/* If ST ends with SUFFIX, removes it and returns true.
   Otherwise, returns false without modifying ST. */
bool
ds_chomp (struct string *st, struct substring suffix)
{
  return ss_chomp (&st->ss, suffix);
}

/* Divides ST into tokens separated by any of the DELIMITERS.
   Each call replaces TOKEN by the next token in ST, or by an
   empty string if no tokens remain.  Returns true if a token was
   obtained, false otherwise.

   Before the first call, initialize *SAVE_IDX to 0.  Do not
   modify *SAVE_IDX between calls.

   ST divides into exactly one more tokens than it contains
   delimiters.  That is, a delimiter at the start or end of ST or
   a pair of adjacent delimiters yields an empty token, and the
   empty string contains a single token. */
bool
ds_separate (const struct string *st, struct substring delimiters,
             size_t *save_idx, struct substring *token)
{
  return ss_separate (ds_ss (st), delimiters, save_idx, token);
}

/* Divides ST into tokens separated by any of the DELIMITERS,
   merging adjacent delimiters so that the empty string is never
   produced as a token.  Each call replaces TOKEN by the next
   token in ST, or by an empty string if no tokens remain.
   Returns true if a token was obtained, false otherwise.

   Before the first call, initialize *SAVE_IDX to 0.  Do not
   modify *SAVE_IDX between calls. */
bool
ds_tokenize (const struct string *st, struct substring delimiters,
             size_t *save_idx, struct substring *token)
{
  return ss_tokenize (ds_ss (st), delimiters, save_idx, token);
}

/* Pad ST on the right with copies of PAD until ST is at least
   LENGTH bytes in size.  If ST is initially LENGTH
   bytes or longer, this is a no-op. */
void
ds_rpad (struct string *st, size_t length, char pad)
{
  if (length > st->ss.length)
    ds_put_byte_multiple (st, pad, length - st->ss.length);
}

/* Sets the length of ST to exactly NEW_LENGTH,
   either by truncating bytes from the end,
   or by padding on the right with PAD. */
void
ds_set_length (struct string *st, size_t new_length, char pad)
{
  if (st->ss.length < new_length)
    ds_rpad (st, new_length, pad);
  else
    st->ss.length = new_length;
}

/* Removes N bytes from ST starting at offset START. */
void
ds_remove (struct string *st, size_t start, size_t n)
{
  if (n > 0 && start < st->ss.length)
    {
      if (st->ss.length - start <= n)
        {
          /* All bytes at or beyond START are deleted. */
          st->ss.length = start;
        }
      else
        {
          /* Some bytes remain and must be shifted into
             position. */
          memmove (st->ss.string + st->ss.length,
                   st->ss.string + st->ss.length + n,
                   st->ss.length - start - n);
          st->ss.length -= n;
        }
    }
  else
    {
      /* There are no bytes to delete or no bytes at or
         beyond START, hence deletion is a no-op. */
    }
}

/* Returns true if ST is empty, false otherwise. */
bool
ds_is_empty (const struct string *st)
{
  return ss_is_empty (st->ss);
}

/* Returns the length of ST. */
size_t
ds_length (const struct string *st)
{
  return ss_length (ds_ss (st));
}

/* Returns the string data inside ST. */
char *
ds_data (const struct string *st)
{
  return ss_data (ds_ss (st));
}

/* Returns a pointer to the null terminator ST.
   This might not be an actual null byte unless ds_c_str() has
   been called since the last modification to ST. */
char *
ds_end (const struct string *st)
{
  return ss_end (ds_ss (st));
}

/* Returns the byte in position IDX in ST, as a value in the
   range of unsigned char.  Returns EOF if IDX is out of the
   range of indexes for ST. */
int
ds_at (const struct string *st, size_t idx)
{
  return ss_at (ds_ss (st), idx);
}

/* Returns the first byte in ST as a value in the range of
   unsigned char.  Returns EOF if ST is the empty string. */
int
ds_first (const struct string *st)
{
  return ss_first (ds_ss (st));
}

/* Returns the last byte in ST as a value in the range of
   unsigned char.  Returns EOF if ST is the empty string. */
int
ds_last (const struct string *st)
{
  return ss_last (ds_ss (st));
}

/* Returns true if ST ends with SUFFIX, false otherwise. */
bool
ds_ends_with (const struct string *st, struct substring suffix)
{
  return ss_ends_with (st->ss, suffix);
}

/* Returns the number of consecutive bytes at the beginning
   of ST that are in SKIP_SET. */
size_t
ds_span (const struct string *st, struct substring skip_set)
{
  return ss_span (ds_ss (st), skip_set);
}

/* Returns the number of consecutive bytes at the beginning
   of ST that are not in STOP_SET.  */
size_t
ds_cspan (const struct string *st, struct substring stop_set)
{
  return ss_cspan (ds_ss (st), stop_set);
}

/* Returns the position of the first occurrence of byte C in
   ST at or after position OFS, or SIZE_MAX if there is no such
   occurrence. */
size_t
ds_find_byte (const struct string *st, char c)
{
  return ss_find_byte (ds_ss (st), c);
}

/* Compares A and B and returns a strcmp()-type comparison
   result. */
int
ds_compare (const struct string *a, const struct string *b)
{
  return ss_compare (ds_ss (a), ds_ss (b));
}

/* Returns the position in ST that the byte at P occupies.
   P must point within ST or one past its end. */
size_t
ds_pointer_to_position (const struct string *st, const char *p)
{
  return ss_pointer_to_position (ds_ss (st), p);
}

/* Allocates and returns a null-terminated string that contains
   ST. */
char *
ds_xstrdup (const struct string *st)
{
  return ss_xstrdup (ds_ss (st));
}

/* Returns the allocation size of ST. */
size_t
ds_capacity (const struct string *st)
{
  return st->capacity;
}

/* Returns the value of ST as a null-terminated string. */
char *
ds_cstr (const struct string *st_)
{
  struct string *st = CONST_CAST (struct string *, st_);
  if (st->ss.string == NULL)
    ds_extend (st, 1);
  st->ss.string[st->ss.length] = '\0';
  return st->ss.string;
}

/* Returns the value of ST as a null-terminated string and then
   reinitialized ST as an empty string.  The caller must free the
   returned string with free(). */
char *
ds_steal_cstr (struct string *st)
{
  char *s = ds_cstr (st);
  ds_init_empty (st);
  return s;
}

/* Reads bytes from STREAM and appends them to ST, stopping
   after MAX_LENGTH bytes, after appending a newline, or
   after an I/O error or end of file was encountered, whichever
   comes first.  Returns true if at least one byte was added
   to ST, false if no bytes were read before an I/O error or
   end of file (or if MAX_LENGTH was 0).

   This function treats LF and CR LF sequences as new-line,
   translating each of them to a single '\n' in ST. */
bool
ds_read_line (struct string *st, FILE *stream, size_t max_length)
{
  size_t length;

  for (length = 0; length < max_length; length++)
    {
      int c = getc (stream);
      switch (c)
        {
        case EOF:
          return length > 0;

        case '\n':
          ds_put_byte (st, c);
          return true;

        case '\r':
          c = getc (stream);
          if (c == '\n')
            {
              /* CR followed by LF is special: translate to \n. */
              ds_put_byte (st, '\n');
              return true;
            }
          else
            {
              /* CR followed by anything else is just CR. */
              ds_put_byte (st, '\r');
              if (c == EOF)
                return true;
              ungetc (c, stream);
            }
          break;

        default:
          ds_put_byte (st, c);
        }
    }

  return length > 0;
}

/* Removes a comment introduced by `#' from ST,
   ignoring occurrences inside quoted strings. */
static void
remove_comment (struct string *st)
{
  char *cp;
  int quote = 0;

  for (cp = ds_data (st); cp < ds_end (st); cp++)
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
        ds_truncate (st, cp - ds_cstr (st));
        break;
      }
}

/* Reads a line from STREAM into ST, then preprocesses as follows:

   - Splices lines terminated with `\'.

   - Deletes comments introduced by `#' outside of single or double
     quotes.

   - Deletes trailing white space.

   Returns true if a line was successfully read, false on
   failure.  If LINE_NUMBER is non-null, then *LINE_NUMBER is
   incremented by the number of lines read. */
bool
ds_read_config_line (struct string *st, int *line_number, FILE *stream)
{
  ds_clear (st);
  do
    {
      if (!ds_read_line (st, stream, SIZE_MAX))
        return false;
      (*line_number)++;
      ds_rtrim (st, ss_cstr (CC_SPACES));
    }
  while (ds_chomp_byte (st, '\\'));

  remove_comment (st);
  return true;
}

/* Attempts to read SIZE * CNT bytes from STREAM and append them
   to ST.
   Returns true if all the requested data was read, false otherwise. */
bool
ds_read_stream (struct string *st, size_t size, size_t cnt, FILE *stream)
{
  if (size != 0)
    {
      size_t try_bytes = xtimes (cnt, size);
      if (size_in_bounds_p (xsum (ds_length (st), try_bytes)))
        {
          char *buffer = ds_put_uninit (st, try_bytes);
          size_t got_bytes = fread (buffer, 1, try_bytes, stream);
          ds_truncate (st, ds_length (st) - (try_bytes - got_bytes));
          return got_bytes == try_bytes;
        }
      else
        {
          errno = ENOMEM;
          return false;
        }
    }
  else
    return true;
}

/* Concatenates S onto ST. */
void
ds_put_cstr (struct string *st, const char *s)
{
  if (s != NULL)
    ds_put_substring (st, ss_cstr (s));
}

/* Concatenates SS to ST. */
void
ds_put_substring (struct string *st, struct substring ss)
{
  memcpy (ds_put_uninit (st, ss_length (ss)), ss_data (ss), ss_length (ss));
}

/* Returns ds_end(ST) and THEN increases the length by INCR. */
char *
ds_put_uninit (struct string *st, size_t incr)
{
  char *end;
  ds_extend (st, ds_length (st) + incr);
  end = ds_end (st);
  st->ss.length += incr;
  return end;
}

/* Moves the bytes in ST following offset OFS + OLD_LEN in ST to offset OFS +
   NEW_LEN and returns the byte at offset OFS.  The first min(OLD_LEN, NEW_LEN)
   bytes at the returned position are unchanged; if NEW_LEN > OLD_LEN then the
   following NEW_LEN - OLD_LEN bytes are initially indeterminate.

   The intention is that the caller should write NEW_LEN bytes at the returned
   position, to effectively replace the OLD_LEN bytes previously at that
   position. */
char *
ds_splice_uninit (struct string *st,
                  size_t ofs, size_t old_len, size_t new_len)
{
  if (new_len != old_len)
    {
      if (new_len > old_len)
        ds_extend (st, ds_length (st) + (new_len - old_len));
      memmove (ds_data (st) + (ofs + new_len),
               ds_data (st) + (ofs + old_len),
               ds_length (st) - (ofs + old_len));
      st->ss.length += new_len - old_len;
    }
  return ds_data (st) + ofs;
}

/* Formats FORMAT as a printf string and appends the result to ST. */
void
ds_put_format (struct string *st, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  ds_put_vformat (st, format, args);
  va_end (args);
}

/* Formats FORMAT as a printf string as if in the C locale and appends the result to ST. */
void
ds_put_c_format (struct string *st, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  ds_put_c_vformat (st, format, args);
  va_end (args);
}


/* Formats FORMAT as a printf string, using fmt_func (a snprintf like function) 
   and appends the result to ST. */
static void
ds_put_vformat_int (struct string *st, const char *format, va_list args_,
		    int (*fmt_func) (char *, size_t, const char *, va_list))
{
  int avail, needed;
  va_list args;

  va_copy (args, args_);
  avail = st->ss.string != NULL ? st->capacity - st->ss.length + 1 : 0;
  needed = fmt_func (st->ss.string + st->ss.length, avail, format, args);
  va_end (args);

  if (needed >= avail)
    {
      va_copy (args, args_);
      fmt_func (ds_put_uninit (st, needed), needed + 1, format, args);
      va_end (args);
    }
  else
    {
      /* Some old libc's returned -1 when the destination string
         was too short. */
      while (needed == -1)
        {
          ds_extend (st, (st->capacity + 1) * 2);
          avail = st->capacity - st->ss.length + 1;

          va_copy (args, args_);
          needed = fmt_func (ds_end (st), avail, format, args);
          va_end (args);
        }
      st->ss.length += needed;
    }
}


static int
vasnwrapper (char *str, size_t size,  const char *format, va_list ap)
{
  c_vasnprintf (str, &size, format, ap);
  return size;
}

/* Formats FORMAT as a printf string and appends the result to ST. */
void
ds_put_vformat (struct string *st, const char *format, va_list args_)
{
  ds_put_vformat_int (st, format, args_, vsnprintf);
}

/* Formats FORMAT as a printf string, as if in the C locale, 
   and appends the result to ST. */
void
ds_put_c_vformat (struct string *st, const char *format, va_list args_)
{
  ds_put_vformat_int (st, format, args_, vasnwrapper);
}

/* Appends byte CH to ST. */
void
ds_put_byte (struct string *st, int ch)
{
  ds_put_uninit (st, 1)[0] = ch;
}

/* Appends CNT copies of byte CH to ST. */
void
ds_put_byte_multiple (struct string *st, int ch, size_t cnt)
{
  memset (ds_put_uninit (st, cnt), ch, cnt);
}

/* Appends Unicode code point UC to ST in UTF-8 encoding. */
void
ds_put_unichar (struct string *st, ucs4_t uc)
{
  ds_extend (st, ds_length (st) + 6);
  st->ss.length += u8_uctomb (CHAR_CAST (uint8_t *, ds_end (st)), uc, 6);
}

/* If relocation has been enabled, replace ST,
   with its relocated version */
void
ds_relocate (struct string *st)
{
  const char *orig = ds_cstr (st);
  const char *rel = relocate (orig);

  if ( orig != rel)
    {
      ds_clear (st);
      ds_put_cstr (st, rel);
      /* The documentation for relocate says that casting away const
	and then freeing is appropriate ... */
      free (CONST_CAST (char *, rel));
    }
}




/* Operations on uint8_t "strings" */

/* Copies buffer SRC, of SRC_SIZE bytes, to DST, of DST_SIZE bytes.
   DST is truncated to DST_SIZE bytes or padded on the right with
   copies of PAD as needed. */
void
u8_buf_copy_rpad (uint8_t *dst, size_t dst_size,
		  const uint8_t *src, size_t src_size,
		  char pad)
{
  if (src_size >= dst_size)
    memmove (dst, src, dst_size);
  else
    {
      memmove (dst, src, src_size);
      memset (&dst[src_size], pad, dst_size - src_size);
    }
}
