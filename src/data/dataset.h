/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2010, 2011, 2013 Free Software Foundation, Inc.

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

#ifndef PROCEDURE_H
#define PROCEDURE_H 1

#include <time.h>
#include <stdbool.h>

#include "data/transformations.h"

struct casereader;
struct dataset;
struct dictionary;
struct session;

struct dataset *dataset_create (struct session *, const char *);
struct dataset *dataset_clone (struct dataset *, const char *);
void dataset_destroy (struct dataset *);

void dataset_clear (struct dataset *);

const char *dataset_name (const struct dataset *);
void dataset_set_name (struct dataset *, const char *);

struct session *dataset_session (const struct dataset *);
void dataset_set_session (struct dataset *, struct session *);

struct dictionary *dataset_dict (const struct dataset *);
void dataset_set_dict (struct dataset *, struct dictionary *);

const struct casereader *dataset_source (const struct dataset *);
bool dataset_has_source (const struct dataset *ds);
bool dataset_set_source (struct dataset *, struct casereader *);
struct casereader *dataset_steal_source (struct dataset *);

unsigned int dataset_seqno (const struct dataset *);

struct dataset_callbacks
  {
    /* Called whenever a procedure completes execution or whenever the
       dictionary within the dataset is modified (though not when it is
       replaced by a new dictionary). */
    void (*changed) (void *aux);

    /* Called whenever a transformation is added or removed.  NON_EMPTY is true
       if after the change there is at least one transformation, false if there
       are no transformations. */
    void (*transformations_changed) (bool non_empty, void *aux);
  };

void dataset_set_callbacks (struct dataset *, const struct dataset_callbacks *,
                            void *aux);

/* Dataset GUI window display status. */
enum dataset_display
  {
    DATASET_ASIS,               /* Current state unchanged. */
    DATASET_FRONT,              /* Display and raise to top. */
    DATASET_MINIMIZED,          /* Display as icon. */
    DATASET_HIDDEN              /* Do not display. */
  };
enum dataset_display dataset_get_display (const struct dataset *);
void dataset_set_display (struct dataset *, enum dataset_display);

/* Transformations. */

void add_transformation (struct dataset *ds,
			 trns_proc_func *, trns_free_func *, void *);
void add_transformation_with_finalizer (struct dataset *ds,
					trns_finalize_func *,
                                        trns_proc_func *,
                                        trns_free_func *, void *);
size_t next_transformation (const struct dataset *ds);

bool proc_cancel_all_transformations (struct dataset *ds);
struct trns_chain *proc_capture_transformations (struct dataset *ds);

void proc_start_temporary_transformations (struct dataset *ds);
bool proc_in_temporary_transformations (const struct dataset *ds);
bool proc_make_temporary_transformations_permanent (struct dataset *ds);
bool proc_cancel_temporary_transformations (struct dataset *ds);
struct variable *add_permanent_ordering_transformation (struct dataset *);

/* Procedures. */

void proc_discard_output (struct dataset *ds);

bool proc_execute (struct dataset *ds);
time_t time_of_last_procedure (struct dataset *ds);

struct casereader *proc_open_filtering (struct dataset *, bool filter);
struct casereader *proc_open (struct dataset *);
bool proc_is_open (const struct dataset *);
bool proc_commit (struct dataset *);

bool dataset_end_of_command (struct dataset *);

const struct ccase *lagged_case (const struct dataset *ds, int n_before);
void dataset_need_lag (struct dataset *ds, int n_before);

/* Private interface for use by session code. */

void dataset_set_session__(struct dataset *, struct session *);

#endif /* dataset.h */
