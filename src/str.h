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

#if !str_h
#define str_h 1

/* Headers and miscellaneous. */

#include <stdarg.h>
#include <stdio.h>

#if STDC_HEADERS
  #include <string.h>
#else
  #ifndef HAVE_STRCHR 
    #define strchr index
    #define strrchr rindex
  #endif

  char *strchr (), *strrchr ();
#endif

#if !HAVE_STRTOK_R
  char *strtok_r (char *, const char *, char **);
#endif

#if !HAVE_STPCPY && !__linux__
  char *stpcpy (char *dest, const char *src);
#endif

#if !HAVE_STRCASECMP
  int strcasecmp (const char *s1, const char *s2);
#endif

#if !HAVE_STRNCASECMP
  int strncasecmp (const char *s1, const char *s2, size_t n);
#endif

#if !HAVE_MEMMEM
  void *memmem (const void *haystack, size_t haystack_len,
	        const void *needle, size_t needle_len);
#endif

/* sprintf() wrapper functions for convenience. */

/* spprintf() calls sprintf() and returns the address of the null
   terminator in the resulting string.  It should be portable the way
   it's been implemented. */
#if __GNUC__
  #if HAVE_GOOD_SPRINTF
    #define spprintf(BUF, FORMAT, ARGS...)			\
	    ((BUF) + sprintf ((BUF), (FORMAT) , ## ARGS))
  #else
    #define spprintf(BUF, FORMAT, ARGS...)		\
	    ({ sprintf ((BUF), (FORMAT) , ## ARGS); 	\
               strchr ((BUF), 0); })
  #endif
#else /* Not GNU C. */
  char *spprintf (char *buf, const char *format, ...);
#endif /* Not GNU C. */

/* nsprintf() calls sprintf() and returns the strlen() of the
   resulting string.  It should be portable the way it's been
   implemented. */
#if __GNUC__
  #if HAVE_GOOD_SPRINTF
    #define nsprintf(BUF, FORMAT, ARGS...)		\
	    (sprintf ((BUF), (FORMAT) , ## ARGS))
    #define nvsprintf(BUF, FORMAT, ARGS)		\
	    (vsprintf ((BUF), (FORMAT), (ARGS)))
  #else /* Not good sprintf(). */
    #define nsprintf(BUF, FORMAT, ARGS...)		\
	    ({						\
	      char *pbuf = BUF;				\
	      sprintf ((pbuf), (FORMAT) , ## ARGS);	\
	      strlen (pbuf);				\
	    })
    #define nvsprintf(BUF, FORMAT, ARGS)		\
	    ({						\
	      char *pbuf = BUF;				\
	      vsprintf ((pbuf), (FORMAT), (ARGS));	\
	      strlen (pbuf);				\
	    })
  #endif /* Not good sprintf(). */
#else /* Not GNU C. */
  #if HAVE_GOOD_SPRINTF
    #define nsprintf sprintf
    #define nvsprintf vsprintf
  #else /* Not good sprintf(). */
    int nsprintf (char *buf, const char *format, ...);
    int nvsprintf (char *buf, const char *format, va_list args);
  #endif /* Not good sprintf(). */
#endif /* Not GNU C. */

#if !HAVE_GETLINE
long getline (char **lineptr, size_t *n, FILE *stream);
#endif

#if !HAVE_GETDELIM
long getdelim (char **lineptr, size_t * n, int delimiter, FILE * stream);
#endif

/* Miscellaneous. */

void mm_reverse (void *, size_t);
char *mm_find_reverse (const char *, size_t, const char *, size_t);

int st_compare_pad (const char *, int, const char *, int);
char *st_spaces (int);
void st_bare_pad_copy (char *dest, const char *src, size_t n);
void st_bare_pad_len_copy (char *dest, const char *src, size_t n, size_t len);
void st_pad_copy (char *dest, const char *src, size_t n);

/* Lengthed strings. */
struct len_string 
  {
    char *string;
    size_t length;
  };

struct pool;
void ls_create (struct pool *, struct len_string *, const char *);
void ls_create_buffer (struct pool *, struct len_string *,
		       const char *, size_t len);
void ls_init (struct len_string *, const char *, size_t);
void ls_shallow_copy (struct len_string *, const struct len_string *);
void ls_destroy (struct pool *, struct len_string *);

void ls_null (struct len_string *);
int ls_null_p (const struct len_string *);
int ls_empty_p (const struct len_string *);

size_t ls_length (const struct len_string *);
char *ls_value (const struct len_string *);
char *ls_end (const struct len_string *);

/* Dynamic strings. */

struct string
  {
    struct pool *pool;
    size_t length;
    size_t size;
    char *string;
  };

void ds_create (struct pool *, struct string *, const char *);
void ds_init (struct pool *, struct string *, size_t size);
void ds_replace (struct string *, const char *);
void ds_destroy (struct string *);
void ds_clear (struct string *);
void ds_extend (struct string *, size_t min_size);
void ds_shrink (struct string *);
void ds_truncate (struct string *, size_t length);

size_t ds_length (const struct string *);
char *ds_value (const struct string *);
char *ds_end (const struct string *);
size_t ds_size (const struct string *);

struct file_locator;
int ds_getline (struct string *st, FILE *stream);
int ds_get_config_line (FILE *, struct string *, struct file_locator *);
void ds_putchar (struct string *, int ch);
void ds_concat (struct string *, const char *);
void ds_concat_buffer (struct string *, const char *buf, size_t len);
void ds_printf (struct string *, const char *, ...)
     __attribute__ ((format (printf, 2, 3)));

#if __GNUC__ > 1
extern inline void
ds_putchar (struct string *st, int ch)
{
  if (st->length == st->size)
    ds_extend (st, st->length + 1);
  st->string[st->length++] = ch;
}

extern inline size_t
ds_length (const struct string *st)
{
  return st->length;
}

extern inline char *
ds_value (const struct string *st)
{
  ((char *) st->string)[st->length] = '\0';
  return st->string;
}

extern inline char *
ds_end (const struct string *st)
{
  return st->string + st->length;
}
#endif

#endif /* str_h */
