/* PSPP - EXAMINE data for normality . -*-c-*-

Copyright (C) 2004 Free Software Foundation, Inc.
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
#include "dictionary.h"
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
#include "factor_stats.h"
/* (headers) */
#include "chart.h"

/* (specification)
   "EXAMINE" (xmn_):
   *variables=custom;
   +total=custom;
   +nototal=custom;
   +missing=miss:pairwise/!listwise,
   rep:report/!noreport,
   incl:include/!exclude;
   +compare=cmp:variables/!groups;
   +plot[plt_]=stemleaf,boxplot,npplot,:spreadlevel(*d:n),histogram,all,none;
   +cinterval=double;
   +statistics[st_]=descriptives,:extreme(*d:n),all,none.
*/

/* (declarations) */

/* (functions) */



static struct cmd_examine cmd;

static struct variable **dependent_vars;

static int n_dependent_vars;

static struct hsh_table *hash_table_factors=0;




struct factor 
{
  /* The independent variable for this factor */
  struct variable *indep_var;

  /* The  factor statistics for each value of the independent variable */
  struct hsh_table *hash_table_val;

  /* The subfactor (if any) */
  struct factor *subfactor;

};




/* Parse the clause specifying the factors */
static int examine_parse_independent_vars(struct cmd_examine *cmd, 
					  struct hsh_table *hash_factors );




/* Functions to support hashes of factors */
int compare_factors(const struct factor *f1, const struct factor *f2, 
		    void *aux);

unsigned hash_factor(const struct factor *f, void *aux);

void free_factor(struct factor *f, void *aux UNUSED);


/* Output functions */
static void show_summary(struct variable **dependent_var, int n_dep_var, 
			 struct factor *f);

static void show_descriptives(struct variable **dependent_var, 
			      int n_dep_var, 
			      struct factor *factor);


static void show_extremes(struct variable **dependent_var, 
			  int n_dep_var, 
			  struct factor *factor,
			  int n_extremities);


void np_plot(const struct metrics *m, const char *varname);



/* Per Split function */
static void run_examine(const struct casefile *cf, void *);

static void output_examine(void);


static struct factor_statistics *totals = 0;



int
cmd_examine(void)
{

  if ( !parse_examine(&cmd) )
    return CMD_FAILURE;
  
  if ( cmd.st_n == SYSMIS ) 
    cmd.st_n = 5;

  if ( ! cmd.sbc_cinterval) 
    cmd.n_cinterval[0] = 95.0;


  totals = xmalloc ( sizeof (struct factor_statistics *) );

  totals->stats = xmalloc(sizeof ( struct metrics ) * n_dependent_vars);

  multipass_procedure_with_splits (run_examine, NULL);


  hsh_destroy(hash_table_factors);

  free(totals->stats);
  free(totals);

  return CMD_SUCCESS;
};


/* Show all the appropriate tables */
static void
output_examine(void)
{

  /* Show totals if appropriate */
  if ( ! cmd.sbc_nototal || 
       ! hash_table_factors || 0 == hsh_count (hash_table_factors))
    {
      show_summary(dependent_vars, n_dependent_vars,0);

      if ( cmd.sbc_statistics ) 
	{
	  if ( cmd.a_statistics[XMN_ST_DESCRIPTIVES]) 
	    show_descriptives(dependent_vars, n_dependent_vars, 0);
	  
	  if ( cmd.a_statistics[XMN_ST_EXTREME]) 
	    show_extremes(dependent_vars, n_dependent_vars, 0, cmd.st_n);
	}

      if ( cmd.sbc_plot) 
	{
	  if ( cmd.a_plot[XMN_PLT_NPPLOT] ) 
	    {
	      int v;

	      for ( v = 0 ; v < n_dependent_vars; ++v ) 
		{
		  np_plot(&totals->stats[v], var_to_string(dependent_vars[v]));
		}

	    }
	}

    }


  /* Show grouped statistics  if appropriate */
  if ( hash_table_factors && 0 != hsh_count (hash_table_factors))
    {
      struct hsh_iterator hi;
      struct factor *f;

      for(f = hsh_first(hash_table_factors,&hi);
	  f != 0;
	  f = hsh_next(hash_table_factors,&hi)) 
	{
	  show_summary(dependent_vars, n_dependent_vars,f);

	  if ( cmd.sbc_statistics )
	    {
	      if ( cmd.a_statistics[XMN_ST_DESCRIPTIVES])
		show_descriptives(dependent_vars, n_dependent_vars, f);
	      
	      if ( cmd.a_statistics[XMN_ST_EXTREME])
		show_extremes(dependent_vars, n_dependent_vars, f, cmd.st_n);
	    }


	  if ( cmd.sbc_plot) 
	    {
	      if ( cmd.a_plot[XMN_PLT_NPPLOT] ) 
		{
		  struct hsh_iterator h2;
		  struct factor_statistics *foo ;
		  for (foo = hsh_first(f->hash_table_val,&h2);
		       foo != 0 ; 
		       foo  = hsh_next(f->hash_table_val,&h2))
		    {
		      int v;
		      for ( v = 0 ; v < n_dependent_vars; ++ v)
			{
			  char buf[100];
			  sprintf(buf, "%s (%s = %s)",
				  var_to_string(dependent_vars[v]),
				  var_to_string(f->indep_var),
				  value_to_string(foo->id,f->indep_var));
			  np_plot(&foo->stats[v], buf);
			}
		    }
		}
	    }
	}
    }

}



/* TOTAL and NOTOTAL are simple, mutually exclusive flags */
static int
xmn_custom_total(struct cmd_examine *p)
{
  if ( p->sbc_nototal ) 
    {
      msg (SE, _("%s and %s are mutually exclusive"),"TOTAL","NOTOTAL");
      return 0;
    }

  return 1;
}

static int
xmn_custom_nototal(struct cmd_examine *p)
{
  if ( p->sbc_total ) 
    {
      msg (SE, _("%s and %s are mutually exclusive"),"TOTAL","NOTOTAL");
      return 0;
    }

  return 1;
}


/* Compare two factors */
int 
compare_factors (const struct factor *f1, 
		 const struct factor *f2, 
		 void *aux)
{
  int indep_var_cmp = strcmp(f1->indep_var->name, f2->indep_var->name);

  if ( 0 != indep_var_cmp ) 
    return indep_var_cmp;

  /* If the names are identical, and there are no subfactors then
     the factors are identical */
  if ( ! f1->subfactor &&  ! f2->subfactor ) 
    return 0;
    
  /* ... otherwise we must compare the subfactors */

  return compare_factors(f1->subfactor, f2->subfactor, aux);

}

/* Create a hash of a factor */
unsigned 
hash_factor( const struct factor *f, void *aux)
{
  unsigned h;
  h = hsh_hash_string(f->indep_var->name);
  
  if ( f->subfactor ) 
    h += hash_factor(f->subfactor, aux);

  return h;
}


/* Free up a factor */
void
free_factor(struct factor *f, void *aux)
{
  hsh_destroy(f->hash_table_val);

  if ( f->subfactor ) 
    free_factor(f->subfactor, aux);

  free(f);
}


/* Parser for the variables sub command */
static int
xmn_custom_variables(struct cmd_examine *cmd )
{

  lex_match('=');

  if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
      && token != T_ALL)
    return 2;
  
  if (!parse_variables (default_dict, &dependent_vars, &n_dependent_vars,
			PV_NO_DUPLICATE | PV_NUMERIC | PV_NO_SCRATCH) )
    {
      free (dependent_vars);
      return 0;
    }

  assert(n_dependent_vars);

  if ( lex_match(T_BY))
    {
      hash_table_factors = hsh_create(4, 
				      (hsh_compare_func *) compare_factors, 
				      (hsh_hash_func *) hash_factor, 
				      (hsh_free_func *) free_factor, 0);

      return examine_parse_independent_vars(cmd, hash_table_factors);
    }

  
  
  return 1;
}


/* Parse the clause specifying the factors */
static int
examine_parse_independent_vars(struct cmd_examine *cmd, 
			       struct hsh_table *hash_table_factors )
{
  struct factor *f = 0;

  if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
      && token != T_ALL)
    return 2;

  if ( !f ) 
    {
      f = xmalloc(sizeof(struct factor));
      f->indep_var = 0;
      f->hash_table_val = 0;
      f->subfactor = 0;
    }
  
  f->indep_var = parse_variable();
  
  if ( ! f->hash_table_val ) 
    f->hash_table_val = hsh_create(4,(hsh_compare_func *) compare_indep_values,
				   (hsh_hash_func *) hash_indep_value,
				   (hsh_free_func *) free_factor_stats,
				   (void *) f->indep_var->width);

  if ( token == T_BY ) 
    {
      lex_match(T_BY);

      if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
	  && token != T_ALL)
	return 2;

      f->subfactor = xmalloc(sizeof(struct factor));

      f->subfactor->indep_var = parse_variable();
      
      f->subfactor->subfactor = 0;

      f->subfactor->hash_table_val = 
	hsh_create(4,
		   (hsh_compare_func *) compare_indep_values,
		   (hsh_hash_func *) hash_indep_value,
		   (hsh_free_func *) free_factor_stats,
		   (void *) f->subfactor->indep_var->width);
    }

  hsh_insert(hash_table_factors, f);
  
  lex_match(',');

  if ( token == '.' || token == '/' ) 
    return 1;

  return examine_parse_independent_vars(cmd, hash_table_factors);
}


void populate_descriptives(struct tab_table *t, int col, int row, 
			   const struct metrics *fs);


void populate_extremities(struct tab_table *t, int col, int row, int n);


/* Show the descriptives table */
void
show_descriptives(struct variable **dependent_var, 
		  int n_dep_var, 
		  struct factor *factor)
{
  int i;
  int heading_columns ;
  int n_cols;
  const int n_stat_rows = 13;

  const int heading_rows = 1;
  int n_rows = heading_rows ;

  struct tab_table *t;


  if ( !factor ) 
    {
      heading_columns = 1;
      n_rows += n_dep_var * n_stat_rows;
    }
  else
    {
      assert(factor->indep_var);
      if ( factor->subfactor == 0 ) 
	{
	  heading_columns = 2;
	  n_rows += n_dep_var * hsh_count(factor->hash_table_val) * n_stat_rows;
	}
      else
	{
	  heading_columns = 3;
	  n_rows += n_dep_var * hsh_count(factor->hash_table_val) * 
	    hsh_count(factor->subfactor->hash_table_val) * n_stat_rows ;
	}
    }

  n_cols = heading_columns + 4;

  t = tab_create (n_cols, n_rows, 0);

  tab_headers (t, heading_columns + 1, 0, heading_rows, 0);

  tab_dim (t, tab_natural_dimensions);

  /* Outline the box and have no internal lines*/
  tab_box (t, 
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_hline (t, TAL_2, 0, n_cols - 1, heading_rows );

  tab_vline (t, TAL_1, heading_columns, 0, n_rows - 1);
  tab_vline (t, TAL_2, n_cols - 2, 0, n_rows - 1);
  tab_vline (t, TAL_1, n_cols - 1, 0, n_rows - 1);

  tab_text (t, n_cols - 2, 0, TAB_CENTER | TAT_TITLE, _("Statistic"));
  tab_text (t, n_cols - 1, 0, TAB_CENTER | TAT_TITLE, _("Std. Error"));


  for ( i = 0 ; i < n_dep_var ; ++i ) 
    {
      int row;
      int n_subfactors = 1;
      int n_factors = 1;
	
      if ( factor ) 
	{
	  n_factors = hsh_count(factor->hash_table_val);
	  if (  factor->subfactor ) 
	    n_subfactors = hsh_count(factor->subfactor->hash_table_val);
	}


      row = heading_rows + i * n_stat_rows * n_factors * n_subfactors; 

      if ( i > 0 )
	tab_hline(t, TAL_1, 0, n_cols - 1, row );

      if ( factor  )
	{
	  struct hsh_iterator hi;
	  const struct factor_statistics *fs;
	  int count = 0;

	  tab_text (t, 1, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		    var_to_string(factor->indep_var));



	  for (fs  = hsh_first(factor->hash_table_val, &hi);
	       fs != 0;
	       fs  = hsh_next(factor->hash_table_val,  &hi))
	    {
	      tab_text (t, 1, 
			row  + count * n_subfactors * n_stat_rows,
			TAB_RIGHT | TAT_TITLE, 
			value_to_string(fs->id, factor->indep_var)
			);

	      if ( count > 0 ) 
		tab_hline (t, TAL_1, 1, n_cols - 1,  
			   row  + count * n_subfactors * n_stat_rows);

	      if ( factor->subfactor ) 
		{
		  int count2=0;
		  struct hsh_iterator h2;
		  const struct factor_statistics *sub_fs;
	      
		  tab_text (t, 2, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
			    var_to_string(factor->subfactor->indep_var));

		  for ( sub_fs = hsh_first(factor->subfactor->hash_table_val, 
					   &h2);
			sub_fs != 0;
			sub_fs = hsh_next(factor->subfactor->hash_table_val, 
					  &h2))
		    {
			
		      tab_text(t, 2, 
			       row
			       + count * n_subfactors * n_stat_rows 
			       + count2 * n_stat_rows,
			       TAB_RIGHT | TAT_TITLE ,
			       value_to_string(sub_fs->id, factor->subfactor->indep_var)
			       );

		      if ( count2 > 0 ) 
			tab_hline (t, TAL_1, 2, n_cols - 1,  
				   row
				   + count * n_subfactors * n_stat_rows 
				   + count2 * n_stat_rows);
			       
		      populate_descriptives(t, heading_columns,
					    row
					    + count * n_subfactors 
					    * n_stat_rows 
					    + count2 * n_stat_rows,
					    &sub_fs->stats[i]);
					    
			
		      count2++;
		    }
		}
	      else
		{
		  
		  populate_descriptives(t, heading_columns, 
					row  
					+ count * n_subfactors * n_stat_rows, 
					&fs->stats[i]);
		}

	      count ++;
	    }
	}
      else
	{
	  populate_descriptives(t, heading_columns, 
				row, &totals->stats[i]);
	}

      tab_text (t, 
		0, row,
		TAB_LEFT | TAT_TITLE, 
		var_to_string(dependent_var[i])
		);

    }

  tab_title (t, 0, _("Descriptives"));

  tab_submit(t);

}



/* Fill in the descriptives data */
void
populate_descriptives(struct tab_table *tbl, int col, int row, 
		      const struct metrics *m)
{

  const double t = gsl_cdf_tdist_Qinv(1 - cmd.n_cinterval[0]/100.0/2.0, \
				       m->n -1);


  tab_text (tbl, col, 
	    row,
	    TAB_LEFT | TAT_TITLE,
	    _("Mean"));

  tab_float (tbl, col + 2,
	     row,
	     TAB_CENTER,
	     m->mean,
	     8,2);
  
  tab_float (tbl, col + 3,
	     row,
	     TAB_CENTER,
	     m->stderr,
	     8,3);
  

  tab_text (tbl, col, 
	    row + 1,
	    TAB_LEFT | TAT_TITLE | TAT_PRINTF,
	    _("%g%% Confidence Interval for Mean"), cmd.n_cinterval[0]);


  tab_text (tbl, col + 1, 
	    row  + 1,
	    TAB_LEFT | TAT_TITLE,
	    _("Lower Bound"));

  tab_float (tbl, col + 2,
	     row + 1,
	     TAB_CENTER,
	     m->mean - t * m->stderr, 
	     8,3);

  tab_text (tbl, col + 1,  
	    row + 2,
	    TAB_LEFT | TAT_TITLE,
	    _("Upper Bound"));


  tab_float (tbl, col + 2,
	     row + 2,
	     TAB_CENTER,
	     m->mean + t * m->stderr, 
	     8,3);

  tab_text (tbl, col, 
	    row + 3,
	    TAB_LEFT | TAT_TITLE,
	    _("5% Trimmed Mean"));

  tab_float (tbl, col + 2, 
	    row + 3,
	     TAB_CENTER,
	     m->trimmed_mean,
	     8,2);

  tab_text (tbl, col, 
	    row + 4,
	    TAB_LEFT | TAT_TITLE,
	    _("Median"));

  tab_text (tbl, col, 
	    row + 5,
	    TAB_LEFT | TAT_TITLE,
	    _("Variance"));

  tab_float (tbl, col + 2,
	     row + 5,
	     TAB_CENTER,
	     m->var,
	     8,3);


  tab_text (tbl, col, 
	    row + 6,
	    TAB_LEFT | TAT_TITLE,
	    _("Std. Deviation"));


  tab_float (tbl, col + 2,
	     row + 6,
	     TAB_CENTER,
	     m->stddev,
	     8,3);

  
  tab_text (tbl, col, 
	    row + 7,
	    TAB_LEFT | TAT_TITLE,
	    _("Minimum"));

  tab_float (tbl, col + 2,
	     row + 7,
	     TAB_CENTER,
	     m->min,
	     8,3);

  tab_text (tbl, col, 
	    row + 8,
	    TAB_LEFT | TAT_TITLE,
	    _("Maximum"));

  tab_float (tbl, col + 2,
	     row + 8,
	     TAB_CENTER,
	     m->max,
	     8,3);


  tab_text (tbl, col, 
	    row + 9,
	    TAB_LEFT | TAT_TITLE,
	    _("Range"));


  tab_float (tbl, col + 2,
	     row + 9,
	     TAB_CENTER,
	     m->max - m->min,
	     8,3);

  tab_text (tbl, col, 
	    row + 10,
	    TAB_LEFT | TAT_TITLE,
	    _("Interquartile Range"));

  tab_text (tbl, col, 
	    row + 11,
	    TAB_LEFT | TAT_TITLE,
	    _("Skewness"));

  tab_text (tbl, col, 
	    row + 12,
	    TAB_LEFT | TAT_TITLE,
	    _("Kurtosis"));
}


void
show_summary(struct variable **dependent_var, 
	     int n_dep_var, 
	     struct factor *factor)
{
  static const char *subtitle[]=
    {
      N_("Valid"),
      N_("Missing"),
      N_("Total")
    };

  int i;
  int heading_columns ;
  int n_cols;
  const int heading_rows = 3;
  struct tab_table *tbl;

  int n_rows = heading_rows;

  if ( !factor ) 
    {
      heading_columns = 1;
      n_rows += n_dep_var;
    }
  else
    {
      assert(factor->indep_var);
      if ( factor->subfactor == 0 ) 
	{
	  heading_columns = 2;
	  n_rows += n_dep_var * hsh_count(factor->hash_table_val);
	}
      else
	{
	  heading_columns = 3;
	  n_rows += n_dep_var * hsh_count(factor->hash_table_val) * 
	    hsh_count(factor->subfactor->hash_table_val) ;
	}
    }


  n_cols = heading_columns + 6;

  tbl = tab_create (n_cols,n_rows,0);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions);
  
  /* Outline the box and have vertical internal lines*/
  tab_box (tbl, 
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_hline (tbl, TAL_2, 0, n_cols - 1, heading_rows );
  tab_hline (tbl, TAL_1, heading_columns, n_cols - 1, 1 );
  tab_hline (tbl, TAL_1, 0, n_cols - 1, heading_rows -1 );

  tab_vline (tbl, TAL_2, heading_columns, 0, n_rows - 1);


  tab_title (tbl, 0, _("Case Processing Summary"));
  

  tab_joint_text(tbl, heading_columns, 0, 
		 n_cols -1, 0,
		 TAB_CENTER | TAT_TITLE,
		 _("Cases"));

  /* Remove lines ... */
  tab_box (tbl, 
	   -1, -1,
	   TAL_0, TAL_0,
	   heading_columns, 0,
	   n_cols - 1, 0);

  if ( factor ) 
    {
      tab_text (tbl, 1, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		var_to_string(factor->indep_var));

      if ( factor->subfactor ) 
	tab_text (tbl, 2, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		  var_to_string(factor->subfactor->indep_var));
    }

  for ( i = 0 ; i < 3 ; ++i ) 
    {
      tab_text (tbl, heading_columns + i*2 , 2, TAB_CENTER | TAT_TITLE, _("N"));
      tab_text (tbl, heading_columns + i*2 + 1, 2, TAB_CENTER | TAT_TITLE, 
		_("Percent"));

      tab_joint_text(tbl, heading_columns + i*2 , 1,
		     heading_columns + i*2 + 1, 1,
		     TAB_CENTER | TAT_TITLE,
		     subtitle[i]);

      tab_box (tbl, -1, -1,
	       TAL_0, TAL_0,
	       heading_columns + i*2, 1,
	       heading_columns + i*2 + 1, 1);

    }


  for ( i = 0 ; i < n_dep_var ; ++i ) 
    {
      int n_subfactors = 1;
      int n_factors = 1;
	
      if ( factor ) 
	{
	  n_factors = hsh_count(factor->hash_table_val);
	  if (  factor->subfactor ) 
	    n_subfactors = hsh_count(factor->subfactor->hash_table_val);
	}

      tab_text (tbl, 
		0, i * n_factors * n_subfactors + heading_rows,
		TAB_LEFT | TAT_TITLE, 
		var_to_string(dependent_var[i])
		);

      if ( factor  )
	{
	  struct hsh_iterator hi;
	  const struct factor_statistics *fs;
	  int count = 0;

	  for (fs  = hsh_first(factor->hash_table_val, &hi);
	       fs != 0;
	       fs  = hsh_next(factor->hash_table_val,  &hi))
	    {
	      tab_text (tbl, 1, 
			i * n_factors * n_subfactors + heading_rows
			+ count * n_subfactors,
			TAB_RIGHT | TAT_TITLE, 
			value_to_string(fs->id, factor->indep_var)
			);

	      if ( factor->subfactor ) 
		{
		  int count2=0;
		  struct hsh_iterator h2;
		  const struct factor_statistics *sub_fs;
		
		  for ( sub_fs = hsh_first(factor->subfactor->hash_table_val, 
					   &h2);
			sub_fs != 0;
			sub_fs = hsh_next(factor->subfactor->hash_table_val, 
					  &h2))
		    {
			
		      tab_text(tbl, 2, 
			       i * n_factors * n_subfactors + heading_rows
			       + count * n_subfactors + count2,
			       TAB_RIGHT | TAT_TITLE ,
			       value_to_string(sub_fs->id, factor->subfactor->indep_var)
			       );
			
		      count2++;
		    }
		}
	      count ++;
	    }
	}
    }


  tab_submit (tbl);
  
}

static int bad_weight_warn = 1;


static void 
run_examine(const struct casefile *cf, void *aux UNUSED)
{
  struct hsh_iterator hi;
  struct factor *fctr;

  struct casereader *r;
  struct ccase c;
  int v;

  /* Make sure we haven't got rubbish left over from a 
     previous split */
  if ( hash_table_factors ) 
    {
      for ( fctr = hsh_first(hash_table_factors, &hi);
	    fctr != 0;
	    fctr = hsh_next (hash_table_factors, &hi) )
	{
	  hsh_clear(fctr->hash_table_val);

	  while ( (fctr = fctr->subfactor) )
	    hsh_clear(fctr->hash_table_val);
	}
    }

  for ( v = 0 ; v < n_dependent_vars ; ++v ) 
    metrics_precalc(&totals->stats[v]);

  for(r = casefile_get_reader (cf);
      casereader_read (r, &c) ;
      case_destroy (&c) ) 
    {
      const double weight = 
	dict_get_case_weight(default_dict, &c, &bad_weight_warn);

      for ( v = 0 ; v < n_dependent_vars ; ++v ) 
	{
	  const struct variable *var = dependent_vars[v];
	  const union value *val = case_data (&c, var->fv);

	  metrics_calc(&totals->stats[v], val, weight);
	}

      if ( hash_table_factors ) 
	{
	  for ( fctr = hsh_first(hash_table_factors, &hi);
		fctr != 0;
		fctr = hsh_next (hash_table_factors, &hi) )
	    {
	      const union value *indep_val = 
		case_data(&c, fctr->indep_var->fv);

	      struct factor_statistics **foo = ( struct factor_statistics ** ) 
		hsh_probe(fctr->hash_table_val, (void *) &indep_val);

	      if ( !*foo ) 
		{
		  *foo = xmalloc ( sizeof ( struct factor_statistics));
		  (*foo)->id = indep_val;
		  (*foo)->stats = xmalloc ( sizeof ( struct metrics ) 
					    * n_dependent_vars);

		  for ( v =  0 ; v  < n_dependent_vars ; ++v ) 
		    metrics_precalc( &(*foo)->stats[v] );

		  hsh_insert(fctr->hash_table_val, (void *) *foo);
		}

	      for ( v =  0 ; v  < n_dependent_vars ; ++v ) 
		{
		  const struct variable *var = dependent_vars[v];
		  const union value *val = case_data (&c, var->fv);

		  metrics_calc( &(*foo)->stats[v], val, weight );
		}

	      if ( fctr->subfactor  ) 
		{
		  struct factor *sfctr  = fctr->subfactor;

		  const union value *ii_val = 
		    case_data (&c, sfctr->indep_var->fv);

		  struct factor_statistics **bar = 
		    (struct factor_statistics **)
		    hsh_probe(sfctr->hash_table_val, (void *) &ii_val);

		  if ( !*bar ) 
		    {
		      *bar = xmalloc ( sizeof ( struct factor_statistics));
		      (*bar)->id = ii_val;
		      (*bar)->stats = xmalloc ( sizeof ( struct metrics ) 
						* n_dependent_vars);
		  
		      for ( v =  0 ; v  < n_dependent_vars ; ++v ) 
			metrics_precalc( &(*bar)->stats[v] );

		      hsh_insert(sfctr->hash_table_val, 
				 (void *) *bar);
		    }

		  for ( v =  0 ; v  < n_dependent_vars ; ++v ) 
		    {
		      const struct variable *var = dependent_vars[v];
		      const union value *val = case_data (&c, var->fv);

		      metrics_calc( &(*bar)->stats[v], val, weight );
		    }
		}
	    }
	}

    }

  for ( v = 0 ; v < n_dependent_vars ; ++v)
    {
      if ( hash_table_factors ) 
	{
	for ( fctr = hsh_first(hash_table_factors, &hi);
	      fctr != 0;
	      fctr = hsh_next (hash_table_factors, &hi) )
	  {
	    struct hsh_iterator h2;
	    struct factor_statistics *fs;

	    for ( fs = hsh_first(fctr->hash_table_val,&h2);
		  fs != 0;
		  fs = hsh_next(fctr->hash_table_val,&h2))
	      {
		metrics_postcalc( &fs->stats[v] );
	      }

	    if ( fctr->subfactor) 
	      {
		struct hsh_iterator hsf;
		struct factor_statistics *fss;

		for ( fss = hsh_first(fctr->subfactor->hash_table_val,&hsf);
		      fss != 0;
		      fss = hsh_next(fctr->subfactor->hash_table_val,&hsf))
		  {
		    metrics_postcalc( &fss->stats[v] );
		  }
	      }
	  }
	}

      metrics_postcalc(&totals->stats[v]);
    }

  output_examine();

}


static void 
show_extremes(struct variable **dependent_var, 
	      int n_dep_var, 
	      struct factor *factor,
	      int n_extremities)
{
  int i;
  int heading_columns ;
  int n_cols;
  const int heading_rows = 1;
  struct tab_table *t;

  int n_rows = heading_rows;

  if ( !factor ) 
    {
      heading_columns = 1 + 1;
      n_rows += n_dep_var * 2 * n_extremities;
    }
  else
    {
      assert(factor->indep_var);
      if ( factor->subfactor == 0 ) 
	{
	  heading_columns = 2 + 1;
	  n_rows += n_dep_var * 2 * n_extremities 
	    * hsh_count(factor->hash_table_val);
	}
      else
	{
	  heading_columns = 3 + 1;
	  n_rows += n_dep_var * 2 * n_extremities 
	    * hsh_count(factor->hash_table_val)
	    * hsh_count(factor->subfactor->hash_table_val) ;
	}
    }


  n_cols = heading_columns + 3;

  t = tab_create (n_cols,n_rows,0);
  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_dim (t, tab_natural_dimensions);
  
  /* Outline the box and have vertical internal lines*/
  tab_box (t, 
	   TAL_2, TAL_2,
	   -1, TAL_1,
	   0, 0,
	   n_cols - 1, n_rows - 1);



  tab_hline (t, TAL_2, 0, n_cols - 1, heading_rows );

  tab_title (t, 0, _("Extreme Values"));




  /* Remove lines ... */
  tab_box (t, 
	   -1, -1,
	   TAL_0, TAL_0,
	   heading_columns, 0,
	   n_cols - 1, 0);

  if ( factor ) 
    {
      tab_text (t, 1, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		var_to_string(factor->indep_var));

      if ( factor->subfactor ) 
	tab_text (t, 2, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		  var_to_string(factor->subfactor->indep_var));
    }

  tab_text (t, n_cols - 1, 0, TAB_CENTER | TAT_TITLE, _("Value"));
  tab_text (t, n_cols - 2, 0, TAB_CENTER | TAT_TITLE, _("Case Number"));


  for ( i = 0 ; i < n_dep_var ; ++i ) 
    {
      int n_subfactors = 1;
      int n_factors = 1;
	
      if ( factor ) 
	{
	  n_factors = hsh_count(factor->hash_table_val);
	  if (  factor->subfactor ) 
	    n_subfactors = hsh_count(factor->subfactor->hash_table_val);
	}

      tab_text (t, 
		0, i * 2 * n_extremities * n_factors * 
		n_subfactors + heading_rows,
		TAB_LEFT | TAT_TITLE, 
		var_to_string(dependent_var[i])
		);


      if ( i > 0 ) 
	tab_hline (t, 
		   TAL_1, 0, n_cols - 1,  
		   heading_rows + 2 * n_extremities * 
		   (i * n_factors * n_subfactors )
		   );

      if ( factor  )
	{
	  struct hsh_iterator hi;
	  const struct factor_statistics *fs;
	  int count = 0;

	  for ( fs  = hsh_first(factor->hash_table_val, &hi);
	        fs != 0;
		fs  = hsh_next(factor->hash_table_val,  &hi))
	    {
	      tab_text (t, 1, heading_rows + 2 * n_extremities * 
			(i * n_factors * n_subfactors 
			 + count * n_subfactors),
			TAB_RIGHT | TAT_TITLE, 
			value_to_string(fs->id, factor->indep_var)
			);

	      if ( count > 0 ) 
		tab_hline (t, TAL_1, 1, n_cols - 1,  
			   heading_rows + 2 * n_extremities *
			   (i * n_factors * n_subfactors 
			    + count * n_subfactors));


	      if ( factor->subfactor ) 
		{
		  struct hsh_iterator h2;
		  const struct factor_statistics *sub_fs;
		  int count2=0;

		  for ( sub_fs = hsh_first(factor->subfactor->hash_table_val, 
					   &h2);
			sub_fs != 0;
			sub_fs = hsh_next(factor->subfactor->hash_table_val, 
					  &h2))
		    {
			
		      tab_text(t, 2, heading_rows + 2 * n_extremities *
			       (i * n_factors * n_subfactors 
				+ count * n_subfactors + count2 ),
			       TAB_RIGHT | TAT_TITLE ,
			       value_to_string(sub_fs->id, 
					       factor->subfactor->indep_var)
			       );


		      if ( count2 > 0 ) 
			tab_hline (t, TAL_1, 2, n_cols - 1,  
				   heading_rows + 2 * n_extremities *
				   (i * n_factors * n_subfactors 
				    + count * n_subfactors + count2 ));

		      populate_extremities(t,3, 
					   heading_rows + 2 * n_extremities *
					   (i * n_factors * n_subfactors 
					    + count * n_subfactors + count2),
					   n_extremities );
					   
		      count2++;
		    }
		}
	      else
		{
		  populate_extremities(t,2, 
				       heading_rows + 2 * n_extremities *
				       (i * n_factors * n_subfactors 
					+ count * n_subfactors),
				       n_extremities);
		}

	      count ++;
	    }
	}
      else
	{
	  populate_extremities(t, 1, 
			       heading_rows + 2 * n_extremities *
			       (i * n_factors * n_subfactors ),
			       n_extremities);

	}
    }

  tab_submit (t);
}



/* Fill in the extremities table */
void 
populate_extremities(struct tab_table *t, int col, int row, int n)
{
  int i;

  tab_text(t, col, row,
	   TAB_RIGHT | TAT_TITLE ,
	   _("Highest")
	   );

  tab_text(t, col, row + n ,
	   TAB_RIGHT | TAT_TITLE ,
	   _("Lowest")
	   );

  for (i = 0; i < n ; ++i ) 
    {
      tab_float(t, col + 1, row + i,
		TAB_RIGHT,
		i + 1, 8, 0);

      tab_float(t, col + 1, row + i + n,
		TAB_RIGHT,
		i + 1, 8, 0);
    }
}



/* Plot the normal and detrended normal plots for m
   Label the plots with factorname */
void
np_plot(const struct metrics *m, const char *factorname)
{
  int i;
  double yfirst=0, ylast=0;

  /* Normal Plot */
  struct chart np_chart;

  /* Detrended Normal Plot */
  struct chart dnp_chart;

  const struct weighted_value *wv = m->wv;
  const int n_data = hsh_count(m->ordered_data) ; 

  /* The slope and intercept of the ideal normal probability line */
  const double slope = 1.0 / m->stddev;
  const double intercept = - m->mean / m->stddev;

  chart_initialise(&np_chart);
  chart_write_title(&np_chart, _("Normal Q-Q Plot of %s"), factorname);
  chart_write_xlabel(&np_chart, _("Observed Value"));
  chart_write_ylabel(&np_chart, _("Expected Normal"));

  chart_initialise(&dnp_chart);
  chart_write_title(&dnp_chart, _("Detrended Normal Q-Q Plot of %s"), 
		    factorname);
  chart_write_xlabel(&dnp_chart, _("Observed Value"));
  chart_write_ylabel(&dnp_chart, _("Dev from Normal"));

  yfirst = gsl_cdf_ugaussian_Pinv (wv[0].rank / ( m->n + 1));
  ylast =  gsl_cdf_ugaussian_Pinv (wv[n_data-1].rank / ( m->n + 1));

  {
    /* Need to make sure that both the scatter plot and the ideal fit into the
       plot */
    double x_lower = min(m->min, (yfirst - intercept) / slope) ;
    double x_upper = max(m->max, (ylast  - intercept) / slope) ;
    double slack = (x_upper - x_lower)  * 0.05 ;

    chart_write_xscale(&np_chart, x_lower  - slack, x_upper + slack,
		       chart_rounded_tick((m->max - m->min) / 5.0));


    chart_write_xscale(&dnp_chart, m->min, m->max,
		       chart_rounded_tick((m->max - m->min) / 5.0));

  }

  chart_write_yscale(&np_chart, yfirst, ylast, 
		     chart_rounded_tick((ylast - yfirst)/5.0) );

  {
  /* We have to cache the detrended data, beacause we need to 
     find its limits before we can plot it */
  double *d_data;
  d_data = xmalloc (n_data * sizeof(double));
  double d_max = -DBL_MAX;
  double d_min = DBL_MAX;
  for ( i = 0 ; i < n_data; ++i ) 
    {
      const double ns = gsl_cdf_ugaussian_Pinv (wv[i].rank / ( m->n + 1));

      chart_datum(&np_chart, 0, wv[i].v.f, ns);

      d_data[i] = (wv[i].v.f - m->mean) / m->stddev  - ns;
   
      if ( d_data[i] < d_min ) d_min = d_data[i];
      if ( d_data[i] > d_max ) d_max = d_data[i];
    }

  chart_write_yscale(&dnp_chart, d_min, d_max, 
		     chart_rounded_tick((d_max - d_min) / 5.0));

  for ( i = 0 ; i < n_data; ++i ) 
      chart_datum(&dnp_chart, 0, wv[i].v.f, d_data[i]);

  free(d_data);
  }

  chart_line(&np_chart, slope, intercept, yfirst, ylast , CHART_DIM_Y);
  chart_line(&dnp_chart, 0, 0, m->min, m->max , CHART_DIM_X);

  chart_finalise(&np_chart);
  chart_finalise(&dnp_chart);

}
