/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>

#include <stdlib.h>

#include <data/any-reader.h>
#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/procedure.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/file-handle.h>
#include <language/lexer/lexer.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Parses and executes APPLY DICTIONARY. */
int
cmd_apply_dictionary (struct dataset *ds)
{
  struct file_handle *handle;
  struct any_reader *reader;
  struct dictionary *dict;

  int n_matched = 0;

  int i;
  
  lex_match_id ("FROM");
  lex_match ('=');
  handle = fh_parse (FH_REF_FILE | FH_REF_SCRATCH);
  if (!handle)
    return CMD_FAILURE;

  reader = any_reader_open (handle, &dict);
  if (dict == NULL)
    return CMD_FAILURE;
  any_reader_close (reader);

  for (i = 0; i < dict_get_var_cnt (dict); i++)
    {
      struct variable *s = dict_get_var (dict, i);
      struct variable *t = dict_lookup_var (dataset_dict (ds), s->name);
      if (t == NULL)
	continue;

      n_matched++;
      if (s->type != t->type)
	{
	  msg (SW, _("Variable %s is %s in target file, but %s in "
		     "source file."),
	       s->name,
	       t->type == ALPHA ? _("string") : _("numeric"),
	       s->type == ALPHA ? _("string") : _("numeric"));
	  continue;
	}

      if (s->label && strcspn (s->label, " ") != strlen (s->label))
	{
	  free (t->label);
	  t->label = s->label;
	  s->label = NULL;
	}

      if (val_labs_count (s->val_labs) && t->width > MAX_SHORT_STRING)
	msg (SW, _("Cannot add value labels from source file to "
		   "long string variable %s."),
	     s->name);
      else if (val_labs_count (s->val_labs))
	{
          if (val_labs_can_set_width (s->val_labs, t->width))
            {
              val_labs_destroy (t->val_labs);
              t->val_labs = s->val_labs;
              val_labs_set_width (t->val_labs, t->width);
              s->val_labs = val_labs_create (s->width);
            }
	}

      if (!mv_is_empty (&s->miss) && t->width > MAX_SHORT_STRING)
	msg (SW, _("Cannot apply missing values from source file to "
		   "long string variable %s."),
	     s->name);
      else if (!mv_is_empty (&s->miss))
	{
          if (mv_is_resizable (&s->miss, t->width)) 
            {
              mv_copy (&t->miss, &s->miss);
              mv_resize (&t->miss, t->width); 
            }
	}

      if (s->type == NUMERIC)
	{
	  t->print = s->print;
	  t->write = s->write;
	}
    }

  if (!n_matched)
    msg (SW, _("No matching variables found between the source "
	       "and target files."));
      
  /* Weighting. */
  if (dict_get_weight (dict) != NULL) 
    {
      struct variable *new_weight
        = dict_lookup_var (dataset_dict (ds), dict_get_weight (dict)->name);

      if (new_weight != NULL)
        dict_set_weight (dataset_dict (ds), new_weight);
    }
  
  any_reader_close (reader);

  return lex_end_of_command ();
}
