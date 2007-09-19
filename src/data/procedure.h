/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007 Free Software Foundation, Inc.

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

#include <data/transformations.h>
#include <libpspp/compiler.h>

struct casereader;
struct dataset;
struct dictionary;

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

/* Procedures. */

struct dictionary ;
typedef void  replace_source_callback (struct casereader *);
typedef void  replace_dictionary_callback (struct dictionary *);

typedef void transformation_change_callback_func (bool non_empty, void *aux);

struct dataset * create_dataset (void);

void destroy_dataset (struct dataset *);

void dataset_add_transform_change_callback (struct dataset *,
					    transformation_change_callback_func *, void *);

void proc_discard_active_file (struct dataset *);
void proc_set_active_file (struct dataset *,
                           struct casereader *, struct dictionary *);
bool proc_set_active_file_data (struct dataset *, struct casereader *);
bool proc_has_active_file (const struct dataset *ds);
struct casereader *proc_extract_active_file_data (struct dataset *);

void proc_discard_output (struct dataset *ds);

bool proc_execute (struct dataset *ds);
time_t time_of_last_procedure (struct dataset *ds);

struct casereader *proc_open (struct dataset *);
bool proc_is_open (const struct dataset *);
bool proc_commit (struct dataset *);

bool dataset_end_of_command (struct dataset *);

struct dictionary *dataset_dict (const struct dataset *ds);
const struct casereader *dataset_source (const struct dataset *ds);


struct ccase *lagged_case (const struct dataset *ds, int n_before);
void dataset_need_lag (struct dataset *ds, int n_before);

#endif /* procedure.h */
