/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#if !expr_h
#define expr_h 1

#include <stddef.h>

/* Expression parsing flags. */
enum expr_type
  {
    EXPR_NUMBER = 0xf000,       /* Number. */
    EXPR_STRING,                /* String. */
    EXPR_BOOLEAN,               /* Boolean (number limited to 0, 1, SYSMIS). */
  };

struct dictionary;
struct expression;
struct ccase;
struct pool;
union value;
struct dataset ;
struct lexer ;

struct expression *expr_parse (struct lexer *lexer, struct dataset *, enum expr_type);
struct expression *expr_parse_pool (struct lexer *, 
				    struct pool *,
				    struct dataset *,
                                    enum expr_type);
void expr_free (struct expression *);

struct dataset;
double expr_evaluate_num (struct expression *, const struct ccase *,
                          int case_idx);
void expr_evaluate_str (struct expression *, const struct ccase *,
                        int case_idx, char *dst, size_t dst_size);

#endif /* expr.h */
