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

#if TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#include <ctype.h>
#include "expr.h"
#include "exprP.h"
#include <assert.h>
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include "data-in.h"
#include "error.h"
#include "julcal/julcal.h"
#include "magic.h"
#include "misc.h"
#include "pool.h"
#include "random.h"
#include "stats.h"
#include "str.h"
#include "var.h"
#include "vfm.h"
#include "vfmP.h"

double
expr_evaluate (struct expression *e, const struct ccase *c, int case_num,
               union value *v)
{
  unsigned char *op = e->op;
  double *dbl = e->num;
  unsigned char *str = e->str;
  struct variable **vars = e->var;
  int i, j;

  /* Stack pointer. */
  union value *sp = e->stack;

  pool_clear (e->pool);

  for (;;)
    {
      switch (*op++)
	{
	case OP_PLUS:
	  sp -= *op - 1;
	  if (sp->f != SYSMIS)
	    for (i = 1; i < *op; i++)
	      {
		if (sp[i].f == SYSMIS)
		  {
		    sp->f = SYSMIS;
		    break;
		  }
		else
		  sp->f += sp[i].f;
	      }
	  op++;
	  break;
	case OP_MUL:
	  sp -= *op - 1;
	  if (sp->f != SYSMIS)
	    for (i = 1; i < *op; i++)
	      {
		if (sp[i].f == SYSMIS)
		  {
		    sp->f = SYSMIS;
		    break;
		  }
		else
		  sp->f *= sp[i].f;
	      }
	  op++;
	  break;
	case OP_POW:
	  sp--;
	  if (sp[0].f == SYSMIS)
	    {
	      if (sp[1].f == 0.0)
		sp->f = 1.0;
	    }
	  else if (sp[1].f == SYSMIS)
	    {
	      if (sp[0].f == 0.0)
		/* SYSMIS**0 */
		sp->f = 0.0;
	      else
		sp->f = SYSMIS;
	    }
	  else if (sp[0].f == 0.0 && sp[1].f == 0.0)
	    sp->f = SYSMIS;
	  else
	    sp->f = pow (sp[0].f, sp[1].f);
	  break;

	case OP_AND:
	  /* Note that booleans are always one of 0, 1, or SYSMIS.

	     Truth table (in order of detection):

	     1:
	     0 and 0 = 0   
	     0 and 1 = 0         
	     0 and SYSMIS = 0
	     
	     2:
	     1 and 0 = 0   
	     SYSMIS and 0 = 0
	     
	     3:
	     1 and SYSMIS = SYSMIS
	     SYSMIS and SYSMIS = SYSMIS
	     
	     4:
	     1 and 1 = 1
	     SYSMIS and 1 = SYSMIS

	   */
	  sp--;
	  if (sp[0].f == 0.0);	/* 1 */
	  else if (sp[1].f == 0.0)
	    sp->f = 0.0;	/* 2 */
	  else if (sp[1].f == SYSMIS)
	    sp->f = SYSMIS;	/* 3 */
	  break;
	case OP_OR:
	  /* Truth table (in order of detection):

	     1:
	     1 or 1 = 1
	     1 or 0 = 1
	     1 or SYSMIS = 1
	 
	     2:
	     0 or 1 = 1
	     SYSMIS or 1 = 1
	 
	     3:
	     0 or SYSMIS = SYSMIS
	     SYSMIS or SYSMIS = SYSMIS
	 
	     4:
	     0 or 0 = 0
	     SYSMIS or 0 = SYSMIS

	   */
	  sp--;
	  if (sp[0].f == 1.0);	/* 1 */
	  else if (sp[1].f == 1.0)
	    sp->f = 1.0;	/* 2 */
	  else if (sp[1].f == SYSMIS)
	    sp->f = SYSMIS;	/* 3 */
	  break;
	case OP_NOT:
	  if (sp[0].f == 0.0)
	    sp->f = 1.0;
	  else if (sp[0].f == 1.0)
	    sp->f = 0.0;
	  break;

	case OP_EQ:
	  sp--;
	  if (sp[0].f != SYSMIS)
	    {
	      if (sp[1].f == SYSMIS)
		sp->f = SYSMIS;
	      else
		sp->f = sp[0].f == sp[1].f;
	    }
	  break;
	case OP_GE:
	  sp--;
	  if (sp[0].f != SYSMIS)
	    {
	      if (sp[1].f == SYSMIS)
		sp->f = SYSMIS;
	      else
		sp->f = sp[0].f >= sp[1].f;
	    }
	  break;
	case OP_GT:
	  sp--;
	  if (sp[0].f != SYSMIS)
	    {
	      if (sp[1].f == SYSMIS)
		sp->f = SYSMIS;
	      else
		sp->f = sp[0].f > sp[1].f;
	    }
	  break;
	case OP_LE:
	  sp--;
	  if (sp[0].f != SYSMIS)
	    {
	      if (sp[1].f == SYSMIS)
		sp->f = SYSMIS;
	      else
		sp->f = sp[0].f <= sp[1].f;
	    }
	  break;
	case OP_LT:
	  sp--;
	  if (sp[0].f != SYSMIS)
	    {
	      if (sp[1].f == SYSMIS)
		sp->f = SYSMIS;
	      else
		sp->f = sp[0].f < sp[1].f;
	    }
	  break;
	case OP_NE:
	  sp--;
	  if (sp[0].f != SYSMIS)
	    {
	      if (sp[1].f == SYSMIS)
		sp->f = SYSMIS;
	      else
		sp->f = sp[0].f != sp[1].f;
	    }
	  break;

	  /* String operators. */
	case OP_STRING_EQ:
	  sp--;
	  sp[0].f = st_compare_pad (&sp[0].c[1], sp[0].c[0],
				    &sp[1].c[1], sp[1].c[0]) == 0;
	  break;
	case OP_STRING_GE:
	  sp--;
	  sp[0].f = st_compare_pad (&sp[0].c[1], sp[0].c[0],
				    &sp[1].c[1], sp[1].c[0]) >= 0;
	  break;
	case OP_STRING_GT:
	  sp--;
	  sp[0].f = st_compare_pad (&sp[0].c[1], sp[0].c[0],
				    &sp[1].c[1], sp[1].c[0]) > 0;
	  break;
	case OP_STRING_LE:
	  sp--;
	  sp[0].f = st_compare_pad (&sp[0].c[1], sp[0].c[0],
				    &sp[1].c[1], sp[1].c[0]) <= 0;
	  break;
	case OP_STRING_LT:
	  sp--;
	  sp[0].f = st_compare_pad (&sp[0].c[1], sp[0].c[0],
				    &sp[1].c[1], sp[1].c[0]) < 0;
	  break;
	case OP_STRING_NE:
	  sp--;
	  sp[0].f = st_compare_pad (&sp[0].c[1], sp[0].c[0],
				    &sp[1].c[1], sp[1].c[0]) != 0;
	  break;

	  /* Unary functions. */
	case OP_NEG:
	  if (sp->f != SYSMIS)
	    sp->f = -sp->f;
	  break;
	case OP_ABS:
	  if (sp->f != SYSMIS)
	    sp->f = fabs (sp->f);
	  break;
	case OP_ARCOS:
	  if (sp->f != SYSMIS)
	    {
	      errno = 0;
	      sp->f = acos (sp->f);
	      if (errno)
		sp->f = SYSMIS;
	    }
	  break;
	case OP_ARSIN:
	  if (sp->f != SYSMIS)
	    {
	      errno = 0;
	      sp->f = asin (sp->f);
	      if (errno)
		sp->f = SYSMIS;
	    }
	  break;
	case OP_ARTAN:
	  if (sp->f != SYSMIS)
	    sp->f = atan (sp->f);
	  break;
	case OP_COS:
	  if (sp->f != SYSMIS)
	    sp->f = cos (sp->f);
	  break;
	case OP_EXP:
	  if (sp->f != SYSMIS)
	    {
	      errno = 0;
	      sp->f = exp (sp->f);
	      if (errno)
		sp->f = SYSMIS;
	    }
	  break;
	case OP_LG10:
	  if (sp->f != SYSMIS)
	    {
	      errno = 0;
	      sp->f = log10 (sp->f);
	      if (errno)
		sp->f = SYSMIS;
	    }
	  break;
	case OP_LN:
	  if (sp->f != SYSMIS)
	    {
	      errno = 0;
	      sp->f = log10 (sp->f);
	      if (errno)
		sp->f = SYSMIS;
	    }
	  break;
	case OP_MOD10:
	  if (sp->f != SYSMIS)
	    sp->f = fmod (sp->f, 10);
	  break;
	case OP_RND:
	  if (sp->f != SYSMIS)
	    {
	      if (sp->f >= 0.0)
		sp->f = floor (sp->f + 0.5);
	      else
		sp->f = -floor (-sp->f + 0.5);
	    }
	  break;
	case OP_SIN:
	  if (sp->f != SYSMIS)
	    sp->f = sin (sp->f);
	  break;
	case OP_SQRT:
	  if (sp->f != SYSMIS)
	    {
	      errno = 0;
	      sp->f = sqrt (sp->f);
	      if (errno)
		sp->f = SYSMIS;
	    }
	  break;
	case OP_TAN:
	  if (sp->f != SYSMIS)
	    {
	      errno = 0;
	      sp->f = tan (sp->f);
	      if (errno)
		sp->f = SYSMIS;
	    }
	  break;
	case OP_TRUNC:
	  if (sp->f != SYSMIS)
	    {
	      if (sp->f >= 0.0)
		sp->f = floor (sp->f);
	      else
		sp->f = -floor (-sp->f);
	    }
	  break;

	  /* N-ary numeric functions. */
	case OP_ANY:
	  {
	    int n_args = *op++;
	    int sysmis = 1;

	    sp -= n_args - 1;
	    if (sp->f == SYSMIS)
	      break;
	    for (i = 1; i <= n_args; i++)
	      if (sp[0].f == sp[i].f)
		{
		  sp->f = 1.0;
		  goto main_loop;
		}
	      else if (sp[i].f != SYSMIS)
		sysmis = 0;
	    sp->f = sysmis ? SYSMIS : 0.0;
	  }
	  break;
	case OP_ANY_STRING:
	  {
	    int n_args = *op++;

	    sp -= n_args - 1;
	    for (i = 1; i <= n_args; i++)
	      if (!st_compare_pad (&sp[0].c[1], sp[0].c[0],
				   &sp[i].c[1], sp[i].c[0]))
		{
		  sp->f = 1.0;
		  goto main_loop;
		}
	    sp->f = 0.0;
	  }
	  break;
	case OP_CFVAR:
	  {
	    int n_args = *op++;
	    int nv = 0;
	    double sum[2] =
	    {0.0, 0.0};

	    sp -= n_args - 1;
	    for (i = 0; i < n_args; i++)
	      if (sp[i].f != SYSMIS)
		{
		  nv++;
		  sum[0] += sp[i].f;
		  sum[1] += sp[i].f * sp[i].f;
		}
	    if (nv < *op++)
	      sp->f = SYSMIS;
	    else
	      sp->f = calc_cfvar (sum, nv);
	  }
	  break;
	case OP_MAX:
	  {
	    int n_args = *op++;
	    int nv = 0;
	    double max = -DBL_MAX;

	    sp -= n_args - 1;
	    for (i = 0; i < n_args; i++)
	      if (sp[i].f != SYSMIS)
		{
		  nv++;
		  if (sp[i].f > max)
		    max = sp[i].f;
		}
	    if (nv < *op++)
	      sp->f = SYSMIS;
	    else
	      sp->f = max;
	  }
	  break;
	case OP_MEAN:
	  {
	    int n_args = *op++;
	    int nv = 0;
	    double sum[1] =
	    {0.0};

	    sp -= n_args - 1;
	    for (i = 0; i < n_args; i++)
	      if (sp[i].f != SYSMIS)
		{
		  nv++;
		  sum[0] += sp[i].f;
		}
	    if (nv < *op++)
	      sp->f = SYSMIS;
	    else
	      sp->f = calc_mean (sum, nv);
	  }
	  break;
	case OP_MIN:
	  {
	    int n_args = *op++;
	    int nv = 0;
	    double min = DBL_MAX;

	    sp -= n_args - 1;
	    for (i = 0; i < n_args; i++)
	      if (sp[i].f != SYSMIS)
		{
		  nv++;
		  if (sp[i].f < min)
		    min = sp[i].f;
		}
	    if (nv < *op++)
	      sp->f = SYSMIS;
	    else
	      sp->f = min;
	  }
	  break;
	case OP_NMISS:
	  {
	    int n_args = *op++;
	    int n_missing = 0;

	    sp -= n_args - 1;
	    for (i = 0; i < n_args; i++)
	      if (sp[i].f == SYSMIS)
		n_missing++;
	    sp->f = n_missing;
	  }
	  break;
	case OP_NVALID:
	  {
	    int n_args = *op++;
	    int n_valid = 0;

	    sp -= n_args - 1;
	    for (i = 0; i < n_args; i++)
	      if (sp[i].f != SYSMIS)
		n_valid++;
	    sp->f = n_valid;
	  }
	  break;
	case OP_RANGE:
	  {
	    int n_args = *op++;
	    int sysmis = 1;

	    sp -= n_args - 1;
	    if (sp->f == SYSMIS)
	      break;
	    for (i = 1; i <= n_args; i += 2)
	      if (sp[i].f == SYSMIS || sp[i + 1].f == SYSMIS)
		continue;
	      else if (sp[0].f >= sp[i].f && sp[0].f <= sp[i + 1].f)
		{
		  sp->f = 1.0;
		  goto main_loop;
		}
	      else
		sysmis = 0;
	    sp->f = sysmis ? SYSMIS : 0.0;
	  }
	  break;
	case OP_RANGE_STRING:
	  {
	    int n_args = *op++;

	    sp -= n_args - 1;
	    for (i = 1; i <= n_args; i += 2)
	      if (st_compare_pad (&sp[0].c[1], sp[0].c[0],
				  &sp[i].c[1], sp[i].c[0]) >= 0
		  && st_compare_pad (&sp[0].c[1], sp[0].c[0],
				     &sp[i + 1].c[1], sp[i + 1].c[0]) <= 0)
		{
		  sp->f = 1.0;
		  goto main_loop;
		}
	    sp->f = 0.0;
	  }
	  break;
	case OP_SD:
	  {
	    int n_args = *op++;
	    int nv = 0;
	    double sum[2];

	    sum[0] = sum[1] = 0.0;

	    sp -= n_args - 1;
	    for (i = 0; i < n_args; i++)
	      if (sp[i].f != SYSMIS)
		{
		  nv++;
		  sum[0] += sp[i].f;
		  sum[1] += sp[i].f * sp[i].f;
		}
	    if (nv < *op++)
	      sp->f = SYSMIS;
	    else
	      sp->f = calc_stddev (calc_variance (sum, nv));
	  }
	  break;
	case OP_SUM:
	  {
	    int n_args = *op++;
	    int nv = 0;
	    double sum = 0.0;

	    sp -= n_args - 1;
	    for (i = 0; i < n_args; i++)
	      if (sp[i].f != SYSMIS)
		{
		  nv++;
		  sum += sp[i].f;
		}
	    if (nv < *op++)
	      sp->f = SYSMIS;
	    else
	      sp->f = sum;
	  }
	  break;
	case OP_VARIANCE:
	  {
	    int n_args = *op++;
	    int nv = 0;
	    double sum[2];

	    sum[0] = sum[1] = 0.0;

	    sp -= n_args - 1;
	    for (i = 0; i < n_args; i++)
	      if (sp[i].f != SYSMIS)
		{
		  nv++;
		  sum[0] += sp[i].f;
		  sum[1] += sp[i].f * sp[i].f;
		}
	    if (nv < *op++)
	      sp->f = SYSMIS;
	    else
	      sp->f = calc_variance (sum, nv);
	  }
	  break;

	  /* Time construction function. */
	case OP_TIME_HMS:
	  sp -= 2;
	  if (sp[0].f == SYSMIS || sp[1].f == SYSMIS || sp[2].f == SYSMIS)
	    sp->f = SYSMIS;
	  else
	    sp->f = 60. * (60. * sp[0].f + sp[1].f) + sp[2].f;
	  break;

	  /* Date construction functions. */
	case OP_DATE_DMY:
	  sp -= 2;
	  sp->f = yrmoda (sp[2].f, sp[1].f, sp[0].f);
	  if (sp->f != SYSMIS)
	    sp->f *= 60. * 60. * 24.;
	  break;
	case OP_DATE_MDY:
	  sp -= 2;
	  sp->f = yrmoda (sp[2].f, sp[0].f, sp[1].f);
	  if (sp->f != SYSMIS)
	    sp->f *= 60. * 60. * 24.;
	  break;
	case OP_DATE_MOYR:
          sp--;
	  sp->f = yrmoda (sp[1].f, sp[0].f, 1);
	  if (sp->f != SYSMIS)
	    sp->f *= 60. * 60. * 24.;
	  break;
	case OP_DATE_QYR:
	  sp--;
	  if (sp[0].f == SYSMIS)
	    sp->f = SYSMIS;
	  else
	    {
	      sp->f = yrmoda (sp[1].f, sp[0].f * 3 - 2, 1);
	      if (sp->f != SYSMIS)
		sp->f *= 60. * 60. * 24.;
	    }
	  break;
	case OP_DATE_WKYR:
	  sp--;
	  if (sp[0].f == SYSMIS)
	    sp->f = SYSMIS;
	  else
	    {
	      sp[1].f = yrmoda (sp[1].f, 1, 1);
	      if (sp->f != SYSMIS)
		sp[1].f = 60. * 60. * 24. * (sp[1].f + 7. * (floor (sp[0].f) - 1.));
	      sp->f = sp[1].f;
	    }
	  break;
	case OP_DATE_YRDAY:
	  sp--;
	  if (sp[1].f == SYSMIS)
	    sp->f = SYSMIS;
	  else
	    {
	      sp->f = yrmoda (sp[0].f, 1, 1);
	      if (sp->f != SYSMIS)
		sp->f = 60. * 60. * 24. * (sp->f + floor (sp[1].f) - 1);
	    }
	  break;
	case OP_YRMODA:
	  sp -= 2;
	  sp->f = yrmoda (sp[0].f, sp[1].f, sp[2].f);
	  break;

	  /* Date extraction functions. */
	case OP_XDATE_DATE:
	  if (sp->f != SYSMIS)
	    sp->f = floor (sp->f / 60. / 60. / 24.) * 60. * 60. * 24.;
	  break;
	case OP_XDATE_HOUR:
	  if (sp->f != SYSMIS)
	    sp->f = fmod (floor (sp->f / 60. / 60.), 24.);
	  break;
	case OP_XDATE_JDAY:
	  if (sp->f != SYSMIS)
	    sp->f = 86400. * julian_to_jday (sp->f / 86400.);
	  break;
	case OP_XDATE_MDAY:
	  if (sp->f != SYSMIS)
	    {
	      int day;
	      julian_to_calendar (sp->f / 86400., NULL, NULL, &day);
	      sp->f = day;
	    }
	  break;
	case OP_XDATE_MINUTE:
	  if (sp->f != SYSMIS)
	    sp->f = fmod (floor (sp->f / 60.), 60.);
	  break;
	case OP_XDATE_MONTH:
	  if (sp->f != SYSMIS)
	    {
	      int month;
	      julian_to_calendar (sp->f / 86400., NULL, &month, NULL);
	      sp->f = month;
	    }
	  break;
	case OP_XDATE_QUARTER:
	  if (sp->f != SYSMIS)
	    {
	      int month;
	      julian_to_calendar (sp->f / 86400., NULL, &month, NULL);
	      sp->f = (month - 1) / 3 + 1;
	    }
	  break;
	case OP_XDATE_SECOND:
	  if (sp->f != SYSMIS)
	    sp->f = fmod (sp->f, 60.);
	  break;
	case OP_XDATE_TDAY:
	  if (sp->f != SYSMIS)
	    sp->f = floor (sp->f / 60. / 60. / 24.);
	  break;
	case OP_XDATE_TIME:
	  if (sp->f != SYSMIS)
	    sp->f -= floor (sp->f / 60. / 60. / 24.) * 60. * 60. * 24.;
	  break;
	case OP_XDATE_WEEK:
	  if (sp->f != SYSMIS)
	    sp->f = (julian_to_jday (sp->f / 86400.) - 1) / 7 + 1;
	  break;
	case OP_XDATE_WKDAY:
	  if (sp->f != SYSMIS)
	    sp->f = julian_to_wday (sp->f / 86400.);
	  break;
	case OP_XDATE_YEAR:
	  if (sp->f != SYSMIS)
	    {
	      int year;
	      julian_to_calendar (sp->f / 86400., &year, NULL, NULL);
	      sp->f = year;
	    }
	  break;

	  /* String functions. */
	case OP_CONCAT:
	  {
	    int n_args = *op++;
	    unsigned char *dest;

	    dest = pool_alloc (e->pool, 256);
	    dest[0] = 0;

	    sp -= n_args - 1;
	    for (i = 0; i < n_args; i++)
	      if (sp[i].c[0] != 0)
		{
		  if (sp[i].c[0] + dest[0] < 255)
		    {
		      memcpy (&dest[dest[0] + 1], &sp[i].c[1], sp[i].c[0]);
		      dest[0] += sp[i].c[0];
		    }
		  else
		    {
		      memcpy (&dest[dest[0] + 1], &sp[i].c[1], 255 - dest[0]);
		      dest[0] = 255;
		      break;
		    }
		}
	    sp[0].c = dest;
	  }
	  break;
	case OP_INDEX:
	  sp--;
	  if (sp[1].c[0] == 0)
	    sp->f = SYSMIS;
	  else
	    {
	      int last = sp[0].c[0] - sp[1].c[0];
	      for (i = 0; i <= last; i++)
		if (!memcmp (&sp[0].c[i + 1], &sp[0].c[1], sp[0].c[0]))
		  {
		    sp->f = i + 1;
		    goto main_loop;
		  }
	      sp->f = 0.0;
	    }
	  break;
	case OP_INDEX_OPT:
	  {
	    /* Length of each search string. */
	    int part_len = sp[2].f;

	    sp -= 2;
	    if (sp[1].c[0] == 0 || part_len <= 0 || sp[2].f == SYSMIS
		|| sp[1].c[0] % part_len != 0)
	      sp->f = SYSMIS;
	    else
	      {
		/* Last possible index. */
		int last = sp[0].c[0] - part_len;

		for (i = 0; i <= last; i++)
		  for (j = 0; j < sp[1].c[0]; j += part_len)
		    if (!memcmp (&sp[0].c[i], &sp[1].c[j], part_len))
		      {
			sp->f = i + 1;
			goto main_loop;
		      }
		sp->f = 0.0;
	      }
	  }
	  break;
	case OP_RINDEX:
	  sp--;
	  if (sp[1].c[0] == 0)
	    sp->f = SYSMIS;
	  else
	    {
	      for (i = sp[0].c[0] - sp[1].c[0]; i >= 0; i--)
		if (!memcmp (&sp[0].c[i + 1], &sp[0].c[1], sp[0].c[0]))
		  {
		    sp->f = i + 1;
		    goto main_loop;
		  }
	      sp->f = 0.0;
	    }
	  break;
	case OP_RINDEX_OPT:
	  {
	    /* Length of each search string. */
	    int part_len = sp[2].f;

	    sp -= 2;
	    if (sp[1].c[0] == 0 || part_len <= 0 || sp[2].f == SYSMIS
		|| sp[1].c[0] % part_len != 0)
	      sp->f = SYSMIS;
	    else
	      {
		for (i = sp[0].c[0] - part_len; i >= 0; i--)
		  for (j = 0; j < sp[1].c[0]; j += part_len)
		    if (!memcmp (&sp[0].c[i], &sp[1].c[j], part_len))
		      {
			sp->f = i + 1;
			goto main_loop;
		      }
		sp->f = 0.0;
	      }
	  }
	  break;
	case OP_LENGTH:
	  sp->f = sp[0].c[0];
	  break;
	case OP_LOWER:
	  for (i = sp[0].c[0]; i >= 1; i--)
	    sp[0].c[i] = tolower ((unsigned char) (sp[0].c[i]));
	  break;
	case OP_UPPER:
	  for (i = sp[0].c[0]; i >= 1; i--)
	    sp[0].c[i] = toupper ((unsigned char) (sp[0].c[i]));
	  break;
	case OP_LPAD:
	  {
	    int len;
	    sp--;
	    len = sp[1].f;
	    if (sp[1].f == SYSMIS || len < 0 || len > 255)
	      sp->c[0] = 0;
	    else if (len > sp[0].c[0])
	      {
		unsigned char *dest;

		dest = pool_alloc (e->pool, len + 1);
		dest[0] = len;
		memset (&dest[1], ' ', len - sp->c[0]);
		memcpy (&dest[len - sp->c[0] + 1], &sp->c[1], sp->c[0]);
		sp->c = dest;
	      }
	  }
	  break;
	case OP_LPAD_OPT:
	  {
	    int len;
	    sp -= 2;
	    len = sp[1].f;
	    if (sp[1].f == SYSMIS || len < 0 || len > 255 || sp[2].c[0] != 1)
	      sp->c[0] = 0;
	    else if (len > sp[0].c[0])
	      {
		unsigned char *dest;

		dest = pool_alloc (e->pool, len + 1);
		dest[0] = len;
		memset (&dest[1], sp[2].c[1], len - sp->c[0]);
		memcpy (&dest[len - sp->c[0] + 1], &sp->c[1], sp->c[0]);
		sp->c = dest;
	      }
	  }
	  break;
	case OP_RPAD:
	  {
	    int len;
	    sp--;
	    len = sp[1].f;
	    if (sp[1].f == SYSMIS || len < 0 || len > 255)
	      sp->c[0] = 0;
	    else if (len > sp[0].c[0])
	      {
		unsigned char *dest;

		dest = pool_alloc (e->pool, len + 1);
		dest[0] = len;
		memcpy (&dest[1], &sp->c[1], sp->c[0]);
		memset (&dest[sp->c[0] + 1], ' ', len - sp->c[0]);
		sp->c = dest;
	      }
	  }
	  break;
	case OP_RPAD_OPT:
	  {
	    int len;
	    sp -= 2;
	    len = sp[1].f;
	    if (len < 0 || len > 255 || sp[2].c[0] != 1)
	      sp->c[0] = 0;
	    else if (len > sp[0].c[0])
	      {
		unsigned char *dest;

		dest = pool_alloc (e->pool, len + 1);
		dest[0] = len;
		memcpy (&dest[1], &sp->c[1], sp->c[0]);
		memset (&dest[sp->c[0] + 1], sp[2].c[1], len - sp->c[0]);
		sp->c = dest;
	      }
	  }
	  break;
	case OP_LTRIM:
	  {
	    int len = sp[0].c[0];

	    i = 1;
	    while (i <= len && sp[0].c[i] == ' ')
	      i++;
	    if (--i)
	      {
		sp[0].c[i] = sp[0].c[0] - i;
		sp->c = &sp[0].c[i];
	      }
	  }
	  break;
	case OP_LTRIM_OPT:
	  {
	    sp--;
	    if (sp[1].c[0] != 1)
	      sp[0].c[0] = 0;
	    else
	      {
		int len = sp[0].c[0];
		int cmp = sp[1].c[1];

		i = 1;
		while (i <= len && sp[0].c[i] == cmp)
		  i++;
		if (--i)
		  {
		    sp[0].c[i] = sp[0].c[0] - i;
		    sp->c = &sp[0].c[i];
		  }
	      }
	  }
	  break;
	case OP_RTRIM:
	  assert (' ' != 0);
	  while (sp[0].c[sp[0].c[0]] == ' ')
	    sp[0].c[0]--;
	  break;
	case OP_RTRIM_OPT:
	  sp--;
	  if (sp[1].c[0] != 1)
	    sp[0].c[0] = 0;
	  else
	    {
	      /* Note that NULs are not allowed in strings.  This code
	         needs to change if this decision is changed. */
	      int cmp = sp[1].c[1];
	      while (sp[0].c[sp[0].c[0]] == cmp)
		sp[0].c[0]--;
	    }
	  break;
	case OP_NUMBER:
	  {
	    struct data_in di;

	    di.s = &sp->c[1];
	    di.e = &sp->c[1] + sp->c[0];
	    di.v = sp;
	    di.flags = DI_IGNORE_ERROR;
	    di.f1 = 1;
	    di.format.type = FMT_F;
	    di.format.w = sp->c[0];
	    di.format.d = 0;
	    data_in (&di);
	  }
	  break;
	case OP_NUMBER_OPT:
	  {
	    struct data_in di;
	    di.s = &sp->c[1];
	    di.e = &sp->c[1] + sp->c[0];
	    di.v = sp;
	    di.flags = DI_IGNORE_ERROR;
	    di.f1 = 1;
	    di.format.type = *op++;
	    di.format.w = *op++;
	    di.format.d = *op++;
	    data_in (&di);
	  }
	  break;
	case OP_STRING:
	  {
	    struct fmt_spec f;
	    unsigned char *dest;

	    f.type = *op++;
	    f.w = *op++;
	    f.d = *op++;

	    dest = pool_alloc (e->pool, f.w + 1);
	    dest[0] = f.w;

            assert ((formats[f.type].cat & FCAT_STRING) == 0);
	    data_out (&dest[1], &f, sp);
	    sp->c = dest;
	  }
	  break;
	case OP_SUBSTR:
	  {
	    int index;

	    sp--;
	    index = sp[1].f;
	    if (index < 1 || index > sp[0].c[0])
	      sp->c[0] = 0;
	    else if (index > 1)
	      {
		index--;
		sp->c[index] = sp->c[0] - index;
		sp->c += index;
	      }
	  }
	  break;
	case OP_SUBSTR_OPT:
	  {
	    int index;
	    int n;

	    sp -= 2;
	    index = sp[1].f;
	    n = sp[2].f;
	    if (sp[1].f == SYSMIS || sp[2].f == SYSMIS || index < 1
		|| index > sp[0].c[0] || n < 1)
	      sp->c[0] = 0;
	    else
	      {
		if (index > 1)
		  {
		    index--;
		    sp->c[index] = sp->c[0] - index;
		    sp->c += index;
		  }
		if (sp->c[0] > n)
		  sp->c[0] = n;
	      }
	  }
	  break;

	  /* Artificial. */
	case OP_INV:
	  if (sp->f != SYSMIS)
	    sp->f = 1. / sp->f;
	  break;
	case OP_SQUARE:
	  if (sp->f != SYSMIS)
	    sp->f *= sp->f;
	  break;
	case OP_NUM_TO_BOOL:
	  if (sp->f == 0.0)
	    sp->f = 0.0;
	  else if (sp->f == 1.0)
	    sp->f = 1.0;
	  else if (sp->f != SYSMIS)
	    {
	      msg (SE, _("A number being treated as a Boolean in an "
			 "expression was found to have a value other than "
			 "0 (false), 1 (true), or the system-missing value.  "
			 "The result was forced to 0."));
	      sp->f = 0.0;
	    }
	  break;

	  /* Weirdness. */
	case OP_MOD:
	  sp--;
	  if (sp[0].f != SYSMIS)
	    {
	      if (sp[1].f == SYSMIS)
		{
		  if (sp[0].f != 0.0)
		    sp->f = SYSMIS;
		}
	      else
		sp->f = fmod (sp[0].f, sp[1].f);
	    }
	  break;
	case OP_NORMAL:
	  if (sp->f != SYSMIS)
	    sp->f *= rng_get_double_normal (pspp_rng ());
	  break;
	case OP_UNIFORM:
	  if (sp->f != SYSMIS)
	    sp->f *= rng_get_double (pspp_rng ());
	  break;
	case OP_SYSMIS:
	  if (sp[0].f == SYSMIS || !finite (sp[0].f))
	    sp->f = 1.0;
	  else
	    sp->f = 0.0;
	  break;
	case OP_VEC_ELEM_NUM:
	  {
	    int rindx = sp[0].f + EPSILON;
	    const struct vector *v = dict_get_vector (default_dict, *op++);

	    if (sp[0].f == SYSMIS || rindx < 1 || rindx > v->cnt)
	      {
		if (sp[0].f == SYSMIS)
		  msg (SE, _("SYSMIS is not a valid index value for vector "
			     "%s.  The result will be set to SYSMIS."),
		       v->name);
		else
		  msg (SE, _("%g is not a valid index value for vector %s.  "
			     "The result will be set to SYSMIS."),
		       sp[0].f, v->name);
		sp->f = SYSMIS;
		break;
	      }
	    sp->f = c->data[v->var[rindx - 1]->fv].f;
	  }
	  break;
	case OP_VEC_ELEM_STR:
	  {
	    int rindx = sp[0].f + EPSILON;
	    const struct vector *vect = dict_get_vector (default_dict, *op++);
	    struct variable *v;

	    if (sp[0].f == SYSMIS || rindx < 1 || rindx > vect->cnt)
	      {
		if (sp[0].f == SYSMIS)
		  msg (SE, _("SYSMIS is not a valid index value for vector "
			     "%s.  The result will be set to the empty "
			     "string."),
		       vect->name);
		else
		  msg (SE, _("%g is not a valid index value for vector %s.  "
			     "The result will be set to the empty string."),
		       sp[0].f, vect->name);
		sp->c = pool_alloc (e->pool, 1);
		sp->c[0] = 0;
		break;
	      }

	    v = vect->var[rindx - 1];
	    sp->c = pool_alloc (e->pool, v->width + 1);
	    sp->c[0] = v->width;
	    memcpy (&sp->c[1], c->data[v->fv].s, v->width);
	  }
	  break;

	  /* Terminals. */
	case OP_NUM_CON:
	  sp++;
	  sp->f = *dbl++;
	  break;
	case OP_STR_CON:
	  sp++;
	  sp->c = pool_alloc (e->pool, *str + 1);
	  memcpy (sp->c, str, *str + 1);
	  str += *str + 1;
	  break;
	case OP_NUM_VAR:
	  sp++;
	  sp->f = c->data[(*vars)->fv].f;
	  if (is_num_user_missing (sp->f, *vars))
	    sp->f = SYSMIS;
	  vars++;
	  break;
	case OP_STR_VAR:
	  {
	    int width = (*vars)->width;

	    sp++;
	    sp->c = pool_alloc (e->pool, width + 1);
	    sp->c[0] = width;
	    memcpy (&sp->c[1], &c->data[(*vars)->fv], width);
	    vars++;
	  }
	  break;
	case OP_NUM_LAG:
	  {
	    struct ccase *c = lagged_case (*op++);

	    sp++;
	    if (c == NULL)
	      sp->f = SYSMIS;
	    else
	      {
		sp->f = c->data[(*vars)->fv].f;
		if (is_num_user_missing (sp->f, *vars))
		  sp->f = SYSMIS;
	      }
	    vars++;
	    break;
	  }
	case OP_STR_LAG:
	  {
	    struct ccase *c = lagged_case (*op++);
	    int width = (*vars)->width;

	    sp++;
	    sp->c = pool_alloc (e->pool, width + 1);
	    sp->c[0] = width;
	    
	    if (c == NULL)
	      memset (sp->c, ' ', width);
	    else
	      memcpy (&sp->c[1], &c->data[(*vars)->fv], width);
	    
	    vars++;
	  }
	  break;
	case OP_NUM_SYS:
	  sp++;
	  sp->f = c->data[*op++].f == SYSMIS;
	  break;
	case OP_STR_MIS:
	  sp++;
	  sp->f = is_str_user_missing (c->data[(*vars)->fv].s, *vars);
	  vars++;
	  break;
	case OP_NUM_VAL:
	  sp++;
	  sp->f = c->data[*op++].f;
	  break;
	case OP_CASENUM:
	  sp++;
	  sp->f = case_num;
	  break;

	case OP_SENTINEL:
	  goto finished;

	default:
	  assert (0);
	}

    main_loop: ;
    }
finished:
  if (e->type != EX_STRING)
    {
      double value = sp->f;
      if (!finite (value))
	value = SYSMIS;
      if (v)
	v->f = value;
      return value;
    }
  else
    {
      assert (v);

      v->c = sp->c;

      return 0.0;
    }
}
