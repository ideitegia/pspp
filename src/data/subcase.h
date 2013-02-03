/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009 Free Software Foundation, Inc.

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

#ifndef DATA_SUBCASE_H
#define DATA_SUBCASE_H 1

#include <stdbool.h>
#include <stddef.h>

struct ccase;
union value;
struct variable;

/* Sort order. */
enum subcase_direction
  {
    SC_ASCEND,			/* A, B, C, ..., X, Y, Z. */
    SC_DESCEND			/* Z, Y, X, ..., C, B, A. */
  };

/* A value within a case. */
struct subcase_field
  {
    size_t case_index;          /* Starting position in the case. */
    int width;                  /* 0=numeric, otherwise string width. */
    enum subcase_direction direction; /* Sort order. */
  };

/* A subcase specifies how to draw values from a case. */
struct subcase
  {
    struct subcase_field *fields;
    size_t n_fields;

    struct caseproto *proto;    /* Created lazily. */
  };

void subcase_init_empty (struct subcase *);
void subcase_init_vars (struct subcase *,
                        const struct variable *const *, size_t n_vars);
void subcase_init_var (struct subcase *,
                       const struct variable *, enum subcase_direction);
void subcase_init (struct subcase *, int index, int width,
		   enum subcase_direction);

void subcase_clone (struct subcase *, const struct subcase *);
void subcase_clear (struct subcase *);
void subcase_destroy (struct subcase *);

bool subcase_contains (const struct subcase *, int case_index);
bool subcase_contains_var (const struct subcase *, const struct variable *);

bool subcase_add (struct subcase *, int case_index, int width,
		  enum subcase_direction direction);
bool subcase_add_var (struct subcase *, const struct variable *,
                      enum subcase_direction);

void subcase_add_always (struct subcase *sc, int case_index, int width,
                         enum subcase_direction direction);
void subcase_add_var_always (struct subcase *, const struct variable *,
                             enum subcase_direction);
void subcase_add_vars_always (struct subcase *,
                              const struct variable *const *, size_t n_vars);
void subcase_add_proto_always (struct subcase *, const struct caseproto *);

const struct caseproto *subcase_get_proto (const struct subcase *);

static inline bool subcase_is_empty (const struct subcase *);
static inline size_t subcase_get_n_fields (const struct subcase *);

static inline size_t subcase_get_case_index (const struct subcase *,
                                             size_t idx);
static inline enum subcase_direction subcase_get_direction (
  const struct subcase *, size_t idx);

bool subcase_conformable (const struct subcase *, const struct subcase *);

void subcase_extract (const struct subcase *, const struct ccase *,
                      union value *values);
void subcase_inject (const struct subcase *,
                     const union value *values, struct ccase *);
void subcase_copy (const struct subcase *src_sc, const struct ccase *src,
                   const struct subcase *dst_sc, struct ccase *dst);

int subcase_compare_3way (const struct subcase *a_sc, const struct ccase *a,
                          const struct subcase *b_sc, const struct ccase *b);
int subcase_compare_3way_xc (const struct subcase *,
                             const union value *a, const struct ccase *b);
int subcase_compare_3way_cx (const struct subcase *,
                             const struct ccase *a, const union value *b);
int subcase_compare_3way_xx (const struct subcase *,
                             const union value *a, const union value *b);
bool subcase_equal (const struct subcase *a_sc, const struct ccase *a,
                    const struct subcase *b_sc, const struct ccase *b);
bool subcase_equal_xc (const struct subcase *,
                       const union value *a, const struct ccase *b);
bool subcase_equal_cx (const struct subcase *,
                       const struct ccase *a, const union value *b);
bool subcase_equal_xx (const struct subcase *,
                       const union value *a, const union value *b);

static inline size_t
subcase_get_case_index (const struct subcase *sc, size_t idx)
{
  return sc->fields[idx].case_index;
}

static inline enum subcase_direction
subcase_get_direction (const struct subcase *sc, size_t idx)
{
  return sc->fields[idx].direction;
}

static inline bool
subcase_is_empty (const struct subcase *sc)
{
  return sc->n_fields == 0;
}

static inline size_t
subcase_get_n_fields (const struct subcase *sc)
{
  return sc->n_fields;
}

#endif /* data/subcase.h */
