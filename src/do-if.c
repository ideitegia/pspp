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
#include "do-ifP.h"
#include "error.h"
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "expressions/public.h"
#include "lexer.h"
#include "str.h"
#include "var.h"

#include "debug-print.h"

/* *INDENT-OFF* */
/* Description of DO IF transformations:

   DO IF has two transformations.  One is a conditional jump around
   a false condition.  The second is an unconditional jump around
   the rest of the code after a true condition.  Both of these types
   have their destinations backpatched in by the next clause (ELSE IF,
   END IF).

   The characters `^V<>' are meant to represent arrows.

   1. DO IF
 V<<<<if false
 V
 V *. Transformations executed when the condition on DO IF is true.
 V
 V 2. GOTO>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>V
 V    								       V
 >>1. ELSE IF							       V
 V<<<<if false							       V
 V 								       V
 V *. Transformations executed when condition on 1st ELSE IF is true.  V
 V 								       V
 V 2. GOTO>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>V
 V 								       V
 >>1. ELSE IF							       V
 V<<<<if false							       V
 V    								       V
 V *. Transformations executed when condition on 2nd ELSE IF is true.  V
 V								       V
 V 2. GOTO>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>V
 V								       V
 >>*. Transformations executed when no condition is true. (ELSE)       V
  								       V
   *. Transformations after DO IF structure.<<<<<<<<<<<<<<<<<<<<<<<<<<<<

*/
/* *INDENT-ON* */

static struct do_if_trns *parse_do_if (void);
static void add_ELSE_IF (struct do_if_trns *);
static trns_proc_func goto_trns_proc, do_if_trns_proc;
static trns_free_func do_if_trns_free;

/* Parse DO IF. */
int
cmd_do_if (void)
{
  struct do_if_trns *t;

  /* Parse the transformation. */
  t = parse_do_if ();
  if (!t)
    return CMD_FAILURE;

  /* Finish up the transformation, add to control stack, add to
     transformation list. */
  t->brk = NULL;
  t->ctl.type = CST_DO_IF;
  t->ctl.down = ctl_stack;
  t->ctl.trns = (struct trns_header *) t;
  t->ctl.brk = NULL;
  t->has_else = 0;
  ctl_stack = &t->ctl;
  add_transformation ((struct trns_header *) t);

  return CMD_SUCCESS;
}

/* Parse ELSE IF. */
int
cmd_else_if (void)
{
  /* Transformation created. */
  struct do_if_trns *t;

  /* Check that we're in a pleasing situation. */
  if (!ctl_stack || ctl_stack->type != CST_DO_IF)
    {
      msg (SE, _("There is no DO IF to match with this ELSE IF."));
      return CMD_FAILURE;
    }
  if (((struct do_if_trns *) ctl_stack->trns)->has_else)
    {
      msg (SE, _("The ELSE command must follow all ELSE IF commands "
		 "in a DO IF structure."));
      return CMD_FAILURE;
    }

  /* Parse the transformation. */
  t = parse_do_if ();
  if (!t)
    return CMD_FAILURE;

  /* Stick in the breakout transformation. */
  t->brk = xmalloc (sizeof *t->brk);
  t->brk->h.proc = goto_trns_proc;
  t->brk->h.free = NULL;

  /* Add to list of transformations, add to string of ELSE IFs. */
  add_transformation ((struct trns_header *) t->brk);
  add_transformation ((struct trns_header *) t);

  add_ELSE_IF (t);

  if (token != '.')
    {
      msg (SE, _("End of command expected."));
      return CMD_TRAILING_GARBAGE;
    }

  return CMD_SUCCESS;
}

/* Parse ELSE. */
int
cmd_else (void)
{
  struct do_if_trns *t;

  /* Check that we're in a pleasing situation. */
  if (!ctl_stack || ctl_stack->type != CST_DO_IF)
    {
      msg (SE, _("There is no DO IF to match with this ELSE."));
      return CMD_FAILURE;
    }
  
  if (((struct do_if_trns *) ctl_stack->trns)->has_else)
    {
      msg (SE, _("There may be at most one ELSE clause in each DO IF "
		 "structure.  It must be the last clause."));
      return CMD_FAILURE;
    }

  /* Note that the ELSE transformation is *not* added to the list of
     transformations.  That's because it doesn't need to do anything.
     Its goto transformation *is* added, because that's necessary.
     The main DO IF do_if_trns is the destructor for this ELSE
     do_if_trns. */
  t = xmalloc (sizeof *t);
  t->next = NULL;
  t->brk = xmalloc (sizeof *t->brk);
  t->brk->h.proc = goto_trns_proc;
  t->brk->h.free = NULL;
  t->cond = NULL;
  add_transformation ((struct trns_header *) t->brk);
  t->h.index = t->brk->h.index + 1;

  /* Add to string of ELSE IFs. */
  add_ELSE_IF (t);

  return lex_end_of_command ();
}

/* Parse END IF. */
int
cmd_end_if (void)
{
  /* List iterator. */
  struct do_if_trns *iter;

  /* Check that we're in a pleasing situation. */
  if (!ctl_stack || ctl_stack->type != CST_DO_IF)
    {
      msg (SE, _("There is no DO IF to match with this END IF."));
      return CMD_FAILURE;
    }

  /* Chain down the list, backpatching destinations for gotos. */
  iter = (struct do_if_trns *) ctl_stack->trns;
  for (;;)
    {
      if (iter->brk)
	iter->brk->dest = n_trns;
      iter->missing_jump = n_trns;
      if (iter->next)
	iter = iter->next;
      else
	break;
    }
  iter->false_jump = n_trns;

  /* Pop control stack. */
  ctl_stack = ctl_stack->down;

  return lex_end_of_command ();
}

/* Adds an ELSE IF or ELSE to the chain of them that hangs off the
   main DO IF. */
static void
add_ELSE_IF (struct do_if_trns * t)
{
  /* List iterator. */
  struct do_if_trns *iter;

  iter = (struct do_if_trns *) ctl_stack->trns;
  while (iter->next)
    iter = iter->next;
  assert (iter != NULL);

  iter->next = t;
  iter->false_jump = t->h.index;
}

/* Parses a DO IF or ELSE IF command and returns a pointer to a mostly
   filled in transformation. */
static struct do_if_trns *
parse_do_if (void)
{
  struct do_if_trns *t;
  struct expression *e;

  e = expr_parse (default_dict, EXPR_BOOLEAN);
  if (!e)
    return NULL;
  if (token != '.')
    {
      expr_free (e);
      lex_error (_("expecting end of command"));
      return NULL;
    }

  t = xmalloc (sizeof *t);
  t->h.proc = do_if_trns_proc;
  t->h.free = do_if_trns_free;
  t->next = NULL;
  t->cond = e;

  return t;
}

/* Executes a goto transformation. */
static int 
goto_trns_proc (struct trns_header * t, struct ccase * c UNUSED,
                int case_num UNUSED)
{
  return ((struct goto_trns *) t)->dest;
}

static int 
do_if_trns_proc (struct trns_header * trns, struct ccase * c,
                 int case_num UNUSED)
{
  struct do_if_trns *t = (struct do_if_trns *) trns;
  double boolean;

  boolean = expr_evaluate_num (t->cond, c, case_num);
  if (boolean == 1.0)
    {
      debug_printf ((_("DO IF %d: true\n"), t->h.index));
      return -1;
    }
  else if (boolean == 0.0)
    {
      debug_printf ((_("DO IF %d: false\n"), t->h.index));
      return t->false_jump;
    }
  else
    {
      debug_printf ((_("DO IF %d: missing\n"), t->h.index));
      return t->missing_jump;
    }
}

static void 
do_if_trns_free (struct trns_header * trns)
{
  struct do_if_trns *t = (struct do_if_trns *) trns;
  expr_free (t->cond);

  /* If brk is NULL then this is the main DO IF; therefore we
     need to chain down to the ELSE and delete it. */
  if (t->brk == NULL)
    {
      struct do_if_trns *iter = t->next;
      while (iter)
	{
	  if (!iter->cond)
	    {
	      /* This is the ELSE. */
	      free (iter);
	      break;
	    }
	  iter = iter->next;
	}
    }
}
