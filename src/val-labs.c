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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "hash.h"
#include "lexer.h"
#include "str.h"
#include "value-labels.h"
#include "var.h"

/* Declarations. */

static int do_value_labels (int);
static int verify_val_labs (struct variable **vars, int var_cnt);
static void erase_labels (struct variable **vars, int var_cnt);
static int get_label (struct variable **vars, int var_cnt);

/* Stubs. */

int
cmd_value_labels (void)
{
  return do_value_labels (1);
}

int
cmd_add_value_labels (void)
{
  return do_value_labels (0);
}

/* Do it. */

static int
do_value_labels (int erase)
{
  struct variable **vars; /* Variable list. */
  int var_cnt;            /* Number of variables. */
  int parse_err=0;        /* true if error parsing variables */

  lex_match ('/');
  
  while (token != '.')
    {
      parse_err = !parse_variables (default_dict, &vars, &var_cnt, 
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
      while (token != '/' && token != '.')
	if (!get_label (vars, var_cnt))
          goto lossage;

      if (token != '/')
	break;
      lex_get ();

      free (vars);
    }
  free (vars);

  if (token != '.')
    {
      lex_error (NULL);
      return CMD_TRAILING_GARBAGE;
    }

  return parse_err ? CMD_PART_SUCCESS_MAYBE : CMD_SUCCESS;

 lossage:
  free (vars);
  return CMD_PART_SUCCESS_MAYBE;
}

/* Verifies that none of the VAR_CNT variables in VARS are long
   string variables. */
static int
verify_val_labs (struct variable **vars, int var_cnt)
{
  int i;

  for (i = 0; i < var_cnt; i++)
    {
      struct variable *vp = vars[i];

      if (vp->type == ALPHA && vp->width > 8)
	{
	  msg (SE, _("It is not possible to assign value labels to long "
		     "string variables such as %s."), vp->name);
	  return 0;
	}
    }
  return 1;
}

/* Erases all the labels for the VAR_CNT variables in VARS. */
static void
erase_labels (struct variable **vars, int var_cnt) 
{
  int i;

  /* Erase old value labels if desired. */
  for (i = 0; i < var_cnt; i++)
    val_labs_clear (vars[i]->val_labs);
}

/* Parse all the labels for the VAR_CNT variables in VARS and add
   the specified labels to those variables.  */
static int
get_label (struct variable **vars, int var_cnt)
{
  /* Parse all the labels and add them to the variables. */
  do
    {
      union value value;
      char *label;
      int i;

      /* Set value. */
      if (vars[0]->type == ALPHA)
	{
	  if (token != T_STRING)
	    {
              lex_error (_("expecting string"));
	      return 0;
	    }
	  st_bare_pad_copy (value.s, ds_c_str (&tokstr), MAX_SHORT_STRING);
	}
      else
	{
	  if (token != T_NUM)
	    {
	      lex_error (_("expecting integer"));
	      return 0;
	    }
	  if (!lex_integer_p ())
	    msg (SW, _("Value label `%g' is not integer."), tokval);
	  value.f = tokval;
	}
      lex_get ();

      /* Set label. */
      if (!lex_force_string ())
	return 0;
      if (ds_length (&tokstr) > 60)
	{
	  msg (SW, _("Truncating value label to 60 characters."));
	  ds_truncate (&tokstr, 60);
	}
      label = ds_c_str (&tokstr);

      for (i = 0; i < var_cnt; i++)
        val_labs_replace (vars[i]->val_labs, value, label);

      lex_get ();
    }
  while (token != '/' && token != '.');

  return 1;
}
