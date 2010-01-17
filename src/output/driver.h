/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009 Free Software Foundation, Inc.

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
struct string_map;
struct string_set;

void output_close (void);

struct output_driver *output_driver_create (const char *class_name,
                                            struct string_map *options);

bool output_define_macro (const char *, struct string_map *macros);
void output_read_configuration (const struct string_map *macros,
                                const struct string_set *drivers);
void output_configure_driver (const char *);

void output_list_classes (void);

void output_submit (struct output_item *);
void output_flush (void);

/* Device types. */
enum output_device_type
  {
    OUTPUT_DEVICE_UNKNOWN,	/* Unknown type of device. */
    OUTPUT_DEVICE_LISTING,	/* Listing device. */
    OUTPUT_DEVICE_SCREEN,	/* Screen device. */
    OUTPUT_DEVICE_PRINTER	/* Printer device. */
  };
unsigned int output_get_enabled_types (void);
void output_set_enabled_types (unsigned int);
void output_set_type_enabled (bool enable, enum output_device_type);

#endif /* output/driver.h */
