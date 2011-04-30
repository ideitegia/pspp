/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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

#include "language/command.h"

#include "data/dataset.h"
#include "data/session.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"
#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static int
parse_window (struct lexer *lexer, unsigned int allowed,
              enum dataset_display def)
{
  if (!lex_match_id (lexer, "WINDOW"))
    return def;
  lex_match (lexer, T_EQUALS);

  if (allowed & (1 << DATASET_MINIMIZED) && lex_match_id (lexer, "MINIMIZED"))
    return DATASET_MINIMIZED;
  else if (allowed & (1 << DATASET_ASIS) && lex_match_id (lexer, "ASIS"))
    return DATASET_ASIS;
  else if (allowed & (1 << DATASET_FRONT) && lex_match_id (lexer, "FRONT"))
    return DATASET_FRONT;
  else if (allowed & (1 << DATASET_HIDDEN) && lex_match_id (lexer, "HIDDEN"))
    return DATASET_HIDDEN;

  lex_error (lexer, NULL);
  return -1;
}

static struct dataset *
parse_dataset_name (struct lexer *lexer, struct session *session)
{
  struct dataset *ds;

  if (!lex_force_id (lexer))
    return NULL;

  ds = session_lookup_dataset (session, lex_tokcstr (lexer));
  if (ds != NULL)
    lex_get (lexer);
  else
    msg (SE, _("There is no dataset named %s."), lex_tokcstr (lexer));
  return ds;
}

int
cmd_dataset_name (struct lexer *lexer, struct dataset *active)
{
  int display;

  if (!lex_force_id (lexer))
    return CMD_FAILURE;
  dataset_set_name (active, lex_tokcstr (lexer));
  lex_get (lexer);

  display = parse_window (lexer, (1 << DATASET_ASIS) | (1 << DATASET_FRONT),
                          DATASET_ASIS);
  if (display < 0)
    return CMD_FAILURE;
  else if (display != DATASET_ASIS)
    dataset_set_display (active, display);

  return CMD_SUCCESS;
}

int
cmd_dataset_activate (struct lexer *lexer, struct dataset *active)
{
  struct session *session = dataset_session (active);
  struct dataset *ds;
  int display;

  ds = parse_dataset_name (lexer, session);
  if (ds == NULL)
    return CMD_FAILURE;

  if (ds != active)
    {
      proc_execute (active);
      session_set_active_dataset (session, ds);
      if (dataset_name (active)[0] == '\0')
        dataset_destroy (active);
      return CMD_SUCCESS;
    }

  display = parse_window (lexer, (1 << DATASET_ASIS) | (1 << DATASET_FRONT),
                          DATASET_ASIS);
  if (display < 0)
    return CMD_FAILURE;
  else if (display != DATASET_ASIS)
    dataset_set_display (ds, display);

  return CMD_SUCCESS;
}

int
cmd_dataset_copy (struct lexer *lexer, struct dataset *old)
{
  struct session *session = dataset_session (old);
  struct dataset *new;
  int display;
  char *name;

  /* Parse the entire command first.  proc_execute() can attempt to parse
     BEGIN DATA...END DATA and it will fail confusingly if we are in the
     middle of the command at the point.  */
  if (!lex_force_id (lexer))
    return CMD_FAILURE;
  name = xstrdup (lex_tokcstr (lexer));
  lex_get (lexer);

  display = parse_window (lexer, ((1 << DATASET_MINIMIZED)
                                  | (1 << DATASET_HIDDEN)
                                  | (1 << DATASET_FRONT)),
                          DATASET_MINIMIZED);
  if (display < 0)
    {
      free (name);
      return CMD_FAILURE;
    }

  if (session_lookup_dataset (session, name) == old)
    {
      new = old;
      dataset_set_name (old, "");
    }
  else
    {
      proc_execute (old);
      new = dataset_clone (old, name);
    }
  dataset_set_display (new, display);

  free (name);
  return CMD_SUCCESS;
}

int
cmd_dataset_declare (struct lexer *lexer, struct dataset *ds)
{
  struct session *session = dataset_session (ds);
  struct dataset *new;
  int display;

  if (!lex_force_id (lexer))
    return CMD_FAILURE;

  new = session_lookup_dataset (session, lex_tokcstr (lexer));
  if (new == NULL)
    new = dataset_create (session, lex_tokcstr (lexer));
  lex_get (lexer);

  display = parse_window (lexer, ((1 << DATASET_MINIMIZED)
                                  | (1 << DATASET_HIDDEN)
                                  | (1 << DATASET_FRONT)),
                          DATASET_MINIMIZED);
  if (display < 0)
    return CMD_FAILURE;
  dataset_set_display (new, display);

  return CMD_SUCCESS;
}

static void
dataset_close_cb (struct dataset *ds, void *session_)
{
  struct session *session = session_;

  if (ds != session_active_dataset (session))
    dataset_destroy (ds);
}

int
cmd_dataset_close (struct lexer *lexer, struct dataset *ds)
{
  struct session *session = dataset_session (ds);

  if (lex_match (lexer, T_ALL))
    {
      session_for_each_dataset (session, dataset_close_cb, session);
      dataset_set_name (session_active_dataset (session), "");
    }
  else
    {
      if (!lex_match (lexer, T_ASTERISK))
        {
          ds = parse_dataset_name (lexer, session);
          if (ds == NULL)
            return CMD_FAILURE;
        }

      if (ds == session_active_dataset (session))
        dataset_set_name (ds, "");
      else
        dataset_destroy (ds);
    }

  return CMD_SUCCESS;
}

static void
dataset_display_cb (struct dataset *ds, void *p_)
{
  struct dataset ***p = p_;
  **p = ds;
  (*p)++;
}

static int
sort_datasets (const void *a_, const void *b_)
{
  struct dataset *const *a = a_;
  struct dataset *const *b = b_;

  return strcmp (dataset_name (*a), dataset_name (*b));
}

int
cmd_dataset_display (struct lexer *lexer UNUSED, struct dataset *ds)
{
  struct session *session = dataset_session (ds);
  struct dataset **datasets, **p;
  struct tab_table *t;
  size_t i, n;

  n = session_n_datasets (session);
  datasets = xmalloc (n * sizeof *datasets);
  p = datasets;
  session_for_each_dataset (session, dataset_display_cb, &p);
  qsort (datasets, n, sizeof *datasets, sort_datasets);

  t = tab_create (1, n + 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, -1, TAL_1, 0, 0, tab_nc (t) - 1, tab_nr (t) - 1);
  tab_hline (t, TAL_2, 0, 0, 1);
  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Dataset"));
  for (i = 0; i < n; i++)
    {
      struct dataset *ds = datasets[i];
      const char *name;

      name = dataset_name (ds);
      if (name[0] == '\0')
        name = _("unnamed dataset");

      if (ds == session_active_dataset (session))
        tab_text_format (t, 0, i + 1, TAB_LEFT, "%s %s",
                         name, _("(active dataset)"));
      else
        tab_text (t, 0, i + 1, TAB_LEFT, name);
    }
  tab_title (t, "Open datasets.");
  tab_submit (t);

  free (datasets);

  return CMD_SUCCESS;
}
