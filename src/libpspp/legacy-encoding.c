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

#include <config.h>

#include <libpspp/legacy-encoding.h>
#include <libpspp/i18n.h>
#include <stdlib.h>

char
legacy_to_native (const char *from, char c)
{
  char x;
  char *s = recode_string (LEGACY_NATIVE, from, &c, 1);
  x = s[0];
  free (s);
  return x;
}

char
legacy_from_native (const char *to, char c)
{
  char x;
  char *s = recode_string (to, LEGACY_NATIVE, &c, 1);
  x = s[0];
  free (s);
  return x;
}
