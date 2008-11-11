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

#if !settings_h
#define settings_h 1

#include <stdbool.h>
#include <stddef.h>
#include <data/format.h>
#include <libpspp/float-format.h>
#include <libpspp/integer-format.h>

struct settings;


void settings_init (int *, int *);
void settings_done (void);

enum float_format settings_get_input_float_format (void);
void settings_set_input_float_format ( enum float_format);

/* Returns the integer format used for IB and PIB input. */
enum integer_format settings_get_input_integer_format (void);

/* Sets the integer format used for IB and PIB input to
   FORMAT. */
void settings_set_input_integer_format ( enum integer_format);


/* Returns the current output integer format. */
enum integer_format settings_get_output_integer_format (void);

/* Sets the output integer format to INTEGER_FORMAT. */
void settings_set_output_integer_format (enum integer_format integer_format);

/* Returns the current output float format. */
enum float_format settings_get_output_float_format (void);

/* Sets the output float format to FLOAT_FORMAT. */
void settings_set_output_float_format (enum float_format float_format);



int settings_get_viewlength (void);
void settings_set_viewlength ( int);

int settings_get_viewwidth (void);
void settings_set_viewwidth ( int);

bool settings_get_safer_mode (void);
void settings_set_safer_mode (void);

bool settings_get_echo (void);
void settings_set_echo ( bool);
bool settings_get_include (void);
void settings_set_include ( bool);

int settings_get_epoch (void);
void settings_set_epoch ( int);

bool settings_get_errorbreak (void);
void settings_set_errorbreak ( bool);

bool settings_get_error_routing_to_terminal (void);
void settings_set_error_routing_to_terminal (bool);
bool settings_get_error_routing_to_listing (void);
void settings_set_error_routing_to_listing (bool);

bool settings_get_scompression (void);
void settings_set_scompression (bool);

bool settings_get_undefined (void);
void settings_set_undefined (bool);
double settings_get_blanks (void);
void settings_set_blanks (double);

int settings_get_mxwarns (void);
void settings_set_mxwarns ( int);
int settings_get_mxerrs (void);
void settings_set_mxerrs ( int);

bool settings_get_printback (void);
void settings_set_printback (bool);
bool settings_get_mprint (void);
void settings_set_mprint (bool);

int settings_get_mxloops (void);
void settings_set_mxloops ( int);

bool settings_get_nulline (void);
void settings_set_nulline (bool);

char settings_get_endcmd (void);
void settings_set_endcmd (char);

size_t settings_get_workspace (void);
size_t settings_get_workspace_cases (size_t value_cnt);
void settings_set_workspace (size_t);

const struct fmt_spec *settings_get_format (void);
void settings_set_format ( const struct fmt_spec *);

bool settings_get_testing_mode (void);
void settings_set_testing_mode (bool);

enum behavior_mode {
  ENHANCED,             /* Use improved PSPP behavior. */
  COMPATIBLE            /* Be as compatible as possible. */
};

enum behavior_mode settings_get_algorithm (void);
void settings_set_algorithm (enum behavior_mode);
enum behavior_mode settings_get_syntax (void);
void settings_set_syntax (enum behavior_mode);

void settings_set_cmd_algorithm (enum behavior_mode);
void unset_cmd_algorithm (void);

enum fmt_type;
bool settings_set_cc (const char *cc_string, enum fmt_type type);

int settings_get_decimal_char (enum fmt_type type);
void settings_set_decimal_char (char decimal);


const struct fmt_number_style * settings_get_style (enum fmt_type type);

char * settings_dollar_template (const struct fmt_spec *fmt);

#endif /* !settings_h */
