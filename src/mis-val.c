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
#include "error.h"
#include <stdlib.h>
#include "command.h"
#include "data-in.h"
#include "error.h"
#include "lexer.h"
#include "magic.h"
#include "str.h"
#include "var.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include "debug-print.h"

static bool parse_number (double *, const struct fmt_spec *);

int
cmd_missing_values (void)
{
  struct variable **v;
  size_t nv;

  int retval = CMD_PART_SUCCESS_MAYBE;
  bool deferred_errors = false;

  while (token != '.')
    {
      size_t i;

      if (!parse_variables (default_dict, &v, &nv, PV_NONE)) 
        goto done;

      if (!lex_match ('('))
        {
          lex_error (_("expecting `('"));
          goto done;
        }

      for (i = 0; i < nv; i++)
        mv_init (&v[i]->miss, v[i]->width);

      if (!lex_match (')')) 
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
              while (!lex_match (')'))
                {
                  double x;

                  if (lex_match_id ("LO") || lex_match_id ("LOWEST"))
                    x = LOWEST;
                  else if (!parse_number (&x, &v[0]->print))
                    goto done;

                  if (lex_match_id ("THRU")) 
                    {
                      double y;
                      
                      if (lex_match_id ("HI") || lex_match_id ("HIGHEST"))
                        y = HIGHEST;
                      else if (!parse_number (&y, &v[0]->print))
                        goto done;

                      if (x == LOWEST && y == HIGHEST)
                        {
                          msg (SE, _("LO THRU HI is an invalid range."));
                          deferred_errors = true;
                        }
                      else if (!mv_add_num_range (&mv, x, y))
                        deferred_errors = true;
                    }
                  else
                    {
                      if (x == LOWEST) 
                        {
                          msg (SE, _("LO or LOWEST must be part of a range."));
                          deferred_errors = true;
                        }
                      else if (!mv_add_num (&mv, x))
                        deferred_errors = true;
                    }

                  lex_match (',');
                }
            }
          else 
            {
              mv_init (&mv, MAX_SHORT_STRING);
              while (!lex_match (')')) 
                {
                  if (!lex_force_string ())
                    {
                      deferred_errors = true;
                      break;
                    }

                  if (ds_length (&tokstr) > MAX_SHORT_STRING) 
                    {
                      ds_truncate (&tokstr, MAX_SHORT_STRING);
                      msg (SE, _("Truncating missing value to short string "
                                 "length (%d characters)."),
                           MAX_SHORT_STRING);
                    }
                  else
                    ds_rpad (&tokstr, MAX_SHORT_STRING, ' ');

                  if (!mv_add_str (&mv, ds_data (&tokstr)))
                    deferred_errors = true;

                  lex_get ();
                  lex_match (',');
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

      lex_match ('/');
      free (v);
      v = NULL;
    }
  retval = lex_end_of_command ();
  
 done:
  free (v);
  if (deferred_errors)
    retval = CMD_PART_SUCCESS_MAYBE;
  return retval;
}

static bool
parse_number (double *x, const struct fmt_spec *f)
{
  if (lex_is_number ()) 
    {
      *x = lex_number ();
      lex_get ();
      return true;
    }
  else if (token == T_STRING) 
    {
      struct data_in di;
      union value v;
      di.s = ds_data (&tokstr);
      di.e = ds_end (&tokstr);
      di.v = &v;
      di.flags = 0;
      di.f1 = 1;
      di.f2 = ds_length (&tokstr);
      di.format = *f;
      data_in (&di);
      lex_get ();
      *x = v.f;
      return true;
    }
  else 
    {
      lex_error (_("expecting number or data string"));
      return false; 
    }
}

