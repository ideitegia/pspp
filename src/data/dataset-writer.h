/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2010 Free Software Foundation, Inc.

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

#ifndef DATASET_WRITER_H
#define DATASET_WRITER_H 1

#include <stdbool.h>

struct dictionary;
struct file_handle;
struct casewriter *dataset_writer_open (struct file_handle *,
                                        const struct dictionary *);

#endif /* dataset-writer.h */
