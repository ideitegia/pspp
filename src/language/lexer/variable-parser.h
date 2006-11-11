/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
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

#ifndef VARIABLE_PARSER_H
#define VARIABLE_PARSER_H 1

#include <stdbool.h>
#include <stddef.h>

struct pool;
struct dictionary;
struct var_set;
struct variable;
struct lexer ;

struct var_set *var_set_create_from_dict (const struct dictionary *d);
struct var_set *var_set_create_from_array (struct variable *const *var,
                                           size_t);

size_t var_set_get_cnt (const struct var_set *vs);
struct variable *var_set_get_var (const struct var_set *vs, size_t idx);
struct variable *var_set_lookup_var (const struct var_set *vs,
                                     const char *name);
bool var_set_lookup_var_idx (const struct var_set *vs, const char *name,
                             size_t *idx);
void var_set_destroy (struct var_set *vs);

/* Variable parsers. */

enum
  {
    PV_NONE = 0,		/* No options. */
    PV_SINGLE = 0001,		/* Restrict to a single name or TO use. */
    PV_DUPLICATE = 0002,	/* Don't merge duplicates. */
    PV_APPEND = 0004,		/* Append to existing list. */
    PV_NO_DUPLICATE = 0010,	/* Error on duplicates. */
    PV_NUMERIC = 0020,		/* Vars must be numeric. */
    PV_STRING = 0040,		/* Vars must be string. */
    PV_SAME_TYPE = 00100,	/* All vars must be the same type. */
    PV_NO_SCRATCH = 00200 	/* Disallow scratch variables. */
  };

struct variable *parse_variable (struct lexer *, const struct dictionary *);
bool parse_variables (struct lexer *, const struct dictionary *, struct variable ***, size_t *,
                     int opts);
bool parse_variables_pool (struct lexer *, struct pool *, const struct dictionary *,
                          struct variable ***, size_t *, int opts);
bool parse_var_set_vars (struct lexer *, const struct var_set *, struct variable ***, size_t *,
                        int opts);
bool parse_DATA_LIST_vars (struct lexer *, char ***names, size_t *cnt, int opts);
bool parse_DATA_LIST_vars_pool (struct lexer *, struct pool *,
                               char ***names, size_t *cnt, int opts);
bool parse_mixed_vars (struct lexer *, const struct dictionary *dict, 
		       char ***names, size_t *cnt, int opts);
bool parse_mixed_vars_pool (struct lexer *, const struct dictionary *dict, 
			    struct pool *,
                           char ***names, size_t *cnt, int opts);

#endif /* variable-parser.h */
