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



static struct cmd_oneway cmd;

/* The independent variable */
static struct variable *indep_var;

/* A hash of the values of the independent variable */
struct hsh_table *ind_vals;

/* Number of factors (groups) */
static int n_groups;

/* Number of dependent variables */
static int n_vars;

/* The dependent variables */
static struct variable **vars;





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

      if ( subc_list_double_count(&cmd.dl_contrast[i]) != n_groups )
	{
	  msg(SE, _("Number of contrast coefficients must equal the number of groups"));
	  return CMD_FAILURE;
	}

      for (j=0; j < n_groups ; ++j )
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

  hsh_destroy(ind_vals);

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
      char *s = (vars[i]->label) ? vars[i]->label : vars[i]->name;

      tab_text (t, 0, i * 3 + 1, TAB_LEFT | TAT_TITLE, s);
      tab_text (t, 1, i * 3 + 1, TAB_LEFT | TAT_TITLE, _("Between Groups"));
      tab_text (t, 1, i * 3 + 2, TAB_LEFT | TAT_TITLE, _("Within Groups"));
      tab_text (t, 1, i * 3 + 3, TAB_LEFT | TAT_TITLE, _("Total"));
      
      if (i > 0)
	tab_hline(t, TAL_1, 0, n_cols - 1 , i * 3 + 1);
    }


  tab_title (t, 0, "ANOVA");
  tab_submit (t);


}


static void 
calculate(const struct casefile *cf, void *cmd_)
{
  struct casereader *r;
  struct ccase c;

  struct cmd_t_test *cmd = (struct cmd_t_test *) cmd_;


  ind_vals = hsh_create(4, (hsh_compare_func *) compare_values, 
			   (hsh_hash_func *) hash_value, 
			   0, (void *) indep_var->width );

  for(r = casefile_get_reader (cf);
      casereader_read (r, &c) ;
      case_destroy (&c)) 
    {

	  const union value *val = case_data (&c, indep_var->fv);
	  
	  hsh_insert(ind_vals, (void *) val);

	  /* 
	  if (! value_is_missing(val,v) )
	    {
	      gs->n+=weight;
	      gs->sum+=weight * val->f;
	      gs->ssq+=weight * val->f * val->f;
	    }
	  */
  
    }
  casereader_destroy (r);


  n_groups = hsh_count(ind_vals);


}


/* Show the descriptives table */
static void  
show_descriptives(void)
{
  int v;
  int n_cols =10;
  int n_rows = n_vars * (n_groups + 1 )+ 2;

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

  /* Underline headers */
  tab_hline (t, TAL_2, 0, n_cols - 1, 2 );
  tab_vline (t, TAL_2, 2, 0, n_rows - 1);
  
  tab_text (t, 2, 1, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (t, 3, 1, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (t, 4, 1, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (t, 5, 1, TAB_CENTER | TAT_TITLE, _("Std. Error"));


  tab_vline(t, TAL_0, 7, 0, 0);
  tab_hline(t, TAL_1, 6, 7, 1);
  tab_joint_text (t, 6, 0, 7, 0, TAB_CENTER | TAT_TITLE, _("95% Confidence Interval for Mean"));

  tab_text (t, 6, 1, TAB_CENTER | TAT_TITLE, _("Lower Bound"));
  tab_text (t, 7, 1, TAB_CENTER | TAT_TITLE, _("Upper Bound"));

  tab_text (t, 8, 1, TAB_CENTER | TAT_TITLE, _("Minimum"));
  tab_text (t, 9, 1, TAB_CENTER | TAT_TITLE, _("Maximum"));


  tab_title (t, 0, "Descriptives");


  for ( v=0 ; v < n_vars ; ++v ) 
    {
      struct hsh_iterator g;
      union value *group_value;
      int count = 0 ;      
      char *s = (vars[v]->label) ? vars[v]->label : vars[v]->name;

      tab_text (t, 0, v * ( n_groups + 1 ) + 2, TAB_LEFT | TAT_TITLE, s);
      if ( v > 0) 
	tab_hline(t, TAL_1, 0, n_cols - 1 , v * (n_groups + 1) + 2);


      for (group_value =  hsh_first (ind_vals,&g); 
	   group_value != 0; 
	   group_value = hsh_next(ind_vals,&g))
	{
	  char *lab;

	  lab = val_labs_find(indep_var->val_labs,*group_value);
  
	  if ( lab ) 
	    tab_text (t, 1, v * (n_groups + 1)+ count + 2, 
		      TAB_LEFT | TAT_TITLE ,lab);
	  else
	    tab_text (t, 1, v * (n_groups + 1) + count + 2, 
		      TAB_LEFT | TAT_TITLE | TAT_PRINTF, "%g", group_value->f);
	  
	  count++ ; 
	}

      tab_text (t, 1, v * (n_groups + 1)+ count + 2, 
		      TAB_LEFT | TAT_TITLE ,_("Total"));
      

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
  int n_cols = 2 + n_groups;
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

  for (group_value =  hsh_first (ind_vals,&g); 
       group_value != 0; 
       group_value = hsh_next(ind_vals,&g))
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
	  tab_text (t,  1, (v * lines_per_variable) + i*2 + 1, 
		    TAB_LEFT | TAT_TITLE, 
		    _("Assume equal variances"));

	  tab_text (t,  1, (v * lines_per_variable) + i*2 + 2, 
		    TAB_LEFT | TAT_TITLE, 
		    _("Does not assume equal"));


	  tab_text (t,  2, (v * lines_per_variable) + i*2 + 1, 
		    TAB_CENTER | TAT_TITLE | TAT_PRINTF, "%d",i+1);

	  tab_text (t,  2, (v * lines_per_variable) + i*2 + 2, 
		    TAB_CENTER | TAT_TITLE | TAT_PRINTF, "%d",i+1);

	}

      if ( v > 0 ) 
	tab_hline(t, TAL_1, 0, n_cols - 1, (v * lines_per_variable) + 1);
    }

  tab_submit (t);

}
