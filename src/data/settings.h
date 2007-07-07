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

void settings_init (void);
void settings_done (void);

void force_long_view (void);
int get_viewlength (void);
void set_viewlength (int);

int get_viewwidth (void);
void set_viewwidth (int);

bool get_safer_mode (void);
void set_safer_mode (void);

bool get_echo (void);
void set_echo (bool);
bool get_include (void);
void set_include (bool);

int get_epoch (void);
void set_epoch (int);

bool get_errorbreak (void);
void set_errorbreak (bool);

bool get_error_routing_to_terminal (void);
void set_error_routing_to_terminal (bool);
bool get_error_routing_to_listing (void);
void set_error_routing_to_listing (bool);

bool get_scompression (void);
void set_scompression (bool);

bool get_undefined (void);
void set_undefined (bool);
double get_blanks (void);
void set_blanks (double);

int get_mxwarns (void);
void set_mxwarns (int);
int get_mxerrs (void);
void set_mxerrs (int);

bool get_printback (void);
void set_printback (bool);
bool get_mprint (void);
void set_mprint (bool);

int get_mxloops (void);
void set_mxloops (int);

bool get_nulline (void);
void set_nulline (bool);

char get_endcmd (void);
void set_endcmd (char);

size_t get_workspace (void);
size_t get_workspace_cases (size_t value_cnt);
void set_workspace (size_t);

const struct fmt_spec *get_format (void);
void set_format (const struct fmt_spec *);

bool get_testing_mode (void);
void set_testing_mode (bool);

enum behavior_mode {
  ENHANCED,             /* Use improved PSPP behavior. */
  COMPATIBLE            /* Be as compatible as possible. */
};

enum behavior_mode get_algorithm (void);
void set_algorithm (enum behavior_mode);
enum behavior_mode get_syntax (void);
void set_syntax(enum behavior_mode);
void set_cmd_algorithm (enum behavior_mode);
void unset_cmd_algorithm (void);

#endif /* !settings_h */
