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

#if !settings_h
#define settings_h 1

#include <stdbool.h>
#include <stddef.h>

/* Types of routing. */
enum
  {
    SET_ROUTE_SCREEN = 001,	/* Output to screen devices? */
    SET_ROUTE_LISTING = 002,	/* Output to listing devices? */
    SET_ROUTE_OTHER = 004,	/* Output to other devices? */
    SET_ROUTE_DISABLE = 010	/* Disable output--overrides all other bits. */
  };

void settings_init (void);
void settings_done (void);

void force_long_view (void);
int get_viewlength (void);
void set_viewlength (int);

int get_viewwidth (void);
void set_viewwidth (int);

bool get_safer_mode (void);
void set_safer_mode (void);

char get_decimal (void);
void set_decimal (char);
char get_grouping (void);
void set_grouping (char);

const char *get_prompt (void);
void set_prompt (const char *);
const char *get_cprompt (void);
void set_cprompt (const char *);
const char *get_dprompt (void);
void set_dprompt (const char *);

bool get_echo (void);
void set_echo (bool);
bool get_include (void);
void set_include (bool);

int get_epoch (void);
void set_epoch (int);

bool get_errorbreak (void);
void set_errorbreak (bool);

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
void set_workspace (size_t);

const struct fmt_spec *get_format (void);
void set_format (const struct fmt_spec *);

/* One custom currency specification. */
#define CC_WIDTH 16
struct custom_currency
  {
    char neg_prefix[CC_WIDTH];	/* Negative prefix. */
    char prefix[CC_WIDTH];	/* Prefix. */
    char suffix[CC_WIDTH];	/* Suffix. */
    char neg_suffix[CC_WIDTH];	/* Negative suffix. */
    char decimal;		/* Decimal point. */
    char grouping;		/* Grouping character. */
  };

const struct custom_currency *get_cc (int idx);
void set_cc (int idx, const struct custom_currency *);

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
