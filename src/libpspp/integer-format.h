/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
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

#ifndef LIBPSPP_INTEGER_FORMAT_H
#define LIBPSPP_INTEGER_FORMAT_H 1

#include <byteswap.h>
#include <stdint.h>

#include <libpspp/str.h>

/* An integer format. */
enum integer_format
  {
    INTEGER_MSB_FIRST,          /* Big-endian: MSB at lowest address. */
    INTEGER_LSB_FIRST,          /* Little-endian: LSB at lowest address. */
    INTEGER_VAX,                /* VAX-endian: little-endian 16-bit words
                                   in big-endian order. */

    /* Native endianness. */
#ifdef WORDS_BIGENDIAN
    INTEGER_NATIVE = INTEGER_MSB_FIRST
#else
    INTEGER_NATIVE = INTEGER_LSB_FIRST
#endif
  };

void integer_convert (enum integer_format, const void *, 
                      enum integer_format, void *,
                      size_t);
uint64_t integer_get (enum integer_format, const void *, size_t);
void integer_put (uint64_t, enum integer_format, void *, size_t);

bool integer_identify (uint64_t expected_value, const void *, size_t,
                       enum integer_format *);

#endif /* libpspp/integer-format.h */
