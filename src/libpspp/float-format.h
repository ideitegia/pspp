/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2011 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_FLOAT_FORMAT_H
#define LIBPSPP_FLOAT_FORMAT_H 1

#include <stdbool.h>
#include <stddef.h>
#include "libpspp/compiler.h"

/* A floating-point format. */
enum float_format
  {
    /* IEEE 754 formats. */
    FLOAT_IEEE_SINGLE_LE,          /* 32 bit, little endian. */
    FLOAT_IEEE_SINGLE_BE,          /* 32 bit, big endian. */
    FLOAT_IEEE_DOUBLE_LE,          /* 64 bit, little endian. */
    FLOAT_IEEE_DOUBLE_BE,          /* 64 bit, big endian. */

    /* VAX formats. */
    FLOAT_VAX_F,                   /* 32 bit VAX F format. */
    FLOAT_VAX_D,                   /* 64 bit VAX D format. */
    FLOAT_VAX_G,                   /* 64 bit VAX G format. */

    /* IBM z architecture (390) hexadecimal formats. */
    FLOAT_Z_SHORT,                 /* 32 bit format. */
    FLOAT_Z_LONG,                  /* 64 bit format. */

    /* Formats useful for testing. */
    FLOAT_FP,                      /* Neutral intermediate format. */
    FLOAT_HEX,                     /* C99 hexadecimal floating constant. */

#ifdef WORDS_BIGENDIAN
    FLOAT_NATIVE_FLOAT = FLOAT_IEEE_SINGLE_BE,
    FLOAT_NATIVE_DOUBLE = FLOAT_IEEE_DOUBLE_BE,
    FLOAT_NATIVE_32_BIT = FLOAT_IEEE_SINGLE_BE,
    FLOAT_NATIVE_64_BIT = FLOAT_IEEE_DOUBLE_BE
#else
    FLOAT_NATIVE_FLOAT = FLOAT_IEEE_SINGLE_LE,
    FLOAT_NATIVE_DOUBLE = FLOAT_IEEE_DOUBLE_LE,
    FLOAT_NATIVE_32_BIT = FLOAT_IEEE_SINGLE_LE,
    FLOAT_NATIVE_64_BIT = FLOAT_IEEE_DOUBLE_LE
#endif
  };

void float_convert (enum float_format, const void *,
                    enum float_format, void *);

double float_get_double (enum float_format, const void *);

size_t float_get_size (enum float_format) PURE_FUNCTION;

int float_identify (double expected_value, const void *, size_t,
                    enum float_format *best_guess);

double float_get_lowest (void);

#endif /* float-format.h */
