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
#include "error.h"
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include "alloc.h"
#include "data-in.h"
#include "error.h"
#include "julcal/julcal.h"
#include "misc.h"
#include "pool.h"
#include "str.h"
#include "var.h"

static void evaluate_tree_no_missing (union any_node **);
static void evaluate_tree_with_missing (union any_node **, size_t count);
static void optimize_tree (union any_node **);

static void collapse_node (union any_node **node, size_t child_idx);
static void set_number (union any_node **node, double);
static void set_number_errno (union any_node **node, double);
static void set_string (union any_node **node, const char *, size_t);

void
optimize_expression (union any_node **node)
{
  int nonconst = 0;     /* Number of nonconstant children. */
  int sysmis = 0;       /* Number of system-missing children. */
  struct nonterm_node *nonterm;
  int i;

  /* We can't optimize a terminal node. */
  if (IS_TERMINAL ((*node)->type))
    return;
  nonterm = &(*node)->nonterm;

  /* Start by optimizing all the children. */
  for (i = 0; i < nonterm->n; i++)
    {
      optimize_expression (&nonterm->arg[i]);
      if (nonterm->arg[i]->type == OP_NUM_CON)
	{
	  if (nonterm->arg[i]->num_con.value == SYSMIS)
	    sysmis++;
	}
      else if (nonterm->arg[i]->type != OP_STR_CON)
	nonconst++;
    }

  if (sysmis && !(ops[nonterm->type].flags & OP_ABSORB_MISS))
    {
      /* Most operation produce SYSMIS given any SYSMIS
         argument. */
      set_number (node, SYSMIS); 
    }
  else if (!nonconst) 
    {
      /* Evaluate constant expressions. */
      if (!sysmis) 
        evaluate_tree_no_missing (node); 
      else 
        evaluate_tree_with_missing (node, sysmis);
    }
  else 
    {
      /* A few optimization possibilities are still left. */
      optimize_tree (node); 
    }
}

static int
eq_num_con (union any_node *node, double number) 
{
  return node->type == OP_NUM_CON && node->num_con.value == number;
}

static void
optimize_tree (union any_node **node)
{
  struct nonterm_node *n = &(*node)->nonterm;
  
  /* x+0, x-0, 0+x => x. */
  if ((n->type == OP_ADD || n->type == OP_SUB) && eq_num_con (n->arg[1], 0.))
    collapse_node (node, 1);
  else if (n->type == OP_ADD && eq_num_con (n->arg[0], 0.)) 
    collapse_node (node, 0);

  /* x*1, x/1, 1*x => x. */
  else if ((n->type == OP_MUL || n->type == OP_DIV)
           && eq_num_con (n->arg[1], 1.))
    collapse_node (node, 0);
  else if (n->type == OP_MUL && eq_num_con (n->arg[0], 1.))
    collapse_node (node, 1);
  
  /* 0*x, 0/x, x*0, MOD(0,x) => x. */
  else if (((n->type == OP_MUL || n->type == OP_DIV || n->type == OP_MOD)
            && eq_num_con (n->arg[0], 0.))
           || (n->type == OP_MUL && eq_num_con (n->arg[1], 0.)))
    set_number (node, 0.);

  /* x**1 => x. */
  else if (n->type == OP_POW && eq_num_con (n->arg[1], 1))
    collapse_node (node, 0);
  
  /* x**2 => SQUARE(x). */
  else if (n->type == OP_POW && eq_num_con (n->arg[2], 2))
    {
      n->type = OP_SQUARE;
      n->n = 1;
    }
}

/* Finds the first NEEDLE of length NEEDLE_LEN in a HAYSTACK of length
   HAYSTACK_LEN.  Returns a 1-based index, 0 on failure. */
static int
str_search (const char *haystack, int haystack_len,
            const char *needle, int needle_len)
{
  char *p = memmem (haystack, haystack_len, needle, needle_len);
  return p ? p - haystack + 1 : 0;
}

/* Finds the last NEEDLE of length NEEDLE_LEN in a HAYSTACK of length
   HAYSTACK_LEN.  Returns a 1-based index, 0 on failure. */
static int
str_rsearch (const char *haystack, int haystack_len,
             const char *needle, int needle_len)
{
  char *p = mm_find_reverse (haystack, haystack_len, needle, needle_len);
  return p ? p - haystack + 1 : 0;
}

static void
evaluate_tree_no_missing (union any_node **node)
{
  struct nonterm_node *n = &(*node)->nonterm;
  double num[3];
  char *str[3];
  size_t str_len[3];
  int i;

  errno = 0;

  for (i = 0; i < n->n && i < 3; i++) 
    {
      union any_node *arg = n->arg[i];
      
      if (arg->type == OP_NUM_CON)
        num[i] = arg->num_con.value;
      else if (arg->type == OP_STR_CON) 
        {
          str[i] = arg->str_con.s;
          str_len[i] = arg->str_con.len;
        }
    }

  switch (n->type)
    {
    case OP_ADD:
      set_number (node, num[0] + num[1]);
      break;
      
    case OP_SUB:
      set_number (node, num[0] - num[1]);
      break;

    case OP_MUL:
      set_number (node, num[0] * num[1]);
      break;
      
    case OP_DIV:
      if (num[1] != 0.)
        set_number (node, num[0] / num[1]);
      break;

    case OP_POW:
      if (num[0] == 0. && num[1] == 0.)
        set_number (node, SYSMIS);
      else
        set_number_errno (node, pow (num[0], num[1]));
      break;

    case OP_AND:
      set_number (node, num[0] && num[1]);
      break;

    case OP_OR:
      set_number (node, num[0] || num[1]);
      break;

    case OP_NOT:
      set_number (node, !num[0]);
      break;

    case OP_EQ:
      set_number (node, num[0] == num[1]);
      break;
    case OP_GE:
      set_number (node, num[0] >= num[1]);
      break;
    case OP_GT:
      set_number (node, num[0] > num[1]);
      break;
    case OP_LE:
      set_number (node, num[0] <= num[1]);
      break;
    case OP_LT:
      set_number (node, num[0] < num[1]);
      break;
    case OP_NE:
      set_number (node, num[0] != num[1]);
      break;

      /* String operators. */
    case OP_EQ_STRING:
      set_number (node, st_compare_pad (str[0], str_len[0],
                                        str[1], str_len[1]) == 0);
      break;
    case OP_GE_STRING:
      set_number (node, st_compare_pad (str[0], str_len[0],
                                        str[1], str_len[1]) >= 0);
      break;
    case OP_GT_STRING:
      set_number (node, st_compare_pad (str[0], str_len[0],
                                        str[1], str_len[1]) > 0);
      break;
    case OP_LE_STRING:
      set_number (node, st_compare_pad (str[0], str_len[0],
                                        str[1], str_len[1]) <= 0);
      break;
    case OP_LT_STRING:
      set_number (node, st_compare_pad (str[0], str_len[0],
                                        str[1], str_len[1]) < 0);
      break;
    case OP_NE_STRING:
      set_number (node, st_compare_pad (str[0], str_len[0],
                                        str[1], str_len[1]) != 0);
      break;

      /* Unary functions. */
    case OP_NEG:
      set_number (node, -num[0]);
      break;
    case OP_ABS:
      set_number (node, fabs (num[0]));
      break;
    case OP_ARCOS:
      set_number_errno (node, acos (num[0]));
      break;
    case OP_ARSIN:
      set_number_errno (node, asin (num[0]));
      break;
    case OP_ARTAN:
      set_number_errno (node, atan (num[0]));
      break;
    case OP_COS:
      set_number_errno (node, cos (num[0]));
      break;
    case OP_EXP:
      set_number_errno (node, exp (num[0]));
      break;
    case OP_LG10:
      set_number_errno (node, log10 (num[0]));
      break;
    case OP_LN:
      set_number_errno (node, log (num[0]));
      break;
    case OP_MOD10:
      set_number_errno (node, fmod (num[0], 10));
      break;
    case OP_RND:
      if (num[0] >= 0.0)
        set_number_errno (node, floor (num[0] + 0.5));
      else 
        set_number_errno (node, -floor (-num[0] + 0.5));
      break;
    case OP_SIN:
      set_number_errno (node, sin (num[0]));
      break;
    case OP_SQRT:
      set_number_errno (node, sqrt (num[0]));
      break;
    case OP_TAN:
      set_number_errno (node, tan (num[0]));
      break;
    case OP_TRUNC:
      if (num[0] >= 0.0)
        set_number_errno (node, floor (num[0]));
      else
        set_number_errno (node, -floor (-num[0]));
      break;

      /* N-ary numeric functions. */
    case OP_ANY:
      {
        double result = 0.0;
        for (i = 1; i < n->n; i++)
          if (num[0] == n->arg[i]->num_con.value)
            {
              result = 1.0;
              break;
            }
        set_number (node, result);
      }
      break; 
    case OP_ANY_STRING: 
      {
        double result = 0.0;
        for (i = 1; i < n->n; i++)
          if (!st_compare_pad (n->arg[0]->str_con.s, n->arg[0]->str_con.len,
                               n->arg[i]->str_con.s, n->arg[i]->str_con.len))
            {
              result = 1.0;
              break;
            }
        set_number (node, result);
      }
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
      /* FIXME */
      break;

    case OP_RANGE: 
      {
        double result = 0.0;
        
        for (i = 1; i < n->n; i += 2)
          {
            double min = n->arg[i]->num_con.value;
            double max = n->arg[i + 1]->num_con.value;
            if (num[0] >= min && num[0] <= max)
              {
                result = 1.0;
                break;
              }
          }
        set_number (node, result);
      }
      break;

    case OP_RANGE_STRING:
      {
        double result = 0.0;

        for (i = 1; i < n->n; i += 2) 
          {
            const char *min = n->arg[i]->str_con.s;
            size_t min_len = n->arg[i]->str_con.len;
            const char *max = n->arg[i + 1]->str_con.s;
            size_t max_len = n->arg[i + 1]->str_con.len;
            
            if (st_compare_pad (str[0], str_len[0], min, min_len) >= 0
                && st_compare_pad (str[0], str_len[0], max, max_len) <= 0)
              {
                result = 1.0;
                break;
              } 
          }
        set_number (node, result);
        break;
      }
      
      /* Time functions. */
    case OP_TIME_HMS:
      {
        double min, max;
        min = min (num[0], min (num[1], num[2]));
        max = max (num[0], max (num[1], num[2]));
        if (min < 0. && max > 0.)
          break;
        set_number (node, 60. * (60. * num[0] + num[1]) + num[2]); 
      }
      break;
    case OP_CTIME_DAYS:
      set_number (node, num[0] / (60. * 60. * 24.));
      break;
    case OP_CTIME_HOURS:
      set_number (node, num[0] / (60. * 60.));
      break;
    case OP_CTIME_MINUTES:
      set_number (node, num[0] / 60.);
      break;
    case OP_TIME_DAYS:
      set_number (node, num[0] * (60. * 60. * 24.));
      break;
    case OP_CTIME_SECONDS:
      set_number (node, num[0]);
      break;

      /* Date construction functions. */
    case OP_DATE_DMY:
      set_number (node, 60. * 60. * 24. * yrmoda (num[2], num[1], num[0]));
      break;
    case OP_DATE_MDY:
      set_number (node, 60. * 60. * 24. * yrmoda (num[2], num[0], num[1]));
      break;
    case OP_DATE_MOYR:
      set_number (node, 60. * 60. * 24. * yrmoda (num[1], num[0], 1));
      break;
    case OP_DATE_QYR:
      set_number (node,
                  60. * 60. * 24. * yrmoda (num[1], 3 * (int) num[0] - 2, 1));
      break;
    case OP_DATE_WKYR:
      {
	double t = yrmoda (num[1], 1, 1);
        if (num[0] < 0. || num[0] > 53.)
          break;
	if (t != SYSMIS)
	  t = 60. * 60. * 24. * (t + 7. * (num[0] - 1));
	set_number (node, t);
      }
      break;
    case OP_DATE_YRDAY:
      {
	double t = yrmoda (num[0], 1, 1);
	if (t != SYSMIS)
	  t = 60. * 60. * 24. * (t + num[1] - 1);
	set_number (node, t);
      }
      break;
    case OP_YRMODA:
      set_number (node, yrmoda (num[0], num[1], num[2]));
      break;

      /* Date extraction functions. */
    case OP_XDATE_DATE:
      set_number_errno (node,
                        floor (num[0] / 60. / 60. / 24.) * 60. * 60. * 24.);
      break;
    case OP_XDATE_HOUR:
      set_number_errno (node, fmod (floor (num[0] / 60. / 60.), 24.));
      break;
    case OP_XDATE_JDAY:
      set_number (node, julian_to_jday (num[0] / 86400.));
      break;
    case OP_XDATE_MDAY:
      {
	int day;
	julian_to_calendar (num[0] / 86400., NULL, NULL, &day);
	set_number (node, day);
      }
      break;
    case OP_XDATE_MINUTE:
      set_number_errno (node, fmod (floor (num[0] / 60.), 60.));
      break;
    case OP_XDATE_MONTH:
      {
	int month;
	julian_to_calendar (num[0] / 86400., NULL, &month, NULL);
	set_number (node, month);
      }
      break;
    case OP_XDATE_QUARTER:
      {
	int month;
	julian_to_calendar (num[0] / 86400., NULL, &month, NULL);
	set_number (node, (month - 1) / 3 + 1);
      }
      break;
    case OP_XDATE_SECOND:
      set_number_errno (node, fmod (num[0], 60.));
      break;
    case OP_XDATE_TDAY:
      set_number_errno (node, floor (num[0] / 60. / 60. / 24.));
      break;
    case OP_XDATE_TIME:
      set_number_errno (node, num[0] - (floor (num[0] / 60. / 60. / 24.)
                                        * 60. * 60. * 24.));
      break;
    case OP_XDATE_WEEK:
      set_number (node, (julian_to_jday (num[0]) - 1) / 7 + 1);
      break;
    case OP_XDATE_WKDAY:
      set_number (node, julian_to_wday (num[0]));
      break;
    case OP_XDATE_YEAR:
      {
	int year;
	julian_to_calendar (num[0] / 86400., &year, NULL, NULL);
	set_number (node, year);
      }
      break;

      /* String functions. */
    case OP_CONCAT:
      {
        char string[256];
	int length = str_len[0];
	memcpy (string, str[0], length);
	for (i = 1; i < n->n; i++)
	  {
	    int add = n->arg[i]->str_con.len;
	    if (add + length > 255)
	      add = 255 - length;
	    memcpy (&string[length], n->arg[i]->str_con.s, add);
	    length += add;
	  }
	set_string (node, string, length);
      }
      break;
    case OP_INDEX_2:
    case OP_INDEX_3:
    case OP_RINDEX_2:
    case OP_RINDEX_3:
      {
        int result, chunk_width, chunk_cnt;

        if (n->type == OP_INDEX_2 || n->type == OP_RINDEX_2)
          chunk_width = str_len[1];
        else
          chunk_width = num[2];
        if (chunk_width <= 0 || chunk_width > str_len[1]
            || str_len[1] % chunk_width != 0)
          break; 
        chunk_cnt = str_len[1] / chunk_width;

        result = 0;
        for (i = 0; i < chunk_cnt; i++)
          {
            const char *chunk = str[1] + chunk_width * i;
            int ofs;
            if (n->type == OP_INDEX_2 || n->type == OP_INDEX_3) 
              {
                ofs = str_search (str[0], str_len[0], chunk, chunk_width);
                if (ofs < result || result == 0)
                  result = ofs; 
              }
            else 
              {
                ofs = str_rsearch (str[0], str_len[0], chunk, chunk_width);
                if (ofs > result)
                  result = ofs; 
              }
          }
        set_number (node, result);
      }
      break;
    case OP_LENGTH:
      set_number (node, str_len[0]);
      break;
    case OP_LOWER:
      {
	char *cp;
	for (cp = str[0]; cp < str[0] + str_len[0]; cp++)
	  *cp = tolower ((unsigned char) *cp);
      }
      break;
    case OP_UPPER:
      {
	char *cp;
	for (cp = str[0]; cp < str[0] + str_len[0]; cp++)
 	  *cp = toupper ((unsigned char) *cp);
      }
      break;
    case OP_LPAD:
    case OP_RPAD:
      {
        char string[256];
        int len, pad_len;
        char pad_char;

        /* Target length. */
        len = num[1];
        if (len < 1 || len > 255)
          break;

        /* Pad character. */
        if (str_len[2] != 1)
          break;
        pad_char = str[2][0];

        if (str_len[0] >= len) 
          len = str_len[0];
        pad_len = len - str_len[0];
        if (n->type == OP_LPAD) 
          {
            memset (string, pad_char, pad_len);
            memcpy (string + pad_len, str[0], str_len[0]);
          }
        else 
          {
            memcpy (string, str[0], str_len[0]);
            memset (string + str_len[0], pad_char, pad_len);
          }

        set_string (node, string, len);
      }
      break;
    case OP_LTRIM:
    case OP_RTRIM:
      {
	char pad_char;
	const char *cp = str[0];
        int len = str_len[0];

        /* Pad character. */
        if (str_len[1] != 1)
          break;
        pad_char = str[1][0];

	if (n->type == OP_LTRIM)
          while (len > 0 && *cp == pad_char)
            cp++, len--;
	else
	  while (len > 0 && str[0][len - 1] == pad_char)
	    len--;
        set_string (node, cp, len);
      }
      break;
    case OP_SUBSTR_2:
    case OP_SUBSTR_3:
      {
	int pos = (int) num[1];
	if (pos > str_len[0] || pos <= 0 || num[1] == SYSMIS
	    || (n->type == OP_SUBSTR_3 && num[2] == SYSMIS))
          set_string (node, NULL, 0);
	else
	  {
            int len;
	    if (n->type == OP_SUBSTR_3)
	      {
		len = (int) num[2];
		if (len + pos - 1 > str_len[0])
		  len = str_len[0] - pos + 1;
	      }
	    else
	      len = str_len[0] - pos + 1;
            set_string (node, &str[0][pos - 1], len);
	  }
      }
      break;

      /* Weirdness. */
    case OP_MOD:
      if (num[0] == 0.0 && num[1] == SYSMIS)
	set_number (node, 0.0);
      else
	set_number (node, fmod (num[0], num[1]));
      break;
    case OP_NUM_TO_BOOL:
      if (num[0] == 0.0)
	num[0] = 0.0;
      else if (num[0] == 1.0)
	num[0] = 1.0;
      else if (num[0] != SYSMIS)
	{
	  msg (SE, _("When optimizing a constant expression, an integer "
	       "that was being used as an Boolean value was found "
	       "to have a constant value other than 0, 1, or SYSMIS."));
	  num[0] = 0.0;
	}
      set_number (node, num[0]);
      break;
    }
}

static void
evaluate_tree_with_missing (union any_node **node UNUSED, size_t count UNUSED) 
{
  /* FIXME */
}

static void
collapse_node (union any_node **node, size_t child_idx) 
{
  struct nonterm_node *nonterm = &(*node)->nonterm;
  union any_node *child;

  child = nonterm->arg[child_idx];
  nonterm->arg[child_idx] = NULL;
  free_node (*node);
  *node = child;
}


static void
set_number (union any_node **node, double value)
{
  struct num_con_node *num;
  
  free_node (*node);

  *node = xmalloc (sizeof *num);
  num = &(*node)->num_con;
  num->type = OP_NUM_CON;
  num->value = finite (value) ? value : SYSMIS;
}

static void
set_number_errno (union any_node **node, double value) 
{
  if (errno == EDOM || errno == ERANGE)
    value = SYSMIS;
  set_number (node, value);
}

static void
set_string (union any_node **node, const char *string, size_t length)
{
  struct str_con_node *str;

  /* The ordering here is important since the source string may be
     part of a subnode of n. */
  str = xmalloc (sizeof *str + length - 1);
  str->type = OP_STR_CON;
  str->len = length;
  memcpy (str->s, string, length);
  free_node (*node);
  *node = (union any_node *) str;
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

  if (year >= 0. && year <= 29.)
    year += 2000.;
  else if (year >= 30. && year <= 99.)
    year += 1900.;
  if ((year < 1582. || year > 19999.)
      || (year == 1582. && (month < 10. || (month == 10. && day < 15.)))
      || (month < 0 || month > 13)
      || (day < 0 || day > 31))
    return SYSMIS;
  return calendar_to_julian (year, month, day);
}

/* Expression dumper. */

struct expr_dump_state 
  {
    struct expression *expr;    /* Output expression. */
    int op_cnt, op_cap;         /* Number of ops, allocated space. */
    int dbl_cnt, dbl_cap;       /* Number of doubles, allocated space. */
    int str_cnt, str_cap;       /* Number of strings, allocated space. */
    int var_cnt, var_cap;       /* Number of variables, allocated space. */
  };

static void dump_node (struct expr_dump_state *, union any_node * n);
static void emit (struct expr_dump_state *, int);
static void emit_num_con (struct expr_dump_state *, double);
static void emit_str_con (struct expr_dump_state *, char *, int);
static void emit_var (struct expr_dump_state *, struct variable *);

void
dump_expression (union any_node * n, struct expression * expr)
{
  struct expr_dump_state eds;
  unsigned char *o;
  int height = 0;
  int max_height = 0;

  expr->op = NULL;
  expr->num = NULL;
  expr->str = NULL;
  expr->var = NULL;
  eds.expr = expr;
  eds.op_cnt = eds.op_cap = 0;
  eds.dbl_cnt = eds.dbl_cap = 0;
  eds.str_cnt = eds.str_cap = 0;
  eds.var_cnt = eds.var_cap = 0;
  dump_node (&eds, n);
  emit (&eds, OP_SENTINEL);

  /* Now compute the stack height needed to evaluate the expression. */
  for (o = expr->op; *o != OP_SENTINEL; o++)
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

  expr->stack = xmalloc (max_height * sizeof *expr->stack);

  expr->pool = pool_create ();
}

static void
dump_node (struct expr_dump_state *eds, union any_node * n)
{
  if (IS_NONTERMINAL (n->type))
    {
      int i;
      for (i = 0; i < n->nonterm.n; i++)
	dump_node (eds, n->nonterm.arg[i]);
      emit (eds, n->type);
      if (ops[n->type].flags & OP_VAR_ARGS)
	emit (eds, n->nonterm.n);
      if (ops[n->type].flags & OP_MIN_ARGS)
	emit (eds, (int) n->nonterm.arg[n->nonterm.n]);
      if (ops[n->type].flags & OP_FMT_SPEC)
	{
	  emit (eds, (int) n->nonterm.arg[n->nonterm.n]);
	  emit (eds, (int) n->nonterm.arg[n->nonterm.n + 1]);
	  emit (eds, (int) n->nonterm.arg[n->nonterm.n + 2]);
	}
    }
  else 
    {
      emit (eds, n->type);
      if (n->type == OP_NUM_CON)
        emit_num_con (eds, n->num_con.value);
      else if (n->type == OP_STR_CON)
        emit_str_con (eds, n->str_con.s, n->str_con.len);
      else if (n->type == OP_NUM_VAR || n->type == OP_STR_VAR)
        emit_var (eds, n->var.v);
      else if (n->type == OP_NUM_LAG || n->type == OP_STR_LAG)
        {
          emit_var (eds, n->lag.v);
          emit (eds, n->lag.lag);
        }
      else if (n->type == OP_NUM_SYS || n->type == OP_NUM_VAL)
        emit (eds, n->var.v->fv);
      else
        assert (n->type == OP_CASENUM);
    }
}

static void
emit (struct expr_dump_state *eds, int op)
{
  if (eds->op_cnt >= eds->op_cap)
    {
      eds->op_cap += 16;
      eds->expr->op = xrealloc (eds->expr->op,
                                eds->op_cap * sizeof *eds->expr->op);
    }
  eds->expr->op[eds->op_cnt++] = op;
}

static void
emit_num_con (struct expr_dump_state *eds, double dbl)
{
  if (eds->dbl_cnt >= eds->dbl_cap)
    {
      eds->dbl_cap += 16;
      eds->expr->num = xrealloc (eds->expr->num,
                                 eds->dbl_cap * sizeof *eds->expr->num);
    }
  eds->expr->num[eds->dbl_cnt++] = dbl;
}

static void
emit_str_con (struct expr_dump_state *eds, char *str, int len)
{
  if (eds->str_cnt + len + 1 > eds->str_cap)
    {
      eds->str_cap += 256;
      eds->expr->str = xrealloc (eds->expr->str, eds->str_cap);
    }
  eds->expr->str[eds->str_cnt++] = len;
  memcpy (&eds->expr->str[eds->str_cnt], str, len);
  eds->str_cnt += len;
}

static void
emit_var (struct expr_dump_state *eds, struct variable * v)
{
  if (eds->var_cnt >= eds->var_cap)
    {
      eds->var_cap += 16;
      eds->expr->var = xrealloc (eds->expr->var,
                                 eds->var_cap * sizeof *eds->expr->var);
    }
  eds->expr->var[eds->var_cnt++] = v;
}
