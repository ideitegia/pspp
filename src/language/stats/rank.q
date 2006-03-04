/* PSPP - RANK. -*-c-*-

Copyright (C) 2005 Free Software Foundation, Inc.
Author: John Darrington 2005

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
#include "command.h"
#include "dictionary.h"
#include "sort.h"
#include "variable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* (headers) */

/* (specification)
   "RANK" (rank_):
   *^variables=custom;
   +rank=custom;
   +normal=custom;
   +percent=custom;
   +ntiles=custom;
   +rfraction=custom;
   +proportion=custom;
   +n=custom;
   +savage=custom;
   +print=print:!yes/no;
   +missing=miss:!exclude/include.
*/
/* (declarations) */
/* (functions) */



enum RANK_FUNC
  {
    RANK,
    NORMAL,
    PERCENT,
    RFRACTION,
    PROPORTION,
    N,
    NTILES,
    SAVAGE,
  };


struct rank_spec
{
  enum RANK_FUNC rfunc;
  struct variable **destvars;
  struct variable *srcvar;
};


static struct rank_spec *rank_specs;
static size_t n_rank_specs;

static struct sort_criteria *sc;

static struct variable **group_vars;
static size_t n_group_vars;

static struct cmd_rank cmd;



int cmd_rank(void);

int
cmd_rank(void)
{
  size_t i;
  n_rank_specs = 0;

  if ( !parse_rank(&cmd) )
    return CMD_FAILURE;

#if 1
  for (i = 0 ; i <  sc->crit_cnt ; ++i )
    {
      struct sort_criterion *crit = &sc->crits[i];
      
      printf("Dir: %d; Index: %d\n", crit->dir, crit->fv);
    }

  for (i = 0 ; i <  n_group_vars ; ++i )
    printf("Group var: %s\n",group_vars[0]->name);

  for (i = 0 ; i <  n_rank_specs ; ++i )
    {
      int j;
      printf("Ranks spec %d; Func: %d\n",i, rank_specs[i].rfunc);
      
      for (j=0; j < sc->crit_cnt ; ++j )
	printf("Dest var is \"%s\"\n", rank_specs[i].destvars[j]->name);
    }
#endif 


  free(group_vars);
  
  for (i = 0 ; i <  n_rank_specs ; ++i )
    {
      free(rank_specs[i].destvars);
    }
      
  free(rank_specs);

  sort_destroy_criteria(sc);

  return CMD_SUCCESS;
}



/* Parser for the variables sub command  
   Returns 1 on success */
static int
rank_custom_variables(struct cmd_rank *cmd UNUSED)
{
  static const int terminators[2] = {T_BY, 0};

  lex_match('=');

  if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL)
      && token != T_ALL)
      return 2;

  sc = sort_parse_criteria (default_dict, 0, 0, 0, terminators);

  if ( lex_match(T_BY)  )
    {
      if ((token != T_ID || dict_lookup_var (default_dict, tokid) == NULL))
	{
	  return 2;
	}

      if (!parse_variables (default_dict, &group_vars, &n_group_vars,
			    PV_NO_DUPLICATE | PV_NUMERIC | PV_NO_SCRATCH) )
	{
	  free (group_vars);
	  return 0;
	}
    }

  return 1;
}


/* Return a name for a new variable which ranks the variable VAR_NAME,
   according to the ranking function F.
   If IDX is non zero, then IDX is used as a disambiguating number.
   FIXME: This is not very robust.
*/
static char *
new_variable_name(const char *ranked_var_name, enum RANK_FUNC f, int idx)
{
  static char new_name[SHORT_NAME_LEN + 1];
  char temp[SHORT_NAME_LEN + 1];
 
  if ( idx == 0 ) 
    {
      switch (f) 
	{
	case RANK:
	case RFRACTION:
	  strcpy(new_name,"R");
	  break;

	case NORMAL:
	case N:
	case NTILES:
	  strcpy(new_name,"N");
	  break;
      
	case PERCENT:
	case PROPORTION:
	  strcpy(new_name,"P");
	  break;

	case SAVAGE:
	  strcpy(new_name,"S");
	  break;

	default:
	  assert(false);
	  break;
	}
  
      strncat(new_name, ranked_var_name, 7);
    }
  else
    {
      strncpy(temp, ranked_var_name, 3);
      snprintf(new_name, SHORT_NAME_LEN, "%s%03d", temp, idx);
    }

  return new_name;
}

/* Parse the [/rank INTO var1 var2 ... varN ] clause */
static int
parse_rank_function(struct cmd_rank *cmd UNUSED, enum RANK_FUNC f)
{
  static const struct fmt_spec f8_2 = {FMT_F, 8, 2};
  int var_count = 0;
  
  n_rank_specs++;
  rank_specs = xnrealloc(rank_specs, n_rank_specs, sizeof *rank_specs);
  rank_specs[n_rank_specs - 1].rfunc = f;

  rank_specs[n_rank_specs - 1].destvars = 
	    xcalloc (sc->crit_cnt, sizeof (struct variable *));
	  
  if (lex_match_id("INTO"))
    {
      struct variable *destvar;

      while( token == T_ID ) 
	{
	  ++var_count;
	  if ( dict_lookup_var (default_dict, tokid) != NULL )
	    {
	      msg(ME, _("Variable %s already exists."), tokid);
	      return 0;
	    }
	  if ( var_count > sc->crit_cnt ) 
	    {
	      msg(ME, _("Too many variables in INTO clause."));
	      return 0;
	    }

	  destvar = dict_create_var (default_dict, tokid, 0);
	  if ( destvar ) 
	    {
	      destvar->print = destvar->write = f8_2;
	    }
	  
	  rank_specs[n_rank_specs - 1].destvars[var_count - 1] = destvar ;

	  lex_get();
	  
	}
    }

  /* Allocate rank  variable names to all those which haven't had INTO 
     variables assigned */
  while (var_count < sc->crit_cnt)
    {
      static int idx=0;
      struct variable *destvar ; 
      const struct variable *v = dict_get_var(default_dict,
					      sc->crits[var_count].fv);

      char *new_name;
      
      do {
	new_name = new_variable_name(v->name, f, idx);

	destvar = dict_create_var (default_dict, new_name, 0);
	if (!destvar ) 
	  ++idx;

      } while( !destvar ) ;

      destvar->print = destvar->write = f8_2;

      rank_specs[n_rank_specs - 1].destvars[var_count] = destvar ;
      
      ++var_count;
    }

  return 1;
}


static int
rank_custom_rank(struct cmd_rank *cmd )
{
  return parse_rank_function(cmd, RANK);
}

static int
rank_custom_normal(struct cmd_rank *cmd )
{
  return parse_rank_function(cmd, NORMAL);
}

static int
rank_custom_percent(struct cmd_rank *cmd )
{
  return parse_rank_function(cmd, NORMAL);
}

static int
rank_custom_rfraction(struct cmd_rank *cmd )
{
  return parse_rank_function(cmd, RFRACTION);
}

static int
rank_custom_proportion(struct cmd_rank *cmd )
{
  return parse_rank_function(cmd, PROPORTION);
}

static int
rank_custom_n(struct cmd_rank *cmd )
{
  return parse_rank_function(cmd, N);
}

static int
rank_custom_savage(struct cmd_rank *cmd )
{
  return parse_rank_function(cmd, SAVAGE);
}


static int
rank_custom_ntiles(struct cmd_rank *cmd )
{
  if ( lex_force_match('(') ) 
    {
      if ( lex_force_int() ) 
	{
	  lex_get();
	  lex_force_match(')');
	}
      else
	return 0;
    }
  else
    return 0;

  return parse_rank_function(cmd, NTILES);
}


