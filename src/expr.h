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

#if !expr_h
#define expr_h 1

/* Expression parsing flags. */
enum expr_type
  {
    EXPR_ANY = 0,               /* Any type. */
    EXPR_BOOLEAN = 1,           /* Must be numeric; coerce to Boolean. */
    EXPR_NUMERIC = 2,           /* Must be numeric result type. */
    EXPR_STRING = 3,            /* Must be string result type. */
    EXPR_ERROR = 4,             /* Indicates an error. */
    EXPR_NO_OPTIMIZE = 0x1000   /* May be set in expr_parse()
                                   argument to disable optimization. */
  };

struct expression;
struct ccase;
union value;

struct expression *expr_parse (enum expr_type);
enum expr_type expr_get_type (const struct expression *);
double expr_evaluate (const struct expression *, const struct ccase *,
                      int case_num, union value *);
void expr_free (struct expression *);
void expr_debug_print_postfix (const struct expression *);

#endif /* expr.h */
