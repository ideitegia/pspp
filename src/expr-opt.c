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
#include "expr.h"
#include "exprP.h"
#include <assert.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include "alloc.h"
#include "approx.h"
#include "data-in.h"
#include "error.h"
#include "julcal/julcal.h"
#include "misc.h"
#include "stats.h"
#include "str.h"
#include "var.h"

/*
   Expression "optimizer"

   Operates on the tree representation of expressions.
   optimize_expression() performs the optimizations listed below:

   1. Constant folding
   Any operation with constant operands is replaced by its value.
   (Exception: random-number-generator functions.)

   2. Strength reduction (x is any expression; a is a numeric constant)
   x/0 => SYSMIS
   x*0 => 0
   x**0 => 1
   x**1, x+0, x-0, x*1 => x
   x**2 => sqr(x)
   x/a => x*(1/a)   (where 1/a is evaluated at optimization time)

   I thought about adding additional optimizations but decided that what
   is here could already be considered overkill.
 */

static struct nonterm_node *evaluate_tree (struct nonterm_node * n);
static struct nonterm_node *optimize_tree (struct nonterm_node * n);

struct nonterm_node *
optimize_expression (struct nonterm_node * n)
{
  int i;

  /* Set to 1 if a child is nonconstant. */
  int nonconst = 0;

  /* Number of system-missing children. */
  int sysmis = 0;

  /* We can't optimize a terminal node. */
  if (n->type > OP_TERMINAL)
    return n;

  /* Start by optimizing all the children. */
  for (i = 0; i < n->n; i++)
    {
      n->arg[i] = ((union any_node *)
		   optimize_expression ((struct nonterm_node *) n->arg[i]));
      if (n->arg[i]->type == OP_NUM_CON)
	{
	  if (n->arg[i]->num_con.value == SYSMIS)
	    sysmis++;
	}
      else if (n->arg[i]->type != OP_STR_CON)
	nonconst = 1;
    }

  if (sysmis && !(ops[n->type].flags & OP_ABSORB_MISS))
    /* Just about any operation produces SYSMIS when given any SYSMIS
       arguments. */
    {
      struct num_con_node *num = xmalloc (sizeof *num);
      free_node ((union any_node *) n);
      num->type = OP_NUM_CON;
      num->value = SYSMIS;
      n = (struct nonterm_node *) num;
    }
  else if (!nonconst)
    /* If all the children of this node are constants, then there are
       obvious optimizations. */
    n = evaluate_tree (n);
  else
    /* Otherwise, we may be able to make certain optimizations
       anyway. */
    n = optimize_tree (n);
  return n;
}

static struct nonterm_node *repl_num_con (struct nonterm_node *, double);
static struct nonterm_node *force_repl_num_con (struct nonterm_node *, double);
static struct nonterm_node *repl_str_con (struct nonterm_node *, char *, int);

#define n0 n->arg[0]->num_con.value
#define n1 n->arg[1]->num_con.value
#define n2 n->arg[2]->num_con.value

#define s0 n->arg[0]->str_con.s
#define s0l n->arg[0]->str_con.len
#define s1 n->arg[1]->str_con.s
#define s1l n->arg[1]->str_con.len
#define s2 n->arg[2]->str_con.s
#define s2l n->arg[2]->str_con.len
#define s(X) n->arg[X]->str_con.s
#define sl(X) n->arg[X]->str_con.len

static struct nonterm_node *
optimize_tree (struct nonterm_node * n)
{
  int i;

  errno = 0;
  if (n->type == OP_PLUS || n->type == OP_MUL)
    {
      /* Default constant value. */
      double def = n->type == OP_MUL ? 1.0 : 0.0;

      /* Total value of all the constants. */
      double cval = def;

      /* Number of nonconst arguments. */
      int nvar = 0;

      /* New node. */
      struct nonterm_node *m;

      /* Argument copying counter. */
      int c;

      /* 1=SYSMIS encountered */
      int sysmis = 0;

      for (i = 0; i < n->n; i++)
	if (n->arg[i]->type == OP_NUM_CON)
	  {
	    if (n->arg[i]->num_con.value != SYSMIS)
	      {
		if (n->type == OP_MUL)
		  cval *= n->arg[i]->num_con.value;
		else
		  cval += n->arg[i]->num_con.value;
	      }
	    else
	      sysmis++;
	  }
	else
	  nvar++;

      /* 0*SYSMIS=0, 0/SYSMIS=0; otherwise, SYSMIS and infinities
         produce SYSMIS. */
      if (approx_eq (cval, 0.0) && n->type == OP_MUL)
	nvar = 0;
      else if (sysmis || !finite (cval))
	{
	  nvar = 0;
	  cval = SYSMIS;
	}

      /* If no nonconstant terms, replace with a constant node. */
      if (nvar == 0)
	return force_repl_num_con (n, cval);

      if (nvar == 1 && cval == def)
	{
	  /* If there is exactly one nonconstant term and no constant
	     terms, replace with the nonconstant term. */
	  for (i = 0; i < n->n; i++)
	    if (n->arg[i]->type != OP_NUM_CON)
	      m = (struct nonterm_node *) n->arg[i];
	    else
	      free_node (n->arg[i]);
	}
      else
	{
	  /* Otherwise consolidate all the nonconstant terms. */
	  m = xmalloc (sizeof (struct nonterm_node)
		       + ((nvar + approx_ne (cval, def) - 1)
			  * sizeof (union any_node *)));
	  for (i = c = 0; i < n->n; i++)
	    if (n->arg[i]->type != OP_NUM_CON)
	      m->arg[c++] = n->arg[i];
	    else
	      free_node (n->arg[i]);

	  if (approx_ne (cval, def))
	    {
	      m->arg[c] = xmalloc (sizeof (struct num_con_node));
	      m->arg[c]->num_con.type = OP_NUM_CON;
	      m->arg[c]->num_con.value = cval;
	      c++;
	    }

	  m->type = n->type;
	  m->n = c;
	}
      free (n);
      n = m;
    }
  else if (n->type == OP_POW)
    {
      if (n->arg[1]->type == OP_NUM_CON)
	{
	  if (approx_eq (n1, 1.0))
	    {
	      struct nonterm_node *m = (struct nonterm_node *) n->arg[0];

	      free_node (n->arg[1]);
	      free (n);
	      return m;
	    }
	  else if (approx_eq (n1, 2.0))
	    {
	      n = xrealloc (n, sizeof (struct nonterm_node));
	      n->type = OP_SQUARE;
	      n->n = 1;
	    }
	}
    }
  return n;
}

#define rnc(D)					\
	(n = repl_num_con (n, D))
     
#define frnc(D)					\
	(n = force_repl_num_con (n, D))

/* Finds the first NEEDLE of length NEEDLE_LEN in a HAYSTACK of length
   HAYSTACK_LEN.  Returns a 1-based index, 0 on failure. */
static inline int
str_search (char *haystack, int haystack_len, char *needle, int needle_len)
{
  char *p = memmem (haystack, haystack_len, needle, needle_len);
  return p ? p - haystack + 1 : 0;
}

/* Finds the last NEEDLE of length NEEDLE_LEN in a HAYSTACK of length
   HAYSTACK_LEN.  Returns a 1-based index, 0 on failure. */
static inline int
str_rsearch (char *haystack, int haystack_len, char *needle, int needle_len)
{
  char *p = mm_find_reverse (haystack, haystack_len, needle, needle_len);
  return p ? p - haystack + 1 : 0;
}

static struct nonterm_node *
evaluate_tree (struct nonterm_node * n)
{
  static char *strbuf;
  int add;
  int len;
  int i;

  if (!strbuf)
    strbuf = xmalloc (256);
  errno = 0;

  switch (n->type)
    {
    case OP_PLUS:
    case OP_MUL:
      return optimize_tree (n);

    case OP_POW:
      if (approx_eq (n0, 0.0) && approx_eq (n1, 0.0))
	frnc (SYSMIS);
      else if (n0 == SYSMIS && n1 == 0.0)
	frnc (1.0);
      else if (n0 == 0.0 && n1 == SYSMIS)
	frnc (0.0);
      else
	rnc (pow (n0, n1));
      break;

    case OP_AND:
      if (n0 == 0.0 || n1 == 0.0)
	frnc (0.0);
      else if (n0 == SYSMIS || n1 == SYSMIS)
	frnc (SYSMIS);
      else
	frnc (1.0);
      break;
    case OP_OR:
      if (n0 == 1.0 || n1 == 1.0)
	frnc (1.0);
      else if (n0 == SYSMIS || n1 == SYSMIS)
	frnc (SYSMIS);
      else
	frnc (0.0);
      break;
    case OP_NOT:
      rnc (n0 == 0.0 ? 1.0 : 0.0);
      break;

    case OP_EQ:
      rnc (approx_eq (n0, n1));
      break;
    case OP_GE:
      rnc (approx_ge (n0, n1));
      break;
    case OP_GT:
      rnc (approx_gt (n0, n1));
      break;
    case OP_LE:
      rnc (approx_le (n0, n1));
      break;
    case OP_LT:
      rnc (approx_lt (n0, n1));
      break;
    case OP_NE:
      rnc (approx_ne (n0, n1));
      break;

      /* String operators. */
    case OP_STRING_EQ:
      rnc (st_compare_pad (s0, s0l, s1, s1l) == 0);
      break;
    case OP_STRING_GE:
      rnc (st_compare_pad (s0, s0l, s1, s1l) >= 0);
      break;
    case OP_STRING_GT:
      rnc (st_compare_pad (s0, s0l, s1, s1l) > 0);
      break;
    case OP_STRING_LE:
      rnc (st_compare_pad (s0, s0l, s1, s1l) <= 0);
      break;
    case OP_STRING_LT:
      rnc (st_compare_pad (s0, s0l, s1, s1l) < 0);
      break;
    case OP_STRING_NE:
      rnc (st_compare_pad (s0, s0l, s1, s1l) != 0);
      break;

      /* Unary functions. */
    case OP_NEG:
      rnc (-n0);
      break;
    case OP_ABS:
      rnc (fabs (n0));
      break;
    case OP_ARCOS:
      rnc (acos (n0));
      break;
    case OP_ARSIN:
      rnc (asin (n0));
      break;
    case OP_ARTAN:
      rnc (atan (n0));
      break;
    case OP_COS:
      rnc (cos (n0));
      break;
    case OP_EXP:
      rnc (exp (n0));
      break;
    case OP_LG10:
      rnc (log10 (n0));
      break;
    case OP_LN:
      rnc (log (n0));
      break;
    case OP_MOD10:
      rnc (fmod (n0, 10));
      break;
    case OP_RND:
      rnc (n0 >= 0.0 ? floor (n0 + 0.5) : -floor (-n0 + 0.5));
      break;
    case OP_SIN:
      rnc (sin (n0));
      break;
    case OP_SQRT:
      rnc (sqrt (n0));
      break;
    case OP_TAN:
      rnc (tan (n0));
      break;
    case OP_TRUNC:
      rnc (n0 >= 0.0 ? floor (n0) : -floor (-n0));
      break;

      /* N-ary numeric functions. */
    case OP_ANY:
      if (n0 == SYSMIS)
	frnc (SYSMIS);
      else
	{
	  int sysmis = 1;
	  double ni;

	  for (i = 1; i < n->n; i++)
	    {
	      ni = n->arg[i]->num_con.value;
	      if (approx_eq (n0, ni))
		{
		  frnc (1.0);
		  goto any_done;
		}
	      if (ni != SYSMIS)
		sysmis = 0;
	    }
	  frnc (sysmis ? SYSMIS : 0.0);
	}
    any_done:
      break;
    case OP_ANY_STRING:
      for (i = 1; i < n->n; i++)
	if (!st_compare_pad (n->arg[0]->str_con.s, n->arg[0]->str_con.len,
			     n->arg[i]->str_con.s, n->arg[i]->str_con.len))
	  {
	    frnc (1.0);
	    goto any_string_done;
	  }
      frnc (0.0);
    any_string_done:
      break;

    case OP_CFVAR:
    case OP_MAX:
    case OP_MEAN:
    case OP_MIN:
    case OP_NMISS:
    case OP_NVALID:
    case OP_SD:
    case OP_SUM:
    case OP_VARIANCE:
      {
	double d[2] =
	{0.0, 0.0};		/* sum, sum of squares */
	double min = DBL_MAX;	/* minimum value */
	double max = -DBL_MAX;	/* maximum value */
	double ni;		/* value of i'th argument */
	int nv = 0;		/* number of valid arguments */

	for (i = 0; i < n->n; i++)
	  {
	    ni = n->arg[i]->num_con.value;
	    if (ni != SYSMIS)
	      {
		nv++;
		d[0] += ni;
		d[1] += ni * ni;
		if (ni < min)
		  min = ni;
		if (ni > max)
		  max = ni;
	      }
	  }
	if (n->type == OP_NMISS)
	  frnc (i - nv);
	else if (n->type == OP_NVALID)
	  frnc (nv);
	else if (nv >= (int) n->arg[i])
	  {
	    switch (n->type)
	      {
	      case OP_CFVAR:
		frnc (calc_cfvar (d, nv));
		break;
	      case OP_MAX:
		frnc (max);
		break;
	      case OP_MEAN:
		frnc (calc_mean (d, nv));
		break;
	      case OP_MIN:
		frnc (min);
		break;
	      case OP_SD:
		frnc (calc_stddev (calc_variance (d, nv)));
		break;
	      case OP_SUM:
		frnc (d[0]);
		break;
	      case OP_VARIANCE:
		frnc (calc_variance (d, nv));
		break;
	      }
	  }
	else
	  frnc (SYSMIS);
      }
      break;
    case OP_RANGE:
      if (n0 == SYSMIS)
	frnc (SYSMIS);
      else
	{
	  double min, max;
	  int sysmis = 1;

	  for (i = 1; i < n->n; i += 2)
	    {
	      min = n->arg[i]->num_con.value;
	      max = n->arg[i + 1]->num_con.value;
	      if (min == SYSMIS || max == SYSMIS)
		continue;
	      sysmis = 0;
	      if (approx_ge (n0, min) && approx_le (n0, max))
		{
		  frnc (1.0);
		  goto range_done;
		}
	    }
	  frnc (sysmis ? SYSMIS : 0.0);
	}
    range_done:
      break;
    case OP_RANGE_STRING:
      for (i = 1; i < n->n; i += 2)
	if (st_compare_pad (n->arg[0]->str_con.s, n->arg[0]->str_con.len,
			    n->arg[i]->str_con.s, n->arg[i]->str_con.len) >= 0
	    && st_compare_pad (n->arg[0]->str_con.s, n->arg[0]->str_con.len,
			       n->arg[i + 1]->str_con.s,
			       n->arg[i + 1]->str_con.len) <= 0)
	  {
	    frnc (1.0);
	    goto range_str_done;
	  }
      frnc (0.0);
    range_str_done:
      break;

      /* Time function. */
    case OP_TIME_HMS:
      rnc (60. * (60. * n0 + n1) + n2);
      break;

      /* Date construction functions. */
    case OP_DATE_DMY:
      rnc (60. * 60. * 24. * yrmoda (n2, n1, n0));
      break;
    case OP_DATE_MDY:
      rnc (60. * 60. * 24. * yrmoda (n2, n0, n1));
      break;
    case OP_DATE_MOYR:
      rnc (60. * 60. * 24. * yrmoda (n1, n0, 1));
      break;
    case OP_DATE_QYR:
      rnc (60. * 60. * 24. * yrmoda (n1, 3 * (int) n0 - 2, 1));
      break;
    case OP_DATE_WKYR:
      {
	double t = yrmoda (n1, 1, 1);
	if (t != SYSMIS)
	  t = 60. * 60. * 24. * (t + 7. * (n0 - 1));
	rnc (t);
      }
      break;
    case OP_DATE_YRDAY:
      {
	double t = yrmoda (n0, 1, 1);
	if (t != SYSMIS)
	  t = 60. * 60. * 24. * (t + n0 - 1);
	rnc (t);
      }
      break;
    case OP_YRMODA:
      rnc (yrmoda (n0, n1, n2));
      break;
      /* Date extraction functions. */
    case OP_XDATE_DATE:
      rnc (floor (n0 / 60. / 60. / 24.) * 60. * 60. * 24.);
      break;
    case OP_XDATE_HOUR:
      rnc (fmod (floor (n0 / 60. / 60.), 24.));
      break;
    case OP_XDATE_JDAY:
      rnc (julian_to_jday (n0 / 86400.));
      break;
    case OP_XDATE_MDAY:
      {
	int day;
	julian_to_calendar (n0 / 86400., NULL, NULL, &day);
	rnc (day);
      }
      break;
    case OP_XDATE_MINUTE:
      rnc (fmod (floor (n0 / 60.), 60.));
      break;
    case OP_XDATE_MONTH:
      {
	int month;
	julian_to_calendar (n0 / 86400., NULL, &month, NULL);
	rnc (month);
      }
      break;
    case OP_XDATE_QUARTER:
      {
	int month;
	julian_to_calendar (n0 / 86400., NULL, &month, NULL);
	rnc ((month - 1) / 3 + 1);
      }
      break;
    case OP_XDATE_SECOND:
      rnc (fmod (n0, 60.));
      break;
    case OP_XDATE_TDAY:
      rnc (floor (n0 / 60. / 60. / 24.));
      break;
    case OP_XDATE_TIME:
      rnc (n0 - floor (n0 / 60. / 60. / 24.) * 60. * 60. * 24.);
      break;
    case OP_XDATE_WEEK:
      rnc ((julian_to_jday (n0) - 1) / 7 + 1);
      break;
    case OP_XDATE_WKDAY:
      rnc (julian_to_wday (n0));
      break;
    case OP_XDATE_YEAR:
      {
	int year;
	julian_to_calendar (n0 / 86400., &year, NULL, NULL);
	rnc (year);
      }
      break;

      /* String functions. */
    case OP_CONCAT:
      {
	len = s0l;
	memcpy (strbuf, s0, len);
	for (i = 1; i < n->n; i++)
	  {
	    add = sl (i);
	    if (add + len > 255)
	      add = 255 - len;
	    memcpy (&strbuf[len], s (i), add);
	    len += add;
	  }
	n = repl_str_con (n, strbuf, len);
      }
      break;
    case OP_INDEX:
      rnc (s1l ? str_search (s0, s0l, s1, s1l) : SYSMIS);
      break;
    case OP_INDEX_OPT:
      if (n2 == SYSMIS || (int) n2 <= 0 || s1l % (int) n2)
	{
	  msg (SW, _("While optimizing a constant expression, there was "
	       "a bad value for the third argument to INDEX."));
	  frnc (SYSMIS);
	}
      else
	{
	  int pos = 0;
	  int c = s1l / (int) n2;
	  int r;

	  for (i = 0; i < c; i++)
	    {
	      r = str_search (s0, s0l, s (i), sl (i));
	      if (r < pos || pos == 0)
		pos = r;
	    }
	  frnc (pos);
	}
      break;
    case OP_RINDEX:
      rnc (str_rsearch (s0, s0l, s1, s1l));
      break;
    case OP_RINDEX_OPT:
      if (n2 == SYSMIS || (int) n2 <= 0 || s1l % (int) n2)
	{
	  msg (SE, _("While optimizing a constant expression, there was "
	       "a bad value for the third argument to RINDEX."));
	  frnc (SYSMIS);
	}
      else
	{
	  int pos = 0;
	  int c = s1l / n2;
	  int r;

	  for (i = 0; i < c; i++)
	    {
	      r = str_rsearch (s0, s0l, s (i), sl (i));
	      if (r > pos)
		pos = r;
	    }
	  frnc (pos);
	}
      break;
    case OP_LENGTH:
      frnc (s0l);
      break;
    case OP_LOWER:
      {
	char *cp;
	for (cp = &s0[s0l]; cp >= s0; cp--)
	  *cp = tolower ((unsigned char) (*cp));
	n = repl_str_con (n, s0, s0l);
      }
      break;
    case OP_UPPER:
      {
	char *cp;
	for (cp = &s0[s0[0] + 1]; cp > s0; cp--)
	  *cp = toupper ((unsigned char) (*cp));
	n = repl_str_con (n, s0, s0l);
      }
      break;
    case OP_LPAD:
    case OP_LPAD_OPT:
    case OP_RPAD:
    case OP_RPAD_OPT:
      {
	int c;

	if (n1 == SYSMIS)
	  {
	    n = repl_str_con (n, NULL, 0);
	    break;
	  }
	len = n1;
	len = range (len, 1, 255);
	add = max (n1 - s0l, 0);

	if (n->type == OP_LPAD_OPT || n->type == OP_RPAD_OPT)
	  {
	    if (s2l < 1)
	      {
		c = n->type == OP_LPAD_OPT ? 'L' : 'R';
		msg (SE, _("Third argument to %cPAD() must be at least one "
		     "character in length."), c);
		c = ' ';
	      }
	    else
	      c = s2[0];
	  }
	else
	  c = ' ';

	if (n->type == OP_LPAD || n->type == OP_LPAD_OPT)
	  memmove (&s0[add], s0, len);
	if (n->type == OP_LPAD || n->type == OP_LPAD_OPT)
	  memset (s0, c, add);
	else
	  memset (&s0[s0l], c, add);

	n = repl_str_con (n, s0, len);
      }
      break;
    case OP_LTRIM:
    case OP_LTRIM_OPT:
    case OP_RTRIM:
    case OP_RTRIM_OPT:
      {
	int c;
	char *cp = s0;

	if (n->type == OP_LTRIM_OPT || n->type == OP_RTRIM_OPT)
	  {
	    if (s1l < 1)
	      {
		c = n->type == OP_LTRIM_OPT ? 'L' : 'R';
		msg (SE, _("Second argument to %cTRIM() must be at least one "
		     "character in length."), c);
		c = ' ';
	      }
	    else
	      c = s1[0];
	  }
	len = s0l;
	if (n->type == OP_LTRIM || n->type == OP_LTRIM_OPT)
	  {
	    while (*cp == c && cp < &s0[len])
	      cp++;
	    len -= cp - s0;
	  }
	else
	  while (len > 0 && s0[len - 1] == c)
	    len--;
	n = repl_str_con (n, cp, len);
      }
      break;
    case OP_NUMBER:
    case OP_NUMBER_OPT:
      {
	union value v;
	struct data_in di;

	di.s = s0;
	di.e = s0 + s0l;
	di.v = &v;
	di.flags = DI_IGNORE_ERROR;
	di.f1 = 1;

	if (n->type == OP_NUMBER_OPT)
	  {
	    di.format.type = (int) n->arg[1];
	    di.format.w = (int) n->arg[2];
	    di.format.d = (int) n->arg[3];
	  }
	else
	  {
	    di.format.type = FMT_F;
	    di.format.w = s0l;
	    di.format.d = 0;
	  }
	
	data_in (&di);
	frnc (v.f);
      }
      break;
    case OP_STRING:
      {
	union value v;
	struct fmt_spec f;
	f.type = (int) n->arg[1];
	f.w = (int) n->arg[2];
	f.d = (int) n->arg[3];
	v.f = n0;

	data_out (strbuf, &f, &v);
	n = repl_str_con (n, strbuf, f.w);
      }
      break;
    case OP_SUBSTR:
    case OP_SUBSTR_OPT:
      {
	int pos = (int) n1;
	if (pos > s0l || pos <= 0 || n1 == SYSMIS
	    || (n->type == OP_SUBSTR_OPT && n2 == SYSMIS))
	  n = repl_str_con (n, NULL, 0);
	else
	  {
	    if (n->type == OP_SUBSTR_OPT)
	      {
		len = (int) n2;
		if (len + pos - 1 > s0l)
		  len = s0l - pos + 1;
	      }
	    else
	      len = s0l - pos + 1;
	    n = repl_str_con (n, &s0[pos - 1], len);
	  }
      }
      break;

      /* Weirdness. */
    case OP_INV:
      rnc (1.0 / n0);
      break;
    case OP_MOD:
      if (approx_eq (n0, 0.0) && n1 == SYSMIS)
	frnc (0.0);
      else
	rnc (fmod (n0, n1));
      break;
    case OP_NUM_TO_BOOL:
      if (approx_eq (n0, 0.0))
	n0 = 0.0;
      else if (approx_eq (n0, 1.0))
	n0 = 1.0;
      else if (n0 != SYSMIS)
	{
	  msg (SE, _("When optimizing a constant expression, an integer "
	       "that was being used as an Boolean value was found "
	       "to have a constant value other than 0, 1, or SYSMIS."));
	  n0 = 0.0;
	}
      rnc (n0);
      break;
    }
  return n;
}

#undef n0
#undef n1
#undef n2

#undef s0
#undef s0l
#undef s1
#undef s1l
#undef s2
#undef s2l
#undef s
#undef sl

#undef rnc
#undef frnc

static struct nonterm_node *
repl_num_con (struct nonterm_node * n, double d)
{
  int i;
  if (!finite (d) || errno)
    d = SYSMIS;
  else
    for (i = 0; i < n->n; i++)
      if (n->arg[i]->type == OP_NUM_CON && n->arg[i]->num_con.value == SYSMIS)
	{
	  d = SYSMIS;
	  break;
	}
  return force_repl_num_con (n, d);
}

static struct nonterm_node *
force_repl_num_con (struct nonterm_node * n, double d)
{
  struct num_con_node *num;

  if (!finite (d) || errno)
    d = SYSMIS;
  free_node ((union any_node *) n);
  num = xmalloc (sizeof *num);
  num->type = OP_NUM_CON;
  num->value = d;
  return (struct nonterm_node *) num;
}

static struct nonterm_node *
repl_str_con (struct nonterm_node * n, char *s, int len)
{
  struct str_con_node *str;

  /* The ordering here is important since the source string may be
     part of a subnode of n. */
  str = xmalloc (sizeof *str + len - 1);
  str->type = OP_STR_CON;
  str->len = len;
  memcpy (str->s, s, len);
  free_node ((union any_node *) n);
  return (struct nonterm_node *) str;
}

/* Returns the number of days since 10 Oct 1582 for the date
   YEAR/MONTH/DAY, where YEAR is in range 0..199 or 1582..19999, MONTH
   is in 1..12, and DAY is in 1..31. */
double
yrmoda (double year, double month, double day)
{
  if (year == SYSMIS || month == SYSMIS || day == SYSMIS)
    return SYSMIS;

  /* The addition of EPSILON avoids converting, for example,
     1991.9999997=>1991. */
  year = floor (year + EPSILON);
  month = floor (month + EPSILON);
  day = floor (day + EPSILON);

  if (year >= 0. && year <= 199.)
    year += 1900.;
  if ((year < 1582. || year > 19999.)
      || (year == 1582. && (month < 10. || (month == 10. && day < 15.)))
      || (month < -1 || month > 13)
      || (day < -1 || day > 32))
    return SYSMIS;
  return calendar_to_julian (year, month, day);
}

/* Expression dumper. */

static struct expression *e;
static int nop, mop;
static int ndbl, mdbl;
static int nstr, mstr;
static int nvars, mvars;

static void dump_node (union any_node * n);
static void emit (int);
static void emit_num_con (double);
static void emit_str_con (char *, int);
static void emit_var (struct variable *);

void
dump_expression (union any_node * n, struct expression * expr)
{
  unsigned char *o;

  int height = 0;

  int max_height = 0;

  e = expr;
  e->op = NULL;
  e->num = NULL;
  e->str = NULL;
  e->var = NULL;
  nop = mop = 0;
  ndbl = mdbl = 0;
  nstr = mstr = 0;
  nvars = mvars = 0;
  dump_node (n);
  emit (OP_SENTINEL);

  /* Now compute the stack height needed to evaluate the expression. */
  for (o = e->op; *o != OP_SENTINEL; o++)
    {
      if (ops[*o].flags & OP_VAR_ARGS)
	height += 1 - o[1];
      else
	height += ops[*o].height;
      o += ops[*o].skip;
      if (height > max_height)
	max_height = height;
    }

  /* We waste space for one `value' since pointers are not
     guaranteed to be able to point to a spot before a block. */
  max_height++;

  e->stack = xmalloc (max_height * sizeof *e->stack);

#if PAGED_STACK
  e->str_stack = e->type == EX_STRING ? xmalloc (256) : NULL;
#else
  e->str_stack = xmalloc (256);
  e->str_size = 256;
#endif
}

static void
dump_node (union any_node * n)
{
  if (n->type == OP_AND || n->type == OP_OR)
    {
      int i;

      dump_node (n->nonterm.arg[0]);
      for (i = 1; i < n->nonterm.n; i++)
	{
	  dump_node (n->nonterm.arg[i]);
	  emit (n->type);
	}
      return;
    }
  else if (n->type < OP_TERMINAL)
    {
      int i;
      for (i = 0; i < n->nonterm.n; i++)
	dump_node (n->nonterm.arg[i]);
      emit (n->type);
      if (ops[n->type].flags & OP_VAR_ARGS)
	emit (n->nonterm.n);
      if (ops[n->type].flags & OP_MIN_ARGS)
	emit ((int) n->nonterm.arg[n->nonterm.n]);
      if (ops[n->type].flags & OP_FMT_SPEC)
	{
	  emit ((int) n->nonterm.arg[n->nonterm.n]);
	  emit ((int) n->nonterm.arg[n->nonterm.n + 1]);
	  emit ((int) n->nonterm.arg[n->nonterm.n + 2]);
	}
      return;
    }

  emit (n->type);
  if (n->type == OP_NUM_CON)
    emit_num_con (n->num_con.value);
  else if (n->type == OP_STR_CON)
    emit_str_con (n->str_con.s, n->str_con.len);
  else if (n->type == OP_NUM_VAR || n->type == OP_STR_VAR
	   || n->type == OP_STR_MIS)
    emit_var (n->var.v);
  else if (n->type == OP_NUM_LAG || n->type == OP_STR_LAG)
    {
      emit_var (n->lag.v);
      emit (n->lag.lag);
    }
  else if (n->type == OP_NUM_SYS || n->type == OP_NUM_VAL)
    emit (n->var.v->fv);
  else
    assert (n->type == OP_CASENUM);
}

static void
emit (int op)
{
  if (nop >= mop)
    {
      mop += 16;
      e->op = xrealloc (e->op, mop * sizeof *e->op);
    }
  e->op[nop++] = op;
}

static void
emit_num_con (double dbl)
{
  if (ndbl >= mdbl)
    {
      mdbl += 16;
      e->num = xrealloc (e->num, mdbl * sizeof *e->num);
    }
  e->num[ndbl++] = dbl;
}

static void
emit_str_con (char *str, int len)
{
  if (nstr + len + 1 > mstr)
    {
      mstr += 256;
      e->str = xrealloc (e->str, mstr);
    }
  e->str[nstr++] = len;
  memcpy (&e->str[nstr], str, len);
  nstr += len;
}

static void
emit_var (struct variable * v)
{
  if (nvars >= mvars)
    {
      mvars += 16;
      e->var = xrealloc (e->var, mvars * sizeof *e->var);
    }
  e->var[nvars++] = v;
}
