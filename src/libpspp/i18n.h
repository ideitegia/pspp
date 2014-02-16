/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010, 2011, 2012, 2014 Free Software Foundation, Inc.

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
int recode_pedantically (const char *to, const char *from,
                         struct substring text, struct pool *,
                         struct substring *out);

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

unsigned int utf8_hash_case_bytes (const char *, size_t n, unsigned int basis);
unsigned int utf8_hash_case_string (const char *, unsigned int basis);
int utf8_strcasecmp (const char *, const char *);
int utf8_strncasecmp (const char *, size_t, const char *, size_t);
char *utf8_to_upper (const char *);
char *utf8_to_lower (const char *);

/* Information about character encodings. */

/* ISO C defines a set of characters that a C implementation must support at
   runtime, called the C basic execution character set, which consists of the
   following characters:

       A B C D E F G H I J K L M
       N O P Q R S T U V W X Y Z
       a b c d e f g h i j k l m
       n o p q r s t u v w x y z
       0 1 2 3 4 5 6 7 8 9
       ! " # % & ' ( ) * + , - . / :
       ; < = > ? [ \ ] ^ _ { | } ~
       space \a \b \r \n \t \v \f \0

   The following is true of every member of the C basic execution character
   set in all "reasonable" encodings:

       1. Every member of the C basic character set is encoded.

       2. Every member of the C basic character set has the same width in
          bytes, called the "unit width".  Most encodings have a unit width of
          1 byte, but UCS-2 and UTF-16 have a unit width of 2 bytes and UCS-4
          and UTF-32 have a unit width of 4 bytes.

       3. In a stateful encoding, the encoding of members of the C basic
          character set does not vary with shift state.

       4. When a string is read unit-by-unit, a unit that has the encoded value
          of a member of the C basic character set, EXCEPT FOR THE DECIMAL
          DIGITS, always represents that member.  That is, if the encoding has
          multi-unit characters, the units that encode the C basic character
          set are never part of a multi-unit character.

          The exception for decimal digits is due to GB18030, which uses
          decimal digits as part of multi-byte encodings.

   All 8-bit and wider encodings that I have been able to find follow these
   rules.  7-bit and narrower encodings (e.g. UTF-7) do not.  I'm not too
   concerned about that. */

#include <stdbool.h>

/* Maximum width of a unit, in bytes.  UTF-32 with 4-byte units is the widest
   that I am aware of. */
#define MAX_UNIT 4

/* Information about an encoding. */
struct encoding_info
  {
    /* Encoding name.  IANA says character set names may be up to 40 US-ASCII
       characters. */
    char name[41];

    /* True if this encoding has a unit width of 1 byte, and every character
       used in ASCII text files has the same value in this encoding. */
    bool is_ascii_compatible;

    /* True if this encoding has a unit width of 1 byte and appears to be
       EBCDIC-based.  */
    bool is_ebcdic_compatible;

    /* Character information. */
    int unit;                   /* Unit width, in bytes. */
    char cr[MAX_UNIT];          /* \r in encoding, 'unit' bytes long. */
    char lf[MAX_UNIT];          /* \n in encoding, 'unit' bytes long. */
    char space[MAX_UNIT];       /* ' ' in encoding, 'unit' bytes long. */
  };

bool get_encoding_info (struct encoding_info *, const char *name);
bool is_encoding_ascii_compatible (const char *encoding);
bool is_encoding_ebcdic_compatible (const char *encoding);
bool is_encoding_supported (const char *encoding);

bool is_encoding_utf8 (const char *encoding);

/* Database of encodings, by language or region. */

struct encoding_category
  {
    const char *category;       /* e.g. "Arabic" or "Western European". */
    const char **encodings;     /* Encodings within the category. */
    size_t n_encodings;         /* Number of encodings in category. */
  };

struct encoding_category *get_encoding_categories (void);
size_t get_n_encoding_categories (void);

#endif /* i18n.h */
