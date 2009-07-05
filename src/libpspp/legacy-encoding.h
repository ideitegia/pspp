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

#ifndef LIBPSPP_LEGACY_ENCODING
#define LIBPSPP_LEGACY_ENCODING 1

#include <stddef.h>
#include <libpspp/compiler.h>


#if 'A' == 0x41
#define  LEGACY_NATIVE "PSPP-LEGACY-ASCII"
#elif 'A' == 0xc1
#define  LEGACY_NATIVE "PSPP-LEGACY-EBCDIC"
#else
#error Cannot detect native character set.
#endif



void legacy_recode (const char *from, const char *src,
                    const char *to, char *dst, size_t);
char legacy_to_native (const char *from, char) PURE_FUNCTION;
char legacy_from_native (const char *to, char) PURE_FUNCTION;

#endif /* libpspp/legacy-encoding.h */
