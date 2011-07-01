/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011 Free Software Foundation, Inc.

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

#include "output/charts/scree.h"

#include "output/chart-item-provider.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct scree *
scree_create (const gsl_vector *eigenvalues, const char *xlabel)
{
  struct scree *rc = xmalloc (sizeof *rc);
  chart_item_init (&rc->chart_item, &scree_class, NULL);

  rc->eval = gsl_vector_alloc (eigenvalues->size);
  gsl_vector_memcpy (rc->eval, eigenvalues);

  rc->xlabel = xstrdup (xlabel);

  return rc;
}

static void
scree_destroy (struct chart_item *chart_item)
{
  struct scree *rc = to_scree (chart_item);

  gsl_vector_free (rc->eval);
  free (rc->xlabel);
  free (rc);
}

const struct chart_item_class scree_class =
  {
    scree_destroy
  };
