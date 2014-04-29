/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"

#include "libpspp/misc.h"
#include "libpspp/pool.h"

#include "math/categoricals.h"
#include "math/interaction.h"
#include "math/moments.h"

#include "output/tab.h"

#include <math.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)


struct means;

struct per_var_data
{
  void **cell_stats;
  struct moments1 *mom;
};


typedef void *stat_create (struct pool *pool);
typedef void stat_update  (void *stat, double w, double x);
typedef double stat_get   (const struct per_var_data *, void *aux);

struct cell_spec
{
  /* Printable title for output */
  const char *title;

  /* Keyword for syntax */
  const char *keyword;

  stat_create *sc;
  stat_update *su;
  stat_get *sd;
};

struct harmonic_mean
{
  double rsum;
  double n;
};

static void *
harmonic_create (struct pool *pool)
{
  struct harmonic_mean *hm = pool_alloc (pool, sizeof *hm);

  hm->rsum = 0;
  hm->n = 0;

  return hm;
}


static void
harmonic_update (void *stat, double w, double x)
{
  struct harmonic_mean *hm = stat;
  hm->rsum  += w / x;
  hm->n += w;
}


static double
harmonic_get (const struct per_var_data *pvd UNUSED, void *stat)
{
  struct harmonic_mean *hm = stat;

  return hm->n / hm->rsum;
}



struct geometric_mean
{
  double prod;
  double n;
};


static void *
geometric_create (struct pool *pool)
{
  struct geometric_mean *gm = pool_alloc (pool, sizeof *gm);

  gm->prod = 1.0;
  gm->n = 0;

  return gm;
}


static void
geometric_update (void *stat, double w, double x)
{
  struct geometric_mean *gm = stat;
  gm->prod  *=  pow (x, w);
  gm->n += w;
}


static double
geometric_get (const struct per_var_data *pvd UNUSED, void *stat)
{
  struct geometric_mean *gm = stat;

  return pow (gm->prod, 1.0 / gm->n);
}



static double
sum_get (const struct per_var_data *pvd, void *stat UNUSED)
{
  double n, mean;

  moments1_calculate (pvd->mom, &n, &mean, 0, 0, 0);

  return mean * n;
}


static double
n_get (const struct per_var_data *pvd, void *stat UNUSED)
{
  double n;

  moments1_calculate (pvd->mom, &n, 0, 0, 0, 0);

  return n;
}

static double
arithmean_get (const struct per_var_data *pvd, void *stat UNUSED)
{
  double n, mean;

  moments1_calculate (pvd->mom, &n, &mean, 0, 0, 0);

  return mean;
}

static double
variance_get (const struct per_var_data *pvd, void *stat UNUSED)
{
  double n, mean, variance;

  moments1_calculate (pvd->mom, &n, &mean, &variance, 0, 0);

  return variance;
}


static double
stddev_get (const struct per_var_data *pvd, void *stat)
{
  return sqrt (variance_get (pvd, stat));
}




static double
skew_get (const struct per_var_data *pvd, void *stat UNUSED)
{
  double skew;

  moments1_calculate (pvd->mom, NULL, NULL, NULL, &skew, 0);

  return skew;
}

static double
sekurt_get (const struct per_var_data *pvd, void *stat UNUSED)
{
  double n;

  moments1_calculate (pvd->mom, &n, NULL, NULL, NULL, NULL);

  return calc_sekurt (n);
}

static double
seskew_get (const struct per_var_data *pvd, void *stat UNUSED)
{
  double n;

  moments1_calculate (pvd->mom, &n, NULL, NULL, NULL, NULL);

  return calc_seskew (n);
}

static double
kurt_get (const struct per_var_data *pvd, void *stat UNUSED)
{
  double kurt;

  moments1_calculate (pvd->mom, NULL, NULL, NULL, NULL, &kurt);

  return kurt;
}

static double
semean_get (const struct per_var_data *pvd, void *stat UNUSED)
{
  double n, var;

  moments1_calculate (pvd->mom, &n, NULL, &var, NULL, NULL);

  return sqrt (var / n);
}



static void *
min_create (struct pool *pool)
{
  double *r = pool_alloc (pool, sizeof *r);

  *r = DBL_MAX;

  return r;
}

static void
min_update (void *stat, double w UNUSED, double x)
{
  double *r = stat;

  if (x < *r)
    *r = x;
}

static double
min_get (const struct per_var_data *pvd UNUSED, void *stat)
{
  double *r = stat;

  return *r;
}

static void *
max_create (struct pool *pool)
{
  double *r = pool_alloc (pool, sizeof *r);

  *r = -DBL_MAX;

  return r;
}

static void
max_update (void *stat, double w UNUSED, double x)
{
  double *r = stat;

  if (x > *r)
    *r = x;
}

static double
max_get (const struct per_var_data *pvd UNUSED, void *stat)
{
  double *r = stat;

  return *r;
}



struct range
{
  double min;
  double max;
};

static void *
range_create (struct pool *pool)
{
  struct range *r = pool_alloc (pool, sizeof *r);

  r->min = DBL_MAX;
  r->max = -DBL_MAX;

  return r;
}

static void
range_update (void *stat, double w UNUSED, double x)
{
  struct range *r = stat;

  if (x > r->max)
    r->max = x;

  if (x < r->min)
    r->min = x;
}

static double
range_get (const struct per_var_data *pvd UNUSED, void *stat)
{
  struct range *r = stat;

  return r->max - r->min;
}



static void *
last_create (struct pool *pool)
{
  double *l = pool_alloc (pool, sizeof *l);

  return l;
}

static void
last_update (void *stat, double w UNUSED, double x)
{
  double *l = stat;

  *l = x;
}

static double
last_get (const struct per_var_data *pvd UNUSED, void *stat)
{
  double *l = stat;

  return *l;
}


static void *
first_create (struct pool *pool)
{
  double *f = pool_alloc (pool, sizeof *f);

  *f = SYSMIS;

  return f;
}

static void
first_update (void *stat, double w UNUSED, double x)
{
  double *f = stat;

  if (*f == SYSMIS)
    *f = x;
}

static double
first_get (const struct per_var_data *pvd UNUSED,  void *stat)
{
  double *f = stat;

  return *f;
}

enum 
  {
    MEANS_MEAN = 0,
    MEANS_N,
    MEANS_STDDEV
  };

/* Table of cell_specs */
static const struct cell_spec cell_spec[] = {
  {N_("Mean"), "MEAN", NULL, NULL, arithmean_get},
  {N_("N"), "COUNT", NULL, NULL, n_get},
  {N_("Std. Deviation"), "STDDEV", NULL, NULL, stddev_get},
#if 0
  {N_("Median"), "MEDIAN", NULL, NULL, NULL},
  {N_("Group Median"), "GMEDIAN", NULL, NULL, NULL},
#endif
  {N_("S.E. Mean"), "SEMEAN", NULL, NULL, semean_get},
  {N_("Sum"), "SUM", NULL, NULL, sum_get},
  {N_("Min"), "MIN", min_create, min_update, min_get},
  {N_("Max"), "MAX", max_create, max_update, max_get},
  {N_("Range"), "RANGE", range_create, range_update, range_get},
  {N_("Variance"), "VARIANCE", NULL, NULL, variance_get},
  {N_("Kurtosis"), "KURT", NULL, NULL, kurt_get},
  {N_("S.E. Kurt"), "SEKURT", NULL, NULL, sekurt_get},
  {N_("Skewness"), "SKEW", NULL, NULL, skew_get},
  {N_("S.E. Skew"), "SESKEW", NULL, NULL, seskew_get},
  {N_("First"), "FIRST", first_create, first_update, first_get},
  {N_("Last"), "LAST", last_create, last_update, last_get},
#if 0
  {N_("Percent N"), "NPCT", NULL, NULL, NULL},
  {N_("Percent Sum"), "SPCT", NULL, NULL, NULL},
#endif
  {N_("Harmonic Mean"), "HARMONIC", harmonic_create, harmonic_update, harmonic_get},
  {N_("Geom. Mean"), "GEOMETRIC", geometric_create, geometric_update, geometric_get}
};

#define n_C (sizeof (cell_spec) / sizeof (struct cell_spec))


struct summary
{
  casenumber missing;
  casenumber non_missing;
};


struct layer
{
  size_t n_factor_vars;
  const struct variable **factor_vars;
};

/* The thing parsed after TABLES= */
struct mtable
{
  size_t n_dep_vars;
  const struct variable **dep_vars;

  int n_layers;
  struct layer *layers;

  struct interaction **interactions;
  struct summary *summary;

  int ii;

  struct categoricals *cats;
};

struct means
{
  const struct dictionary *dict;

  struct mtable *table;
  size_t n_tables;

  /* Missing value class for categorical variables */
  enum mv_class exclude;

  /* Missing value class for dependent variables */
  enum mv_class dep_exclude;

  bool listwise_exclude;

  /* an array  indicating which statistics are to be calculated */
  int *cells;

  /* Size of cells */
  int n_cells;

  /* Pool on which cell functions may allocate data */
  struct pool *pool;
};


static void
run_means (struct means *cmd, struct casereader *input,
	   const struct dataset *ds);



static bool
parse_means_table_syntax (struct lexer *lexer, const struct means *cmd, struct mtable *table)
{
  table->ii = 0;
  table->n_layers = 0;
  table->layers = NULL;
  table->interactions = NULL;

  /* Dependent variable (s) */
  if (!parse_variables_const_pool (lexer, cmd->pool, cmd->dict,
				   &table->dep_vars, &table->n_dep_vars,
				   PV_NO_DUPLICATE | PV_NUMERIC))
    return false;

  /* Factor variable (s) */
  while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
    {
      if (lex_match (lexer, T_BY))
	{
	  table->n_layers++;
          table->layers = 
	    pool_realloc (cmd->pool, table->layers, 
			  sizeof (*table->layers) * table->n_layers);

	  if (!parse_variables_const_pool 
              (lexer, cmd->pool, cmd->dict,
               &table->layers[table->n_layers - 1].factor_vars,
               &table->layers[table->n_layers - 1].n_factor_vars,
               PV_NO_DUPLICATE))
	    return false;

	}
    }

  /* There is always at least one layer.
     However the final layer is the total, and not
     normally considered by the user as a 
     layer.
  */

  table->n_layers++;
  table->layers = 
    pool_realloc (cmd->pool, table->layers, 
		  sizeof (*table->layers) * table->n_layers);
  table->layers[table->n_layers - 1].factor_vars = NULL;
  table->layers[table->n_layers - 1].n_factor_vars = 0;

  return true;
}

/* Match a variable.
   If the match succeeds, the variable will be placed in VAR.
   Returns true if successful */
static bool
lex_is_variable (struct lexer *lexer, const struct dictionary *dict,
		 int n)
{
  const char *tstr;
  if (lex_next_token (lexer, n) !=  T_ID)
    return false;

  tstr = lex_next_tokcstr (lexer, n);

  if (NULL == dict_lookup_var (dict, tstr) )
    return false;

  return true;
}


int
cmd_means (struct lexer *lexer, struct dataset *ds)
{
  int t;
  int i;
  int l;
  struct means means;
  bool more_tables = true;

  means.pool = pool_create ();

  means.exclude = MV_ANY;
  means.dep_exclude = MV_ANY;
  means.listwise_exclude = false;
  means.table = NULL;
  means.n_tables = 0;

  means.dict = dataset_dict (ds);

  means.n_cells = 3;
  means.cells = pool_calloc (means.pool, means.n_cells, sizeof (*means.cells));


  /* The first three items (MEAN, COUNT, STDDEV) are the default */
  for (i = 0; i < 3; ++i)
    means.cells[i] = i;


  /*   Optional TABLES =   */
  if (lex_match_id (lexer, "TABLES"))
    {
      lex_force_match (lexer, T_EQUALS);
    }


  more_tables = true;
  /* Parse the "tables" */
  while (more_tables)
    {
      means.n_tables ++;
      means.table = pool_realloc (means.pool, means.table, means.n_tables * sizeof (*means.table));

      if (! parse_means_table_syntax (lexer, &means, 
				      &means.table[means.n_tables - 1]))
	{
	  goto error;
	}

      /* Look ahead to see if there are more tables to be parsed */
      more_tables = false;
      if ( T_SLASH == lex_next_token (lexer, 0) )
	{
	  if (lex_is_variable (lexer, means.dict, 1) )
	    {
	      more_tables = true;
	      lex_force_match (lexer, T_SLASH);
	    }
	}
    }

  /* /MISSING subcommand */
  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "MISSING"))
	{
	  /*
	    If no MISSING subcommand is specified, each combination of
	    a dependent variable and categorical variables is handled
	    separately.
	  */
	  lex_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "INCLUDE"))
	    {
	      /*
		Use the subcommand  "/MISSING=INCLUDE" to include user-missing
		values in the analysis.
	      */

	      means.exclude = MV_SYSTEM;
	      means.dep_exclude = MV_SYSTEM;
	    }
	  else if (lex_match_id (lexer, "TABLE"))
	    /*
	      This is the default. (I think).
	      Every case containing a complete set of variables for a given
	      table. If any variable, categorical or dependent for in a table
	      is missing (as defined by what?), then that variable will
	      be dropped FOR THAT TABLE ONLY.
	    */
	    {
	      means.listwise_exclude = true;
	    }
	  else if (lex_match_id (lexer, "DEPENDENT"))
	    /*
	      Use the command "/MISSING=DEPENDENT" to
	      include user-missing values for the categorical variables, 
	      while excluding them for the dependent variables.

	      Cases are dropped only when user-missing values
	      appear in dependent  variables.  User-missing
	      values for categorical variables are treated according to
	      their face value.

	      Cases are ALWAYS dropped when System Missing values appear 
	      in the categorical variables.
	    */
	    {
	      means.dep_exclude = MV_ANY;
	      means.exclude = MV_SYSTEM;
	    }
	  else
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "CELLS"))
	{
	  lex_match (lexer, T_EQUALS);

	  /* The default values become overwritten */
	  means.n_cells = 0;
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      int k = 0;
	      if (lex_match (lexer, T_ALL))
		{
		  int x;
		  means.cells =
		    pool_realloc (means.pool, means.cells,
				  (means.n_cells += n_C) * sizeof (*means.cells));

		  for (x = 0; x < n_C; ++x)
		    means.cells[means.n_cells - (n_C - 1 - x) - 1] = x;
		}
	      else if (lex_match_id (lexer, "NONE"))
		{
		  /* Do nothing */
		}
	      else if (lex_match_id (lexer, "DEFAULT"))
		{
		  means.cells =
		    pool_realloc (means.pool, means.cells,
				  (means.n_cells += 3) * sizeof (*means.cells));
		  
		  means.cells[means.n_cells - 2 - 1] = MEANS_MEAN;
		  means.cells[means.n_cells - 1 - 1] = MEANS_N;
		  means.cells[means.n_cells - 0 - 1] = MEANS_STDDEV;
		}
	      else
		{
		  for (; k < n_C; ++k)
		    {
		      if (lex_match_id (lexer, cell_spec[k].keyword))
			{
			  means.cells =
			    pool_realloc (means.pool, means.cells,
					  ++means.n_cells * sizeof (*means.cells));
			
			  means.cells[means.n_cells - 1] = k;
			  break;
			}
		    }
		}
	      if (k >= n_C)
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }



  for (t = 0; t < means.n_tables; ++t)
    {
      struct mtable *table = &means.table[t];

      table->interactions =
	pool_calloc (means.pool, table->n_layers, sizeof (*table->interactions));

      table->summary =
	pool_calloc (means.pool, table->n_dep_vars * table->n_layers, sizeof (*table->summary));

      for (l = 0; l < table->n_layers; ++l)
	{
	  int v;
	  const struct layer *lyr = &table->layers[l];
	  const int n_vars = lyr->n_factor_vars;
	  table->interactions[l] = interaction_create (NULL);
	  for (v = 0; v < n_vars ; ++v)
	    {
	      interaction_add_variable (table->interactions[l],
					lyr->factor_vars[v]);
	    }
	}
    }

  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;

    grouper = casegrouper_create_splits (proc_open (ds), means.dict);
    while (casegrouper_get_next_group (grouper, &group))
      {
	run_means (&means, group, ds);
      }
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  for (t = 0; t < means.n_tables; ++t)
    {
      int l;
      struct mtable *table = &means.table[t];
      if (table->interactions)
	for (l = 0; l < table->n_layers; ++l)
	  {
	    interaction_destroy (table->interactions[l]);
	  }
    }

  pool_destroy (means.pool);
  return CMD_SUCCESS;

 error:

  for (t = 0; t < means.n_tables; ++t)
    {
      int l;
      struct mtable *table = &means.table[t];
      if (table->interactions)
	for (l = 0; l < table->n_layers; ++l)
	  {
	    interaction_destroy (table->interactions[l]);
	  }  
    }
  
  pool_destroy (means.pool);
  return CMD_FAILURE;
}


static bool
is_missing (const struct means *cmd,
	    const struct variable *dvar,
	    const struct interaction *iact,
	    const struct ccase *c)
{
  if ( interaction_case_is_missing (iact, c, cmd->exclude) )
    return true;


  if (var_is_value_missing (dvar,
			    case_data (c, dvar),
			    cmd->dep_exclude))
    return true;

  return false;
}

static void output_case_processing_summary (const struct mtable *);

static void output_report (const struct means *, int, const struct mtable *);


struct per_cat_data
{
  struct per_var_data *pvd;

  bool warn;
};


static void 
destroy_n (const void *aux1 UNUSED, void *aux2, void *user_data)
{
  struct mtable *table = aux2;
  int v;
  struct per_cat_data *per_cat_data = user_data;
  struct per_var_data *pvd = per_cat_data->pvd;

  for (v = 0; v < table->n_dep_vars; ++v)
    {
      struct per_var_data *pp = &pvd[v];
      moments1_destroy (pp->mom);
    }
}

static void *
create_n (const void *aux1, void *aux2)
{
  int i, v;
  const struct means *means = aux1;
  struct mtable *table = aux2;
  struct per_cat_data *per_cat_data = pool_malloc (means->pool, sizeof *per_cat_data);

  struct per_var_data *pvd = pool_calloc (means->pool, table->n_dep_vars, sizeof *pvd);

  for (v = 0; v < table->n_dep_vars; ++v)
    {
      enum moment maxmom = MOMENT_KURTOSIS;
      struct per_var_data *pp = &pvd[v];

      pp->cell_stats = pool_calloc (means->pool, means->n_cells, sizeof *pp->cell_stats);
      

      for (i = 0; i < means->n_cells; ++i)
	{
	  int csi = means->cells[i];
	  const struct cell_spec *cs = &cell_spec[csi];
	  if (cs->sc)
	    {
	      pp->cell_stats[i] = cs->sc (means->pool);
	    }
	}
      pp->mom = moments1_create (maxmom);
    }


  per_cat_data->pvd = pvd;
  per_cat_data->warn = true;
  return per_cat_data;
}

static void
update_n (const void *aux1, void *aux2, void *user_data, const struct ccase *c, double weight)
{
  int i;
  int v = 0;
  const struct means *means = aux1;
  struct mtable *table = aux2;
  struct per_cat_data *per_cat_data = user_data;

  for (v = 0; v < table->n_dep_vars; ++v)
    {
      struct per_var_data *pvd = &per_cat_data->pvd[v];

      const double x = case_data (c, table->dep_vars[v])->f;

      for (i = 0; i < table->n_layers; ++i)
	{
	  if ( is_missing (means, table->dep_vars[v],
			   table->interactions[i], c))
	    goto end;
	}

      for (i = 0; i < means->n_cells; ++i)
	{
	  const int csi = means->cells[i];
	  const struct cell_spec *cs = &cell_spec[csi];


	  if (cs->su)
	    cs->su (pvd->cell_stats[i],
		    weight, x);
	}

      moments1_add (pvd->mom, x, weight);

    end:
      continue;
    }
}

static void
calculate_n (const void *aux1, void *aux2, void *user_data)
{
  int i;
  int v = 0;
  struct per_cat_data *per_cat_data = user_data;
  const struct means *means = aux1;
  struct mtable *table = aux2;

  for (v = 0; v < table->n_dep_vars; ++v)
    {
      struct per_var_data *pvd = &per_cat_data->pvd[v];
      for (i = 0; i < means->n_cells; ++i)
	{
	  int csi = means->cells[i];
	  const struct cell_spec *cs = &cell_spec[csi];

	  if (cs->su)
	    cs->sd (pvd, pvd->cell_stats[i]);
	}
    }
}

static void
run_means (struct means *cmd, struct casereader *input,
	   const struct dataset *ds UNUSED)
{
  int t;
  const struct variable *wv = dict_get_weight (cmd->dict);
  struct ccase *c;
  struct casereader *reader;

  struct payload payload;
  payload.create = create_n;
  payload.update = update_n;
  payload.calculate = calculate_n;
  payload.destroy = destroy_n;
  
  for (t = 0; t < cmd->n_tables; ++t)
    {
      struct mtable *table = &cmd->table[t];
      table->cats
	= categoricals_create (table->interactions,
			       table->n_layers, wv, cmd->dep_exclude, cmd->exclude);

      categoricals_set_payload (table->cats, &payload, cmd, table);
    }

  for (reader = input;
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      for (t = 0; t < cmd->n_tables; ++t)
	{
	  bool something_missing = false;
	  int  v;
	  struct mtable *table = &cmd->table[t];

	  for (v = 0; v < table->n_dep_vars; ++v)
	    {
	      int i;
	      for (i = 0; i < table->n_layers; ++i)
		{
		  const bool missing =
		    is_missing (cmd, table->dep_vars[v],
				table->interactions[i], c);
		  if (missing)
		    {
		      something_missing = true;
		      table->summary[v * table->n_layers + i].missing++;
		    }
		  else
		    table->summary[v * table->n_layers  + i].non_missing++;
		}
	    }
	  if ( something_missing && cmd->listwise_exclude)
	    continue;

	  categoricals_update (table->cats, c);
	}
    }
  casereader_destroy (reader);

  for (t = 0; t < cmd->n_tables; ++t)
    {
      struct mtable *table = &cmd->table[t];

      categoricals_done (table->cats);
    }


  for (t = 0; t < cmd->n_tables; ++t)
    {
      int i;
      const struct mtable *table = &cmd->table[t];

      output_case_processing_summary (table);

      for (i = 0; i < table->n_layers; ++i)
	{
	  output_report (cmd, i, table);
	}
      categoricals_destroy (table->cats);
    }

}



static void
output_case_processing_summary (const struct mtable *table)
{
  int i, v;
  const int heading_columns = 1;
  const int heading_rows = 3;
  struct tab_table *t;

  const int nr = heading_rows + table->n_layers * table->n_dep_vars;
  const int nc = 7;

  t = tab_create (nc, nr);
  tab_title (t, _("Case Processing Summary"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, nc - 1, nr - 1);

  tab_hline (t, TAL_2, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);


  tab_joint_text (t, heading_columns, 0,
		  nc - 1, 0, TAB_CENTER | TAT_TITLE, _("Cases"));

  tab_joint_text (t, 1, 1, 2, 1, TAB_CENTER | TAT_TITLE, _("Included"));
  tab_joint_text (t, 3, 1, 4, 1, TAB_CENTER | TAT_TITLE, _("Excluded"));
  tab_joint_text (t, 5, 1, 6, 1, TAB_CENTER | TAT_TITLE, _("Total"));

  tab_hline (t, TAL_1, heading_columns, nc - 1, 1);
  tab_hline (t, TAL_1, heading_columns, nc - 1, 2);


  for (i = 0; i < 3; ++i)
    {
      tab_text (t, heading_columns + i * 2, 2, TAB_CENTER | TAT_TITLE,
		_("N"));
      tab_text (t, heading_columns + i * 2 + 1, 2, TAB_CENTER | TAT_TITLE,
		_("Percent"));
    }

  for (v = 0; v < table->n_dep_vars; ++v)
    {
      const struct variable *var = table->dep_vars[v];
      const char *dv_name = var_to_string (var);
      for (i = 0; i < table->n_layers; ++i)
	{
	  const int row = v * table->n_layers + i;
	  const struct interaction *iact = table->interactions[i];
	  casenumber n_total;

	  struct string str;
	  ds_init_cstr (&str, dv_name);
	  ds_put_cstr (&str, ": ");

	  interaction_to_string (iact, &str);

	  tab_text (t, 0, row + heading_rows,
		    TAB_LEFT | TAT_TITLE, ds_cstr (&str));


	  n_total = table->summary[row].missing + 
	    table->summary[row].non_missing;

	  tab_double (t, 1, row + heading_rows,
		      0, table->summary[row].non_missing, NULL, RC_INTEGER);

	  tab_text_format (t, 2, row + heading_rows,
			   0, _("%g%%"), 
			   table->summary[row].non_missing / (double) n_total * 100.0);


	  tab_double (t, 3, row + heading_rows,
		      0, table->summary[row].missing, NULL, RC_INTEGER);


	  tab_text_format (t, 4, row + heading_rows,
			   0, _("%g%%"), 
			   table->summary[row].missing / (double) n_total * 100.0);


	  tab_double (t, 5, row + heading_rows,
		      0, table->summary[row].missing + 
		      table->summary[row].non_missing, NULL, RC_INTEGER);

	  tab_text_format (t, 6, row + heading_rows,
			   0, _("%g%%"), 
			   n_total / (double) n_total * 100.0);


	  ds_destroy (&str);
	}
    }

  tab_submit (t);
}


static void
output_report (const struct means *cmd,  int iact_idx,
	       const struct mtable *table)
{
  int grp;
  int i;

  const struct interaction *iact = table->interactions[iact_idx];

  const int heading_columns = 1 + iact->n_vars;
  const int heading_rows = 1;
  struct tab_table *t;

  const int n_cats = categoricals_n_count (table->cats, iact_idx);

  const int nr = n_cats * table->n_dep_vars + heading_rows;

  const int nc = heading_columns + cmd->n_cells;

  t = tab_create (nc, nr);
  tab_title (t, _("Report"));

  tab_headers (t, heading_columns, 0, heading_rows, 0);

  tab_box (t, TAL_2, TAL_2, -1, TAL_1, 0, 0, nc - 1, nr - 1);

  tab_hline (t, TAL_2, 0, nc - 1, heading_rows);
  tab_vline (t, TAL_2, heading_columns, 0, nr - 1);

  for (i = 0; i < iact->n_vars; ++i)
    {
      tab_text (t, 1 + i, 0, TAB_CENTER | TAT_TITLE,
		var_to_string (iact->vars[i]));
    }

  for (i = 0; i < cmd->n_cells; ++i)
    {
      tab_text (t, heading_columns + i, 0,
		TAB_CENTER | TAT_TITLE,
		gettext (cell_spec[cmd->cells[i]].title));
    }


  for (i = 0; i < n_cats; ++i)
    {
      int v, dv;
      const struct ccase *c =
	categoricals_get_case_by_category_real (table->cats, iact_idx, i);

      for (dv = 0; dv < table->n_dep_vars; ++dv)
	{
	  tab_text (t, 0,
		    heading_rows + dv * n_cats,
		    TAB_RIGHT | TAT_TITLE,
		    var_to_string (table->dep_vars[dv])
		    );

	  if ( dv > 0)
	    tab_hline (t, TAL_1, 0, nc - 1, heading_rows + dv * n_cats);

	  for (v = 0; v < iact->n_vars; ++v)
	    {
	      const struct variable *var = iact->vars[v];
	      const union value *val = case_data (c, var);
	      struct string str;
	      ds_init_empty (&str);
	      var_append_value_name (var, val, &str);

	      tab_text (t, 1 + v, heading_rows + dv * n_cats + i,
			TAB_RIGHT | TAT_TITLE, ds_cstr (&str));

	      ds_destroy (&str);
	    }
	}
    }

  for (grp = 0; grp < n_cats; ++grp)
    {
      int dv;
      struct per_cat_data *per_cat_data =
	categoricals_get_user_data_by_category_real (table->cats, iact_idx, grp);

      for (dv = 0; dv < table->n_dep_vars; ++dv)
	{
	  const struct per_var_data *pvd = &per_cat_data->pvd[dv];
	  for (i = 0; i < cmd->n_cells; ++i)
	    {
	      const int csi = cmd->cells[i];
	      const struct cell_spec *cs = &cell_spec[csi];

	      double result = cs->sd (pvd, pvd->cell_stats[i]);

	      tab_double (t, heading_columns + i,
			  heading_rows + grp + dv * n_cats,
			  0, result, NULL, RC_OTHER);
	    }
	}
    }

  tab_submit (t);
}
