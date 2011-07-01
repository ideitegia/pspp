/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011 Free Software Foundation, Inc.

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

#ifndef DATA_CASEWRITER_H
#define DATA_CASEWRITER_H 1

#include <stdbool.h>
#include "data/transformations.h"
#include "libpspp/compiler.h"

struct casewriter;

void casewriter_write (struct casewriter *, struct ccase *);
bool casewriter_destroy (struct casewriter *);

const struct caseproto *casewriter_get_proto (const struct casewriter *);

struct casereader *casewriter_make_reader (struct casewriter *);

struct casewriter *casewriter_rename (struct casewriter *);

bool casewriter_error (const struct casewriter *);
void casewriter_force_error (struct casewriter *);
const struct taint *casewriter_get_taint (const struct casewriter *);

struct casewriter *mem_writer_create (const struct caseproto *);
struct casewriter *tmpfile_writer_create (const struct caseproto *);
struct casewriter *autopaging_writer_create (const struct caseproto *);

struct casewriter *
casewriter_create_translator (struct casewriter *,
                              const struct caseproto *translated_proto,
                              struct ccase *(*translate) (struct ccase *input,
                                                          void *aux),
                              bool (*destroy) (void *aux),
                              void *aux);

#endif /* data/casewriter.h */
