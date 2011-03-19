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

#ifndef OUTPUT_ITEM_H
#define OUTPUT_ITEM_H 1

/* Output items.

   An output item is a self-contained chunk of output.  The
   following kinds of output items currently exist:

        - Tables (see output/table-item.h).

        - Charts (see output/chart-item.h).

        - Text strings (see output/text-item.h).

        - Messages (see output/message-item.h).
*/

#include <stdbool.h>
#include "libpspp/cast.h"

/* A single output item. */
struct output_item
  {
    const struct output_item_class *class;

    /* Reference count.  An output item may be shared between multiple owners,
       indicated by a reference count greater than 1.  When this is the case,
       the output item must not be modified. */
    int ref_cnt;
  };

struct output_item *output_item_ref (const struct output_item *);
void output_item_unref (struct output_item *);
bool output_item_is_shared (const struct output_item *);

#endif /* output/output-item.h */
