/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "alloc.h"
#include "avl.h"
#include "command.h"
#include "lexer.h"
#include "error.h"
#include "magic.h"
#include "var.h"
/* (headers) */

#include "debug-print.h"

/* (specification)
   means (mns_):
     *tables=custom;
     +variables=custom;
     +crossbreak=custom;
     +format=lab:!labels/nolabels/nocatlabs,
            name:!names/nonames,
            val:!values/novalues,
            fmt:!table/tree;
     +missing=miss:!table/include/dependent;
     +cells[cl_]=default,count,sum,mean,stddev,variance,all;
     +statistics[st_]=anova,linearity,all,none.
*/
/* (declarations) */
/* (functions) */

#if DEBUGGING
static void debug_print (struct cmd_means *cmd);
#endif

/* TABLES: Variable lists for each dimension. */
int n_dim;		/* Number of dimensions. */
int *nv_dim;		/* Number of variables in each dimension. */
struct variable ***v_dim;	/* Variables in each dimension.  */

/* VARIABLES: List of variables. */
int n_var;
struct variable **v_var;

/* Parses and executes the T-TEST procedure. */
int
cmd_means (void)
{
  struct cmd_means cmd;
  int success = CMD_FAILURE;
  
  n_dim = 0;
  nv_dim = NULL;
  v_dim = NULL;
  v_var = NULL;

  lex_match_id ("MEANS");
  if (!parse_means (&cmd))
    goto free;

  if (cmd.sbc_cells)
    {
      int i;
      for (i = 0; i < MNS_CL_count; i++)
	if (cmd.a_cells[i])
	  break;
      if (i >= MNS_CL_count)
	cmd.a_cells[MNS_CL_ALL] = 1;
    }
  else
    cmd.a_cells[MNS_CL_DEFAULT] = 1;
  if (cmd.a_cells[MNS_CL_DEFAULT] || cmd.a_cells[MNS_CL_ALL])
    cmd.a_cells[MNS_CL_MEAN] = cmd.a_cells[MNS_CL_STDDEV] = cmd.a_cells[MNS_CL_COUNT] = 1;
  if (cmd.a_cells[MNS_CL_ALL])
    cmd.a_cells[MNS_CL_SUM] = cmd.a_cells[MNS_CL_VARIANCE] = 1;

  if (cmd.sbc_statistics)
    {
      if (!cmd.a_statistics[MNS_ST_ANOVA] && !cmd.a_statistics[MNS_ST_LINEARITY])
	cmd.a_statistics[MNS_ST_ANOVA] = 1;
      if (cmd.a_statistics[MNS_ST_ALL])
	cmd.a_statistics[MNS_ST_ANOVA] = cmd.a_statistics[MNS_ST_LINEARITY] = 1;
    }

  if (!cmd.sbc_tables)
    {
      msg (SE, _("Missing required subcommand TABLES."));
      goto free;
    }

#if DEBUGGING
  debug_print (&cmd);
#endif
  
  success = CMD_SUCCESS;

free:
  {
    int i;
    
    for (i = 0; i < n_dim; i++)
      free (v_dim[i]);
    free (nv_dim);
    free (v_dim);
    free (v_var);
  }
  
  return success;
}

/* Returns nonzero only if value V is valid as an endpoint for a
   dependent variable in integer mode. */
int
validate_dependent_endpoint (double V)
{
  return V == (int) V && V != LOWEST && V != HIGHEST;
}

/* Parses the TABLES subcommand. */
static int
mns_custom_tables (struct cmd_means *cmd)
{
  struct dictionary *dict;
  struct dictionary temp_dict;
  
  if (!lex_match_id ("TABLES")
      && (token != T_ID || !is_varname (tokid))
      && token != T_ALL)
    return 2;
  lex_match ('=');

  if (cmd->sbc_tables || cmd->sbc_crossbreak)
    {
      msg (SE, _("TABLES or CROSSBREAK subcommand may not appear more "
		 "than once."));
      return 0;
    }

  if (cmd->sbc_variables)
    {
      dict = &temp_dict;
      temp_dict.var = v_var;
      temp_dict.nvar = n_var;
      
      {
	int i;
      
	temp_dict.var_by_name = avl_create (NULL, cmp_variable, NULL);
	for (i = 0; i < temp_dict.nvar; i++)
	  avl_force_insert (temp_dict.var_by_name, temp_dict.var[i]);
      }
    }
  else
    dict = &default_dict;

  do
    {
      int nvl;
      struct variable **vl;
	
      if (!parse_variables (dict, &vl, &nvl, PV_NO_DUPLICATE | PV_NO_SCRATCH))
	return 0;
      
      n_dim++;
      nv_dim = xrealloc (nv_dim, n_dim * sizeof (int));
      v_dim = xrealloc (v_dim, n_dim * sizeof (struct variable **));

      nv_dim[n_dim - 1] = nvl;
      v_dim[n_dim - 1] = vl;

      if (cmd->sbc_variables)
	{
	  int i;

	  for (i = 0; i < nv_dim[0]; i++)
	    {
	      struct means_proc *v_inf = &v_dim[0][i]->p.mns;

	      if (v_inf->min == SYSMIS)
		{
		  msg (SE, _("Variable %s specified on TABLES or "
			     "CROSSBREAK, but not specified on "
			     "VARIABLES."),
		       v_dim[0][i]->name);
		  return 0;
		}
	      
	      if (n_dim == 1)
		{
		  v_inf->min = (int) v_inf->min;
		  v_inf->max = (int) v_inf->max;
		} else {
		  if (v_inf->min == LOWEST || v_inf->max == HIGHEST)
		    {
		      msg (SE, _("LOWEST and HIGHEST may not be used "
				 "for independent variables (%s)."),
			   v_dim[0][i]->name);
		      return 0;
		    }
		  if (v_inf->min != (int) v_inf->min
		      || v_inf->max != (int) v_inf->max)
		    {
		      msg (SE, _("Independent variables (%s) may not "
				 "have noninteger endpoints in their "
				 "ranges."),
			   v_dim[0][i]->name);
		      return 0;
		    }
		}
	    }
	}
    }
  while (lex_match (T_BY));

  /* Check for duplicates. */
  {
    int i;
    
    for (i = 0; i < default_dict.nvar; i++)
      default_dict.var[i]->foo = 0;
    for (i = 0; i < dict->nvar; i++)
      if (dict->var[i]->foo++)
	{
	  msg (SE, _("Variable %s is multiply specified on TABLES "
		     "or CROSSBREAK."),
	       dict->var[i]->name);
	  return 0;
	}
  }
  
  if (cmd->sbc_variables)
    avl_destroy (temp_dict.var_by_name, NULL);

  return 1;
}

/* Parse CROSSBREAK subcommand. */
static int
mns_custom_crossbreak (struct cmd_means *cmd)
{
  return mns_custom_tables (cmd);
}

/* Parses the VARIABLES subcommand. */
static int
mns_custom_variables (struct cmd_means *cmd)
{
  if (cmd->sbc_tables)
    {
      msg (SE, _("VARIABLES must precede TABLES."));
      return 0;
    }

  if (cmd->sbc_variables == 1)
    {
      int i;
      
      for (i = 0; i < default_dict.nvar; i++)
	default_dict.var[i]->p.mns.min = SYSMIS;
    }
  
  do
    {
      int orig_n = n_var;
      
      double min, max;
      
      if (!parse_variables (&default_dict, &v_var, &n_var,
			    PV_APPEND | PV_NO_DUPLICATE | PV_NO_SCRATCH))
	return 0;

      if (!lex_force_match ('('))
	return 0;

      /* Lower value. */
      if (token == T_ID
	  && (!strcmp (tokid, "LO") || lex_id_match ("LOWEST", tokid)))
	min = LOWEST;
      else
	{
	  if (!lex_force_num ())
	    return 0;
	  min = tokval;
	}
      lex_get ();

      lex_match (',');

      /* Higher value. */
      if (token == T_ID
	  && (!strcmp (tokid, "HI") || lex_id_match ("HIGHEST", tokid)))
	max = HIGHEST;
      else
	{
	  if (!lex_force_num ())
	    return 0;
	  max = tokval;
	}
      lex_get ();

      if (!lex_force_match (')'))
	return 0;

      /* Range check. */
      if (max < min)
	{
	  msg (SE, _("Upper value (%g) is less than lower value "
		     "(%g) on VARIABLES subcommand."), max, min);
	  return 0;
	}
      
      {
	int i;

	for (i = orig_n; i < n_var; i++)
	  {
	    struct means_proc *v_inf = &v_var[i]->p.mns;

	    v_inf->min = min;
	    v_inf->max = max;
	  }
      }
    }
  while (token != '/' && token != '.');
  
  return 1;
}

#if DEBUGGING
static void
debug_print (struct cmd_means *cmd)
{
  int i;
  
  printf ("MEANS");

  if (cmd->sbc_variables)
    {
      int j = 0;
      
      printf (" VARIABLES=");
      for (i = 0; i < default_dict.nvar; i++)
	{
	  struct variable *v = default_dict.var[i];
	  
	  if (v->p.mns.min == SYSMIS)
	    continue;
	  if (j++)
	    printf (" ");
	  printf ("%s(", v->name);
	  if (v->p.mns.min == LOWEST)
	    printf ("LO");
	  else
	    printf ("%g", v->p.mns.min);
	  printf (",");
	  if (v->p.mns.max == HIGHEST)
	    printf ("HI");
	  else
	    printf ("%g", v->p.mns.max);
	  printf (")");
	}
      printf ("\n");
    }

  printf (" TABLES=");
  for (i = 0; i < n_dim; i++)
    {
      int j;

      if (i)
	printf (" BY");

      for (j = 0; j < nv_dim[i]; j++)
	{
	  if (i || j)
	    printf (" ");
	  printf (v_dim[i][j]->name);
	}
    }
  printf ("\n");
}
#endif /* DEBUGGING */

/* 
   Local Variables:
   mode: c
   End:
*/
