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

#include "debug-print.h"

/* Variable list. */
static struct variable **v;

/* Number of variables. */
static int nv;

static int do_value_labels (int);
static int verify_val_labs (int erase);
static int get_label (void);

#if DEBUGGING
static void debug_print (void);
#endif

/* Stubs. */

static void
init (void)
{
  v = NULL;
}

static void
done (void)
{
  free (v);
}

int
cmd_value_labels (void)
{
  int code;
  init ();
  lex_match_id ("VALUE");
  lex_match_id ("LABELS");
  code = do_value_labels (1);
  done ();
  return code;
}

int
cmd_add_value_labels (void)
{
  int code;
  lex_match_id ("ADD");
  lex_match_id ("VALUE");
  lex_match_id ("LABELS");
  code = do_value_labels (0);
  done ();
  return code;
}

/* Do it. */

static int
do_value_labels (int erase)
{
  lex_match ('/');
  
  while (token != '.')
    {
      parse_variables (default_dict, &v, &nv, PV_SAME_TYPE);
      if (!verify_val_labs (erase))
	return CMD_PART_SUCCESS_MAYBE;
      while (token != '/' && token != '.')
	if (!get_label ())
	  return CMD_PART_SUCCESS_MAYBE;

      if (token != '/')
	break;
      lex_get ();

      free (v);
      v = NULL;
    }

  if (token != '.')
    {
      lex_error (NULL);
      return CMD_TRAILING_GARBAGE;
    }

#if 0 && DEBUGGING
  debug_print ();
#endif
  return CMD_SUCCESS;
}

static int
verify_val_labs (int erase)
{
  int i;

  if (!nv)
    return 1;

  for (i = 0; i < nv; i++)
    {
      struct variable *vp = v[i];

      if (vp->type == ALPHA && vp->width > 8)
	{
	  msg (SE, _("It is not possible to assign value labels to long "
		     "string variables such as %s."), vp->name);
	  return 0;
	}

      if (erase)
        val_labs_clear (vp->val_labs);
    }
  return 1;
}

/* Parse all the labels for a particular set of variables and add the
   specified labels to those variables. */
static int
get_label (void)
{
  int i;

  /* Make sure there's some variables. */
  if (!nv)
    {
      if (token != T_STRING && token != T_NUM)
	return 0;
      lex_get ();
      return 1;
    }

  /* Parse all the labels and add them to the variables. */
  do
    {
      union value value;
      char *label;

      /* Set value. */
      if (v[0]->type == ALPHA)
	{
	  if (token != T_STRING)
	    {
	      msg (SE, _("String expected for value."));
	      return 0;
	    }
	  st_bare_pad_copy (value.s, ds_value (&tokstr), MAX_SHORT_STRING);
	}
      else
	{
	  if (token != T_NUM)
	    {
	      msg (SE, _("Number expected for value."));
	      return 0;
	    }
	  if (!lex_integer_p ())
	    msg (SW, _("Value label `%g' is not integer."), tokval);
	  value.f = tokval;
	}

      /* Set label. */
      lex_get ();
      if (!lex_force_string ())
	return 0;
      if (ds_length (&tokstr) > 60)
	{
	  msg (SW, _("Truncating value label to 60 characters."));
	  ds_truncate (&tokstr, 60);
	}
      label = ds_value (&tokstr);

      for (i = 0; i < nv; i++)
        val_labs_replace (v[i]->val_labs, value, label);

      lex_get ();
    }
  while (token != '/' && token != '.');

  return 1;
}

#if 0 && DEBUGGING
static void
debug_print ()
{
  int i;

  puts (_("Value labels:"));
  for (i = 0; i < nvar; i++)
    {
      struct hsh_iterator i;
      struct value_label *val;

      printf ("  %s\n", var[i]->name);
      if (var[i]->val_lab) 
        {
          for (val = hsh_first (var[i]->val_lab, &i); val != NULL;
               val = hsh_next (var[i]->val_lab, &i))
            if (var[i]->type == NUMERIC)
              printf ("    %g:  `%s'\n", val->v.f, val->s);
            else
              printf ("    `%.8s':  `%s'\n", val->v.s, val->s); 
        }
      else
	printf (_("    (no value labels)\n"));
    }
}
#endif /* DEBUGGING */
