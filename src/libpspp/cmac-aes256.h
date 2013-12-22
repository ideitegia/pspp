/* PSPP - a program for statistical analysis.
   Copyright (C) 2013 Free Software Foundation, Inc.

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

#ifndef CMAC_AES256_H
#define CMAC_AES256_H 1

#include <stddef.h>
#include <stdint.h>

void cmac_aes256(const uint8_t key[32],
                 const void *data, size_t size,
                 uint8_t cmac[16]);

#endif /* libpspp/cmac-aes256.h */
