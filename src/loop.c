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
#include <assert.h>
#include "alloc.h"
#include "approx.h"
#include "command.h"
#include "do-ifP.h"
#include "error.h"
#include "expr.h"
#include "lexer.h"
#include "settings.h"
#include "str.h"
#include "var.h"

#include "debug-print.h"

/* *INDENT-OFF* */
/* LOOP strategy:

   Each loop causes 3 different transformations to be output.  The
   first two are output when the LOOP command is encountered; the last
   is output when the END LOOP command is encountered.

   The first to be output resets the pass number in the second
   transformation to -1.  This ensures that the pass number is set to
   -1 every time the loop is encountered, before the first iteration.

   The second transformation increments the pass number.  If there is
   no indexing or test clause on either LOOP or END LOOP, then the
   pass number is checked against MXLOOPS and control may pass out of
   the loop; otherwise the indexing or test clause(s) on LOOP are
   checked, and again control may pass out of the loop.

   After the second transformation the body of the loop is executed.

   The last transformation checks the test clause if present and
   either jumps back up to the second transformation or terminates the
   loop.

   Flow of control: (The characters ^V<> represents arrows.)

     1. LOOP (sets pass # to -1)
	 V
	 V
   >>2. LOOP (increment pass number)
   ^         (test optional indexing clause)
   ^         (test optional IF clause)
   ^    if we need another trip     if we're done with the loop>>V
   ^     V                                                       V
   ^     V 				           		    V
   ^ *. execute loop body		      	   		    V
   ^    .		    		      	   		    V
   ^    .   (any number of transformations)	   		    V
   ^    .				                    	    V
   ^                                                             V
   ^ 3. END LOOP (test optional IF clause)               	    V
   ^<<<<if we need another trip     if we're done with the loop>>V
								 V
								 V
     *. transformations after loop body<<<<<<<<<<<<<<<<<<<<<<<<<<<

 */
/* *INDENT-ON* */

/* Types of limits on loop execution. */
enum
  {
    LPC_INDEX = 001,		/* Limited by indexing clause. */
    LPC_COND = 002,		/* Limited by IF clause. */
    LPC_RINDEX = 004		/* Indexing clause counts downward, at least
				   for this pass thru the loop. */
  };

/* LOOP transformation 1. */
struct loop_1_trns
  {
    struct trns_header h;

    struct loop_2_trns *two;	/* Allows modification of associated
				   second transformation. */

    struct expression *init;	/* Starting index. */
    struct expression *incr;	/* Index increment. */
    struct expression *term;	/* Terminal index. */
  };

/* LOOP transformation 2. */
struct loop_2_trns
  {
    struct trns_header h;

    struct ctl_stmt ctl;	/* Nesting control info. */

    int flags;			/* Types of limits on loop execution. */
    int pass;			/* Number of passes thru the loop so far. */

    struct variable *index;	/* Index variable. */
    double curr;		/* Current index. */
    double incr;		/* Increment. */
    double term;		/* Terminal index. */

    struct expression *cond;	/* Optional IF condition when non-NULL. */

    int loop_term;		/* 1+(t_trns[] index of transformation 3);
				   backpatched in by END LOOP. */
  };

/* LOOP transformation 3.  (Actually output by END LOOP.)  */
struct loop_3_trns
  {
    struct trns_header h;

    struct expression *cond;	/* Optional IF condition when non-NULL. */

    int loop_start;		/* t_trns[] index of transformation 2. */
  };

/* LOOP transformations being created. */
static struct loop_1_trns *one;
static struct loop_2_trns *two;
static struct loop_3_trns *thr;

static int internal_cmd_loop (void);
static int internal_cmd_end_loop (void);
static int break_trns_proc (struct trns_header *, struct ccase *);
static int loop_1_trns_proc (struct trns_header *, struct ccase *);
static void loop_1_trns_free (struct trns_header *);
static int loop_2_trns_proc (struct trns_header *, struct ccase *);
static void loop_2_trns_free (struct trns_header *);
static int loop_3_trns_proc (struct trns_header *, struct ccase *);
static void loop_3_trns_free (struct trns_header *);
static void pop_ctl_stack (void);

/* LOOP. */

/* Parses a LOOP command.  Passes the real work off to
   internal_cmd_loop(). */
int
cmd_loop (void)
{
  if (!internal_cmd_loop ())
    {
      loop_1_trns_free ((struct trns_header *) one);
      loop_2_trns_free ((struct trns_header *) two);
      return CMD_FAILURE;
    }

  return CMD_SUCCESS;
}

/* Parses a LOOP command, returns success. */
static int
internal_cmd_loop (void)
{
  /* Name of indexing variable if applicable. */
  char name[9];

  lex_match_id ("LOOP");

  /* Create and initialize transformations to facilitate
     error-handling. */
  two = xmalloc (sizeof *two);
  two->h.proc = loop_2_trns_proc;
  two->h.free = loop_2_trns_free;
  two->cond = NULL;
  two->flags = 0;

  one = xmalloc (sizeof *one);
  one->h.proc = loop_1_trns_proc;
  one->h.free = loop_1_trns_free;
  one->init = one->incr = one->term = NULL;
  one->two = two;

  /* Parse indexing clause. */
  if (token == T_ID && lex_look_ahead () == '=')
    {
      struct variable *v = dict_lookup_var (default_dict, tokid);

      two->flags |= LPC_INDEX;

      if (v && v->type == ALPHA)
	{
	  msg (SE, _("The index variable may not be a string variable."));
	  return 0;
	}
      strcpy (name, tokid);

      lex_get ();
      assert (token == '=');
      lex_get ();

      one->init = expr_parse (PXP_NUMERIC);
      if (!one->init)
	return 0;

      if (!lex_force_match (T_TO))
	{
	  expr_free (one->init);
	  return 0;
	}
      one->term = expr_parse (PXP_NUMERIC);
      if (!one->term)
	{
	  expr_free (one->init);
	  return 0;
	}

      if (lex_match (T_BY))
	{
	  one->incr = expr_parse (PXP_NUMERIC);
	  if (!one->incr)
	    return 0;
	}
    }
  else
    name[0] = 0;

  /* Parse IF clause. */
  if (lex_match_id ("IF"))
    {
      two->flags |= LPC_COND;

      two->cond = expr_parse (PXP_BOOLEAN);
      if (!two->cond)
	return 0;
    }

  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      return 0;
    }

  /* Find variable; create if necessary. */
  if (name[0])
    {
      two->index = dict_lookup_var (default_dict, name);
      if (!two->index)
        two->index = dict_create_var (default_dict, name, 0);
    }
  
  /* Push on control stack. */
  two->ctl.down = ctl_stack;
  two->ctl.type = CST_LOOP;
  two->ctl.trns = (struct trns_header *) two;
  two->ctl.brk = NULL;
  ctl_stack = &two->ctl;

  /* Dump out the transformations. */
  add_transformation ((struct trns_header *) one);
  add_transformation ((struct trns_header *) two);

#if DEBUGGING
  printf ("LOOP");
  if (two->flags & LPC_INDEX)
    printf ("(INDEX)");
  if (two->flags & LPC_COND)
    printf ("(IF)");
  printf ("\n");
#endif

  return 1;
}

/* Parses the END LOOP command by passing the buck off to
   cmd_internal_end_loop(). */
int
cmd_end_loop (void)
{
  if (!internal_cmd_end_loop ())
    {
      loop_3_trns_free ((struct trns_header *) thr);
      if (ctl_stack && ctl_stack->type == CST_LOOP)
	pop_ctl_stack ();
      return CMD_FAILURE;
    }

  return CMD_SUCCESS;
}

/* Parses the END LOOP command. */
int
internal_cmd_end_loop (void)
{
  /* Backpatch pointer for BREAK commands. */
  struct break_trns *brk;

  /* Allocate, initialize transformation to facilitate
     error-handling. */
  thr = xmalloc (sizeof *thr);
  thr->h.proc = loop_3_trns_proc;
  thr->h.free = loop_3_trns_free;
  thr->cond = NULL;

  /* There must be a matching LOOP command. */
  if (!ctl_stack || ctl_stack->type != CST_LOOP)
    {
      msg (SE, _("There is no LOOP command that corresponds to this "
		 "END LOOP."));
      return 0;
    }
  thr->loop_start = ((struct loop_2_trns *) ctl_stack->trns)->h.index;

  /* Parse the expression if any. */
  if (lex_match_id ("IF"))
    {
      thr->cond = expr_parse (PXP_BOOLEAN);
      if (!thr->cond)
	return 0;
    }

  add_transformation ((struct trns_header *) thr);

  /* Backpatch. */
  ((struct loop_2_trns *) ctl_stack->trns)->loop_term = n_trns;
  for (brk = ctl_stack->brk; brk; brk = brk->next)
    brk->loop_term = n_trns;

  /* Pop off the top of stack. */
  ctl_stack = ctl_stack->down;

#if DEBUGGING
  printf ("END LOOP");
  if (thr->cond)
    printf ("(IF)");
  printf ("\n");
#endif

  return 1;
}

/* Performs transformation 1. */
static int
loop_1_trns_proc (struct trns_header * trns, struct ccase * c)
{
  struct loop_1_trns *one = (struct loop_1_trns *) trns;
  struct loop_2_trns *two = one->two;

  two->pass = -1;
  if (two->flags & LPC_INDEX)
    {
      union value t1, t2, t3;

      expr_evaluate (one->init, c, &t1);
      if (one->incr)
	expr_evaluate (one->incr, c, &t2);
      else
	t2.f = 1.0;
      expr_evaluate (one->term, c, &t3);

      /* Even if the loop is never entered, force the index variable
         to assume the initial value. */
      c->data[two->index->fv].f = t1.f;

      /* Throw out various pathological cases. */
      if (!finite (t1.f) || !finite (t2.f) || !finite (t3.f)
	  || approx_eq (t2.f, 0.0))
	return two->loop_term;
      debug_printf (("LOOP %s=%g TO %g BY %g.\n", two->index->name,
		     t1.f, t3.f, t2.f));
      if (t2.f > 0.0)
	{
	  /* Loop counts upward: I=1 TO 5 BY 1. */
	  two->flags &= ~LPC_RINDEX;

	  /* incr>0 but init>term */
	  if (approx_gt (t1.f, t3.f))
	    return two->loop_term;
	}
      else
	{
	  /* Loop counts downward: I=5 TO 1 BY -1. */
	  two->flags |= LPC_RINDEX;

	  /* incr<0 but init<term */
	  if (approx_lt (t1.f, t3.f))
	    return two->loop_term;
	}

      two->curr = t1.f;
      two->incr = t2.f;
      two->term = t3.f;
    }

  return -1;
}

/* Frees transformation 1. */
static void
loop_1_trns_free (struct trns_header * trns)
{
  struct loop_1_trns *one = (struct loop_1_trns *) trns;

  expr_free (one->init);
  expr_free (one->incr);
  expr_free (one->term);
}

/* Performs transformation 2. */
static int
loop_2_trns_proc (struct trns_header * trns, struct ccase * c)
{
  struct loop_2_trns *two = (struct loop_2_trns *) trns;

  /* MXLOOPS limiter. */
  if (two->flags == 0)
    {
      two->pass++;
      if (two->pass > set_mxloops)
         return two->loop_term;
    }

  /* Indexing clause limiter: counting downward. */
  if (two->flags & LPC_RINDEX)
    {
      /* Test if we're at the end of the looping. */
      if (approx_lt (two->curr, two->term))
	return two->loop_term;

      /* Set the current value into the case. */
      c->data[two->index->fv].f = two->curr;

      /* Decrement the current value. */
      two->curr += two->incr;
    }
  /* Indexing clause limiter: counting upward. */
  else if (two->flags & LPC_INDEX)
    {
      /* Test if we're at the end of the looping. */
      if (approx_gt (two->curr, two->term))
	return two->loop_term;

      /* Set the current value into the case. */
      c->data[two->index->fv].f = two->curr;

      /* Increment the current value. */
      two->curr += two->incr;
    }

  /* Conditional clause limiter. */
  if ((two->flags & LPC_COND)
      && expr_evaluate (two->cond, c, NULL) != 1.0)
    return two->loop_term;

  return -1;
}

/* Frees transformation 2. */
static void
loop_2_trns_free (struct trns_header * trns)
{
  struct loop_2_trns *two = (struct loop_2_trns *) trns;

  expr_free (two->cond);
}

/* Performs transformation 3. */
static int
loop_3_trns_proc (struct trns_header * trns, struct ccase * c)
{
  struct loop_3_trns *thr = (struct loop_3_trns *) trns;

  /* Note that it breaks out of the loop if the expression is true *or
     missing*.  This is conformant. */
  if (thr->cond && expr_evaluate (two->cond, c, NULL) != 0.0)
    return -1;

  return thr->loop_start;
}

/* Frees transformation 3. */
static void
loop_3_trns_free (struct trns_header * trns)
{
  struct loop_3_trns *thr = (struct loop_3_trns *) trns;

  expr_free (thr->cond);
}

/* BREAK. */

/* Parses the BREAK command. */
int
cmd_break (void)
{
  /* Climbs down the stack to find a LOOP. */
  struct ctl_stmt *loop;

  /* New transformation. */
  struct break_trns *t;

  lex_match_id ("BREAK");

  for (loop = ctl_stack; loop; loop = loop->down)
    if (loop->type == CST_LOOP)
      break;
  if (!loop)
    {
      msg (SE, _("This command may only appear enclosed in a LOOP/"
		 "END LOOP control structure."));
      return CMD_FAILURE;
    }
  
  if (ctl_stack->type != CST_DO_IF)
    msg (SW, _("BREAK not enclosed in DO IF structure."));

  t = xmalloc (sizeof *t);
  t->h.proc = break_trns_proc;
  t->h.free = NULL;
  t->next = loop->brk;
  loop->brk = t;
  add_transformation ((struct trns_header *) t);

  return lex_end_of_command ();
}

static int
break_trns_proc (struct trns_header * trns, struct ccase * c UNUSED)
{
  return ((struct break_trns *) trns)->loop_term;
}

/* Control stack operations. */

/* Pops the top of stack element off of ctl_stack.  Does not
   check that ctl_stack is indeed non-NULL. */
static void
pop_ctl_stack (void)
{
  switch (ctl_stack->type)
    {
    case CST_LOOP:
      {
	/* Pointer for chasing down and backpatching BREAKs. */
	struct break_trns *brk;

	/* Terminate the loop. */
	thr = xmalloc (sizeof *thr);
	thr->h.proc = loop_3_trns_proc;
	thr->h.free = loop_3_trns_free;
	thr->cond = NULL;
	thr->loop_start = ((struct loop_2_trns *) ctl_stack->trns)->h.index;
	add_transformation ((struct trns_header *) thr);

	/* Backpatch. */
	((struct loop_2_trns *) ctl_stack->trns)->loop_term = n_trns;
	for (brk = ctl_stack->brk; brk; brk = brk->next)
	  brk->loop_term = n_trns;
      }
      break;
    case CST_DO_IF:
      {
	/* List iterator. */
	struct do_if_trns *iter;

	iter = ((struct do_if_trns *) ctl_stack->trns);
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
      }
      break;
    default:
      assert (0);
    }
  ctl_stack = ctl_stack->down;
}

/* Checks for unclosed LOOPs and DO IFs and closes them out. */
void
discard_ctl_stack (void)
{
  if (!ctl_stack)
    return;
  msg (SE, _("%s without %s."), ctl_stack->type == CST_LOOP ? "LOOP" : "DO IF",
       ctl_stack->type == CST_LOOP ? "END LOOP" : "END IF");
  while (ctl_stack)
    pop_ctl_stack ();
  ctl_stack = NULL;
}
