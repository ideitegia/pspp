/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010 Free Software Foundation, Inc.

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

#ifndef OUTPUT_OPTIONS_H
#define OUTPUT_OPTIONS_H 1

/* Helper functions for driver option parsing. */

#include <stdbool.h>
#include "libpspp/compiler.h"

struct output_driver;
struct string_map;

/* An option being parsed. */
struct driver_option
  {
    char *driver_name;          /* Driver's name, for use in error messages. */
    char *name;                 /* Option name, for use in error messages.  */
    char *value;                /* Value supplied by user (NULL if none). */
    char *default_value;        /* Default value supplied by driver. */
  };

struct driver_option *driver_option_create (const char *driver_name,
                                            const char *name,
                                            const char *value,
                                            const char *default_value);
struct driver_option *driver_option_get (struct output_driver *,
                                         struct string_map *,
                                         const char *name,
                                         const char *default_value);
void driver_option_destroy (struct driver_option *);

void parse_paper_size (struct driver_option *, int *h, int *v);
bool parse_boolean (struct driver_option *);
int parse_enum (struct driver_option *, ...) SENTINEL(0);
int parse_int (struct driver_option *, int min_value, int max_value);
int parse_dimension (struct driver_option *);
char *parse_string (struct driver_option *);
char *parse_chart_file_name (struct driver_option *);

#endif /* output/options.h */
