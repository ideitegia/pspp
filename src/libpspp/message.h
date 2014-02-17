/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011, 2014 Free Software Foundation, Inc.

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
#include "libpspp/compiler.h"

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

const char *msg_severity_to_string (enum msg_severity);

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

/* A message. */
struct msg
  {
    enum msg_category category; /* Message category. */
    enum msg_severity severity; /* Message severity. */
    char *file_name;            /* Name of file containing error, or NULL. */
    int first_line;             /* 1-based line number, or 0 if none. */
    int last_line;             /* 1-based exclusive last line (0=none). */
    int first_column;           /* 1-based first column, or 0 if none. */
    int last_column;            /* 1-based exclusive last column (0=none). */
    char *text;                 /* Error text. */
    bool shipped;               /* True if this message has been emitted */
  };

/* Initialization. */
void msg_set_handler (void (*handler) (const struct msg *, void *lexer),
                      void *aux);

/* Working with messages. */
struct msg *msg_dup (const struct msg *);
void msg_destroy(struct msg *);
char *msg_to_string (const struct msg *, const char *command_name);

/* Emitting messages. */
void vmsg (enum msg_class class, const char *format, va_list args)
     PRINTF_FORMAT (2, 0);
void msg (enum msg_class, const char *format, ...)
     PRINTF_FORMAT (2, 3);
void msg_emit (struct msg *);

void msg_error (int errnum, const char *format, ...);


/* Enable and disable messages. */
void msg_enable (void);
void msg_disable (void);

/* Error context. */
bool msg_ui_too_many_errors (void);
void msg_ui_reset_counts (void);
bool msg_ui_any_errors (void);
void msg_ui_disable_warnings (bool);


/* Used in panic situations only. */
void request_bug_report (const char *msg);


#endif /* message.h */
