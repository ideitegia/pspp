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
enum
  {
    SE, SW, SM,			/* Script error/warning/message. */
    DE, DW,			/* Data-file error/warning. */
    ME, MW, MM,			/* General error/warning/message. */
    ERR_CLASS_COUNT,		/* Number of message classes. */
    ERR_CLASS_MASK = 0xf,	/* Bitmask for class. */
    ERR_VERBOSITY_SHIFT = 4,	/* Shift count for verbosity. */
    ERR_VERBOSITY_MASK = 0xf 	/* Bitmask for verbosity. */
  };

/* If passed to msg() as CLASS, the return value will cause the message
   to be displayed only if `verbosity' is at least LEVEL. */
#define VM(LEVEL) (MM | ((LEVEL) << ERR_VERBOSITY_SHIFT))

/* A file location.  */
struct file_locator
  {
    const char *filename;		/* Filename. */
    int line_number;			/* Line number. */
  };

/* An error message. */
struct error
  {
    int class;			/* One of the classes above. */
    struct file_locator where;	/* File location, or (NULL, -1). */
    const char *title;		/* Special text inserted if not null. */
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

/* Functions. */
void msg (int class, const char *format, ...)
     PRINTF_FORMAT (2, 3);
void tmsg (int class, const char *title, const char *format, ...)
     PRINTF_FORMAT (3, 4);

/* File-locator stack. */
void err_push_file_locator (const struct file_locator *);
void err_pop_file_locator (const struct file_locator *);
void err_location (struct file_locator *);

/* Obscure functions. */
void err_set_command_name (const char *);
void err_done (void);
void err_check_count (void);
void err_vmsg (const struct error *, const char *, va_list);

/* Used in panic situations only */
void request_bug_report_and_abort(const char *msg );

void err_assert_fail(const char *expr, const char *file, int line);

#undef __STRING
#define __STRING(x) #x
#undef assert

			       
#define assert(expr) ( (void) ( expr ? (void) 0 : \
	       err_assert_fail(__STRING(expr), __FILE__, __LINE__)) )



#endif /* error.h */
