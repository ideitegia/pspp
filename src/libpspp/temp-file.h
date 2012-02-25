/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

/* Functions for temporary files that honor $TMPDIR. */

#ifndef LIBPSPP_TEMP_FILE_H
#define LIBPSPP_TEMP_FILE_H 1

#include <stdio.h>

FILE *create_temp_file (void);
void close_temp_file (FILE *);
const char *temp_dir_name (void);


#endif /* libpspp/ext-array.h */
