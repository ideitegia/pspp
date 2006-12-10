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

#include <stdio.h>
#include <stdlib.h>

#include <data/procedure.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Declarations. */

static int do_value_labels (struct lexer *, 
			    const struct dictionary *dict, int);
static int verify_val_labs (struct variable **vars, size_t var_cnt);
static void erase_labels (struct variable **vars, size_t var_cnt);
static int get_label (struct lexer *, struct variable **vars, size_t var_cnt);

/* Stubs. */

int
cmd_value_labels (struct lexer *lexer, struct dataset *ds)
{
  return do_value_labels (lexer, dataset_dict (ds), 1);
}

int
cmd_add_value_labels (struct lexer *lexer, struct dataset *ds)
{
  return do_value_labels (lexer, dataset_dict (ds), 0);
}

/* Do it. */

static int
do_value_labels (struct lexer *lexer, const struct dictionary *dict, int erase)
{
  struct variable **vars; /* Variable list. */
  size_t var_cnt;         /* Number of variables. */
  int parse_err=0;        /* true if error parsing variables */

  lex_match (lexer, '/');
  
  while (lex_token (lexer) != '.')
    {
      parse_err = !parse_variables (lexer, dict, &vars, &var_cnt, 
				    PV_SAME_TYPE) ;
      if (var_cnt < 1)
	{
	  free(vars);
	  return CMD_FAILURE;
	}
      if (!verify_val_labs (vars, var_cnt))
        goto lossage;
      if (erase)
        erase_labels (vars, var_cnt);
      while (lex_token (lexer) != '/' && lex_token (lexer) != '.')
	if (!get_label (lexer, vars, var_cnt))
          goto lossage;

      if (lex_token (lexer) != '/')
	{
          free (vars);
          break;
	}

      lex_get (lexer);

      free (vars);
    }

  if (parse_err)
    return CMD_FAILURE;

  return lex_end_of_command (lexer);

 lossage:
  free (vars);
  return CMD_FAILURE;
}

/* Verifies that none of the VAR_CNT variables in VARS are long
   string variables. */
static int
verify_val_labs (struct variable **vars, size_t var_cnt)
{
  size_t i;

  for (i = 0; i < var_cnt; i++)
    {
      struct variable *vp = vars[i];

      if (var_is_long_string (vp))
	{
	  msg (SE, _("It is not possible to assign value labels to long "
		     "string variables such as %s."), var_get_name (vp));
	  return 0;
	}
    }
  return 1;
}

/* Erases all the labels for the VAR_CNT variables in VARS. */
static void
erase_labels (struct variable **vars, size_t var_cnt) 
{
  size_t i;

  /* Erase old value labels if desired. */
  for (i = 0; i < var_cnt; i++)
    var_clear_value_labels (vars[i]);
}

/* Parse all the labels for the VAR_CNT variables in VARS and add
   the specified labels to those variables.  */
static int
get_label (struct lexer *lexer, struct variable **vars, size_t var_cnt)
{
  /* Parse all the labels and add them to the variables. */
  do
    {
      union value value;
      struct string label;
      size_t i;

      /* Set value. */
      if (var_is_alpha (vars[0]))
	{
	  if (lex_token (lexer) != T_STRING)
	    {
              lex_error (lexer, _("expecting string"));
	      return 0;
	    }
	  buf_copy_str_rpad (value.s, MAX_SHORT_STRING, ds_cstr (lex_tokstr (lexer)));
	}
      else
	{
	  if (!lex_is_number (lexer))
	    {
	      lex_error (lexer, _("expecting integer"));
	      return 0;
	    }
	  if (!lex_is_integer (lexer))
	    msg (SW, _("Value label `%g' is not integer."), lex_tokval (lexer));
	  value.f = lex_tokval (lexer);
	}
      lex_get (lexer);
      lex_match (lexer, ',');

      /* Set label. */
      if (!lex_force_string (lexer))
	return 0;
      
      ds_init_string (&label, lex_tokstr (lexer));

      if (ds_length (&label) > 60)
	{
	  msg (SW, _("Truncating value label to 60 characters."));
	  ds_truncate (&label, 60);
	}

      for (i = 0; i < var_cnt; i++)
        var_replace_value_label (vars[i], &value, ds_cstr (&label));

      ds_destroy (&label);

      lex_get (lexer);
      lex_match (lexer, ',');
    }
  while (lex_token (lexer) != '/' && lex_token (lexer) != '.');

  return 1;
}
