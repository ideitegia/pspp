/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2013 Free Software Foundation, Inc.

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

#ifndef COMMAND_H
#define COMMAND_H 1

#include <stdbool.h>

/* Command return values. */
enum cmd_result
  {
    /* Successful return values. */
    CMD_SUCCESS = 1,            /* Successfully parsed and executed. */
    CMD_EOF = 2,                /* End of input. */
    CMD_FINISH = 3,             /* FINISH was executed. */

    /* Successful return values returned by specific commands to let INPUT
       PROGRAM function properly. */
    CMD_DATA_LIST,
    CMD_END_CASE,
    CMD_END_FILE,

    /* Various kinds of failures. */
    CMD_FAILURE = -1,           /* Not executed at all. */
    CMD_NOT_IMPLEMENTED = -2,   /* Command not implemented. */
    CMD_CASCADING_FAILURE = -3  /* Serious error: don't continue. */
  };

bool cmd_result_is_success (enum cmd_result);
bool cmd_result_is_failure (enum cmd_result);

/* Command processing state. */
enum cmd_state
  {
    CMD_STATE_INITIAL,          /* No active dataset yet defined. */
    CMD_STATE_DATA,             /* Active dataset has been defined. */
    CMD_STATE_INPUT_PROGRAM,    /* Inside INPUT PROGRAM. */
    CMD_STATE_FILE_TYPE         /* Inside FILE TYPE. */
  };

struct dataset;
struct lexer;

enum cmd_result cmd_parse_in_state (struct lexer *lexer, struct dataset *ds,
				    enum cmd_state);

enum cmd_result cmd_parse (struct lexer *lexer, struct dataset *ds);

struct command;
const char *cmd_complete (const char *, const struct command **);

struct dataset;

/* Prototype all the command functions. */
#define DEF_CMD(STATES, FLAGS, NAME, FUNCTION) int FUNCTION (struct lexer *, struct dataset *);
#define UNIMPL_CMD(NAME, DESCRIPTION)
#include "command.def"
#undef DEF_CMD
#undef UNIMPL_CMD

#endif /* command.h */
