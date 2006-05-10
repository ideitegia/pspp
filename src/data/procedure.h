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

#include <data/transformations.h>
#include <libpspp/compiler.h>

struct ccase;
struct casefile;
struct case_sink;
struct case_source;

/* Dictionary produced by permanent and temporary transformations
   on data from the source. */
extern struct dictionary *default_dict;

/* Transformations. */

void add_transformation (trns_proc_func *, trns_free_func *, void *);
void add_transformation_with_finalizer (trns_finalize_func *,
                                        trns_proc_func *,
                                        trns_free_func *, void *);
size_t next_transformation (void);

void discard_variables (void);

bool proc_cancel_all_transformations (void);
struct trns_chain *proc_capture_transformations (void);

void proc_start_temporary_transformations (void);
bool proc_in_temporary_transformations (void);
bool proc_make_temporary_transformations_permanent (void);
bool proc_cancel_temporary_transformations (void);

/* Procedures. */

void proc_init (void);
void proc_done (void);

void proc_set_source (struct case_source *);
bool proc_has_source (void);

void proc_set_sink (struct case_sink *);
struct casefile *proc_capture_output (void);

bool procedure (bool (*proc_func) (const struct ccase *, void *),
                void *aux)
     WARN_UNUSED_RESULT;
bool procedure_with_splits (void (*begin_func) (const struct ccase *, void *),
                            bool (*proc_func) (const struct ccase *, void *),
                            void (*end_func) (void *),
                            void *aux)
     WARN_UNUSED_RESULT;
bool multipass_procedure (bool (*proc_func) (const struct casefile *, void *),
                          void *aux)
     WARN_UNUSED_RESULT;
bool multipass_procedure_with_splits (bool (*) (const struct ccase *,
                                                const struct casefile *,
                                                void *),
                                      void *aux)
     WARN_UNUSED_RESULT;
time_t time_of_last_procedure (void);

/* Number of cases to lag. */
extern int n_lag;

struct ccase *lagged_case (int n_before);

#endif /* procedure.h */
