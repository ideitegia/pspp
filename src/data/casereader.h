/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

/* Casereader client interface.

   A casereader abstracts interfaces through which cases may be
   read.  A casereader may be a front-end for a system file, a
   portable file, the active file in a data set, or anything else
   on which a casereader interface has been overlaid.  Casereader
   layering, in which a casereader acts as a filter or translator
   on top of another casereader, is also supported.

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

#include <libpspp/compiler.h>
#include <data/case.h>
#include <data/missing-values.h>

struct dictionary;
struct casereader;
struct casewriter;

bool casereader_read (struct casereader *, struct ccase *);
bool casereader_destroy (struct casereader *);

struct casereader *casereader_clone (const struct casereader *);
void casereader_split (struct casereader *,
                       struct casereader **, struct casereader **);
struct casereader *casereader_rename (struct casereader *);
void casereader_swap (struct casereader *, struct casereader *);

bool casereader_peek (struct casereader *, casenumber, struct ccase *)
     WARN_UNUSED_RESULT;

bool casereader_error (const struct casereader *);
void casereader_force_error (struct casereader *);
const struct taint *casereader_get_taint (const struct casereader *);

casenumber casereader_get_case_cnt (struct casereader *);
casenumber casereader_count_cases (struct casereader *);
size_t casereader_get_value_cnt (struct casereader *);

void casereader_transfer (struct casereader *, struct casewriter *);

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
                                  const struct variable **vars, size_t var_cnt,
                                  enum mv_class,
                                  struct casewriter *exclude);

struct casereader *
casereader_create_counter (struct casereader *, casenumber *counter,
                           casenumber initial_value);

struct casereader *
casereader_create_translator (struct casereader *, size_t output_value_cnt,
                              void (*translate) (const struct ccase *input,
                                                 struct ccase *output,
                                                 void *aux),
                              bool (*destroy) (void *aux),
                              void *aux);

#endif /* data/casereader.h */
