/* PSPP - EXAMINE data for normality . -*-c-*-

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

/* (specification)
   "EXAMINE" (xmn_):
   *variables=custom;
   +total=custom;
   +nototal=custom;
   +missing=miss:pairwise/!listwise,
   rep:report/!noreport,
   incl:include/!exclude;
   +compare=cmp:variables/!groups;
   +cinterval=double;
   +statistics[st_]=descriptives,:extreme(*d:n),all,none.
*/

/* (declarations) */

/* (functions) */


static struct cmd_examine cmd;

static struct variable **dependent_vars;

static int n_dependent_vars;

static struct hsh_table *hash_table_factors;


struct factor 
{
  struct variable *v1;
  struct hsh_table *hash_table_v1;

  struct variable *v2;
  struct hsh_table *hash_table_v2;
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


/* Calculations */
static void calculate(const struct casefile *cf, void *cmd_);


int
cmd_examine(void)
{
  int i;
  short total=1;



  if ( !parse_examine(&cmd) )
    return CMD_FAILURE;
  
  if ( cmd.st_n == SYSMIS ) 
    cmd.st_n = 5;

  if ( ! cmd.sbc_cinterval) 
    cmd.n_cinterval[0] = 95.0;

  if ( cmd.sbc_nototal ) 
    total = 0;


  multipass_procedure_with_splits (calculate, &cmd);

  /* Show totals if appropriate */
  if ( total || !hash_table_factors || 0 == hsh_count (hash_table_factors))
    {
      show_summary(dependent_vars, n_dependent_vars,0);

      if ( cmd.sbc_statistics ) 
	{
	  if ( cmd.a_statistics[XMN_ST_DESCRIPTIVES]) 
	    show_descriptives(dependent_vars, n_dependent_vars, 0);
	  
	  if ( cmd.a_statistics[XMN_ST_EXTREME]) 
	    show_extremes(dependent_vars, n_dependent_vars, 0, cmd.st_n);
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
		show_descriptives(dependent_vars, n_dependent_vars,f);
	      
	      if ( cmd.a_statistics[XMN_ST_EXTREME])
		show_extremes(dependent_vars, n_dependent_vars,f,cmd.st_n);
	    }
	}
    }

  hsh_destroy(hash_table_factors);

  return CMD_SUCCESS;
};


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
		 void *aux UNUSED)
{
  int v1_cmp;

  v1_cmp = strcmp(f1->v1->name, f2->v1->name);

  if ( 0 != v1_cmp ) 
    return v1_cmp;

  if ( f1->v2 == 0 && f2->v2 == 0 ) 
    return 0;

  if ( f1->v2 == 0 && f2->v2 != 0 ) 
    return -1;

  if ( f1->v2 != 0 && f2->v2 == 0 ) 
    return +1;

  return strcmp(f1->v2->name, f2->v2->name);

}

/* Create a hash of a factor */
unsigned 
hash_factor( const struct factor *f, 
	     void *aux UNUSED)
{
  unsigned h;
  h = hsh_hash_string(f->v1->name);
  
  if ( f->v2 ) 
    h += hsh_hash_string(f->v2->name);

  return h;
}


/* Free up a factor */
void
free_factor(struct factor *f, void *aux UNUSED)
{
  hsh_destroy(f->hash_table_v1);
  hsh_destroy(f->hash_table_v2);

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
      f->v2 = 0;
      f->v1 = 0;
      f->hash_table_v2 = 0;
      f->hash_table_v1 = 0;
    }
  
  f->v1 = parse_variable();
  
  if ( ! f->hash_table_v1 ) 
    f->hash_table_v1 = hsh_create(4,(hsh_compare_func *)compare_values,
				  (hsh_hash_func *)hash_value,
				  0,(void *) f->v1->width);

  if ( token == T_BY ) 
    {
      lex_match(T_BY);
      if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
	  && token != T_ALL)
	return 2;

      f->v2 = parse_variable();
      
      if ( !f->hash_table_v2 ) 
	{
	  f->hash_table_v2 = hsh_create(4,
					(hsh_compare_func *) compare_values,
					(hsh_hash_func *) hash_value,
					0, 
					(void *) f->v2->width);
	}
    }

  hsh_insert(hash_table_factors, f);
  
  lex_match(',');

  if ( token == '.' || token == '/' ) 
    return 1;

  return examine_parse_independent_vars(cmd, hash_table_factors);
}


void populate_descriptives(struct tab_table *t, int col, int row);


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
      assert(factor->v1);
      if ( factor->v2 == 0 ) 
	{
	  heading_columns = 2;
	  n_rows += n_dep_var * hsh_count(factor->hash_table_v1) * n_stat_rows;
	}
      else
	{
	  heading_columns = 3;
	  n_rows += n_dep_var * hsh_count(factor->hash_table_v1) * 
	    hsh_count(factor->hash_table_v2) * n_stat_rows ;
	}
    }

  n_cols = heading_columns + 4;

  t = tab_create (n_cols, n_rows, 0);

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_dim (t, tab_natural_dimensions);

  /* Outline the box and have no internal lines*/
  tab_box (t, 
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_hline (t, TAL_2, 0, n_cols - 1, heading_rows );

  tab_vline (t, TAL_2, heading_columns, 0, n_rows - 1);
  tab_vline (t, TAL_1, n_cols - 2, 0, n_rows - 1);
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
	  n_factors = hsh_count(factor->hash_table_v1);
	  if (  factor->v2 ) 
	    n_subfactors = hsh_count(factor->hash_table_v2);
	}


      row = heading_rows + i * n_stat_rows * n_factors * n_subfactors; 

      if ( i > 0 )
	tab_hline(t, TAL_1, 0, n_cols - 1, row );



      if ( factor  )
	{
	  struct hsh_iterator hi;
	  union value *v;
	  int count = 0;

      tab_text (t, 1, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		var_to_string(factor->v1));



	  for ( v  = hsh_first(factor->hash_table_v1, &hi);
		v != 0;
		v  = hsh_next(factor->hash_table_v1,  &hi))
	    {
	      struct hsh_iterator h2;
	      union value *vv;
		
	      tab_text (t, 1, 
			row  + count * n_subfactors * n_stat_rows,
			TAB_RIGHT | TAT_TITLE, 
			value_to_string(v, factor->v1)
			);

	      if ( count > 0 ) 
		tab_hline (t, TAL_1, 1, n_cols - 1,  
			   row  + count * n_subfactors * n_stat_rows);

	      if ( factor->v2 ) 
		{
		  int count2=0;

		  tab_text (t, 2, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
			    var_to_string(factor->v2));

		  for ( vv = hsh_first(factor->hash_table_v2, &h2);
			vv != 0;
			vv = hsh_next(factor->hash_table_v2, &h2))
		    {
			
		      tab_text(t, 2, 
			       row
			       + count * n_subfactors * n_stat_rows 
			       + count2 * n_stat_rows,
			       TAB_RIGHT | TAT_TITLE ,
			       value_to_string(vv, factor->v2)
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
					    + count2 * n_stat_rows);
					    
			
		      count2++;
		    }
		}
	      else
		{
		  populate_descriptives(t, heading_columns, 
					row  
					+ count * n_subfactors * n_stat_rows);
		}

	      count ++;
	    }
	}
      else
	{
	  populate_descriptives(t, heading_columns, 
				row);
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
populate_descriptives(struct tab_table *t, int col, int row)
{

  tab_text (t, col, 
	    row,
	    TAB_LEFT | TAT_TITLE,
	    _("Mean"));


  tab_text (t, col, 
	    row + 1,
	    TAB_LEFT | TAT_TITLE | TAT_PRINTF,
	    _("%g%% Confidence Interval for Mean"), cmd.n_cinterval[0]);

  tab_text (t, col + 1,  
	    row + 1,
	    TAB_LEFT | TAT_TITLE,
	    _("Upper Bound"));

  tab_text (t, col + 1, 
	    row  + 2,
	    TAB_LEFT | TAT_TITLE,
	    _("Lower Bound"));


  tab_text (t, col, 
	    row + 3,
	    TAB_LEFT | TAT_TITLE,
	    _("5% Trimmed Mean"));

  tab_text (t, col, 
	    row + 4,
	    TAB_LEFT | TAT_TITLE,
	    _("Median"));

  tab_text (t, col, 
	    row + 5,
	    TAB_LEFT | TAT_TITLE,
	    _("Variance"));

  tab_text (t, col, 
	    row + 6,
	    TAB_LEFT | TAT_TITLE,
	    _("Std. Deviation"));

  tab_text (t, col, 
	    row + 7,
	    TAB_LEFT | TAT_TITLE,
	    _("Minimum"));

  tab_text (t, col, 
	    row + 8,
	    TAB_LEFT | TAT_TITLE,
	    _("Maximum"));

  tab_text (t, col, 
	    row + 9,
	    TAB_LEFT | TAT_TITLE,
	    _("Range"));

  tab_text (t, col, 
	    row + 10,
	    TAB_LEFT | TAT_TITLE,
	    _("Interquartile Range"));

  tab_text (t, col, 
	    row + 11,
	    TAB_LEFT | TAT_TITLE,
	    _("Skewness"));

  tab_text (t, col, 
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
  struct tab_table *t;

  int n_rows = heading_rows;

  if ( !factor ) 
    {
      heading_columns = 1;
      n_rows += n_dep_var;
    }
  else
    {
      assert(factor->v1);
      if ( factor->v2 == 0 ) 
	{
	  heading_columns = 2;
	  n_rows += n_dep_var * hsh_count(factor->hash_table_v1);
	}
      else
	{
	  heading_columns = 3;
	  n_rows += n_dep_var * hsh_count(factor->hash_table_v1) * 
	    hsh_count(factor->hash_table_v2) ;
	}
    }


  n_cols = heading_columns + 6;

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
  tab_hline (t, TAL_1, heading_columns, n_cols - 1, 1 );
  tab_hline (t, TAL_1, 0, n_cols - 1, heading_rows -1 );

  tab_vline (t, TAL_2, heading_columns, 0, n_rows - 1);


  tab_title (t, 0, _("Case Processing Summary"));
  

  tab_joint_text(t, heading_columns, 0, 
		 n_cols -1, 0,
		 TAB_CENTER | TAT_TITLE,
		 _("Cases"));

  /* Remove lines ... */
  tab_box (t, 
	   -1, -1,
	   TAL_0, TAL_0,
	   heading_columns, 0,
	   n_cols - 1, 0);

  if ( factor ) 
    {
      tab_text (t, 1, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		var_to_string(factor->v1));

      if ( factor->v2 ) 
	tab_text (t, 2, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		  var_to_string(factor->v2));
    }

  for ( i = 0 ; i < 3 ; ++i ) 
    {
      tab_text (t, heading_columns + i*2 , 2, TAB_CENTER | TAT_TITLE, _("N"));
      tab_text (t, heading_columns + i*2 + 1, 2, TAB_CENTER | TAT_TITLE, 
		_("Percent"));

      tab_joint_text(t, heading_columns + i*2 , 1,
		     heading_columns + i*2 + 1, 1,
		     TAB_CENTER | TAT_TITLE,
		     subtitle[i]);

      tab_box (t, -1, -1,
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
	  n_factors = hsh_count(factor->hash_table_v1);
	  if (  factor->v2 ) 
	    n_subfactors = hsh_count(factor->hash_table_v2);
	}

      tab_text (t, 
		0, i * n_factors * n_subfactors + heading_rows,
		TAB_LEFT | TAT_TITLE, 
		var_to_string(dependent_var[i])
		);

      if ( factor  )
	{
	  struct hsh_iterator hi;
	  union value *v;
	  int count = 0;

	  for ( v  = hsh_first(factor->hash_table_v1, &hi);
		v != 0;
		v  = hsh_next(factor->hash_table_v1,  &hi))
	    {
	      struct hsh_iterator h2;
	      union value *vv;
		
	      tab_text (t, 1, 
			i * n_factors * n_subfactors + heading_rows
			+ count * n_subfactors,
			TAB_RIGHT | TAT_TITLE, 
			value_to_string(v, factor->v1)
			);

	      if ( factor->v2 ) 
		{
		  int count2=0;
		  for ( vv = hsh_first(factor->hash_table_v2, &h2);
			vv != 0;
			vv = hsh_next(factor->hash_table_v2, &h2))
		    {
			
		      tab_text(t, 2, 
			       i * n_factors * n_subfactors + heading_rows
			       + count * n_subfactors + count2,
			       TAB_RIGHT | TAT_TITLE ,
			       value_to_string(vv, factor->v2)
			       );
			
		      count2++;
		    }
		}
	      count ++;
	    }
	}
    }


  tab_submit (t);
  
}



static int bad_weight_warn = 1;

static void 
calculate(const struct casefile *cf, void *cmd_)
{
  struct casereader *r;
  struct ccase c;

  struct cmd_examine *cmd = (struct cmd_examine *) cmd_;

  for(r = casefile_get_reader (cf);
      casereader_read (r, &c) ;
      case_destroy (&c)) 
    {
      int i;
      struct hsh_iterator hi;
      struct factor *fctr;

      const double weight = 
	dict_get_case_weight(default_dict,&c,&bad_weight_warn);

      if ( hash_table_factors ) 
	{
	  for ( fctr = hsh_first(hash_table_factors, &hi);
		fctr != 0;
		fctr = hsh_next (hash_table_factors, &hi) )
	    {
	      union value *val;


	      val = case_data (&c, fctr->v1->fv);
	      hsh_insert(fctr->hash_table_v1,val);

	      if ( fctr->hash_table_v2  ) 
		{
		  val = case_data (&c, fctr->v2->fv);
		  hsh_insert(fctr->hash_table_v2,val);
		}
	    }
	}

    }
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
      assert(factor->v1);
      if ( factor->v2 == 0 ) 
	{
	  heading_columns = 2 + 1;
	  n_rows += n_dep_var * 2 * n_extremities 
	    * hsh_count(factor->hash_table_v1);
	}
      else
	{
	  heading_columns = 3 + 1;
	  n_rows += n_dep_var * 2 * n_extremities 
	    * hsh_count(factor->hash_table_v1)
	    * hsh_count(factor->hash_table_v2) ;
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
		var_to_string(factor->v1));

      if ( factor->v2 ) 
	tab_text (t, 2, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		  var_to_string(factor->v2));
    }

  tab_text (t, n_cols - 1, 0, TAB_CENTER | TAT_TITLE, _("Value"));
  tab_text (t, n_cols - 2, 0, TAB_CENTER | TAT_TITLE, _("Case Number"));


  for ( i = 0 ; i < n_dep_var ; ++i ) 
    {
      int n_subfactors = 1;
      int n_factors = 1;
	
      if ( factor ) 
	{
	  n_factors = hsh_count(factor->hash_table_v1);
	  if (  factor->v2 ) 
	    n_subfactors = hsh_count(factor->hash_table_v2);
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
	  union value *v;
	  int count = 0;

	  for ( v  = hsh_first(factor->hash_table_v1, &hi);
		v != 0;
		v  = hsh_next(factor->hash_table_v1,  &hi))
	    {
	      struct hsh_iterator h2;
	      union value *vv;
		
	      tab_text (t, 1, heading_rows + 2 * n_extremities * 
			(i * n_factors * n_subfactors 
			 + count * n_subfactors),
			TAB_RIGHT | TAT_TITLE, 
			value_to_string(v, factor->v1)
			);

	      if ( count > 0 ) 
		tab_hline (t, TAL_1, 1, n_cols - 1,  
			   heading_rows + 2 * n_extremities *
			   (i * n_factors * n_subfactors 
			    + count * n_subfactors));


	      if ( factor->v2 ) 
		{
		  int count2=0;
		  for ( vv = hsh_first(factor->hash_table_v2, &h2);
			vv != 0;
			vv = hsh_next(factor->hash_table_v2, &h2))
		    {
			
		      tab_text(t, 2, heading_rows + 2 * n_extremities *
			       (i * n_factors * n_subfactors 
				+ count * n_subfactors + count2 ),
			       TAB_RIGHT | TAT_TITLE ,
			       value_to_string(vv, factor->v2)
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
		TAB_RIGHT | TAT_TITLE,
		i + 1, 8, 0);

      tab_float(t, col + 1, row + i + n,
		TAB_RIGHT | TAT_TITLE,
		i + 1, 8, 0);
    }
}
