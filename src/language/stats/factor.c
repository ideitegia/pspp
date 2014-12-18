/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include <gsl/gsl_vector.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_eigen.h> 
#include <gsl/gsl_blas.h> 
#include <gsl/gsl_sort_vector.h>
#include <gsl/gsl_cdf.h>

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/subcase.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/correlation.h"
#include "math/covariance.h"
#include "math/moments.h"
#include "output/chart-item.h"
#include "output/charts/scree.h"
#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum method
  {
    METHOD_CORR,
    METHOD_COV
  };

enum missing_type
  {
    MISS_LISTWISE,
    MISS_PAIRWISE,
    MISS_MEANSUB,
  };

enum extraction_method
  {
    EXTRACTION_PC,
    EXTRACTION_PAF,
  };

enum plot_opts
  {
    PLOT_SCREE = 0x0001,
    PLOT_ROTATION = 0x0002
  };

enum print_opts
  {
    PRINT_UNIVARIATE  = 0x0001,
    PRINT_DETERMINANT = 0x0002,
    PRINT_INV         = 0x0004,
    PRINT_AIC         = 0x0008,
    PRINT_SIG         = 0x0010,
    PRINT_COVARIANCE  = 0x0020,
    PRINT_CORRELATION = 0x0040,
    PRINT_ROTATION    = 0x0080,
    PRINT_EXTRACTION  = 0x0100,
    PRINT_INITIAL     = 0x0200,
    PRINT_KMO         = 0x0400,
    PRINT_REPR        = 0x0800, 
    PRINT_FSCORE      = 0x1000
  };

enum rotation_type
  {
    ROT_VARIMAX = 0,
    ROT_EQUAMAX,
    ROT_QUARTIMAX,
    ROT_PROMAX,
    ROT_NONE
  };

typedef void (*rotation_coefficients) (double *x, double *y,
				    double a, double b, double c, double d,
				    const gsl_matrix *loadings );


static void
varimax_coefficients (double *x, double *y,
		      double a, double b, double c, double d,
		      const gsl_matrix *loadings )
{
  *x = d - 2 * a * b / loadings->size1;
  *y = c - (a * a - b * b) / loadings->size1;
}

static void
equamax_coefficients (double *x, double *y,
		      double a, double b, double c, double d,
		      const gsl_matrix *loadings )
{
  *x = d - loadings->size2 * a * b / loadings->size1;
  *y = c - loadings->size2 * (a * a - b * b) / (2 * loadings->size1);
}

static void
quartimax_coefficients (double *x, double *y,
		      double a UNUSED, double b UNUSED, double c, double d,
		      const gsl_matrix *loadings UNUSED)
{
  *x = d ;
  *y = c ;
}

static const rotation_coefficients rotation_coeff[] = {
  varimax_coefficients,
  equamax_coefficients,
  quartimax_coefficients,
  varimax_coefficients  /* PROMAX is identical to VARIMAX */
};


/* return diag (C'C) ^ {-0.5} */
static gsl_matrix *
diag_rcp_sqrt (const gsl_matrix *C) 
{
  int j;
  gsl_matrix *d =  gsl_matrix_calloc (C->size1, C->size2);
  gsl_matrix *r =  gsl_matrix_calloc (C->size1, C->size2);

  assert (C->size1 == C->size2);

  gsl_linalg_matmult_mod (C,  GSL_LINALG_MOD_TRANSPOSE,
			  C,  GSL_LINALG_MOD_NONE,
			  d);

  for (j = 0 ; j < d->size2; ++j)
    {
      double e = gsl_matrix_get (d, j, j);
      e = 1.0 / sqrt (e);
      gsl_matrix_set (r, j, j, e);
    }

  gsl_matrix_free (d);

  return r;
}



/* return diag ((C'C)^-1) ^ {-0.5} */
static gsl_matrix *
diag_rcp_inv_sqrt (const gsl_matrix *CCinv) 
{
  int j;
  gsl_matrix *r =  gsl_matrix_calloc (CCinv->size1, CCinv->size2);

  assert (CCinv->size1 == CCinv->size2);

  for (j = 0 ; j < CCinv->size2; ++j)
    {
      double e = gsl_matrix_get (CCinv, j, j);
      e = 1.0 / sqrt (e);
      gsl_matrix_set (r, j, j, e);
    }

  return r;
}





struct cmd_factor 
{
  size_t n_vars;
  const struct variable **vars;

  const struct variable *wv;

  enum method method;
  enum missing_type missing_type;
  enum mv_class exclude;
  enum print_opts print;
  enum extraction_method extraction;
  enum plot_opts plot;
  enum rotation_type rotation;
  int rotation_iterations;
  int promax_power;

  /* Extraction Criteria */
  int n_factors;
  double min_eigen;
  double econverge;
  int extraction_iterations;

  double rconverge;

  /* Format */
  double blank;
  bool sort;
};

struct idata
{
  /* Intermediate values used in calculation */

  const gsl_matrix *corr ;  /* The correlation matrix */
  gsl_matrix *cov ;         /* The covariance matrix */
  const gsl_matrix *n ;     /* Matrix of number of samples */

  gsl_vector *eval ;  /* The eigenvalues */
  gsl_matrix *evec ;  /* The eigenvectors */

  int n_extractions;

  gsl_vector *msr ;  /* Multiple Squared Regressions */

  double detR;  /* The determinant of the correlation matrix */
};

static struct idata *
idata_alloc (size_t n_vars)
{
  struct idata *id = xzalloc (sizeof (*id));

  id->n_extractions = 0;
  id->msr = gsl_vector_alloc (n_vars);

  id->eval = gsl_vector_alloc (n_vars);
  id->evec = gsl_matrix_alloc (n_vars, n_vars);

  return id;
}

static void
idata_free (struct idata *id)
{
  gsl_vector_free (id->msr);
  gsl_vector_free (id->eval);
  gsl_matrix_free (id->evec);
  if (id->cov != NULL)
    gsl_matrix_free (id->cov);
  if (id->corr != NULL)
    gsl_matrix_free (CONST_CAST (gsl_matrix *, id->corr));

  free (id);
}


static gsl_matrix *
anti_image (const gsl_matrix *m)
{
  int i, j;
  gsl_matrix *a;
  assert (m->size1 == m->size2);

  a = gsl_matrix_alloc (m->size1, m->size2);
  
  for (i = 0; i < m->size1; ++i)
    {
      for (j = 0; j < m->size2; ++j)
	{
	  double *p = gsl_matrix_ptr (a, i, j);
	  *p = gsl_matrix_get (m, i, j);
	  *p /= gsl_matrix_get (m, i, i);
	  *p /= gsl_matrix_get (m, j, j);
	}
    }

  return a;
}


/* Return the sum of all the elements excluding row N */
static double
ssq_od_n (const gsl_matrix *m, int n)
{
  int i, j;
  double ss = 0;
  assert (m->size1 == m->size2);

  assert (n < m->size1);
  
  for (i = 0; i < m->size1; ++i)
    {
      if (i == n ) continue;
      for (j = 0; j < m->size2; ++j)
	{
	  ss += pow2 (gsl_matrix_get (m, i, j));
	}
    }

  return ss;
}



#if 1
static void
dump_matrix (const gsl_matrix *m)
{
  size_t i, j;

  for (i = 0 ; i < m->size1; ++i)
    {
      for (j = 0 ; j < m->size2; ++j)
	printf ("%02f ", gsl_matrix_get (m, i, j));
      printf ("\n");
    }
}

static void
dump_matrix_permute (const gsl_matrix *m, const gsl_permutation *p)
{
  size_t i, j;

  for (i = 0 ; i < m->size1; ++i)
    {
      for (j = 0 ; j < m->size2; ++j)
	printf ("%02f ", gsl_matrix_get (m, gsl_permutation_get (p, i), j));
      printf ("\n");
    }
}


static void
dump_vector (const gsl_vector *v)
{
  size_t i;
  for (i = 0 ; i < v->size; ++i)
    {
      printf ("%02f\n", gsl_vector_get (v, i));
    }
  printf ("\n");
}
#endif


static int 
n_extracted_factors (const struct cmd_factor *factor, struct idata *idata)
{
  int i;
  
  /* If there is a cached value, then return that. */
  if ( idata->n_extractions != 0)
    return idata->n_extractions;

  /* Otherwise, if the number of factors has been explicitly requested,
     use that. */
  if (factor->n_factors > 0)
    {
      idata->n_extractions = factor->n_factors;
      goto finish;
    }
  
  /* Use the MIN_EIGEN setting. */
  for (i = 0 ; i < idata->eval->size; ++i)
    {
      double evali = fabs (gsl_vector_get (idata->eval, i));

      idata->n_extractions = i;

      if (evali < factor->min_eigen)
	goto finish;
    }

 finish:
  return idata->n_extractions;
}


/* Returns a newly allocated matrix identical to M.
   It it the callers responsibility to free the returned value.
*/
static gsl_matrix *
matrix_dup (const gsl_matrix *m)
{
  gsl_matrix *n =  gsl_matrix_alloc (m->size1, m->size2);

  gsl_matrix_memcpy (n, m);

  return n;
}


struct smr_workspace
{
  /* Copy of the subject */
  gsl_matrix *m;
  
  gsl_matrix *inverse;

  gsl_permutation *perm;

  gsl_matrix *result1;
  gsl_matrix *result2;
};


static struct smr_workspace *ws_create (const gsl_matrix *input)
{
  struct smr_workspace *ws = xmalloc (sizeof (*ws));
  
  ws->m = gsl_matrix_alloc (input->size1, input->size2);
  ws->inverse = gsl_matrix_calloc (input->size1 - 1, input->size2 - 1);
  ws->perm = gsl_permutation_alloc (input->size1 - 1);
  ws->result1 = gsl_matrix_calloc (input->size1 - 1, 1);
  ws->result2 = gsl_matrix_calloc (1, 1);

  return ws;
}

static void
ws_destroy (struct smr_workspace *ws)
{
  gsl_matrix_free (ws->result2);
  gsl_matrix_free (ws->result1);
  gsl_permutation_free (ws->perm);
  gsl_matrix_free (ws->inverse);
  gsl_matrix_free (ws->m);

  free (ws);
}


/* 
   Return the square of the regression coefficient for VAR regressed against all other variables.
 */
static double
squared_multiple_correlation (const gsl_matrix *corr, int var, struct smr_workspace *ws)
{
  /* For an explanation of what this is doing, see 
     http://www.visualstatistics.net/Visual%20Statistics%20Multimedia/multiple_regression_analysis.htm
  */

  int signum = 0;
  gsl_matrix_view rxx;

  gsl_matrix_memcpy (ws->m, corr);

  gsl_matrix_swap_rows (ws->m, 0, var);
  gsl_matrix_swap_columns (ws->m, 0, var);

  rxx = gsl_matrix_submatrix (ws->m, 1, 1, ws->m->size1 - 1, ws->m->size1 - 1); 

  gsl_linalg_LU_decomp (&rxx.matrix, ws->perm, &signum);

  gsl_linalg_LU_invert (&rxx.matrix, ws->perm, ws->inverse);

  {
    gsl_matrix_const_view rxy = gsl_matrix_const_submatrix (ws->m, 1, 0, ws->m->size1 - 1, 1);
    gsl_matrix_const_view ryx = gsl_matrix_const_submatrix (ws->m, 0, 1, 1, ws->m->size1 - 1);

    gsl_blas_dgemm (CblasNoTrans,  CblasNoTrans,
		    1.0, ws->inverse, &rxy.matrix, 0.0, ws->result1);

    gsl_blas_dgemm (CblasNoTrans,  CblasNoTrans,
		    1.0, &ryx.matrix, ws->result1, 0.0, ws->result2);
  }

  return gsl_matrix_get (ws->result2, 0, 0);
}



static double the_communality (const gsl_matrix *evec, const gsl_vector *eval, int n, int n_factors);


struct factor_matrix_workspace
{
  size_t n_factors;
  gsl_eigen_symmv_workspace *eigen_ws;

  gsl_vector *eval ;
  gsl_matrix *evec ;

  gsl_matrix *gamma ;

  gsl_matrix *r;
};

static struct factor_matrix_workspace *
factor_matrix_workspace_alloc (size_t n, size_t nf)
{
  struct factor_matrix_workspace *ws = xmalloc (sizeof (*ws));

  ws->n_factors = nf;
  ws->gamma = gsl_matrix_calloc (nf, nf);
  ws->eigen_ws = gsl_eigen_symmv_alloc (n);
  ws->eval = gsl_vector_alloc (n);
  ws->evec = gsl_matrix_alloc (n, n);
  ws->r  = gsl_matrix_alloc (n, n);
  
  return ws;
}

static void
factor_matrix_workspace_free (struct factor_matrix_workspace *ws)
{
  gsl_eigen_symmv_free (ws->eigen_ws);
  gsl_vector_free (ws->eval);
  gsl_matrix_free (ws->evec);
  gsl_matrix_free (ws->gamma);
  gsl_matrix_free (ws->r);
  free (ws);
}

/*
  Shift P left by OFFSET places, and overwrite TARGET
  with the shifted result.
  Positions in TARGET less than OFFSET are unchanged.
*/
static void
perm_shift_apply (gsl_permutation *target, const gsl_permutation *p,
		  size_t offset)
{
  size_t i;
  assert (target->size == p->size);
  assert (offset <= target->size);

  for (i = 0; i < target->size - offset; ++i)
    {
      target->data[i] = p->data [i + offset];
    }
}


/* 
   Indirectly sort the rows of matrix INPUT, storing the sort order in PERM.
   The sort criteria are as follows:
   
   Rows are sorted on the first column, until the absolute value of an
   element in a subsequent column  is greater than that of the first
   column.  Thereafter, rows will be sorted on the second column,
   until the absolute value of an element in a subsequent column
   exceeds that of the second column ...
*/
static void
sort_matrix_indirect (const gsl_matrix *input, gsl_permutation *perm)
{
  const size_t n = perm->size;
  const size_t m = input->size2;
  int i, j;
  gsl_matrix *mat ;
  int column_n = 0;
  int row_n = 0;
  gsl_permutation *p;

  assert (perm->size == input->size1);

  p = gsl_permutation_alloc (n);

  /* Copy INPUT into MAT, discarding the sign */
  mat = gsl_matrix_alloc (n, m);
  for (i = 0 ; i < mat->size1; ++i)
    {
      for (j = 0 ; j < mat->size2; ++j)
	{
	  double x = gsl_matrix_get (input, i, j);
	  gsl_matrix_set (mat, i, j, fabs (x));
	}
    }

  while (column_n < m && row_n < n) 
    {
      gsl_vector_const_view columni = gsl_matrix_const_column (mat, column_n);
      gsl_sort_vector_index (p, &columni.vector);

      for (i = 0 ; i < n; ++i)
	{
	  gsl_vector_view row = gsl_matrix_row (mat, p->data[n - 1 - i]);
	  size_t maxindex = gsl_vector_max_index (&row.vector);
	  
	  if ( maxindex > column_n )
	    break;

	  /* All subsequent elements of this row, are of no interest.
	     So set them all to a highly negative value */
	  for (j = column_n + 1; j < row.vector.size ; ++j)
	    gsl_vector_set (&row.vector, j, -DBL_MAX);
	}

      perm_shift_apply (perm, p, row_n);
      row_n += i;

      column_n++;
    }

  gsl_permutation_free (p);
  gsl_matrix_free (mat);
  
  assert ( 0 == gsl_permutation_valid (perm));

  /* We want the biggest value to be first */
  gsl_permutation_reverse (perm);    
}


static void
drot_go (double phi, double *l0, double *l1)
{
  double r0 = cos (phi) * *l0 + sin (phi) * *l1;
  double r1 = - sin (phi) * *l0 + cos (phi) * *l1;

  *l0 = r0;
  *l1 = r1;
}


static gsl_matrix *
clone_matrix (const gsl_matrix *m)
{
  int j, k;
  gsl_matrix *c = gsl_matrix_calloc (m->size1, m->size2);

  for (j = 0 ; j < c->size1; ++j)
    {
      for (k = 0 ; k < c->size2; ++k)
	{
	  const double *v = gsl_matrix_const_ptr (m, j, k);
	  gsl_matrix_set (c, j, k, *v);
	}
    }

  return c;
}


static double 
initial_sv (const gsl_matrix *fm)
{
  int j, k;

  double sv = 0.0;
  for (j = 0 ; j < fm->size2; ++j)
    {
      double l4s = 0;
      double l2s = 0;

      for (k = j + 1 ; k < fm->size2; ++k)
	{
	  double lambda = gsl_matrix_get (fm, k, j);
	  double lambda_sq = lambda * lambda;
	  double lambda_4 = lambda_sq * lambda_sq;

	  l4s += lambda_4;
	  l2s += lambda_sq;
	}
      sv += ( fm->size1 * l4s - (l2s * l2s) ) / (fm->size1 * fm->size1 );
    }
  return sv;
}

static void
rotate (const struct cmd_factor *cf, const gsl_matrix *unrot,
	const gsl_vector *communalities,
	gsl_matrix *result,
	gsl_vector *rotated_loadings,
	gsl_matrix *pattern_matrix,
	gsl_matrix *factor_correlation_matrix
	)
{
  int j, k;
  int i;
  double prev_sv;

  /* First get a normalised version of UNROT */
  gsl_matrix *normalised = gsl_matrix_calloc (unrot->size1, unrot->size2);
  gsl_matrix *h_sqrt = gsl_matrix_calloc (communalities->size, communalities->size);
  gsl_matrix *h_sqrt_inv ;

  /* H is the diagonal matrix containing the absolute values of the communalities */
  for (i = 0 ; i < communalities->size ; ++i)
    {
      double *ptr = gsl_matrix_ptr (h_sqrt, i, i);
      *ptr = fabs (gsl_vector_get (communalities, i));
    }

  /* Take the square root of the communalities */
  gsl_linalg_cholesky_decomp (h_sqrt);


  /* Save a copy of h_sqrt and invert it */
  h_sqrt_inv = clone_matrix (h_sqrt);
  gsl_linalg_cholesky_decomp (h_sqrt_inv);
  gsl_linalg_cholesky_invert (h_sqrt_inv);

  /* normalised vertion is H^{1/2} x UNROT */
  gsl_blas_dgemm (CblasNoTrans,  CblasNoTrans, 1.0, h_sqrt_inv, unrot, 0.0, normalised);

  gsl_matrix_free (h_sqrt_inv);


  /* Now perform the rotation iterations */

  prev_sv = initial_sv (normalised);
  for (i = 0 ; i < cf->rotation_iterations ; ++i)
    {
      double sv = 0.0;
      for (j = 0 ; j < normalised->size2; ++j)
	{
	  /* These variables relate to the convergence criterium */
	  double l4s = 0;
	  double l2s = 0;

	  for (k = j + 1 ; k < normalised->size2; ++k)
	    {
	      int p;
	      double a = 0.0;
	      double b = 0.0;
	      double c = 0.0;
	      double d = 0.0;
	      double x, y;
	      double phi;

	      for (p = 0; p < normalised->size1; ++p)
		{
		  double jv = gsl_matrix_get (normalised, p, j);
		  double kv = gsl_matrix_get (normalised, p, k);
	      
		  double u = jv * jv - kv * kv;
		  double v = 2 * jv * kv;
		  a += u;
		  b += v;
		  c +=  u * u - v * v;
		  d += 2 * u * v;
		}

	      rotation_coeff [cf->rotation] (&x, &y, a, b, c, d, normalised);

	      phi = atan2 (x,  y) / 4.0 ;

	      /* Don't bother rotating if the angle is small */
	      if ( fabs (sin (phi) ) <= pow (10.0, -15.0))
		  continue;

	      for (p = 0; p < normalised->size1; ++p)
		{
		  double *lambda0 = gsl_matrix_ptr (normalised, p, j);
		  double *lambda1 = gsl_matrix_ptr (normalised, p, k);
		  drot_go (phi, lambda0, lambda1);
		}

	      /* Calculate the convergence criterium */
	      {
		double lambda = gsl_matrix_get (normalised, k, j);
		double lambda_sq = lambda * lambda;
		double lambda_4 = lambda_sq * lambda_sq;

		l4s += lambda_4;
		l2s += lambda_sq;
	      }
	    }
	  sv += ( normalised->size1 * l4s - (l2s * l2s) ) / (normalised->size1 * normalised->size1 );
	}

      if ( fabs (sv - prev_sv) <= cf->rconverge)
	break;

      prev_sv = sv;
    }

  gsl_blas_dgemm (CblasNoTrans,  CblasNoTrans, 1.0,
		  h_sqrt, normalised,  0.0,   result);

  gsl_matrix_free (h_sqrt);
  gsl_matrix_free (normalised);

  if (cf->rotation == ROT_PROMAX) 
    {
      /* general purpose m by m matrix, where m is the number of factors */
      gsl_matrix *mm1 =  gsl_matrix_calloc (unrot->size2, unrot->size2); 
      gsl_matrix *mm2 =  gsl_matrix_calloc (unrot->size2, unrot->size2);

      /* general purpose m by p matrix, where p is the number of variables */
      gsl_matrix *mp1 =  gsl_matrix_calloc (unrot->size2, unrot->size1);

      gsl_matrix *pm1 =  gsl_matrix_calloc (unrot->size1, unrot->size2);

      gsl_permutation *perm = gsl_permutation_alloc (unrot->size2);

      int signum;

      int i, j;

      /* The following variables follow the notation by SPSS Statistical Algorithms
	 page 342 */
      gsl_matrix *L =  gsl_matrix_calloc (unrot->size2, unrot->size2);
      gsl_matrix *P = clone_matrix (result);
      gsl_matrix *D ;
      gsl_matrix *Q ;


      /* Vector of length p containing (indexed by i)
	 \Sum^m_j {\lambda^2_{ij}} */
      gsl_vector *rssq = gsl_vector_calloc (unrot->size1); 

      for (i = 0; i < P->size1; ++i)
	{
	  double sum = 0;
	  for (j = 0; j < P->size2; ++j)
	    {
	      sum += gsl_matrix_get (result, i, j)
		* gsl_matrix_get (result, i, j);
		    
	    }
		
	  gsl_vector_set (rssq, i, sqrt (sum));
	}

      for (i = 0; i < P->size1; ++i)
	{
	  for (j = 0; j < P->size2; ++j)
	    {
	      double l = gsl_matrix_get (result, i, j);
	      double r = gsl_vector_get (rssq, i);
	      gsl_matrix_set (P, i, j, pow (fabs (l / r), cf->promax_power + 1) * r / l);
	    }
	}

      gsl_vector_free (rssq);

      gsl_linalg_matmult_mod (result,
			      GSL_LINALG_MOD_TRANSPOSE,
			      result,
			      GSL_LINALG_MOD_NONE,
			      mm1);

      gsl_linalg_LU_decomp (mm1, perm, &signum);
      gsl_linalg_LU_invert (mm1, perm, mm2);

      gsl_linalg_matmult_mod (mm2,   GSL_LINALG_MOD_NONE,
			      result,  GSL_LINALG_MOD_TRANSPOSE,
			      mp1);

      gsl_linalg_matmult_mod (mp1, GSL_LINALG_MOD_NONE,
			      P,   GSL_LINALG_MOD_NONE,
			      L);

      D = diag_rcp_sqrt (L);
      Q = gsl_matrix_calloc (unrot->size2, unrot->size2);

      gsl_linalg_matmult_mod (L, GSL_LINALG_MOD_NONE,
			      D, GSL_LINALG_MOD_NONE,
			      Q);

      gsl_matrix *QQinv = gsl_matrix_calloc (unrot->size2, unrot->size2);

      gsl_linalg_matmult_mod (Q, GSL_LINALG_MOD_TRANSPOSE,
			      Q,  GSL_LINALG_MOD_NONE,
			      QQinv);

      gsl_linalg_cholesky_decomp (QQinv);
      gsl_linalg_cholesky_invert (QQinv);


      gsl_matrix *C = diag_rcp_inv_sqrt (QQinv);
      gsl_matrix *Cinv =  clone_matrix (C);

      gsl_linalg_cholesky_decomp (Cinv);
      gsl_linalg_cholesky_invert (Cinv);


      gsl_linalg_matmult_mod (result, GSL_LINALG_MOD_NONE,
			      Q,      GSL_LINALG_MOD_NONE,
			      pm1);

      gsl_linalg_matmult_mod (pm1,      GSL_LINALG_MOD_NONE,
			      Cinv,         GSL_LINALG_MOD_NONE,
			      pattern_matrix);


      gsl_linalg_matmult_mod (C,      GSL_LINALG_MOD_NONE,
			      QQinv,  GSL_LINALG_MOD_NONE,
			      mm1);

      gsl_linalg_matmult_mod (mm1,      GSL_LINALG_MOD_NONE,
			      C,  GSL_LINALG_MOD_TRANSPOSE,
			      factor_correlation_matrix);
      
      gsl_linalg_matmult_mod (pattern_matrix,      GSL_LINALG_MOD_NONE,
			      factor_correlation_matrix,  GSL_LINALG_MOD_NONE,
			      pm1);

      gsl_matrix_memcpy (result, pm1);
    }


  /* reflect negative sums and populate the rotated loadings vector*/
  for (i = 0 ; i < result->size2; ++i)
    {
      double ssq = 0.0;
      double sum = 0.0;
      for (j = 0 ; j < result->size1; ++j)
	{
	  double s = gsl_matrix_get (result, j, i);
	  ssq += s * s;
	  sum += s;
	}

      gsl_vector_set (rotated_loadings, i, ssq);

      if ( sum < 0 )
	for (j = 0 ; j < result->size1; ++j)
	  {
	    double *lambda = gsl_matrix_ptr (result, j, i);
	    *lambda = - *lambda;
	  }
    }
}


/*
  Get an approximation for the factor matrix into FACTORS, and the communalities into COMMUNALITIES.
  R is the matrix to be analysed.
  WS is a pointer to a structure which must have been initialised with factor_matrix_workspace_init.
 */
static void
iterate_factor_matrix (const gsl_matrix *r, gsl_vector *communalities, gsl_matrix *factors, 
		       struct factor_matrix_workspace *ws)
{
  size_t i;
  gsl_matrix_view mv ;

  assert (r->size1 == r->size2);
  assert (r->size1 == communalities->size);

  assert (factors->size1 == r->size1);
  assert (factors->size2 == ws->n_factors);

  gsl_matrix_memcpy (ws->r, r);

  /* Apply Communalities to diagonal of correlation matrix */
  for (i = 0 ; i < communalities->size ; ++i)
    {
      double *x = gsl_matrix_ptr (ws->r, i, i);
      *x = gsl_vector_get (communalities, i);
    }

  gsl_eigen_symmv (ws->r, ws->eval, ws->evec, ws->eigen_ws);

  mv = gsl_matrix_submatrix (ws->evec, 0, 0, ws->evec->size1, ws->n_factors);

  /* Gamma is the diagonal matrix containing the absolute values of the eigenvalues */
  for (i = 0 ; i < ws->n_factors ; ++i)
    {
      double *ptr = gsl_matrix_ptr (ws->gamma, i, i);
      *ptr = fabs (gsl_vector_get (ws->eval, i));
    }

  /* Take the square root of gamma */
  gsl_linalg_cholesky_decomp (ws->gamma);

  gsl_blas_dgemm (CblasNoTrans,  CblasNoTrans, 1.0, &mv.matrix, ws->gamma, 0.0, factors);

  for (i = 0 ; i < r->size1 ; ++i)
    {
      double h = the_communality (ws->evec, ws->eval, i, ws->n_factors);
      gsl_vector_set (communalities, i, h);
    }
}



static bool run_factor (struct dataset *ds, const struct cmd_factor *factor);


int
cmd_factor (struct lexer *lexer, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);
  int n_iterations = 25;
  struct cmd_factor factor;
  factor.n_vars = 0;
  factor.vars = NULL;
  factor.method = METHOD_CORR;
  factor.missing_type = MISS_LISTWISE;
  factor.exclude = MV_ANY;
  factor.print = PRINT_INITIAL | PRINT_EXTRACTION | PRINT_ROTATION;
  factor.extraction = EXTRACTION_PC;
  factor.n_factors = 0;
  factor.min_eigen = SYSMIS;
  factor.extraction_iterations = 25;
  factor.rotation_iterations = 25;
  factor.econverge = 0.001;

  factor.blank = 0;
  factor.sort = false;
  factor.plot = 0;
  factor.rotation = ROT_VARIMAX;

  factor.rconverge = 0.0001;

  factor.wv = dict_get_weight (dict);

  lex_match (lexer, T_SLASH);

  if (!lex_force_match_id (lexer, "VARIABLES"))
    {
      goto error;
    }

  lex_match (lexer, T_EQUALS);

  if (!parse_variables_const (lexer, dict, &factor.vars, &factor.n_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  if (factor.n_vars < 2)
    msg (MW, _("Factor analysis on a single variable is not useful."));

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "PLOT"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "EIGEN"))
		{
		  factor.plot |= PLOT_SCREE;
		}
#if FACTOR_FULLY_IMPLEMENTED
	      else if (lex_match_id (lexer, "ROTATION"))
		{
		}
#endif
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "METHOD"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "COVARIANCE"))
		{
		  factor.method = METHOD_COV;
		}
	      else if (lex_match_id (lexer, "CORRELATION"))
		{
		  factor.method = METHOD_CORR;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "ROTATION"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      /* VARIMAX and DEFAULT are defaults */
	      if (lex_match_id (lexer, "VARIMAX") || lex_match_id (lexer, "DEFAULT"))
		{
		  factor.rotation = ROT_VARIMAX;
		}
	      else if (lex_match_id (lexer, "EQUAMAX"))
		{
		  factor.rotation = ROT_EQUAMAX;
		}
	      else if (lex_match_id (lexer, "QUARTIMAX"))
		{
		  factor.rotation = ROT_QUARTIMAX;
		}
	      else if (lex_match_id (lexer, "PROMAX"))
		{
		  factor.promax_power = 5;
		  if (lex_match (lexer, T_LPAREN))
		    {
		      lex_force_int (lexer);
		      factor.promax_power = lex_integer (lexer);
		      lex_get (lexer);
		      lex_force_match (lexer, T_RPAREN);
		    }
		  factor.rotation = ROT_PROMAX;
		}
	      else if (lex_match_id (lexer, "NOROTATE"))
		{
		  factor.rotation = ROT_NONE;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
          factor.rotation_iterations = n_iterations;
	}
      else if (lex_match_id (lexer, "CRITERIA"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "FACTORS"))
		{
		  if ( lex_force_match (lexer, T_LPAREN))
		    {
		      lex_force_int (lexer);
		      factor.n_factors = lex_integer (lexer);
		      lex_get (lexer);
		      lex_force_match (lexer, T_RPAREN);
		    }
		}
	      else if (lex_match_id (lexer, "MINEIGEN"))
		{
		  if ( lex_force_match (lexer, T_LPAREN))
		    {
		      lex_force_num (lexer);
		      factor.min_eigen = lex_number (lexer);
		      lex_get (lexer);
		      lex_force_match (lexer, T_RPAREN);
		    }
		}
	      else if (lex_match_id (lexer, "ECONVERGE"))
		{
		  if ( lex_force_match (lexer, T_LPAREN))
		    {
		      lex_force_num (lexer);
		      factor.econverge = lex_number (lexer);
		      lex_get (lexer);
		      lex_force_match (lexer, T_RPAREN);
		    }
		}
	      else if (lex_match_id (lexer, "RCONVERGE"))
		{
		  if ( lex_force_match (lexer, T_LPAREN))
		    {
		      lex_force_num (lexer);
		      factor.rconverge = lex_number (lexer);
		      lex_get (lexer);
		      lex_force_match (lexer, T_RPAREN);
		    }
		}
	      else if (lex_match_id (lexer, "ITERATE"))
		{
		  if ( lex_force_match (lexer, T_LPAREN))
		    {
		      lex_force_int (lexer);
		      n_iterations = lex_integer (lexer);
		      lex_get (lexer);
		      lex_force_match (lexer, T_RPAREN);
		    }
		}
	      else if (lex_match_id (lexer, "DEFAULT"))
		{
		  factor.n_factors = 0;
		  factor.min_eigen = 1;
		  n_iterations = 25;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "EXTRACTION"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "PAF"))
		{
		  factor.extraction = EXTRACTION_PAF;
		}
	      else if (lex_match_id (lexer, "PC"))
		{
		  factor.extraction = EXTRACTION_PC;
		}
	      else if (lex_match_id (lexer, "PA1"))
		{
		  factor.extraction = EXTRACTION_PC;
		}
	      else if (lex_match_id (lexer, "DEFAULT"))
		{
		  factor.extraction = EXTRACTION_PC;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
          factor.extraction_iterations = n_iterations;
	}
      else if (lex_match_id (lexer, "FORMAT"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "SORT"))
		{
		  factor.sort = true;
		}
	      else if (lex_match_id (lexer, "BLANK"))
		{
		  if ( lex_force_match (lexer, T_LPAREN))
		    {
		      lex_force_num (lexer);
		      factor.blank = lex_number (lexer);
		      lex_get (lexer);
		      lex_force_match (lexer, T_RPAREN);
		    }
		}
	      else if (lex_match_id (lexer, "DEFAULT"))
		{
		  factor.blank = 0;
		  factor.sort = false;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "PRINT"))
	{
	  factor.print = 0;
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "UNIVARIATE"))
		{
		  factor.print |= PRINT_UNIVARIATE;
		}
	      else if (lex_match_id (lexer, "DET"))
		{
		  factor.print |= PRINT_DETERMINANT;
		}
#if FACTOR_FULLY_IMPLEMENTED
	      else if (lex_match_id (lexer, "INV"))
		{
		}
	      else if (lex_match_id (lexer, "AIC"))
		{
		}
#endif
	      else if (lex_match_id (lexer, "SIG"))
		{
		  factor.print |= PRINT_SIG;
		}
	      else if (lex_match_id (lexer, "CORRELATION"))
		{
		  factor.print |= PRINT_CORRELATION;
		}
#if FACTOR_FULLY_IMPLEMENTED
	      else if (lex_match_id (lexer, "COVARIANCE"))
		{
		}
#endif
	      else if (lex_match_id (lexer, "ROTATION"))
		{
		  factor.print |= PRINT_ROTATION;
		}
	      else if (lex_match_id (lexer, "EXTRACTION"))
		{
		  factor.print |= PRINT_EXTRACTION;
		}
	      else if (lex_match_id (lexer, "INITIAL"))
		{
		  factor.print |= PRINT_INITIAL;
		}
	      else if (lex_match_id (lexer, "KMO"))
		{
		  factor.print |= PRINT_KMO;
		}
#if FACTOR_FULLY_IMPLEMENTED
	      else if (lex_match_id (lexer, "REPR"))
		{
		}
	      else if (lex_match_id (lexer, "FSCORE"))
		{
		}
#endif
              else if (lex_match (lexer, T_ALL))
		{
		  factor.print = 0xFFFF;
		}
	      else if (lex_match_id (lexer, "DEFAULT"))
		{
		  factor.print |= PRINT_INITIAL ;
		  factor.print |= PRINT_EXTRACTION ;
		  factor.print |= PRINT_ROTATION ;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  factor.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  factor.exclude = MV_ANY;
		}
	      else if (lex_match_id (lexer, "LISTWISE"))
		{
		  factor.missing_type = MISS_LISTWISE;
		}
	      else if (lex_match_id (lexer, "PAIRWISE"))
		{
		  factor.missing_type = MISS_PAIRWISE;
		}
	      else if (lex_match_id (lexer, "MEANSUB"))
		{
		  factor.missing_type = MISS_MEANSUB;
		}
	      else
		{
                  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }

  if ( factor.rotation == ROT_NONE )
    factor.print &= ~PRINT_ROTATION;

  if ( ! run_factor (ds, &factor)) 
    goto error;

  free (factor.vars);
  return CMD_SUCCESS;

 error:
  free (factor.vars);
  return CMD_FAILURE;
}

static void do_factor (const struct cmd_factor *factor, struct casereader *group);


static bool
run_factor (struct dataset *ds, const struct cmd_factor *factor)
{
  struct dictionary *dict = dataset_dict (ds);
  bool ok;
  struct casereader *group;

  struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), dict);

  while (casegrouper_get_next_group (grouper, &group))
    {
      if ( factor->missing_type == MISS_LISTWISE )
	group  = casereader_create_filter_missing (group, factor->vars, factor->n_vars,
						   factor->exclude,
						   NULL,  NULL);
      do_factor (factor, group);
    }

  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  return ok;
}


/* Return the communality of variable N, calculated to N_FACTORS */
static double
the_communality (const gsl_matrix *evec, const gsl_vector *eval, int n, int n_factors)
{
  size_t i;

  double comm = 0;

  assert (n >= 0);
  assert (n < eval->size);
  assert (n < evec->size1);
  assert (n_factors <= eval->size);

  for (i = 0 ; i < n_factors; ++i)
    {
      double evali = fabs (gsl_vector_get (eval, i));

      double eveci = gsl_matrix_get (evec, n, i);

      comm += pow2 (eveci) * evali;
    }

  return comm;
}

/* Return the communality of variable N, calculated to N_FACTORS */
static double
communality (struct idata *idata, int n, int n_factors)
{
  return the_communality (idata->evec, idata->eval, n, n_factors);
}


static void
show_scree (const struct cmd_factor *f, struct idata *idata)
{
  struct scree *s;
  const char *label ;

  if ( !(f->plot & PLOT_SCREE) )
    return;


  label = f->extraction == EXTRACTION_PC ? _("Component Number") : _("Factor Number");

  s = scree_create (idata->eval, label);

  scree_submit (s);
}

static void
show_communalities (const struct cmd_factor * factor,
		    const gsl_vector *initial, const gsl_vector *extracted)
{
  int i;
  int c = 0;
  const int heading_columns = 1;
  int nc = heading_columns;
  const int heading_rows = 1;
  const int nr = heading_rows + factor->n_vars;
  struct tab_table *t;

  if (factor->print & PRINT_EXTRACTION)
    nc++;

  if (factor->print & PRINT_INITIAL)
    nc++;

  /* No point having a table with only headings */
  if (nc <= 1)
    return;

  t = tab_create (nc, nr);

  tab_title (t, _("Communalities"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  c = 1;
  if (factor->print & PRINT_INITIAL)
    tab_text (t, c++, 0, TAB_CENTER | TAT_TITLE, _("Initial"));

  if (factor->print & PRINT_EXTRACTION)
    tab_text (t, c++, 0, TAB_CENTER | TAT_TITLE, _("Extraction"));

  /* Outline the box */
  tab_box (t,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   nc - 1, nr - 1);

  /* Vertical lines */
  tab_box (t,
	   -1, -1,
	   -1, TAL_1,
	   heading_columns, 0,
	   nc - 1, nr - 1);

  tab_hline (t, TAL_1, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);

  for (i = 0 ; i < factor->n_vars; ++i)
    {
      c = 0;
      tab_text (t, c++, i + heading_rows, TAT_TITLE, var_to_string (factor->vars[i]));

      if (factor->print & PRINT_INITIAL)
	tab_double (t, c++, i + heading_rows, 0, gsl_vector_get (initial, i), NULL, RC_OTHER);

      if (factor->print & PRINT_EXTRACTION)
	tab_double (t, c++, i + heading_rows, 0, gsl_vector_get (extracted, i), NULL, RC_OTHER);
    }

  tab_submit (t);
}


static void
show_factor_matrix (const struct cmd_factor *factor, struct idata *idata, const char *title, const gsl_matrix *fm)
{
  int i;

  const int n_factors = idata->n_extractions;

  const int heading_columns = 1;
  const int heading_rows = 2;
  const int nr = heading_rows + factor->n_vars;
  const int nc = heading_columns + n_factors;
  gsl_permutation *perm;

  struct tab_table *t = tab_create (nc, nr);

  /* 
  if ( factor->extraction == EXTRACTION_PC )
    tab_title (t, _("Component Matrix"));
  else 
    tab_title (t, _("Factor Matrix"));
  */

  tab_title (t, "%s", title);

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  if ( factor->extraction == EXTRACTION_PC )
    tab_joint_text (t,
		    1, 0,
		    nc - 1, 0,
		    TAB_CENTER | TAT_TITLE, _("Component"));
  else
    tab_joint_text (t,
		    1, 0,
		    nc - 1, 0,
		    TAB_CENTER | TAT_TITLE, _("Factor"));


  tab_hline (t, TAL_1, heading_columns, nc - 1, 1);


  /* Outline the box */
  tab_box (t,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   nc - 1, nr - 1);

  /* Vertical lines */
  tab_box (t,
	   -1, -1,
	   -1, TAL_1,
	   heading_columns, 1,
	   nc - 1, nr - 1);

  tab_hline (t, TAL_1, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);


  /* Initialise to the identity permutation */
  perm = gsl_permutation_calloc (factor->n_vars);

  if ( factor->sort)
    sort_matrix_indirect (fm, perm);

  for (i = 0 ; i < n_factors; ++i)
    {
      tab_text_format (t, heading_columns + i, 1, TAB_CENTER | TAT_TITLE, _("%d"), i + 1);
    }

  for (i = 0 ; i < factor->n_vars; ++i)
    {
      int j;
      const int matrix_row = perm->data[i];
      tab_text (t, 0, i + heading_rows, TAT_TITLE, var_to_string (factor->vars[matrix_row]));

      for (j = 0 ; j < n_factors; ++j)
	{
	  double x = gsl_matrix_get (fm, matrix_row, j);

	  if ( fabs (x) < factor->blank)
	    continue;

	  tab_double (t, heading_columns + j, heading_rows + i, 0, x, NULL, RC_OTHER);
	}
    }

  gsl_permutation_free (perm);

  tab_submit (t);
}


static void
show_explained_variance (const struct cmd_factor * factor, struct idata *idata,
			 const gsl_vector *initial_eigenvalues,
			 const gsl_vector *extracted_eigenvalues,
			 const gsl_vector *rotated_loadings)
{
  size_t i;
  int c = 0;
  const int heading_columns = 1;
  const int heading_rows = 2;
  const int nr = heading_rows + factor->n_vars;

  struct tab_table *t ;

  double i_total = 0.0;
  double i_cum = 0.0;

  double e_total = 0.0;
  double e_cum = 0.0;

  double r_cum = 0.0;

  int nc = heading_columns;

  if (factor->print & PRINT_EXTRACTION)
    nc += 3;

  if (factor->print & PRINT_INITIAL)
    nc += 3;

  if (factor->print & PRINT_ROTATION)
    {
      nc += factor->rotation == ROT_PROMAX ? 1 : 3;
    }

  /* No point having a table with only headings */
  if ( nc <= heading_columns)
    return;

  t = tab_create (nc, nr);

  tab_title (t, _("Total Variance Explained"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  /* Outline the box */
  tab_box (t,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   nc - 1, nr - 1);

  /* Vertical lines */
  tab_box (t,
	   -1, -1,
	   -1, TAL_1,
	   heading_columns, 0,
	   nc - 1, nr - 1);

  tab_hline (t, TAL_1, 0, nc - 1, heading_rows);
  tab_hline (t, TAL_1, 1, nc - 1, 1);

  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);


  if ( factor->extraction == EXTRACTION_PC)
    tab_text (t, 0, 1, TAB_LEFT | TAT_TITLE, _("Component"));
  else
    tab_text (t, 0, 1, TAB_LEFT | TAT_TITLE, _("Factor"));

  c = 1;
  if (factor->print & PRINT_INITIAL)
    {
      tab_joint_text (t, c, 0, c + 2, 0, TAB_CENTER | TAT_TITLE, _("Initial Eigenvalues"));
      c += 3;
    }

  if (factor->print & PRINT_EXTRACTION)
    {
      tab_joint_text (t, c, 0, c + 2, 0, TAB_CENTER | TAT_TITLE, _("Extraction Sums of Squared Loadings"));
      c += 3;
    }

  if (factor->print & PRINT_ROTATION)
    {
      const int width = factor->rotation == ROT_PROMAX ? 0 : 2;
      tab_joint_text (t, c, 0, c + width, 0, TAB_CENTER | TAT_TITLE, _("Rotation Sums of Squared Loadings"));
      c += width + 1;
    }

  for (i = 0; i < (nc - heading_columns + 2) / 3 ; ++i)
    {
      tab_text (t, i * 3 + 1, 1, TAB_CENTER | TAT_TITLE, _("Total"));

      tab_vline (t, TAL_2, heading_columns + i * 3, 0, nr - 1);

      if (i == 2 && factor->rotation == ROT_PROMAX)
	continue;

      /* xgettext:no-c-format */
      tab_text (t, i * 3 + 2, 1, TAB_CENTER | TAT_TITLE, _("% of Variance"));
      tab_text (t, i * 3 + 3, 1, TAB_CENTER | TAT_TITLE, _("Cumulative %"));
    }

  for (i = 0 ; i < initial_eigenvalues->size; ++i)
    i_total += gsl_vector_get (initial_eigenvalues, i);

  if ( factor->extraction == EXTRACTION_PAF)
    {
      e_total = factor->n_vars;
    }
  else
    {
      e_total = i_total;
    }

  for (i = 0 ; i < factor->n_vars; ++i)
    {
      const double i_lambda = gsl_vector_get (initial_eigenvalues, i);
      double i_percent = 100.0 * i_lambda / i_total ;

      const double e_lambda = gsl_vector_get (extracted_eigenvalues, i);
      double e_percent = 100.0 * e_lambda / e_total ;

      c = 0;

      tab_text_format (t, c++, i + heading_rows, TAB_LEFT | TAT_TITLE, _("%zu"), i + 1);

      i_cum += i_percent;
      e_cum += e_percent;

      /* Initial Eigenvalues */
      if (factor->print & PRINT_INITIAL)
      {
	tab_double (t, c++, i + heading_rows, 0, i_lambda, NULL, RC_OTHER);
	tab_double (t, c++, i + heading_rows, 0, i_percent, NULL, RC_OTHER);
	tab_double (t, c++, i + heading_rows, 0, i_cum, NULL, RC_OTHER);
      }


      if (factor->print & PRINT_EXTRACTION)
	{
	  if (i < idata->n_extractions)
	    {
	      /* Sums of squared loadings */
	      tab_double (t, c++, i + heading_rows, 0, e_lambda, NULL, RC_OTHER);
	      tab_double (t, c++, i + heading_rows, 0, e_percent, NULL, RC_OTHER);
	      tab_double (t, c++, i + heading_rows, 0, e_cum, NULL, RC_OTHER);
	    }
	}

      if (rotated_loadings != NULL)
        {
          const double r_lambda = gsl_vector_get (rotated_loadings, i);
          double r_percent = 100.0 * r_lambda / e_total ;

          if (factor->print & PRINT_ROTATION)
            {
              if (i < idata->n_extractions)
                {
                  r_cum += r_percent;
                  tab_double (t, c++, i + heading_rows, 0, r_lambda, NULL, RC_OTHER);
		  if (factor->rotation != ROT_PROMAX)
		    {
		      tab_double (t, c++, i + heading_rows, 0, r_percent, NULL, RC_OTHER);
		      tab_double (t, c++, i + heading_rows, 0, r_cum, NULL, RC_OTHER);
		    }
                }
            }
        }
    }

  tab_submit (t);
}


static void
show_factor_correlation (const struct cmd_factor * factor, const gsl_matrix *fcm)
{
  size_t i, j;
  const int heading_columns = 1;
  const int heading_rows = 1;
  const int nr = heading_rows + fcm->size2;
  const int nc = heading_columns + fcm->size1;
  struct tab_table *t = tab_create (nc, nr);

  tab_title (t, _("Factor Correlation Matrix"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  /* Outline the box */
  tab_box (t,
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   nc - 1, nr - 1);

  /* Vertical lines */
  tab_box (t,
	   -1, -1,
	   -1, TAL_1,
	   heading_columns, 0,
	   nc - 1, nr - 1);

  tab_hline (t, TAL_1, 0, nc - 1, heading_rows);
  tab_hline (t, TAL_1, 1, nc - 1, 1);

  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);


  if ( factor->extraction == EXTRACTION_PC)
    tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Component"));
  else
    tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Factor"));

  for (i = 0 ; i < fcm->size1; ++i)
    {
      tab_text_format (t, heading_columns + i, 0, TAB_CENTER | TAT_TITLE, _("%d"), i + 1);
    }

  for (i = 0 ; i < fcm->size2; ++i)
    {
      tab_text_format (t, 0, heading_rows + i, TAB_CENTER | TAT_TITLE, _("%d"), i + 1);
    }


  for (i = 0 ; i < fcm->size1; ++i)
    {
      for (j = 0 ; j < fcm->size2; ++j)
	tab_double (t, heading_columns + i,  heading_rows +j, 0, 
		    gsl_matrix_get (fcm, i, j), NULL, RC_OTHER);
    }

  tab_submit (t);
}


static void
show_correlation_matrix (const struct cmd_factor *factor, const struct idata *idata)
{
  struct tab_table *t ;
  size_t i, j;
  int y_pos_corr = -1;
  int y_pos_sig = -1;
  int suffix_rows = 0;

  const int heading_rows = 1;
  const int heading_columns = 2;

  int nc = heading_columns ;
  int nr = heading_rows ;
  int n_data_sets = 0;

  if (factor->print & PRINT_CORRELATION)
    {
      y_pos_corr = n_data_sets;
      n_data_sets++;
      nc = heading_columns + factor->n_vars;
    }

  if (factor->print & PRINT_SIG)
    {
      y_pos_sig = n_data_sets;
      n_data_sets++;
      nc = heading_columns + factor->n_vars;
    }

  nr += n_data_sets * factor->n_vars;

  if (factor->print & PRINT_DETERMINANT)
    suffix_rows = 1;

  /* If the table would contain only headings, don't bother rendering it */
  if (nr <= heading_rows && suffix_rows == 0)
    return;

  t = tab_create (nc, nr + suffix_rows);

  tab_title (t, _("Correlation Matrix"));

  tab_hline (t, TAL_1, 0, nc - 1, heading_rows);

  if (nr > heading_rows)
    {
      tab_headers (t, heading_columns, 0, heading_rows, 0);

      tab_vline (t, TAL_2, 2, 0, nr - 1);

      /* Outline the box */
      tab_box (t,
	       TAL_2, TAL_2,
	       -1, -1,
	       0, 0,
	       nc - 1, nr - 1);

      /* Vertical lines */
      tab_box (t,
	       -1, -1,
	       -1, TAL_1,
	       heading_columns, 0,
	       nc - 1, nr - 1);


      for (i = 0; i < factor->n_vars; ++i)
	tab_text (t, heading_columns + i, 0, TAT_TITLE, var_to_string (factor->vars[i]));


      for (i = 0 ; i < n_data_sets; ++i)
	{
	  int y = heading_rows + i * factor->n_vars;
	  size_t v;
	  for (v = 0; v < factor->n_vars; ++v)
	    tab_text (t, 1, y + v, TAT_TITLE, var_to_string (factor->vars[v]));

	  tab_hline (t, TAL_1, 0, nc - 1, y);
	}

      if (factor->print & PRINT_CORRELATION)
	{
	  const double y = heading_rows + y_pos_corr;
	  tab_text (t, 0, y, TAT_TITLE, _("Correlations"));

	  for (i = 0; i < factor->n_vars; ++i)
	    {
	      for (j = 0; j < factor->n_vars; ++j)
		tab_double (t, heading_columns + i,  y + j, 0, gsl_matrix_get (idata->corr, i, j), NULL, RC_OTHER);
	    }
	}

      if (factor->print & PRINT_SIG)
	{
	  const double y = heading_rows + y_pos_sig * factor->n_vars;
	  tab_text (t, 0, y, TAT_TITLE, _("Sig. (1-tailed)"));

	  for (i = 0; i < factor->n_vars; ++i)
	    {
	      for (j = 0; j < factor->n_vars; ++j)
		{
		  double rho = gsl_matrix_get (idata->corr, i, j);
		  double w = gsl_matrix_get (idata->n, i, j);

		  if (i == j)
		    continue;

		  tab_double (t, heading_columns + i,  y + j, 0, significance_of_correlation (rho, w), NULL, RC_PVALUE);
		}
	    }
	}
    }

  if (factor->print & PRINT_DETERMINANT)
    {
      tab_text (t, 0, nr, TAB_LEFT | TAT_TITLE, _("Determinant"));

      tab_double (t, 1, nr, 0, idata->detR, NULL, RC_OTHER);
    }

  tab_submit (t);
}



static void
do_factor (const struct cmd_factor *factor, struct casereader *r)
{
  struct ccase *c;
  const gsl_matrix *var_matrix;
  const gsl_matrix *mean_matrix;

  const gsl_matrix *analysis_matrix;
  struct idata *idata = idata_alloc (factor->n_vars);

  struct covariance *cov = covariance_1pass_create (factor->n_vars, factor->vars,
					      factor->wv, factor->exclude);

  for ( ; (c = casereader_read (r) ); case_unref (c))
    {
      covariance_accumulate (cov, c);
    }

  idata->cov = covariance_calculate (cov);

  if (idata->cov == NULL)
    {
      msg (MW, _("The dataset contains no complete observations. No analysis will be performed."));
      covariance_destroy (cov);
      goto finish;
    }

  var_matrix = covariance_moments (cov, MOMENT_VARIANCE);
  mean_matrix = covariance_moments (cov, MOMENT_MEAN);
  idata->n = covariance_moments (cov, MOMENT_NONE);
  

  if ( factor->method == METHOD_CORR)
    {
      idata->corr = correlation_from_covariance (idata->cov, var_matrix);
      
      analysis_matrix = idata->corr;
    }
  else
    analysis_matrix = idata->cov;


  if (factor->print & PRINT_DETERMINANT
      || factor->print & PRINT_KMO)
    {
      int sign = 0;

      const int size = idata->corr->size1;
      gsl_permutation *p = gsl_permutation_calloc (size);
      gsl_matrix *tmp = gsl_matrix_calloc (size, size);
      gsl_matrix_memcpy (tmp, idata->corr);

      gsl_linalg_LU_decomp (tmp, p, &sign);
      idata->detR = gsl_linalg_LU_det (tmp, sign);
      gsl_permutation_free (p);
      gsl_matrix_free (tmp);
    }

  if ( factor->print & PRINT_UNIVARIATE)
    {
      const struct fmt_spec *wfmt = factor->wv ? var_get_print_format (factor->wv) : & F_8_0;
      const int nc = 4;
      int i;

      const int heading_columns = 1;
      const int heading_rows = 1;

      const int nr = heading_rows + factor->n_vars;

      struct tab_table *t = tab_create (nc, nr);
      tab_set_format (t, RC_WEIGHT, wfmt);
      tab_title (t, _("Descriptive Statistics"));

      tab_headers (t, heading_columns, 0, heading_rows, 0);

      /* Outline the box */
      tab_box (t,
	       TAL_2, TAL_2,
	       -1, -1,
	       0, 0,
	       nc - 1, nr - 1);

      /* Vertical lines */
      tab_box (t,
	       -1, -1,
	       -1, TAL_1,
	       heading_columns, 0,
	       nc - 1, nr - 1);

      tab_hline (t, TAL_1, 0, nc - 1, heading_rows);
      tab_vline (t, TAL_2, heading_columns, 0, nr - 1);

      tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Mean"));
      tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
      tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Analysis N"));

      for (i = 0 ; i < factor->n_vars; ++i)
	{
	  const struct variable *v = factor->vars[i];
	  tab_text (t, 0, i + heading_rows, TAB_LEFT | TAT_TITLE, var_to_string (v));

	  tab_double (t, 1, i + heading_rows, 0, gsl_matrix_get (mean_matrix, i, i), NULL, RC_OTHER);
	  tab_double (t, 2, i + heading_rows, 0, sqrt (gsl_matrix_get (var_matrix, i, i)), NULL, RC_OTHER);
	  tab_double (t, 3, i + heading_rows, 0, gsl_matrix_get (idata->n, i, i), NULL, RC_WEIGHT);
	}

      tab_submit (t);
    }

  if (factor->print & PRINT_KMO)
    {
      int i;
      double sum_ssq_r = 0;
      double sum_ssq_a = 0;

      double df = factor->n_vars * ( factor->n_vars - 1) / 2;

      double w = 0;


      double xsq;

      const int heading_columns = 2;
      const int heading_rows = 0;

      const int nr = heading_rows + 4;
      const int nc = heading_columns + 1;

      gsl_matrix *a, *x;

      struct tab_table *t = tab_create (nc, nr);
      tab_title (t, _("KMO and Bartlett's Test"));

      x  = clone_matrix (idata->corr);
      gsl_linalg_cholesky_decomp (x);
      gsl_linalg_cholesky_invert (x);

      a = anti_image (x);

      for (i = 0; i < x->size1; ++i)
	{
	  sum_ssq_r += ssq_od_n (x, i);
	  sum_ssq_a += ssq_od_n (a, i);
	}

      gsl_matrix_free (a);
      gsl_matrix_free (x);

      tab_headers (t, heading_columns, 0, heading_rows, 0);

      /* Outline the box */
      tab_box (t,
	       TAL_2, TAL_2,
	       -1, -1,
	       0, 0,
	       nc - 1, nr - 1);

      tab_vline (t, TAL_2, heading_columns, 0, nr - 1);

      tab_text (t, 0, 0, TAT_TITLE | TAB_LEFT, _("Kaiser-Meyer-Olkin Measure of Sampling Adequacy"));

      tab_double (t, 2, 0, 0, sum_ssq_r /  (sum_ssq_r + sum_ssq_a), NULL, RC_OTHER);

      tab_text (t, 0, 1, TAT_TITLE | TAB_LEFT, _("Bartlett's Test of Sphericity"));

      tab_text (t, 1, 1, TAT_TITLE, _("Approx. Chi-Square"));
      tab_text (t, 1, 2, TAT_TITLE, _("df"));
      tab_text (t, 1, 3, TAT_TITLE, _("Sig."));


      /* The literature doesn't say what to do for the value of W when 
	 missing values are involved.  The best thing I can think of
	 is to take the mean average. */
      w = 0;
      for (i = 0; i < idata->n->size1; ++i)
	w += gsl_matrix_get (idata->n, i, i);
      w /= idata->n->size1;

      xsq = w - 1 - (2 * factor->n_vars + 5) / 6.0;
      xsq *= -log (idata->detR);

      tab_double (t, 2, 1, 0, xsq, NULL, RC_OTHER);
      tab_double (t, 2, 2, 0, df, NULL, RC_INTEGER);
      tab_double (t, 2, 3, 0, gsl_cdf_chisq_Q (xsq, df), NULL, RC_PVALUE);
      

      tab_submit (t);
    }

  show_correlation_matrix (factor, idata);
  covariance_destroy (cov);

  {
    gsl_matrix *am = matrix_dup (analysis_matrix);
    gsl_eigen_symmv_workspace *workspace = gsl_eigen_symmv_alloc (factor->n_vars);
    
    gsl_eigen_symmv (am, idata->eval, idata->evec, workspace);

    gsl_eigen_symmv_free (workspace);
    gsl_matrix_free (am);
  }

  gsl_eigen_symmv_sort (idata->eval, idata->evec, GSL_EIGEN_SORT_ABS_DESC);

  idata->n_extractions = n_extracted_factors (factor, idata);

  if (idata->n_extractions == 0)
    {
      msg (MW, _("The %s criteria result in zero factors extracted. Therefore no analysis will be performed."), "FACTOR");
      goto finish;
    }

  if (idata->n_extractions > factor->n_vars)
    {
      msg (MW, 
	   _("The %s criteria result in more factors than variables, which is not meaningful. No analysis will be performed."), 
	   "FACTOR");
      goto finish;
    }
    
  {
    gsl_matrix *rotated_factors = NULL;
    gsl_matrix *pattern_matrix = NULL;
    gsl_matrix *fcm = NULL;
    gsl_vector *rotated_loadings = NULL;

    const gsl_vector *extracted_eigenvalues = NULL;
    gsl_vector *initial_communalities = gsl_vector_alloc (factor->n_vars);
    gsl_vector *extracted_communalities = gsl_vector_alloc (factor->n_vars);
    size_t i;
    struct factor_matrix_workspace *fmw = factor_matrix_workspace_alloc (idata->msr->size, idata->n_extractions);
    gsl_matrix *factor_matrix = gsl_matrix_calloc (factor->n_vars, fmw->n_factors);

    if ( factor->extraction == EXTRACTION_PAF)
      {
	gsl_vector *diff = gsl_vector_alloc (idata->msr->size);
	struct smr_workspace *ws = ws_create (analysis_matrix);

	for (i = 0 ; i < factor->n_vars ; ++i)
	  {
	    double r2 = squared_multiple_correlation (analysis_matrix, i, ws);

	    gsl_vector_set (idata->msr, i, r2);
	  }
	ws_destroy (ws);

	gsl_vector_memcpy (initial_communalities, idata->msr);

	for (i = 0; i < factor->extraction_iterations; ++i)
	  {
	    double min, max;
	    gsl_vector_memcpy (diff, idata->msr);

	    iterate_factor_matrix (analysis_matrix, idata->msr, factor_matrix, fmw);
      
	    gsl_vector_sub (diff, idata->msr);

	    gsl_vector_minmax (diff, &min, &max);
      
	    if ( fabs (min) < factor->econverge && fabs (max) < factor->econverge)
	      break;
	  }
	gsl_vector_free (diff);



	gsl_vector_memcpy (extracted_communalities, idata->msr);
	extracted_eigenvalues = fmw->eval;
      }
    else if (factor->extraction == EXTRACTION_PC)
      {
	for (i = 0; i < factor->n_vars; ++i)
	  gsl_vector_set (initial_communalities, i, communality (idata, i, factor->n_vars));

	gsl_vector_memcpy (extracted_communalities, initial_communalities);

	iterate_factor_matrix (analysis_matrix, extracted_communalities, factor_matrix, fmw);


	extracted_eigenvalues = idata->eval;
      }


    show_communalities (factor, initial_communalities, extracted_communalities);


    if ( factor->rotation != ROT_NONE)
      {
	rotated_factors = gsl_matrix_calloc (factor_matrix->size1, factor_matrix->size2);
	rotated_loadings = gsl_vector_calloc (factor_matrix->size2);
	if (factor->rotation == ROT_PROMAX)
	  {
	    pattern_matrix = gsl_matrix_calloc (factor_matrix->size1, factor_matrix->size2);
	    fcm = gsl_matrix_calloc (factor_matrix->size2, factor_matrix->size2);
	  }
	  

	rotate (factor, factor_matrix, extracted_communalities, rotated_factors, rotated_loadings, pattern_matrix, fcm);
      }
    
    show_explained_variance (factor, idata, idata->eval, extracted_eigenvalues, rotated_loadings);

    factor_matrix_workspace_free (fmw);

    show_scree (factor, idata);

    show_factor_matrix (factor, idata,
			factor->extraction == EXTRACTION_PC ? _("Component Matrix") : _("Factor Matrix"),
			factor_matrix);

    if ( factor->rotation == ROT_PROMAX)
      {
	show_factor_matrix (factor, idata, _("Pattern Matrix"),  pattern_matrix);
	gsl_matrix_free (pattern_matrix);
      }

    if ( factor->rotation != ROT_NONE)
      {
	show_factor_matrix (factor, idata,
			    (factor->rotation == ROT_PROMAX) ? _("Structure Matrix") :
			    (factor->extraction == EXTRACTION_PC ? _("Rotated Component Matrix") : _("Rotated Factor Matrix")),
			    rotated_factors);

	gsl_matrix_free (rotated_factors);
      }

    if ( factor->rotation == ROT_PROMAX)
      {
	show_factor_correlation (factor, fcm);
	gsl_matrix_free (fcm);
      }

    gsl_matrix_free (factor_matrix);
    gsl_vector_free (rotated_loadings);
    gsl_vector_free (initial_communalities);
    gsl_vector_free (extracted_communalities);
  }

 finish:

  idata_free (idata);

  casereader_destroy (r);
}


