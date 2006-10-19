/* PSPP - RANK. -*-c-*-

Copyright (C) 2005, 2006 Free Software Foundation, Inc.
Author: John Darrington <john@darrington.wattle.id.au>, 
        Ben Pfaff <blp@gnu.org>.

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

#include "sort-criteria.h"

#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/variable.h>
#include <data/case.h>
#include <data/casefile.h>
#include <data/fastfile.h>
#include <data/storage-stream.h>
#include <language/command.h>
#include <language/stats/sort-criteria.h>
#include <limits.h>
#include <libpspp/compiler.h>
#include <math/sort.h>
#include <output/table.h>
#include <output/manager.h>

#include <gsl/gsl_cdf.h>
#include <math.h>

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
   +fraction=fraction:!blom/tukey/vw/rankit;
   +ties=ties:!mean/low/high/condense;
   missing=miss:!exclude/include.
*/
/* (declarations) */
/* (functions) */

typedef double (*rank_function_t) (double c, double cc, double cc_1, 
				 int i, double w);

static double rank_proportion (double c, double cc, double cc_1, 
			       int i, double w);

static double rank_normal (double c, double cc, double cc_1, 
			   int i, double w);

static double rank_percent (double c, double cc, double cc_1, 
			    int i, double w);

static double rank_rfraction (double c, double cc, double cc_1, 
			      int i, double w);

static double rank_rank (double c, double cc, double cc_1, 
			 int i, double w);

static double rank_n (double c, double cc, double cc_1, 
		      int i, double w);

static double rank_savage (double c, double cc, double cc_1, 
		      int i, double w);

static double rank_ntiles (double c, double cc, double cc_1, 
		      int i, double w);


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
    n_RANK_FUNCS
  };

static const struct fmt_spec dest_format[n_RANK_FUNCS] = {
  {FMT_F, 9, 3}, /* rank */
  {FMT_F, 6, 4}, /* normal */
  {FMT_F, 6, 2}, /* percent */
  {FMT_F, 6, 4}, /* rfraction */
  {FMT_F, 6, 4}, /* proportion */
  {FMT_F, 6, 0}, /* n */
  {FMT_F, 3, 0}, /* ntiles */
  {FMT_F, 8, 4}  /* savage */
};

static const char *function_name[n_RANK_FUNCS] = {
  "RANK",
  "NORMAL",
  "PERCENT",
  "RFRACTION",
  "PROPORTION",
  "N",
  "NTILES",
  "SAVAGE"
};

static rank_function_t rank_func[n_RANK_FUNCS] = {
  rank_rank,
  rank_normal,
  rank_percent,
  rank_rfraction,
  rank_proportion,
  rank_n,
  rank_ntiles,
  rank_savage 
  };


struct rank_spec
{
  enum RANK_FUNC rfunc;
  struct variable **destvars;
};


/* Function to use for testing for missing values */
static is_missing_func *value_is_missing;

static struct rank_spec *rank_specs;
static size_t n_rank_specs;

static struct sort_criteria *sc;

static struct variable **group_vars;
static size_t n_group_vars;

static struct variable **src_vars;
static size_t n_src_vars;


static int k_ntiles;

static struct cmd_rank cmd;

static struct casefile *rank_sorted_casefile (struct casefile *cf, 
					      const struct sort_criteria *, 
					      const struct rank_spec *rs, 
					      int n_rank_specs,
					      int idx,
					      const struct missing_values *miss
					      );
static const char *
fraction_name(void)
{
  static char name[10];
  switch ( cmd.fraction ) 
    {
    case RANK_BLOM:
      strcpy (name, "BLOM");
      break;
    case RANK_RANKIT:
      strcpy (name, "RANKIT");
      break;
    case RANK_TUKEY:
      strcpy (name, "TUKEY");
      break;
    case RANK_VW:
      strcpy (name, "VW");
      break;
    default:
      NOT_REACHED ();
    }
  return name;
}

/* Create a label on DEST_VAR, describing its derivation from SRC_VAR and F */
static void
create_var_label (struct variable *dest_var, 
		  const struct variable *src_var, enum RANK_FUNC f)
{
  struct string label;
  ds_init_empty (&label);

  if ( n_group_vars > 0 ) 
    {
      struct string group_var_str;
      int g;

      ds_init_empty (&group_var_str);

      for (g = 0 ; g < n_group_vars ; ++g ) 
	{
	  if ( g > 0 ) ds_put_cstr (&group_var_str, " ");
	  ds_put_cstr (&group_var_str, group_vars[g]->name);
	}

      ds_put_format (&label, _("%s of %s by %s"), function_name[f], 
		     src_var->name, ds_cstr (&group_var_str));
      ds_destroy (&group_var_str);
    }
  else
    ds_put_format (&label,_("%s of %s"), function_name[f], src_var->name);  

  dest_var->label = strdup (ds_cstr (&label) );

  ds_destroy (&label);
}


static bool 
rank_cmd (const struct sort_criteria *sc, 
      const struct rank_spec *rank_specs, int n_rank_specs)
{
  struct sort_criteria criteria;
  bool result = true;
  int i;
  const int n_splits = dict_get_split_cnt (dataset_dict (current_dataset));

  criteria.crit_cnt = n_splits + n_group_vars + 1;
  criteria.crits = xnmalloc (criteria.crit_cnt, sizeof *criteria.crits);
  for (i = 0; i < n_splits ; i++) 
    {
      struct variable *v = dict_get_split_vars (dataset_dict (current_dataset))[i];
      criteria.crits[i].fv = v->fv;
      criteria.crits[i].width = v->width;
      criteria.crits[i].dir = SRT_ASCEND;
    }
  for (i = 0; i < n_group_vars; i++) 
    {
      criteria.crits[i + n_splits].fv = group_vars[i]->fv;
      criteria.crits[i + n_splits].width = group_vars[i]->width;
      criteria.crits[i + n_splits].dir = SRT_ASCEND;
    }
  for (i = 0 ; i < sc->crit_cnt ; ++i )
    {
      struct casefile *out ;
      struct casefile *cf ; 
      struct casereader *reader ;
      struct casefile *sorted_cf ;

      /* Obtain active file in CF. */
      if (!procedure (current_dataset, NULL, NULL))
	goto error;

      cf = proc_capture_output (current_dataset);

      /* Sort CF into SORTED_CF. */
      reader = casefile_get_destructive_reader (cf) ;
      criteria.crits[criteria.crit_cnt - 1] = sc->crits[i];
      assert ( sc->crits[i].fv == src_vars[i]->fv );
      sorted_cf = sort_execute (reader, &criteria);
      casefile_destroy (cf);

      out = rank_sorted_casefile (sorted_cf, &criteria,
                                  rank_specs, n_rank_specs, 
				  i, &src_vars[i]->miss)  ;
      if ( NULL == out ) 
	{
	  result = false ;
	  continue ;
	}
      
      proc_set_source (current_dataset, storage_source_create (out));
    }

  free (criteria.crits);
  return result ; 

error:
  free (criteria.crits);
  return false ;
}

/* Hardly a rank function !! */
static double 
rank_n (double c UNUSED, double cc UNUSED, double cc_1 UNUSED, 
	  int i UNUSED, double w)
{
  return w;
}


static double 
rank_rank (double c, double cc, double cc_1, 
	  int i, double w UNUSED)
{
  double rank;
  if ( c >= 1.0 ) 
    {
      switch (cmd.ties)
	{
	case RANK_LOW:
	  rank = cc_1 + 1;
	  break;
	case RANK_HIGH:
	  rank = cc;
	  break;
	case RANK_MEAN:
	  rank = cc_1 + (c + 1.0)/ 2.0;
	  break;
	case RANK_CONDENSE:
	  rank = i;
	  break;
	default:
	  NOT_REACHED ();
	}
    }
  else
    {
      switch (cmd.ties)
	{
	case RANK_LOW:
	  rank = cc_1;
	  break;
	case RANK_HIGH:
	  rank = cc;
	  break;
	case RANK_MEAN:
	  rank = cc_1 + c / 2.0 ;
	  break;
	case RANK_CONDENSE:
	  rank = i;
	  break;
	default:
	  NOT_REACHED ();
	}
    }

  return rank;
}


static double 
rank_rfraction (double c, double cc, double cc_1, 
		int i, double w)
{
  return rank_rank (c, cc, cc_1, i, w) / w ;
}


static double 
rank_percent (double c, double cc, double cc_1, 
		int i, double w)
{
  return rank_rank (c, cc, cc_1, i, w) * 100.0 / w ;
}


static double 
rank_proportion (double c, double cc, double cc_1, 
		 int i, double w)
{
  const double r =  rank_rank (c, cc, cc_1, i, w) ;

  double f;
  
  switch ( cmd.fraction ) 
    {
    case RANK_BLOM:
      f =  (r - 3.0/8.0) / (w + 0.25);
      break;
    case RANK_RANKIT:
      f = (r - 0.5) / w ;
      break;
    case RANK_TUKEY:
      f = (r - 1.0/3.0) / (w + 1.0/3.0);
      break;
    case RANK_VW:
      f = r / ( w + 1.0);
      break;
    default:
      NOT_REACHED ();
    }


  return (f > 0) ? f : SYSMIS;
}

static double 
rank_normal (double c, double cc, double cc_1, 
	     int i, double w)
{
  double f = rank_proportion (c, cc, cc_1, i, w);
  
  return gsl_cdf_ugaussian_Pinv (f);
}

static double 
rank_ntiles (double c, double cc, double cc_1, 
		int i, double w)
{
  double r = rank_rank (c, cc, cc_1, i, w);  


  return ( floor (( r * k_ntiles) / ( w + 1) ) + 1);
}

/* Expected value of the order statistics from an exponential distribution */
static double
ee (int j, double w_star)
{
  int k;
  double sum = 0.0;
  
  for (k = 1 ; k <= j; k++) 
    sum += 1.0 / ( w_star + 1 - k );

  return sum;
}


static double 
rank_savage (double c, double cc, double cc_1, 
		int i UNUSED, double w)
{
  double int_part;
  const int i_1 = floor (cc_1);
  const int i_2 = floor (cc);

  const double w_star = (modf (w, &int_part) == 0 ) ? w : floor (w) + 1;

  const double g_1 = cc_1 - i_1;
  const double g_2 = cc - i_2;

  /* The second factor is infinite, when the first is zero.
     Therefore, evaluate the second, only when the first is non-zero */
  const double expr1 =  (1 - g_1) ? (1 - g_1) * ee(i_1+1, w_star) : ( 1 - g_1);
  const double expr2 =  g_2 ? g_2 * ee (i_2+1, w_star) : g_2 ;
  
  if ( i_1 == i_2 ) 
    return ee (i_1 + 1, w_star) - 1;
  
  if ( i_1 + 1 == i_2 )
    return ( ( expr1 + expr2 )/c ) - 1;

  if ( i_1 + 2 <= i_2 ) 
    {
      int j;
      double sigma = 0.0;
      for (j = i_1 + 2 ; j <= i_2; ++j )
	sigma += ee (j, w_star);
      return ( (expr1 + expr2 + sigma) / c) -1;
    }

  NOT_REACHED();
}


/* Rank the casefile belonging to CR, starting from the current
   postition of CR continuing up to and including the ENDth case.

   RS points to an array containing  the rank specifications to
   use. N_RANK_SPECS is the number of elements of RS.


   DEST_VAR_INDEX is the index into the rank_spec destvar element 
   to be used for this ranking.

   Prerequisites: 1. The casefile must be sorted according to CRITERION.
                  2. W is the sum of the non-missing caseweights for this 
		  range of the casefile.
*/
static void
rank_cases (struct casereader *cr,
	    unsigned long end,
	    const struct sort_criterion *criterion,
	    const struct missing_values *mv,
	    double w,
	    const struct rank_spec *rs, 
	    int n_rank_specs, 
	    int dest_var_index,
	    struct casefile *dest)
{
  bool warn = true;
  double cc = 0.0;
  double cc_1;
  int iter = 1;

  const int fv = criterion->fv;
  const int width = criterion->width;

  while (casereader_cnum (cr) < end)
    {
      struct casereader *lookahead;
      const union value *this_value;
      struct ccase this_case, lookahead_case;
      double c;
      int i;
      size_t n = 0;

      if (!casereader_read_xfer (cr, &this_case))
        break;
      
      this_value = case_data (&this_case, fv);
      c = dict_get_case_weight (dataset_dict (current_dataset), &this_case, &warn);
              
      lookahead = casereader_clone (cr);
      n = 0;
      while (casereader_cnum (lookahead) < end
             && casereader_read_xfer (lookahead, &lookahead_case))
        {
          const union value *lookahead_value = case_data (&lookahead_case, fv);
          int diff = compare_values (this_value, lookahead_value, width);

          if (diff != 0) 
            {
	      /* Make sure the casefile was sorted */
	      assert ( diff == ((criterion->dir == SRT_ASCEND) ? -1 :1));

              case_destroy (&lookahead_case);
              break; 
            }

          c += dict_get_case_weight (dataset_dict (current_dataset), &lookahead_case, &warn);
          case_destroy (&lookahead_case);
          n++;
        }
      casereader_destroy (lookahead);

      cc_1 = cc;
      if ( !value_is_missing (mv, this_value) )
	cc += c;

      do
        {
          for (i = 0; i < n_rank_specs; ++i) 
            {
              const int dest_idx = rs[i].destvars[dest_var_index]->fv;

	      if  ( value_is_missing (mv, this_value) )
		case_data_rw (&this_case, dest_idx)->f = SYSMIS;
	      else
		case_data_rw (&this_case, dest_idx)->f = 
		  rank_func[rs[i].rfunc](c, cc, cc_1, iter, w);
            }
          casefile_append_xfer (dest, &this_case); 
        }
      while (n-- > 0 && casereader_read_xfer (cr, &this_case));

      if ( !value_is_missing (mv, this_value) )
	iter++;
    }

  /* If this isn't true, then all the results will be wrong */
  assert ( w == cc );
}

static bool
same_group (const struct ccase *a, const struct ccase *b,
            const struct sort_criteria *crit)
{
  size_t i;

  for (i = 0; i < crit->crit_cnt - 1; i++)
    {
      struct sort_criterion *c = &crit->crits[i];
      if (compare_values (case_data (a, c->fv), case_data (b, c->fv),
                          c->width) != 0)
        return false;
    }

  return true;
}

static struct casefile *
rank_sorted_casefile (struct casefile *cf, 
		      const struct sort_criteria *crit, 
		      const struct rank_spec *rs, 
		      int n_rank_specs, 
		      int dest_idx, 
		      const struct missing_values *mv)
{
  struct casefile *dest = fastfile_create (casefile_get_value_cnt (cf));
  struct casereader *lookahead = casefile_get_reader (cf);
  struct casereader *pos = casereader_clone (lookahead);
  struct ccase group_case;
  bool warn = true;

  struct sort_criterion *ultimate_crit = &crit->crits[crit->crit_cnt - 1];

  if (casereader_read (lookahead, &group_case)) 
    {
      struct ccase this_case;
      const union value *this_value ;
      double w = 0.0;
      this_value = case_data( &group_case, ultimate_crit->fv);

      if ( !value_is_missing(mv, this_value) )
	w = dict_get_case_weight (dataset_dict (current_dataset), &group_case, &warn);

      while (casereader_read (lookahead, &this_case)) 
        {
	  const union value *this_value = 
	    case_data(&this_case, ultimate_crit->fv);
          double c = dict_get_case_weight (dataset_dict (current_dataset), &this_case, &warn);
          if (!same_group (&group_case, &this_case, crit)) 
            {
              rank_cases (pos, casereader_cnum (lookahead) - 1,
			  ultimate_crit, 
			  mv, w, 
			  rs, n_rank_specs, 
			  dest_idx, dest);

              w = 0.0;
              case_destroy (&group_case);
              case_move (&group_case, &this_case);
            }
	  if ( !value_is_missing (mv, this_value) )
	    w += c;
          case_destroy (&this_case);
        }
      case_destroy (&group_case);
      rank_cases (pos, ULONG_MAX, ultimate_crit, mv, w,
		  rs, n_rank_specs, dest_idx, dest);
    }

  if (casefile_error (dest))
    {
      casefile_destroy (dest);
      dest = NULL;
    }
  
  casefile_destroy (cf);
  return dest;
}


/* Transformation function to enumerate all the cases */
static int 
create_resort_key (void *key_var_, struct ccase *cc, casenum_t case_num)
{
  struct variable *key_var = key_var_;

  case_data_rw(cc, key_var->fv)->f = case_num;
  
  return TRNS_CONTINUE;
}


/* Create and return a new variable in which to store the ranks of SRC_VAR
   accoring to the rank function F.
   VNAME is the name of the variable to be created.
   If VNAME is NULL, then a name will be automatically chosen.
 */
static struct variable *
create_rank_variable (enum RANK_FUNC f, 
		      const struct variable *src_var, 
		      const char *vname)
{
  int i;
  struct variable *var = NULL; 
  char name[SHORT_NAME_LEN + 1];

  if ( vname ) 
    var = dict_create_var(dataset_dict (current_dataset), vname, 0);

  if ( NULL == var )
    {
      snprintf(name, SHORT_NAME_LEN + 1, "%c%s", 
	       function_name[f][0], src_var->name);
  
      var = dict_create_var(dataset_dict (current_dataset), name, 0);
    }

  i = 1;
  while( NULL == var )
    {
      char func_abb[4];
      snprintf(func_abb, 4, "%s", function_name[f]);
      snprintf(name, SHORT_NAME_LEN + 1, "%s%03d", func_abb, 
	       i);

      var = dict_create_var(dataset_dict (current_dataset), name, 0);
      if (i++ >= 999) 
	break;
    }

  i = 1;
  while ( NULL == var )
    {
      char func_abb[3];
      snprintf(func_abb, 3, "%s", function_name[f]);

      snprintf(name, SHORT_NAME_LEN + 1, 
	       "RNK%s%02d", func_abb, i);

      var = dict_create_var(dataset_dict (current_dataset), name, 0);
      if ( i++ >= 99 ) 
	break;
    }
  
  if ( NULL == var ) 
    {
      msg(ME, _("Cannot create new rank variable.  All candidates in use."));
      return NULL;
    }

  var->write = var->print = dest_format[f];

  return var;
}

int cmd_rank(void);

static void
rank_cleanup(void)
{
  int i;

  free (group_vars);
  group_vars = NULL;
  n_group_vars = 0;
  
  for (i = 0 ; i <  n_rank_specs ; ++i )
    {
      free (rank_specs[i].destvars);
    }
      
  free (rank_specs);
  rank_specs = NULL;
  n_rank_specs = 0;

  sort_destroy_criteria (sc);
  sc = NULL;

  free (src_vars);
  src_vars = NULL;
  n_src_vars = 0;
}

int
cmd_rank(void)
{
  bool result;
  struct variable *order;
  size_t i;
  n_rank_specs = 0;

  if ( !parse_rank(&cmd, NULL) )
    {
      rank_cleanup ();
    return CMD_FAILURE;
    }

  /* If /MISSING=INCLUDE is set, then user missing values are ignored */
  if (cmd.miss == RANK_INCLUDE ) 
    value_is_missing = mv_is_value_system_missing;
  else
    value_is_missing = mv_is_value_missing;


  /* Default to /RANK if no function subcommands are given */
  if ( !( cmd.sbc_normal  || cmd.sbc_ntiles || cmd.sbc_proportion || 
	  cmd.sbc_rfraction || cmd.sbc_savage || cmd.sbc_n || 
	  cmd.sbc_percent || cmd.sbc_rank ) )
    {
      assert ( n_rank_specs == 0 );
      
      rank_specs = xmalloc (sizeof (*rank_specs));
      rank_specs[0].rfunc = RANK;
      rank_specs[0].destvars = 
	xcalloc (sc->crit_cnt, sizeof (struct variable *));

      n_rank_specs = 1;
    }

  assert ( sc->crit_cnt == n_src_vars);

  /* Create variables for all rank destinations which haven't
     already been created with INTO.
     Add labels to all the destination variables.
  */
  for (i = 0 ; i <  n_rank_specs ; ++i )
    {
      int v;
      for ( v = 0 ; v < n_src_vars ;  v ++ ) 
	{
	  if ( rank_specs[i].destvars[v] == NULL ) 
	    {
	      rank_specs[i].destvars[v] = 
		create_rank_variable (rank_specs[i].rfunc, src_vars[v], NULL);
	    }
      
	  create_var_label ( rank_specs[i].destvars[v],
			     src_vars[v],
			     rank_specs[i].rfunc);
	}
    }

  if ( cmd.print == RANK_YES ) 
    {
      int v;

      tab_output_text (0, _("Variables Created By RANK"));
      tab_output_text (0, "\n");
  
      for (i = 0 ; i <  n_rank_specs ; ++i )
	{
	  for ( v = 0 ; v < n_src_vars ;  v ++ ) 
	    {
	      if ( n_group_vars > 0 )
		{
		  struct string varlist;
		  int g;

		  ds_init_empty (&varlist);
		  for ( g = 0 ; g < n_group_vars ; ++g ) 
		    {
		      ds_put_cstr (&varlist, group_vars[g]->name);

		      if ( g < n_group_vars - 1)
			ds_put_cstr (&varlist, " ");
		    }

		  if ( rank_specs[i].rfunc == NORMAL || 
		       rank_specs[i].rfunc == PROPORTION ) 
		    tab_output_text (TAT_PRINTF,
				     _("%s into %s(%s of %s using %s BY %s)"), 
				     src_vars[v]->name,
				     rank_specs[i].destvars[v]->name,
				     function_name[rank_specs[i].rfunc],
				     src_vars[v]->name,
				     fraction_name(),
				     ds_cstr (&varlist)
				     );
		    
		  else
		    tab_output_text (TAT_PRINTF,
				     _("%s into %s(%s of %s BY %s)"), 
				     src_vars[v]->name,
				     rank_specs[i].destvars[v]->name,
				     function_name[rank_specs[i].rfunc],
				     src_vars[v]->name,
				     ds_cstr (&varlist)
				     );
		  ds_destroy (&varlist);
		}
	      else
		{
		  if ( rank_specs[i].rfunc == NORMAL || 
		       rank_specs[i].rfunc == PROPORTION ) 
		    tab_output_text (TAT_PRINTF,
				     _("%s into %s(%s of %s using %s)"), 
				     src_vars[v]->name,
				     rank_specs[i].destvars[v]->name,
				     function_name[rank_specs[i].rfunc],
				     src_vars[v]->name,
				     fraction_name()
				     );
		    
		  else
		    tab_output_text (TAT_PRINTF,
				     _("%s into %s(%s of %s)"), 
				     src_vars[v]->name,
				     rank_specs[i].destvars[v]->name,
				     function_name[rank_specs[i].rfunc],
				     src_vars[v]->name
				     );
		}
	    }
	}
    }

  if ( cmd.sbc_fraction && 
       ( ! cmd.sbc_normal && ! cmd.sbc_proportion) )
    msg(MW, _("FRACTION has been specified, but NORMAL and PROPORTION rank functions have not been requested.  The FRACTION subcommand will be ignored.") );

  /* Add a variable which we can sort by to get back the original
     order */
  order = dict_create_var_assert (dataset_dict (current_dataset), "$ORDER_", 0);

  add_transformation (current_dataset, create_resort_key, 0, order);

  /* Do the ranking */
  result = rank_cmd (sc, rank_specs, n_rank_specs);

  /* Put the active file back in its original order */
  {
    struct sort_criteria criteria;
    struct sort_criterion restore_criterion ;
    restore_criterion.fv = order->fv;
    restore_criterion.width = 0;
    restore_criterion.dir = SRT_ASCEND;

    criteria.crits = &restore_criterion;
    criteria.crit_cnt = 1;
    
    sort_active_file_in_place (&criteria);
}

  /* ... and we don't need our sort key anymore. So delete it */
  dict_delete_var (dataset_dict (current_dataset), order);

  rank_cleanup();

  return (result ? CMD_SUCCESS : CMD_CASCADING_FAILURE);
}


/* Parser for the variables sub command  
   Returns 1 on success */
static int
rank_custom_variables(struct cmd_rank *cmd UNUSED, void *aux UNUSED)
{
  static const int terminators[2] = {T_BY, 0};

  lex_match('=');

  if ((token != T_ID || dict_lookup_var (dataset_dict (current_dataset), tokid) == NULL)
      && token != T_ALL)
      return 2;

  sc = sort_parse_criteria (dataset_dict (current_dataset), 
			    &src_vars, &n_src_vars, 0, terminators);

  if ( lex_match(T_BY)  )
    {
      if ((token != T_ID || dict_lookup_var (dataset_dict (current_dataset), tokid) == NULL))
	{
	  return 2;
	}

      if (!parse_variables (dataset_dict (current_dataset), &group_vars, &n_group_vars,
			    PV_NO_DUPLICATE | PV_NUMERIC | PV_NO_SCRATCH) )
	{
	  free (group_vars);
	  return 0;
	}
    }

  return 1;
}


/* Parse the [/rank INTO var1 var2 ... varN ] clause */
static int
parse_rank_function(struct cmd_rank *cmd UNUSED, enum RANK_FUNC f)
{
  int var_count = 0;
  
  n_rank_specs++;
  rank_specs = xnrealloc(rank_specs, n_rank_specs, sizeof *rank_specs);
  rank_specs[n_rank_specs - 1].rfunc = f;
  rank_specs[n_rank_specs - 1].destvars = NULL;

  rank_specs[n_rank_specs - 1].destvars = 
	    xcalloc (sc->crit_cnt, sizeof (struct variable *));
	  
  if (lex_match_id("INTO"))
    {
      struct variable *destvar;

      while( token == T_ID ) 
	{

	  if ( dict_lookup_var (dataset_dict (current_dataset), tokid) != NULL )
	    {
	      msg(SE, _("Variable %s already exists."), tokid);
	      return 0;
	    }
	  if ( var_count >= sc->crit_cnt ) 
	    {
	      msg(SE, _("Too many variables in INTO clause."));
	      return 0;
	    }

	  destvar = create_rank_variable (f, src_vars[var_count], tokid);
	  rank_specs[n_rank_specs - 1].destvars[var_count] = destvar ;

	  lex_get();
	  ++var_count;
	}
    }

  return 1;
}


static int
rank_custom_rank(struct cmd_rank *cmd, void *aux UNUSED )
{
  return parse_rank_function(cmd, RANK);
}

static int
rank_custom_normal(struct cmd_rank *cmd, void *aux UNUSED )
{
  return parse_rank_function(cmd, NORMAL);
}

static int
rank_custom_percent(struct cmd_rank *cmd, void *aux UNUSED )
{
  return parse_rank_function (cmd, PERCENT);
}

static int
rank_custom_rfraction(struct cmd_rank *cmd, void *aux UNUSED )
{
  return parse_rank_function(cmd, RFRACTION);
}

static int
rank_custom_proportion(struct cmd_rank *cmd, void *aux UNUSED )
{
  return parse_rank_function(cmd, PROPORTION);
}

static int
rank_custom_n(struct cmd_rank *cmd, void *aux UNUSED )
{
  return parse_rank_function(cmd, N);
}

static int
rank_custom_savage(struct cmd_rank *cmd, void *aux UNUSED )
{
  return parse_rank_function(cmd, SAVAGE);
}


static int
rank_custom_ntiles(struct cmd_rank *cmd, void *aux UNUSED )
{
  if ( lex_force_match('(') ) 
    {
      if ( lex_force_int() ) 
	{
	  k_ntiles = lex_integer ();
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
