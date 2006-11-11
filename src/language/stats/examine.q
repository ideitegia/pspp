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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA. */

#include <config.h>

#include <gsl/gsl_cdf.h>
#include <libpspp/message.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <data/case.h>
#include <data/casefile.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/dictionary/split-file.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/magic.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include <math/factor-stats.h>
#include <math/moments.h>
#include <math/percentiles.h>
#include <output/charts/box-whisker.h>
#include <output/charts/cartesian.h>
#include <output/manager.h>
#include <output/table.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* (headers) */
#include <output/chart.h>
#include <output/charts/plot-hist.h>
#include <output/charts/plot-chart.h>

/* (specification)
   "EXAMINE" (xmn_):
   *^variables=custom;
   +total=custom;
   +nototal=custom;
   missing=miss:pairwise/!listwise,
           rep:report/!noreport,
           incl:include/!exclude;
   +compare=cmp:variables/!groups;
   +percentiles=custom;
   +id=var;
   +plot[plt_]=stemleaf,boxplot,npplot,:spreadlevel(*d:n),histogram,all,none;
   +cinterval=double;
   +statistics[st_]=descriptives,:extreme(*d:n),all,none.
*/

/* (declarations) */

/* (functions) */



static struct cmd_examine cmd;

static struct variable **dependent_vars;

static size_t n_dependent_vars;


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

/* Parse the clause specifying the factors */
static int examine_parse_independent_vars (struct lexer *lexer, const struct dictionary *dict, struct cmd_examine *cmd);



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

static void show_percentiles(struct variable **dependent_var, 
			     int n_dep_var, 
			     struct factor *factor);




void np_plot(const struct metrics *m, const char *factorname);


void box_plot_group(const struct factor *fctr, 
		    const struct variable **vars, int n_vars,
		    const struct variable *id
		    ) ;


void box_plot_variables(const struct factor *fctr, 
			const struct variable **vars, int n_vars, 
			const struct variable *id
			);



/* Per Split function */
static bool run_examine (const struct ccase *,
                        const struct casefile *cf, void *cmd_, const struct dataset *);

static void output_examine(void);


void factor_calc(struct ccase *c, int case_no, 
		 double weight, int case_missing);


/* Represent a factor as a string, so it can be
   printed in a human readable fashion */
const char * factor_to_string(const struct factor *fctr, 
			      struct factor_statistics *fs,
			      const struct variable *var);


/* Represent a factor as a string, so it can be
   printed in a human readable fashion,
   but sacrificing some readablility for the sake of brevity */
const char *factor_to_string_concise(const struct factor *fctr, 
				     struct factor_statistics *fs);




/* Function to use for testing for missing values */
static is_missing_func *value_is_missing;


/* PERCENTILES */

static subc_list_double percentile_list;

static enum pc_alg percentile_algorithm;

static short sbc_percentile;


int
cmd_examine (struct lexer *lexer, struct dataset *ds)
{
  bool ok;

  subc_list_double_create(&percentile_list);
  percentile_algorithm = PC_HAVERAGE;

  if ( !parse_examine (lexer, ds, &cmd, NULL) )
    return CMD_FAILURE;

  /* If /MISSING=INCLUDE is set, then user missing values are ignored */
  if (cmd.incl == XMN_INCLUDE ) 
    value_is_missing = mv_is_value_system_missing;
  else
    value_is_missing = mv_is_value_missing;

  if ( cmd.st_n == SYSMIS ) 
    cmd.st_n = 5;

  if ( ! cmd.sbc_cinterval) 
    cmd.n_cinterval[0] = 95.0;

  /* If descriptives have been requested, make sure the 
     quartiles are calculated */
  if ( cmd.a_statistics[XMN_ST_DESCRIPTIVES] )
    {
      subc_list_double_push(&percentile_list, 25);
      subc_list_double_push(&percentile_list, 50);
      subc_list_double_push(&percentile_list, 75);
    }

  ok = multipass_procedure_with_splits (ds, run_examine, &cmd);

  if ( totals ) 
    {
      free( totals );
    }
  
  if ( dependent_vars ) 
    free (dependent_vars);

  {
    struct factor *f = factors ;
    while ( f ) 
      {
	struct factor *ff = f;

	f = f->next;
	free ( ff->fs );
	hsh_destroy ( ff->fstats ) ;
	free ( ff ) ;
      }
    factors = 0;
  }

  subc_list_double_destroy(&percentile_list);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
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
      if ( sbc_percentile ) 
	show_percentiles(dependent_vars, n_dependent_vars, 0);

      if ( cmd.sbc_plot) 
	{
	  int v;
	  if ( cmd.a_plot[XMN_PLT_STEMLEAF] ) 
	    msg (SW, _("%s is not currently supported."), "STEMLEAF");

	  if ( cmd.a_plot[XMN_PLT_SPREADLEVEL] ) 
	    msg (SW, _("%s is not currently supported."), "SPREADLEVEL");

	  if ( cmd.a_plot[XMN_PLT_NPPLOT] ) 
	    {
	      for ( v = 0 ; v < n_dependent_vars; ++v ) 
		np_plot(&totals[v], var_to_string(dependent_vars[v]));
	    }

	  if ( cmd.a_plot[XMN_PLT_BOXPLOT] ) 
	    {
	      if ( cmd.cmp == XMN_GROUPS ) 
		{
		  box_plot_group (0, (const struct variable **) dependent_vars,
                                  n_dependent_vars, cmd.v_id);
		}
	      else
		box_plot_variables (0,
                                    (const struct variable **) dependent_vars,
                                    n_dependent_vars, cmd.v_id);
	    }

	  if ( cmd.a_plot[XMN_PLT_HISTOGRAM] ) 
	    {
	      for ( v = 0 ; v < n_dependent_vars; ++v ) 
		{
		  struct normal_curve normal;

		  normal.N      = totals[v].n;
		  normal.mean   = totals[v].mean;
		  normal.stddev = totals[v].stddev;
		  
		  histogram_plot(totals[v].histogram, 
				 var_to_string(dependent_vars[v]),
				 &normal, 0);
		}
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

      if ( sbc_percentile ) 
	show_percentiles(dependent_vars, n_dependent_vars, fctr);


      if ( cmd.sbc_plot) 
	{
	  size_t v;

	  struct factor_statistics **fs = fctr->fs ;

	  if ( cmd.a_plot[XMN_PLT_BOXPLOT] )
	    {
	      if ( cmd.cmp == XMN_VARIABLES ) 
		box_plot_variables (fctr,
                                    (const struct variable **) dependent_vars,
                                    n_dependent_vars, cmd.v_id);
	      else
		box_plot_group (fctr,
                                (const struct variable **) dependent_vars,
                                n_dependent_vars, cmd.v_id);
	    }

	  for ( v = 0 ; v < n_dependent_vars; ++v )
	    {

	      for ( fs = fctr->fs ; *fs ; ++fs ) 
		{
		  const char *s = factor_to_string(fctr, *fs, dependent_vars[v]);

		  if ( cmd.a_plot[XMN_PLT_NPPLOT] ) 
		    np_plot(&(*fs)->m[v], s);

		  if ( cmd.a_plot[XMN_PLT_HISTOGRAM] ) 
		    {
		      struct normal_curve normal;

		      normal.N      = (*fs)->m[v].n;
		      normal.mean   = (*fs)->m[v].mean;
		      normal.stddev = (*fs)->m[v].stddev;
		  
		      histogram_plot((*fs)->m[v].histogram, 
				     s,  &normal, 0);
		    }
		  
		} /* for ( fs .... */

	    } /* for ( v = 0 ..... */

	}

      fctr = fctr->next;
    }

}


/* Create a hash table of percentiles and their values from the list of
   percentiles */
static struct hsh_table *
list_to_ptile_hash(const subc_list_double *l)
{
  int i;
  
  struct hsh_table *h ; 

  h = hsh_create(subc_list_double_count(l), 
		 (hsh_compare_func *) ptile_compare,
		 (hsh_hash_func *) ptile_hash, 
		 (hsh_free_func *) free,
		 0);


  for ( i = 0 ; i < subc_list_double_count(l) ; ++i )
    {
      struct percentile *p = xmalloc (sizeof *p);
      
      p->p = subc_list_double_at(l,i);
      p->v = SYSMIS;

      hsh_insert(h, p);

    }

  return h;

}

/* Parse the PERCENTILES subcommand */
static int
xmn_custom_percentiles(struct lexer *lexer, struct dataset *ds UNUSED, 
		       struct cmd_examine *p UNUSED, void *aux UNUSED)
{
  sbc_percentile = 1;

  lex_match (lexer, '=');

  lex_match (lexer, '(');

  while ( lex_is_number (lexer) ) 
    {
      subc_list_double_push (&percentile_list, lex_number (lexer));

      lex_get (lexer);

      lex_match (lexer, ',') ;
    }
  lex_match (lexer, ')');

  lex_match (lexer, '=');

  if ( lex_match_id (lexer, "HAVERAGE"))
    percentile_algorithm = PC_HAVERAGE; 

  else if ( lex_match_id (lexer, "WAVERAGE"))
    percentile_algorithm = PC_WAVERAGE; 

  else if ( lex_match_id (lexer, "ROUND"))
    percentile_algorithm = PC_ROUND;

  else if ( lex_match_id (lexer, "EMPIRICAL"))
    percentile_algorithm = PC_EMPIRICAL;

  else if ( lex_match_id (lexer, "AEMPIRICAL"))
    percentile_algorithm = PC_AEMPIRICAL; 

  else if ( lex_match_id (lexer, "NONE"))
    percentile_algorithm = PC_NONE; 


  if ( 0 == subc_list_double_count(&percentile_list))
    {
      subc_list_double_push (&percentile_list, 5);
      subc_list_double_push (&percentile_list, 10);
      subc_list_double_push (&percentile_list, 25);
      subc_list_double_push (&percentile_list, 50);
      subc_list_double_push (&percentile_list, 75);
      subc_list_double_push (&percentile_list, 90);
      subc_list_double_push (&percentile_list, 95);
    }

  return 1;
}

/* TOTAL and NOTOTAL are simple, mutually exclusive flags */
static int
xmn_custom_total (struct lexer *lexer UNUSED, struct dataset *ds UNUSED, struct cmd_examine *p, void *aux UNUSED)
{
  if ( p->sbc_nototal ) 
    {
      msg (SE, _("%s and %s are mutually exclusive"),"TOTAL","NOTOTAL");
      return 0;
    }

  return 1;
}

static int
xmn_custom_nototal (struct lexer *lexer UNUSED, struct dataset *ds UNUSED, 
		    struct cmd_examine *p, void *aux UNUSED)
{
  if ( p->sbc_total ) 
    {
      msg (SE, _("%s and %s are mutually exclusive"),"TOTAL","NOTOTAL");
      return 0;
    }

  return 1;
}



/* Parser for the variables sub command  
   Returns 1 on success */
static int
xmn_custom_variables (struct lexer *lexer, struct dataset *ds, struct cmd_examine *cmd, void *aux UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);
  lex_match (lexer, '=');

  if ((lex_token (lexer) != T_ID || dict_lookup_var (dict, lex_tokid (lexer)) == NULL)
      && lex_token (lexer) != T_ALL)
    {
      return 2;
    }
  
  if (!parse_variables (lexer, dict, &dependent_vars, &n_dependent_vars,
			PV_NO_DUPLICATE | PV_NUMERIC | PV_NO_SCRATCH) )
    {
      free (dependent_vars);
      return 0;
    }

  assert(n_dependent_vars);

  totals = xnmalloc (n_dependent_vars, sizeof *totals);

  if ( lex_match (lexer, T_BY))
    {
      int success ; 
      success =  examine_parse_independent_vars (lexer, dict, cmd);
      if ( success != 1 ) {
        free (dependent_vars);
      	free (totals) ; 
      }
      return success;
    }

  return 1;
}



/* Parse the clause specifying the factors */
static int
examine_parse_independent_vars (struct lexer *lexer, const struct dictionary *dict, struct cmd_examine *cmd)
{
  int success;
  struct factor *sf = xmalloc (sizeof *sf);

  if ((lex_token (lexer) != T_ID || dict_lookup_var (dict, lex_tokid (lexer)) == NULL)
      && lex_token (lexer) != T_ALL)
    {
      free ( sf ) ;
      return 2;
    }


  sf->indep_var[0] = parse_variable (lexer, dict);
  sf->indep_var[1] = 0;

  if ( lex_token (lexer) == T_BY ) 
    {

      lex_match (lexer, T_BY);

      if ((lex_token (lexer) != T_ID || dict_lookup_var (dict, lex_tokid (lexer)) == NULL)
	  && lex_token (lexer) != T_ALL)
	{
	  free ( sf ) ;
	  return 2;
	}

      sf->indep_var[1] = parse_variable (lexer, dict);

    }


  sf->fstats = hsh_create(4,
			  (hsh_compare_func *) factor_statistics_compare,
			  (hsh_hash_func *) factor_statistics_hash,
			  (hsh_free_func *) factor_statistics_free,
			  0);

  sf->next = factors;
  factors = sf;
  
  lex_match (lexer, ',');

  if ( lex_token (lexer) == '.' || lex_token (lexer) == '/' ) 
    return 1;

  success =  examine_parse_independent_vars (lexer, dict, cmd);
  
  if ( success != 1 ) 
    free ( sf ) ; 

  return success;
}




void populate_percentiles(struct tab_table *tbl, int col, int row, 
			  const struct metrics *m);

void populate_descriptives(struct tab_table *t, int col, int row, 
			   const struct metrics *fs);

void populate_extremes(struct tab_table *t, int col, int row, int n, 
		       const struct metrics *m);

void populate_summary(struct tab_table *t, int col, int row,
		      const struct metrics *m);




static bool bad_weight_warn = true;


/* Perform calculations for the sub factors */
void
factor_calc(struct ccase *c, int case_no, double weight, int case_missing)
{
  size_t v;
  struct factor *fctr = factors;

  while ( fctr) 
    {
      struct factor_statistics **foo ;
      union value indep_vals[2] ;

      indep_vals[0] = * case_data(c, fctr->indep_var[0]->fv);

      if ( fctr->indep_var[1] ) 
	indep_vals[1] = * case_data(c, fctr->indep_var[1]->fv);
      else
	indep_vals[1].f = SYSMIS;

      assert(fctr->fstats);

      foo = ( struct factor_statistics ** ) 
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

	  if ( value_is_missing (&var->miss, val) || case_missing ) 
	    val = 0;
	  
	  metrics_calc( &(*foo)->m[v], val, weight, case_no);
	  
	}

      fctr = fctr->next;
    }


}

static bool 
run_examine(const struct ccase *first, const struct casefile *cf, 
	    void *cmd_, const struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct casereader *r;
  struct ccase c;
  int v;

  const struct cmd_examine *cmd = (struct cmd_examine *) cmd_;

  struct factor *fctr;

  output_split_file_values (ds, first);

  /* Make sure we haven't got rubbish left over from a 
     previous split */
  fctr = factors;
  while (fctr) 
    {
      struct factor *next = fctr->next;

      hsh_clear(fctr->fstats);

      fctr->fs = 0;

      fctr = next;
    }

  for ( v = 0 ; v < n_dependent_vars ; ++v ) 
    metrics_precalc(&totals[v]);

  for(r = casefile_get_reader (cf, NULL);
      casereader_read (r, &c) ;
      case_destroy (&c) ) 
    {
      int case_missing=0;
      const int case_no = casereader_cnum(r);

      const double weight = 
	dict_get_case_weight(dict, &c, &bad_weight_warn);

      if ( cmd->miss == XMN_LISTWISE ) 
	{
	  for ( v = 0 ; v < n_dependent_vars ; ++v ) 
	    {
	      const struct variable *var = dependent_vars[v];
	      const union value *val = case_data (&c, var->fv);

	      if ( value_is_missing(&var->miss, val))
		case_missing = 1;
		   
	    }
	}

      for ( v = 0 ; v < n_dependent_vars ; ++v ) 
	{
	  const struct variable *var = dependent_vars[v];
	  const union value *val = case_data (&c, var->fv);

	  if ( value_is_missing(&var->miss, val) || case_missing ) 
	    val = 0;

	  metrics_calc(&totals[v], val, weight, case_no);
    
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
	      
	      fs->m[v].ptile_hash = list_to_ptile_hash(&percentile_list);
	      fs->m[v].ptile_alg = percentile_algorithm;
	      metrics_postcalc(&fs->m[v]);
	    }

	  fctr = fctr->next;
	}

      totals[v].ptile_hash = list_to_ptile_hash(&percentile_list);
      totals[v].ptile_alg = percentile_algorithm;
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
		size_t i;
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


  if ( totals ) 
    {
      size_t i;
      for ( i = 0 ; i < n_dependent_vars ; ++i ) 
	{
	  metrics_destroy(&totals[i]);
	}
    }

  return true;
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


  tab_title (tbl, _("Case Processing Summary"));
  

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

  tab_title (tbl, _("Extreme Values"));

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

  tab_title (tbl, _("Descriptives"));


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
	     m->se_mean,
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
	     m->mean - t * m->se_mean, 
	     8,3);

  tab_text (tbl, col + 1,  
	    row + 2,
	    TAB_LEFT | TAT_TITLE,
	    _("Upper Bound"));


  tab_float (tbl, col + 2,
	     row + 2,
	     TAB_CENTER,
	     m->mean + t * m->se_mean, 
	     8,3);

  tab_text (tbl, col, 
	    row + 3,
	    TAB_LEFT | TAT_TITLE | TAT_PRINTF,
	    _("5%% Trimmed Mean"));

  tab_float (tbl, col + 2, 
	     row + 3,
	     TAB_CENTER,
	     m->trimmed_mean,
	     8,2);

  tab_text (tbl, col, 
	    row + 4,
	    TAB_LEFT | TAT_TITLE,
	    _("Median"));

  {
    struct percentile *p;
    double d = 50;
    
    p = hsh_find(m->ptile_hash, &d);
    
    assert(p);


    tab_float (tbl, col + 2, 
	       row + 4,
	       TAB_CENTER,
	       p->v,
	       8, 2);
  }
    

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

  {
    struct percentile *p1;
    struct percentile *p2;

    double d = 75;
    p1 = hsh_find(m->ptile_hash, &d);

    d = 25;
    p2 = hsh_find(m->ptile_hash, &d);

    assert(p1);
    assert(p2);

    tab_float (tbl, col + 2, 
	       row + 10,
	       TAB_CENTER,
	       p1->v - p2->v,
	       8, 2);
  }



  tab_text (tbl, col, 
	    row + 11,
	    TAB_LEFT | TAT_TITLE,
	    _("Skewness"));


  tab_float (tbl, col + 2,
	     row + 11,
	     TAB_CENTER,
	     m->skewness,
	     8,3);

  /* stderr of skewness */
  tab_float (tbl, col + 3,
	     row + 11,
	     TAB_CENTER,
	     calc_seskew(m->n),
	     8,3);


  tab_text (tbl, col, 
	    row + 12,
	    TAB_LEFT | TAT_TITLE,
	    _("Kurtosis"));


  tab_float (tbl, col + 2,
	     row + 12,
	     TAB_CENTER,
	     m->kurtosis,
	     8,3);

  /* stderr of kurtosis */
  tab_float (tbl, col + 3,
	     row + 12,
	     TAB_CENTER,
	     calc_sekurt(m->n),
	     8,3);


}



void
box_plot_variables(const struct factor *fctr, 
		   const struct variable **vars, int n_vars, 
		   const struct variable *id)
{

  int i;
  struct factor_statistics **fs ;

  if ( ! fctr ) 
    {
      box_plot_group(fctr, vars, n_vars, id);
      return;
    }

  for ( fs = fctr->fs ; *fs ; ++fs ) 
    {
      double y_min = DBL_MAX;
      double y_max = -DBL_MAX;
      struct chart *ch = chart_create();
      const char *s = factor_to_string(fctr, *fs, 0 );

      chart_write_title(ch, s);

      for ( i = 0 ; i < n_vars ; ++i ) 
	{
	  y_max = max(y_max, (*fs)->m[i].max);
	  y_min = min(y_min, (*fs)->m[i].min);
	}
      
      boxplot_draw_yscale(ch, y_max, y_min);
	  
      for ( i = 0 ; i < n_vars ; ++i ) 
	{

	  const double box_width = (ch->data_right - ch->data_left) 
	    / (n_vars * 2.0 ) ;

	  const double box_centre = ( i * 2 + 1) * box_width 
	    + ch->data_left;
	      
	  boxplot_draw_boxplot(ch,
			       box_centre, box_width,
			       &(*fs)->m[i],
			       var_to_string(vars[i]));


	}

      chart_submit(ch);

    }
}



/* Do a box plot, grouping all factors into one plot ;
   each dependent variable has its own plot.
*/
void
box_plot_group(const struct factor *fctr, 
	       const struct variable **vars, 
	       int n_vars,
	       const struct variable *id UNUSED)
{

  int i;

  for ( i = 0 ; i < n_vars ; ++i ) 
    {
      struct factor_statistics **fs ;
      struct chart *ch;

      ch = chart_create();

      boxplot_draw_yscale(ch, totals[i].max, totals[i].min);

      if ( fctr ) 
	{
	  int n_factors = 0;
	  int f=0;
	  for ( fs = fctr->fs ; *fs ; ++fs ) 
	    ++n_factors;

	  chart_write_title(ch, _("Boxplot of %s vs. %s"), 
			    var_to_string(vars[i]), var_to_string(fctr->indep_var[0]) );

	  for ( fs = fctr->fs ; *fs ; ++fs ) 
	    {
	      
	      const char *s = factor_to_string_concise(fctr, *fs);

	      const double box_width = (ch->data_right - ch->data_left) 
		/ (n_factors * 2.0 ) ;

	      const double box_centre = ( f++ * 2 + 1) * box_width 
		+ ch->data_left;
	      
	      boxplot_draw_boxplot(ch,
				   box_centre, box_width,
				   &(*fs)->m[i],
				   s);
	    }
	}
      else if ( ch )
	{
	  const double box_width = (ch->data_right - ch->data_left) / 3.0;
	  const double box_centre = (ch->data_right + ch->data_left) / 2.0;

	  chart_write_title(ch, _("Boxplot"));

	  boxplot_draw_boxplot(ch,
			       box_centre,    box_width, 
			       &totals[i],
			       var_to_string(vars[i]) );
	  
	}

      chart_submit(ch);
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
  struct chart *np_chart;

  /* Detrended Normal Plot */
  struct chart *dnp_chart;

  /* The slope and intercept of the ideal normal probability line */
  const double slope = 1.0 / m->stddev;
  const double intercept = - m->mean / m->stddev;

  /* Cowardly refuse to plot an empty data set */
  if ( m->n_data == 0 ) 
    return ; 

  np_chart = chart_create();
  dnp_chart = chart_create();

  if ( !np_chart || ! dnp_chart ) 
    return ;

  chart_write_title(np_chart, _("Normal Q-Q Plot of %s"), factorname);
  chart_write_xlabel(np_chart, _("Observed Value"));
  chart_write_ylabel(np_chart, _("Expected Normal"));


  chart_write_title(dnp_chart, _("Detrended Normal Q-Q Plot of %s"), 
		    factorname);
  chart_write_xlabel(dnp_chart, _("Observed Value"));
  chart_write_ylabel(dnp_chart, _("Dev from Normal"));

  yfirst = gsl_cdf_ugaussian_Pinv (m->wvp[0]->rank / ( m->n + 1));
  ylast =  gsl_cdf_ugaussian_Pinv (m->wvp[m->n_data-1]->rank / ( m->n + 1));


  {
    /* Need to make sure that both the scatter plot and the ideal fit into the
       plot */
    double x_lower = min(m->min, (yfirst - intercept) / slope) ;
    double x_upper = max(m->max, (ylast  - intercept) / slope) ;
    double slack = (x_upper - x_lower)  * 0.05 ;

    chart_write_xscale(np_chart, x_lower - slack, x_upper + slack, 5);

    chart_write_xscale(dnp_chart, m->min, m->max, 5);

  }

  chart_write_yscale(np_chart, yfirst, ylast, 5);

  {
    /* We have to cache the detrended data, beacause we need to 
       find its limits before we can plot it */
    double *d_data = xnmalloc (m->n_data, sizeof *d_data);
    double d_max = -DBL_MAX;
    double d_min = DBL_MAX;
    for ( i = 0 ; i < m->n_data; ++i ) 
      {
	const double ns = gsl_cdf_ugaussian_Pinv (m->wvp[i]->rank / ( m->n + 1));

	chart_datum(np_chart, 0, m->wvp[i]->v.f, ns);

	d_data[i] = (m->wvp[i]->v.f - m->mean) / m->stddev  - ns;
   
	if ( d_data[i] < d_min ) d_min = d_data[i];
	if ( d_data[i] > d_max ) d_max = d_data[i];
      }
    chart_write_yscale(dnp_chart, d_min, d_max, 5);

    for ( i = 0 ; i < m->n_data; ++i ) 
      chart_datum(dnp_chart, 0, m->wvp[i]->v.f, d_data[i]);

    free(d_data);
  }

  chart_line(np_chart, slope, intercept, yfirst, ylast , CHART_DIM_Y);
  chart_line(dnp_chart, 0, 0, m->min, m->max , CHART_DIM_X);

  chart_submit(np_chart);
  chart_submit(dnp_chart);
}




/* Show the percentiles */
void
show_percentiles(struct variable **dependent_var, 
		 int n_dep_var, 
		 struct factor *fctr)
{
  struct tab_table *tbl;
  int i;
  
  int n_cols, n_rows;
  int n_factors;

  struct hsh_table *ptiles ;

  int n_heading_columns;
  const int n_heading_rows = 2;
  const int n_stat_rows = 2;

  int n_ptiles ;

  if ( fctr )
    {
      struct factor_statistics **fs = fctr->fs ; 
      n_heading_columns = 3;
      n_factors = hsh_count(fctr->fstats);

      ptiles = (*fs)->m[0].ptile_hash;

      if ( fctr->indep_var[1] )
	n_heading_columns = 4;
    }
  else
    {
      n_factors = 1;
      n_heading_columns = 2;

      ptiles = totals[0].ptile_hash;
    }

  n_ptiles = hsh_count(ptiles);

  n_rows = n_heading_rows + n_dep_var * n_stat_rows * n_factors;

  n_cols = n_heading_columns + n_ptiles ; 

  tbl = tab_create (n_cols, n_rows, 0);

  tab_headers (tbl, n_heading_columns + 1, 0, n_heading_rows, 0);

  tab_dim (tbl, tab_natural_dimensions);

  /* Outline the box and have no internal lines*/
  tab_box (tbl, 
	   TAL_2, TAL_2,
	   -1, -1,
	   0, 0,
	   n_cols - 1, n_rows - 1);

  tab_hline (tbl, TAL_2, 0, n_cols - 1, n_heading_rows );

  tab_vline (tbl, TAL_2, n_heading_columns, 0, n_rows - 1);


  tab_title (tbl, _("Percentiles"));


  tab_hline (tbl, TAL_1, n_heading_columns, n_cols - 1, 1 );


  tab_box (tbl, 
	   -1, -1,
	   -1, TAL_1,
	   0, n_heading_rows,
	   n_heading_columns - 1, n_rows - 1);


  tab_box (tbl, 
	   -1, -1,
	   -1, TAL_1,
	   n_heading_columns, n_heading_rows - 1,
	   n_cols - 1, n_rows - 1);

  tab_joint_text(tbl, n_heading_columns + 1, 0,
		 n_cols - 1 , 0,
		 TAB_CENTER | TAT_TITLE ,
		 _("Percentiles"));


  {
    /* Put in the percentile break points as headings */

    struct percentile **p = (struct percentile **) hsh_sort(ptiles);

    i = 0;
    while ( (*p)  ) 
      {
	tab_float(tbl, n_heading_columns + i++ , 1, 
		  TAB_CENTER,
		  (*p)->p, 8, 0);
	
	p++;
      }

  }

  for ( i = 0 ; i < n_dep_var ; ++i ) 
    {
      const int n_stat_rows = 2;
      const int row = n_heading_rows + i * n_stat_rows * n_factors ;

      if ( i > 0 )
	tab_hline(tbl, TAL_1, 0, n_cols - 1, row );

      tab_text (tbl, 0,
		i * n_stat_rows * n_factors  + n_heading_rows,
		TAB_LEFT | TAT_TITLE, 
		var_to_string(dependent_var[i])
		);

      if ( fctr  )
	{
	  struct factor_statistics **fs = fctr->fs;
	  int count = 0;

	  tab_text (tbl, 1, n_heading_rows - 1, 
		    TAB_CENTER | TAT_TITLE, 
		    var_to_string(fctr->indep_var[0]));


	  if ( fctr->indep_var[1])
	    tab_text (tbl, 2, n_heading_rows - 1, TAB_CENTER | TAT_TITLE, 
		      var_to_string(fctr->indep_var[1]));

	  while( *fs ) 
	    {

	      static union value prev ;

	      const int row = n_heading_rows + n_stat_rows  * 
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


	      populate_percentiles(tbl, n_heading_columns - 1, 
				   row, &(*fs)->m[i]);


	      count++ ; 
	      fs++;
	    }


	}
      else 
	{
	  populate_percentiles(tbl, n_heading_columns - 1, 
			       i * n_stat_rows * n_factors  + n_heading_rows,
			       &totals[i]);
	}


    }


  tab_submit(tbl);


}




void
populate_percentiles(struct tab_table *tbl, int col, int row, 
		     const struct metrics *m)
{
  int i;

  struct percentile **p = (struct percentile **) hsh_sort(m->ptile_hash);
  
  tab_text (tbl, 
	    col, row + 1,
	    TAB_LEFT | TAT_TITLE, 
	    _("Tukey\'s Hinges")
	    );

  tab_text (tbl, 
	    col, row, 
	    TAB_LEFT | TAT_TITLE, 
	    ptile_alg_desc[m->ptile_alg]
	    );


  i = 0;
  while ( (*p)  ) 
    {
      tab_float(tbl, col + i + 1 , row, 
		TAB_CENTER,
		(*p)->v, 8, 2);
      if ( (*p)->p == 25 ) 
	tab_float(tbl, col + i + 1 , row + 1, 
		  TAB_CENTER,
		  m->hinge[0], 8, 2);

      if ( (*p)->p == 50 ) 
	tab_float(tbl, col + i + 1 , row + 1, 
		  TAB_CENTER,
		  m->hinge[1], 8, 2);

      if ( (*p)->p == 75 ) 
	tab_float(tbl, col + i + 1 , row + 1, 
		  TAB_CENTER,
		  m->hinge[2], 8, 2);


      i++;

      p++;
    }

}



const char *
factor_to_string(const struct factor *fctr, 
		 struct factor_statistics *fs,
		 const struct variable *var)
{

  static char buf1[100];
  char buf2[100];

  strcpy(buf1,"");

  if (var)
    sprintf(buf1, "%s (",var_to_string(var) );

		      
  snprintf(buf2, 100, "%s = %s",
	   var_to_string(fctr->indep_var[0]),
	   value_to_string(&fs->id[0],fctr->indep_var[0]));
		      
  strcat(buf1, buf2);
		      
  if ( fctr->indep_var[1] ) 
    {
      sprintf(buf2, "; %s = %s)",
	      var_to_string(fctr->indep_var[1]),
	      value_to_string(&fs->id[1],
			      fctr->indep_var[1]));
      strcat(buf1, buf2);
    }
  else
    {
      if ( var ) 
	strcat(buf1, ")");
    }

  return buf1;
}



const char *
factor_to_string_concise(const struct factor *fctr, 
			 struct factor_statistics *fs)

{

  static char buf[100];

  char buf2[100];

  snprintf(buf, 100, "%s",
	   value_to_string(&fs->id[0], fctr->indep_var[0]));
		      
  if ( fctr->indep_var[1] ) 
    {
      sprintf(buf2, ",%s)", value_to_string(&fs->id[1], fctr->indep_var[1]) );
      strcat(buf, buf2);
    }


  return buf;
}
