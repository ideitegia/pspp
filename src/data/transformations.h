/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2011 Free Software Foundation, Inc.

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

#ifndef TRANSFORMATIONS_H
#define TRANSFORMATIONS_H 1

#include <stdbool.h>
#include <stddef.h>
#include "data/case.h"

/* trns_proc_func return values. */
enum trns_result
  {
    TRNS_CONTINUE = -1,         /* Continue to next transformation. */
    TRNS_DROP_CASE = -2,        /* Drop this case. */
    TRNS_ERROR = -3,            /* A serious error, so stop the procedure. */
    TRNS_END_CASE = -4,         /* Skip to next case.  INPUT PROGRAM only. */
    TRNS_END_FILE = -5          /* End of input.  INPUT PROGRAM only. */
  };

struct ccase;
typedef void trns_finalize_func (void *);
typedef int trns_proc_func (void *, struct ccase **, casenumber);
typedef bool trns_free_func (void *);

/* Transformation chains. */

struct trns_chain *trns_chain_create (void);
void trns_chain_finalize (struct trns_chain *);
bool trns_chain_destroy (struct trns_chain *);

bool trns_chain_is_empty (const struct trns_chain *);

void trns_chain_append (struct trns_chain *, trns_finalize_func *,
                        trns_proc_func *, trns_free_func *, void *);
size_t trns_chain_next (struct trns_chain *);
enum trns_result trns_chain_execute (const struct trns_chain *,
                                     enum trns_result, struct ccase **,
                                     casenumber case_nr);

void trns_chain_splice (struct trns_chain *, struct trns_chain *);

#endif /* transformations.h */
