/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010 Free Software Foundation, Inc.

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

#ifndef LANGUAGE_STATS_FREQ_H
#define LANGUAGE_STATS_FREQ_H 1

#include "data/value.h"
#include "libpspp/hmap.h"

/* Frequency table entry. */
struct freq
  {
    struct hmap_node hmap_node; /* Element in hash table. */
    union value value;          /* The value. */
    double count;		/* The number of occurrences of the value. */
  };

void freq_hmap_destroy (struct hmap *, int width);

struct freq *freq_hmap_search (struct hmap *, const union value *, int width,
                               size_t hash);
struct freq *freq_hmap_insert (struct hmap *, const union value *, int width,
                               size_t hash);

struct freq **freq_hmap_sort (struct hmap *, int width);
struct freq *freq_hmap_extract (struct hmap *);

#endif /* language/stats/freq.h */
