/* PSPP - One way ANOVA. -*-c-*-

   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Author: John Darrington 2004

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
#include <gsl/gsl_cdf.h>
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "alloc.h"
#include "str.h"
#include "case.h"
#include "command.h"
#include "lexer.h"
#include "error.h"
#include "magic.h"
#include "misc.h"
#include "tab.h"
#include "som.h"
#include "value-labels.h"
#include "var.h"
#include "vfm.h"
#include "hash.h"
#include "casefile.h"
#include "oneway.h"
#include "group.h"
#include "levene.h"

/* (specification)
   "ONEWAY" (oneway_):
     *variables=custom;
     +missing=miss:!analysis/listwise,
             incl:include/!exclude;
     contrast= double list;
     statistics[st_]=descriptives,homogeneity.
*/
/* (declarations) */
/* (functions) */



static int bad_weight_warn = 1;


static struct cmd_oneway cmd;

/* The independent variable */
static struct variable *indep_var;

/* Number of dependent variables */
static int n_vars;

/* The dependent variables */
static struct variable **vars;


/* A  hash table containing all the distinct values of the independent
   variables */
static struct hsh_table *global_group_hash ;

/* The number of distinct values of the independent variable, when all 
   missing values are disregarded */
static int ostensible_number_of_groups=-1;


/* Function to use for testing for missing values */
static is_missing_func value_is_missing;


static void calculate(const struct casefile *cf, void *_mode);


/* Routines to show the output tables */
static void show_anova_table(void);
static void show_descriptives(void);
static void show_homogeneity(void);
static void show_contrast_coeffs(void);
static void show_contrast_tests(void);



int
cmd_oneway(void)
{
  int i;

  if ( !parse_oneway(&cmd) )
    return CMD_FAILURE;

  /* If /MISSING=INCLUDE is set, then user missing values are ignored */
  if (cmd.incl == ONEWAY_INCLUDE ) 
    value_is_missing = is_system_missing;
  else
    value_is_missing = is_missing;

  multipass_procedure_with_splits (calculate, &cmd);

  /* Check the sanity of the given contrast values */
  for (i = 0 ; i < cmd.sbc_contrast ; ++i ) 
    {
      int j;
      double sum = 0;

      if ( subc_list_double_count(&cmd.dl_contrast[i]) != 
	   ostensible_number_of_groups )
	{
	  msg(SE, 
	      _("Number of contrast coefficients must equal the number of groups"));
	  return CMD_FAILURE;
	}

      for (j=0; j < ostensible_number_of_groups ; ++j )
	sum += subc_list_double_at(&cmd.dl_contrast[i],j);

      if ( sum != 0.0 ) 
	msg(SW,_("Coefficients for contrast %d do not total zero"),i + 1);
    }



  /* Show the statistics tables */
  if ( cmd.sbc_statistics ) 
    {
    for (i = 0 ; i < ONEWAY_ST_count ; ++i ) 
      {
	if  ( ! cmd.a_statistics[i]  ) continue;

	switch (i) {
	case ONEWAY_ST_DESCRIPTIVES:
	  show_descriptives();
	  break;
	case ONEWAY_ST_HOMOGENEITY:
	  show_homogeneity();
	  break;
	}
      }
  }


  show_anova_table();
     
  if (cmd.sbc_contrast)
    {
      show_contrast_coeffs();
      show_contrast_tests();
    }


  /* Clean up */
  for (i = 0 ; i < n_vars ; ++i ) 
    {
      struct hsh_table *group_hash = vars[i]->p.ww.group_hash;

      hsh_destroy(group_hash);
    }

  hsh_destroy(global_group_hash);

  return CMD_SUCCESS;
}





/* Parser for the variables sub command */
static int
oneway_custom_variables(struct cmd_oneway *cmd UNUSED)
{

  lex_match('=');

  if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
      && token != T_ALL)
    return 2;
  

  if (!parse_variables (default_dict, &vars, &n_vars,
			PV_DUPLICATE 
			| PV_NUMERIC | PV_NO_SCRATCH) )
    {
      free (vars);
      return 0;
    }

  assert(n_vars);

  if ( ! lex_match(T_BY))
    return 2;


  indep_var = parse_variable();

  if ( !indep_var ) 
    {
      msg(SE,_("`%s' is not a variable name"),tokid);
      return 0;
    }


  return 1;
}


/* Show the ANOVA table */
static void  
show_anova_table(void)
{
  int i;
  int n_cols =7;
  int n_rows = n_vars * 3 + 1;

  struct tab_table *t;


  t = tab_create (n_cols,n_rows,0);
  tab_headers (t, 2, 0, 1, 0);
  tab_dim (t, tab_natural_dimensions);


  tab_box (t, 
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_hline (t, TAL_2, 0, n_cols - 1, 1 );
  tab_vline (t, TAL_2, 2, 0, n_rows - 1);
  tab_vline (t, TAL_0, 1, 0, 0);
  
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Sum of Squares"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (t, 4, 0, TAB_CENTER | TAT_TITLE, _("Mean Square"));
  tab_text (t, 5, 0, TAB_CENTER | TAT_TITLE, _("F"));
  tab_text (t, 6, 0, TAB_CENTER | TAT_TITLE, _("Significance"));


  for ( i=0 ; i < n_vars ; ++i ) 
    {
      struct group_statistics *totals = &vars[i]->p.ww.ugs;
      struct hsh_table *group_hash = vars[i]->p.ww.group_hash;
      struct hsh_iterator g;
      struct group_statistics *gs;
      double ssa=0;


      for (gs =  hsh_first (group_hash,&g); 
	   gs != 0; 
	   gs = hsh_next(group_hash,&g))
       {
	 ssa += (gs->sum * gs->sum)/gs->n;
       }
      
      ssa -= ( totals->sum * totals->sum ) / totals->n ;

      const char *s = (vars[i]->label) ? vars[i]->label : vars[i]->name;


      tab_text (t, 0, i * 3 + 1, TAB_LEFT | TAT_TITLE, s);
      tab_text (t, 1, i * 3 + 1, TAB_LEFT | TAT_TITLE, _("Between Groups"));
      tab_text (t, 1, i * 3 + 2, TAB_LEFT | TAT_TITLE, _("Within Groups"));
      tab_text (t, 1, i * 3 + 3, TAB_LEFT | TAT_TITLE, _("Total"));
      
      if (i > 0)
	tab_hline(t, TAL_1, 0, n_cols - 1 , i * 3 + 1);

      {
	const double sst = totals->ssq - ( totals->sum * totals->sum) / totals->n ;
	const double df1 = vars[i]->p.ww.n_groups - 1;
	const double df2 = totals->n - vars[i]->p.ww.n_groups ;
	const double msa = ssa / df1;
	
	vars[i]->p.ww.mse  = (sst - ssa) / df2;
	
	
	/* Sums of Squares */
	tab_float (t, 2, i * 3 + 1, 0, ssa, 10, 2);
	tab_float (t, 2, i * 3 + 3, 0, sst, 10, 2);
	tab_float (t, 2, i * 3 + 2, 0, sst - ssa, 10, 2);


	/* Degrees of freedom */
	tab_float (t, 3, i * 3 + 1, 0, df1, 4, 0);
	tab_float (t, 3, i * 3 + 2, 0, df2, 4, 0);
	tab_float (t, 3, i * 3 + 3, 0, totals->n - 1, 4, 0);

	/* Mean Squares */
	tab_float (t, 4, i * 3 + 1, TAB_RIGHT, msa, 8, 3);
	tab_float (t, 4, i * 3 + 2, TAB_RIGHT, vars[i]->p.ww.mse, 8, 3);
	

	{ 
	  const double F = msa/vars[i]->p.ww.mse ;

	  /* The F value */
	  tab_float (t, 5, i * 3 + 1, 0,  F, 8, 3);
	
	  /* The significance */
	  tab_float (t, 6, i * 3 + 1, 0, gsl_cdf_fdist_Q(F,df1,df2), 8, 3);
	}

      }

    }


  tab_title (t, 0, _("ANOVA"));
  tab_submit (t);


}

/* Show the descriptives table */
static void  
show_descriptives(void)
{
  int v;
  int n_cols =10;
  struct tab_table *t;
  int row;

  const double confidence=0.95;
  const double q = (1.0 - confidence) / 2.0;

  
  int n_rows = 2 ; 



  for ( v = 0 ; v < n_vars ; ++v ) 
    n_rows += vars[v]->p.ww.n_groups + 1;

  t = tab_create (n_cols,n_rows,0);
  tab_headers (t, 2, 0, 2, 0);
  tab_dim (t, tab_natural_dimensions);


  /* Put a frame around the entire box, and vertical lines inside */
  tab_box (t, 
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  /* Underline headers */
  tab_hline (t, TAL_2, 0, n_cols - 1, 2 );
  tab_vline (t, TAL_2, 2, 0, n_rows - 1);
  
  tab_text (t, 2, 1, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (t, 3, 1, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (t, 4, 1, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (t, 5, 1, TAB_CENTER | TAT_TITLE, _("Std. Error"));


  tab_vline(t, TAL_0, 7, 0, 0);
  tab_hline(t, TAL_1, 6, 7, 1);
  tab_joint_text (t, 6, 0, 7, 0, TAB_CENTER | TAT_TITLE | TAT_PRINTF, _("%g%% Confidence Interval for Mean"),confidence*100.0);

  tab_text (t, 6, 1, TAB_CENTER | TAT_TITLE, _("Lower Bound"));
  tab_text (t, 7, 1, TAB_CENTER | TAT_TITLE, _("Upper Bound"));

  tab_text (t, 8, 1, TAB_CENTER | TAT_TITLE, _("Minimum"));
  tab_text (t, 9, 1, TAB_CENTER | TAT_TITLE, _("Maximum"));


  tab_title (t, 0, _("Descriptives"));


  row = 2;
  for ( v=0 ; v < n_vars ; ++v ) 
    {
      double T;
      double stderr;


      struct hsh_iterator g;
      struct group_statistics *gs;
      struct group_statistics *totals = &vars[v]->p.ww.ugs; 

      int count = 0 ;      
      char *s = (vars[v]->label) ? vars[v]->label : vars[v]->name;

      struct hsh_table *group_hash = vars[v]->p.ww.group_hash;


      tab_text (t, 0, row, TAB_LEFT | TAT_TITLE, s);
      if ( v > 0) 
	tab_hline(t, TAL_1, 0, n_cols - 1 , row);


      for (gs =  hsh_first (group_hash,&g); 
	   gs != 0; 
	   gs = hsh_next(group_hash,&g))
	{
	  const char *s = val_labs_find(indep_var->val_labs, gs->id );
  
	  if ( s ) 
	    tab_text (t, 1, row + count, 
		      TAB_LEFT | TAT_TITLE ,s);
	  else if ( indep_var->width != 0 ) 
	    tab_text (t, 1, row + count,
		      TAB_LEFT | TAT_TITLE, gs->id.s);
	  else
	    tab_text (t, 1, row + count,
		      TAB_LEFT | TAT_TITLE | TAT_PRINTF, "%g", gs->id.f);
	  

	  /* Now fill in the numbers ... */

	  tab_float (t, 2, row + count, 0, gs->n, 8,0);

	  tab_float (t, 3, row + count, 0, gs->mean,8,2);
	  
	  tab_float (t, 4, row + count, 0, gs->std_dev,8,2);

	  stderr = gs->std_dev/sqrt(gs->n) ;
	  tab_float (t, 5, row + count, 0, 
		     stderr, 8,2);

	  /* Now the confidence interval */
      
	  T = gsl_cdf_tdist_Qinv(q,gs->n - 1);

	  tab_float(t, 6, row + count, 0,
		    gs->mean - T * stderr, 8, 2); 

	  tab_float(t, 7, row + count, 0,
		    gs->mean + T * stderr, 8, 2); 

	  /* Min and Max */

	  tab_float(t, 8, row + count, 0,  gs->minimum, 8, 2); 
	  tab_float(t, 9, row + count, 0,  gs->maximum, 8, 2); 

	  count++ ; 
	}

      tab_text (t, 1, row + count, 
		      TAB_LEFT | TAT_TITLE ,_("Total"));

      tab_float (t, 2, row + count, 0, totals->n, 8,0);

      tab_float (t, 3, row + count, 0, totals->mean, 8,2);

      tab_float (t, 4, row + count, 0, totals->std_dev,8,2);

      stderr = totals->std_dev/sqrt(totals->n) ;

      tab_float (t, 5, row + count, 0, stderr, 8,2);

      /* Now the confidence interval */
      
      T = gsl_cdf_tdist_Qinv(q,totals->n - 1);

      tab_float(t, 6, row + count, 0,
		totals->mean - T * stderr, 8, 2); 

      tab_float(t, 7, row + count, 0,
		totals->mean + T * stderr, 8, 2); 

      /* Min and Max */

      tab_float(t, 8, row + count, 0,  totals->minimum, 8, 2); 
      tab_float(t, 9, row + count, 0,  totals->maximum, 8, 2); 

      row += vars[v]->p.ww.n_groups + 1;
    }


  tab_submit (t);


}

/* Show the homogeneity table */
static void 
show_homogeneity(void)
{
  int v;
  int n_cols = 5;
  int n_rows = n_vars + 1;

  struct tab_table *t;


  t = tab_create (n_cols,n_rows,0);
  tab_headers (t, 1, 0, 1, 0);
  tab_dim (t, tab_natural_dimensions);

  /* Put a frame around the entire box, and vertical lines inside */
  tab_box (t, 
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);


  tab_hline(t, TAL_2, 0, n_cols - 1, 1);
  tab_vline(t, TAL_2, 1, 0, n_rows - 1);


  tab_text (t,  1, 0, TAB_CENTER | TAT_TITLE, _("Levene Statistic"));
  tab_text (t,  2, 0, TAB_CENTER | TAT_TITLE, _("df1"));
  tab_text (t,  3, 0, TAB_CENTER | TAT_TITLE, _("df2"));
  tab_text (t,  4, 0, TAB_CENTER | TAT_TITLE, _("Significance"));
  

  tab_title (t, 0, _("Test of Homogeneity of Variances"));

  for ( v=0 ; v < n_vars ; ++v ) 
    {
      char *s = (vars[v]->label) ? vars[v]->label : vars[v]->name;

      tab_text (t, 0, v + 1, TAB_LEFT | TAT_TITLE, s);
    }

  tab_submit (t);


}


/* Show the contrast coefficients table */
static void 
show_contrast_coeffs(void)
{
  char *s;
  int n_cols = 2 + ostensible_number_of_groups;
  int n_rows = 2 + cmd.sbc_contrast;
  struct hsh_iterator g;
  union value *group_value;
  int count = 0 ;      


  struct tab_table *t;


  t = tab_create (n_cols,n_rows,0);
  tab_headers (t, 2, 0, 2, 0);
  tab_dim (t, tab_natural_dimensions);

  /* Put a frame around the entire box, and vertical lines inside */
  tab_box (t, 
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);


  tab_box (t, 
	   -1,-1,
	   TAL_0, TAL_0,
	   2, 0,
	   n_cols - 1, 0);

  tab_box (t,
	   -1,-1,
	   TAL_0, TAL_0,
	   0,0,
	   1,1);


  tab_hline(t, TAL_1, 2, n_cols - 1, 1);


  tab_hline(t, TAL_2, 0, n_cols - 1, 2);
  tab_vline(t, TAL_2, 2, 0, n_rows - 1);


  tab_title (t, 0, _("Contrast Coefficients"));

  tab_text (t,  0, 2, TAB_LEFT | TAT_TITLE, _("Contrast"));

  s = (indep_var->label) ? indep_var->label : indep_var->name;

  tab_joint_text (t, 2, 0, n_cols - 1, 0, TAB_CENTER | TAT_TITLE, s);

  for (group_value =  hsh_first (global_group_hash,&g); 
       group_value != 0; 
       group_value = hsh_next(global_group_hash,&g))
    {
      int i;
      char *lab;

      lab = val_labs_find(indep_var->val_labs,*group_value);
  
      if ( lab ) 
	tab_text (t, count + 2, 1,
		  TAB_CENTER | TAT_TITLE ,lab);
      else
	tab_text (t, count + 2, 1, 
		  TAB_CENTER | TAT_TITLE | TAT_PRINTF, "%g", group_value->f);

      for (i = 0 ; i < cmd.sbc_contrast ; ++i ) 
	{
	  tab_text(t, 1, i + 2, TAB_CENTER | TAT_PRINTF, "%d", i + 1);
	  tab_text(t, count + 2, i + 2, TAB_RIGHT | TAT_PRINTF, "%g", 
		   subc_list_double_at(&cmd.dl_contrast[i],count)
		   );
	}
	  
      count++ ; 
    }

  tab_submit (t);

}


/* Show the results of the contrast tests */
static void 
show_contrast_tests(void)
{
  int v;
  int n_cols = 8;
  int n_rows = 1 + n_vars * 2 * cmd.sbc_contrast;

  struct tab_table *t;

  t = tab_create (n_cols,n_rows,0);
  tab_headers (t, 3, 0, 1, 0);
  tab_dim (t, tab_natural_dimensions);

  /* Put a frame around the entire box, and vertical lines inside */
  tab_box (t, 
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_box (t, 
	   -1,-1,
	   TAL_0, TAL_0,
	   0, 0,
	   2, 0);

  tab_hline(t, TAL_2, 0, n_cols - 1, 1);
  tab_vline(t, TAL_2, 3, 0, n_rows - 1);


  tab_title (t, 0, _("Contrast Tests"));

  tab_text (t,  2, 0, TAB_CENTER | TAT_TITLE, _("Contrast"));
  tab_text (t,  3, 0, TAB_CENTER | TAT_TITLE, _("Value of Contrast"));
  tab_text (t,  4, 0, TAB_CENTER | TAT_TITLE, _("Std. Error"));
  tab_text (t,  5, 0, TAB_CENTER | TAT_TITLE, _("t"));
  tab_text (t,  6, 0, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (t,  7, 0, TAB_CENTER | TAT_TITLE, _("Sig. (2-tailed)"));

  for ( v = 0 ; v < n_vars ; ++v ) 
    {
      int i;
      int lines_per_variable = 2 * cmd.sbc_contrast;


      tab_text (t,  0, (v * lines_per_variable) + 1, TAB_LEFT | TAT_TITLE,
		vars[v]->label?vars[v]->label:vars[v]->name);



      for ( i = 0 ; i < cmd.sbc_contrast ; ++i ) 
	{
	  int ci;
	  double contrast_value = 0.0;
	  double coef_msq = 0.0;
	  struct oneway_proc *ww = &vars[v]->p.ww ;
	  struct hsh_table *group_hash = ww->group_hash;
	  struct hsh_iterator g;
	  struct group_statistics *gs;

	  double T;
	  double stderr_contrast ;
	  double df;

	  
	  if ( i == 0 ) 
	    {
	      tab_text (t,  1, (v * lines_per_variable) + i + 1, 
			TAB_LEFT | TAT_TITLE,
			_("Assume equal variances"));

	      tab_text (t,  1, (v * lines_per_variable) + i + 1 + cmd.sbc_contrast, 
			TAB_LEFT | TAT_TITLE, 
			_("Does not assume equal"));
	    }

	  tab_text (t,  2, (v * lines_per_variable) + i + 1, 
		    TAB_CENTER | TAT_TITLE | TAT_PRINTF, "%d",i+1);


	  tab_text (t,  2, (v * lines_per_variable) + i + 1 + cmd.sbc_contrast,
		    TAB_CENTER | TAT_TITLE | TAT_PRINTF, "%d",i+1);

	  /* FIXME: Potential danger here.
	     We're ASSUMING THE array is in the order corresponding to the 
	     hash order. */
	  for (ci = 0, gs = hsh_first (group_hash,&g); 	
	       gs != 0;
	       ++ci, gs = hsh_next(group_hash,&g))
	    {
	      const double coef = subc_list_double_at(&cmd.dl_contrast[i],ci);

	      contrast_value += coef * gs->mean;

	      coef_msq += (coef * coef) / gs->n ; 
	    }

	  tab_float (t,  3, (v * lines_per_variable) + i + 1, 
		     TAB_RIGHT, contrast_value, 8,2);

	  tab_float (t,  3, (v * lines_per_variable) + i + 1 + cmd.sbc_contrast,
		     TAB_RIGHT, contrast_value, 8,2);


	  stderr_contrast = sqrt(vars[v]->p.ww.mse * coef_msq);

	  /* Std. Error */
	  tab_float (t,  4, (v * lines_per_variable) + i + 1, 
		     TAB_RIGHT, stderr_contrast,
		     8,3);

	  T = fabs(contrast_value / stderr_contrast) ;

	  /* T Statistic */

	  tab_float (t,  5, (v * lines_per_variable) + i + 1, 
		     TAB_RIGHT, T,
		     8,3);

	  df = ww->ugs.n - ww->n_groups;

	  /* Degrees of Freedom */
	  tab_float (t,  6, (v * lines_per_variable) + i + 1, 
		     TAB_RIGHT,  df,
		     8,0);


	  /* Significance TWO TAILED !!*/
	  tab_float (t,  7, (v * lines_per_variable) + i + 1, 
		     TAB_RIGHT,  2 * gsl_cdf_tdist_Q(T,df),
		     8,3);

	}

      if ( v > 0 ) 
	tab_hline(t, TAL_1, 0, n_cols - 1, (v * lines_per_variable) + 1);
    }

  tab_submit (t);

}


/* ONEWAY ANOVA Calculations */

static void  postcalc (  struct cmd_oneway *cmd UNUSED );

static void  precalc ( struct cmd_oneway *cmd UNUSED );

int  compare_group_id (const struct group_statistics *a, 
		       const struct group_statistics *b, int width);

unsigned int hash_group_id(const struct group_statistics *v, int width);

void  free_group_id(struct group_statistics *v, void *aux UNUSED);




int 
compare_group_id (const struct group_statistics *a, 
		  const struct group_statistics *b, int width)
{
  return compare_values(&a->id, &b->id, width);
}

unsigned int
hash_group_id(const struct group_statistics *v, int width)
{
  return hash_value ( &v->id, width);
}

void 
free_group_id(struct group_statistics *v, void *aux UNUSED)
{
  free(v);
}


/* Pre calculations */
static void 
precalc ( struct cmd_oneway *cmd UNUSED )
{
  int i=0;

  for(i=0; i< n_vars ; ++i) 
    {
      struct group_statistics *totals = &vars[i]->p.ww.ugs;
      
      /* Create a hash for each of the dependent variables.
	 The hash contains a group_statistics structure, 
	 and is keyed by value of the independent variable */

      vars[i]->p.ww.group_hash = 
	hsh_create(4, 
		   (hsh_compare_func *) compare_group_id,
		   (hsh_hash_func *) hash_group_id,
		   (hsh_free_func *) free_group_id,
		   (void *) indep_var->width );


      totals->sum=0;
      totals->n=0;
      totals->ssq=0;
      totals->sum_diff=0;
      totals->maximum = - DBL_MAX;
      totals->minimum = DBL_MAX;
    }
}


static void 
calculate(const struct casefile *cf, void *cmd_)
{
  struct casereader *r;
  struct ccase c;


  struct cmd_oneway *cmd = (struct cmd_oneway *) cmd_;

  global_group_hash = hsh_create(4, 
				 (hsh_compare_func *) compare_values,
				 (hsh_hash_func *) hash_value,
				 0,
				 (void *) indep_var->width );

  precalc(cmd);

  for(r = casefile_get_reader (cf);
      casereader_read (r, &c) ;
      case_destroy (&c)) 
    {
      int i;

      const double weight = 
	dict_get_case_weight(default_dict,&c,&bad_weight_warn);
      
      const union value *indep_val = case_data (&c, indep_var->fv);
	  
      hsh_insert ( global_group_hash, (void *) indep_val );


      for ( i = 0 ; i < n_vars ; ++i ) 
	{
	  const struct variable *v = vars[i];

	  const union value *val = case_data (&c, v->fv);

	  struct hsh_table *group_hash = vars[i]->p.ww.group_hash;

	  struct group_statistics *gs;

	  gs = hsh_find(group_hash, (void *) indep_val );

	  if ( ! gs ) 
	    {
	      gs = (struct group_statistics *) 
		xmalloc (sizeof(struct group_statistics));

	      gs->id = *indep_val;
	      gs->sum=0;
	      gs->n=0;
	      gs->ssq=0;
	      gs->sum_diff=0;
	      gs->minimum = DBL_MAX;
	      gs->maximum = -DBL_MAX;

	      hsh_insert ( group_hash, (void *) gs );
	    }
	  
	  if (! value_is_missing(val,v) )
	    {
	      struct group_statistics *totals = &vars[i]->p.ww.ugs;

	      totals->n+=weight;
	      totals->sum+=weight * val->f;
	      totals->ssq+=weight * val->f * val->f;

	      if ( val->f * weight  < totals->minimum ) 
		totals->minimum = val->f * weight;

	      if ( val->f * weight  > totals->maximum ) 
		totals->maximum = val->f * weight;

	      gs->n+=weight;
	      gs->sum+=weight * val->f;
	      gs->ssq+=weight * val->f * val->f;

	      if ( val->f * weight  < gs->minimum ) 
		gs->minimum = val->f * weight;

	      if ( val->f * weight  > gs->maximum ) 
		gs->maximum = val->f * weight;
	    }

	  vars[i]->p.ww.n_groups = hsh_count ( group_hash );
	}
  
    }
  casereader_destroy (r);

  postcalc(cmd);

  ostensible_number_of_groups = hsh_count (global_group_hash);

}


/* Post calculations for the ONEWAY command */
void 
postcalc (  struct cmd_oneway *cmd UNUSED )
{
  int i=0;


  for(i = 0; i < n_vars ; ++i) 
    {
      struct hsh_table *group_hash = vars[i]->p.ww.group_hash;
      struct group_statistics *totals = &vars[i]->p.ww.ugs;

      struct hsh_iterator g;
      struct group_statistics *gs;

      for (gs =  hsh_first (group_hash,&g); 
	   gs != 0; 
	   gs = hsh_next(group_hash,&g))
	{
	  gs->mean=gs->sum / gs->n;
	  gs->s_std_dev= sqrt(
			      ( (gs->ssq / gs->n ) - gs->mean * gs->mean )
			      ) ;

	  gs->std_dev= sqrt(
			    gs->n/(gs->n-1) *
			    ( (gs->ssq / gs->n ) - gs->mean * gs->mean )
			    ) ;

	  gs->se_mean = gs->std_dev / sqrt(gs->n);
	  gs->mean_diff= gs->sum_diff / gs->n;

	}



      totals->mean = totals->sum / totals->n;
      totals->std_dev= sqrt(
			    totals->n/(totals->n-1) *
			    ( (totals->ssq / totals->n ) - totals->mean * totals->mean )
			    ) ;

      totals->se_mean = totals->std_dev / sqrt(totals->n);
	


      
    }
}
