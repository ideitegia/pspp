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

#include "math/levene.h"

#include <math.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/variable.h"
#include "libpspp/hmap.h"
#include "libpspp/misc.h"

struct lev
{
  struct hmap_node node;
  union value group;
  int width;

  double t_bar;
  double z_mean;
  double n;
};


static struct lev *
find_group (struct hmap *map, const union value *target, int width)
{
  struct lev *l = NULL;
  unsigned int hash = value_hash (target, width, 0);
  HMAP_FOR_EACH_WITH_HASH (l, struct lev, node, hash, map)
    {
      if (value_equal (&l->group, target, width))
	break;
      l = NULL;
    }

  return l;
}


double
levene (struct casereader *rx, const struct variable *gvar,
	const struct variable *var, const struct variable *wv,
	enum mv_class exclude)
{
  double numerator = 0.0;
  double denominator = 0.0;
  int n_groups = 0;
  double z_grand_mean = 0.0;
  double grand_n = 0.0;

  struct hmap map = HMAP_INITIALIZER (map);

  struct ccase *c;
  struct casereader *r = casereader_clone (rx);

  for (; (c = casereader_read (r)) != NULL; case_unref (c))
    {
      struct lev *l = NULL;
      const union value *target = case_data (c, gvar);
      int width = var_get_width (gvar);
      unsigned int hash = value_hash (target, width, 0);
      const double x = case_data (c, var)->f;
      const double weight = wv ? case_data (c, wv)->f : 1.0;

      if (var_is_value_missing (var, case_data (c, var), exclude))
	continue;

      l = find_group (&map, target, width);
      
      if (l == NULL)
	{
	  l = xzalloc (sizeof *l);
	  value_clone (&l->group, target, width);
	  hmap_insert (&map, &l->node, hash);
	}

      l->n += weight;
      l->t_bar += x * weight;
      grand_n += weight;
    }
  casereader_destroy (r);

  {
    struct lev *l;
    HMAP_FOR_EACH (l, struct lev, node, &map)
      {
	l->t_bar /= l->n;
      }
  }

  n_groups = hmap_count (&map);

  r = casereader_clone (rx);
  for (; (c = casereader_read (r)) != NULL; case_unref (c))
    {
      struct lev *l = NULL;
      const union value *target = case_data (c, gvar);
      int width = var_get_width (gvar);
      const double x = case_data (c, var)->f;
      const double weight = wv ? case_data (c, wv)->f : 1.0;

      if (var_is_value_missing (var, case_data (c, var), exclude))
	continue;

      l = find_group (&map, target, width);
      assert (l);
      
      l->z_mean += fabs (x - l->t_bar) * weight;
      z_grand_mean += fabs (x - l->t_bar) * weight;
    }
  casereader_destroy (r);

  {
    struct lev *l;
    HMAP_FOR_EACH (l, struct lev, node, &map)
      {
	l->z_mean /= l->n;
      }
  }

  z_grand_mean /= grand_n;

  r = casereader_clone (rx);
  for (; (c = casereader_read (r)) != NULL; case_unref (c))
    {
      double z;
      struct lev *l;
      const union value *target = case_data (c, gvar);
      int width = var_get_width (gvar);
      const double x = case_data (c, var)->f;
      const double weight = wv ? case_data (c, wv)->f : 1.0;

      if (var_is_value_missing (var, case_data (c, var), exclude))
	continue;

      l = find_group (&map, target, width);
      assert (l);

      z = fabs (x - l->t_bar);
      denominator += pow2 (z - l->z_mean) * weight;
    }
  casereader_destroy (r);

  denominator *= n_groups - 1;

  {
    double grand_n = 0.0;
    struct lev *next;
    struct lev *l;
    HMAP_FOR_EACH_SAFE (l, next, struct lev, node, &map)
      {
	numerator += l->n * pow2 (l->z_mean - z_grand_mean);
	grand_n += l->n;

	/* We don't need these anymore */
	free (l);
      }
    numerator *= grand_n - n_groups ;
    hmap_destroy (&map);
  }

  return numerator/ denominator;
}
