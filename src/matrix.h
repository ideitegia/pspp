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

#if !matrix_h
#define matrix_h 1

/* Vector representation. */
struct vector
  {
    int n;
    int m;
    double *data;
  };

/* Allocate vectors. */
struct vector *vec_alloc (int n);
void vec_realloc (struct vector *, int n);
void vec_free (struct vector *);

/* Vector elements. */
#define vec_elem(VEC, INDEX) ((VEC)->data[INDEX])

/* Set the vector to a constant value. */
void vec_init (struct vector *, double);

/* Print out the vector to stdout. */
#if GLOBAL_DEBUGGING
void vec_print (const struct vector *);
#endif

/* Sum the vector values. */
double vec_total (const struct vector *);

/* Matrix representation. */
struct matrix
  {
    int nr, nc;
    int m;
    double *data;
  };

/* Allocate matrices. */
struct matrix *mat_alloc (int nr, int nc);
void mat_realloc (struct matrix *, int nr, int nc);
void mat_free (struct matrix *);

/* Matrix elements. */
#define mat_elem(MAT, R, C) ((MAT)->data[(C) + (R) * (MAT)->nc])

/* Set matrix values to a constant. */
void mat_init (struct matrix *, double);
void mat_init_row (struct matrix *, int r, double);
void mat_init_col (struct matrix *, int c, double);

/* Print out the matrix values to stdout, optionally with row and
   column labels (for debugging purposes). */
#if GLOBAL_DEBUGGING
void mat_print (const struct matrix *,
		const struct vector *row_labels, const struct vector *col_labels);
#endif

/* Sum matrix values. */
void mat_row_totals (const struct matrix *, struct vector *row_tots);
void mat_col_totals (const struct matrix *, struct vector *col_tots);
double mat_grand_total (const struct matrix *);

/* Chi-square statistics. */
enum
  {
    CHISQ_PEARSON,
    CHISQ_LIKELIHOOD_RATIO,
    CHISQ_FISHER,
    CHISQ_CC,
    CHISQ_LINEAR,
    N_CHISQ
  };

void mat_chisq (const struct matrix *, double chisq[N_CHISQ], int df[N_CHISQ]);

#endif /* matrix_h */
