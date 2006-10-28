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

#ifndef LIBPSPP_LEGACY_ENCODING
#define LIBPSPP_LEGACY_ENCODING 1

#include <stddef.h>
#include <libpspp/compiler.h>

/* A legacy character encoding.
   This exists only to handle the specific legacy EBCDIC-to-ASCII
   recoding that MODE=360 file handles perform. */
enum legacy_encoding
  {
    LEGACY_ASCII,         /* ASCII or similar character set. */
    LEGACY_EBCDIC,        /* IBM EBCDIC character set. */

    /* Native character set. */
#if 'A' == 0x41
    LEGACY_NATIVE = LEGACY_ASCII
#elif 'A' == 0xc1
    LEGACY_NATIVE = LEGACY_EBCDIC
#else
#error Cannot detect native character set.
#endif
  };

void legacy_recode (enum legacy_encoding, const char *src,
                    enum legacy_encoding, char *dst, size_t);
char legacy_to_native (enum legacy_encoding from, char) PURE_FUNCTION;
char legacy_from_native (enum legacy_encoding to, char) PURE_FUNCTION;

#endif /* libpspp/legacy-encoding.h */
