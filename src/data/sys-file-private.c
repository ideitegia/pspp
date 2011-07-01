/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2010 Free Software Foundation, Inc.

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

#include "data/sys-file-private.h"

#include "data/dictionary.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/misc.h"

#include "gl/c-strcase.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

/* Number of bytes really stored in each segment of a very long
   string variable. */
#define REAL_VLS_CHUNK 255

/* Number of bytes per segment by which the amount of space for
   very long string variables is allocated. */
#define EFFECTIVE_VLS_CHUNK 252

/* Returns true if WIDTH is a very long string width,
   false otherwise. */
static bool
is_very_long (int width)
{
  return width >= 256;
}

/* Returns the smaller of A or B.
   (Defined as a function to avoid evaluating A or B more than
   once.) */
static int
min_int (int a, int b)
{
  return MIN (a, b);
}

/* Returns the larger of A or B.
   (Defined as a function to avoid evaluating A or B more than
   once.) */
static int
max_int (int a, int b)
{
  return MAX (a, b);
}

/* Returns the number of bytes of uncompressed case data used for
   writing a variable of the given WIDTH to a system file.  All
   required space is included, including trailing padding and
   internal padding. */
static int
sfm_width_to_bytes (int width)
{
  int bytes;

  assert (width >= 0);

  if (width == 0)
    bytes = 8;
  else if (!is_very_long (width))
    bytes = width;
  else
    {
      int chunks = width / EFFECTIVE_VLS_CHUNK;
      int remainder = width % EFFECTIVE_VLS_CHUNK;
      bytes = remainder + (chunks * ROUND_UP (REAL_VLS_CHUNK, 8));
    }
  return ROUND_UP (bytes, 8);
}

/* Returns the number of 8-byte units (octs) used to write data
   for a variable of the given WIDTH. */
int
sfm_width_to_octs (int width)
{
  return sfm_width_to_bytes (width) / 8;
}

/* Returns the number of "segments" used for writing case data
   for a variable of the given WIDTH.  A segment is a physical
   variable in the system file that represents some piece of a
   logical variable as seen by a PSPP user.  Only very long
   string variables have more than one segment. */
int
sfm_width_to_segments (int width)
{
  assert (width >= 0);

  return !is_very_long (width) ? 1 : DIV_RND_UP (width, EFFECTIVE_VLS_CHUNK);
}

/* Returns the width to allocate to the given SEGMENT within a
   variable of the given WIDTH.  A segment is a physical variable
   in the system file that represents some piece of a logical
   variable as seen by a PSPP user. */
int
sfm_segment_alloc_width (int width, int segment)
{
  assert (segment < sfm_width_to_segments (width));

  return (!is_very_long (width) ? width
          : segment < sfm_width_to_segments (width) - 1 ? 255
          : width - segment * EFFECTIVE_VLS_CHUNK);
}

/* Returns the number of bytes to allocate to the given SEGMENT
   within a variable of the given width.  This is the same as
   sfm_segment_alloc_width, except that a numeric value takes up
   8 bytes despite having a width of 0. */
static int
sfm_segment_alloc_bytes (int width, int segment)
{
  assert (segment < sfm_width_to_segments (width));
  return (width == 0 ? 8
          : ROUND_UP (sfm_segment_alloc_width (width, segment), 8));
}

/* Returns the number of bytes in the given SEGMENT within a
   variable of the given WIDTH that are actually used to store
   data.  For a numeric value (WIDTH of 0), this is 8 bytes; for
   a string value less than 256 bytes wide, it is WIDTH bytes.
   For very long string values, the calculation is more
   complicated and ranges between 255 bytes for the first segment
   to as little as 0 bytes for final segments. */
static int
sfm_segment_used_bytes (int width, int segment)
{
  assert (segment < sfm_width_to_segments (width));
  return (width == 0 ? 8
          : !is_very_long (width) ? width
          : max_int (0, min_int (width - REAL_VLS_CHUNK * segment,
                                 REAL_VLS_CHUNK)));
}

/* Returns the number of bytes at the end of the given SEGMENT
   within a variable of the given WIDTH that are not used for
   data; that is, the number of bytes that must be padded with
   data that a reader ignores. */
static int
sfm_segment_padding (int width, int segment)
{
  return (sfm_segment_alloc_bytes (width, segment)
          - sfm_segment_used_bytes (width, segment));
}

/* Returns the byte offset of the start of the given SEGMENT
   within a variable of the given WIDTH.  The first segment
   starts at offset 0; only very long string variables have any
   other segments. */
static int
sfm_segment_offset (int width, int segment)
{
  assert (segment < sfm_width_to_segments (width));
  return min_int (REAL_VLS_CHUNK * segment, width);
}

/* Returns the byte offset of the start of the given SEGMENT
   within a variable of the given WIDTH, given the (incorrect)
   assumption that there are EFFECTIVE_VLS_CHUNK bytes per
   segment.  (Use of this function is questionable at best.) */
int
sfm_segment_effective_offset (int width, int segment)
{
  assert (segment < sfm_width_to_segments (width));
  return EFFECTIVE_VLS_CHUNK * segment;
}

/* Creates and initializes an array of struct sfm_vars that
   describe how a case drawn from dictionary DICT is laid out in
   a system file.  Returns the number of segments in a case.  A
   segment is a physical variable in the system file that
   represents some piece of a logical variable as seen by a PSPP
   user.

   The array is allocated with malloc and stored in *SFM_VARS,
   and its number of elements is stored in *SFM_VAR_CNT.  The
   caller is responsible for freeing it when it is no longer
   needed. */
int
sfm_dictionary_to_sfm_vars (const struct dictionary *dict,
                            struct sfm_var **sfm_vars, size_t *sfm_var_cnt)
{
  size_t var_cnt = dict_get_var_cnt (dict);
  size_t segment_cnt;
  size_t i;

  /* Estimate the number of sfm_vars that will be needed.
     We might not need all of these, because very long string
     variables can have segments that are all padding, which do
     not need sfm_vars of their own. */
  segment_cnt = 0;
  for (i = 0; i < var_cnt; i++)
    {
      const struct variable *v = dict_get_var (dict, i);
      segment_cnt += sfm_width_to_segments (var_get_width (v));
    }

  /* Compose the sfm_vars. */
  *sfm_vars = xnmalloc (segment_cnt, sizeof **sfm_vars);
  *sfm_var_cnt = 0;
  for (i = 0; i < var_cnt; i++)
    {
      const struct variable *dv = dict_get_var (dict, i);
      int width = var_get_width (dv);
      int j;

      for (j = 0; j < sfm_width_to_segments (width); j++)
        {
          int used_bytes = sfm_segment_used_bytes (width, j);
          int padding = sfm_segment_padding (width, j);
          struct sfm_var *sv;
          if (used_bytes != 0)
            {
              sv = &(*sfm_vars)[(*sfm_var_cnt)++];
              sv->var_width = width;
              sv->segment_width = width == 0 ? 0 : used_bytes;
              sv->case_index = var_get_case_index (dv);
              sv->offset = sfm_segment_offset (width, j);
              sv->padding = padding;
            }
          else
            {
              /* Segment is all padding.  Just add it to the
                 previous segment. */
              sv = &(*sfm_vars)[*sfm_var_cnt - 1];
              sv->padding += padding;
            }
          assert ((sv->segment_width + sv->padding) % 8 == 0);
        }
    }

  return segment_cnt;
}

/* Given the name of an encoding, returns the codepage number to use in the
   'character_code' member of the machine integer info record for writing a
   system file. */
int
sys_get_codepage_from_encoding (const char *name)
{
  const struct sys_encoding *e;

  for (e = sys_codepage_name_to_number; e->name != NULL; e++)
    if (!c_strcasecmp (name, e->name))
      return e->number;

  return 0;
}

/* Given a codepage number from the 'character_code' member of the machine
   integer info record in a system file, returns a corresponding encoding name.
   Most encodings have multiple aliases; the one returned is the one that would
   be used in the character encoding record. */
const char *
sys_get_encoding_from_codepage (int codepage)
{
  const struct sys_encoding *e;

  for (e = sys_codepage_number_to_name; e->name != NULL; e++)
    if (codepage == e->number)
      return e->name;

  return NULL;
}
