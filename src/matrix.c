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
#include "matrix.h"
#include "error.h"
#include <stdlib.h>
#include "alloc.h"

/* Kahan summation formula, Thm. 8, _What Every Computer Scientist
   Should Know About Floating-Point Arithmetic_, David Goldberg,
   orig. March 1991 issue of Computing Surveys, also at
   <URL:http://www.wam.umd.edu/whats_new/workshop3.0/common-tools/numerical_comp_guide/goldberg1.doc.html>.
   Hopefully your compiler won't try to optimize the code below too
   much, because that will ruin the precision. */
#define KAHAN_SUMMATION_FORMULA(S)				\
	do 							\
	  {							\
	    double S_c;						\
	    int S_j;						\
								\
	    S = SUMMATION_ELEMENT (0);				\
	    S_c = 0.;						\
	    for (S_j = 1; S_j < SUMMATION_COUNT; S_j++)		\
	      {							\
		double S_y = SUMMATION_ELEMENT (S_j) - S_c;	\
		double S_t = S + S_y;				\
		S_c = (S_t - S) - S_y;				\
		S = S_t;					\
	      }							\
	  }							\
	while (0)


/* Vectors. */

/* Allocate a new vector of length N. */
struct vector *
vec_alloc (int n)
{
  struct vector *vec = xmalloc (sizeof *vec);
  vec->data = xmalloc (sizeof *vec->data * n);
  vec->n = vec->m = n;
  return vec;
}

/* Change the length of VEC to N.  The amount of space allocated will
   not be lowered, but may be enlarged. */
void
vec_realloc (struct vector *vec, int n)
{
  if (n < vec->m)
    {
      vec->m = n;
      vec->data = xrealloc (vec->data, sizeof *vec->data * n);
    }
  vec->n = n;
}

/* Free vector VEC. */
void
vec_free (struct vector *vec)
{
  free (vec->data);
  free (vec);
}

/* Set the values in vector VEC to constant VALUE. */
#if 0
void
vec_init (struct vector *vec, double value)
{
  double *p;
  int i;

  p = vec->data;
  for (i = 0; i < vec->n; i++)
    *p++ = value;
}
#endif

/* Print out vector VEC to stdout for debugging purposes. */
#if GLOBAL_DEBUGGING
#include <stdio.h>
#include "settings.h"

void
vec_print (const struct vector *vec)
{
  int i;

  for (i = 0; i < vec->n; i++)
    {
      if (i % ((get_viewwidth() - 4) / 8) == 0)
	{
	  if (i)
	    putchar ('\n');
	  printf ("%3d:", i);
	}
      
      printf ("%8g", vec_elem (vec, i));
    }
}
#endif

/* Return the sum of the values in VEC. */
double
vec_total (const struct vector *vec)
{
  double sum;

#define SUMMATION_COUNT (vec->n)
#define SUMMATION_ELEMENT(INDEX) (vec_elem (vec, (INDEX)))
  KAHAN_SUMMATION_FORMULA (sum);
#undef SUMMATION_COUNT
#undef SUMMATION_ELEMENT

  return sum;
}

/* Matrices. */

/* Allocate a new matrix with NR rows and NC columns. */
struct matrix *
mat_alloc (int nr, int nc)
{
  struct matrix *mat = xmalloc (sizeof *mat);
  mat->nr = nr;
  mat->nc = nc;
  mat->m = nr * nc;
  mat->data = xmalloc (sizeof *mat->data * nr * nc);
  return mat;
}

/* Set the size of matrix MAT to NR rows and NC columns.  The matrix
   data array will be enlarged if necessary but will not be shrunk. */
void
mat_realloc (struct matrix *mat, int nr, int nc)
{
  if (nc * nr > mat->m)
    {
      mat->m = nc * nr;
      mat->data = xrealloc (mat->data, sizeof *mat->data * mat->m);
    }
  mat->nr = nr;
  mat->nc = nc;
}

/* Free matrix MAT. */
void
mat_free (struct matrix *mat)
{
  free (mat->data);
  free (mat);
}

/* Set all matrix MAT entries to VALUE. */
void
mat_init (struct matrix *mat, double value)
{
  double *p;
  int i;

  p = mat->data;
  for (i = 0; i < mat->nr * mat->nc; i++)
    *p++ = value;
}

/* Set all MAT entries in row R to VALUE. */
void
mat_init_row (struct matrix *mat, int r, double value)
{
  double *p;
  int i;

  p = &mat_elem (mat, r, 0);
  for (i = 0; i < mat->nc; i++)
    *p++ = value;
}

/* Set all MAT entries in column C to VALUE. */
void
mat_init_col (struct matrix *mat, int c, double value)
{
  double *p;
  int i;

  p = &mat_elem (mat, 0, c);
  for (i = 0; i < mat->nr; i++)
    {
      *p = value;
      p += mat->nc;
    }
}

/* Print out MAT entries to stdout, optionally with row and column
   labels ROW_LABELS and COL_LABELS. */
#if GLOBAL_DEBUGGING
void
mat_print (const struct matrix *mat,
	   const struct vector *row_labels,
	   const struct vector *col_labels)
{
  int r, c;
  
  assert (!row_labels || row_labels->n == mat->nr);
  if (col_labels)
    {
      int c;
      
      assert (col_labels->n == mat->nc);
      if (row_labels)
	printf ("        ");
      for (c = 0; c < mat->nc; c++)
	printf ("%8g", vec_elem (col_labels, c));
    }

  for (r = 0; r < mat->nr; r++)
    {
      if (row_labels)
	printf ("%8g:", vec_elem (row_labels, r));
      for (c = 0; c < mat->nc; c++)
	printf ("%8g", mat_elem (mat, r, c));
      putchar ('\n');
    }
}
#endif /* GLOBAL_DEBUGGING */

/* Calculate row totals for matrix MAT into vector ROW_TOTS. */
void
mat_row_totals (const struct matrix *mat, struct vector *row_tots)
{
  int r;
  
  vec_realloc (row_tots, mat->nr);
  for (r = 0; r < mat->nr; r++)
    {
      double sum;

#define SUMMATION_COUNT (mat->nc)
#define SUMMATION_ELEMENT(INDEX) (mat_elem (mat, r, INDEX))
      KAHAN_SUMMATION_FORMULA (sum);
#undef SUMMATION_COUNT
#undef SUMMATION_ELEMENT

      vec_elem (row_tots, r) = sum;
    }
}

/* Calculate column totals for matrix MAT into vector COL_TOTS. */
void
mat_col_totals (const struct matrix *mat, struct vector *col_tots)
{
  int c;
  
  vec_realloc (col_tots, mat->nc);
  for (c = 0; c < mat->nc; c++)
    {
      double sum;

#define SUMMATION_COUNT (mat->nr)
#define SUMMATION_ELEMENT(INDEX) (mat_elem (mat, INDEX, c))
      KAHAN_SUMMATION_FORMULA (sum);
#undef SUMMATION_COUNT
#undef SUMMATION_ELEMENT

      vec_elem (col_tots, c) = sum;
    }
}

/* Return the grand total for matrix MAT.  Of course, if you're also
   calculating column or row totals, it would be faster to use
   vec_total on one of those sets of totals. */
double
mat_grand_total (const struct matrix *mat)
{
  double sum;

#define SUMMATION_COUNT (mat->nr * mat->nc)
#define SUMMATION_ELEMENT(INDEX) (mat->data[INDEX])
  KAHAN_SUMMATION_FORMULA (sum);
#undef SUMMATION_COUNT
#undef SUMMATION_ELEMENT

  return sum;
}
