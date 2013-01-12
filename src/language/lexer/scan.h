/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2013 Free Software Foundation, Inc.

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

#ifndef SCAN_H
#define SCAN_H 1

#include "language/lexer/segment.h"
#include "libpspp/str.h"

struct token;

/* PSPP syntax scanning.

   PSPP divides traditional "lexical analysis" or "tokenization" into two
   phases: a lower-level phase called "segmentation" and a higher-level phase
   called "scanning".  segment.h provides declarations for the segmentation
   phase.  This header file contains declarations for the scanning phase.

   Scanning accepts as input a stream of segments, which are UTF-8 strings each
   labeled with a segment type.  It outputs a stream of "scan tokens", which
   are the same as the tokens used by the PSPP parser with a few additional
   types.
*/

#define SCAN_TYPES                              \
    SCAN_TYPE(BAD_HEX_LENGTH)                   \
    SCAN_TYPE(BAD_HEX_DIGIT)                    \
                                                \
    SCAN_TYPE(BAD_UNICODE_LENGTH)               \
    SCAN_TYPE(BAD_UNICODE_DIGIT)                \
    SCAN_TYPE(BAD_UNICODE_CODE_POINT)           \
                                                \
    SCAN_TYPE(EXPECTED_QUOTE)                   \
    SCAN_TYPE(EXPECTED_EXPONENT)                \
    SCAN_TYPE(UNEXPECTED_DOT)                   \
    SCAN_TYPE(UNEXPECTED_CHAR)                  \
                                                \
    SCAN_TYPE(SKIP)

/* Types of scan tokens.

   Scan token types are a superset of enum token_type.  Only the additional
   scan token types are defined here, so see the definition of enum token_type
   for the others. */
enum scan_type
  {
#define SCAN_TYPE(TYPE) SCAN_##TYPE,
    SCAN_FIRST = 255,
    SCAN_TYPES
    SCAN_LAST
#undef SCAN_TYPE
  };

const char *scan_type_to_string (enum scan_type);
bool is_scan_type (enum scan_type);

/* A scanner.  Opaque. */
struct scanner
  {
    unsigned char state;
    unsigned char substate;
  };

/* scanner_push() return type. */
enum scan_result
  {
    /* Complete token. */
    SCAN_DONE,                  /* Token successfully scanned. */
    SCAN_MORE,                  /* More segments needed to scan token. */

    /* Incomplete token. */
    SCAN_BACK,                  /* Done, but go back to saved position too. */
    SCAN_SAVE                   /* Need more segments, and save position. */
  };

void scanner_init (struct scanner *, struct token *);
enum scan_result scanner_push (struct scanner *, enum segment_type,
                               struct substring, struct token *);

/* A simplified lexer for handling syntax in a string. */

struct string_lexer
  {
    const char *input;
    size_t length;
    size_t offset;
    struct segmenter segmenter;
  };

void string_lexer_init (struct string_lexer *, const char *input,
                        enum segmenter_mode);
bool string_lexer_next (struct string_lexer *, struct token *);

#endif /* scan.h */
