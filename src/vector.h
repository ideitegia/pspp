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

#if !vector_h
#define vector_h 1

/* Represents a vector as created by the VECTOR transformation. */
struct vector
  {
    int index;			/* Index into vec[]. */
    char name[9];		/* Name. */
    struct variable **v;	/* Vector of variables. */
    int nv;			/* Number of variables. */
  };

extern struct vector *vec;
extern int nvec;

struct vector *find_vector (const char *name);

#endif /* !vector_h */
