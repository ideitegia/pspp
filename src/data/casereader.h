/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011, 2013 Free Software Foundation, Inc.

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

/* Casereader client interface.

   A casereader abstracts interfaces through which cases may be
   read.  A casereader may be a front-end for a system file, a
   portable file, a dataset, or anything else on which a
   casereader interface has been overlaid.  Casereader layering,
   in which a casereader acts as a filter or translator on top of
   another casereader, is also supported.

   There is no central interface for obtaining casereaders: a
   casereader for reading a system file is obtained from the
   system file reading module, and so on.  Once a casereader has
   been obtained, by whatever means, the interface to it is
   uniform.  The most important functions for casereader usage
   are:

     - casereader_read: Reads a case from the casereader.  The
       case is consumed and cannot be read again.  The caller is
       responsible for destroying the case.

     - casereader_clone: Makes a copy of a casereader.  May be
       used to read one or a set of cases from a casereader
       repeatedly.

     - casereader_destroy: Destroys a casereader.

   Casereaders can encounter error conditions, such as I/O
   errors, as they read cases.  Error conditions prevent any more
   cases from being read from the casereader.  Error conditions
   are reported by casereader_error.  Error condition may be
   propagated to or from a casereader with taint_propagate using
   the casereader's taint object, which may be obtained with
   casereader_get_taint. */

#ifndef DATA_CASEREADER_H
#define DATA_CASEREADER_H 1

#include "libpspp/compiler.h"
#include "data/case.h"
#include "data/missing-values.h"

struct dictionary;
struct casereader;
struct casewriter;
struct subcase;

struct ccase *casereader_read (struct casereader *);
bool casereader_destroy (struct casereader *);

struct casereader *casereader_clone (const struct casereader *);
struct casereader *casereader_rename (struct casereader *);
void casereader_swap (struct casereader *, struct casereader *);

struct ccase *casereader_peek (struct casereader *, casenumber);
bool casereader_is_empty (struct casereader *);

bool casereader_error (const struct casereader *);
void casereader_force_error (struct casereader *);
const struct taint *casereader_get_taint (const struct casereader *);

casenumber casereader_get_case_cnt (struct casereader *);
casenumber casereader_count_cases (const struct casereader *);
void casereader_truncate (struct casereader *, casenumber);
const struct caseproto *casereader_get_proto (const struct casereader *);

casenumber casereader_advance (struct casereader *, casenumber);
void casereader_transfer (struct casereader *, struct casewriter *);

struct casereader *casereader_create_empty (const struct caseproto *);

struct casereader *
casereader_create_filter_func (struct casereader *,
                               bool (*include) (const struct ccase *,
                                                void *aux),
                               bool (*destroy) (void *aux),
                               void *aux,
                               struct casewriter *exclude);
struct casereader *
casereader_create_filter_weight (struct casereader *,
                                 const struct dictionary *dict,
                                 bool *warn_on_invalid,
                                 struct casewriter *exclude);
struct casereader *
casereader_create_filter_missing (struct casereader *,
                                  const struct variable *const*vars, size_t var_cnt,
                                  enum mv_class,
				  casenumber *n_missing,
                                  struct casewriter *exclude);

struct casereader *
casereader_create_counter (struct casereader *, casenumber *counter,
                           casenumber initial_value);

struct casereader *
casereader_create_translator (struct casereader *,
                              const struct caseproto *output_proto,
                              struct ccase *(*translate) (struct ccase *,
                                                          void *aux),
                              bool (*destroy) (void *aux),
                              void *aux);

struct casereader *
casereader_translate_stateless (struct casereader *,
                                const struct caseproto *output_proto,
                                struct ccase *(*translate) (struct ccase *,
                                                            casenumber idx,
                                                            const void *aux),
                                bool (*destroy) (void *aux),
                                void *aux);

struct casereader *casereader_project (struct casereader *,
                                       const struct subcase *);
struct casereader *casereader_project_1 (struct casereader *, int column);
struct casereader *casereader_select (struct casereader *,
                                      casenumber first, casenumber last,
                                      casenumber by);

/* A function which creates a numberic value from an existing case */
typedef double new_value_func (const struct ccase *, casenumber, void *);

struct casereader *
casereader_create_append_numeric (struct casereader *subreader,
				  new_value_func func, void *aux,
				  void (*destroy) (void *aux));

struct casereader *
casereader_create_arithmetic_sequence (struct casereader *,
                                       double first, double increment);

enum rank_error
  {
    RANK_ERR_NONE = 0,
    RANK_ERR_NEGATIVE_WEIGHT = 0x01,
    RANK_ERR_UNSORTED = 0x02
  };


typedef void distinct_func (double v, casenumber n, double w, void *aux);

struct casereader *
casereader_create_append_rank (struct casereader *,
			       const struct variable *v, const struct variable *w,
			       enum rank_error *err,
			       distinct_func *distinct_callback, void *aux);

struct casereader *
casereader_create_distinct (struct casereader *input,
			    const struct variable *key,
			    const struct variable *weight);


#endif /* data/casereader.h */
