/* PSPP - computes sample statistics.
   Copyright (C) 2007, 2011 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 3 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#ifndef DATA_VAL_TYPE_H
#define DATA_VAL_TYPE_H 1

#include <float.h>
#include <stdbool.h>
#include "libpspp/float-format.h"

/* Special numeric values. */
#define SYSMIS (-DBL_MAX)               /* System-missing value. */
#define LOWEST (float_get_lowest ())    /* Smallest nonmissing finite value. */
#define HIGHEST DBL_MAX                 /* Largest finite value. */

/* Maximum length of a string variable. */
#define MAX_STRING 32767

/* Value type. */
enum val_type
  {
    VAL_NUMERIC,              /* A numeric value. */
    VAL_STRING                /* A string value. */
  };

/* Returns true if VAL_TYPE is a valid value type. */
static inline bool
val_type_is_valid (enum val_type val_type)
{
  return val_type == VAL_NUMERIC || val_type == VAL_STRING;
}

/* Returns the value type for the given WIDTH. */
static inline enum val_type
val_type_from_width (int width)
{
  return width != 0 ? VAL_STRING : VAL_NUMERIC;
}

#endif /* data/val-type.h */
