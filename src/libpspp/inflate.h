/* PSPP - a program for statistical analysis.
   Copyright (C) 2011 Free Software Foundation, Inc.

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

#ifndef INFLATE_H
#define INFLATE_H 1

#include <stddef.h>
#include <stdbool.h>

struct zip_member ;

bool inflate_init (struct zip_member *zm);

int inflate_read (struct zip_member *zm, void *buf, size_t n);

void inflate_finish (struct zip_member *zm);


#endif
