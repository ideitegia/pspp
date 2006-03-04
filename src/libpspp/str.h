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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#if !str_h
#define str_h 1

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "memcasecmp.h"
#include "memmem.h"
#include "snprintf.h"
#include "stpcpy.h"
#include "strcase.h"
#include "strftime.h"
#include "strstr.h"
#include "strtok_r.h"
#include "vsnprintf.h"
#include "xvasprintf.h"

#ifndef HAVE_STRCHR
#define strchr index
#endif
#ifndef HAVE_STRRCHR
#define strrchr rindex
#endif

/* Miscellaneous. */

void buf_reverse (char *, size_t);
char *buf_find_reverse (const char *, size_t, const char *, size_t);
int buf_compare_case (const char *, const char *, size_t);
int buf_compare_rpad (const char *, size_t, const char *, size_t);
void buf_copy_rpad (char *, size_t, const char *, size_t);
void buf_copy_str_lpad (char *, size_t, const char *);
void buf_copy_str_rpad (char *, size_t, const char *);

int str_compare_rpad (const char *, const char *);
void str_copy_rpad (char *, size_t, const char *);
void str_copy_trunc (char *, size_t, const char *);
void str_copy_buf_trunc (char *, size_t, const char *, size_t);
void str_uppercase (char *);
void str_lowercase (char *);

/* Fixed-length strings. */
struct fixed_string 
  {
    char *string;
    size_t length;
  };

void ls_create (struct fixed_string *, const char *);
void ls_create_buffer (struct fixed_string *,
		       const char *, size_t len);
void ls_init (struct fixed_string *, const char *, size_t);
void ls_shallow_copy (struct fixed_string *, const struct fixed_string *);
void ls_destroy (struct fixed_string *);

void ls_null (struct fixed_string *);
int ls_null_p (const struct fixed_string *);
int ls_empty_p (const struct fixed_string *);

size_t ls_length (const struct fixed_string *);
char *ls_c_str (const struct fixed_string *);
char *ls_end (const struct fixed_string *);

#if __GNUC__ > 1
extern inline size_t
ls_length (const struct fixed_string *st)
{
  return st->length;
}

extern inline char *
ls_c_str (const struct fixed_string *st)
{
  return st->string;
}

extern inline char *
ls_end (const struct fixed_string *st)
{
  return st->string + st->length;
}
#endif

/* Variable length strings. */

struct string
  {
    size_t length;      /* Length, not including a null terminator. */
    size_t capacity;    /* Allocated capacity, not including one
                           extra byte allocated for null terminator. */
    char *string;       /* String data, not necessarily null
                           terminated. */
  };

/* Constructors, destructors. */
void ds_create (struct string *, const char *);
void ds_init (struct string *, size_t);
void ds_destroy (struct string *);
void ds_swap (struct string *, struct string *);

/* Copy, shrink, extend. */
void ds_replace (struct string *, const char *);
void ds_clear (struct string *);
void ds_extend (struct string *, size_t);
void ds_shrink (struct string *);
void ds_truncate (struct string *, size_t);
void ds_rpad (struct string *, size_t length, char pad);
int ds_rtrim_spaces (struct string *);
bool ds_chomp (struct string *, char);

/* Inspectors. */
bool ds_is_empty (const struct string *);
size_t ds_length (const struct string *);
char *ds_c_str (const struct string *);
char *ds_data (const struct string *);
char *ds_end (const struct string *);
size_t ds_capacity (const struct string *);
int ds_first (const struct string *);
int ds_last (const struct string *);

/* File input. */
struct file_locator;
int ds_gets (struct string *, FILE *);
int ds_get_config_line (FILE *, struct string *, struct file_locator *);

/* Append. */
void ds_putc (struct string *, int ch);
void ds_puts (struct string *, const char *);
void ds_concat (struct string *, const char *, size_t);
void ds_vprintf (struct string *st, const char *, va_list);
void ds_printf (struct string *, const char *, ...)
     PRINTF_FORMAT (2, 3);

#if __GNUC__ > 1
extern inline void
ds_putc (struct string *st, int ch)
{
  if (st->length == st->capacity)
    ds_extend (st, st->length + 1);
  st->string[st->length++] = ch;
}

extern inline size_t
ds_length (const struct string *st)
{
  return st->length;
}

extern inline char *
ds_c_str (const struct string *st)
{
  ((char *) st->string)[st->length] = '\0';
  return st->string;
}

extern inline char *
ds_data (const struct string *st)
{
  return st->string;
}

extern inline char *
ds_end (const struct string *st)
{
  return st->string + st->length;
}
#endif

#define nsprintf sprintf
#define nvsprintf vsprintf

/* Formats FORMAT into DST, as with sprintf(), and returns the
   address of the terminating null written to DST. */
static inline char *
spprintf (char *dst, const char *format, ...) 
{
  va_list args;
  int count;

  va_start (args, format);
  count = nvsprintf (dst, format, args);
  va_end (args);

  return dst + count;
}

#endif /* str_h */
