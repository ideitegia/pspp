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

#include "ui/terminal/msg-ui.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "data/settings.h"
#include "libpspp/getl.h"
#include "libpspp/message.h"
#include "libpspp/msg-locator.h"
#include "libpspp/str.h"
#include "output/journal.h"
#include "output/driver.h"
#include "output/tab.h"
#include "output/message-item.h"

#include "gl/unilbrk.h"
#include "gl/localcharset.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Number of messages reported, by severity level. */
static int counts[MSG_N_SEVERITIES];

/* True after the maximum number of errors or warnings has been exceeded. */
static bool too_many_errors;

/* True after the maximum number of notes has been exceeded. */
static bool too_many_notes;

static void handle_msg (const struct msg *);

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

/* Checks whether we've had so many errors that it's time to quit
   processing this syntax file. */
bool
msg_ui_too_many_errors (void)
{
  return too_many_errors;
}

void
msg_ui_reset_counts (void)
{
  int i;

  for (i = 0; i < MSG_N_SEVERITIES; i++)
    counts[i] = 0;
  too_many_errors = false;
  too_many_notes = false;
}

bool
msg_ui_any_errors (void)
{
  return counts[MSG_S_ERROR] > 0;
}

static void
submit_note (char *s)
{
  struct msg m;

  m.category = MSG_C_GENERAL;
  m.severity = MSG_S_NOTE;
  m.where.file_name = NULL;
  m.where.line_number = -1;
  m.text = s;
  message_item_submit (message_item_create (&m));
  free (s);
}

static void
handle_msg (const struct msg *m)
{
  int n_msgs, max_msgs;

  if (too_many_errors || (too_many_notes && m->severity == MSG_S_NOTE))
    return;

  message_item_submit (message_item_create (m));

  counts[m->severity]++;
  max_msgs = settings_get_max_messages (m->severity);
  n_msgs = counts[m->severity];
  if (m->severity == MSG_S_WARNING)
    n_msgs += counts[MSG_S_ERROR];
  if (n_msgs > max_msgs)
    {
      if (m->severity == MSG_S_NOTE)
        {
          too_many_notes = true;
          submit_note (xasprintf (_("Notes (%d) exceed limit (%d).  "
                                    "Suppressing further notes."),
                                  n_msgs, max_msgs));
        }
      else
        {
          too_many_errors = true;
          if (m->severity == MSG_S_WARNING)
            submit_note (xasprintf (_("Warnings (%d) exceed limit (%d)."),
                                    n_msgs, max_msgs));
          else
            submit_note (xasprintf (_("Errors (%d) exceed limit (%d)."),
                                    n_msgs, max_msgs));
        }
    }
}
