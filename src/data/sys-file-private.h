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

#ifndef DATA_SYS_FILE_PRIVATE_H
#define DATA_SYS_FILE_PRIVATE_H 1

/* This nonsense is required for SPSS compatibility. */

#define MIN_VERY_LONG_STRING 256
#define EFFECTIVE_LONG_STRING_LENGTH (MIN_VERY_LONG_STRING - 4)

int sfm_width_to_bytes (int width);

#endif /* data/sys-file-private.h */
