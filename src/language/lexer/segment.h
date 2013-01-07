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

#ifndef SEGMENT_H
#define SEGMENT_H 1

#include <stdbool.h>
#include <stddef.h>
#include "libpspp/prompt.h"

/* PSPP syntax segmentation.

   PSPP divides traditional "lexical analysis" or "tokenization" into two
   phases: a lower-level phase called "segmentation" and a higher-level phase
   called "scanning".  This header file provides declarations for the
   segmentation phase.  scan.h contains declarations for the scanning phase.

   Segmentation accepts a stream of UTF-8 bytes as input.  It outputs a label
   (a segment type) for each byte or contiguous sequence of bytes in the input.
   It also, in a few corner cases, outputs zero-width segments that label the
   boundary between a pair of bytes in the input.

   Some segment types correspond directly to tokens; for example, an
   "identifier" segment (SEG_IDENTIFIER) becomes an identifier token (T_ID)
   later in lexical analysis.  Other segments contribute to tokens but do not
   correspond diectly; for example, multiple quoted string segments
   (SEG_QUOTED_STRING) separated by spaces (SEG_SPACES) and  "+" punctuators
   (SEG_PUNCT) may be combined to form a single string token (T_STRING).
   Still other segments are ignored (e.g. SEG_SPACES) or trigger special
   behavior such as error messages later in tokenization
   (e.g. SEG_EXPECTED_QUOTE).
*/

/* Segmentation mode.

   This corresponds to the syntax mode for which a syntax file is intended.
   This is the only configuration setting for a segmenter. */
enum segmenter_mode
  {
    /* Try to interpret input correctly regardless of whether it is written
       for interactive or batch mode. */
    SEG_MODE_AUTO,

    /* Interactive or batch syntax mode. */
    SEG_MODE_INTERACTIVE,
    SEG_MODE_BATCH
  };

#define SEG_TYPES                               \
    SEG_TYPE(NUMBER)                            \
    SEG_TYPE(QUOTED_STRING)                     \
    SEG_TYPE(HEX_STRING)                        \
    SEG_TYPE(UNICODE_STRING)                    \
    SEG_TYPE(UNQUOTED_STRING)                   \
    SEG_TYPE(RESERVED_WORD)                     \
    SEG_TYPE(IDENTIFIER)                        \
    SEG_TYPE(PUNCT)                             \
                                                \
    SEG_TYPE(SHBANG)                            \
    SEG_TYPE(SPACES)                            \
    SEG_TYPE(COMMENT)                           \
    SEG_TYPE(NEWLINE)                           \
                                                \
    SEG_TYPE(COMMENT_COMMAND)                   \
    SEG_TYPE(DO_REPEAT_COMMAND)                 \
    SEG_TYPE(INLINE_DATA)                       \
                                                \
    SEG_TYPE(START_DOCUMENT)                    \
    SEG_TYPE(DOCUMENT)                          \
                                                \
    SEG_TYPE(START_COMMAND)                     \
    SEG_TYPE(SEPARATE_COMMANDS)                 \
    SEG_TYPE(END_COMMAND)                       \
    SEG_TYPE(END)                               \
                                                \
    SEG_TYPE(EXPECTED_QUOTE)                    \
    SEG_TYPE(EXPECTED_EXPONENT)                 \
    SEG_TYPE(UNEXPECTED_DOT)                    \
    SEG_TYPE(UNEXPECTED_CHAR)

/* Types of segments. */
enum segment_type
  {
#define SEG_TYPE(NAME) SEG_##NAME,
    SEG_TYPES
#undef SEG_TYPE
  };

/* Number of segment types. */
#define SEG_TYPE(NAME) + 1
enum { SEG_N_TYPES = SEG_TYPES };
#undef SEG_TYPE

const char *segment_type_to_string (enum segment_type);

/* A segmenter.  Opaque. */
struct segmenter
  {
    unsigned char state;
    unsigned char substate;
    unsigned char mode;
  };

void segmenter_init (struct segmenter *, enum segmenter_mode);

enum segmenter_mode segmenter_get_mode (const struct segmenter *);

int segmenter_push (struct segmenter *, const char *input, size_t n,
                    enum segment_type *);

enum prompt_style segmenter_get_prompt (const struct segmenter *);

#endif /* segment.h */
