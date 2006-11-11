/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
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

#include <data/data-in.h>
#include <data/procedure.h>
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
        mv_init (&v[i]->miss, v[i]->width);

      if (!lex_match (lexer, ')')) 
        {
          struct missing_values mv;

          for (i = 0; i < nv; i++)
            if (v[i]->type != v[0]->type)
              {
                const struct variable *n = v[0]->type == NUMERIC ? v[0] : v[i];
                const struct variable *s = v[0]->type == NUMERIC ? v[i] : v[0];
                msg (SE, _("Cannot mix numeric variables (e.g. %s) and "
                           "string variables (e.g. %s) within a single list."),
                     n->name, s->name);
                goto done;
              }

          if (v[0]->type == NUMERIC) 
            {
              mv_init (&mv, 0);
              while (!lex_match (lexer, ')'))
                {
                  double x, y;
                  bool ok;

                  if (!parse_num_range (lexer, &x, &y, &v[0]->print))
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
              if (!mv_is_resizable (&mv, v[i]->width)) 
                {
                  msg (SE, _("Missing values provided are too long to assign "
                             "to variable of width %d."),
                       v[i]->width);
                  deferred_errors = true;
                }
              else 
                {
                  mv_copy (&v[i]->miss, &mv);
                  mv_resize (&v[i]->miss, v[i]->width);
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

