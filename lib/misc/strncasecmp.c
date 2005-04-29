/* PSPP - computes sample statistics.
   Copyright (C) 1997, 1998 Free Software Foundation, Inc.
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

#include <ctype.h>

int
strncasecmp (const char *s1, const char *s2, size_t n)
{
  size_t index;

  for (index = 0; index < n; index++)
    {
      if (tolower ((unsigned char) s1[index])
          != tolower ((unsigned char) s2[index]))
	return (((unsigned const char *)s1)[index] 
		- ((unsigned const char *)s2)[index]);
      if (s1[index] == 0)
	return 0;
    }
  return 0;
}
