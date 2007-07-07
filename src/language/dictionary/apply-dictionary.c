/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include <data/any-reader.h>
#include <data/casereader.h>
#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/missing-values.h>
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
cmd_apply_dictionary (struct lexer *lexer, struct dataset *ds)
{
  struct file_handle *handle;
  struct casereader *reader;
  struct dictionary *dict;

  int n_matched = 0;

  int i;

  lex_match_id (lexer, "FROM");
  lex_match (lexer, '=');
  handle = fh_parse (lexer, FH_REF_FILE | FH_REF_SCRATCH);
  if (!handle)
    return CMD_FAILURE;

  reader = any_reader_open (handle, &dict);
  if (dict == NULL)
    return CMD_FAILURE;
  casereader_destroy (reader);

  for (i = 0; i < dict_get_var_cnt (dict); i++)
    {
      struct variable *s = dict_get_var (dict, i);
      struct variable *t = dict_lookup_var (dataset_dict (ds),
                                            var_get_name (s));
      if (t == NULL)
	continue;

      n_matched++;
      if (var_get_type (s) != var_get_type (t))
	{
	  msg (SW, _("Variable %s is %s in target file, but %s in "
		     "source file."),
	       var_get_name (s),
	       var_is_alpha (t) ? _("string") : _("numeric"),
	       var_is_alpha (s) ? _("string") : _("numeric"));
	  continue;
	}

      if (var_get_label (s))
        {
          const char *label = var_get_label (s);
          if (strcspn (label, " ") != strlen (label))
            var_set_label (t, label);
        }

      if (var_has_value_labels (s))
        {
          if (!var_is_long_string (t))
            {
              const struct val_labs *value_labels = var_get_value_labels (s);
              if (val_labs_can_set_width (value_labels, var_get_width (t)))
                var_set_value_labels (s, value_labels);
            }
          else
            msg (SW, _("Cannot add value labels from source file to "
                       "long string variable %s."),
                 var_get_name (s));
        }

      if (var_has_missing_values (s))
        {
          if (!var_is_long_string (t))
            {
              const struct missing_values *miss = var_get_missing_values (s);
              if (mv_is_resizable (miss, var_get_width (t)))
                var_set_missing_values (t, miss);
            }
          else
            msg (SW, _("Cannot apply missing values from source file to "
                       "long string variable %s."),
                 var_get_name (s));
        }

      if (var_is_numeric (s))
	{
          var_set_print_format (t, var_get_print_format (s));
          var_set_write_format (t, var_get_write_format (s));
	}
    }

  if (!n_matched)
    msg (SW, _("No matching variables found between the source "
	       "and target files."));

  /* Weighting. */
  if (dict_get_weight (dict) != NULL)
    {
      struct variable *new_weight
        = dict_lookup_var (dataset_dict (ds),
                           var_get_name (dict_get_weight (dict)));

      if (new_weight != NULL)
        dict_set_weight (dataset_dict (ds), new_weight);
    }

  return lex_end_of_command (lexer);
}
