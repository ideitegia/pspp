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

#if !getl_h
#define getl_h 1

#include <stdio.h>

/* Defines a list of lines used by DO REPEAT. */
/* Special case: if LEN is negative then it is a line number; in this
   case LINE is a file name.  This is used to allow errors to be
   reported for the correct file and line number when DO REPEAT spans
   files. */
struct getl_line_list
  {
    char *line;				/* Line contents. */
    int len;				/* Line length. */
    struct getl_line_list *next;	/* Next line. */
  };

/* Source file. */
struct getl_script
  {
    struct getl_script *included_from;	/* File that this is nested inside. */
    struct getl_script *includes;	/* File nested inside this file. */
    struct getl_script *next;		/* Next file in list. */
    char *fn;				/* Filename. */
    int ln;				/* Line number. */
    int separate;			/* !=0 means this is a separate job. */
    FILE *f;				/* File handle. */

    /* Used only if F is NULL.  Used for DO REPEAT. */
    struct getl_line_list *first_line;	/* First line in line buffer. */
    struct getl_line_list *cur_line;	/* Current line in line buffer. */
    int remaining_loops;		/* Number of remaining loops through LINES. */
    int loop_index;			/* Number of loops through LINES so far. */
    void *macros;			/* Pointer to macro table. */
    int print;				/* 1=Print lines as executed. */
  };

/* List of script files. */
extern struct getl_script *getl_head;	/* Current file. */
extern struct getl_script *getl_tail;	/* End of list. */

/* If getl_head==0 and getl_interactive!=0, lines will be read from
   the console rather than terminating. */
extern int getl_interactive;

/* 1=the welcome message has been printed. */
extern int getl_welcomed;

/* Prompt styles. */
enum
  {
    GETL_PRPT_STANDARD,		/* Just asks for a command. */
    GETL_PRPT_CONTINUATION,	/* Continuation lines for a single command. */
    GETL_PRPT_DATA		/* Between BEGIN DATA and END DATA. */
  };

/* Current mode. */
enum
  {
    GETL_MODE_BATCH,		/* Batch mode. */
    GETL_MODE_INTERACTIVE	/* Interactive mode. */
  };

/* One of GETL_MODE_*, representing the current mode. */
extern int getl_mode;

/* Current prompting style: one of GETL_PRPT_*. */
extern int getl_prompt;

/* Are we reading a script? Are we interactive? */
#define getl_am_interactive (getl_head == NULL)
#define getl_reading_script (getl_head != NULL)

/* Current line.  This line may be modified by modules other than
   getl.c, and by lexer.c in particular. */
extern struct string getl_buf;

/* Name of the command history file. */
#if HAVE_LIBREADLINE && HAVE_LIBHISTORY
extern char *getl_history;
#endif

void getl_initialize (void);
void getl_uninitialize (void);
void getl_clear_include_path (void);
char *getl_get_current_directory (void);
void getl_add_include_dir (const char *);
void getl_add_file (const char *fn, int separate, int where);
void getl_include (const char *fn);
int getl_read_line (void);
void getl_close_file (void);
void getl_close_all (void);
int getl_perform_delayed_reset (void);
void getl_add_DO_REPEAT_file (struct getl_script *);
void getl_add_virtual_file (struct getl_script *);
void getl_location (const char **, int *);

#endif /* getl_h */
