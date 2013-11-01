/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include "levene.h"
#include <math.h>

#include "libpspp/misc.h"
#include "libpspp/hmap.h"
#include "data/value.h"
#include "data/val-type.h"

#include <gl/xalloc.h>
#include <assert.h>

struct lev
{
  struct hmap_node node;
  union value group;

  double t_bar;
  double z_mean;
  double n;
};

typedef unsigned int hash_func (const struct levene *, const union value *v);
typedef bool cmp_func (const struct levene *, const union value *v0, const union value *v1);

struct levene
{
  /* Width of the categorical variable */
  int gvw ;

  /* The value dividing the groups. Valid only for dichotomous categorical variable.*/
  const union value *cutpoint;


  /* A hashtable of struct lev objects indexed by union value */
  struct hmap hmap;

  hash_func *hash;
  cmp_func *cmp;


  /* A state variable indicating how many passes have been done */
  int pass;

  double grand_n;
  double z_grand_mean;

  double denominator;
};


static unsigned int
unique_hash (const struct levene *nl, const union value *val)
{
  return value_hash (val, nl->gvw, 0);
}

static bool
unique_cmp (const struct levene *nl, const union value *val0, const union value *val1)
{
  return value_equal (val0, val1, nl->gvw);
}

static unsigned int
cutpoint_hash (const struct levene *nl, const union value *val)
{
  int x = value_compare_3way (val, nl->cutpoint, nl->gvw);

  return (x < 0);
}

static bool
cutpoint_cmp (const struct levene *nl, const union value *val0, const union value *val1)
{
  int x = value_compare_3way (val0, nl->cutpoint, nl->gvw);

  int y = value_compare_3way (val1, nl->cutpoint, nl->gvw);

  if ( x == 0) x = 1;
  if ( y == 0) y = 1;

  return ( x == y);
}



static struct lev *
find_group (const struct levene *nl, const union value *target)
{
  struct lev *l = NULL;

  HMAP_FOR_EACH_WITH_HASH (l, struct lev, node, nl->hash (nl, target), &nl->hmap)
    {
      if (nl->cmp (nl, &l->group, target))
	break;
      l = NULL;
    }
  return l;
}


struct levene *
levene_create (int indep_width, const union value *cutpoint)
{
  struct levene *nl = xzalloc (sizeof *nl);

  hmap_init (&nl->hmap);

  nl->gvw = indep_width;
  nl->cutpoint = cutpoint;

  nl->hash  = cutpoint ? cutpoint_hash : unique_hash;
  nl->cmp   = cutpoint ? cutpoint_cmp : unique_cmp;

  return nl;
}


/* Data accumulation. First pass */
void 
levene_pass_one (struct levene *nl, double value, double weight, const union value *gv)
{
  struct lev *lev = find_group (nl, gv);

  if ( nl->pass == 0 ) 
    {
      nl->pass = 1;
    }
  assert (nl->pass == 1);

  if ( NULL == lev)
    {
      struct lev *l = xzalloc (sizeof *l);
      value_clone (&l->group, gv, nl->gvw);
      hmap_insert (&nl->hmap, &l->node, nl->hash (nl, &l->group));
      lev = l;
    }

  lev->n += weight;
  lev->t_bar += value * weight;

  nl->grand_n += weight;
}

/* Data accumulation. Second pass */
void 
levene_pass_two (struct levene *nl, double value, double weight, const union value *gv)
{
  struct lev *lev = NULL;

  if ( nl->pass == 1 )
    {
      struct lev *next;
      struct lev *l;

      nl->pass = 2;

      HMAP_FOR_EACH_SAFE (l, next, struct lev, node, &nl->hmap)
      {
	l->t_bar /= l->n;
      }
    }
  assert (nl->pass == 2);

  lev = find_group (nl, gv);

  lev->z_mean += fabs (value - lev->t_bar) * weight;
  nl->z_grand_mean += fabs (value - lev->t_bar) * weight;
}

/* Data accumulation. Third pass */
void 
levene_pass_three (struct levene *nl, double value, double weight, const union value *gv)
{
  double z;
  struct lev *lev = NULL;

  if ( nl->pass == 2 )
    {
      struct lev *next;
      struct lev *l;

      nl->pass = 3;

      HMAP_FOR_EACH_SAFE (l, next, struct lev, node, &nl->hmap)
      {
	l->z_mean /= l->n;
      }

      nl->z_grand_mean /= nl->grand_n;
  }

  assert (nl->pass == 3);
  lev = find_group (nl, gv);

  z = fabs (value - lev->t_bar);
  nl->denominator += pow2 (z - lev->z_mean) * weight;
}


/* Return the value of the levene statistic */
double
levene_calculate (struct levene *nl)
{
  struct lev *next;
  struct lev *l;

  double numerator = 0.0;
  double nn = 0.0;

  /* The Levene calculation requires three passes.
     Normally this should have been done prior to calling this function.
     However, in abnormal circumstances (eg. the dataset is empty) there
     will have been no passes.
   */
  assert (nl->pass == 0 || nl->pass == 3);

  if ( nl->pass == 0 )
    return SYSMIS;

  nl->denominator *= hmap_count (&nl->hmap) - 1;

  HMAP_FOR_EACH_SAFE (l, next, struct lev, node, &nl->hmap)
    {
      numerator += l->n * pow2 (l->z_mean - nl->z_grand_mean);
      nn += l->n;
    }

  numerator *= nn - hmap_count (&nl->hmap);
    
  return numerator / nl->denominator;
}

void
levene_destroy (struct levene *nl)
{
  struct lev *next;
  struct lev *l;

  HMAP_FOR_EACH_SAFE (l, next, struct lev, node, &nl->hmap)
    {
      value_destroy (&l->group, nl->gvw);
      free (l);
    }

  hmap_destroy (&nl->hmap);
  free (nl);
}
