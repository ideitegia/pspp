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

#if !data_in_h
#define data_in_h 1

#include <stddef.h>
#include "format.h"

/* Flags. */
enum
  {
    DI_IGNORE_ERROR = 01,	/* Don't report errors to the user. */
  };

/* Information about parsing one data field. */
struct data_in
  {
    const unsigned char *s;	/* Source start. */
    const unsigned char *e;	/* Source end. */

    union value *v;		/* Destination. */

    int flags;			/* Zero or more of DI_*. */
    int f1, f2;			/* Columns the field was taken from. */
    struct fmt_spec format;	/* Format specification to use. */
  };

int data_in (struct data_in *);

void data_in_finite_line (struct data_in *di, const char *line, size_t len,
			  int fc, int lc);

#endif /* data-in.h */
