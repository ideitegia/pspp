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

#if !sort_h
#define sort_h 1

#include "vfm.h"

/* Sort direction. */
enum sort_direction
  {
    SRT_ASCEND,			/* A, B, C, ..., X, Y, Z. */
    SRT_DESCEND			/* Z, Y, X, ..., C, B, A. */
  };

/* SORT CASES input program. */
struct sort_cases_pgm 
  {
    int ref_cnt;                        /* Reference count. */
                        
    struct variable **vars;             /* Variables to sort. */
    enum sort_direction *dirs;          /* Sort directions. */
    int var_cnt;                        /* Number of variables to sort. */

    struct internal_sort *isrt;         /* Internal sort output. */
    struct external_sort *xsrt;         /* External sort output. */
    size_t case_size;                   /* Number of bytes in case. */
  };

/* SORT CASES programmatic interface. */

typedef int read_sort_output_func (const struct ccase *, void *aux);

struct sort_cases_pgm *parse_sort (void);
int sort_cases (struct sort_cases_pgm *, int separate);
void read_sort_output (struct sort_cases_pgm *,
                       read_sort_output_func, void *aux);
void destroy_sort_cases_pgm (struct sort_cases_pgm *);

#endif /* !sort_h */
