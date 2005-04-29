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

#if !do_ifP_h
#define do_ifP_h 1

#include "var.h"

/* BREAK transformation. */
struct break_trns
  {
    struct trns_header h;

    struct break_trns *next;	/* Next in chain of BREAKs associated
				   with a single LOOP. */
    int loop_term;		/* t_trns[] index to jump to; backpatched
				   in by END LOOP. */
  };

/* Types of control structures. */
enum
  {
    CST_LOOP,
    CST_DO_IF
  };

/* Control structure info. */
struct ctl_stmt
  {
    int type;			/* One of CST_*. */
    struct ctl_stmt *down;	/* Points toward the bottom of ctl_stack. */
    struct trns_header *trns;	/* Associated transformation. */
    struct break_trns *brk;	/* (LOOP only): Chain of associated BREAKs. */
  };				/* ctl_stmt */

/* Goto transformation. */
struct goto_trns
  {
    struct trns_header h;

    int dest;			/* t_trns[] index of destination of jump. */
  };

/* DO IF/ELSE IF/ELSE transformation. */
struct do_if_trns
  {
    struct trns_header h;

    struct ctl_stmt ctl;	/* DO IF: Control information for nesting. */

    /* Keeping track of clauses. */
    struct do_if_trns *next;	/* Points toward next ELSE IF. */
    struct goto_trns *brk;	/* ELSE IF: jumps out of DO IF structure. */
    int has_else;		/* DO IF: 1=there's been an ELSE. */

    /* Runtime info. */
    struct expression *cond;	/* Condition. */
    int false_jump;		/* t_trns[] index of destination when false. */
    int missing_jump;		/* t_trns[] index to break out of DO IF. */
  };

/* Top of the control structure stack. */
extern struct ctl_stmt *ctl_stack;

void discard_ctl_stack (void);

#endif /* !do_ifP_h */
