/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by John Williams <johnr.williams@stonebow.otago.ac.nz>.

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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "alloc.h"
#include "str.h"
#include "dcdflib/cdflib.h"
#include "command.h"
#include "lexer.h"
#include "error.h"
#include "magic.h"
#include "value-labels.h"
#include "var.h"
#include "vfm.h"

/* (specification)
   "T-TEST" (tts_):
     groups=custom;
     variables=varlist("PV_NO_SCRATCH | PV_NUMERIC");
     *+pairs=custom;
     +missing=miss:!analysis/listwise,
             incl:include/!exclude;
     +format=fmt:!labels/nolabels;
     +criteria=:ci(d:criteria,"%s > 0. && %s < 1.").
*/
/* (declarations) */
/* (functions) */

#include "debug-print.h"

/* Command parsing information. */
static struct cmd_t_test cmd;

/* Variable for the GROUPS subcommand, if given. */
static struct variable *groups;

/* GROUPS: Number of values specified by the user; the values
   specified if any. */
static int n_groups_values;
static union value groups_values[2];

/* PAIRED: Number of pairs; each pair. */
static int n_pairs;
static struct variable *(*pairs)[2];

/* Routines to scan data and perform t-tests */
static void precalc (void);
static void postcalc (void);
static void g_postcalc (void);
static void t_pairs (void);
static void t_groups (void);
static int groups_calc (struct ccase *);
static int pairs_calc (struct ccase *);
static int z_calc (struct ccase *);

struct value_list
  {
    double sum;
    double ss;
    double n;
    struct value_list *next;
  };

/* general workhorses - should  move these to a separate library... */
double variance (double n, double ss, double sum);

double covariance (double x_sum, double x_n,
		   double y_sum, double y_n, double ss);

double pooled_variance (double n_1, double var_1,
			double n_2, double var_2);

double oneway (double *f, double *p, struct value_list *list);

double pearson_r (double c_xy, double c_xx, double c_yy);

double f_sig (double f, double dfn, double dfd);
double t_crt (double df, double q);
double t_sig (double t, double df);

/* massive function simply to remove any responsibility for output
   from the function which does the actual t-test calculations */
void print_t_groups (struct variable * grps, union value * g1, union value * g2,
		     double n1, double n2, double mean1, double mean2,
		     double sd1, double sd2, double se1, double se2,
		     double diff, double l_f, double l_p,
		     double p_t, double p_sig, double p_df, double p_sed,
		     double p_l, double p_h,
		     double s_t, double s_sig, double s_df, double s_sed,
		     double s_l, double s_h);

/* Global variables to communicate between calc() and postcalc()
   should move to a structure in the p union of variable... */
static double v1_n, v1_ss, v1_sum, v1_se, v1_var, v1_mean;
static double v2_n, v2_ss, v2_sum, v2_se, v2_var, v2_mean;
static double v1_z_sum, v1_z_ss;
static double v2_z_sum, v2_z_ss;
static double diff, se_diff, sp, xy_sum, xy_diff, xy_ss;
static int cur_var;

/* some defines for CDFlib */
#define FIND_P 1
#define FIND_CRITICAL_VALUE 2
#define ERROR_SIG -1

#ifdef DEBUGGING
static void debug_print (void);
#endif

/* Parses and executes the T-TEST procedure. */
int
cmd_t_test (void)
{
  struct cmd_t_test cmd;
  
  if (!lex_force_match_id ("T"))
    return CMD_FAILURE;
  lex_match ('-');
  lex_match_id ("TEST");

  if (!parse_t_test (&cmd))
    return CMD_FAILURE;

#if DEBUGGING
  debug_print ();
#endif

  if (n_pairs > 0)
    procedure (precalc, pairs_calc, postcalc);
  else
    /* probably groups then... */
    {
      printf ("\n\n  t-tests for independent samples of %s %s\n",
	      groups->name, groups->label);

      for (cur_var = 0; cur_var < cmd.n_variables; cur_var++)
	{
	  v1_n = v1_ss = v1_sum = v1_se = v1_var = v1_mean = 0.0;
	  v2_n = v2_ss = v2_sum = v2_se = v2_var = v2_mean = 0.0;
	  v1_z_sum = v1_z_ss = v2_z_sum = v2_z_ss = 0.0;
	  diff = se_diff = sp = xy_diff = xy_ss = xy_sum = 0.0;

	  procedure (precalc, groups_calc, g_postcalc);
	  procedure (precalc, z_calc, postcalc);
	}
    }

  return CMD_SUCCESS;
}

void
precalc (void)
{
  return;			/* rilly void... */
}

int
groups_calc (struct ccase * c)
{
  int bad_weight;
  double group, w;
  struct variable *v = cmd.v_variables[cur_var];
  double X = c->data[v->fv].f;

  /* Get the weight for this case. */
  w = dict_get_case_weight (default_dict, c);
  if (w <= 0.0 || w == SYSMIS)
    {
      w = 0.0;
      bad_weight = 1;
      printf ("Bad weight\n");
    }

  if (X == SYSMIS || X == 0.0)	/* FIXME: should be USER_MISSING? */
    {
      /* printf("Missing value\n"); */
      return 1;
    }
  else
    {
      X = X * w;
      group = c->data[groups->fv].f;

      if (group == groups_values[0].f)
	{
	  v1_sum += X;
	  v1_ss += X * X;
	  v1_n += w;
	}
      else if (group == groups_values[1].f)
	{
	  v2_sum += X;
	  v2_ss += X * X;
	  v2_n += w;
	}
    }

  return 1;
}

void
g_postcalc (void)
{
  v1_mean = v1_sum / v1_n;
  v2_mean = v2_sum / v2_n;
  return;
}

int				/* this pass generates the z-zcores */
z_calc (struct ccase * c)
{
  double group, z, w;
  struct variable *v = cmd.v_variables[cur_var];
  double X = c->data[v->fv].f;

  z = 0.0;

  /* Get the weight for this case. */
  w = dict_get_case_weight (default_dict, c);

  if (X == SYSMIS || X == 0.0)	/* FIXME: how to specify user missing? */
    {
      return 1;
    }
  else
    {
      group = c->data[groups->fv].f;
      X = w * X;

      if (group == groups_values[0].f)
	{
	  z = fabs (X - v1_mean);
	  v1_z_sum += z;
	  v1_z_ss += pow (z, 2);
	}
      else if (group == groups_values[1].f)
	{
	  z = fabs (X - v2_mean);
	  v2_z_ss += pow (z, 2);
	  v2_z_sum += z;
	}
    }

  return 1;
}


int
pairs_calc (struct ccase * c)
{
  int i;
  struct variable *v1, *v2;
  double X, Y;

  for (i = 0; i < n_pairs; i++)
    {

      v1 = pairs[i][0];
      v2 = pairs[i][1];
      X = c->data[v1->fv].f;
      Y = c->data[v2->fv].f;

      if (X == SYSMIS || Y == SYSMIS)
	{
	  printf ("Missing value\n");
	}
      else
	{
	  xy_sum += X * Y;
	  xy_diff += (X - Y);
	  xy_ss += pow ((X - Y), 2);
	  v1_sum += X;
	  v2_sum += Y;
	  v1_n++;
	  v2_n++;
	  v1_ss += (X * X);
	  v2_ss += (Y * Y);
	}
    }

  return 1;
}

void
postcalc (void)
{
  /* Calculate basic statistics */
  v1_var = variance (v1_n, v1_ss, v1_sum);	/* variances */
  v2_var = variance (v2_n, v2_ss, v2_sum);
  v1_se = sqrt (v1_var / v1_n);	/* standard errors */
  v2_se = sqrt (v2_var / v2_n);
  diff = v1_mean - v2_mean;

  if (n_pairs > 0)
    {
      t_pairs ();
    }
  else
    {
      t_groups ();
    }

  return;
}

void
t_groups (void)
{
  double df_pooled, t_pooled, t_sep, p_pooled, p_sep;
  double crt_t_p, crt_t_s, tmp, v1_z, v2_z, f_levene, p_levene;
  double df_sep, se_diff_s, se_diff_p;
  struct value_list *val_1, *val_2;

  /* Levene's test */
  val_1 = malloc (sizeof (struct value_list));
  val_1->sum = v1_z_sum;
  val_1->ss = v1_z_ss;
  val_1->n = v1_n;
  val_2 = malloc (sizeof (struct value_list));
  val_2->sum = v2_z_sum;
  val_2->ss = v2_z_ss;
  val_2->n = v2_n;

  val_1->next = val_2;
  val_2->next = NULL;

  f_levene = oneway (&f_levene, &p_levene, val_1);

  /* T test results for pooled variances */
  se_diff_p = sqrt (pooled_variance (v1_n, v1_var, v2_n, v2_var));
  df_pooled = v1_n + v2_n - 2.0;
  t_pooled = diff / se_diff_p;
  p_pooled = t_sig (t_pooled, df_pooled);
  crt_t_p = t_crt (df_pooled, 0.025);

  if ((2.0 * p_pooled) >= 1.0)
    p_pooled = 1.0 - p_pooled;

  /* oh god, the separate variance calculations... */
  t_sep = diff / sqrt ((v1_var / v1_n) + (v2_var / v2_n));

  tmp = (v1_var / v1_n) + (v2_var / v2_n);
  tmp = (v1_var / v1_n) / tmp;
  tmp = pow (tmp, 2);
  tmp = tmp / (v1_n - 1.0);
  v1_z = tmp;

  tmp = (v1_var / v1_n) + (v2_var / v2_n);
  tmp = (v2_var / v2_n) / tmp;
  tmp = pow (tmp, 2);
  tmp = tmp / (v2_n - 1.0);
  v2_z = tmp;

  tmp = 1.0 / (v1_z + v2_z);

  df_sep = tmp;
  p_sep = t_sig (t_sep, df_sep);
  if ((2.0 * p_sep) >= 1.0)
    p_sep = 1.0 - p_sep;
  crt_t_s = t_crt (df_sep, 0.025);
  se_diff_s = sqrt ((v1_var / v1_n) + (v2_var / v2_n));

  /* FIXME: convert to a proper PSPP output call */
  print_t_groups (groups, &groups_values[0], &groups_values[1],
		  v1_n, v2_n, v1_mean, v2_mean,
		  sqrt (v1_var), sqrt (v2_var), v1_se, v2_se,
		  diff, f_levene, p_levene,
		  t_pooled, 2.0 * p_pooled, df_pooled, se_diff_p,
		  diff - (crt_t_p * se_diff_p), diff + (crt_t_p * se_diff_p),
		  t_sep, 2.0 * p_sep, df_sep, se_diff_s,
		diff - (crt_t_s * se_diff_s), diff + (crt_t_s * se_diff_s));
  return;
}

void
t_pairs (void)
{
  double cov12, cov11, cov22, r, t, p, crt_t, sp, r_t, r_p;
  struct variable *v1, *v2;

  v1 = pairs[0][0];
  v2 = pairs[0][1];
  cov12 = covariance (v1_sum, v1_n, v2_sum, v2_n, xy_sum);
  cov11 = covariance (v1_sum, v1_n, v1_sum, v1_n, v1_ss);
  cov22 = covariance (v2_sum, v2_n, v2_sum, v2_n, v2_ss);
  r = pearson_r (cov12, cov11, cov22);
  /* this t and it's associated p is a significance test for the pearson's r */
  r_t = r * sqrt ((v1_n - 2.0) / (1.0 - (r * r)));
  r_p = t_sig (r_t, v1_n - 2.0);

  /* now we move to the t test for the difference in means */
  diff = xy_diff / v1_n;
  sp = sqrt (variance (v1_n, xy_ss, xy_diff));
  se_diff = sp / sqrt (v1_n);
  t = diff / se_diff;
  crt_t = t_crt (v1_n - 1.0, 0.025);
  p = t_sig (t, v1_n - 1.0);


  printf ("             Number of        2-tail\n");
  printf (" Variable      pairs    Corr   Sig      Mean    SD   SE of Mean\n");
  printf ("---------------------------------------------------------------\n");
  printf ("%s                                  %8.4f %8.4f %8.4f\n",
	  v1->name, v1_mean, sqrt (v1_var), v1_se);
  printf ("           %8.4f  %0.4f  %0.4f\n", v1_n, r, r_p);
  printf ("%s                                  %8.4f %8.4f %8.4f\n",
	  v2->name, v2_mean, sqrt (v2_var), v2_se);
  printf ("---------------------------------------------------------------\n");

  printf ("\n\n\n");
  printf ("      Paired Differences              |\n");
  printf (" Mean          SD         SE of Mean  |  t-value   df   2-tail Sig\n");
  printf ("--------------------------------------|---------------------------\n");

  printf ("%8.4f    %8.4f    %8.4f      | %8.4f %8.4f %8.4f\n",
	  diff, sp, se_diff, t, v1_n - 1.0, 2.0 * (1.0 - p));

  printf ("95pc CI (%8.4f, %8.4f)          |\n\n",
	  diff - (se_diff * crt_t), diff + (se_diff * crt_t));

  return;
}

static int parse_value (union value *);

/* Parses the GROUPS subcommand. */
int
tts_custom_groups (struct cmd_t_test *cmd unused)
{
  groups = parse_variable ();
  if (!groups)
    {
      lex_error (_("expecting variable name in GROUPS subcommand"));
      return 0;
    }
  if (groups->type == T_STRING && groups->width > MAX_SHORT_STRING)
    {
      msg (SE, _("Long string variable %s is not valid here."),
	   groups->name);
      return 0;
    }

  if (!lex_match ('('))
    {
      if (groups->type == NUMERIC)
	{
	  n_groups_values = 2;
	  groups_values[0].f = 1;
	  groups_values[1].f = 2;
	  return 1;
	}
      else
	{
	  msg (SE, _("When applying GROUPS to a string variable, at "
		     "least one value must be specified."));
	  return 0;
	}
    }
  
  if (!parse_value (&groups_values[0]))
    return 0;
  n_groups_values = 1;

  lex_match (',');

  if (lex_match (')'))
    return 1;

  if (!parse_value (&groups_values[1]))
    return 0;
  n_groups_values = 2;

  if (!lex_force_match (')'))
    return 0;

  return 1;
}

/* Parses the current token (numeric or string, depending on the
   variable in `groups') into value V and returns success. */
static int
parse_value (union value * v)
{
  if (groups->type == NUMERIC)
    {
      if (!lex_force_num ())
	return 0;
      v->f = tokval;
    }
  else
    {
      if (!lex_force_string ())
	return 0;
      strncpy (v->s, ds_value (&tokstr), ds_length (&tokstr));
    }

  lex_get ();

  return 1;
}

/* Parses the PAIRS subcommand. */
static int
tts_custom_pairs (struct cmd_t_test *cmd unused)
{
  struct variable **vars;
  int n_before_WITH;
  int n_vars;
  int paired;
  int extra;
#if DEBUGGING
  int n_predicted;
#endif

  if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
      && token != T_ALL)
    return 2;
  if (!parse_variables (default_dict, &vars, &n_vars,
			PV_DUPLICATE | PV_NUMERIC | PV_NO_SCRATCH))
    return 0;

  assert (n_vars);
  if (lex_match (T_WITH))
    {
      n_before_WITH = n_vars;

      if (!parse_variables (default_dict, &vars, &n_vars,
			    PV_DUPLICATE | PV_APPEND
			    | PV_NUMERIC | PV_NO_SCRATCH))
	{
	  free (vars);
	  return 0;
	}
    }
  else
    n_before_WITH = 0;

  paired = (lex_match ('(') && lex_match_id ("PAIRED") && lex_match (')'));

  if (paired)
    {
      if (n_before_WITH * 2 != n_vars)
	{
	  free (vars);
	  msg (SE, _("PAIRED was specified but the number of variables "
		     "preceding WITH (%d) did not match the number "
		     "following (%d)."),
	       n_before_WITH, n_vars - n_before_WITH);
	  return 0;
	}

      extra = n_before_WITH;
    }
  else if (n_before_WITH)
    extra = n_before_WITH * (n_vars - n_before_WITH);
  else
    {
      if (n_vars < 2)
	{
	  free (vars);
	  msg (SE, _("At least two variables must be specified "
		     "on PAIRS."));
	  return 0;
	}

      extra = n_vars * (n_vars - 1) / 2;
    }

#if DEBUGGING
  n_predicted = n_pairs + extra;
#endif

  pairs = xrealloc (pairs, sizeof (struct variable *[2]) * (n_pairs + extra));

  if (paired)
    {
      int i;

      for (i = 0; i < extra; i++)
	{
	  pairs[n_pairs][0] = vars[i];
	  pairs[n_pairs++][1] = vars[i + extra];
	}
    }
  else if (n_before_WITH)
    {
      int i;

      for (i = 0; i < n_before_WITH; i++)
	{
	  int j;

	  for (j = n_before_WITH; j < n_vars; j++)
	    {
	      pairs[n_pairs][0] = vars[i];
	      pairs[n_pairs++][1] = vars[j];
	    }
	}
    }
  else
    {
      int i;

      for (i = 0; i < n_vars; i++)
	{
	  int j;

	  for (j = i + 1; j < n_vars; j++)
	    {
	      pairs[n_pairs][0] = vars[i];
	      pairs[n_pairs++][1] = vars[j];
	    }
	}
    }

#if DEBUGGING
  assert (n_pairs == n_predicted);
#endif

  free (vars);
  return 1;
}

#if DEBUGGING
static void
debug_print (void)
{
  printf ("T-TEST\n");
  if (groups)
    {
      printf ("  GROUPS=%s", groups->name);
      if (n_groups_values)
	{
	  int i;

	  printf (" (");
	  for (i = 0; i < n_groups_values; i++)
	    if (groups->type == NUMERIC)
	      printf ("%g%s", groups_values[i].f, i ? " " : "");
	    else
	      printf ("%.*s%s", groups->width, groups_values[i].s,
		      i ? " " : "");
	  printf (")");
	}
      printf ("\n");
    }
  if (cmd.n_variables)
    {
      int i;

      printf ("  VARIABLES=");
      for (i = 0; i < cmd.n_variables; i++)
	printf ("%s ", cmd.v_variables[i]->name);
      printf ("\n");
    }
  if (cmd.sbc_pairs)
    {
      int i;

      printf ("  PAIRS=");
      for (i = 0; i < n_pairs; i++)
	printf ("%s ", pairs[i][0]->name);
      printf ("WITH");
      for (i = 0; i < n_pairs; i++)
	printf (" %s", pairs[i][1]->name);
      printf (" (PAIRED)\n");
    }
  printf ("  MISSING=%s %s\n",
	  cmd.miss == TTS_ANALYSIS ? "ANALYSIS" : "LISTWISE",
	  cmd.miss == TTS_INCLUDE ? "INCLUDE" : "EXCLUDE");
  printf ("  FORMAT=%s\n",
	  cmd.fmt == TTS_LABELS ? "LABELS" : "NOLABELS");
  if (cmd.criteria != NOT_LONG)
    printf ("  CRITERIA=%f\n", cmd.criteria);
}

#endif /* DEBUGGING */

/* Here are some general routines tha should probably be moved into
   a separate library and documented as part of the PSPP "API"   */
double
variance (double n, double ss, double sum)
{
  return ((ss - ((sum * sum) / n)) / (n - 1.0));
}

double
pooled_variance (double n_1, double var_1, double n_2, double var_2)
{
  double tmp;

  tmp = n_1 + n_2 - 2.0;
  tmp = (((n_1 - 1.0) * var_1) + ((n_2 - 1.0) * var_2)) / tmp;
  tmp = tmp * ((n_1 + n_2) / (n_1 * n_2));
  return tmp;
}

double
oneway (double *f, double *p, struct value_list *levels)
{
  double k, SSTR, SSE, SSTO, N, MSTR, MSE, sum, dftr, dfe, print;
  struct value_list *g;

  k = 0.0;

  for (g = levels; g != NULL; g = g->next)
    {
      k++;
      sum += g->sum;
      N += g->n;
      SSTR += g->ss - (pow (g->sum, 2) / g->n);
      SSTO += g->ss;
    }

  SSTO = SSTO - (pow (sum, 2) / N);
  SSE = SSTO - SSTR;

  dftr = N - k;
  dfe = k - 1.0;
  MSTR = SSTR / dftr;
  MSE = SSE / dfe;

  *f = (MSE / MSTR);
  *p = f_sig (*f, dfe, dftr);

  print = 1.0;
  if (print == 1.0)
    {
      printf ("sum1 %f, sum2 %f, ss1 %f, ss2 %f\n",
	      levels->sum, levels->next->sum, levels->ss, levels->next->ss);
      printf ("                - - - - - - O N E W A Y - - - - - -\n\n");
      printf ("   Variable %s %s\n",
	      cmd.v_variables[0]->name, cmd.v_variables[0]->label);
      printf ("By Variable %s %s\n", groups->name, groups->label);
      printf ("\n             Analysis of Variance\n\n");
      printf ("                    Sum of    Mean     F       F\n");
      printf ("Source       D.F.  Squares  Squares  Ratio   Prob\n\n");
      printf ("Between   %8.0f %8.4f %8.4f %8.4f %8.4f\n",
	      dfe, SSE, MSE, *f, *p);
      printf ("Within    %8.0f %8.4f %8.4f\n", dftr, SSTR, MSTR);
      printf ("Total     %8.0f %8.4f\n\n\n", N - 1.0, SSTO);
    }
  return (*f);
}

double
f_sig (double f, double dfn, double dfd)
{
  int which, status;
  double p, q, bound;

  which = FIND_P;
  status = 1;
  p = q = bound = 0.0;
  cdff (&which, &p, &q, &f, &dfn, &dfd, &status, &bound);

  switch (status)
    {
    case -1:
      {
	printf ("Parameter 1 is out of range\n");
	break;
      }
    case -2:
      {
	printf ("Parameter 2 is out of range\n");
	break;
      }
    case -3:
      {
	printf ("Parameter 3 is out of range\n");
	break;
      }
    case -4:
      {
	printf ("Parameter 4 is out of range\n");
	break;
      }
    case -5:
      {
	printf ("Parameter 5 is out of range\n");
	break;
      }
    case -6:
      {
	printf ("Parameter 6 is out of range\n");
	break;
      }
    case -7:
      {
	printf ("Parameter 7 is out of range\n");
	break;
      }
    case -8:
      {
	printf ("Parameter 8 is out of range\n");
	break;
      }
    case 0:
      {
	/* printf( "Command completed successfully\n" ); */
	break;
      }
    case 1:
      {
	printf ("Answer appears to be lower than the lowest search bound\n");
	break;
      }
    case 2:
      {
	printf ("Answer appears to be higher than the greatest search bound\n");
	break;
      }
    case 3:
      {
	printf ("P - Q NE 1\n");
	break;
      }
    }

  if (status)
    {
      return (double) ERROR_SIG;
    }
  else
    {
      return q;
    }
}

double
t_crt (double df, double q)
{
  int which, status;
  double p, bound, t;

  which = FIND_CRITICAL_VALUE;
  bound = 0.0;
  p = 1.0 - q;
  t = 0.0;

  cdft (&which, &p, &q, &t, &df, &status, &bound);

  switch (status)
    {
    case -1:
      {
	printf ("t_crt: Parameter 1 is out of range\n");
	break;
      }
    case -2:
      {
	printf ("t_crt: value of p (%f) is out of range\n", p);
	break;
      }
    case -3:
      {
	printf ("t_crt: value of q (%f) is out of range\n", q);
	break;
      }
    case -4:
      {
	printf ("t_crt: value of df (%f) is out of range\n", df);
	break;
      }
    case -5:
      {
	printf ("t_crt: Parameter 5 is out of range\n");
	break;
      }
    case -6:
      {
	printf ("t_crt: Parameter 6 is out of range\n");
	break;
      }
    case -7:
      {
	printf ("t_crt: Parameter 7 is out of range\n");
	break;
      }
    case 0:
      {
	/* printf( "Command completed successfully\n" ); */
	break;
      }
    case 1:
      {
	printf ("t_crt: Answer appears to be lower than the lowest search bound\n");
	break;
      }
    case 2:
      {
	printf ("t_crt: Answer appears to be higher than the greatest search bound\n");
	break;
      }
    case 3:
      {
	printf ("t_crt: P - Q NE 1\n");
	break;
      }
    }

  if (status)
    {
      return (double) ERROR_SIG;
    }
  else
    {
      return t;
    }
}

double
t_sig (double t, double df)
{
  int which, status;
  double p, q, bound;

  which = FIND_P;
  q = 0.0;
  p = 0.0;
  bound = 0.0;

  cdft (&which, &p, &q, &t, &df, &status, &bound);

  switch (status)
    {
    case -1:
      {
	printf ("t-sig: Parameter 1 is out of range\n");
	break;
      }
    case -2:
      {
	printf ("t-sig: Parameter 2 is out of range\n");
	break;
      }
    case -3:
      {
	printf ("t-sig: Parameter 3 is out of range\n");
	break;
      }
    case -4:
      {
	printf ("t-sig: Parameter 4 is out of range\n");
	break;
      }
    case -5:
      {
	printf ("t-sig: Parameter 5 is out of range\n");
	break;
      }
    case -6:
      {
	printf ("t-sig: Parameter 6 is out of range\n");
	break;
      }
    case -7:
      {
	printf ("t-sig: Parameter 7 is out of range\n");
	break;
      }
    case 0:
      {
	/* printf( "Command completed successfully\n" ); */
	break;
      }
    case 1:
      {
	printf ("t-sig: Answer appears to be lower than the lowest search bound\n");
	break;
      }
    case 2:
      {
	printf ("t-sig: Answer appears to be higher than the greatest search bound\n");
	break;
      }
    case 3:
      {
	printf ("t-sig: P - Q NE 1\n");
	break;
      }
    }

  if (status)
    {
      return (double) ERROR_SIG;
    }
  else
    {
      return q;
    }
}

double
covariance (double x_sum, double x_n, double y_sum, double y_n, double ss)
{
  double tmp;

  tmp = x_sum * y_sum;
  tmp = tmp / x_n;
  tmp = ss - tmp;
  tmp = (tmp / (x_n + y_n - 1.0));
  return tmp;
}

double
pearson_r (double c_xy, double c_xx, double c_yy)
{
  return (c_xy / (sqrt (c_xx * c_yy)));
}

void 
print_t_groups (struct variable * grps, union value * g1, union value * g2,
		double n1, double n2, double mean1, double mean2,
		double sd1, double sd2, double se1, double se2,
		double diff, double l_f, double l_p,
		double p_t, double p_sig, double p_df, double p_sed,
		double p_l, double p_h,
		double s_t, double s_sig, double s_df, double s_sed,
		double s_l, double s_h)
{

  /* Display all this shit as SPSS 6.0 does (roughly) */
  printf ("\n\n                 Number                                 \n");
  printf ("   Variable     of Cases    Mean      SD      SE of Mean\n");
  printf ("-----------------------------------------------------------\n");
  printf ("   %s %s\n\n", cmd.v_variables[cur_var]->name, cmd.v_variables[cur_var]->label);
  printf ("%s %8.4f %8.0f    %8.4f  %8.3f    %8.3f\n",
	  val_labs_find (grps->val_labs, *g1), g1->f, n1, mean1, sd1, se1);
  printf ("%s %8.4f %8.0f    %8.4f  %8.3f    %8.3f\n",
	  val_labs_find (grps->val_labs, *g2), g2->f, n2, mean2, sd2, se2);
  printf ("-----------------------------------------------------------\n");
  printf ("\n   Mean Difference = %8.4f\n", diff);
  printf ("\n   Levene's Test for Equality of Variances: F= %.3f  P= %.3f\n",
	  l_f, l_p);
  printf ("\n\n   t-test for Equality of Means                         95pc     \n");
  printf ("Variances   t-value    df   2-Tail Sig SE of Diff    CI for Diff  \n");
  printf ("-----------------------------------------------------------------\n");
  printf ("Equal     %8.2f %8.0f %8.3f %8.3f (%8.3f, %8.3f)\n",
	  p_t, p_df, p_sig, p_sed, p_l, p_h);
  printf ("Unequal   %8.2f %8.2f %8.3f %8.3f (%8.3f, %8.3f)\n",
	  s_t, s_df, s_sig, s_sed, s_l, s_h);
  printf ("-----------------------------------------------------------------\n");
}

/* 
   Local Variables:
   mode: c
   End:
*/
