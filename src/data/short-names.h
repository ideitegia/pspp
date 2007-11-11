/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

/* Short names for variables.

   PSPP allows variable names to be up to 64 bytes long, but the
   system and portable file formats require that each variable
   have a unique name no more than 8 bytes long, called its
   "short name".  Furthermore, each "very long" string variable
   that is more than 255 bytes long has to be divided into
   multiple long string variables within that limit, and each of
   these segments must also have its own unique short name.

   The function in this module generates short names for
   variables with long names or that have very long string
   width. */

#ifndef DATA_SHORT_NAMES_H
#define DATA_SHORT_NAMES_H 1

struct dictionary;

/* Maximum length of a short name, in bytes. */
#define SHORT_NAME_LEN 8

void short_names_assign (struct dictionary *);

#endif /* data/short-names.h */
