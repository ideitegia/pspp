/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2010, 2012, 2013 Free Software Foundation, Inc.

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

#include "output/journal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "data/file-name.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "output/driver-provider.h"
#include "output/message-item.h"
#include "output/text-item.h"

#include "gl/fwriteerror.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct journal_driver
  {
    struct output_driver driver;
    FILE *file;
    char *command_name;

    /* Name of journal file. */
    char *file_name;
    bool destroyed;
  };

static const struct output_driver_class journal_class;

/* Journal driver, if journaling is enabled. */
static struct journal_driver journal;


static struct journal_driver *
journal_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &journal_class);
  return UP_CAST (driver, struct journal_driver, driver);
}

static void
journal_close (void)
{
  if (journal.file != NULL)
    {
      if (fwriteerror (journal.file))
        msg_error (errno, _("error writing output file `%s'"),
		   journal.file_name);

      }
  journal.file = NULL;
}

static void
journal_destroy (struct output_driver *driver)
{
  struct journal_driver *j = journal_driver_cast (driver);

  if ( !j->destroyed)
    {
      journal_close ();
      free (j->command_name);
    }

  j->destroyed = true;
}

static void
journal_output (struct journal_driver *j, const char *s)
{
  if ( j->file == NULL)
    return;

  fprintf (j->file, "%s\n", s);

  /* Flush the journal in case the syntax we're about to write
     causes a crash.  Having the syntax already written to disk
     makes postmortem analysis of the problem possible. */
  fflush (j->file);
}

static void
journal_submit (struct output_driver *driver, const struct output_item *item)
{
  struct journal_driver *j = journal_driver_cast (driver);

  output_driver_track_current_command (item, &j->command_name);

  if (is_text_item (item))
    {
      const struct text_item *text_item = to_text_item (item);
      enum text_item_type type = text_item_get_type (text_item);

      if (type == TEXT_ITEM_SYNTAX)
        journal_output (j, text_item_get_text (text_item));
    }
  else if (is_message_item (item))
    {
      const struct message_item *message_item = to_message_item (item);
      const struct msg *msg = message_item_get_msg (message_item);
      char *s = msg_to_string (msg, j->command_name);
      journal_output (j, s);
      free (s);
    }
}

static const struct output_driver_class journal_class =
  {
    "journal",
    journal_destroy,
    journal_submit,
    NULL                        /* flush */
  };



/* Enables journaling. */
void
journal_init (void)
{
  /* Create journal driver. */
  output_driver_init (&journal.driver, &journal_class, "journal",
		      SETTINGS_DEVICE_UNFILTERED);
  journal.file = NULL;
  journal.command_name = NULL;
  
  /* Register journal driver. */
  output_driver_register (&journal.driver);

  journal_enable ();
  journal.destroyed = false;
}

/* Disables journaling. */
void
journal_disable (void)
{
  journal_close ();
}


/* Enable journaling. */
void
journal_enable (void)
{
  if (journal.file == NULL)
    {
      journal.file = fopen (journal_get_file_name (), "a");
      if (journal.file == NULL)
        {
          msg_error (errno, _("error opening output file `%s'"),
		     journal_get_file_name ());
	  journal_close ();
        }
    }
}


/* Returns true if journaling is enabled, false otherwise. */
bool
journal_is_enabled (void)
{
  return journal.file != NULL ;
}

/* Sets the name of the journal file to FILE_NAME. */
void
journal_set_file_name (const char *file_name)
{
  journal_close ();
  free (journal.file_name);
  journal.file_name = xstrdup (file_name);
}

/* Returns the name of the journal file.  The caller must not modify or free
   the returned string. */
const char *
journal_get_file_name (void)
{
  if (journal.file_name == NULL)
    {
      const char *output_path = default_output_path ();
      journal.file_name = xasprintf ("%s%s", output_path, "pspp.jnl");
    }
  return journal.file_name;
}
