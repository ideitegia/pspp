/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2014 Free Software Foundation, Inc.

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

#ifndef OUTPUT_DRIVER_H
#define OUTPUT_DRIVER_H 1

#include <stdbool.h>

struct output_item;
struct string_set;
struct string_map;

void output_get_supported_formats (struct string_set *);

void output_engine_push (void);
void output_engine_pop (void);

void output_submit (struct output_item *);
void output_flush (void);

struct output_driver *output_driver_create (struct string_map *options);
bool output_driver_is_registered (const struct output_driver *);

void output_driver_register (struct output_driver *);
void output_driver_unregister (struct output_driver *);

#endif /* output/driver.h */
