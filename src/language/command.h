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

#if !command_h
#define command_h 1

/* Current program state. */
enum
  {
    STATE_INIT,			/* Initialization state. */
    STATE_INPUT,		/* Input state. */
    STATE_TRANS,		/* Transformation state. */
    STATE_PROC,			/* Procedure state. */
    STATE_ERROR			/* Invalid state transition. */
  };

/* Command return values. */
enum
  {
    /* Successful return values. */
    CMD_SUCCESS = 0x1000,       /* Successfully parsed and executed. */
    CMD_EOF,                    /* Requested exit. */

    /* Various kinds of failures, in increasing order of severity. */
    CMD_TRAILING_GARBAGE, 	/* Followed by garbage. */
    CMD_PART_SUCCESS,		/* Fully executed up to error. */
    CMD_PART_SUCCESS_MAYBE,	/* May have been partially executed. */
    CMD_FAILURE,                /* Not executed at all. */
    CMD_CASCADING_FAILURE       /* Serious error: don't continue. */
  };

extern int pgm_state;

char *pspp_completion_function (const char *text,   int state);

int cmd_parse (void);

/* Prototype all the command functions. */
#define DEFCMD(NAME, T1, T2, T3, T4, FUNC)	\
	int FUNC (void);
#define SPCCMD(NAME, T1, T2, T3, T4, FUNC)	\
	int FUNC (void);
#define DBGCMD(NAME, T1, T2, T3, T4, FUNC)	\
	int FUNC (void);
#define UNIMPL(NAME, T1, T2, T3, T4, DESC)
#include "command.def"
#undef DEFCMD
#undef SPCCMD
#undef UNIMPL
#undef DBGCMD

#endif /* !command_h */
