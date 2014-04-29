/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "math/covariance.h"

#include <gsl/gsl_matrix.h>

#include "data/case.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/misc.h"
#include "math/categoricals.h"
#include "math/interaction.h"
#include "math/moments.h"

#include "gl/xalloc.h"

#define n_MOMENTS (MOMENT_VARIANCE + 1)


/* Create a new matrix of NEW_SIZE x NEW_SIZE and copy the elements of
   matrix IN into it.  IN must be a square matrix, and in normal usage
   it will be smaller than NEW_SIZE.
   IN is destroyed by this function.  The return value must be destroyed
   when no longer required.
*/
static gsl_matrix *
resize_matrix (gsl_matrix *in, size_t new_size)
{
  size_t i, j;

  gsl_matrix *out = NULL;

  assert (in->size1 == in->size2);

  if (new_size <= in->size1)
    return in;

  out = gsl_matrix_calloc (new_size, new_size);

  for (i = 0; i < in->size1; ++i)
    {
      for (j = 0; j < in->size2; ++j)
	{
	  double x = gsl_matrix_get (in, i, j);

	  gsl_matrix_set (out, i, j, x);
	}
    }
    
  gsl_matrix_free (in);

  return out;
}

struct covariance
{
  /* The variables for which the covariance matrix is to be calculated. */
  size_t n_vars;
  const struct variable *const *vars;

  /* Categorical variables. */
  struct categoricals *categoricals;

  /* Array containing number of categories per categorical variable. */
  size_t *n_categories;

  /* Dimension of the covariance matrix. */
  size_t dim;

  /* The weight variable (or NULL if none) */
  const struct variable *wv;

  /* A set of matrices containing the 0th, 1st and 2nd moments */
  gsl_matrix **moments;

  /* The class of missing values to exclude */
  enum mv_class exclude;

  /* An array of doubles representing the covariance matrix.
     Only the top triangle is included, and no diagonals */
  double *cm;
  int n_cm;

  /* 1 for single pass algorithm; 
     2 for double pass algorithm
  */
  short passes;

  /*
    0 : No pass has  been made
    1 : First pass has been started
    2 : Second pass has been 
    
    IE: How many passes have been (partially) made. */
  short state;

  /* Flags indicating that the first case has been seen */
  bool pass_one_first_case_seen;
  bool pass_two_first_case_seen;

  gsl_matrix *unnormalised;
};



/* Return a matrix containing the M th moments.
   The matrix is of size  NxN where N is the number of variables.
   Each row represents the moments of a variable.
   In the absence of missing values, the columns of this matrix will
   be identical.  If missing values are involved, then element (i,j)
   is the moment of the i th variable, when paired with the j th variable.
 */
const gsl_matrix *
covariance_moments (const struct covariance *cov, int m)
{
  return cov->moments[m];
}



/* Create a covariance struct.
 */
struct covariance *
covariance_1pass_create (size_t n_vars, const struct variable *const *vars,
			 const struct variable *weight, enum mv_class exclude)
{
  size_t i;
  struct covariance *cov = xzalloc (sizeof *cov);

  cov->passes = 1;
  cov->state = 0;
  cov->pass_one_first_case_seen = cov->pass_two_first_case_seen = false;
  
  cov->vars = vars;

  cov->wv = weight;
  cov->n_vars = n_vars;
  cov->dim = n_vars;

  cov->moments = xmalloc (sizeof *cov->moments * n_MOMENTS);
  
  for (i = 0; i < n_MOMENTS; ++i)
    cov->moments[i] = gsl_matrix_calloc (n_vars, n_vars);

  cov->exclude = exclude;

  cov->n_cm = (n_vars * (n_vars - 1)  ) / 2;


  cov->cm = xcalloc (cov->n_cm, sizeof *cov->cm);
  cov->categoricals = NULL;

  return cov;
}

/*
  Create a covariance struct for a two-pass algorithm. If categorical
  variables are involed, the dimension cannot be know until after the
  first data pass, so the actual covariances will not be allocated
  until then.
 */
struct covariance *
covariance_2pass_create (size_t n_vars, const struct variable *const *vars,
			 struct categoricals *cats,
			 const struct variable *wv, enum mv_class exclude)
{
  size_t i;
  struct covariance *cov = xmalloc (sizeof *cov);

  cov->passes = 2;
  cov->state = 0;
  cov->pass_one_first_case_seen = cov->pass_two_first_case_seen = false;
  
  cov->vars = vars;

  cov->wv = wv;
  cov->n_vars = n_vars;
  cov->dim = n_vars;

  cov->moments = xmalloc (sizeof *cov->moments * n_MOMENTS);
  
  for (i = 0; i < n_MOMENTS; ++i)
    cov->moments[i] = gsl_matrix_calloc (n_vars, n_vars);

  cov->exclude = exclude;

  cov->n_cm = -1;
  cov->cm = NULL;

  cov->categoricals = cats;
  cov->unnormalised = NULL;

  return cov;
}

/* Return an integer, which can be used to index 
   into COV->cm, to obtain the I, J th element
   of the covariance matrix.  If COV->cm does not
   contain that element, then a negative value
   will be returned.
*/
static int
cm_idx (const struct covariance *cov, int i, int j)
{
  int as;
  const int n2j = cov->dim - 2 - j;
  const int nj = cov->dim - 2 ;
  
  assert (i >= 0);
  assert (j < cov->dim);

  if ( i == 0)
    return -1;

  if (j >= cov->dim - 1)
    return -1;

  if ( i <= j) 
    return -1 ;

  as = nj * (nj + 1) ;
  as -= n2j * (n2j + 1) ; 
  as /= 2;

  return i - 1 + as;
}


/*
  Returns true iff the variable corresponding to the Ith element of the covariance matrix 
   has a missing value for case C
*/
static bool
is_missing (const struct covariance *cov, int i, const struct ccase *c)
{
  const struct variable *var = i < cov->n_vars ?
    cov->vars[i] : 
    categoricals_get_interaction_by_subscript (cov->categoricals, i - cov->n_vars)->vars[0];

  const union value *val = case_data (c, var);

  return var_is_value_missing (var, val, cov->exclude);
}


static double
get_val (const struct covariance *cov, int i, const struct ccase *c)
{
  if ( i < cov->n_vars)
    {
      const struct variable *var = cov->vars[i];

      const union value *val = case_data (c, var);

      return val->f;
    }

  return categoricals_get_effects_code_for_case (cov->categoricals, i - cov->n_vars, c);
}

#if 0
void
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
#endif

/* Call this function for every case in the data set */
void
covariance_accumulate_pass1 (struct covariance *cov, const struct ccase *c)
{
  size_t i, j, m;
  const double weight = cov->wv ? case_data (c, cov->wv)->f : 1.0;

  assert (cov->passes == 2);
  if (!cov->pass_one_first_case_seen)
    {
      assert (cov->state == 0);
      cov->state = 1;
    }

  if (cov->categoricals)
    categoricals_update (cov->categoricals, c);

  for (i = 0 ; i < cov->dim; ++i)
    {
      double v1 = get_val (cov, i, c);

      if ( is_missing (cov, i, c))
	continue;

      for (j = 0 ; j < cov->dim; ++j)
	{
	  double pwr = 1.0;

	  if ( is_missing (cov, j, c))
	    continue;

	  for (m = 0 ; m <= MOMENT_MEAN; ++m)
	    {
	      double *x = gsl_matrix_ptr (cov->moments[m], i, j);

	      *x += pwr * weight;
	      pwr *= v1;
	    }
	}
    }

  cov->pass_one_first_case_seen = true;
}


/* Call this function for every case in the data set */
void
covariance_accumulate_pass2 (struct covariance *cov, const struct ccase *c)
{
  size_t i, j;
  const double weight = cov->wv ? case_data (c, cov->wv)->f : 1.0;

  assert (cov->passes == 2);
  assert (cov->state >= 1);

  if (! cov->pass_two_first_case_seen)
    {
      size_t m;
      assert (cov->state == 1);
      cov->state = 2;

      if (cov->categoricals)
      	categoricals_done (cov->categoricals);

      cov->dim = cov->n_vars;
      
      if (cov->categoricals)
	cov->dim += categoricals_df_total (cov->categoricals);

      cov->n_cm = (cov->dim * (cov->dim - 1)  ) / 2;
      cov->cm = xcalloc (cov->n_cm, sizeof *cov->cm);

      /* Grow the moment matrices so that they're large enough to accommodate the
	 categorical elements */
      for (i = 0; i < n_MOMENTS; ++i)
	{
	  cov->moments[i] = resize_matrix (cov->moments[i], cov->dim);
	}

      /* Populate the moments matrices with the categorical value elements */
      for (i = cov->n_vars; i < cov->dim; ++i)
	{
	  for (j = 0 ; j < cov->dim ; ++j) /* FIXME: This is WRONG !!! */
	    {
	      double w = categoricals_get_weight_by_subscript (cov->categoricals, i - cov->n_vars);

	      gsl_matrix_set (cov->moments[MOMENT_NONE], i, j, w);

	      w = categoricals_get_sum_by_subscript (cov->categoricals, i - cov->n_vars);

	      gsl_matrix_set (cov->moments[MOMENT_MEAN], i, j, w);
	    }
	}

      /* FIXME: This is WRONG!!  It must be fixed to properly handle missing values.  For
       now it assumes there are none */
      for (m = 0 ; m < n_MOMENTS; ++m)
	{
	  for (i = 0 ; i < cov->dim ; ++i)
	    {
	      double x = gsl_matrix_get (cov->moments[m], i, cov->n_vars -1);
	      for (j = cov->n_vars; j < cov->dim; ++j)
		{
		  gsl_matrix_set (cov->moments[m], i, j, x);
		}
	    }
	}

      /* Divide the means by the number of samples */
      for (i = 0; i < cov->dim; ++i)
	{
	  for (j = 0; j < cov->dim; ++j)
	    {
	      double *x = gsl_matrix_ptr (cov->moments[MOMENT_MEAN], i, j);
	      *x /= gsl_matrix_get (cov->moments[MOMENT_NONE], i, j);
 	    }
	}
    }

  for (i = 0 ; i < cov->dim; ++i)
    {
      double v1 = get_val (cov, i, c);

      if ( is_missing (cov, i, c))
	continue;

      for (j = 0 ; j < cov->dim; ++j)
	{
	  int idx;
	  double ss ;
	  double v2 = get_val (cov, j, c);

	  const double s = pow2 (v1 - gsl_matrix_get (cov->moments[MOMENT_MEAN], i, j)) * weight;

	  if ( is_missing (cov, j, c))
	    continue;

	  {
	    double *x = gsl_matrix_ptr (cov->moments[MOMENT_VARIANCE], i, j);
	    *x += s;
	  }

	  ss = 
	    (v1 - gsl_matrix_get (cov->moments[MOMENT_MEAN], i, j))
	    * 
	    (v2 - gsl_matrix_get (cov->moments[MOMENT_MEAN], i, j))
	    * weight
	    ;

	  idx = cm_idx (cov, i, j);
	  if (idx >= 0)
	    {
	      cov->cm [idx] += ss;
	    }
	}
    }

  cov->pass_two_first_case_seen = true;
}


/* Call this function for every case in the data set.
   After all cases have been passed, call covariance_calculate
 */
void
covariance_accumulate (struct covariance *cov, const struct ccase *c)
{
  size_t i, j, m;
  const double weight = cov->wv ? case_data (c, cov->wv)->f : 1.0;

  assert (cov->passes == 1);

  if ( !cov->pass_one_first_case_seen)
    {
      assert ( cov->state == 0);
      cov->state = 1;
    }

  for (i = 0 ; i < cov->dim; ++i)
    {
      const union value *val1 = case_data (c, cov->vars[i]);

      if ( is_missing (cov, i, c))
	continue;

      for (j = 0 ; j < cov->dim; ++j)
	{
	  double pwr = 1.0;
	  int idx;
	  const union value *val2 = case_data (c, cov->vars[j]);

	  if ( is_missing (cov, j, c))
	    continue;

	  idx = cm_idx (cov, i, j);
	  if (idx >= 0)
	    {
	      cov->cm [idx] += val1->f * val2->f * weight;
	    }

	  for (m = 0 ; m < n_MOMENTS; ++m)
	    {
	      double *x = gsl_matrix_ptr (cov->moments[m], i, j);

	      *x += pwr * weight;
	      pwr *= val1->f;
	    }
	}
    }

  cov->pass_one_first_case_seen = true;
}


/* 
   Allocate and return a gsl_matrix containing the covariances of the
   data.
*/
static gsl_matrix *
cm_to_gsl (struct covariance *cov)
{
  int i, j;
  gsl_matrix *m = gsl_matrix_calloc (cov->dim, cov->dim);

  /* Copy the non-diagonal elements from cov->cm */
  for ( j = 0 ; j < cov->dim - 1; ++j)
    {
      for (i = j+1 ; i < cov->dim; ++i)
	{
	  double x = cov->cm [cm_idx (cov, i, j)];
	  gsl_matrix_set (m, i, j, x);
	  gsl_matrix_set (m, j, i, x);
	}
    }

  /* Copy the diagonal elements from cov->moments[2] */
  for (j = 0 ; j < cov->dim ; ++j)
    {
      double sigma = gsl_matrix_get (cov->moments[2], j, j);
      gsl_matrix_set (m, j, j, sigma);
    }

  return m;
}


static gsl_matrix *
covariance_calculate_double_pass (struct covariance *cov)
{
  size_t i, j;
  for (i = 0 ; i < cov->dim; ++i)
    {
      for (j = 0 ; j < cov->dim; ++j)
	{
	  int idx;
	  double *x = gsl_matrix_ptr (cov->moments[MOMENT_VARIANCE], i, j);
	  *x /= gsl_matrix_get (cov->moments[MOMENT_NONE], i, j);

	  idx = cm_idx (cov, i, j);
	  if ( idx >= 0)
	    {
	      x = &cov->cm [idx];
	      *x /= gsl_matrix_get (cov->moments[MOMENT_NONE], i, j);
	    }
	}
    }

  return  cm_to_gsl (cov);
}

static gsl_matrix *
covariance_calculate_single_pass (struct covariance *cov)
{
  size_t i, j;
  size_t m;

  for (m = 0; m < n_MOMENTS; ++m)
    {
      /* Divide the moments by the number of samples */
      if ( m > 0)
	{
	  for (i = 0 ; i < cov->dim; ++i)
	    {
	      for (j = 0 ; j < cov->dim; ++j)
		{
		  double *x = gsl_matrix_ptr (cov->moments[m], i, j);
		  *x /= gsl_matrix_get (cov->moments[0], i, j);

		  if ( m == MOMENT_VARIANCE)
		    *x -= pow2 (gsl_matrix_get (cov->moments[1], i, j));
		}
	    }
	}
    }

  /* Centre the moments */
  for ( j = 0 ; j < cov->dim - 1; ++j)
    {
      for (i = j + 1 ; i < cov->dim; ++i)
	{
	  double *x = &cov->cm [cm_idx (cov, i, j)];
	  
	  *x /= gsl_matrix_get (cov->moments[0], i, j);

	  *x -=
	    gsl_matrix_get (cov->moments[MOMENT_MEAN], i, j) 
	    *
	    gsl_matrix_get (cov->moments[MOMENT_MEAN], j, i); 
	}
    }

  return cm_to_gsl (cov);
}


/* Return a pointer to gsl_matrix containing the pairwise covariances.  The
   caller owns the returned matrix and must free it when it is no longer
   needed.

   Call this function only after all data have been accumulated.  */
gsl_matrix *
covariance_calculate (struct covariance *cov)
{
  if ( cov->state <= 0 )
    return NULL;

  switch (cov->passes)
    {
    case 1:
      return covariance_calculate_single_pass (cov);  
      break;
    case 2:
      return covariance_calculate_double_pass (cov);  
      break;
    default:
      NOT_REACHED ();
    }
}

/*
  Covariance computed without dividing by the sample size.
 */
static gsl_matrix *
covariance_calculate_double_pass_unnormalized (struct covariance *cov)
{
  return  cm_to_gsl (cov);
}

static gsl_matrix *
covariance_calculate_single_pass_unnormalized (struct covariance *cov)
{
  size_t i, j;

  for (i = 0 ; i < cov->dim; ++i)
    {
      for (j = 0 ; j < cov->dim; ++j)
	{
	  double *x = gsl_matrix_ptr (cov->moments[MOMENT_VARIANCE], i, j);
	  *x -= pow2 (gsl_matrix_get (cov->moments[MOMENT_MEAN], i, j))
	    / gsl_matrix_get (cov->moments[MOMENT_NONE], i, j);
	}
    }
  for ( j = 0 ; j < cov->dim - 1; ++j)
    {
      for (i = j + 1 ; i < cov->dim; ++i)
	{
	  double *x = &cov->cm [cm_idx (cov, i, j)];
	  
	  *x -=
	    gsl_matrix_get (cov->moments[MOMENT_MEAN], i, j) 
	    *
	    gsl_matrix_get (cov->moments[MOMENT_MEAN], j, i) 
	  / gsl_matrix_get (cov->moments[MOMENT_NONE], i, j);
	}
    }

  return cm_to_gsl (cov);
}


/* Return a pointer to gsl_matrix containing the pairwise covariances.  The
   returned matrix is owned by the structure, and must not be freed.

   Call this function only after all data have been accumulated.  */
const gsl_matrix *
covariance_calculate_unnormalized (struct covariance *cov)
{
  if ( cov->state <= 0 )
    return NULL;

  if (cov->unnormalised != NULL)
    return cov->unnormalised;

  switch (cov->passes)
    {
    case 1:
      cov->unnormalised =  covariance_calculate_single_pass_unnormalized (cov);  
      break;
    case 2:
      cov->unnormalised =  covariance_calculate_double_pass_unnormalized (cov);  
      break;
    default:
      NOT_REACHED ();
    }

  return cov->unnormalised;
}

/* Function to access the categoricals used by COV
   The return value is owned by the COV
*/
const struct categoricals *
covariance_get_categoricals (const struct covariance *cov)
{
  return cov->categoricals;
}


/* Destroy the COV object */
void
covariance_destroy (struct covariance *cov)
{
  size_t i;

  categoricals_destroy (cov->categoricals);

  for (i = 0; i < n_MOMENTS; ++i)
    gsl_matrix_free (cov->moments[i]);

  gsl_matrix_free (cov->unnormalised);
  free (cov->moments);
  free (cov->cm);
  free (cov);
}

size_t
covariance_dim (const struct covariance * cov)
{
  return (cov->dim);
}



/*
  Routines to assist debugging.
  The following are not thoroughly tested and in certain respects
  unreliable.  They should only be
  used for aids to development. Not as user accessible code.
*/

#include "libpspp/str.h"
#include "output/tab.h"
#include "data/format.h"


/* Create a table which can be populated with the encodings for
   the covariance matrix COV */
struct tab_table *
covariance_dump_enc_header (const struct covariance *cov, int length)
{
  struct tab_table *t = tab_create (cov->dim,  length);
  int n;
  int i;

  tab_title (t, "Covariance Encoding");

  tab_box (t, 
	   TAL_2, TAL_2, 0, 0,
	   0, 0,   tab_nc (t) - 1,   tab_nr (t) - 1);

  tab_hline (t, TAL_2, 0, tab_nc (t) - 1, 1);


  for (i = 0 ; i < cov->n_vars; ++i)
    {
      tab_text (t, i, 0, TAT_TITLE, var_get_name (cov->vars[i]));
      tab_vline (t, TAL_1, i + 1, 0, tab_nr (t) - 1);
    }

  n = 0;
  while (i < cov->dim)
    {
      struct string str;
      int idx = i - cov->n_vars;
      const struct interaction *iact =
	categoricals_get_interaction_by_subscript (cov->categoricals, idx);
      int df;

      ds_init_empty (&str);
      interaction_to_string (iact, &str);

      df = categoricals_df (cov->categoricals, n);

      tab_joint_text (t,
		      i, 0,
		      i + df - 1, 0,
		      TAT_TITLE, ds_cstr (&str));

      if (i + df < tab_nr (t) - 1)
	tab_vline (t, TAL_1, i + df, 0, tab_nr (t) - 1);

      i += df;
      n++;
      ds_destroy (&str);
    }

  return t;
}


/*
  Append table T, which should have been returned by covariance_dump_enc_header
  with an entry corresponding to case C for the covariance matrix COV
 */
void
covariance_dump_enc (const struct covariance *cov, const struct ccase *c,
		     struct tab_table *t)
{
  static int row = 0;
  int i;
  ++row;
  for (i = 0 ; i < cov->dim; ++i)
    {
      double v = get_val (cov, i, c);
      tab_double (t, i, row, 0, v, i < cov->n_vars ? NULL : &F_8_0, RC_OTHER);
    }
}
