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

#ifndef ZIP_PRIVATE_H
#define ZIP_PRIVATE_H 1

#define MAGIC_EOCD ( (uint32_t) 0x06054b50) /* End of directory */
#define MAGIC_SOCD ( (uint32_t) 0x02014b50) /* Start of directory */
#define MAGIC_LHDR ( (uint32_t) 0x04034b50) /* Local Header */
#define MAGIC_DDHD ( (uint32_t) 0x08074b50) /* Data Descriptor Header */

#endif

