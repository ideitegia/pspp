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


struct factor 
{
  /* The independent variable */
  struct variable *indep_var[2];


  /* Hash table of factor stats indexed by 2 values */
  struct hsh_table *fstats;

  /* The hash table after it has been crunched */
  struct factor_statistics **fs;

  struct factor *next;

};

/* Linked list of factors */
static struct factor *factors=0;

static struct metrics *totals=0;

void
print_factors(void)
{
  struct factor *f = factors;

  while (f) 
    {
      struct  factor_statistics **fs = f->fs;

      printf("Factor: %s BY %s\n", 
	     var_to_string(f->indep_var[0]),
	     var_to_string(f->indep_var[1]) );


      printf("Contains %d entries\n", hsh_count(f->fstats));

      
      while (*fs) 
	{
	  printf("Factor %g; %g\n", (*fs)->id[0].f, (*fs)->id[1].f);
	  
	  /* 
	     printf("Factor %s; %s\n",
	     value_to_string(&(*fs)->id[0], f->indep_var[0]),
	     value_to_string(&(*fs)->id[1], f->indep_var[1]));
	  */

		 
	  printf("Sum is %g; ",(*fs)->m[0].sum);
	  printf("N is %g; ",(*fs)->m[0].n);
	  printf("Mean is %g\n",(*fs)->m[0].mean);

	  fs++ ;
	}

      f = f->next;
    }

  
}


/* Parse the clause specifying the factors */
static int examine_parse_independent_vars(struct cmd_examine *cmd);



/* Output functions */
static void show_summary(struct variable **dependent_var, int n_dep_var, 
			 const struct factor *f);

static void show_extremes(struct variable **dependent_var, 
			  int n_dep_var, 
			  const struct factor *factor,
			  int n_extremities);

static void show_descriptives(struct variable **dependent_var, 
			      int n_dep_var, 
			      struct factor *factor);


void np_plot(const struct metrics *m, const char *factorname);




/* Per Split function */
static void run_examine(const struct casefile *cf, void *cmd_);

static void output_examine(void);


void factor_calc(struct ccase *c, int case_no, 
		 double weight, int case_missing);


/* Function to use for testing for missing values */
static is_missing_func value_is_missing;


int
cmd_examine(void)
{

  if ( !parse_examine(&cmd) )
    return CMD_FAILURE;

  /* If /MISSING=INCLUDE is set, then user missing values are ignored */
  if (cmd.incl == XMN_INCLUDE ) 
    value_is_missing = is_system_missing;
  else
    value_is_missing = is_missing;

  if ( cmd.st_n == SYSMIS ) 
    cmd.st_n = 5;

  if ( ! cmd.sbc_cinterval) 
    cmd.n_cinterval[0] = 95.0;

  multipass_procedure_with_splits (run_examine, &cmd);

  if ( totals ) 
    free(totals);

  return CMD_SUCCESS;
};



/* Show all the appropriate tables */
static void
output_examine(void)
{
  struct factor *fctr;

  /* Show totals if appropriate */
  if ( ! cmd.sbc_nototal || factors == 0 )
    {
      show_summary(dependent_vars, n_dependent_vars, 0);

      if ( cmd.sbc_statistics ) 
	{
	  if ( cmd.a_statistics[XMN_ST_EXTREME]) 
	    show_extremes(dependent_vars, n_dependent_vars, 0, cmd.st_n);

	  if ( cmd.a_statistics[XMN_ST_DESCRIPTIVES]) 
	    show_descriptives(dependent_vars, n_dependent_vars, 0);

	}

      if ( cmd.sbc_plot) 
	{
	  if ( cmd.a_plot[XMN_PLT_NPPLOT] ) 
	    {
	      int v;

	      for ( v = 0 ; v < n_dependent_vars; ++v ) 
		  np_plot(&totals[v], var_to_string(dependent_vars[v]));
	    }
	}


    }


  /* Show grouped statistics  as appropriate */
  fctr = factors;
  while ( fctr ) 
    {
      show_summary(dependent_vars, n_dependent_vars, fctr);

      if ( cmd.sbc_statistics ) 
	{
	  if ( cmd.a_statistics[XMN_ST_EXTREME]) 
	    show_extremes(dependent_vars, n_dependent_vars, fctr, cmd.st_n);

	  if ( cmd.a_statistics[XMN_ST_DESCRIPTIVES]) 
	    show_descriptives(dependent_vars, n_dependent_vars, fctr);
	}

      if ( cmd.sbc_plot) 
	{
	  if ( cmd.a_plot[XMN_PLT_NPPLOT] ) 
	    {
	      int v;
	      for ( v = 0 ; v < n_dependent_vars; ++ v)
		{
		  
		  struct factor_statistics **fs = fctr->fs ;
		  for ( fs = fctr->fs ; *fs ; ++fs ) 
		    {
		      char buf1[100];
		      char buf2[100];
		      sprintf(buf1, "%s (",
			      var_to_string(dependent_vars[v]));
		      
		      sprintf(buf2, "%s = %s",
			     var_to_string(fctr->indep_var[0]),
			     value_to_string(&(*fs)->id[0],fctr->indep_var[0]));
		      
		      strcat(buf1, buf2);

		      
		      if ( fctr->indep_var[1] ) 
			{
			  sprintf(buf2, "; %s = %s)",
				  var_to_string(fctr->indep_var[1]),
				  value_to_string(&(*fs)->id[1],
						  fctr->indep_var[1]));
			  strcat(buf1, buf2);
			}
		      else
			{
			  strcat(buf1, ")");
			}

		      np_plot(&(*fs)->m[v],buf1);

		    }
		  
		}

	    }
	}

      fctr = fctr->next;
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

  totals = xmalloc( sizeof(struct metrics) * n_dependent_vars);

  if ( lex_match(T_BY))
    {
      return examine_parse_independent_vars(cmd);
    }

  return 1;
}



/* Parse the clause specifying the factors */
static int
examine_parse_independent_vars(struct cmd_examine *cmd)
{

  struct factor *sf = xmalloc(sizeof(struct factor));

  if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
      && token != T_ALL)
    return 2;


  sf->indep_var[0] = parse_variable();
  sf->indep_var[1] = 0;

  if ( token == T_BY ) 
    {

      lex_match(T_BY);

      if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
	  && token != T_ALL)
	return 2;

      sf->indep_var[1] = parse_variable();

    }


  sf->fstats = hsh_create(4,
			  (hsh_compare_func *) factor_statistics_compare,
			  (hsh_hash_func *) factor_statistics_hash,
			  (hsh_free_func *) factor_statistics_free,
			  0);

  sf->next = factors;
  factors = sf;
  
  lex_match(',');

  if ( token == '.' || token == '/' ) 
    return 1;

  return examine_parse_independent_vars(cmd);
}




void populate_descriptives(struct tab_table *t, int col, int row, 
			   const struct metrics *fs);

void populate_extremes(struct tab_table *t, int col, int row, int n, 
		       const struct metrics *m);

void populate_summary(struct tab_table *t, int col, int row,
		      const struct metrics *m);




static int bad_weight_warn = 1;


/* Perform calculations for the sub factors */
void
factor_calc(struct ccase *c, int case_no, double weight, int case_missing)
{
  int v;
  struct factor *fctr = factors;

  while ( fctr) 
    {
      union value indep_vals[2] ;

      indep_vals[0] = * case_data(c, fctr->indep_var[0]->fv);

      if ( fctr->indep_var[1] ) 
	indep_vals[1] = * case_data(c, fctr->indep_var[1]->fv);
      else
	indep_vals[1].f = SYSMIS;

      assert(fctr->fstats);

      struct factor_statistics **foo = ( struct factor_statistics ** ) 
	hsh_probe(fctr->fstats, (void *) &indep_vals);

      if ( !*foo ) 
	{

	  *foo = create_factor_statistics(n_dependent_vars, 
					  &indep_vals[0],
					  &indep_vals[1]);

	  for ( v =  0 ; v  < n_dependent_vars ; ++v ) 
	    {
	      metrics_precalc( &(*foo)->m[v] );
	    }

	}

      for ( v =  0 ; v  < n_dependent_vars ; ++v ) 
	{
	  const struct variable *var = dependent_vars[v];
	  const union value *val = case_data (c, var->fv);

	  if ( value_is_missing(val,var) || case_missing ) 
	    val = 0;

	  metrics_calc( &(*foo)->m[v], val, weight, case_no );
	}

      fctr = fctr->next;
    }


}




static void 
run_examine(const struct casefile *cf, void *cmd_ )
{
  struct casereader *r;
  struct ccase c;
  int v;

  const struct cmd_examine *cmd = (struct cmd_examine *) cmd_;

  /* Make sure we haven't got rubbish left over from a 
     previous split */

  struct factor *fctr = factors;
  while (fctr) 
    {
      struct factor *next = fctr->next;

      hsh_clear(fctr->fstats);

      fctr->fs = 0;

      fctr = next;
    }



  for ( v = 0 ; v < n_dependent_vars ; ++v ) 
    metrics_precalc(&totals[v]);

  for(r = casefile_get_reader (cf);
      casereader_read (r, &c) ;
      case_destroy (&c) ) 
    {
      int case_missing=0;
      const int case_no = casereader_cnum(r);

      const double weight = 
	dict_get_case_weight(default_dict, &c, &bad_weight_warn);

      if ( cmd->miss == XMN_LISTWISE ) 
	{
	  for ( v = 0 ; v < n_dependent_vars ; ++v ) 
	    {
	      const struct variable *var = dependent_vars[v];
	      const union value *val = case_data (&c, var->fv);

	      if ( value_is_missing(val,var))
		case_missing = 1;
		   
	    }
	}

      for ( v = 0 ; v < n_dependent_vars ; ++v ) 
	{
	  const struct variable *var = dependent_vars[v];
	  const union value *val = case_data (&c, var->fv);

	  if ( value_is_missing(val,var) || case_missing ) 
	    val = 0;

	  metrics_calc(&totals[v], val, weight, case_no );
    
	}

      factor_calc(&c, case_no, weight, case_missing);

    }


  for ( v = 0 ; v < n_dependent_vars ; ++v)
    {
      fctr = factors;
      while ( fctr ) 
	{
	  struct hsh_iterator hi;
	  struct factor_statistics *fs;

	  for ( fs = hsh_first(fctr->fstats, &hi);
		fs != 0 ;
		fs = hsh_next(fctr->fstats, &hi))
	    {
	      metrics_postcalc(&fs->m[v]);
	    }

	  fctr = fctr->next;
	}
      metrics_postcalc(&totals[v]);
    }


  /* Make sure that the combination of factors are complete */

  fctr = factors;
  while ( fctr ) 
    {
      struct hsh_iterator hi;
      struct hsh_iterator hi0;
      struct hsh_iterator hi1;
      struct factor_statistics *fs;

      struct hsh_table *idh0=0;
      struct hsh_table *idh1=0;
      union value *val0;
      union value *val1;
	  
      idh0 = hsh_create(4, (hsh_compare_func *) compare_values,
			(hsh_hash_func *) hash_value,
			0,0);

      idh1 = hsh_create(4, (hsh_compare_func *) compare_values,
			(hsh_hash_func *) hash_value,
			0,0);


      for ( fs = hsh_first(fctr->fstats, &hi);
	    fs != 0 ;
	    fs = hsh_next(fctr->fstats, &hi))
	{
	  hsh_insert(idh0,(void *) &fs->id[0]);
	  hsh_insert(idh1,(void *) &fs->id[1]);
	}

      /* Ensure that the factors combination is complete */
      for ( val0 = hsh_first(idh0, &hi0);
	    val0 != 0 ;
	    val0 = hsh_next(idh0, &hi0))
	{
	  for ( val1 = hsh_first(idh1, &hi1);
		val1 != 0 ;
		val1 = hsh_next(idh1, &hi1))
	    {
	      struct factor_statistics **ffs;
	      union value key[2];
	      key[0] = *val0;
	      key[1] = *val1;
		  
	      ffs = (struct factor_statistics **) 
		hsh_probe(fctr->fstats, (void *) &key );

	      if ( !*ffs ) {
		int i;
		(*ffs) = create_factor_statistics (n_dependent_vars,
						   &key[0], &key[1]);
		for ( i = 0 ; i < n_dependent_vars ; ++i ) 
		  metrics_precalc( &(*ffs)->m[i]);
	      }
	    }
	}

      hsh_destroy(idh0);
      hsh_destroy(idh1);

      fctr->fs = (struct factor_statistics **) hsh_sort_copy(fctr->fstats);

      fctr = fctr->next;
    }

  output_examine();

  for ( v = 0 ; v < n_dependent_vars ; ++v ) 
    hsh_destroy(totals[v].ordered_data);

}


static void
show_summary(struct variable **dependent_var, int n_dep_var, 
	     const struct factor *fctr)
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

  int n_rows ;
  int n_factors = 1;

  if ( fctr )
    {
      heading_columns = 2;
      n_factors = hsh_count(fctr->fstats);
      n_rows = n_dep_var * n_factors ;

      if ( fctr->indep_var[1] )
	  heading_columns = 3;
    }
  else
    {
      heading_columns = 1;
      n_rows = n_dep_var;
    }

  n_rows += heading_rows;

  n_cols = heading_columns + 6;

  tbl = tab_create (n_cols,n_rows,0);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions);
  
  /* Outline the box */
  tab_box (tbl, 
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  /* Vertical lines for the data only */
  tab_box (tbl, 
	   -1, -1,
	   -1, TAL_1,
	   heading_columns, 0,
	   n_cols - 1, n_rows - 1);


  tab_hline (tbl, TAL_2, 0, n_cols - 1, heading_rows );
  tab_hline (tbl, TAL_1, heading_columns, n_cols - 1, 1 );
  tab_hline (tbl, TAL_1, heading_columns, n_cols - 1, heading_rows -1 );

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

  for ( i = 0 ; i < 3 ; ++i ) 
    {
      tab_text (tbl, heading_columns + i*2 , 2, TAB_CENTER | TAT_TITLE, 
		_("N"));

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


  /* Titles for the independent variables */
  if ( fctr ) 
    {
      tab_text (tbl, 1, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		var_to_string(fctr->indep_var[0]));

      if ( fctr->indep_var[1] ) 
	{
	  tab_text (tbl, 2, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		    var_to_string(fctr->indep_var[1]));
	}
		
    }


  for ( i = 0 ; i < n_dep_var ; ++i ) 
    {
      int n_factors = 1;
      if ( fctr ) 
	n_factors = hsh_count(fctr->fstats);
      

      if ( i > 0 ) 
	tab_hline(tbl, TAL_1, 0, n_cols -1 , i * n_factors + heading_rows);
      
      tab_text (tbl, 
		0, i * n_factors + heading_rows,
		TAB_LEFT | TAT_TITLE, 
		var_to_string(dependent_var[i])
		);


      if ( !fctr ) 
	populate_summary(tbl, heading_columns, 
			 (i * n_factors) + heading_rows,
			 &totals[i]);


      else
	{
	  struct factor_statistics **fs = fctr->fs;
	  int count = 0 ;

	  while (*fs) 
	    {
	      static union value prev;
	      
	      if ( 0 != compare_values(&prev, &(*fs)->id[0], 
				       fctr->indep_var[0]->width))
		{
		   tab_text (tbl, 
			     1,
			     (i * n_factors ) + count + 
			     heading_rows,
			     TAB_LEFT | TAT_TITLE, 
			     value_to_string(&(*fs)->id[0], fctr->indep_var[0])
			     );

		   if (fctr->indep_var[1] && count > 0 ) 
		     tab_hline(tbl, TAL_1, 1, n_cols - 1, 
			       (i * n_factors ) + count + heading_rows);

		}
	      
	      prev = (*fs)->id[0];


	      if ( fctr->indep_var[1]) 
		tab_text (tbl, 
			  2,
			  (i * n_factors ) + count + 
			  heading_rows,
			  TAB_LEFT | TAT_TITLE, 
			  value_to_string(&(*fs)->id[1], fctr->indep_var[1])
			  );

	      populate_summary(tbl, heading_columns, 
			       (i * n_factors) + count 
			       + heading_rows,
			       &(*fs)->m[i]);

	      count++ ; 
	      fs++;
	    }
	}
    }

  tab_submit (tbl);
}


void 
populate_summary(struct tab_table *t, int col, int row,
		 const struct metrics *m)

{
  const double total = m->n + m->n_missing ; 

  tab_float(t, col + 0, row + 0, TAB_RIGHT, m->n, 8, 0);
  tab_float(t, col + 2, row + 0, TAB_RIGHT, m->n_missing, 8, 0);
  tab_float(t, col + 4, row + 0, TAB_RIGHT, total, 8, 0);


  if ( total > 0 ) {
    tab_text (t, col + 1, row + 0, TAB_RIGHT | TAT_PRINTF, "%2.0f%%", 
	      100.0 * m->n / total );

    tab_text (t, col + 3, row + 0, TAB_RIGHT | TAT_PRINTF, "%2.0f%%", 
	      100.0 * m->n_missing / total );

    /* This seems a bit pointless !!! */
    tab_text (t, col + 5, row + 0, TAB_RIGHT | TAT_PRINTF, "%2.0f%%", 
	      100.0 * total / total );


  }


}  



static void 
show_extremes(struct variable **dependent_var, int n_dep_var, 
	      const struct factor *fctr, int n_extremities)
{
  int i;
  int heading_columns ;
  int n_cols;
  const int heading_rows = 1;
  struct tab_table *tbl;

  int n_factors = 1;
  int n_rows ;

  if ( fctr )
    {
      heading_columns = 2;
      n_factors = hsh_count(fctr->fstats);

      n_rows = n_dep_var * 2 * n_extremities * n_factors;

      if ( fctr->indep_var[1] )
	  heading_columns = 3;
    }
  else
    {
      heading_columns = 1;
      n_rows = n_dep_var * 2 * n_extremities;
    }

  n_rows += heading_rows;

  heading_columns += 2;
  n_cols = heading_columns + 2;

  tbl = tab_create (n_cols,n_rows,0);
  tab_headers (tbl, heading_columns, 0, heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions);
  
  /* Outline the box, No internal lines*/
  tab_box (tbl, 
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_hline (tbl, TAL_2, 0, n_cols - 1, heading_rows );

  tab_title (tbl, 0, _("Extreme Values"));


  tab_vline (tbl, TAL_2, n_cols - 2, 0, n_rows -1);
  tab_vline (tbl, TAL_1, n_cols - 1, 0, n_rows -1);

  if ( fctr ) 
    {
      tab_text (tbl, 1, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		var_to_string(fctr->indep_var[0]));

      if ( fctr->indep_var[1] ) 
	tab_text (tbl, 2, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		  var_to_string(fctr->indep_var[1]));
    }

  tab_text (tbl, n_cols - 1, 0, TAB_CENTER | TAT_TITLE, _("Value"));
  tab_text (tbl, n_cols - 2, 0, TAB_CENTER | TAT_TITLE, _("Case Number"));




  for ( i = 0 ; i < n_dep_var ; ++i ) 
    {

      if ( i > 0 ) 
	tab_hline(tbl, TAL_1, 0, n_cols -1 , 
		  i * 2 * n_extremities * n_factors + heading_rows);
      
      tab_text (tbl, 0,
		i * 2 * n_extremities * n_factors  + heading_rows,
		TAB_LEFT | TAT_TITLE, 
		var_to_string(dependent_var[i])
		);


      if ( !fctr ) 
	populate_extremes(tbl, heading_columns - 2, 
			  i * 2 * n_extremities * n_factors  + heading_rows,
			  n_extremities, &totals[i]);

      else
	{
	  struct factor_statistics **fs = fctr->fs;
	  int count = 0 ;

	  while (*fs) 
	    {
	      static union value prev ;

	      const int row = heading_rows + ( 2 * n_extremities )  * 
		( ( i  * n_factors  ) +  count );


	      if ( 0 != compare_values(&prev, &(*fs)->id[0], 
				       fctr->indep_var[0]->width))
		{
		  
		  if ( count > 0 ) 
		    tab_hline (tbl, TAL_1, 1, n_cols - 1, row);

		  tab_text (tbl, 
			    1, row,
			    TAB_LEFT | TAT_TITLE, 
			    value_to_string(&(*fs)->id[0], fctr->indep_var[0])
			    );
		}

	      prev = (*fs)->id[0];

	      if (fctr->indep_var[1] && count > 0 ) 
		tab_hline(tbl, TAL_1, 2, n_cols - 1, row);

	      if ( fctr->indep_var[1]) 
		tab_text (tbl, 2, row,
			  TAB_LEFT | TAT_TITLE, 
			  value_to_string(&(*fs)->id[1], fctr->indep_var[1])
			  );

	      populate_extremes(tbl, heading_columns - 2, 
				row, n_extremities,
				&(*fs)->m[i]);

	      count++ ; 
	      fs++;
	    }
	}
    }

  tab_submit(tbl);
}



/* Fill in the extremities table */
void 
populate_extremes(struct tab_table *t, 
		  int col, int row, int n, const struct metrics *m)
{
  int extremity;
  int idx=0;


  tab_text(t, col, row,
	   TAB_RIGHT | TAT_TITLE ,
	   _("Highest")
	   );

  tab_text(t, col, row + n ,
	   TAB_RIGHT | TAT_TITLE ,
	   _("Lowest")
	   );


  tab_hline(t, TAL_1, col, col + 3, row + n );
	    
  for (extremity = 0; extremity < n ; ++extremity ) 
    {
      /* Highest */
      tab_float(t, col + 1, row + extremity,
		TAB_RIGHT,
		extremity + 1, 8, 0);


      /* Lowest */
      tab_float(t, col + 1, row + extremity + n,
		TAB_RIGHT,
		extremity + 1, 8, 0);

    }


  /* Lowest */
  for (idx = 0, extremity = 0; extremity < n && idx < m->n_data ; ++idx ) 
    {
      int j;
      const struct weighted_value *wv = m->wvp[idx];
      struct case_node *cn = wv->case_nos;

      
      for (j = 0 ; j < wv->w ; ++j  )
	{
	  if ( extremity + j >= n ) 
	    break ;

	  tab_float(t, col + 3, row + extremity + j  + n,
		    TAB_RIGHT,
		    wv->v.f, 8, 2);

	  tab_float(t, col + 2, row + extremity + j  + n,
		    TAB_RIGHT,
		    cn->num, 8, 0);

	  if ( cn->next ) 
	      cn = cn->next;

	}

      extremity +=  wv->w ;
    }


  /* Highest */
  for (idx = m->n_data - 1, extremity = 0; extremity < n && idx >= 0; --idx ) 
    {
      int j;
      const struct weighted_value *wv = m->wvp[idx];
      struct case_node *cn = wv->case_nos;

      for (j = 0 ; j < wv->w ; ++j  )
	{
	  if ( extremity + j >= n ) 
	    break ;

	  tab_float(t, col + 3, row + extremity + j,
		    TAB_RIGHT,
		    wv->v.f, 8, 2);

	  tab_float(t, col + 2, row + extremity + j,
		    TAB_RIGHT,
		    cn->num, 8, 0);

	  if ( cn->next ) 
	      cn = cn->next;

	}

      extremity +=  wv->w ;
    }
}


/* Show the descriptives table */
void
show_descriptives(struct variable **dependent_var, 
		  int n_dep_var, 
		  struct factor *fctr)
{
  int i;
  int heading_columns ;
  int n_cols;
  const int n_stat_rows = 13;

  const int heading_rows = 1;

  struct tab_table *tbl;

  int n_factors = 1;
  int n_rows ;

  if ( fctr )
    {
      heading_columns = 4;
      n_factors = hsh_count(fctr->fstats);

      n_rows = n_dep_var * n_stat_rows * n_factors;

      if ( fctr->indep_var[1] )
	  heading_columns = 5;
    }
  else
    {
      heading_columns = 3;
      n_rows = n_dep_var * n_stat_rows;
    }

  n_rows += heading_rows;

  n_cols = heading_columns + 2;


  tbl = tab_create (n_cols, n_rows, 0);

  tab_headers (tbl, heading_columns + 1, 0, heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions);

  /* Outline the box and have no internal lines*/
  tab_box (tbl, 
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_hline (tbl, TAL_2, 0, n_cols - 1, heading_rows );

  tab_vline (tbl, TAL_1, heading_columns, 0, n_rows - 1);
  tab_vline (tbl, TAL_2, n_cols - 2, 0, n_rows - 1);
  tab_vline (tbl, TAL_1, n_cols - 1, 0, n_rows - 1);

  tab_text (tbl, n_cols - 2, 0, TAB_CENTER | TAT_TITLE, _("Statistic"));
  tab_text (tbl, n_cols - 1, 0, TAB_CENTER | TAT_TITLE, _("Std. Error"));

  tab_title (tbl, 0, _("Descriptives"));


  for ( i = 0 ; i < n_dep_var ; ++i ) 
    {
      const int row = heading_rows + i * n_stat_rows * n_factors ;

      if ( i > 0 )
	tab_hline(tbl, TAL_1, 0, n_cols - 1, row );

      tab_text (tbl, 0,
		i * n_stat_rows * n_factors  + heading_rows,
		TAB_LEFT | TAT_TITLE, 
		var_to_string(dependent_var[i])
		);


      if ( fctr  )
	{
	  struct factor_statistics **fs = fctr->fs;
	  int count = 0;

	  tab_text (tbl, 1, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		    var_to_string(fctr->indep_var[0]));


	  if ( fctr->indep_var[1])
	    tab_text (tbl, 2, heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		      var_to_string(fctr->indep_var[1]));

	  while( *fs ) 
	    {

	      static union value prev ;

	      const int row = heading_rows + n_stat_rows  * 
		( ( i  * n_factors  ) +  count );


	      if ( 0 != compare_values(&prev, &(*fs)->id[0], 
				       fctr->indep_var[0]->width))
		{
		  
		  if ( count > 0 ) 
		    tab_hline (tbl, TAL_1, 1, n_cols - 1, row);

		  tab_text (tbl, 
			    1, row,
			    TAB_LEFT | TAT_TITLE, 
			    value_to_string(&(*fs)->id[0], fctr->indep_var[0])
			    );
		}

	      prev = (*fs)->id[0];

	      if (fctr->indep_var[1] && count > 0 ) 
		tab_hline(tbl, TAL_1, 2, n_cols - 1, row);

	      if ( fctr->indep_var[1]) 
		tab_text (tbl, 2, row,
			  TAB_LEFT | TAT_TITLE, 
			  value_to_string(&(*fs)->id[1], fctr->indep_var[1])
			  );

	      populate_descriptives(tbl, heading_columns - 2, 
				row, &(*fs)->m[i]);

	      count++ ; 
	      fs++;
	    }

	}

      else 
	{
	  
	  populate_descriptives(tbl, heading_columns - 2, 
				i * n_stat_rows * n_factors  + heading_rows,
				&totals[i]);
	}
    }

  tab_submit(tbl);

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

  const struct weighted_value *wv = *(m->wvp);


  /* The slope and intercept of the ideal normal probability line */
  const double slope = 1.0 / m->stddev;
  const double intercept = - m->mean / m->stddev;

  /* Cowardly refuse to plot an empty data set */
  if ( m->n_data == 0 ) 
    return ; 

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
  ylast =  gsl_cdf_ugaussian_Pinv (wv[m->n_data-1].rank / ( m->n + 1));

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
  d_data = xmalloc (m->n_data * sizeof(double));
  double d_max = -DBL_MAX;
  double d_min = DBL_MAX;
  for ( i = 0 ; i < m->n_data; ++i ) 
    {
      const double ns = gsl_cdf_ugaussian_Pinv (wv[i].rank / ( m->n + 1));

      chart_datum(&np_chart, 0, wv[i].v.f, ns);

      d_data[i] = (wv[i].v.f - m->mean) / m->stddev  - ns;
   
      if ( d_data[i] < d_min ) d_min = d_data[i];
      if ( d_data[i] > d_max ) d_max = d_data[i];
    }

  chart_write_yscale(&dnp_chart, d_min, d_max, 
		     chart_rounded_tick((d_max - d_min) / 5.0));

  for ( i = 0 ; i < m->n_data; ++i ) 
      chart_datum(&dnp_chart, 0, wv[i].v.f, d_data[i]);

  free(d_data);
  }

  chart_line(&np_chart, slope, intercept, yfirst, ylast , CHART_DIM_Y);
  chart_line(&dnp_chart, 0, 0, m->min, m->max , CHART_DIM_X);

  chart_finalise(&np_chart);
  chart_finalise(&dnp_chart);

}
