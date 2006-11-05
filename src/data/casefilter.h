/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

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

#if !casefilter_h
#define casefilter_h 1

#include <stdbool.h>

struct ccase;
struct casefilter;
struct variable ;

/* Create a new casefilter.
   If EXCL is true, then the filter  user missing values to be missing, 
   otherwise they are considered at their face value.
   VARS is an array of variables which if *any* of them are missing.
   N_VARS is the size of VARS.
 */
struct casefilter * casefilter_create (bool, struct variable **, int);

/* Add the variables in VARS to the list of variables for which the
   filter considers. N_VARS is the size of VARS */
void casefilter_add_variables (struct casefilter *, struct variable **, int);

/* Destroy the filter FILTER */
void casefilter_destroy (struct casefilter *); 

/* Returns true iff the entire case should be skipped */
bool casefilter_skip_case (const struct casefilter *, const struct ccase *);

/* Returns true iff the variable V in case C is missing */
bool casefilter_variable_missing (const struct casefilter *f, 
				   const struct ccase *c, 
				   const struct variable *v);

#endif
