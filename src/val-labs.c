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
#include "avl.h"
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "str.h"
#include "var.h"

/* Declarations. */

#undef DEBUGGING
/*#define DEBUGGING 1 */
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
      parse_variables (NULL, &v, &nv, PV_SAME_TYPE);
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

#if DEBUGGING
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

      if (erase && v[i]->val_lab)
	{
	  avl_destroy (vp->val_lab, free_val_lab);
	  vp->val_lab = NULL;
	}
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
      struct value_label *label;

      /* Allocate label. */
      label = xmalloc (sizeof *label);
#if __CHECKER__
      memset (&label->v, 0, sizeof label->v);
#endif
      label->ref_count = nv;

      /* Set label->v. */
      if (v[0]->type == ALPHA)
	{
	  if (token != T_STRING)
	    {
	      msg (SE, _("String expected for value."));
	      return 0;
	    }
	  st_bare_pad_copy (label->v.s, ds_value (&tokstr), MAX_SHORT_STRING);
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
	  label->v.f = tokval;
	}

      /* Set label->s. */
      lex_get ();
      if (!lex_force_string ())
	return 0;
      if (ds_length (&tokstr) > 60)
	{
	  msg (SW, _("Truncating value label to 60 characters."));
	  ds_truncate (&tokstr, 60);
	}
      label->s = xstrdup (ds_value (&tokstr));

      for (i = 0; i < nv; i++)
	{
	  if (!v[i]->val_lab)
	    v[i]->val_lab = avl_create (NULL, val_lab_cmp,
					(void *) (v[i]->width));
	  
	  {
	    struct value_label *old;
	    
	    old = avl_replace (v[i]->val_lab, label);
	    if (old)
	      free_value_label (old);
	  }
	}

      lex_get ();
    }
  while (token != '/' && token != '.');

  return 1;
}

#if DEBUGGING
static void
debug_print ()
{
  int i;

  puts (_("Value labels:"));
  for (i = 0; i < nvar; i++)
    {
      AVLtraverser *t = NULL;
      struct value_label *val;

      printf ("  %s\n", var[i]->name);
      if (var[i]->val_lab)
	if (var[i]->type == NUMERIC)
	  for (val = avltrav (var[i]->val_lab, &t);
	       val; val = avltrav (var[i]->val_lab, &t))
	    printf ("    %g:  `%s'\n", val->v.f, val->s);
	else
	  for (val = avltrav (var[i]->val_lab, &t);
	       val; val = avltrav (var[i]->val_lab, &t))
	    printf ("    `%.8s':  `%s'\n", val->v.s, val->s);
      else
	printf (_("    (no value labels)\n"));
    }
}
#endif /* DEBUGGING */

/* Compares two value labels and returns a strcmp()-type result. */
int
val_lab_cmp (const void *a, const void *b, void *param)
{
  if ((int) param)
    return strncmp (((struct value_label *) a)->v.s,
		    ((struct value_label *) b)->v.s,
		    (int) param);
  else
    {
      int temp = (((struct value_label *) a)->v.f
		  - ((struct value_label *) b)->v.f);
      if (temp > 0)
	return 1;
      else if (temp < 0)
	return -1;
      else
	return 0;
    }
}

/* Callback function to increment the reference count for a value
   label. */
void *
inc_ref_count (void *pv, void *param unused)
{
  ((struct value_label *) pv)->ref_count++;
  return pv;
}

/* Copy the avl tree of value labels and return a pointer to the
   copy. */
avl_tree *
copy_value_labels (avl_tree *src)
{
  avl_tree *dest;

  if (src == NULL)
    return NULL;
  dest = avl_copy (NULL, src, inc_ref_count);

  return dest;
}
