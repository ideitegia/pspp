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

#ifndef GETL_H
#define GETL_H 1

#include <stdbool.h>
#include <libpspp/ll.h>

struct string;

struct getl_source;

/* Syntax rules that apply to a given source line. */
enum getl_syntax
  {
    /* Each line that begins in column 1 starts a new command.  A
       `+' or `-' in column 1 is ignored to allow visual
       indentation of new commands.  Continuation lines must be
       indented from the left margin.  A period at the end of a
       line does end a command, but it is optional. */
    GETL_BATCH,

    /* Each command must end in a period or in a blank line. */
    GETL_INTERACTIVE
  };

/* An abstract base class for objects which act as line buffers for the
   PSPP.  Ie anything which might contain content for the lexer */
struct getl_interface
  {
    /* Returns true if the interface is interactive, that is, if
       it prompts a human user.  This property is independent of
       the syntax mode returned by the read member function. */
    bool  (*interactive) (const struct getl_interface *);

    /* Read a line the intended syntax mode from the interface.
       Returns true if succesful, false on failure or at end of
       input. */
    bool  (*read)  (struct getl_interface *,
                    struct string *, enum getl_syntax *);

    /* Close and destroy the interface */
    void  (*close) (struct getl_interface *);

    /* Filter for current and all included sources, which may
       modify the line.  Usually null.  */
    void  (*filter) (struct getl_interface *,
                     struct string *line, enum getl_syntax);

    /* Returns the name of the source */
    const char * (*name) (const struct getl_interface *);

    /* Returns the current location within the source */
    int (*location) (const struct getl_interface *);
  };

struct source_stream;

struct source_stream * create_source_stream (const char *);
void destroy_source_stream (struct source_stream *);

void getl_clear_include_path (struct source_stream *);
void getl_add_include_dir (struct source_stream *, const char *);
const char * getl_include_path (const struct source_stream *);

void getl_abort_noninteractive (struct source_stream *);
bool getl_is_interactive (const struct source_stream *);

bool getl_read_line (struct source_stream *, struct string *,
		     enum getl_syntax *);

void getl_append_source (struct source_stream *, struct getl_interface *s) ;
void getl_include_source (struct source_stream *, struct getl_interface *s) ;

const char * getl_source_name (const struct source_stream *);
int getl_source_location (const struct source_stream *);

#endif /* line-buffer.h */
