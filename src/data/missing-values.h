/* PSPP - computes sample statistics.
   Copyright (C) 2005 Free Software Foundation, Inc.
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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#if !missing_values_h
#define missing_values_h 1

#include <stdbool.h>
#include "value.h"

/* Types of user-missing values.
   Invisible--use access functions defined below instead. */
enum mv_type 
  {
    MV_NONE = 0,                /* No user-missing values. */
    MV_1 = 1,                   /* One user-missing value. */
    MV_2 = 2,                   /* Two user-missing values. */
    MV_3 = 3,                   /* Three user-missing values. */
    MV_RANGE = 4,               /* A range of user-missing values. */
    MV_RANGE_1 = 5              /* A range plus an individual value. */
  };

/* Missing values.
   Opaque--use access functions defined below. */
struct missing_values 
  {
    unsigned type;              /* Number and type of missing values. */
    int width;                  /* 0=numeric, otherwise string width. */
    union value values[3];      /* Missing values.  [y,z] are the range. */
  };


void mv_init (struct missing_values *, int width);
void mv_set_type(struct missing_values *mv, enum mv_type type);

void mv_copy (struct missing_values *, const struct missing_values *);
bool mv_is_empty (const struct missing_values *);
int mv_get_width (const struct missing_values *);

bool mv_add_value (struct missing_values *, const union value *);
bool mv_add_str (struct missing_values *, const char[]);
bool mv_add_num (struct missing_values *, double);
bool mv_add_num_range (struct missing_values *, double low, double high);

bool mv_has_value (const struct missing_values *);
void mv_pop_value (struct missing_values *, union value *);
void mv_peek_value (const struct missing_values *mv, union value *v, int idx);
void mv_replace_value (struct missing_values *mv, const union value *v, int idx);

int  mv_n_values (const struct missing_values *mv);


bool mv_has_range (const struct missing_values *);
void mv_pop_range (struct missing_values *, double *low, double *high);
void mv_peek_range (const struct missing_values *, double *low, double *high);

bool mv_is_resizable (const struct missing_values *, int width);
void mv_resize (struct missing_values *, int width);

typedef bool mv_is_missing_func (const struct missing_values *,
                                 const union value *);

/* Is a value system or user missing? */
bool mv_is_value_missing (const struct missing_values *, const union value *);
bool mv_is_num_missing (const struct missing_values *, double);
bool mv_is_str_missing (const struct missing_values *, const char[]);

/* Is a value user missing? */
bool mv_is_value_user_missing (const struct missing_values *,
                               const union value *);
bool mv_is_num_user_missing (const struct missing_values *, double);
bool mv_is_str_user_missing (const struct missing_values *, const char[]);

/* Is a value system missing? */
bool mv_is_value_system_missing (const struct missing_values *,
                                 const union value *);

#endif /* missing-values.h */
