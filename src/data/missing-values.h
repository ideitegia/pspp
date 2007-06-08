/* PSPP - computes sample statistics.
   Copyright (C) 2005 Free Software Foundation, Inc.

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

/* Missing values.
   Opaque--use access functions defined below. */
struct missing_values
  {
    int type;                   /* Types of missing values, one of MVT_*. */
    int width;                  /* 0=numeric, otherwise string width. */
    union value values[3];      /* Missing values.  [y,z] are the range. */
  };

/* Classes of missing values. */
enum mv_class
  {
    MV_NEVER = 0,               /* Never considered missing. */
    MV_USER = 1,                /* Missing if value is user-missing. */
    MV_SYSTEM = 2,              /* Missing if value is system-missing. */
    MV_ANY = MV_USER | MV_SYSTEM /* Missing if it is user or system-missing. */
  };

void mv_init (struct missing_values *, int width);
void mv_clear (struct missing_values *);

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

/* Is a value missing? */
bool mv_is_value_missing (const struct missing_values *, const union value *,
                          enum mv_class);
bool mv_is_num_missing (const struct missing_values *, double, enum mv_class);
bool mv_is_str_missing (const struct missing_values *, const char[],
                        enum mv_class);

#endif /* missing-values.h */
