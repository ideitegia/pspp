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
#include "error.h"
#include <stdlib.h>
#include "alloc.h"
#include "case.h"
#include "command.h"
#include "dictionary.h"
#include "error.h"
#include "lexer.h"
#include "str.h"
#include "var.h"

/* Implementation details:

   The S?SS manuals do not specify the order that COUNT subcommands are
   performed in.  Experiments, however, have shown that they are performed
   in the order that they are specified in, rather than simultaneously.
   So, with the two variables A and B, and the two cases,

   A B
   1 2
   2 1

   the command COUNT A=A B (1) / B=A B (2) will produce the following
   results,

   A B
   1 1
   1 0

   rather than the results that would be produced if subcommands were
   simultaneous:

   A B
   1 1
   1 1

   Perhaps simultaneity could be implemented as an option.  On the
   other hand, what good are the above commands?  */

/* Definitions. */

enum
  {
    CNT_ERROR,			/* Invalid value. */
    CNT_SINGLE,			/* Single value. */
    CNT_HIGH,			/* x >= a. */
    CNT_LOW,			/* x <= a. */
    CNT_RANGE,			/* a <= x <= b. */
    CNT_ANY,			/* Count any. */
    CNT_SENTINEL		/* List terminator. */
  };

struct cnt_num
  {
    int type;
    double a, b;
  };

struct cnt_str
  {
    int type;
    char *s;
  };

struct counting
  {
    struct counting *next;

    /* variables to count */
    struct variable **v;
    int n;

    /* values to count */
    int missing;		/* (numeric only)
				   0=don't count missing,
				   1=count SYSMIS,
				   2=count system- and user-missing */
    union			/* Criterion values. */
      {
	struct cnt_num *n;
	struct cnt_str *s;
      }
    crit;
  };

struct cnt_var_info
  {
    struct cnt_var_info *next;

    struct variable *d;		/* Destination variable. */
    char n[9];			/* Name of dest var. */

    struct counting *c;		/* The counting specifications. */
  };

struct count_trns
  {
    struct trns_header h;
    struct cnt_var_info *specs;
  };

/* Parser. */

static trns_proc_func count_trns_proc;
static trns_free_func count_trns_free;

static int parse_numeric_criteria (struct counting *);
static int parse_string_criteria (struct counting *);

int
cmd_count (void)
{
  struct cnt_var_info *cnt;     /* Specification currently being parsed. */
  struct counting *c;           /* Counting currently being parsed. */
  int ret;                      /* Return value from parsing function. */
  struct count_trns *trns;      /* Transformation. */
  struct cnt_var_info *head;    /* First counting in chain. */

  /* Parses each slash-delimited specification. */
  head = cnt = xmalloc (sizeof *cnt);
  for (;;)
    {
      /* Initialize this struct cnt_var_info to ensure proper cleanup. */
      cnt->next = NULL;
      cnt->d = NULL;
      cnt->c = NULL;

      /* Get destination struct variable, or at least its name. */
      if (!lex_force_id ())
	goto fail;
      cnt->d = dict_lookup_var (default_dict, tokid);
      if (cnt->d)
	{
	  if (cnt->d->type == ALPHA)
	    {
	      msg (SE, _("Destination cannot be a string variable."));
	      goto fail;
	    }
	}
      else
	strcpy (cnt->n, tokid);

      lex_get ();
      if (!lex_force_match ('='))
	goto fail;

      c = cnt->c = xmalloc (sizeof *c);
      for (;;)
	{
	  c->next = NULL;
	  c->v = NULL;
	  if (!parse_variables (default_dict, &c->v, &c->n,
                                PV_DUPLICATE | PV_SAME_TYPE))
	    goto fail;

	  if (!lex_force_match ('('))
	    goto fail;

	  ret = (c->v[0]->type == NUMERIC
		 ? parse_numeric_criteria
		 : parse_string_criteria) (c);
	  if (!ret)
	    goto fail;

	  if (token == '/' || token == '.')
	    break;

	  c = c->next = xmalloc (sizeof *c);
	}

      if (token == '.')
	break;

      if (!lex_force_match ('/'))
	goto fail;
      cnt = cnt->next = xmalloc (sizeof *cnt);
    }

  /* Create all the nonexistent destination variables. */
  for (cnt = head; cnt; cnt = cnt->next)
    if (!cnt->d)
      {
	/* It's valid, though motivationally questionable, to count to
	   the same dest var more than once. */
	cnt->d = dict_lookup_var (default_dict, cnt->n);

	if (cnt->d == NULL) 
          cnt->d = dict_create_var_assert (default_dict, cnt->n, 0);
      }

  trns = xmalloc (sizeof *trns);
  trns->h.proc = count_trns_proc;
  trns->h.free = count_trns_free;
  trns->specs = head;
  add_transformation ((struct trns_header *) trns);

  return CMD_SUCCESS;

fail:
  {
    struct count_trns t;
    t.specs = head;
    count_trns_free ((struct trns_header *) & t);
    return CMD_FAILURE;
  }
}

/* Parses a set of numeric criterion values. */
static int
parse_numeric_criteria (struct counting * c)
{
  int n = 0;
  int m = 0;

  c->crit.n = 0;
  c->missing = 0;
  for (;;)
    {
      struct cnt_num *cur;
      if (n >= m - 1)
	{
	  m += 16;
	  c->crit.n = xrealloc (c->crit.n, m * sizeof (struct cnt_num));
	}

      cur = &c->crit.n[n++];
      if (lex_is_number ())
	{
	  cur->a = tokval;
	  lex_get ();
	  if (lex_match_id ("THRU"))
	    {
	      if (lex_is_number ())
		{
		  if (!lex_force_num ())
		    return 0;
		  cur->b = tokval;
		  cur->type = CNT_RANGE;
		  lex_get ();

		  if (cur->a > cur->b)
		    {
		      msg (SE, _("%g THRU %g is not a valid range.  The "
				 "number following THRU must be at least "
				 "as big as the number preceding THRU."),
			   cur->a, cur->b);
		      return 0;
		    }
		}
	      else if (lex_match_id ("HI") || lex_match_id ("HIGHEST"))
		cur->type = CNT_HIGH;
	      else
		{
		  lex_error (NULL);
		  return 0;
		}
	    }
	  else
	    cur->type = CNT_SINGLE;
	}
      else if (lex_match_id ("LO") || lex_match_id ("LOWEST"))
	{
	  if (!lex_force_match_id ("THRU"))
	    return 0;
	  if (lex_is_number ())
	    {
	      cur->type = CNT_LOW;
	      cur->a = tokval;
	      lex_get ();
	    }
	  else if (lex_match_id ("HI") || lex_match_id ("HIGHEST"))
	    cur->type = CNT_ANY;
	  else
	    {
	      lex_error (NULL);
	      return 0;
	    }
	}
      else if (lex_match_id ("SYSMIS"))
	{
	  if (c->missing < 1)
	    c->missing = 1;
	}
      else if (lex_match_id ("MISSING"))
	c->missing = 2;
      else
	{
	  lex_error (NULL);
	  return 0;
	}

      lex_match (',');
      if (lex_match (')'))
	break;
    }

  c->crit.n[n].type = CNT_SENTINEL;
  return 1;
}

/* Parses a set of string criteria values.  The skeleton is the same
   as parse_numeric_criteria(). */
static int
parse_string_criteria (struct counting * c)
{
  int len = 0;

  int n = 0;
  int m = 0;

  int i;

  for (i = 0; i < c->n; i++)
    if (c->v[i]->width > len)
      len = c->v[i]->width;

  c->crit.n = 0;
  for (;;)
    {
      struct cnt_str *cur;
      if (n >= m - 1)
	{
	  m += 16;
	  c->crit.n = xrealloc (c->crit.n, m * sizeof (struct cnt_str));
	}

      if (!lex_force_string ())
	return 0;
      cur = &c->crit.s[n++];
      cur->type = CNT_SINGLE;
      cur->s = malloc (len + 1);
      st_pad_copy (cur->s, ds_c_str (&tokstr), len + 1);
      lex_get ();

      lex_match (',');
      if (lex_match (')'))
	break;
    }

  c->crit.s[n].type = CNT_SENTINEL;
  return 1;
}

/* Transformation. */

/* Counts the number of values in case C matching counting CNT. */
static inline int
count_numeric (struct counting * cnt, struct ccase * c)
{
  int counter = 0;
  int i;

  for (i = 0; i < cnt->n; i++)
    {
      struct cnt_num *num;

      /* Extract the variable value and eliminate missing values. */
      double cmp = case_num (c, cnt->v[i]->fv);
      if (cmp == SYSMIS)
	{
	  if (cnt->missing >= 1)
	    counter++;
	  continue;
	}
      if (cnt->missing >= 2 && is_num_user_missing (cmp, cnt->v[i]))
	{
	  counter++;
	  continue;
	}

      /* Try to find the value in the list. */
      for (num = cnt->crit.n;; num++)
	switch (num->type)
	  {
	  case CNT_ERROR:
	    assert (0);
	    break;
	  case CNT_SINGLE:
	    if (cmp != num->a)
	      break;
	    counter++;
	    goto done;
	  case CNT_HIGH:
	    if (cmp < num->a)
	      break;
	    counter++;
	    goto done;
	  case CNT_LOW:
	    if (cmp > num->a)
	      break;
	    counter++;
	    goto done;
	  case CNT_RANGE:
	    if (cmp < num->a || cmp > num->b)
	      break;
	    counter++;
	    goto done;
	  case CNT_ANY:
	    counter++;
	    goto done;
	  case CNT_SENTINEL:
	    goto done;
	  default:
	    assert (0);
	  }
    done: ;
    }
  return counter;
}

/* Counts the number of values in case C matching counting CNT. */
static inline int
count_string (struct counting * cnt, struct ccase * c)
{
  int counter = 0;
  int i;

  for (i = 0; i < cnt->n; i++)
    {
      struct cnt_str *str;

      /* Extract the variable value, variable width. */
      for (str = cnt->crit.s;; str++)
	switch (str->type)
	  {
	  case CNT_ERROR:
	    assert (0);
	  case CNT_SINGLE:
	    if (memcmp (case_str (c, cnt->v[i]->fv), str->s,
                        cnt->v[i]->width))
	      break;
	    counter++;
	    goto done;
	  case CNT_SENTINEL:
	    goto done;
	  default:
	    assert (0);
	  }
    done: ;
    }
  return counter;
}

/* Performs the COUNT transformation T on case C. */
static int
count_trns_proc (struct trns_header * trns, struct ccase * c,
                 int case_num UNUSED)
{
  struct cnt_var_info *info;
  struct counting *cnt;
  int counter;

  for (info = ((struct count_trns *) trns)->specs; info; info = info->next)
    {
      counter = 0;
      for (cnt = info->c; cnt; cnt = cnt->next)
	if (cnt->v[0]->type == NUMERIC)
	  counter += count_numeric (cnt, c);
	else
	  counter += count_string (cnt, c);
      case_data_rw (c, info->d->fv)->f = counter;
    }
  return -1;
}

/* Destroys all dynamic data structures associated with T. */
static void
count_trns_free (struct trns_header * t)
{
  struct cnt_var_info *iter, *next;

  for (iter = ((struct count_trns *) t)->specs; iter; iter = next)
    {
      struct counting *i, *n;

      for (i = iter->c; i; i = n)
	{
	  if (i->n && i->v)
	    {
	      if (i->v[0]->type == NUMERIC)
		free (i->crit.n);
	      else
		{
		  struct cnt_str *s;

		  for (s = i->crit.s; s->type != CNT_SENTINEL; s++)
		    free (s->s);
		  free (i->crit.s);
		}
	    }
	  free (i->v);

	  n = i->next;
	  free (i);
	}

      next = iter->next;
      free (iter);
    }
}
