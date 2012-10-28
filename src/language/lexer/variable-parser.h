/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2010 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef VARIABLE_PARSER_H
#define VARIABLE_PARSER_H 1

#include <stdbool.h>
#include <stddef.h>

struct pool;
struct dictionary;
struct var_set;
struct const_var_set;
struct variable;
struct lexer ;

struct var_set *var_set_create_from_dict (const struct dictionary *d);
struct var_set *var_set_create_from_array (struct variable *const *var,
                                           size_t);

size_t var_set_get_cnt (const struct var_set *vs);

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
    PV_SAME_WIDTH = 00200,	/* All vars must be the same type and width. */
    PV_NO_SCRATCH = 00400 	/* Disallow scratch variables. */
  };

struct variable *parse_variable (struct lexer *, const struct dictionary *);
bool parse_variables (struct lexer *, const struct dictionary *, struct variable ***, size_t *,
                     int opts);
bool parse_variables_pool (struct lexer *, struct pool *, const struct dictionary *,
                          struct variable ***, size_t *, int opts);
bool parse_var_set_vars (struct lexer *, const struct var_set *, struct variable ***, size_t *,
                        int opts);
bool parse_DATA_LIST_vars (struct lexer *, const struct dictionary *,
                           char ***names, size_t *cnt, int opts);
bool parse_DATA_LIST_vars_pool (struct lexer *, const struct dictionary *,
                                struct pool *,
                                char ***names, size_t *cnt, int opts);
bool parse_mixed_vars (struct lexer *, const struct dictionary *dict,
		       char ***names, size_t *cnt, int opts);
bool parse_mixed_vars_pool (struct lexer *, const struct dictionary *dict,
			    struct pool *,
                           char ***names, size_t *cnt, int opts);


/* Const wrappers */

static inline const struct variable *
parse_variable_const (struct lexer *l, const struct dictionary *d)
{
  return parse_variable (l, d);
}

static inline bool
parse_variables_const (struct lexer *l, const struct dictionary *d,
		       const struct variable ***v, size_t *s,
		       int opts)
{
  return parse_variables (l, d, (struct variable ***) v, s, opts);
}

static inline bool
parse_variables_const_pool (struct lexer *l, struct pool *p,
			    const struct dictionary *d,
			    const struct variable ***v, size_t *s, int opts)
{
  return parse_variables_pool (l, p, d, (struct variable ***) v, s, opts);
}



static inline struct const_var_set *
const_var_set_create_from_dict (const struct dictionary *d)
{
  return (struct const_var_set *) var_set_create_from_dict (d);
}

static inline struct const_var_set *
const_var_set_create_from_array (const struct variable *const *var,
				 size_t s)
{
  return (struct const_var_set *) var_set_create_from_array ((struct variable *const *) var, s);
}


static inline bool
parse_const_var_set_vars (struct lexer *l, const struct const_var_set *vs,
			  const struct variable ***v, size_t *s, int opts)
{
  return parse_var_set_vars (l, (const struct var_set *) vs,
			     (struct variable ***) v, s, opts);
}

static inline void
const_var_set_destroy (struct const_var_set *vs)
{
  var_set_destroy ( (struct var_set *) vs);
}

/* Match a variable.
   If the match succeeds, the variable will be placed in VAR.
   Returns true if successful */
bool
lex_match_variable (struct lexer *lexer, const struct dictionary *dict, const struct variable **var);

struct interaction;

/* Parse an interaction.
   If not successfull return false.
   Otherwise, a newly created interaction will be placed in IACT.
   It is the caller's responsibility to destroy this interaction.
 */
bool
parse_design_interaction (struct lexer *lexer, const struct dictionary *dict, struct interaction **iact);



#endif /* variable-parser.h */
