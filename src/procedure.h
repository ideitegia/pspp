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

#ifndef PROCEDURE_H
#define PROCEDURE_H 1

#include <time.h>
#include <stdbool.h>

struct ccase;
struct casefile;

/* The current active file, from which cases are read. */
extern struct case_source *vfm_source;

/* The replacement active file, to which cases are written. */
extern struct case_sink *vfm_sink;

bool procedure (bool (*proc_func) (struct ccase *, void *aux), void *aux);
bool procedure_with_splits (void (*begin_func) (void *aux),
                            bool (*proc_func) (struct ccase *, void *aux),
                            void (*end_func) (void *aux),
                            void *aux);
bool multipass_procedure_with_splits (bool (*) (const struct casefile *,
                                                void *),
                                      void *aux);
time_t time_of_last_procedure (void);

/* Number of cases to lag. */
extern int n_lag;

struct ccase *lagged_case (int n_before);

#endif /* procedure.h */
