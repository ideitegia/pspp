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
#include "tab.h"
#include "som.h"
#include "value-labels.h"
#include "var.h"
#include "vfm.h"
#include "pool.h"

/* (specification)
   "T-TEST" (tts_):
     groups=custom;
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

static struct cmd_t_test cmd;



static struct pool *t_test_pool ;

/* Variable for the GROUPS subcommand, if given. */
static struct variable *groups;

/* GROUPS: Number of values specified by the user; the values
   specified if any. */
static int n_groups_values;
static union value groups_values[2];

/* PAIRS: Number of pairs to be compared ; each pair. */
static int n_pairs ;
typedef struct variable *pair_t[2] ;
static pair_t *pairs;


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


static int common_calc (struct ccase *);
static void common_precalc (void);
static void common_postcalc (void);

static int one_sample_calc (struct ccase *);
static void one_sample_precalc (void);
static void one_sample_postcalc (void);


int
cmd_t_test(void)
{
  int mode;

  struct ssbox stat_summary_box;
  struct trbox test_results_box;

  if (!lex_force_match_id ("T"))
    return CMD_FAILURE;

  lex_match ('-');
  lex_match_id ("TEST");

  if ( !parse_t_test(&cmd) )
    return CMD_FAILURE;

  if (! cmd.sbc_criteria)
    cmd.criteria=0.95;

  if ( cmd.sbc_testval + cmd.sbc_groups + cmd.sbc_pairs != 1 ) 
    {
      msg(SE, 
	  _("Exactly one of TESTVAL, GROUPS or PAIRS subcommands is required")
	  );
      return CMD_FAILURE;
    }

  if (cmd.sbc_testval) 
    mode=T_1_SAMPLE;
  else if (cmd.sbc_groups)
    mode=T_IND_SAMPLES;
  else
    mode=T_PAIRED;

  if ( mode == T_PAIRED && cmd.sbc_variables) 
    {
      msg(SE, _("VARIABLES subcommand is not appropriate with PAIRS"));
      return CMD_FAILURE;
    }

  procedure(common_precalc,common_calc,common_postcalc);

  if (mode == T_1_SAMPLE)
    procedure(one_sample_precalc,one_sample_calc,one_sample_postcalc);

  t_test_pool = pool_create ();

  ssbox_create(&stat_summary_box,&cmd,mode);
  trbox_create(&test_results_box,&cmd,mode);

  ssbox_populate(&stat_summary_box,&cmd);
  trbox_populate(&test_results_box,&cmd);

  ssbox_finalize(&stat_summary_box);
  trbox_finalize(&test_results_box);

  pool_destroy (t_test_pool);

  t_test_pool=0;
    
  return CMD_SUCCESS;
}

static int
tts_custom_groups (struct cmd_t_test *cmd unused)
{
  lex_match('=');

  if (token != T_ALL && 
      (token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
     ) 
  {
    msg(SE,_("`%s' is not a variable name"),tokid);
    return 0;
  }

  groups = parse_variable ();
  if (!groups)
    {
      lex_error ("expecting variable name in GROUPS subcommand");
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

  if (!parse_value (&groups_values[0],groups->type))
    return 0;
  n_groups_values = 1;

  lex_match (',');
  if (lex_match (')'))
    return 1;

  if (!parse_value (&groups_values[1],groups->type))
    return 0;
  n_groups_values = 2;

  if (!lex_force_match (')'))
    return 0;

  return 1;
}




static int
tts_custom_pairs (struct cmd_t_test *cmd unused)
{
  struct variable **vars;
  int n_vars;

  int n_before_WITH ;
  int n_after_WITH = -1;
  int paired ; /* Was the PAIRED keyword given ? */

  lex_match('=');

  if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
      && token != T_ALL)
    {
      msg(SE,_("`%s' is not a variable name"),tokid);
      return 0;
    }

  n_vars=0;
  if (!parse_variables (default_dict, &vars, &n_vars,
			PV_DUPLICATE | PV_NUMERIC | PV_NO_SCRATCH))
    {
      free (vars);
      return 0;
    }
  assert (n_vars);

  n_before_WITH=0;
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
      n_pairs=n_before_WITH;
    }
  else if (n_before_WITH > 0) /* WITH keyword given, but not PAIRED keyword */
    {
      n_pairs=n_before_WITH * n_after_WITH ;
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
      n_pairs = n_vars * (n_vars -1 ) /2 ;
    }

  /* Allocate storage for the pairs */
  pairs = xrealloc(pairs,sizeof(pair_t) *n_pairs);

  /* Populate the pairs with the appropriate variables */
  if ( paired ) 
    {
      int i;

      assert(n_pairs == n_vars/2);
      for (i = 0; i < n_pairs ; ++i)
	{
	  pairs[i][0] = vars[i];
	  pairs[i][1] = vars[i+n_pairs];
	}
    }
  else if (n_before_WITH > 0) /* WITH keyword given, but not PAIRED keyword */
    {
      int i,j;
      int p=0;

      for(i=0 ; i < n_before_WITH ; ++i ) 
	{
	  for(j=0 ; j < n_after_WITH ; ++j)
	    {
	      pairs[p][0] = vars[i];
	      pairs[p][1] = vars[j+n_before_WITH];
	      ++p;
	    }
	}
    }
  else /* Neither WITH nor PAIRED given */
    {
      int i,j;
      int p=0;
      
      for(i=0 ; i < n_vars ; ++i ) 
	{
	  for(j=i+1 ; j < n_vars ; ++j)
	    {
	      pairs[p][0] = vars[i];
	      pairs[p][1] = vars[j];
	      ++p;
	    }
	}
    }

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
      strncpy (v->s, ds_value (&tokstr), ds_length (&tokstr));
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
  tab_vline(this->t, TAL_2, 1,0,vsize);
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
  tab_vline(this->t,0,1,0,vsize);
  tab_text (this->t, 1, 0, TAB_CENTER | TAT_TITLE, groups->name);
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

  char *val_lab1=0;
  char *val_lab2=0;

  if ( groups->type == NUMERIC ) 
    {
      val_lab1 = val_labs_find( groups->val_labs,groups_values[0]); 
      val_lab2 = val_labs_find( groups->val_labs,groups_values[1]);
    }
  else
    {
      val_lab1 = groups_values[0].s;
      val_lab2 = groups_values[1].s;
    }

  assert(ssb->t);

  for (i=0; i < cmd->n_variables; ++i)
    {
      tab_text (ssb->t, 0, i*2+1, TAB_LEFT, cmd->v_variables[i]->name);

      if (val_lab1)
	tab_text (ssb->t, 1, i*2+1, TAB_LEFT, val_lab1);
      else
	tab_float(ssb->t, 1 ,i*2+1, TAB_LEFT, groups_values[0].f, 2,0);

      if (val_lab2)
	tab_text (ssb->t, 1, i*2+1+1, TAB_LEFT, val_lab2);
      else
	tab_float(ssb->t, 1 ,i*2+1+1, TAB_LEFT, groups_values[1].f,2,0);
    }
}


void ssbox_paired_populate(struct ssbox *ssb,
			   struct cmd_t_test *cmd);

/* Initialize the paired values ssbox */
void 
ssbox_paired_init(struct ssbox *this, struct cmd_t_test *cmd unused)
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
ssbox_paired_populate(struct ssbox *ssb,struct cmd_t_test *cmd unused)
{
  int i;
  struct string ds;

  assert(ssb->t);
  ds_init(t_test_pool,&ds,15);

  for (i=0; i < n_pairs; ++i)
    {
      ds_clear(&ds);

      ds_printf(&ds,_("Pair %d"),i);

      tab_text (ssb->t, 0, i*2+1, TAB_LEFT, ds.string);
      tab_text (ssb->t, 1, i*2+1, TAB_LEFT, pairs[i][0]->name);
      tab_text (ssb->t, 1, i*2+2, TAB_LEFT, pairs[i][1]->name);
    }

  ds_destroy(&ds);
}

/* Populate the one sample ssbox */
void 
ssbox_one_sample_populate(struct ssbox *ssb, struct cmd_t_test *cmd)
{
  int i;

  assert(ssb->t);

  for (i=0; i < cmd->n_variables; ++i)
    {
      struct t_test_proc *ttp;
      ttp= &cmd->v_variables[i]->p.t_t;

      tab_text (ssb->t, 0, i+1, TAB_LEFT, cmd->v_variables[i]->name);
      tab_float (ssb->t,1, i+1, TAB_RIGHT, ttp->n, 2, 0);
      tab_float (ssb->t,2, i+1, TAB_RIGHT, ttp->mean, 8, 2);
      tab_float (ssb->t,3, i+1, TAB_RIGHT, ttp->std_dev, 8, 2);
      tab_float (ssb->t,4, i+1, TAB_RIGHT, ttp->se_mean, 8, 3);
    }
  
}



/* Implementation of the Test Results box struct */

void trbox_base_init(struct trbox *self,int n_vars, int cols);
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
			   struct cmd_t_test *cmd unused)
{
  const int hsize=11;
  const int vsize=cmd->n_variables*2+3;

  struct string ds;

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
		 TAB_CENTER,_("Levine's Test for Equality of Variances"));
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

  ds_init(t_test_pool,&ds,80);
		
  ds_printf(&ds,_("%d%% Confidence Interval of the Difference"),
	    (int)round(cmd->criteria*100.0));

  tab_joint_text(self->t,9,1,10,1,TAB_CENTER, ds.string);

  ds_destroy(&ds);
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
      tab_text (self->t, 0, i*2+3, TAB_LEFT, cmd->v_variables[i]->name);

      tab_text (self->t, 1, i*2+3, TAB_LEFT, _("Equal variances assumed"));

      tab_text (self->t, 1, i*2+3+1, 
		TAB_LEFT, _("Equal variances not assumed"));
    }
}

/* Initialize the paired samples trbox */
void 
trbox_paired_init(struct trbox *self,
			   struct cmd_t_test *cmd unused)
{

  const int hsize=10;
  const int vsize=n_pairs*2+3;

  struct string ds;

  self->populate = trbox_paired_populate;

  trbox_base_init(self,n_pairs*2,hsize);
  tab_title (self->t, 0, _("Paired Samples Test"));
  tab_hline(self->t,TAL_1,2,6,1);
  tab_vline(self->t,TAL_2,2,0,vsize);
  tab_joint_text(self->t,2,0,6,0,TAB_CENTER,_("Paired Differences"));
  tab_box(self->t,-1,-1,-1,TAL_1, 2,1,6,vsize-1);
  tab_box(self->t,-1,-1,-1,TAL_1, 6,0,hsize-1,vsize-1);
  tab_hline(self->t,TAL_1,5,6, 2);
  tab_vline(self->t,TAL_0,6,0,1);

  ds_init(t_test_pool,&ds,80);
		
  ds_printf(&ds,_("%d%% Confidence Interval of the Difference"),
	    (int)round(cmd->criteria*100.0));

  tab_joint_text(self->t,5,1,6,1,TAB_CENTER, ds.string);

  ds_destroy(&ds);

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
			      struct cmd_t_test *cmd unused)
{
  int i;
  struct string ds;

  ds_init(t_test_pool,&ds,15);  

  for (i=0; i < n_pairs; ++i)
    {
      ds_clear(&ds);
      ds_printf(&ds,_("Pair %d"),i);

      tab_text (trb->t, 0, i*2+3, TAB_LEFT, ds.string);
      tab_text (trb->t, 1, i*2+3, TAB_LEFT, pairs[i][0]->name);
      tab_text (trb->t, 1, i*2+4, TAB_LEFT, pairs[i][1]->name);
    }
  ds_destroy(&ds);
}

/* Initialize the one sample trbox */
void 
trbox_one_sample_init(struct trbox *self, struct cmd_t_test *cmd )
{
  const int hsize=7;
  const int vsize=cmd->n_variables+3;

  struct string ds;
  
  self->populate = trbox_one_sample_populate;

  trbox_base_init(self, cmd->n_variables,hsize);
  tab_title (self->t, 0, _("One-Sample Test"));
  tab_hline(self->t, TAL_1, 1, hsize - 1, 1);
  tab_vline(self->t, TAL_2, 1, 0, vsize);
  ds_init(t_test_pool, &ds, 80);
  ds_printf(&ds,_("Test Value = %f"),cmd->n_testval);
  tab_joint_text(self->t, 1, 0, hsize-1,0, TAB_CENTER,ds.string);
  tab_box(self->t, -1, -1, -1, TAL_1, 1,1,hsize-1,vsize-1);

  ds_clear(&ds);
  ds_printf(&ds,_("%d%% Confidence Interval of the Difference"),
	    (int)round(cmd->criteria*100.0));
  tab_joint_text(self->t,5,1,6,1,TAB_CENTER, ds.string);
  ds_destroy(&ds);
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
      int which =1;
      double t;
      double p,q;
      double df;
      int status;
      double bound;
      struct t_test_proc *ttp;
      ttp= &cmd->v_variables[i]->p.t_t;


      tab_text (trb->t, 0, i+3, TAB_LEFT, cmd->v_variables[i]->name);

      t = (ttp->mean - cmd->n_testval ) * sqrt(ttp->n) / ttp->std_dev ;

      tab_float (trb->t, 1, i+3, TAB_RIGHT, t, 8,3);

      /* degrees of freedom */
      df = ttp->n - 1;

      tab_float (trb->t, 2, i+3, TAB_RIGHT, df, 8,0);

      cdft(&which, &p, &q, &t, &df, &status, &bound);

      if ( 0 != status )
	{
	  msg( SE, _("Error calculating T statistic (cdft returned %d)."),status);
	}


      /* Multiply by 2 to get 2-tailed significance */
      tab_float (trb->t, 3, i+3, TAB_RIGHT, q*2.0, 8,3);

      tab_float (trb->t, 4, i+3, TAB_RIGHT, ttp->mean_diff, 8,3);


      q = (1 - cmd->criteria)/2.0;  /* 2-tailed test */
      p = 1 - q ;
      which=2; /* Calc T from p,q and df */
      cdft(&which, &p, &q, &t, &df, &status, &bound);
      if ( 0 != status )
	{
	  msg( SE, _("Error calculating T statistic (cdft returned %d)."),status);
	}

      tab_float (trb->t, 5, i+3, TAB_RIGHT,
		 ttp->mean_diff - t * ttp->se_mean, 8,4);

      tab_float (trb->t, 6, i+3, TAB_RIGHT,
		 ttp->mean_diff + t * ttp->se_mean, 8,4);
    }
}

/* Base initializer for the generalized trbox */
void 
trbox_base_init(struct trbox *self, int data_rows, int cols)
{
  const int rows = 3 + data_rows;

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


/* Calculation Implementation */

/* Per case calculations common to all variants of the T test */
static int 
common_calc (struct ccase *c)
{
  int i;

  double weight = dict_get_case_weight(default_dict,c);

  for(i=0; i< cmd.n_variables ; ++i) 
    {
      struct t_test_proc *ttp;
      struct variable *v = cmd.v_variables[i];
      union value *val = &c->data[v->fv];

      ttp= &cmd.v_variables[i]->p.t_t;

      if (val->f != SYSMIS) 
	{
	  ttp->n+=weight;
	  ttp->sum+=weight * val->f;
	  ttp->ssq+=weight * val->f * val->f;
	}
    }
  return 0;
}

/* Pre calculations common to all variants of the T test */
static void 
common_precalc (void)
{
  int i=0;
  
  for(i=0; i< cmd.n_variables ; ++i) 
    {
      struct t_test_proc *ttp;
      ttp= &cmd.v_variables[i]->p.t_t;
      
      ttp->sum=0;
      ttp->n=0;
      ttp->ssq=0;
      ttp->sum_diff=0;
    }
}

/* Post calculations common to all variants of the T test */
void 
common_postcalc (void)
{
  int i=0;

  for(i=0; i< cmd.n_variables ; ++i) 
    {
      struct t_test_proc *ttp;
      ttp= &cmd.v_variables[i]->p.t_t;
      
      ttp->mean=ttp->sum / ttp->n;
      ttp->std_dev= sqrt(
			 ttp->n/(ttp->n-1) *
			 ( (ttp->ssq / ttp->n ) - ttp->mean * ttp->mean )
			 ) ;

      ttp->se_mean = ttp->std_dev / sqrt(ttp->n);

      ttp->mean_diff= ttp->sum_diff / ttp->n;
    }
}

/* Per case calculations for one sample t test  */
static int 
one_sample_calc (struct ccase *c)
{
  int i;

  double weight = dict_get_case_weight(default_dict,c);

  for(i=0; i< cmd.n_variables ; ++i) 
    {
      struct t_test_proc *ttp;
      struct variable *v = cmd.v_variables[i];
      union value *val = &c->data[v->fv];

      ttp= &cmd.v_variables[i]->p.t_t;
      
      if (val->f != SYSMIS) 
	ttp->sum_diff += weight * (val->f - cmd.n_testval);
    }

  return 0;
}

/* Pre calculations for one sample t test */
static void 
one_sample_precalc (void)
{
  int i=0;
  
  for(i=0; i< cmd.n_variables ; ++i) 
    {
      struct t_test_proc *ttp;
      ttp= &cmd.v_variables[i]->p.t_t;
      
      ttp->sum_diff=0;
    }
}

/* Post calculations for one sample t test */
static void 
one_sample_postcalc (void)
{
  int i=0;
  
  for(i=0; i< cmd.n_variables ; ++i) 
    {
      struct t_test_proc *ttp;
      ttp= &cmd.v_variables[i]->p.t_t;

      
      ttp->mean_diff = ttp->sum_diff / ttp->n ;
    }
}
