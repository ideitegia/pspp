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

#ifndef GETL_H
#define GETL_H 1

#include <stdbool.h>
#include <libpspp/ll.h>

struct string; 

struct getl_source;


/* An abstract base class for objects which act as line buffers for the 
   PSPP.  Ie anything which might contain content for the lexer */
struct getl_interface 
  {
    /* Returns true, if the interface is interactive */
    bool  (*interactive) (const struct getl_interface *); 

    /* Read a line from the interface */
    bool  (*read)  (struct getl_interface *, struct string *);

    /* Close and destroy the interface */
    void  (*close) (struct getl_interface *);

    /* Filter for current and all included sources.  May be NULL */
    void  (*filter) (struct getl_interface *, struct string *line);

    /* Returns the name of the source */
    const char * (*name) (const struct getl_interface *);

    /* Returns the current location within the source */
    int (*location) (const struct getl_interface *);
  };

void getl_initialize (void);
void getl_uninitialize (void);

void getl_clear_include_path (void);
void getl_add_include_dir (const char *);
const char * getl_include_path (void);

void getl_abort_noninteractive (void);
bool getl_is_interactive (void);

bool getl_read_line (bool *interactive);

bool do_read_line (struct string *line, bool *interactive);

void getl_append_source (struct getl_interface *s) ;
void getl_include_source (struct getl_interface *s) ;

const char * getl_source_name (void);
int getl_source_location (void);

#endif /* line-buffer.h */
