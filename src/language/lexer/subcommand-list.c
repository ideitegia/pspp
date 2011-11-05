/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2011 Free Software Foundation, Inc.

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
#include "language/lexer/subcommand-list.h"
#include <stdlib.h>
#include "language/lexer/lexer.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* I call these objects `lists' but they are in fact simple dynamic arrays */

#define CHUNKSIZE 16

/* Create a  list */
void
subc_list_double_create(subc_list_double *l)
{
  l->data = xnmalloc (CHUNKSIZE, sizeof *l->data);
  l->sz = CHUNKSIZE;
  l->n_data = 0;
}

void
subc_list_int_create(subc_list_int *l)
{
  l->data = xnmalloc (CHUNKSIZE, sizeof *l->data);
  l->sz = CHUNKSIZE;
  l->n_data = 0;
}

/* Push a value onto the list */
void
subc_list_double_push(subc_list_double *l, double d)
{
  l->data[l->n_data++] = d;

  if (l->n_data >= l->sz )
    {
      l->sz += CHUNKSIZE;
      l->data = xnrealloc (l->data, l->sz, sizeof *l->data);
    }

}

void
subc_list_int_push(subc_list_int *l, int d)
{
  l->data[l->n_data++] = d;

  if (l->n_data >= l->sz )
    {
      l->sz += CHUNKSIZE;
      l->data = xnrealloc (l->data, l->sz, sizeof *l->data);
    }

}

/* Return the number of items in the list */
int
subc_list_double_count(const subc_list_double *l)
{
  return l->n_data;
}

int
subc_list_int_count(const subc_list_int *l)
{
  return l->n_data;
}


/* Index into the list (array) */
double
subc_list_double_at(const subc_list_double *l, int idx)
{
  return l->data[idx];
}

int
subc_list_int_at(const subc_list_int *l, int idx)
{
  return l->data[idx];
}

/* Free up the list */
void
subc_list_double_destroy(subc_list_double *l)
{
  free(l->data);
}

void
subc_list_int_destroy(subc_list_int *l)
{
  free(l->data);
}

void
subc_list_error (struct lexer *lexer, const char *sbc, int max_list)
{
  lex_error (lexer, _("No more than %d %s subcommands allowed."),
             max_list, sbc);
}
