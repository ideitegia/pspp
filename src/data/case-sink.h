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

#ifndef CASE_SINK_H
#define CASE_SINK_H 1

#include <stdbool.h>
#include <stddef.h>

struct ccase;
struct dictionary;

/* A case sink. */
struct case_sink 
  {
    const struct case_sink_class *class;        /* Class. */
    void *aux;          /* Auxiliary data. */
    size_t value_cnt;   /* Number of `union value's in case. */
  };

/* A case sink class. */
struct case_sink_class
  {
    const char *name;                   /* Identifying name. */
    
    /* Opens the sink for writing. */
    void (*open) (struct case_sink *);
                  
    /* Writes a case to the sink. */
    bool (*write) (struct case_sink *, const struct ccase *);
    
    /* Closes and destroys the sink. */
    void (*destroy) (struct case_sink *);

    /* Closes the sink and returns a source that can read back
       the cases that were written, perhaps transformed in some
       way.  The sink must still be separately destroyed by
       calling destroy(). */
    struct case_source *(*make_source) (struct case_sink *);
  };

extern const struct case_sink_class null_sink_class;

struct case_sink *create_case_sink (const struct case_sink_class *,
                                    const struct dictionary *,
                                    void *);
void free_case_sink (struct case_sink *);

#endif /* case-sink.h */
