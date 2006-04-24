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

#if !error_h
#define error_h 1

#include <stdarg.h>
#include <stdbool.h>
#include "compiler.h"

/* Message classes. */
enum msg_class
  {
    ME, MW, MN,			/* General error/warning/note. */
    SE, SW, SN,			/* Script error/warning/note. */
    DE, DW, DN,			/* Data-file error/note. */
    MSG_CLASS_CNT,
  };

/* What kind of message is this? */
enum msg_category 
  {
    MSG_GENERAL,        /* General info. */
    MSG_SYNTAX,         /* Messages that relate to syntax files. */
    MSG_DATA            /* Messages that relate to data files. */
  };

/* How important a condition is it? */
enum msg_severity 
  {
    MSG_ERROR,
    MSG_WARNING,
    MSG_NOTE
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
struct file_locator
  {
    const char *file_name;		/* File name. */
    int line_number;			/* Line number. */
  };

/* An error message. */
struct error
  {
    enum msg_category category; /* Message category. */
    enum msg_severity severity; /* Message severity. */
    struct file_locator where;	/* File location, or (NULL, -1). */
    char *text;                 /* Error text. */
  };

/* Number of errors, warnings reported. */
extern int err_error_count;
extern int err_warning_count;

/* If number of allowable errors/warnings is exceeded, then a message
   is displayed and this flag is set to suppress subsequent
   messages. */
extern int err_already_flagged;

/* Nonnegative verbosity level.  Higher value == more verbose. */
extern int err_verbosity;

/* Emitting messages. */
void msg (enum msg_class, const char *format, ...)
     PRINTF_FORMAT (2, 3);
void err_msg (const struct error *);

void verbose_msg (int level, const char *format, ...)
     PRINTF_FORMAT (2, 3);

/* File-locator stack. */
void err_push_file_locator (const struct file_locator *);
void err_pop_file_locator (const struct file_locator *);
void err_location (struct file_locator *);

/* Obscure functions. */
void err_set_command_name (const char *);
void err_done (void);
void err_check_count (void);

/* Used in panic situations only */
void request_bug_report_and_abort(const char *msg );

void err_assert_fail(const char *expr, const char *file, int line);

#undef __STRING
#define __STRING(x) #x
#undef assert

			       
#define assert(expr) ( (void) ( expr ? (void) 0 : \
	       err_assert_fail(__STRING(expr), __FILE__, __LINE__)) )



#endif /* error.h */
