/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#include <data/data-in.h>
#include <data/missing-values.h>
#include <data/procedure.h>
#include <data/value.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <language/lexer/range-parser.h>
#include <libpspp/magic.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_missing_values (struct lexer *lexer, struct dataset *ds)
{
  struct variable **v;
  size_t nv;

  int retval = CMD_FAILURE;
  bool deferred_errors = false;

  while (lex_token (lexer) != '.')
    {
      size_t i;

      if (!parse_variables (lexer, dataset_dict (ds), &v, &nv, PV_NONE))
        goto done;

      if (!lex_match (lexer, '('))
        {
          lex_error (lexer, _("expecting `('"));
          goto done;
        }

      for (i = 0; i < nv; i++)
        var_clear_missing_values (v[i]);

      if (!lex_match (lexer, ')'))
        {
          struct missing_values mv;

          for (i = 0; i < nv; i++)
            if (var_get_type (v[i]) != var_get_type (v[0]))
              {
                const struct variable *n = var_is_numeric (v[0]) ? v[0] : v[i];
                const struct variable *s = var_is_numeric (v[0]) ? v[i] : v[0];
                msg (SE, _("Cannot mix numeric variables (e.g. %s) and "
                           "string variables (e.g. %s) within a single list."),
                     var_get_name (n), var_get_name (s));
                goto done;
              }

          if (var_is_numeric (v[0]))
            {
              mv_init (&mv, 0);
              while (!lex_match (lexer, ')'))
                {
                  enum fmt_type type = var_get_print_format (v[0])->type;
                  double x, y;
                  bool ok;

                  if (!parse_num_range (lexer, &x, &y, &type))
                    goto done;

                  ok = (x == y
                        ? mv_add_num (&mv, x)
                        : mv_add_num_range (&mv, x, y));
                  if (!ok)
                    deferred_errors = true;

                  lex_match (lexer, ',');
                }
            }
          else
            {
	      struct string value;

              mv_init (&mv, MAX_SHORT_STRING);
              while (!lex_match (lexer, ')'))
                {
                  if (!lex_force_string (lexer))
                    {
                      deferred_errors = true;
                      break;
                    }

		  ds_init_string (&value, lex_tokstr (lexer));

                  if (ds_length (&value) > MAX_SHORT_STRING)
                    {
                      ds_truncate (&value, MAX_SHORT_STRING);
                      msg (SE, _("Truncating missing value to short string "
                                 "length (%d characters)."),
                           MAX_SHORT_STRING);
                    }
                  else
                    ds_rpad (&value, MAX_SHORT_STRING, ' ');

                  if (!mv_add_str (&mv, ds_data (&value)))
                    deferred_errors = true;
		  ds_destroy (&value);

                  lex_get (lexer);
                  lex_match (lexer, ',');
                }
            }

          for (i = 0; i < nv; i++)
            {
              if (mv_is_resizable (&mv, var_get_width (v[i])))
                var_set_missing_values (v[i], &mv);
              else
                {
                  msg (SE, _("Missing values provided are too long to assign "
                             "to variable of width %d."),
                       var_get_width (v[i]));
                  deferred_errors = true;
                }
            }
        }

      lex_match (lexer, '/');
      free (v);
      v = NULL;
    }
  retval = lex_end_of_command (lexer);

 done:
  free (v);
  if (deferred_errors)
    retval = CMD_FAILURE;
  return retval;
}

