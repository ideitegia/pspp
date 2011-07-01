/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011 Free Software Foundation, Inc.

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

#ifndef OUTPUT_ITEM_PROVIDER_H
#define OUTPUT_ITEM_PROVIDER_H 1

#include "output/output-item.h"

/* Class structure for an output item.

   This structure must be provided by an output_item subclass to initialize an
   instance of output_item. */
struct output_item_class
  {
    /* Destroys and frees ITEM.  Called when output_item_unref() drops ITEM's
       reference count to 0. */
    void (*destroy) (struct output_item *item);
  };

void output_item_init (struct output_item *, const struct output_item_class *);

#endif /* output/output-item-provider.h */
