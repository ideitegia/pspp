/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
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

#ifndef SORT_PRS_H
#define SORT_PRS_H

#include <config.h>
#include <stdbool.h>

struct variable;
struct dictionary;

struct sort_criteria *sort_parse_criteria (const struct dictionary *,
                                           struct variable ***, int *,
                                           bool *saw_direction,
					   const int *terminators
					   );

void sort_destroy_criteria (struct sort_criteria *criteria) ;


#endif /* SORT_PRS_H */
