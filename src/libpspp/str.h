/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2012, 2014 Free Software Foundation, Inc.

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

#if !str_h
#define str_h 1

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unitypes.h>

#include "compiler.h"
#include "memcasecmp.h"
#include "xstrndup.h"
#include "xvasprintf.h"

/* Miscellaneous. */

void buf_reverse (char *, size_t);
int buf_compare_case (const char *, const char *, size_t);
int buf_compare_rpad (const char *, size_t, const char *, size_t);
void buf_copy_lpad (char *, size_t, const char *, size_t, char pad);
void buf_copy_rpad (char *, size_t, const char *, size_t, char pad);
void buf_copy_str_lpad (char *, size_t, const char *, char pad);
void buf_copy_str_rpad (char *, size_t, const char *, char pad);

int str_compare_rpad (const char *, const char *);
void str_copy_rpad (char *, size_t, const char *);
void str_copy_trunc (char *, size_t, const char *);
void str_copy_buf_trunc (char *, size_t, const char *, size_t);
void str_uppercase (char *);
void str_lowercase (char *);

bool str_format_26adic (unsigned long int number, bool uppercase,
                        char buffer[], size_t);

void *mempset (void *, int, size_t);

/* Common character classes for use with substring and string functions. */

#define CC_SPACES " \t\v\r\n"
#define CC_DIGITS "0123456789"
#define CC_XDIGITS "0123456789abcdefABCDEF"
#define CC_LETTERS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define CC_ALNUM CC_LETTERS CC_DIGITS

/* Substrings. */
struct substring
  {
    char *string;
    size_t length;
  };

#define SS_EMPTY_INITIALIZER {NULL, 0}
#define SS_LITERAL_INITIALIZER(LITERAL)                 \
        {(char *) LITERAL, (sizeof LITERAL) - 1}

/* Constructors.
   These functions do not allocate any memory, so the substrings
   they create should not normally be destroyed. */
static inline struct substring ss_empty (void);
static inline struct substring ss_cstr (const char *);
static inline struct substring ss_buffer (const char *, size_t);
struct substring ss_substr (struct substring, size_t start, size_t);
struct substring ss_head (struct substring, size_t);
struct substring ss_tail (struct substring, size_t);

/* Constructors and destructor that allocate and deallocate
   memory. */
struct pool;
void ss_alloc_substring (struct substring *, struct substring);
void ss_alloc_uninit (struct substring *, size_t);
void ss_realloc (struct substring *, size_t);
void ss_alloc_substring_pool (struct substring *, struct substring,
                              struct pool *);
void ss_alloc_uninit_pool (struct substring *, size_t, struct pool *);
void ss_dealloc (struct substring *);

/* Mutators.
   Functions that advance the beginning of a string should not be
   used if a substring is to be deallocated. */
void ss_truncate (struct substring *, size_t);
size_t ss_rtrim (struct substring *, struct substring trim_set);
size_t ss_ltrim (struct substring *, struct substring trim_set);
void ss_trim (struct substring *, struct substring trim_set);
bool ss_chomp_byte (struct substring *, char);
bool ss_chomp (struct substring *, struct substring);
bool ss_separate (struct substring src, struct substring delimiters,
                  size_t *save_idx, struct substring *token);
bool ss_tokenize (struct substring src, struct substring delimiters,
                  size_t *save_idx, struct substring *token);
void ss_advance (struct substring *, size_t);
bool ss_match_byte (struct substring *, char);
int ss_match_byte_in (struct substring *, struct substring);
bool ss_match_string (struct substring *, const struct substring);
int ss_get_byte (struct substring *);
size_t ss_get_bytes (struct substring *, size_t cnt, struct substring *);
bool ss_get_until (struct substring *, char delimiter, struct substring *);
size_t ss_get_long (struct substring *, long *);

/* Inspectors. */
bool ss_is_empty (struct substring);
size_t ss_length (struct substring);
char *ss_data (struct substring);
char *ss_end (struct substring);
int ss_at (struct substring, size_t idx);
int ss_first (struct substring);
int ss_last (struct substring);
bool ss_ends_with (struct substring, struct substring suffix);
size_t ss_span (struct substring, struct substring skip_set);
size_t ss_cspan (struct substring, struct substring stop_set);
size_t ss_find_byte (struct substring, char);
int ss_compare (struct substring, struct substring);
int ss_compare_case (struct substring, struct substring);
int ss_equals (struct substring, struct substring);
int ss_equals_case (struct substring, struct substring);
size_t ss_pointer_to_position (struct substring, const char *);
char *ss_xstrdup (struct substring);

/* UTF-8. */
ucs4_t ss_first_mb (struct substring);
int ss_first_mblen (struct substring);
ucs4_t ss_get_mb (struct substring *);
ucs4_t ss_at_mb (struct substring, size_t ofs);
int ss_at_mblen (struct substring, size_t ofs);

/* Variable length strings. */

struct string
  {
    struct substring ss;

    size_t capacity;    /* Allocated capacity, not including one
                           extra byte allocated for null terminator. */
  };

#define DS_EMPTY_INITIALIZER {SS_EMPTY_INITIALIZER, 0}

/* Constructors, destructors. */
void ds_init_empty (struct string *);
void ds_init_string (struct string *, const struct string *);
void ds_init_substring (struct string *, struct substring);
void ds_init_cstr (struct string *, const char *);
void ds_destroy (struct string *);
void ds_swap (struct string *, struct string *);

/* Pools. */
struct pool;
void ds_register_pool (struct string *, struct pool *);
void ds_unregister_pool (struct string *, struct pool *);

/* Replacement. */
void ds_assign_string (struct string *, const struct string *);
void ds_assign_substring (struct string *, struct substring);
void ds_assign_cstr (struct string *, const char *);

/* Shrink, extend. */
void ds_clear (struct string *);
void ds_extend (struct string *, size_t);
void ds_shrink (struct string *);
void ds_truncate (struct string *, size_t);

/* Padding, trimming. */
size_t ds_rtrim (struct string *, struct substring trim_set);
size_t ds_ltrim (struct string *, struct substring trim_set);
size_t ds_trim (struct string *, struct substring trim_set);
bool ds_chomp_byte (struct string *, char);
bool ds_chomp (struct string *, struct substring);
bool ds_separate (const struct string *src, struct substring delimiters,
                  size_t *save_idx, struct substring *token);
bool ds_tokenize (const struct string *src, struct substring delimiters,
                  size_t *save_idx, struct substring *token);
void ds_rpad (struct string *, size_t length, char pad);
void ds_set_length (struct string *, size_t new_length, char pad);
void ds_remove (struct string *, size_t start, size_t n);

/* Extracting substrings. */
struct substring ds_ss (const struct string *);
struct substring ds_substr (const struct string *, size_t start, size_t);
struct substring ds_head (const struct string *, size_t);
struct substring ds_tail (const struct string *, size_t);

/* Inspectors. */
bool ds_is_empty (const struct string *);
size_t ds_length (const struct string *);
char *ds_data (const struct string *);
char *ds_end (const struct string *);
int ds_at (const struct string *, size_t idx);
int ds_first (const struct string *);
int ds_last (const struct string *);
bool ds_ends_with (const struct string *, struct substring suffix);
size_t ds_span (const struct string *, struct substring skip_set);
size_t ds_cspan (const struct string *, struct substring stop_set);
size_t ds_find_byte (const struct string *, char);
int ds_compare (const struct string *, const struct string *);
size_t ds_pointer_to_position (const struct string *, const char *);
char *ds_xstrdup (const struct string *);

size_t ds_capacity (const struct string *);
char *ds_cstr (const struct string *);
char *ds_steal_cstr (struct string *);

/* File input. */
bool ds_read_line (struct string *, FILE *, size_t max_length);
bool ds_read_config_line (struct string *, int *line_number, FILE *);
bool ds_read_stream (struct string *, size_t size, size_t cnt, FILE *stream);

/* Append. */
void ds_put_byte (struct string *, int ch);
void ds_put_byte_multiple (struct string *, int ch, size_t);
void ds_put_unichar (struct string *, ucs4_t uc);
void ds_put_cstr (struct string *, const char *);
void ds_put_substring (struct string *, struct substring);
void ds_put_vformat (struct string *st, const char *, va_list)
     PRINTF_FORMAT (2, 0);
void ds_put_c_vformat (struct string *st, const char *, va_list)
     PRINTF_FORMAT (2, 0);

void ds_put_format (struct string *, const char *, ...)
     PRINTF_FORMAT (2, 3);
void ds_put_c_format (struct string *, const char *, ...)
     PRINTF_FORMAT (2, 3);

char *ds_put_uninit (struct string *st, size_t incr);

char *ds_splice_uninit (struct string *, size_t ofs, size_t old_len,
                        size_t new_len);

/* Other */
/* calls relocate from gnulib on ST */
void ds_relocate (struct string *st);


void u8_buf_copy_rpad (uint8_t *dst, size_t dst_size,
		       const uint8_t *src, size_t src_size,
		       char pad);

static inline struct substring
ss_empty (void)
{
  struct substring ss;
  ss.string = NULL;
  ss.length = 0;
  return ss;
}

/* Returns a substring whose contents are the given C-style
   string CSTR. */
static inline struct substring
ss_cstr (const char *cstr)
{
  return ss_buffer (cstr, strlen (cstr));
}

/* Returns a substring whose contents are the CNT characters in
   BUFFER. */
static inline struct substring
ss_buffer (const char *buffer, size_t cnt)
{
  struct substring ss;
  ss.string = (char *) buffer;
  ss.length = cnt;
  return ss;
}

#endif /* str_h */
