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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#if !cases_h
#define cases_h 1

/* Vectors. */

/* A vector of longs. */
struct long_vec
  {
    long *vec;			/* Contents. */
    int n;			/* Number of elements. */
    int m;			/* Number of elements room is allocated for. */
  };

struct variable;

void vec_init (struct long_vec *);
void vec_clear (struct long_vec *);
void vec_insert (struct long_vec *, long);
void vec_delete (struct long_vec *, long a, long b);
void devector (const struct variable *);
void envector (const struct variable *);

#endif /* !cases_h */
