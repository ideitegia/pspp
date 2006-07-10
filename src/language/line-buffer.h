/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
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
#include <libpspp/str.h>

enum getl_prompt_style
  {
    GETL_PROMPT_FIRST,		/* First line of command. */
    GETL_PROMPT_LATER,          /* Second or later line of command. */
    GETL_PROMPT_DATA,		/* Between BEGIN DATA and END DATA. */
    GETL_PROMPT_CNT
  };

/* Current line.  This line may be modified by modules other than
   getl.c, and by lexer.c in particular.  (Ugh.) */
extern struct string getl_buf;

void getl_initialize (void);
void getl_uninitialize (void);

void getl_clear_include_path (void);
void getl_add_include_dir (const char *);

void getl_append_syntax_file (const char *);
void getl_include_syntax_file (const char *);
void getl_include_filter (void (*filter) (struct string *, void *aux),
                          void (*close) (void *aux),
                          void *aux);
void getl_include_function (bool (*read) (struct string *line,
                                          char **fn, int *ln, void *aux),
                            void (*close) (void *aux),
                            void *aux);
void getl_append_interactive (bool (*function) (struct string *line,
                                                enum getl_prompt_style));
void getl_abort_noninteractive (void);
bool getl_is_interactive (void);

bool getl_read_line (bool *interactive);

const char *getl_get_prompt (enum getl_prompt_style);
void getl_set_prompt (enum getl_prompt_style, const char *);
void getl_set_prompt_style (enum getl_prompt_style);

struct msg_locator;
void get_msg_location (struct msg_locator *loc);

void getl_location (const char **fn, int *ln);


#endif /* line-buffer.h */
