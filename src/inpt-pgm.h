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

#if !inpt_pgm_h
#define inpt_pgm_h 1

/* Bitmasks to indicate variable type. */
enum
  {
    INP_MASK = 03,		/* 2#11. */
    
    INP_NUMERIC = 0,		/* Numeric. */
    INP_STRING = 01,		/* String. */
    
    INP_RIGHT = 0,		/* Ordinary. */
    INP_LEFT = 02		/* Scratch or LEAVE. */
  };

extern unsigned char *inp_init;
extern size_t inp_init_size;

#endif /* !inpt_pgm_h */
