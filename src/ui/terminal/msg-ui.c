/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010 Free Software Foundation, Inc.

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

#include "msg-ui.h"
#include "libpspp/message.h"
#include "libpspp/msg-locator.h"
#include "output/message-item.h"

static void
handle_msg (const struct msg *m)
{
  message_item_submit (message_item_create (m));
}

void
msg_ui_init (struct source_stream *ss)
{
  msg_init (ss, handle_msg);
}

void
msg_ui_done (void)
{
  msg_done ();
  msg_locator_done ();
}
