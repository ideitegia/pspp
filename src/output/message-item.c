/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Sonftware Foundation, Inc.

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

#include <config.h>

#include "output/message-item.h"

#include <stdlib.h>

#include "libpspp/message.h"
#include "output/driver.h"
#include "output/output-item-provider.h"

#include "gl/xalloc.h"

struct message_item *
message_item_create (const struct msg *msg)
{
  struct message_item *item;

  item = xmalloc (sizeof *msg);
  output_item_init (&item->output_item, &message_item_class);
  item->msg = msg_dup (msg);

  return item;
}

const struct msg *
message_item_get_msg (const struct message_item *item)
{
  return item->msg;
}

static void
message_item_destroy (struct output_item *output_item)
{
  struct message_item *item = to_message_item (output_item);
  msg_destroy (item->msg);
  free (item);
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
message_item_submit (struct message_item *item)
{
  output_submit (&item->output_item);
}

const struct output_item_class message_item_class =
  {
    message_item_destroy,
  };
