/* PSPP - computes sample statistics. -*-c-*-

   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by John Williams <johnr.williams@stonebow.otago.ac.nz>.
   Almost completly re-written by John Darrington 2004

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
#include "message.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "alloc.h"
#include "case.h"
#include "casefile.h"
#include "command.h"
#include "compiler.h"
#include "dictionary.h"
#include "message.h"
#include "group-proc.h"
#include "hash.h"
#include "levene.h"
#include "lexer.h"
#include "magic.h"
#include "misc.h"
#include "size_max.h"
#include "manager.h"
#include "str.h"
#include "table.h"
#include "value-labels.h"
#include "variable.h"
#include "procedure.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* (headers) */

/* (specification)
   "T-TEST" (tts_):
     +groups=custom;
     testval=double;
     variables=varlist("PV_NO_SCRATCH | PV_NUMERIC");
     pairs=custom;
     +missing=miss:!analysis/listwise,
             incl:include/!exclude;
     format=fmt:!labels/nolabels;
     criteria=:cin(d:criteria,"%s > 0. && %s < 1.").
*/
/* (declarations) */
/* (functions) */




/* Function to use for testing for missing values */
static is_missing_func *value_is_missing;

/* Variable for the GROUPS subcommand, if given. */
static struct variable *indep_var;

enum comparison
  {
    CMP_LE = -2,
    CMP_EQ = 0,
  };

struct group_properties
{
  /* The comparison criterion */
  enum comparison criterion;

  /* The width of the independent variable */
  int indep_width ;  

  union {
    /* The value of the independent variable at which groups are determined to 
       belong to one group or the other */
    double critical_value;
    

    /* The values of the independent variable for each group */
    union value g_value[2];
  } v ;

};


static struct group_properties gp ;



/* PAIRS: Number of pairs to be compared ; each pair. */
static int n_pairs = 0 ;
struct pair 
{
  /* The variables comprising the pair */
  struct variable *v[2];

  /* The number of valid variable pairs */
  double n;

  /* The sum of the members */
  double sum[2];

  /* sum of squares of the members */
  double ssq[2];

  /* Std deviation of the members */
  double std_dev[2];


  /* Sample Std deviation of the members */
  double s_std_dev[2];

  /* The means of the members */
  double mean[2];

  /* The correlation coefficient between the variables */
  double correlation;

  /* The sum of the differences */
  double sum_of_diffs;

  /* The sum of the products */
  double sum_of_prod;

  /* The mean of the differences */
  double mean_diff;

  /* The sum of the squares of the differences */
  double ssq_diffs;

  /* The std deviation of the differences */
  double std_dev_diff;
};

static struct pair *pairs=0;

static int parse_value (union value * v, int type) ;

/* Structures and Functions for the Statistics Summary Box */
struct ssbox;
typedef void populate_ssbox_func(struct ssbox *ssb,
					    struct cmd_t_test *cmd);
typedef void finalize_ssbox_func(struct ssbox *ssb);

struct ssbox
{
  struct tab_table *t;

  populate_ssbox_func *populate;
  finalize_ssbox_func *finalize;

};

/* Create a ssbox */
void ssbox_create(struct ssbox *ssb,   struct cmd_t_test *cmd, int mode);

/* Populate a ssbox according to cmd */
void ssbox_populate(struct ssbox *ssb, struct cmd_t_test *cmd);

/* Submit and destroy a ssbox */
void ssbox_finalize(struct ssbox *ssb);

/* A function to create, populate and submit the Paired Samples Correlation 
   box */
void pscbox(void);


/* Structures and Functions for the Test Results Box */
struct trbox;

typedef void populate_trbox_func(struct trbox *trb,
				 struct cmd_t_test *cmd);
typedef void finalize_trbox_func(struct trbox *trb);

struct trbox {
  struct tab_table *t;
  populate_trbox_func *populate;
  finalize_trbox_func *finalize;
};

/* Create a trbox */
void trbox_create(struct trbox *trb,   struct cmd_t_test *cmd, int mode);

/* Populate a ssbox according to cmd */
void trbox_populate(struct trbox *trb, struct cmd_t_test *cmd);

/* Submit and destroy a ssbox */
void trbox_finalize(struct trbox *trb);

/* Which mode was T-TEST invoked */
enum {
  T_1_SAMPLE = 0 ,
  T_IND_SAMPLES, 
  T_PAIRED
};


static int common_calc (const struct ccase *, void *);
static void common_precalc (struct cmd_t_test *);
static void common_postcalc (struct cmd_t_test *);

static int one_sample_calc (const struct ccase *, void *);
static void one_sample_precalc (struct cmd_t_test *);
static void one_sample_postcalc (struct cmd_t_test *);

static int  paired_calc (const struct ccase *, void *);
static void paired_precalc (struct cmd_t_test *);
static void paired_postcalc (struct cmd_t_test *);

static void group_precalc (struct cmd_t_test *);
static int  group_calc (const struct ccase *, struct cmd_t_test *);
static void group_postcalc (struct cmd_t_test *);


static bool calculate(const struct casefile *cf, void *_mode);

static  int mode;

static struct cmd_t_test cmd;

static int bad_weight_warn;


static int compare_group_binary(const struct group_statistics *a, 
				const struct group_statistics *b, 
				const struct group_properties *p);


static unsigned  hash_group_binary(const struct group_statistics *g, 
				   const struct group_properties *p);



int
cmd_t_test(void)
{
  bool ok;
  
  if ( !parse_t_test(&cmd) )
    return CMD_FAILURE;

  if (! cmd.sbc_criteria)
    cmd.criteria=0.95;

  {
    int m=0;
    if (cmd.sbc_testval) ++m;
    if (cmd.sbc_groups) ++m;
    if (cmd.sbc_pairs) ++m;

    if ( m != 1)
      {
	msg(SE, 
	    _("TESTVAL, GROUPS and PAIRS subcommands are mutually exclusive.")
	    );
        free_t_test(&cmd);
	return CMD_FAILURE;
      }
  }

  if (cmd.sbc_testval) 
    mode=T_1_SAMPLE;
  else if (cmd.sbc_groups)
    mode=T_IND_SAMPLES;
  else
    mode=T_PAIRED;

  if ( mode == T_PAIRED) 
    {
      if (cmd.sbc_variables) 
	{
	  msg(SE, _("VARIABLES subcommand is not appropriate with PAIRS"));
          free_t_test(&cmd);
	  return CMD_FAILURE;
	}
      else
	{
	  /* Iterate through the pairs and put each variable that is a 
	     member of a pair into cmd.v_variables */

	  int i;
	  struct hsh_iterator hi;
	  struct hsh_table *hash;
	  struct variable *v;

	  hash = hsh_create (n_pairs, compare_var_names, hash_var_name, 0, 0);

	  for (i=0; i < n_pairs; ++i)
	    {
	      hsh_insert(hash,pairs[i].v[0]);
	      hsh_insert(hash,pairs[i].v[1]);
	    }

	  assert(cmd.n_variables == 0);
	  cmd.n_variables = hsh_count(hash);

	  cmd.v_variables = xnrealloc (cmd.v_variables, cmd.n_variables,
                                       sizeof *cmd.v_variables);
	  /* Iterate through the hash */
	  for (i=0,v = (struct variable *) hsh_first(hash,&hi);
	       v != 0;
	       v=hsh_next(hash,&hi) ) 
	    cmd.v_variables[i++]=v;

	  hsh_destroy(hash);
	}
    }
  else if ( !cmd.sbc_variables) 
    {
      msg(SE, _("One or more VARIABLES must be specified."));
      free_t_test(&cmd);
      return CMD_FAILURE;
    }


  /* If /MISSING=INCLUDE is set, then user missing values are ignored */
  if (cmd.incl == TTS_INCLUDE ) 
    value_is_missing = mv_is_value_system_missing;
  else
    value_is_missing = mv_is_value_missing;

  bad_weight_warn = 1;

  ok = multipass_procedure_with_splits (calculate, &cmd);

  n_pairs=0;
  free(pairs);
  pairs=0;

  if ( mode == T_IND_SAMPLES) 
    {
      int v;
      /* Destroy any group statistics we created */
      for (v = 0 ; v < cmd.n_variables ; ++v ) 
	{
	  struct group_proc *grpp = group_proc_get (cmd.v_variables[v]);
	  hsh_destroy (grpp->group_hash);
	}
    }
    
  free_t_test(&cmd);
  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

static int
tts_custom_groups (struct cmd_t_test *cmd UNUSED)
{
  int n_group_values=0;

  lex_match('=');

  indep_var = parse_variable ();
  if (!indep_var)
    {
      lex_error ("expecting variable name in GROUPS subcommand");
      return 0;
    }

  if (indep_var->type == T_STRING && indep_var->width > MAX_SHORT_STRING)
    {
      msg (SE, _("Long string variable %s is not valid here."),
	   indep_var->name);
      return 0;
    }

  if (!lex_match ('('))
    {
      if (indep_var->type == NUMERIC)
	{
	  gp.v.g_value[0].f = 1;
	  gp.v.g_value[1].f = 2;

	  gp.criterion = CMP_EQ;
	  
	  n_group_values = 2;

	  return 1;
	}
      else
	{
	  msg (SE, _("When applying GROUPS to a string variable, two "
		     "values must be specified."));
	  return 0;
	}
    }

  if (!parse_value (&gp.v.g_value[0], indep_var->type))
      return 0;

  lex_match (',');
  if (lex_match (')'))
    {
      if (indep_var->type != NUMERIC)
	{

	  msg (SE, _("When applying GROUPS to a string variable, two "
		     "values must be specified."));
	  return 0;
	}
      gp.criterion = CMP_LE;
      gp.v.critical_value = gp.v.g_value[0].f;

      n_group_values = 1;
      return 1;
    }

  if (!parse_value (&gp.v.g_value[1], indep_var->type))
    return 0;

  n_group_values = 2;
  if (!lex_force_match (')'))
    return 0;

  if ( n_group_values == 2 ) 
    gp.criterion = CMP_EQ ;
  else
    gp.criterion = CMP_LE ;


  return 1;
}


static int
tts_custom_pairs (struct cmd_t_test *cmd UNUSED)
{
  struct variable **vars;
  size_t n_vars;
  size_t n_pairs_local;

  size_t n_before_WITH;
  size_t n_after_WITH = SIZE_MAX;
  int paired ; /* Was the PAIRED keyword given ? */

  lex_match('=');

  n_vars=0;
  if (!parse_variables (default_dict, &vars, &n_vars,
			PV_DUPLICATE | PV_NUMERIC | PV_NO_SCRATCH))
    {
      free (vars);
      return 0;
    }
  assert (n_vars);

  n_before_WITH = 0;
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
      n_after_WITH = n_vars - n_before_WITH;
    }

  paired = (lex_match ('(') && lex_match_id ("PAIRED") && lex_match (')'));

  /* Determine the number of pairs needed */
  if (paired)
    {
      if (n_before_WITH != n_after_WITH)
	{
	  free (vars);
	  msg (SE, _("PAIRED was specified but the number of variables "
		     "preceding WITH (%d) did not match the number "
		     "following (%d)."),
	       n_before_WITH, n_after_WITH );
	  return 0;
	}
      n_pairs_local = n_before_WITH;
    }
  else if (n_before_WITH > 0) /* WITH keyword given, but not PAIRED keyword */
    {
      n_pairs_local = n_before_WITH * n_after_WITH ;
    }
  else /* Neither WITH nor PAIRED keyword given */
    {
      if (n_vars < 2)
	{
	  free (vars);
	  msg (SE, _("At least two variables must be specified "
		     "on PAIRS."));
	  return 0;
	}

      /* how many ways can you pick 2 from n_vars ? */
      n_pairs_local = n_vars * (n_vars - 1) / 2;
    }


  /* Allocate storage for the pairs */
  pairs = xnrealloc (pairs, n_pairs + n_pairs_local, sizeof *pairs);

  /* Populate the pairs with the appropriate variables */
  if ( paired ) 
    {
      int i;

      assert(n_pairs_local == n_vars / 2);
      for (i = 0; i < n_pairs_local; ++i)
	{
	  pairs[i].v[n_pairs] = vars[i];
	  pairs[i].v[n_pairs + 1] = vars[i + n_pairs_local];
	}
    }
  else if (n_before_WITH > 0) /* WITH keyword given, but not PAIRED keyword */
    {
      int i,j;
      size_t p = n_pairs;

      for(i=0 ; i < n_before_WITH ; ++i ) 
	{
	  for(j=0 ; j < n_after_WITH ; ++j)
	    {
	      pairs[p].v[0] = vars[i];
	      pairs[p].v[1] = vars[j+n_before_WITH];
	      ++p;
	    }
	}
    }
  else /* Neither WITH nor PAIRED given */
    {
      size_t i,j;
      size_t p=n_pairs;
      
      for(i=0 ; i < n_vars ; ++i ) 
	{
	  for(j=i+1 ; j < n_vars ; ++j)
	    {
	      pairs[p].v[0] = vars[i];
	      pairs[p].v[1] = vars[j];
	      ++p;
	    }
	}
    }

  n_pairs+=n_pairs_local;

  free (vars);
  return 1;
}

/* Parses the current token (numeric or string, depending on type)
    value v and returns success. */
static int
parse_value (union value * v, int type )
{
  if (type == NUMERIC)
    {
      if (!lex_force_num ())
	return 0;
      v->f = tokval;
    }
  else
    {
      if (!lex_force_string ())
	return 0;
      strncpy (v->s, ds_c_str (&tokstr), ds_length (&tokstr));
    }

  lex_get ();

  return 1;
}


/* Implementation of the SSBOX object */

void ssbox_base_init(struct ssbox *this, int cols,int rows);

void ssbox_base_finalize(struct ssbox *ssb);

void ssbox_one_sample_init(struct ssbox *this, 
			   struct cmd_t_test *cmd );

void ssbox_independent_samples_init(struct ssbox *this,
				    struct cmd_t_test *cmd);

void ssbox_paired_init(struct ssbox *this,
			   struct cmd_t_test *cmd);


/* Factory to create an ssbox */
void 
ssbox_create(struct ssbox *ssb, struct cmd_t_test *cmd, int mode)
{
    switch (mode) 
      {
      case T_1_SAMPLE:
	ssbox_one_sample_init(ssb,cmd);
	break;
      case T_IND_SAMPLES:
	ssbox_independent_samples_init(ssb,cmd);
	break;
      case T_PAIRED:
	ssbox_paired_init(ssb,cmd);
	break;
      default:
	assert(0);
      }
}



/* Despatcher for the populate method */
void
ssbox_populate(struct ssbox *ssb,struct cmd_t_test *cmd)
{
  ssb->populate(ssb,cmd);
}


/* Despatcher for finalize */
void
ssbox_finalize(struct ssbox *ssb)
{
  ssb->finalize(ssb);
}


/* Submit the box and clear up */
void 
ssbox_base_finalize(struct ssbox *ssb)
{
  tab_submit(ssb->t);
}



/* Initialize a ssbox struct */
void 
ssbox_base_init(struct ssbox *this, int cols,int rows)
{
  this->finalize = ssbox_base_finalize;
  this->t = tab_create (cols, rows, 0);

  tab_columns (this->t, SOM_COL_DOWN, 1);
  tab_headers (this->t,0,0,1,0); 
  tab_box (this->t, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, cols -1, rows -1 );
  tab_hline(this->t, TAL_2,0,cols-1,1);
  tab_dim (this->t, tab_natural_dimensions);
}

void  ssbox_one_sample_populate(struct ssbox *ssb,
			      struct cmd_t_test *cmd);

/* Initialize the one_sample ssbox */
void 
ssbox_one_sample_init(struct ssbox *this, 
			   struct cmd_t_test *cmd )
{
  const int hsize=5;
  const int vsize=cmd->n_variables+1;

  this->populate = ssbox_one_sample_populate;

  ssbox_base_init(this, hsize,vsize);
  tab_title (this->t, 0, _("One-Sample Statistics"));
  tab_vline(this->t, TAL_2, 1,0,vsize - 1);
  tab_text (this->t, 1, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (this->t, 2, 0, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (this->t, 3, 0, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (this->t, 4, 0, TAB_CENTER | TAT_TITLE, _("SE. Mean"));
}

void ssbox_independent_samples_populate(struct ssbox *ssb,
					struct cmd_t_test *cmd);

/* Initialize the independent samples ssbox */
void 
ssbox_independent_samples_init(struct ssbox *this, 
	struct cmd_t_test *cmd)
{
  int hsize=6;
  int vsize = cmd->n_variables*2 +1;

  this->populate = ssbox_independent_samples_populate;

  ssbox_base_init(this, hsize,vsize);
  tab_title (this->t, 0, _("Group Statistics"));
  tab_vline(this->t,0,1,0,vsize - 1);
  tab_text (this->t, 1, 0, TAB_CENTER | TAT_TITLE, indep_var->name);
  tab_text (this->t, 2, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (this->t, 3, 0, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (this->t, 4, 0, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (this->t, 5, 0, TAB_CENTER | TAT_TITLE, _("SE. Mean"));
}


/* Populate the ssbox for independent samples */
void 
ssbox_independent_samples_populate(struct ssbox *ssb,
			      struct cmd_t_test *cmd)
{
  int i;

  char *val_lab0=0;
  char *val_lab1=0;
  double indep_value[2];

  char prefix[2][3]={"",""};

  if ( indep_var->type == NUMERIC ) 
    {
      val_lab0 = val_labs_find( indep_var->val_labs,gp.v.g_value[0]); 
      val_lab1 = val_labs_find( indep_var->val_labs,gp.v.g_value[1]);
    }
  else
    {
      val_lab0 = gp.v.g_value[0].s;
      val_lab1 = gp.v.g_value[1].s;
    }

  if (gp.criterion == CMP_LE ) 
    {
      strcpy(prefix[0],"< ");
      strcpy(prefix[1],">=");
      indep_value[0] = gp.v.critical_value;
      indep_value[1] = gp.v.critical_value;
    }
  else
    {
      indep_value[0] = gp.v.g_value[0].f;
      indep_value[1] = gp.v.g_value[1].f;
    }

  assert(ssb->t);

  for (i=0; i < cmd->n_variables; ++i)
    {
      struct variable *var = cmd->v_variables[i];
      struct hsh_table *grp_hash = group_proc_get (var)->group_hash;
      int count=0;

      tab_text (ssb->t, 0, i*2+1, TAB_LEFT, cmd->v_variables[i]->name);

      if (val_lab0)
	tab_text (ssb->t, 1, i*2+1, TAB_LEFT | TAT_PRINTF, 
		  "%s%s", prefix[0], val_lab0);
      else
	  tab_text (ssb->t, 1, i*2+1, TAB_LEFT | TAT_PRINTF, 
		    "%s%g", prefix[0], indep_value[0]);


      if (val_lab1)
	tab_text (ssb->t, 1, i*2+1+1, TAB_LEFT | TAT_PRINTF, 
		  "%s%s", prefix[1], val_lab1);
      else
	  tab_text (ssb->t, 1, i*2+1+1, TAB_LEFT | TAT_PRINTF, 
		    "%s%g", prefix[1], indep_value[1]);


      /* Fill in the group statistics */
      for ( count = 0 ; count < 2 ; ++count ) 
	{
	  union value search_val;

	  struct group_statistics *gs;

	  if ( gp.criterion == CMP_LE ) 
	    {
	      if ( count == 0 ) 
		{
		  /*  less than ( < )  case */
		  search_val.f = gp.v.critical_value - 1.0;
		}
	      else
		{
		  /* >= case  */
		  search_val.f = gp.v.critical_value + 1.0;
		}
	    }
	  else
	    {
	      search_val = gp.v.g_value[count];
	    }

	  gs = hsh_find(grp_hash, (void *) &search_val);
	  assert(gs);

	  tab_float(ssb->t, 2 ,i*2+count+1, TAB_RIGHT, gs->n, 2, 0);
	  tab_float(ssb->t, 3 ,i*2+count+1, TAB_RIGHT, gs->mean, 8, 2);
	  tab_float(ssb->t, 4 ,i*2+count+1, TAB_RIGHT, gs->std_dev, 8, 3);
	  tab_float(ssb->t, 5 ,i*2+count+1, TAB_RIGHT, gs->se_mean, 8, 3);
	}
    }
}


void ssbox_paired_populate(struct ssbox *ssb,
			   struct cmd_t_test *cmd);

/* Initialize the paired values ssbox */
void 
ssbox_paired_init(struct ssbox *this, struct cmd_t_test *cmd UNUSED)
{
  int hsize=6;

  int vsize = n_pairs*2+1;

  this->populate = ssbox_paired_populate;

  ssbox_base_init(this, hsize,vsize);
  tab_title (this->t, 0, _("Paired Sample Statistics"));
  tab_vline(this->t,TAL_0,1,0,vsize-1);
  tab_vline(this->t,TAL_2,2,0,vsize-1);
  tab_text (this->t, 2, 0, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (this->t, 3, 0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text (this->t, 4, 0, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (this->t, 5, 0, TAB_CENTER | TAT_TITLE, _("SE. Mean"));
}


/* Populate the ssbox for paired values */
void 
ssbox_paired_populate(struct ssbox *ssb,struct cmd_t_test *cmd UNUSED)
{
  int i;

  assert(ssb->t);

  for (i=0; i < n_pairs; ++i)
    {
      int j;

      tab_text (ssb->t, 0, i*2+1, TAB_LEFT | TAT_PRINTF , _("Pair %d"),i);

      for (j=0 ; j < 2 ; ++j) 
	{
	  struct group_statistics *gs;

	  gs = &group_proc_get (pairs[i].v[j])->ugs;

	  /* Titles */

	  tab_text (ssb->t, 1, i*2+j+1, TAB_LEFT, pairs[i].v[j]->name);

	  /* Values */
	  tab_float (ssb->t,2, i*2+j+1, TAB_RIGHT, pairs[i].mean[j], 8, 2);
	  tab_float (ssb->t,3, i*2+j+1, TAB_RIGHT, pairs[i].n, 2, 0);
	  tab_float (ssb->t,4, i*2+j+1, TAB_RIGHT, pairs[i].std_dev[j], 8, 3);
	  tab_float (ssb->t,5, i*2+j+1, TAB_RIGHT, pairs[i].std_dev[j]/sqrt(pairs[i].n), 8, 3);

	}
    }
}

/* Populate the one sample ssbox */
void 
ssbox_one_sample_populate(struct ssbox *ssb, struct cmd_t_test *cmd)
{
  int i;

  assert(ssb->t);

  for (i=0; i < cmd->n_variables; ++i)
    {
      struct group_statistics *gs = &group_proc_get (cmd->v_variables[i])->ugs;

      tab_text (ssb->t, 0, i+1, TAB_LEFT, cmd->v_variables[i]->name);
      tab_float (ssb->t,1, i+1, TAB_RIGHT, gs->n, 2, 0);
      tab_float (ssb->t,2, i+1, TAB_RIGHT, gs->mean, 8, 2);
      tab_float (ssb->t,3, i+1, TAB_RIGHT, gs->std_dev, 8, 2);
      tab_float (ssb->t,4, i+1, TAB_RIGHT, gs->se_mean, 8, 3);
    }
  
}



/* Implementation of the Test Results box struct */

void trbox_base_init(struct trbox *self,size_t n_vars, int cols);
void trbox_base_finalize(struct trbox *trb);

void trbox_independent_samples_init(struct trbox *trb,
				    struct cmd_t_test *cmd );

void trbox_independent_samples_populate(struct trbox *trb,
					struct cmd_t_test *cmd);

void trbox_one_sample_init(struct trbox *self,
		      struct cmd_t_test *cmd );

void trbox_one_sample_populate(struct trbox *trb,
			       struct cmd_t_test *cmd);

void trbox_paired_init(struct trbox *self,
		       struct cmd_t_test *cmd );

void trbox_paired_populate(struct trbox *trb,
		      struct cmd_t_test *cmd);



/* Create a trbox according to mode*/
void 
trbox_create(struct trbox *trb,   
	     struct cmd_t_test *cmd, int mode)
{
    switch (mode) 
      {
      case T_1_SAMPLE:
	trbox_one_sample_init(trb,cmd);
	break;
      case T_IND_SAMPLES:
	trbox_independent_samples_init(trb,cmd);
	break;
      case T_PAIRED:
	trbox_paired_init(trb,cmd);
	break;
      default:
	assert(0);
      }
}

/* Populate a trbox according to cmd */
void 
trbox_populate(struct trbox *trb, struct cmd_t_test *cmd)
{
  trb->populate(trb,cmd);
}

/* Submit and destroy a trbox */
void 
trbox_finalize(struct trbox *trb)
{
  trb->finalize(trb);
}

/* Initialize the independent samples trbox */
void 
trbox_independent_samples_init(struct trbox *self,
			   struct cmd_t_test *cmd UNUSED)
{
  const int hsize=11;
  const int vsize=cmd->n_variables*2+3;

  assert(self);
  self->populate = trbox_independent_samples_populate;

  trbox_base_init(self,cmd->n_variables*2,hsize);
  tab_title(self->t,0,_("Independent Samples Test"));
  tab_hline(self->t,TAL_1,2,hsize-1,1);
  tab_vline(self->t,TAL_2,2,0,vsize-1);
  tab_vline(self->t,TAL_1,4,0,vsize-1);
  tab_box(self->t,-1,-1,-1,TAL_1, 2,1,hsize-2,vsize-1);
  tab_hline(self->t,TAL_1, hsize-2,hsize-1,2);
  tab_box(self->t,-1,-1,-1,TAL_1, hsize-2,2,hsize-1,vsize-1);
  tab_joint_text(self->t, 2, 0, 3, 0, 
		 TAB_CENTER,_("Levene's Test for Equality of Variances"));
  tab_joint_text(self->t, 4,0,hsize-1,0,
		 TAB_CENTER,_("t-test for Equality of Means"));

  tab_text(self->t,2,2, TAB_CENTER | TAT_TITLE,_("F"));
  tab_text(self->t,3,2, TAB_CENTER | TAT_TITLE,_("Sig."));
  tab_text(self->t,4,2, TAB_CENTER | TAT_TITLE,_("t"));
  tab_text(self->t,5,2, TAB_CENTER | TAT_TITLE,_("df"));
  tab_text(self->t,6,2, TAB_CENTER | TAT_TITLE,_("Sig. (2-tailed)"));
  tab_text(self->t,7,2, TAB_CENTER | TAT_TITLE,_("Mean Difference"));
  tab_text(self->t,8,2, TAB_CENTER | TAT_TITLE,_("Std. Error Difference"));
  tab_text(self->t,9,2, TAB_CENTER | TAT_TITLE,_("Lower"));
  tab_text(self->t,10,2, TAB_CENTER | TAT_TITLE,_("Upper"));

  tab_joint_text(self->t, 9, 1, 10, 1, TAB_CENTER | TAT_PRINTF, 
		 _("%g%% Confidence Interval of the Difference"),
		 cmd->criteria*100.0);

}

/* Populate the independent samples trbox */
void 
trbox_independent_samples_populate(struct trbox *self,
				   struct cmd_t_test *cmd )
{
  int i;

  assert(self);
  for (i=0; i < cmd->n_variables; ++i)
    {
      double p,q;

      double t;
      double df;

      double df1, df2;

      double pooled_variance;
      double std_err_diff;
      double mean_diff;

      struct variable *var = cmd->v_variables[i];
      struct group_proc *grp_data = group_proc_get (var);

      struct hsh_table *grp_hash = grp_data->group_hash;

      struct group_statistics *gs0 ;
      struct group_statistics *gs1 ;
	  
      union value search_val;
	  
      if ( gp.criterion == CMP_LE ) 
	search_val.f = gp.v.critical_value - 1.0;
      else
	search_val = gp.v.g_value[0];

      gs0 = hsh_find(grp_hash, (void *) &search_val);
      assert(gs0);

      if ( gp.criterion == CMP_LE ) 
	search_val.f = gp.v.critical_value + 1.0;
      else
	search_val = gp.v.g_value[1];

      gs1 = hsh_find(grp_hash, (void *) &search_val);
      assert(gs1);

	  
      tab_text (self->t, 0, i*2+3, TAB_LEFT, cmd->v_variables[i]->name);

      tab_text (self->t, 1, i*2+3, TAB_LEFT, _("Equal variances assumed"));


      tab_float(self->t, 2, i*2+3, TAB_CENTER, grp_data->levene, 8,3);

      /* Now work out the significance of the Levene test */
      df1 = 1; df2 = grp_data->ugs.n - 2;
      q = gsl_cdf_fdist_Q(grp_data->levene, df1, df2);

      tab_float(self->t, 3, i*2+3, TAB_CENTER, q, 8,3 );

      df = gs0->n + gs1->n - 2.0 ;
      tab_float (self->t, 5, i*2+3, TAB_RIGHT, df, 2, 0);

      pooled_variance = ( (gs0->n )*pow2(gs0->s_std_dev)
			  + 
			  (gs1->n )*pow2(gs1->s_std_dev) 
			) / df  ;

      t = (gs0->mean - gs1->mean) / sqrt(pooled_variance) ;
      t /= sqrt((gs0->n + gs1->n)/(gs0->n*gs1->n)); 

      tab_float (self->t, 4, i*2+3, TAB_RIGHT, t, 8, 3);

      p = gsl_cdf_tdist_P(t, df);
      q = gsl_cdf_tdist_Q(t, df);

      tab_float(self->t, 6, i*2+3, TAB_RIGHT, 2.0*(t>0?q:p) , 8, 3);

      mean_diff = gs0->mean - gs1->mean;
      tab_float(self->t, 7, i*2+3, TAB_RIGHT, mean_diff, 8, 3);


      std_err_diff = sqrt( pow2(gs0->se_mean) + pow2(gs1->se_mean));
      tab_float(self->t, 8, i*2+3, TAB_RIGHT, std_err_diff, 8, 3);


      /* Now work out the confidence interval */
      q = (1 - cmd->criteria)/2.0;  /* 2-tailed test */

      t = gsl_cdf_tdist_Qinv(q,df);
      tab_float(self->t, 9, i*2+3, TAB_RIGHT, 
		mean_diff - t * std_err_diff, 8, 3); 

      tab_float(self->t, 10, i*2+3, TAB_RIGHT, 
		mean_diff + t * std_err_diff, 8, 3); 


      {
	double se2;
      /* Now for the \sigma_1 != \sigma_2 case */
      tab_text (self->t, 1, i*2+3+1, 
		TAB_LEFT, _("Equal variances not assumed"));


      se2 = (pow2(gs0->s_std_dev)/(gs0->n -1) ) +
	(pow2(gs1->s_std_dev)/(gs1->n -1) );

      t = mean_diff / sqrt(se2) ;
      tab_float (self->t, 4, i*2+3+1, TAB_RIGHT, t, 8, 3);
		
      df = pow2(se2) / ( 
		       (pow2(pow2(gs0->s_std_dev)/(gs0->n - 1 )) 
			/(gs0->n -1 )
			)
		       + 
		       (pow2(pow2(gs1->s_std_dev)/(gs1->n - 1 ))
			/(gs1->n -1 )
			)
		       ) ;
      tab_float (self->t, 5, i*2+3+1, TAB_RIGHT, df, 8, 3);

      p = gsl_cdf_tdist_P(t, df);
      q = gsl_cdf_tdist_Q(t, df);

      tab_float(self->t, 6, i*2+3+1, TAB_RIGHT, 2.0*(t>0?q:p) , 8, 3);

      /* Now work out the confidence interval */
      q = (1 - cmd->criteria)/2.0;  /* 2-tailed test */

      t = gsl_cdf_tdist_Qinv(q, df);

      tab_float(self->t, 7, i*2+3+1, TAB_RIGHT, mean_diff, 8, 3);


      tab_float(self->t, 8, i*2+3+1, TAB_RIGHT, std_err_diff, 8, 3);


      tab_float(self->t, 9, i*2+3+1, TAB_RIGHT, 
		mean_diff - t * std_err_diff, 8, 3); 

      tab_float(self->t, 10, i*2+3+1, TAB_RIGHT, 
		mean_diff + t * std_err_diff, 8, 3); 

      }
    }
}

/* Initialize the paired samples trbox */
void 
trbox_paired_init(struct trbox *self,
			   struct cmd_t_test *cmd UNUSED)
{

  const int hsize=10;
  const int vsize=n_pairs+3;

  self->populate = trbox_paired_populate;

  trbox_base_init(self,n_pairs,hsize);
  tab_title (self->t, 0, _("Paired Samples Test"));
  tab_hline(self->t,TAL_1,2,6,1);
  tab_vline(self->t,TAL_2,2,0,vsize - 1);
  tab_joint_text(self->t,2,0,6,0,TAB_CENTER,_("Paired Differences"));
  tab_box(self->t,-1,-1,-1,TAL_1, 2,1,6,vsize-1);
  tab_box(self->t,-1,-1,-1,TAL_1, 6,0,hsize-1,vsize-1);
  tab_hline(self->t,TAL_1,5,6, 2);
  tab_vline(self->t,TAL_0,6,0,1);

  tab_joint_text(self->t, 5, 1, 6, 1, TAB_CENTER | TAT_PRINTF, 
		 _("%g%% Confidence Interval of the Difference"),
		 cmd->criteria*100.0);

  tab_text (self->t, 2, 2, TAB_CENTER | TAT_TITLE, _("Mean"));
  tab_text (self->t, 3, 2, TAB_CENTER | TAT_TITLE, _("Std. Deviation"));
  tab_text (self->t, 4, 2, TAB_CENTER | TAT_TITLE, _("Std. Error Mean"));
  tab_text (self->t, 5, 2, TAB_CENTER | TAT_TITLE, _("Lower"));
  tab_text (self->t, 6, 2, TAB_CENTER | TAT_TITLE, _("Upper"));
  tab_text (self->t, 7, 2, TAB_CENTER | TAT_TITLE, _("t"));
  tab_text (self->t, 8, 2, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (self->t, 9, 2, TAB_CENTER | TAT_TITLE, _("Sig. (2-tailed)"));
}

/* Populate the paired samples trbox */
void 
trbox_paired_populate(struct trbox *trb,
			      struct cmd_t_test *cmd UNUSED)
{
  int i;

  for (i=0; i < n_pairs; ++i)
    {
      double p,q;
      double se_mean;

      double n = pairs[i].n;
      double t;
      double df = n - 1;
      
      tab_text (trb->t, 0, i+3, TAB_LEFT | TAT_PRINTF, _("Pair %d"),i); 

      tab_text (trb->t, 1, i+3, TAB_LEFT | TAT_PRINTF, "%s - %s",
		pairs[i].v[0]->name, pairs[i].v[1]->name);

      tab_float(trb->t, 2, i+3, TAB_RIGHT, pairs[i].mean_diff, 8, 4);

      tab_float(trb->t, 3, i+3, TAB_RIGHT, pairs[i].std_dev_diff, 8, 5);

      /* SE Mean */
      se_mean = pairs[i].std_dev_diff / sqrt(n) ;
      tab_float(trb->t, 4, i+3, TAB_RIGHT, se_mean, 8,5 );

      /* Now work out the confidence interval */
      q = (1 - cmd->criteria)/2.0;  /* 2-tailed test */

      t = gsl_cdf_tdist_Qinv(q, df);

      tab_float(trb->t, 5, i+3, TAB_RIGHT, 
		pairs[i].mean_diff - t * se_mean , 8, 4); 

      tab_float(trb->t, 6, i+3, TAB_RIGHT, 
		pairs[i].mean_diff + t * se_mean , 8, 4); 

      t = (pairs[i].mean[0] - pairs[i].mean[1])
	/ sqrt (
		( pow2 (pairs[i].s_std_dev[0]) + pow2 (pairs[i].s_std_dev[1]) -
		  2 * pairs[i].correlation * 
		  pairs[i].s_std_dev[0] * pairs[i].s_std_dev[1] )
		/ (n - 1)
		);

      tab_float(trb->t, 7, i+3, TAB_RIGHT, t , 8,3 );

      /* Degrees of freedom */
      tab_float(trb->t, 8, i+3, TAB_RIGHT, df , 2, 0 );

      p = gsl_cdf_tdist_P(t,df);
      q = gsl_cdf_tdist_P(t,df);

      tab_float(trb->t, 9, i+3, TAB_RIGHT, 2.0*(t>0?q:p) , 8, 3);

    }
}

/* Initialize the one sample trbox */
void 
trbox_one_sample_init(struct trbox *self, struct cmd_t_test *cmd )
{
  const int hsize=7;
  const int vsize=cmd->n_variables+3;

  self->populate = trbox_one_sample_populate;

  trbox_base_init(self, cmd->n_variables,hsize);
  tab_title (self->t, 0, _("One-Sample Test"));
  tab_hline(self->t, TAL_1, 1, hsize - 1, 1);
  tab_vline(self->t, TAL_2, 1, 0, vsize - 1);

  tab_joint_text(self->t, 1, 0, hsize-1,0, TAB_CENTER | TAT_PRINTF, 
		 _("Test Value = %f"), cmd->n_testval[0]);

  tab_box(self->t, -1, -1, -1, TAL_1, 1,1,hsize-1,vsize-1);


  tab_joint_text(self->t,5,1,6,1,TAB_CENTER  | TAT_PRINTF, 
		 _("%g%% Confidence Interval of the Difference"),
		 cmd->criteria*100.0);

  tab_vline(self->t,TAL_0,6,1,1);
  tab_hline(self->t,TAL_1,5,6,2);
  tab_text (self->t, 1, 2, TAB_CENTER | TAT_TITLE, _("t"));
  tab_text (self->t, 2, 2, TAB_CENTER | TAT_TITLE, _("df"));
  tab_text (self->t, 3, 2, TAB_CENTER | TAT_TITLE, _("Sig. (2-tailed)"));
  tab_text (self->t, 4, 2, TAB_CENTER | TAT_TITLE, _("Mean Difference"));
  tab_text (self->t, 5, 2, TAB_CENTER | TAT_TITLE, _("Lower"));
  tab_text (self->t, 6, 2, TAB_CENTER | TAT_TITLE, _("Upper"));

}


/* Populate the one sample trbox */
void 
trbox_one_sample_populate(struct trbox *trb, struct cmd_t_test *cmd)
{
  int i;

  assert(trb->t);

  for (i=0; i < cmd->n_variables; ++i)
    {
      double t;
      double p,q;
      double df;
      struct group_statistics *gs = &group_proc_get (cmd->v_variables[i])->ugs;


      tab_text (trb->t, 0, i+3, TAB_LEFT, cmd->v_variables[i]->name);

      t = (gs->mean - cmd->n_testval[0] ) * sqrt(gs->n) / gs->std_dev ;

      tab_float (trb->t, 1, i+3, TAB_RIGHT, t, 8,3);

      /* degrees of freedom */
      df = gs->n - 1;

      tab_float (trb->t, 2, i+3, TAB_RIGHT, df, 8,0);

      p = gsl_cdf_tdist_P(t, df);
      q = gsl_cdf_tdist_Q(t, df);

      /* Multiply by 2 to get 2-tailed significance, makeing sure we've got 
	 the correct tail*/
      tab_float (trb->t, 3, i+3, TAB_RIGHT, 2.0*(t>0?q:p), 8,3);

      tab_float (trb->t, 4, i+3, TAB_RIGHT, gs->mean_diff, 8,3);


      q = (1 - cmd->criteria)/2.0;  /* 2-tailed test */
      t = gsl_cdf_tdist_Qinv(q, df);

      tab_float (trb->t, 5, i+3, TAB_RIGHT,
		 gs->mean_diff - t * gs->se_mean, 8,4);

      tab_float (trb->t, 6, i+3, TAB_RIGHT,
		 gs->mean_diff + t * gs->se_mean, 8,4);
    }
}

/* Base initializer for the generalized trbox */
void 
trbox_base_init(struct trbox *self, size_t data_rows, int cols)
{
  const size_t rows = 3 + data_rows;

  self->finalize = trbox_base_finalize;
  self->t = tab_create (cols, rows, 0);
  tab_headers (self->t,0,0,3,0); 
  tab_box (self->t, TAL_2, TAL_2, TAL_0, TAL_0, 0, 0, cols -1, rows -1);
  tab_hline(self->t, TAL_2,0,cols-1,3);
  tab_dim (self->t, tab_natural_dimensions);
}


/* Base finalizer for the trbox */
void 
trbox_base_finalize(struct trbox *trb)
{
  tab_submit(trb->t);
}


/* Create , populate and submit the Paired Samples Correlation box */
void
pscbox(void)
{
  const int rows=1+n_pairs;
  const int cols=5;
  int i;
  
  struct tab_table *table;
  
  table = tab_create (cols,rows,0);

  tab_columns (table, SOM_COL_DOWN, 1);
  tab_headers (table,0,0,1,0); 
  tab_box (table, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, cols -1, rows -1 );
  tab_hline(table, TAL_2, 0, cols - 1, 1);
  tab_vline(table, TAL_2, 2, 0, rows - 1);
  tab_dim(table, tab_natural_dimensions);
  tab_title(table, 0, _("Paired Samples Correlations"));

  /* column headings */
  tab_text(table, 2,0, TAB_CENTER | TAT_TITLE, _("N"));
  tab_text(table, 3,0, TAB_CENTER | TAT_TITLE, _("Correlation"));
  tab_text(table, 4,0, TAB_CENTER | TAT_TITLE, _("Sig."));

  for (i=0; i < n_pairs; ++i)
    {
      double p,q;

      double df = pairs[i].n -2;

      double correlation_t = 
	pairs[i].correlation * sqrt(df) /
	sqrt(1 - pow2(pairs[i].correlation));


      /* row headings */
      tab_text(table, 0,i+1, TAB_LEFT | TAT_TITLE | TAT_PRINTF, 
	       _("Pair %d"), i);
      
      tab_text(table, 1,i+1, TAB_LEFT | TAT_TITLE | TAT_PRINTF, 
	       _("%s & %s"), pairs[i].v[0]->name, pairs[i].v[1]->name);


      /* row data */
      tab_float(table, 2, i+1, TAB_RIGHT, pairs[i].n, 4, 0);
      tab_float(table, 3, i+1, TAB_RIGHT, pairs[i].correlation, 8, 3);

      p = gsl_cdf_tdist_P(correlation_t, df);
      q = gsl_cdf_tdist_Q(correlation_t, df);

      tab_float(table, 4, i+1, TAB_RIGHT, 2.0*(correlation_t>0?q:p), 8, 3);
    }

  tab_submit(table);
}




/* Calculation Implementation */

/* Per case calculations common to all variants of the T test */
static int 
common_calc (const struct ccase *c, void *_cmd)
{
  int i;
  struct cmd_t_test *cmd = (struct cmd_t_test *)_cmd;  

  double weight = dict_get_case_weight(default_dict,c,&bad_weight_warn);


  /* Skip the entire case if /MISSING=LISTWISE is set */
  if ( cmd->miss == TTS_LISTWISE ) 
    {
      for(i=0; i< cmd->n_variables ; ++i) 
	{
	  struct variable *v = cmd->v_variables[i];
	  const union value *val = case_data (c, v->fv);

	  if (value_is_missing(&v->miss, val) )
	    {
	      return 0;
	    }
	}
    }

  /* Listwise has to be implicit if the independent variable is missing ?? */
  if ( cmd->sbc_groups )
    {
      const union value *gv = case_data (c, indep_var->fv);
      if ( value_is_missing(&indep_var->miss, gv) )
	{
	  return 0;
	}
    }


  for(i=0; i< cmd->n_variables ; ++i) 
    {
      struct group_statistics *gs;
      struct variable *v = cmd->v_variables[i];
      const union value *val = case_data (c, v->fv);

      gs= &group_proc_get (cmd->v_variables[i])->ugs;

      if (! value_is_missing(&v->miss, val) )
	{
	  gs->n+=weight;
	  gs->sum+=weight * val->f;
	  gs->ssq+=weight * val->f * val->f;
	}
    }
  return 0;
}

/* Pre calculations common to all variants of the T test */
static void 
common_precalc ( struct cmd_t_test *cmd )
{
  int i=0;

  for(i=0; i< cmd->n_variables ; ++i) 
    {
      struct group_statistics *gs;
      gs= &group_proc_get (cmd->v_variables[i])->ugs;
      
      gs->sum=0;
      gs->n=0;
      gs->ssq=0;
      gs->sum_diff=0;
    }
}

/* Post calculations common to all variants of the T test */
void 
common_postcalc (  struct cmd_t_test *cmd )
{
  int i=0;


  for(i=0; i< cmd->n_variables ; ++i) 
    {
      struct group_statistics *gs;
      gs= &group_proc_get (cmd->v_variables[i])->ugs;
      
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
}

/* Per case calculations for one sample t test  */
static int 
one_sample_calc (const struct ccase *c, void *cmd_)
{
  int i;
  struct cmd_t_test *cmd = (struct cmd_t_test *)cmd_;


  double weight = dict_get_case_weight(default_dict,c,&bad_weight_warn);

  /* Skip the entire case if /MISSING=LISTWISE is set */
  if ( cmd->miss == TTS_LISTWISE ) 
    {
      for(i=0; i< cmd->n_variables ; ++i) 
	{
	  struct variable *v = cmd->v_variables[i];
	  const union value *val = case_data (c, v->fv);

	  if (value_is_missing(&v->miss, val) )
	    {
	      return 0;
	    }
	}
    }

  for(i=0; i< cmd->n_variables ; ++i) 
    {
      struct group_statistics *gs;
      struct variable *v = cmd->v_variables[i];
      const union value *val = case_data (c, v->fv);

      gs= &group_proc_get (cmd->v_variables[i])->ugs;
      
      if ( ! value_is_missing(&v->miss, val))
	gs->sum_diff += weight * (val->f - cmd->n_testval[0]);
    }

  return 0;
}

/* Pre calculations for one sample t test */
static void 
one_sample_precalc ( struct cmd_t_test *cmd )
{
  int i=0; 
 
  for(i=0; i< cmd->n_variables ; ++i) 
    {
      struct group_statistics *gs;
      gs= &group_proc_get (cmd->v_variables[i])->ugs;
      
      gs->sum_diff=0;
    }
}

/* Post calculations for one sample t test */
static void 
one_sample_postcalc (struct cmd_t_test *cmd)
{
  int i=0;
  
  for(i=0; i< cmd->n_variables ; ++i) 
    {
      struct group_statistics *gs;
      gs= &group_proc_get (cmd->v_variables[i])->ugs;

      gs->mean_diff = gs->sum_diff / gs->n ;
    }
}



static void 
paired_precalc (struct cmd_t_test *cmd UNUSED)
{
  int i;

  for(i=0; i < n_pairs ; ++i )
    {
      pairs[i].n = 0;
      pairs[i].sum[0] = 0;      pairs[i].sum[1] = 0;
      pairs[i].ssq[0] = 0;      pairs[i].ssq[1] = 0;
      pairs[i].sum_of_prod = 0;
      pairs[i].correlation = 0;
      pairs[i].sum_of_diffs = 0;
      pairs[i].ssq_diffs = 0;
    }

}


static int  
paired_calc (const struct ccase *c, void *cmd_)
{
  int i;

  struct cmd_t_test *cmd  = (struct cmd_t_test *) cmd_;

  double weight = dict_get_case_weight(default_dict,c,&bad_weight_warn);

  /* Skip the entire case if /MISSING=LISTWISE is set , 
   AND one member of a pair is missing */
  if ( cmd->miss == TTS_LISTWISE ) 
    {
      for(i=0; i < n_pairs ; ++i )
      	{
	  struct variable *v0 = pairs[i].v[0];
	  struct variable *v1 = pairs[i].v[1];

	  const union value *val0 = case_data (c, v0->fv);
	  const union value *val1 = case_data (c, v1->fv);
	  
	  if ( value_is_missing(&v0->miss, val0) ||
	       value_is_missing(&v1->miss, val1) )
	    {
	      return 0;
	    }
	}
    }

  for(i=0; i < n_pairs ; ++i )
    {
      struct variable *v0 = pairs[i].v[0];
      struct variable *v1 = pairs[i].v[1];

      const union value *val0 = case_data (c, v0->fv);
      const union value *val1 = case_data (c, v1->fv);

      if ( ( !value_is_missing(&v0->miss, val0)
             && !value_is_missing(&v1->miss, val1) ) )
      {
	pairs[i].n += weight;
	pairs[i].sum[0] += weight * val0->f;
	pairs[i].sum[1] += weight * val1->f;

	pairs[i].ssq[0] += weight * pow2(val0->f);
	pairs[i].ssq[1] += weight * pow2(val1->f);

	pairs[i].sum_of_prod += weight * val0->f * val1->f ;

	pairs[i].sum_of_diffs += weight * ( val0->f - val1->f ) ;
	pairs[i].ssq_diffs += weight * pow2(val0->f - val1->f);
      }
    }

  return 0;
}

static void 
paired_postcalc (struct cmd_t_test *cmd UNUSED)
{
  int i;

  for(i=0; i < n_pairs ; ++i )
    {
      int j;
      const double n = pairs[i].n;

      for (j=0; j < 2 ; ++j) 
	{
	  pairs[i].mean[j] = pairs[i].sum[j] / n ;
	  pairs[i].s_std_dev[j] = sqrt((pairs[i].ssq[j] / n - 
					      pow2(pairs[i].mean[j]))
				     );

	  pairs[i].std_dev[j] = sqrt(n/(n-1)*(pairs[i].ssq[j] / n - 
					      pow2(pairs[i].mean[j]))
				     );
	}
      
      pairs[i].correlation = pairs[i].sum_of_prod / pairs[i].n - 
	pairs[i].mean[0] * pairs[i].mean[1] ;
      /* correlation now actually contains the covariance */
      
      pairs[i].correlation /= pairs[i].std_dev[0] * pairs[i].std_dev[1];
      pairs[i].correlation *= pairs[i].n / ( pairs[i].n - 1 );
      
      pairs[i].mean_diff = pairs[i].sum_of_diffs / n ;

      pairs[i].std_dev_diff = sqrt (  n / (n - 1) * (
				    ( pairs[i].ssq_diffs / n )
				    - 
				    pow2(pairs[i].mean_diff )
				    ) );
    }
}

static void 
group_precalc (struct cmd_t_test *cmd )
{
  int i;
  int j;

  for(i=0; i< cmd->n_variables ; ++i) 
    {
      struct group_proc *ttpr = group_proc_get (cmd->v_variables[i]);

      /* There's always 2 groups for a T - TEST */
      ttpr->n_groups = 2;

      gp.indep_width = indep_var->width;
      
      ttpr->group_hash = hsh_create(2, 
				    (hsh_compare_func *) compare_group_binary,
				    (hsh_hash_func *) hash_group_binary,
				    (hsh_free_func *) free_group,
				    (void *) &gp );

      for (j=0 ; j < 2 ; ++j)
	{

	  struct group_statistics *gs = xmalloc (sizeof *gs);

	  gs->sum = 0;
	  gs->n = 0;
	  gs->ssq = 0;
	
	  if ( gp.criterion == CMP_EQ ) 
	    {
	      gs->id = gp.v.g_value[j];
	    }
	  else
	    {
	      if ( j == 0 ) 
		gs->id.f = gp.v.critical_value - 1.0 ;
	      else
		gs->id.f = gp.v.critical_value + 1.0 ;
	    }
	  
	  hsh_insert ( ttpr->group_hash, (void *) gs );

	}
    }

}

static int  
group_calc (const struct ccase *c, struct cmd_t_test *cmd)
{
  int i;

  const union value *gv = case_data (c, indep_var->fv);

  const double weight = dict_get_case_weight(default_dict,c,&bad_weight_warn);

  if ( value_is_missing(&indep_var->miss, gv) )
    {
      return 0;
    }

  if ( cmd->miss == TTS_LISTWISE ) 
    {
      for(i=0; i< cmd->n_variables ; ++i) 
	{
	  struct variable *v = cmd->v_variables[i];
	  const union value *val = case_data (c, v->fv);

	  if (value_is_missing(&v->miss, val) )
	    {
	      return 0;
	    }
	}
    }

  gv = case_data (c, indep_var->fv);

  for(i=0; i< cmd->n_variables ; ++i) 
    {
      struct variable *var = cmd->v_variables[i];
      const union value *val = case_data (c, var->fv);
      struct hsh_table *grp_hash = group_proc_get (var)->group_hash;
      struct group_statistics *gs;

      gs = hsh_find(grp_hash, (void *) gv);

      /* If the independent variable doesn't match either of the values 
         for this case then move on to the next case */
      if ( ! gs ) 
      	return 0;

      if ( !value_is_missing(&var->miss, val) )
	{
	  gs->n+=weight;
	  gs->sum+=weight * val->f;
	  gs->ssq+=weight * pow2(val->f);
	}
    }

  return 0;
}


static void 
group_postcalc ( struct cmd_t_test *cmd )
{
  int i;

  for(i=0; i< cmd->n_variables ; ++i) 
    {
      struct variable *var = cmd->v_variables[i];
      struct hsh_table *grp_hash = group_proc_get (var)->group_hash;
      struct hsh_iterator g;
      struct group_statistics *gs;
      int count=0;

      for (gs =  hsh_first (grp_hash,&g); 
	   gs != 0; 
	   gs = hsh_next(grp_hash,&g))
	{
	  gs->mean = gs->sum / gs->n;
	  
	  gs->s_std_dev= sqrt(
			      ( (gs->ssq / gs->n ) - gs->mean * gs->mean )
			      ) ;

	  gs->std_dev= sqrt(
			    gs->n/(gs->n-1) *
			    ( (gs->ssq / gs->n ) - gs->mean * gs->mean )
			    ) ;
	  
	  gs->se_mean = gs->std_dev / sqrt(gs->n);
	  count ++;
	}
      assert(count == 2);
    }
}



static bool
calculate(const struct casefile *cf, void *cmd_)
{
  struct ssbox stat_summary_box;
  struct trbox test_results_box;

  struct casereader *r;
  struct ccase c;

  struct cmd_t_test *cmd = (struct cmd_t_test *) cmd_;

  common_precalc(cmd);
  for(r = casefile_get_reader (cf);
      casereader_read (r, &c) ;
      case_destroy (&c)) 
    {
      common_calc(&c,cmd);
    }
  casereader_destroy (r);
  common_postcalc(cmd);

  switch(mode)
    {
    case T_1_SAMPLE:
      one_sample_precalc(cmd);
      for(r = casefile_get_reader (cf);
	  casereader_read (r, &c) ;
          case_destroy (&c)) 
	{
	  one_sample_calc(&c,cmd);
	}
      casereader_destroy (r);
      one_sample_postcalc(cmd);

      break;
    case T_PAIRED:
      paired_precalc(cmd);
      for(r = casefile_get_reader (cf);
	  casereader_read (r, &c) ;
          case_destroy (&c)) 
	{
	  paired_calc(&c,cmd);
	}
      casereader_destroy (r);
      paired_postcalc(cmd);

      break;
    case T_IND_SAMPLES:

      group_precalc(cmd);
      for(r = casefile_get_reader (cf);
	  casereader_read (r, &c) ;
          case_destroy (&c)) 
	{
	  group_calc(&c,cmd);
	}
      casereader_destroy (r);
      group_postcalc(cmd);

      levene(cf, indep_var, cmd->n_variables, cmd->v_variables,
	     (cmd->miss == TTS_LISTWISE)?LEV_LISTWISE:LEV_ANALYSIS ,
	     value_is_missing);
      break;
    }

  ssbox_create(&stat_summary_box,cmd,mode);
  ssbox_populate(&stat_summary_box,cmd);
  ssbox_finalize(&stat_summary_box);

  if ( mode == T_PAIRED) 
      pscbox();

  trbox_create(&test_results_box,cmd,mode);
  trbox_populate(&test_results_box,cmd);
  trbox_finalize(&test_results_box);

  return true;
}

short which_group(const struct group_statistics *g,
		  const struct group_properties *p);

/* Return -1 if the id of a is less than b; +1 if greater than and 
   0 if equal */
static int 
compare_group_binary(const struct group_statistics *a, 
		     const struct group_statistics *b, 
		     const struct group_properties *p)
{
  short flag_a;
  short flag_b;
  
  if ( p->criterion == CMP_LE ) 
    {
      /* less-than-or-equal comparision is not meaningfull for
	 alpha variables, so we shouldn't ever arrive here */
      assert(p->indep_width == 0 ) ;
      
      flag_a = ( a->id.f < p->v.critical_value ) ;
      flag_b = ( b->id.f < p->v.critical_value ) ;
    }
  else
    {
      flag_a = which_group(a, p);
      flag_b = which_group(b, p);
    }

  if (flag_a < flag_b ) 
    return -1;

  return (flag_a > flag_b);
}

/* This is a degenerate case of a hash, since it can only return three possible
   values.  It's really a comparison, being used as a hash function */

static unsigned 
hash_group_binary(const struct group_statistics *g, 
		  const struct group_properties *p)
{
  short flag = -1;

  if ( p->criterion == CMP_LE ) 
    {
      /* Not meaningfull to do a less than compare for alpha values ? */
      assert(p->indep_width == 0 ) ;
      flag = ( g->id.f < p->v.critical_value ) ; 
    }
  else if ( p->criterion == CMP_EQ) 
    {
      flag = which_group(g,p);
    }
  else
    assert(0);

  return flag;
}

/* return 0 if G belongs to group 0, 
          1 if it belongs to group 1,
	  2 if it belongs to neither group */
short
which_group(const struct group_statistics *g,
	    const struct group_properties *p)
{
 
  if ( 0 == compare_values (&g->id, &p->v.g_value[0], p->indep_width))
    return 0;

  if ( 0 == compare_values (&g->id, &p->v.g_value[1], p->indep_width))
    return 1;

  return 2;
}
	    
