/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2009, 2013 Free Software Foundation, Inc.

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

/* User-missing values.

   struct missing_values is an opaque type that represents a set
   of user-missing values associated with a variable.  Valid sets
   of missing values depend on variable width:

        - Numeric variables may have up to 3 discrete numeric
          user-missing values, or a range of numeric values, or a
          range plus one discrete value.

        - String variables may have up to 3 discrete string
          user-missing values.  (However, for long string
          variables all bytes after the first MV_MAX_STRING must
          be spaces.)
*/

#ifndef DATA_MISSING_VALUES_H
#define DATA_MISSING_VALUES_H 1

#include <stdbool.h>
#include "data/value.h"

struct pool;

/* Missing values for long string variables after the first
   MV_MAX_STRING bytes must be all spaces. */
#define MV_MAX_STRING 8

/* Missing values.
   Opaque--use access functions defined below. */
struct missing_values
  {
    int type;                   /* Types of missing values, one of MVT_*. */
    int width;                  /* 0=numeric, otherwise string width. */
    union value values[3];      /* Missing values.  [1], [2] are the range. */
  };

/* Classes of missing values. */
enum mv_class
  {
    MV_NEVER = 0,               /* Never considered missing. */
    MV_USER = 1,                /* Missing if value is user-missing. */
    MV_SYSTEM = 2,              /* Missing if value is system-missing. */
    MV_ANY = MV_USER | MV_SYSTEM /* Missing if it is user or system-missing. */
  };

/* Is a value missing? */
bool mv_is_value_missing (const struct missing_values *, const union value *,
                          enum mv_class);
bool mv_is_num_missing (const struct missing_values *, double, enum mv_class);
bool mv_is_str_missing (const struct missing_values *, const uint8_t[],
                        enum mv_class);

/* Initializing missing value sets. */
void mv_init (struct missing_values *, int width);
void mv_init_pool (struct pool *pool, struct missing_values *, int width);
void mv_destroy (struct missing_values *);
void mv_copy (struct missing_values *, const struct missing_values *);
void mv_clear (struct missing_values *);

/* Changing width of a missing value set. */
bool mv_is_resizable (const struct missing_values *, int width);
void mv_resize (struct missing_values *, int width);

/* Basic property inspection. */
bool mv_is_acceptable (const union value *, int width);
bool mv_is_empty (const struct missing_values *);
int mv_get_width (const struct missing_values *);

/* Inspecting discrete values. */
int mv_n_values (const struct missing_values *);
bool mv_has_value (const struct missing_values *);
const union value *mv_get_value (const struct missing_values *, int idx);

/* Inspecting ranges. */
bool mv_has_range (const struct missing_values *);
void mv_get_range (const struct missing_values *, double *low, double *high);

/* Adding and modifying discrete values. */
bool mv_add_value (struct missing_values *, const union value *);
bool mv_add_str (struct missing_values *, const uint8_t[], size_t len);
bool mv_add_num (struct missing_values *, double);
void mv_pop_value (struct missing_values *, union value *);
bool mv_replace_value (struct missing_values *, const union value *, int idx);

/* Adding and modifying ranges. */
bool mv_add_range (struct missing_values *, double low, double high);
void mv_pop_range (struct missing_values *, double *low, double *high);

#endif /* data/missing-values.h */
