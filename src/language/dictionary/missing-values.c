/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "data/data-in.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "data/procedure.h"
#include "data/value.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_missing_values (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct variable **v = NULL;
  size_t nv;

  bool ok = true;

  while (lex_token (lexer) != T_ENDCMD)
    {
      size_t i;

      if (!parse_variables (lexer, dict, &v, &nv, PV_NONE))
        goto error;

      if (!lex_force_match (lexer, T_LPAREN))
        goto error;

      for (i = 0; i < nv; i++)
        var_clear_missing_values (v[i]);

      if (!lex_match (lexer, T_RPAREN))
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
                goto error;
              }

          if (var_is_numeric (v[0]))
            {
              mv_init (&mv, 0);
              while (!lex_match (lexer, T_RPAREN))
                {
                  enum fmt_type type = var_get_print_format (v[0])->type;
                  double x, y;
                  bool ok;

                  if (!parse_num_range (lexer, &x, &y, &type))
                    goto error;

                  ok = (x == y
                        ? mv_add_num (&mv, x)
                        : mv_add_range (&mv, x, y));
                  if (!ok)
                    ok = false;

                  lex_match (lexer, T_COMMA);
                }
            }
          else
            {
              mv_init (&mv, MV_MAX_STRING);
              while (!lex_match (lexer, T_RPAREN))
                {
                  uint8_t value[MV_MAX_STRING];
                  char *dict_mv;
                  size_t length;

                  if (!lex_force_string (lexer))
                    {
                      ok = false;
                      break;
                    }

                  dict_mv = recode_string (dict_get_encoding (dict), "UTF-8",
                                           lex_tokcstr (lexer),
                                           ss_length (lex_tokss (lexer)));
                  length = strlen (dict_mv);
                  if (length > MV_MAX_STRING)
                    {
                      /* XXX truncate graphemes not bytes */
                      msg (SE, _("Truncating missing value to maximum "
                                 "acceptable length (%d bytes)."),
                           MV_MAX_STRING);
                      length = MV_MAX_STRING;
                    }
                  memset (value, ' ', MV_MAX_STRING);
                  memcpy (value, dict_mv, length);
                  free (dict_mv);

                  if (!mv_add_str (&mv, value))
                    ok = false;

                  lex_get (lexer);
                  lex_match (lexer, T_COMMA);
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
                  ok = false;
                }
            }

          mv_destroy (&mv);
        }

      lex_match (lexer, T_SLASH);
      free (v);
      v = NULL;
    }

  free (v);
  return ok ? CMD_SUCCESS : CMD_FAILURE;

error:
  free (v);
  return CMD_FAILURE;
}

