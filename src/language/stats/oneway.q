/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007 Free Software Foundation, Inc.

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

#include <gsl/gsl_cdf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <data/case.h>
#include <data/casegrouper.h>
#include <data/casereader.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/dictionary/split-file.h>
#include <language/lexer/lexer.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include <libpspp/taint.h>
#include <math/group-proc.h>
#include <math/group.h>
#include <math/levene.h>
#include <output/manager.h>
#include <output/table.h>
#include "sort-criteria.h"

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* (headers) */

/* (specification)
   "ONEWAY" (oneway_):
   *^variables=custom;
   missing=miss:!analysis/listwise,
           incl:include/!exclude;
   +contrast= double list;
   +statistics[st_]=descriptives,homogeneity.
*/
/* (declarations) */
/* (functions) */

static struct cmd_oneway cmd;

/* The independent variable */
static const struct variable *indep_var;

/* Number of dependent variables */
static size_t n_vars;

/* The dependent variables */
static const struct variable **vars;


/* A  hash table containing all the distinct values of the independent
   variables */
static struct hsh_table *global_group_hash ;

/* The number of distinct values of the independent variable, when all
   missing values are disregarded */
static int ostensible_number_of_groups = -1;


static void run_oneway (struct cmd_oneway *, struct casereader *,
                        const struct dataset *);


/* Routines to show the output tables */
static void show_anova_table(void);
static void show_descriptives(void);
static void show_homogeneity(void);

static void show_contrast_coeffs(short *);
static void show_contrast_tests(short *);


enum stat_table_t {STAT_DESC = 1, STAT_HOMO = 2};

static enum stat_table_t stat_tables ;

void output_oneway(void);


int
cmd_oneway (struct lexer *lexer, struct dataset *ds)
{
  struct casegrouper *grouper;
  struct casereader *group;
  int i;
  bool ok;

  if ( !parse_oneway (lexer, ds, &cmd, NULL) )
    return CMD_FAILURE;

  /* What statistics were requested */
  if ( cmd.sbc_statistics )
    {

      for (i = 0 ; i < ONEWAY_ST_count ; ++i )
	{
	  if  ( ! cmd.a_statistics[i]  ) continue;

	  switch (i) {
	  case ONEWAY_ST_DESCRIPTIVES:
	    stat_tables |= STAT_DESC;
	    break;
	  case ONEWAY_ST_HOMOGENEITY:
	    stat_tables |= STAT_HOMO;
	    break;
	  }
	}
    }

  /* Data pass.  FIXME: error handling. */
  grouper = casegrouper_create_splits (proc_open (ds), dataset_dict (ds));
  while (casegrouper_get_next_group (grouper, &group))
    run_oneway (&cmd, group, ds);
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  free (vars);
  free_oneway (&cmd);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}


void
output_oneway(void)
{
  size_t i;
  short *bad_contrast ;

  bad_contrast = xnmalloc (cmd.sbc_contrast, sizeof *bad_contrast);

  /* Check the sanity of the given contrast values */
  for (i = 0 ; i < cmd.sbc_contrast ; ++i )
    {
      int j;
      double sum = 0;

      bad_contrast[i] = 0;
      if ( subc_list_double_count(&cmd.dl_contrast[i]) !=
	   ostensible_number_of_groups )
	{
	  msg(SW,
	      _("Number of contrast coefficients must equal the number of groups"));
	  bad_contrast[i] = 1;
	  continue;
	}

      for (j=0; j < ostensible_number_of_groups ; ++j )
	sum += subc_list_double_at(&cmd.dl_contrast[i],j);

      if ( sum != 0.0 )
	msg(SW,_("Coefficients for contrast %d do not total zero"),
            (int) i + 1);
    }

  if ( stat_tables & STAT_DESC )
    show_descriptives();

  if ( stat_tables & STAT_HOMO )
    show_homogeneity();

  show_anova_table();

  if (cmd.sbc_contrast )
    {
      show_contrast_coeffs(bad_contrast);
      show_contrast_tests(bad_contrast);
    }


  free(bad_contrast);

  /* Clean up */
  for (i = 0 ; i < n_vars ; ++i )
    {
      struct hsh_table *group_hash = group_proc_get (vars[i])->group_hash;

      hsh_destroy(group_hash);
    }

  hsh_destroy(global_group_hash);

}




/* Parser for the variables sub command */
static int
oneway_custom_variables (struct lexer *lexer,
			struct dataset *ds, struct cmd_oneway *cmd UNUSED,
			void *aux UNUSED)
{
  struct dictionary *dict = dataset_dict (ds);

  lex_match (lexer, '=');

  if ((lex_token (lexer) != T_ID || dict_lookup_var (dict, lex_tokid (lexer)) == NULL)
      && lex_token (lexer) != T_ALL)
    return 2;

  if (!parse_variables_const (lexer, dict, &vars, &n_vars,
			PV_DUPLICATE
			| PV_NUMERIC | PV_NO_SCRATCH) )
    {
      free (vars);
      return 0;
    }

  assert(n_vars);

  if ( ! lex_match (lexer, T_BY))
    return 2;

  indep_var = parse_variable (lexer, dict);

  if ( !indep_var )
    {
      msg(SE,_("`%s' is not a variable name"),lex_tokid (lexer));
      return 0;
    }

  return 1;
}


/* Show the ANOVA table */
static void
show_anova_table(void)
{
  size_t i;
  int n_cols =7;
  size_t n_rows = n_vars * 3 + 1;

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
      struct group_statistics *totals = &group_proc_get (vars[i])->ugs;
      struct hsh_table *group_hash = group_proc_get (vars[i])->group_hash;
      struct hsh_iterator g;
      struct group_statistics *gs;
      double ssa=0;
      const char *s = var_to_string(vars[i]);

      for (gs =  hsh_first (group_hash,&g);
	   gs != 0;
	   gs = hsh_next(group_hash,&g))
	{
	  ssa += (gs->sum * gs->sum)/gs->n;
	}

      ssa -= ( totals->sum * totals->sum ) / totals->n ;

      tab_text (t, 0, i * 3 + 1, TAB_LEFT | TAT_TITLE, s);
      tab_text (t, 1, i * 3 + 1, TAB_LEFT | TAT_TITLE, _("Between Groups"));
      tab_text (t, 1, i * 3 + 2, TAB_LEFT | TAT_TITLE, _("Within Groups"));
      tab_text (t, 1, i * 3 + 3, TAB_LEFT | TAT_TITLE, _("Total"));

      if (i > 0)
	tab_hline(t, TAL_1, 0, n_cols - 1 , i * 3 + 1);

      {
        struct group_proc *gp = group_proc_get (vars[i]);
	const double sst = totals->ssq - ( totals->sum * totals->sum) / totals->n ;
	const double df1 = gp->n_groups - 1;
	const double df2 = totals->n - gp->n_groups ;
	const double msa = ssa / df1;

	gp->mse  = (sst - ssa) / df2;


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
	tab_float (t, 4, i * 3 + 2, TAB_RIGHT, gp->mse, 8, 3);


	{
	  const double F = msa/gp->mse ;

	  /* The F value */
	  tab_float (t, 5, i * 3 + 1, 0,  F, 8, 3);

	  /* The significance */
	  tab_float (t, 6, i * 3 + 1, 0, gsl_cdf_fdist_Q(F,df1,df2), 8, 3);
	}

      }

    }


  tab_title (t, _("ANOVA"));
  tab_submit (t);
}


/* Show the descriptives table */
static void
show_descriptives(void)
{
  size_t v;
  int n_cols =10;
  struct tab_table *t;
  int row;

  const double confidence=0.95;
  const double q = (1.0 - confidence) / 2.0;


  int n_rows = 2 ;

  for ( v = 0 ; v < n_vars ; ++v )
    n_rows += group_proc_get (vars[v])->n_groups + 1;

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


  tab_title (t, _("Descriptives"));


  row = 2;
  for ( v=0 ; v < n_vars ; ++v )
    {
      double T;
      double std_error;

      struct group_proc *gp = group_proc_get (vars[v]);

      struct group_statistics *gs;
      struct group_statistics *totals = &gp->ugs;

      const char *s = var_to_string(vars[v]);

      struct group_statistics *const *gs_array =
	(struct group_statistics *const *) hsh_sort(gp->group_hash);
      int count = 0;

      tab_text (t, 0, row, TAB_LEFT | TAT_TITLE, s);
      if ( v > 0)
	tab_hline(t, TAL_1, 0, n_cols - 1 , row);

      for (count = 0 ; count < hsh_count(gp->group_hash) ; ++count)
	{
	  gs = gs_array[count];

	  tab_text (t, 1, row + count,
		    TAB_LEFT | TAT_TITLE, var_get_value_name(indep_var,
                                                             &gs->id));

	  /* Now fill in the numbers ... */

	  tab_float (t, 2, row + count, 0, gs->n, 8,0);

	  tab_float (t, 3, row + count, 0, gs->mean,8,2);

	  tab_float (t, 4, row + count, 0, gs->std_dev,8,2);

	  std_error = gs->std_dev/sqrt(gs->n) ;
	  tab_float (t, 5, row + count, 0,
		     std_error, 8,2);

	  /* Now the confidence interval */

	  T = gsl_cdf_tdist_Qinv(q,gs->n - 1);

	  tab_float(t, 6, row + count, 0,
		    gs->mean - T * std_error, 8, 2);

	  tab_float(t, 7, row + count, 0,
		    gs->mean + T * std_error, 8, 2);

	  /* Min and Max */

	  tab_float(t, 8, row + count, 0,  gs->minimum, 8, 2);
	  tab_float(t, 9, row + count, 0,  gs->maximum, 8, 2);

	}

      tab_text (t, 1, row + count,
		TAB_LEFT | TAT_TITLE ,_("Total"));

      tab_float (t, 2, row + count, 0, totals->n, 8,0);

      tab_float (t, 3, row + count, 0, totals->mean, 8,2);

      tab_float (t, 4, row + count, 0, totals->std_dev,8,2);

      std_error = totals->std_dev/sqrt(totals->n) ;

      tab_float (t, 5, row + count, 0, std_error, 8,2);

      /* Now the confidence interval */

      T = gsl_cdf_tdist_Qinv(q,totals->n - 1);

      tab_float(t, 6, row + count, 0,
		totals->mean - T * std_error, 8, 2);

      tab_float(t, 7, row + count, 0,
		totals->mean + T * std_error, 8, 2);

      /* Min and Max */

      tab_float(t, 8, row + count, 0,  totals->minimum, 8, 2);
      tab_float(t, 9, row + count, 0,  totals->maximum, 8, 2);

      row += gp->n_groups + 1;
    }


  tab_submit (t);


}

/* Show the homogeneity table */
static void
show_homogeneity(void)
{
  size_t v;
  int n_cols = 5;
  size_t n_rows = n_vars + 1;

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


  tab_title (t, _("Test of Homogeneity of Variances"));

  for ( v=0 ; v < n_vars ; ++v )
    {
      double F;
      const struct variable *var = vars[v];
      const struct group_proc *gp = group_proc_get (vars[v]);
      const char *s = var_to_string(var);
      const struct group_statistics *totals = &gp->ugs;

      const double df1 = gp->n_groups - 1;
      const double df2 = totals->n - gp->n_groups ;

      tab_text (t, 0, v + 1, TAB_LEFT | TAT_TITLE, s);

      F = gp->levene;
      tab_float (t, 1, v + 1, TAB_RIGHT, F, 8,3);
      tab_float (t, 2, v + 1, TAB_RIGHT, df1 ,8,0);
      tab_float (t, 3, v + 1, TAB_RIGHT, df2 ,8,0);

      /* Now the significance */
      tab_float (t, 4, v + 1, TAB_RIGHT,gsl_cdf_fdist_Q(F,df1,df2), 8, 3);
    }

  tab_submit (t);
}


/* Show the contrast coefficients table */
static void
show_contrast_coeffs (short *bad_contrast)
{
  int n_cols = 2 + ostensible_number_of_groups;
  int n_rows = 2 + cmd.sbc_contrast;
  union value *group_value;
  int count = 0 ;
  void *const *group_values ;

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

  tab_title (t, _("Contrast Coefficients"));

  tab_text (t,  0, 2, TAB_LEFT | TAT_TITLE, _("Contrast"));


  tab_joint_text (t, 2, 0, n_cols - 1, 0, TAB_CENTER | TAT_TITLE,
		  var_to_string(indep_var));

  group_values = hsh_sort(global_group_hash);
  for (count = 0 ;
       count < hsh_count(global_group_hash) ;
       ++count)
    {
      int i;
      group_value = group_values[count];

      tab_text (t, count + 2, 1, TAB_CENTER | TAT_TITLE,
		var_get_value_name (indep_var, group_value));

      for (i = 0 ; i < cmd.sbc_contrast ; ++i )
	{
	  tab_text(t, 1, i + 2, TAB_CENTER | TAT_PRINTF, "%d", i + 1);

	  if ( bad_contrast[i] )
	    tab_text(t, count + 2, i + 2, TAB_RIGHT, "?" );
	  else
	    tab_text(t, count + 2, i + 2, TAB_RIGHT | TAT_PRINTF, "%g",
		     subc_list_double_at(&cmd.dl_contrast[i], count)
		     );
	}
    }

  tab_submit (t);
}


/* Show the results of the contrast tests */
static void
show_contrast_tests(short *bad_contrast)
{
  size_t v;
  int n_cols = 8;
  size_t n_rows = 1 + n_vars * 2 * cmd.sbc_contrast;

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


  tab_title (t, _("Contrast Tests"));

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
		var_to_string(vars[v]));

      for ( i = 0 ; i < cmd.sbc_contrast ; ++i )
	{
	  int ci;
	  double contrast_value = 0.0;
	  double coef_msq = 0.0;
	  struct group_proc *grp_data = group_proc_get (vars[v]);
	  struct hsh_table *group_hash = grp_data->group_hash;

	  void *const *group_stat_array;

	  double T;
	  double std_error_contrast ;
	  double df;
	  double sec_vneq=0.0;


	  /* Note: The calculation of the degrees of freedom in the
	     "variances not equal" case is painfull!!
	     The following formula may help to understand it:
	     \frac{\left(\sum_{i=1}^k{c_i^2\frac{s_i^2}{n_i}}\right)^2}
	     {
	     \sum_{i=1}^k\left(
	     \frac{\left(c_i^2\frac{s_i^2}{n_i}\right)^2}  {n_i-1}
	     \right)
	     }
	  */

	  double df_denominator = 0.0;
	  double df_numerator = 0.0;
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


	  if ( bad_contrast[i])
	    continue;

	  group_stat_array = hsh_sort(group_hash);

	  for (ci = 0 ; ci < hsh_count(group_hash) ;  ++ci)
	    {
	      const double coef = subc_list_double_at(&cmd.dl_contrast[i], ci);
	      struct group_statistics *gs = group_stat_array[ci];

	      const double winv = (gs->std_dev * gs->std_dev) / gs->n;

	      contrast_value += coef * gs->mean;

	      coef_msq += (coef * coef) / gs->n ;

	      sec_vneq += (coef * coef) * (gs->std_dev * gs->std_dev ) /gs->n ;

	      df_numerator += (coef * coef) * winv;
	      df_denominator += pow2((coef * coef) * winv) / (gs->n - 1);
	    }
	  sec_vneq = sqrt(sec_vneq);

	  df_numerator = pow2(df_numerator);

	  tab_float (t,  3, (v * lines_per_variable) + i + 1,
		     TAB_RIGHT, contrast_value, 8,2);

	  tab_float (t,  3, (v * lines_per_variable) + i + 1 +
		     cmd.sbc_contrast,
		     TAB_RIGHT, contrast_value, 8,2);

	  std_error_contrast = sqrt(grp_data->mse * coef_msq);

	  /* Std. Error */
	  tab_float (t,  4, (v * lines_per_variable) + i + 1,
		     TAB_RIGHT, std_error_contrast,
		     8,3);

	  T = fabs(contrast_value / std_error_contrast) ;

	  /* T Statistic */

	  tab_float (t,  5, (v * lines_per_variable) + i + 1,
		     TAB_RIGHT, T,
		     8,3);

	  df = grp_data->ugs.n - grp_data->n_groups;

	  /* Degrees of Freedom */
	  tab_float (t,  6, (v * lines_per_variable) + i + 1,
		     TAB_RIGHT,  df,
		     8,0);


	  /* Significance TWO TAILED !!*/
	  tab_float (t,  7, (v * lines_per_variable) + i + 1,
		     TAB_RIGHT,  2 * gsl_cdf_tdist_Q(T,df),
		     8,3);


	  /* Now for the Variances NOT Equal case */

	  /* Std. Error */
	  tab_float (t,  4,
		     (v * lines_per_variable) + i + 1 + cmd.sbc_contrast,
		     TAB_RIGHT, sec_vneq,
		     8,3);


	  T = contrast_value / sec_vneq;
	  tab_float (t,  5,
		     (v * lines_per_variable) + i + 1 + cmd.sbc_contrast,
		     TAB_RIGHT, T,
		     8,3);


	  df = df_numerator / df_denominator;

	  tab_float (t,  6,
		     (v * lines_per_variable) + i + 1 + cmd.sbc_contrast,
		     TAB_RIGHT, df,
		     8,3);

	  /* The Significance */

	  tab_float (t, 7, (v * lines_per_variable) + i + 1 + cmd.sbc_contrast,
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



/* Pre calculations */
static void
precalc ( struct cmd_oneway *cmd UNUSED )
{
  size_t i=0;

  for(i=0; i< n_vars ; ++i)
    {
      struct group_proc *gp = group_proc_get (vars[i]);
      struct group_statistics *totals = &gp->ugs;

      /* Create a hash for each of the dependent variables.
	 The hash contains a group_statistics structure,
	 and is keyed by value of the independent variable */

      gp->group_hash =
	hsh_create(4,
		   (hsh_compare_func *) compare_group,
		   (hsh_hash_func *) hash_group,
		   (hsh_free_func *) free_group,
		   (void *) var_get_width (indep_var) );


      totals->sum=0;
      totals->n=0;
      totals->ssq=0;
      totals->sum_diff=0;
      totals->maximum = - DBL_MAX;
      totals->minimum = DBL_MAX;
    }
}

static void
free_value (void *value_, const void *aux UNUSED)
{
  union value *value = value_;
  free (value);
}

static void
run_oneway (struct cmd_oneway *cmd,
            struct casereader *input,
            const struct dataset *ds)
{
  struct taint *taint;
  struct dictionary *dict = dataset_dict (ds);
  enum mv_class exclude;
  struct casereader *reader;
  struct ccase c;

  if (!casereader_peek (input, 0, &c))
    {
      casereader_destroy (input);
      return;
    }
  output_split_file_values (ds, &c);
  case_destroy (&c);

  taint = taint_clone (casereader_get_taint (input));

  global_group_hash = hsh_create(4,
				 (hsh_compare_func *) compare_values,
				 (hsh_hash_func *) hash_value,
				 free_value,
				 (void *) var_get_width (indep_var) );

  precalc(cmd);

  exclude = cmd->incl != ONEWAY_INCLUDE ? MV_ANY : MV_SYSTEM;
  input = casereader_create_filter_missing (input, &indep_var, 1,
                                            exclude, NULL);
  if (cmd->miss == ONEWAY_LISTWISE)
    input = casereader_create_filter_missing (input, vars, n_vars,
                                              exclude, NULL);
  input = casereader_create_filter_weight (input, dict, NULL, NULL);

  reader = casereader_clone (input);
  for (; casereader_read (reader, &c); case_destroy (&c))
    {
      size_t i;

      const double weight = dict_get_case_weight (dict, &c, NULL);

      const union value *indep_val = case_data (&c, indep_var);
      void **p = hsh_probe (global_group_hash, indep_val);
      if (*p == NULL)
        *p = value_dup (indep_val, var_get_width (indep_var));

      for ( i = 0 ; i < n_vars ; ++i )
	{
	  const struct variable *v = vars[i];

	  const union value *val = case_data (&c, v);

          struct group_proc *gp = group_proc_get (vars[i]);
	  struct hsh_table *group_hash = gp->group_hash;

	  struct group_statistics *gs;

	  gs = hsh_find(group_hash, (void *) indep_val );

	  if ( ! gs )
	    {
	      gs = xmalloc (sizeof *gs);
	      gs->id = *indep_val;
	      gs->sum=0;
	      gs->n=0;
	      gs->ssq=0;
	      gs->sum_diff=0;
	      gs->minimum = DBL_MAX;
	      gs->maximum = -DBL_MAX;

	      hsh_insert ( group_hash, (void *) gs );
	    }

	  if (!var_is_value_missing (v, val, exclude))
	    {
	      struct group_statistics *totals = &gp->ugs;

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

	  gp->n_groups = hsh_count ( group_hash );
	}

    }
  casereader_destroy (reader);

  postcalc(cmd);


  if ( stat_tables & STAT_HOMO )
    levene (dict, casereader_clone (input), indep_var, n_vars, vars, exclude);

  casereader_destroy (input);

  ostensible_number_of_groups = hsh_count (global_group_hash);

  if (!taint_has_tainted_successor (taint))
    output_oneway();
  taint_destroy (taint);
}


/* Post calculations for the ONEWAY command */
void
postcalc (  struct cmd_oneway *cmd UNUSED )
{
  size_t i=0;


  for(i = 0; i < n_vars ; ++i)
    {
      struct group_proc *gp = group_proc_get (vars[i]);
      struct hsh_table *group_hash = gp->group_hash;
      struct group_statistics *totals = &gp->ugs;

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

/*
  Local Variables:
  mode: c
  End:
*/
