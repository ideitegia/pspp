/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

struct dataset;


/* Transformations. */

void add_transformation (struct dataset *ds, 
			 trns_proc_func *, trns_free_func *, void *);
void add_transformation_with_finalizer (struct dataset *ds, 
					trns_finalize_func *,
                                        trns_proc_func *,
                                        trns_free_func *, void *);
size_t next_transformation (const struct dataset *ds);

void discard_variables (struct dataset *ds);



bool proc_cancel_all_transformations (struct dataset *ds);
struct trns_chain *proc_capture_transformations (struct dataset *ds);

void proc_start_temporary_transformations (struct dataset *ds);
bool proc_in_temporary_transformations (const struct dataset *ds);
bool proc_make_temporary_transformations_permanent (struct dataset *ds);
bool proc_cancel_temporary_transformations (struct dataset *ds);

/* Procedures. */

struct dataset * create_dataset (void);
void destroy_dataset (struct dataset *);

void proc_set_source (struct dataset *ds, struct case_source *);
bool proc_has_source (const struct dataset *ds);

void proc_set_sink (struct dataset *ds, struct case_sink *);
struct casefile *proc_capture_output (struct dataset *ds);

typedef bool casefile_func (const struct casefile *, void *);
typedef bool case_func (const struct ccase *, void *, const struct dataset *);
typedef void begin_func (const struct ccase *, void *, const struct dataset*);

typedef bool end_func (void *, const struct dataset *);

typedef bool split_func (const struct ccase *, const struct casefile *,
			      void *, const struct dataset *);



bool procedure (struct dataset *ds, case_func *, void *aux)  WARN_UNUSED_RESULT;

bool procedure_with_splits (struct dataset *ds, 
			    begin_func *,
                            case_func *,
			    end_func *,
                            void *aux)
     WARN_UNUSED_RESULT;
bool multipass_procedure (struct dataset *ds, casefile_func *, void  *aux)
     WARN_UNUSED_RESULT;
bool multipass_procedure_with_splits (struct dataset *ds,
					   split_func *,
					   void *aux)
     WARN_UNUSED_RESULT;



time_t time_of_last_procedure (struct dataset *ds);


struct ccase *lagged_case (const struct dataset *ds, int n_before);

inline struct dictionary *dataset_dict (const struct dataset *ds);
inline void dataset_set_dict ( struct dataset *ds, struct dictionary *dict);

inline int dataset_n_lag (const struct dataset *ds);
inline void dataset_set_n_lag (struct dataset *ds, int n_lag);


#endif /* procedure.h */
