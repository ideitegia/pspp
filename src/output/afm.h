/* PSPP - a program for statistical analysis.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#ifndef AFM_H
#define AFM_H 1

#include <stddef.h>
#include <libpspp/str.h>

/* Metrics for a single character.  */
struct afm_character
  {
    int code;                   /* Non-negative character code, -1 if none. */
    const char *name;           /* Character name, if any. */
    int width;			/* Width. */
    int ascent;			/* Height above baseline, never negative. */
    int descent;                /* Depth below baseline, never negative. */

    /* Pairwise kerning data for this character in the first
       position, other characters in the second position. */
    struct afm_kern_pair *kern_pairs;
    size_t kern_pair_cnt;

    /* Ligature data for this character in the first position,
       other characters in the second position. */
    struct afm_ligature *ligatures;
    size_t ligature_cnt;
  };

struct afm *afm_open (const char *file_name);
void afm_close (struct afm *);

int afm_get_ascent (const struct afm *);
int afm_get_descent (const struct afm *);
const char *afm_get_findfont_name (const struct afm *);

const struct afm_character *afm_get_character (const struct afm *,
                                               int code);
const struct afm_character *afm_get_ligature (const struct afm_character *,
                                              const struct afm_character *);
int afm_get_kern_adjustment (const struct afm_character *,
                             const struct afm_character *);

size_t afm_encode_string (const struct afm *,
                          const struct afm_character **, size_t,
                          struct string *);

#endif /* afm.h */
