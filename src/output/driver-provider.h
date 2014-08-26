/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2012, 2014 Free Software Foundation, Inc.

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

#ifndef OUTPUT_DRIVER_PROVIDER_H
#define OUTPUT_DRIVER_PROVIDER_H 1

#include <stdbool.h>

#include "data/settings.h"
#include "libpspp/compiler.h"
#include "output/driver.h"

struct output_item;
struct string_map;

/* A configured output driver. */
struct output_driver
  {
    const struct output_driver_class *class; /* Driver class. */
    char *name;                              /* Name of this driver. */
    enum settings_output_devices device_type; /* One of SETTINGS_DEVICE_*. */
  };

void output_driver_init (struct output_driver *,
                         const struct output_driver_class *,
                         const char *name, enum settings_output_devices);
void output_driver_destroy (struct output_driver *);

const char *output_driver_get_name (const struct output_driver *);

/* One kind of output driver.

   Output driver implementations must not call msg() to report errors.  This
   can lead to reentrance in the output driver, because msg() may report error
   messages using the output drivers.  Instead, this code should report errors
   with error(), which will never call into the output drivers.  */
struct output_driver_class
  {
    /* Name of this driver class. */
    const char *name;

    /* Closes and frees DRIVER. */
    void (*destroy) (struct output_driver *driver);

    /* Passes ITEM to DRIVER to be written as output.  The caller retains
       ownership of ITEM (but DRIVER may keep a copy of it by incrementing the
       reference count by calling output_item_ref). */
    void (*submit) (struct output_driver *driver,
                    const struct output_item *item);

    /* Ensures that any output items passed to the 'submit' function for DRIVER
       have actually been displayed.

       This is called from the text-based UI before showing the command prompt,
       to ensure that the user has actually been shown any preceding output If
       it doesn't make sense for DRIVER to be used this way, then this function
       need not do anything. */
    void (*flush) (struct output_driver *driver);
  };

/* Useful for output driver implementation. */
void output_driver_track_current_command (const struct output_item *, char **);

/* An abstract way for the output subsystem to create an output driver. */
struct output_driver_factory
  {
    /* The file extension, without the leading dot, e.g. "pdf". */
    const char *extension;

    /* The default file name, including extension.

       If this is "-", that implies that by default output will be directed to
       stdout. */
    const char *default_file_name;

    /* Creates a new output driver of this class.  NAME and TYPE should be
       passed directly to output_driver_init.  Returns the new output driver if
       successful, otherwise a null pointer.

       It is up to the driver class to decide how to interpret OPTIONS.  The
       create function should delete pairs that it understands from OPTIONS,
       because the caller may issue errors about unknown options for any pairs
       that remain.  The functions in output/options.h can be useful.

       The returned driver should not have been registered (with
       output_driver_register).  The caller will register the driver (if this
       is desirable). */
    struct output_driver *(*create) (const char *name,
                                     enum settings_output_devices type,
                                     struct string_map *options);
  };


#endif /* output/driver-provider.h */
