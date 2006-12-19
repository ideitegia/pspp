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

#ifndef CASE_SOURCE_H
#define CASE_SOURCE_H 1

#include <stdbool.h>

struct ccase;

/* A case source. */
struct case_source 
  {
    const struct case_source_class *class;      /* Class. */
    void *aux;          /* Auxiliary data. */
  };

/* A case source class. */
struct case_source_class
  {
    const char *name;                   /* Identifying name. */
    
    /* Returns the exact number of cases that READ will pass to
       WRITE_CASE, if known, or -1 otherwise. */
    int (*count) (const struct case_source *);

    /* Reads one case into C.
       Returns true if successful, false at end of file or if an
       I/O error occurred. */
    bool (*read) (struct case_source *, struct ccase *);

    /* Destroys the source.
       Returns true if successful read, false if an I/O occurred
       during destruction or previously. */
    bool (*destroy) (struct case_source *);
  };


struct case_source *create_case_source (const struct case_source_class *,
                                        void *);
bool free_case_source (struct case_source *);

bool case_source_is_class (const struct case_source *,
                          const struct case_source_class *);

#endif /* case-source.h */
