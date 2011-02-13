/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010, 2011 Free Software Foundation, Inc.

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

#ifndef I18N_H
#define I18N_H

#include <stdbool.h>
#include <unistr.h>

void  i18n_done (void);
void  i18n_init (void);

#define UTF8 "UTF-8"

/* The encoding of literal strings in PSPP source code, as seen at execution
   time.  In fact this is likely to be some extended ASCII encoding, such as
   UTF-8 or ISO-8859-1, but ASCII is adequate for our purposes. */
#define C_ENCODING "ASCII"

struct pool;

char recode_byte (const char *to, const char *from, char);

char *recode_string (const char *to, const char *from,
                     const char *text, int len);
char *recode_string_pool (const char *to, const char *from,
			  const char *text, int length, struct pool *);
struct substring recode_substring_pool (const char *to, const char *from,
                                        struct substring text, struct pool *);

size_t recode_string_len (const char *to, const char *from,
                          const char *text, int len);

char *utf8_encoding_trunc (const char *, const char *encoding,
                           size_t max_len);
size_t utf8_encoding_trunc_len (const char *, const char *encoding,
                                size_t max_len);

char *utf8_encoding_concat (const char *head, const char *tail,
                            const char *encoding, size_t max_len);
size_t utf8_encoding_concat_len (const char *head, const char *tail,
                                 const char *encoding, size_t max_len);

char *utf8_to_filename (const char *filename);
char *filename_to_utf8 (const char *filename);

bool valid_encoding (const char *enc);

char get_system_decimal (void);

const char * get_default_encoding (void);
void set_default_encoding (const char *enc);

bool set_encoding_from_locale (const char *loc);

const char *uc_name (ucs4_t uc, char buffer[16]);

#endif /* i18n.h */
