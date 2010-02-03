/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#ifndef MESSAGE_H
#define MESSAGE_H 1

#include <stdarg.h>
#include <stdbool.h>
#include <libpspp/compiler.h>

/* What kind of message is this? */
enum msg_category
  {
    MSG_C_GENERAL,              /* General info. */
    MSG_C_SYNTAX,               /* Messages that relate to syntax files. */
    MSG_C_DATA,                 /* Messages that relate to data files. */
    MSG_N_CATEGORIES
  };

/* How important a condition is it? */
enum msg_severity
  {
    MSG_S_ERROR,
    MSG_S_WARNING,
    MSG_S_NOTE,
    MSG_N_SEVERITIES
  };

/* Combination of a category and a severity for convenience. */
enum msg_class
  {
    ME, MW, MN,			/* General error/warning/note. */
    SE, SW, SN,			/* Script error/warning/note. */
    DE, DW, DN,			/* Data-file error/note. */
    MSG_CLASS_CNT,
  };

static inline enum msg_category
msg_class_to_category (enum msg_class class)
{
  return class / 3;
}

static inline enum msg_severity
msg_class_to_severity (enum msg_class class)
{
  return class % 3;
}

static inline enum msg_class
msg_class_from_category_and_severity (enum msg_category category,
                                      enum msg_severity severity)
{
  return category * 3 + severity;
}

/* A file location.  */
struct msg_locator
  {
    char *file_name;		/* File name. */
    int line_number;            /* Line number. */
  };

/* A message. */
struct msg
  {
    enum msg_category category; /* Message category. */
    enum msg_severity severity; /* Message severity. */
    struct msg_locator where;	/* File location, or (NULL, -1). */
    char *text;                 /* Error text. */
  };

struct source_stream ;

/* Initialization. */
void msg_init (struct source_stream *, void (*handler) (const struct msg *) );

void msg_done (void);

struct msg * msg_dup(const struct msg *m);
void msg_destroy(struct msg *m);

/* Emitting messages. */
void msg (enum msg_class, const char *format, ...)
     PRINTF_FORMAT (2, 3);
void msg_emit (struct msg *);

/* Enable and disable messages. */
void msg_enable (void);
void msg_disable (void);

/* Error context. */
void msg_set_command_name (const char *);
const char *msg_get_command_name (void);
void msg_push_msg_locator (const struct msg_locator *);
void msg_pop_msg_locator (const struct msg_locator *);


/* Used in panic situations only. */
void request_bug_report_and_abort (const char *msg) NO_RETURN;

#endif /* message.h */
