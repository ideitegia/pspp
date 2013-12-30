/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

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

#include <gsl/gsl_matrix.h>
#include <gsl/gsl_permutation.h>
#include <gsl/gsl_sort_vector.h>
#include <gsl/gsl_statistics.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "math/random.h"
#include "output/tab.h"
#include "output/text-item.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum missing_type
  {
    MISS_LISTWISE,
    MISS_PAIRWISE,
  };


struct qc
{
  const struct variable **vars;
  size_t n_vars;

  int ngroups;			/* Number of group. (Given by the user) */
  int maxiter;			/* Maximum iterations (Given by the user) */

  const struct variable *wv;	/* Weighting variable. */

  enum missing_type missing_type;
  enum mv_class exclude;
};

/* Holds all of the information for the functions.  int n, holds the number of
   observation and its default value is -1.  We set it in
   kmeans_recalculate_centers in first invocation. */
struct Kmeans
{
  gsl_matrix *centers;		/* Centers for groups. */
  gsl_vector_long *num_elements_groups;

  casenumber n;			/* Number of observations (default -1). */

  int lastiter;			/* Iteration where it found the solution. */
  int trials;			/* If not convergence, how many times has
                                   clustering done. */
  gsl_matrix *initial_centers;	/* Initial random centers. */

  gsl_permutation *group_order;	/* Group order for reporting. */
  struct caseproto *proto;
  struct casereader *index_rdr;	/* Group ids for each case. */
};

static struct Kmeans *kmeans_create (const struct qc *qc);

static void kmeans_randomize_centers (struct Kmeans *kmeans, const struct qc *qc);

static int kmeans_get_nearest_group (struct Kmeans *kmeans, struct ccase *c, const struct qc *);

static void kmeans_recalculate_centers (struct Kmeans *kmeans, const struct casereader *reader, const struct qc *);

static int
kmeans_calculate_indexes_and_check_convergence (struct Kmeans *kmeans, const struct casereader *reader, const struct qc *);

static void kmeans_order_groups (struct Kmeans *kmeans, const struct qc *);

static void kmeans_cluster (struct Kmeans *kmeans, struct casereader *reader, const struct qc *);

static void quick_cluster_show_centers (struct Kmeans *kmeans, bool initial, const struct qc *);

static void quick_cluster_show_number_cases (struct Kmeans *kmeans, const struct qc *);

static void quick_cluster_show_results (struct Kmeans *kmeans, const struct qc *);

int cmd_quick_cluster (struct lexer *lexer, struct dataset *ds);

static void kmeans_destroy (struct Kmeans *kmeans);

/* Creates and returns a struct of Kmeans with given casereader 'cs', parsed
   variables 'variables', number of cases 'n', number of variables 'm', number
   of clusters and amount of maximum iterations. */
static struct Kmeans *
kmeans_create (const struct qc *qc)
{
  struct Kmeans *kmeans = xmalloc (sizeof (struct Kmeans));
  kmeans->centers = gsl_matrix_alloc (qc->ngroups, qc->n_vars);
  kmeans->num_elements_groups = gsl_vector_long_alloc (qc->ngroups);
  kmeans->n = 0;
  kmeans->lastiter = 0;
  kmeans->trials = 0;
  kmeans->group_order = gsl_permutation_alloc (kmeans->centers->size1);
  kmeans->initial_centers = NULL;

  kmeans->proto = caseproto_create ();
  kmeans->proto = caseproto_add_width (kmeans->proto, 0);
  kmeans->index_rdr = NULL;
  return (kmeans);
}

static void
kmeans_destroy (struct Kmeans *kmeans)
{
  gsl_matrix_free (kmeans->centers);
  gsl_matrix_free (kmeans->initial_centers);

  gsl_vector_long_free (kmeans->num_elements_groups);

  gsl_permutation_free (kmeans->group_order);

  caseproto_unref (kmeans->proto);

  casereader_destroy (kmeans->index_rdr);

  free (kmeans);
}

/* Creates random centers using randomly selected cases from the data. */
static void
kmeans_randomize_centers (struct Kmeans *kmeans, const struct qc *qc)
{
  int i, j;
  for (i = 0; i < qc->ngroups; i++)
    {
      for (j = 0; j < qc->n_vars; j++)
	{
	  if (i == j)
	    {
	      gsl_matrix_set (kmeans->centers, i, j, 1);
	    }
	  else
	    {
	      gsl_matrix_set (kmeans->centers, i, j, 0);
	    }
	}
    }
  /* If it is the first iteration, the variable kmeans->initial_centers is NULL
     and it is created once for reporting issues. In SPSS, initial centers are
     shown in the reports but in PSPP it is not shown now. I am leaving it
     here. */
  if (!kmeans->initial_centers)
    {
      kmeans->initial_centers = gsl_matrix_alloc (qc->ngroups, qc->n_vars);
      gsl_matrix_memcpy (kmeans->initial_centers, kmeans->centers);
    }
}

static int
kmeans_get_nearest_group (struct Kmeans *kmeans, struct ccase *c, const struct qc *qc)
{
  int result = -1;
  int i, j;
  double mindist = INFINITY;
  for (i = 0; i < qc->ngroups; i++)
    {
      double dist = 0;
      for (j = 0; j < qc->n_vars; j++)
	{
	  const union value *val = case_data (c, qc->vars[j]);
	  if ( var_is_value_missing (qc->vars[j], val, qc->exclude))
	    continue;

	  dist += pow2 (gsl_matrix_get (kmeans->centers, i, j) - val->f);
	}
      if (dist < mindist)
	{
	  mindist = dist;
	  result = i;
	}
    }
  return (result);
}

/* Re-calculate the cluster centers. */
static void
kmeans_recalculate_centers (struct Kmeans *kmeans, const struct casereader *reader, const struct qc *qc)
{
  casenumber i = 0;
  int v, j;
  struct ccase *c;

  struct casereader *cs = casereader_clone (reader);
  struct casereader *cs_index = casereader_clone (kmeans->index_rdr);

  gsl_matrix_set_all (kmeans->centers, 0.0);
  for (; (c = casereader_read (cs)) != NULL; case_unref (c))
    {
      double weight = qc->wv ? case_data (c, qc->wv)->f : 1.0;
      struct ccase *c_index = casereader_read (cs_index);
      int index = case_data_idx (c_index, 0)->f;
      for (v = 0; v < qc->n_vars; ++v)
	{
	  const union value *val = case_data (c, qc->vars[v]);
	  double x = val->f * weight;
	  double curval;

	  if ( var_is_value_missing (qc->vars[v], val, qc->exclude))
	    continue;

	  curval = gsl_matrix_get (kmeans->centers, index, v);
	  gsl_matrix_set (kmeans->centers, index, v, curval + x);
	}
      i++;
      case_unref (c_index);
    }
  casereader_destroy (cs);
  casereader_destroy (cs_index);

  /* Getting number of cases */
  if (kmeans->n == 0)
    kmeans->n = i;

  /* We got sum of each center but we need averages.
     We are dividing centers to numobs. This may be inefficient and
     we should check it again. */
  for (i = 0; i < qc->ngroups; i++)
    {
      casenumber numobs = kmeans->num_elements_groups->data[i];
      for (j = 0; j < qc->n_vars; j++)
	{
	  if (numobs > 0)
	    {
	      double *x = gsl_matrix_ptr (kmeans->centers, i, j);
	      *x /= numobs;
	    }
	  else
	    {
	      gsl_matrix_set (kmeans->centers, i, j, 0);
	    }
	}
    }
}

/* The variable index in struct Kmeans holds integer values that represents the
   current groups of cases.  index[n]=a shows the nth case is belong to ath
   cluster.  This function calculates these indexes and returns the number of
   different cases of the new and old index variables.  If last two index
   variables are equal, there is no any enhancement of clustering. */
static int
kmeans_calculate_indexes_and_check_convergence (struct Kmeans *kmeans, const struct casereader *reader, const struct qc *qc)
{
  int totaldiff = 0;
  struct ccase *c;
  struct casereader *cs = casereader_clone (reader);

  /* A casewriter into which we will write the indexes. */
  struct casewriter *index_wtr = autopaging_writer_create (kmeans->proto);

  gsl_vector_long_set_all (kmeans->num_elements_groups, 0);

  for (; (c = casereader_read (cs)) != NULL; case_unref (c))
    {
      /* A case to hold the new index. */
      struct ccase *index_case_new = case_create (kmeans->proto);
      int bestindex = kmeans_get_nearest_group (kmeans, c, qc);
      double weight = qc->wv ? case_data (c, qc->wv)->f : 1.0;
      assert (bestindex < kmeans->num_elements_groups->size);
      kmeans->num_elements_groups->data[bestindex] += weight;
      if (kmeans->index_rdr)
	{
	  /* A case from which the old index will be read. */
	  struct ccase *index_case_old = NULL;

	  /* Read the case from the index casereader. */
	  index_case_old = casereader_read (kmeans->index_rdr);

	  /* Set totaldiff, using the old_index. */
	  totaldiff += abs (case_data_idx (index_case_old, 0)->f - bestindex);

	  /* We have no use for the old case anymore, so unref it. */
	  case_unref (index_case_old);
	}
      else
	{
	  /* If this is the first run, then assume index is zero. */
	  totaldiff += bestindex;
	}

      /* Set the value of the new inde.x */
      case_data_rw_idx (index_case_new, 0)->f = bestindex;

      /* and write the new index to the casewriter */
      casewriter_write (index_wtr, index_case_new);
    }
  casereader_destroy (cs);
  /* We have now read through the entire index_rdr, so it's of no use
     anymore. */
  casereader_destroy (kmeans->index_rdr);

  /* Convert the writer into a reader, ready for the next iteration to read */
  kmeans->index_rdr = casewriter_make_reader (index_wtr);

  return (totaldiff);
}

static void
kmeans_order_groups (struct Kmeans *kmeans, const struct qc *qc)
{
  gsl_vector *v = gsl_vector_alloc (qc->ngroups);
  gsl_matrix_get_col (v, kmeans->centers, 0);
  gsl_sort_vector_index (kmeans->group_order, v);
  gsl_vector_free (v);
}

/* Main algorithm.
   Does iterations, checks convergency. */
static void
kmeans_cluster (struct Kmeans *kmeans, struct casereader *reader, const struct qc *qc)
{
  int i;
  bool redo;
  int diffs;
  bool show_warning1;

  show_warning1 = true;
cluster:
  redo = false;
  kmeans_randomize_centers (kmeans, qc);
  for (kmeans->lastiter = 0; kmeans->lastiter < qc->maxiter;
       kmeans->lastiter++)
    {
      diffs = kmeans_calculate_indexes_and_check_convergence (kmeans, reader, qc);
      kmeans_recalculate_centers (kmeans, reader, qc);
      if (show_warning1 && qc->ngroups > kmeans->n)
	{
	  msg (MW, _("Number of clusters may not be larger than the number "
                     "of cases."));
	  show_warning1 = false;
	}
      if (diffs == 0)
	break;
    }

  for (i = 0; i < qc->ngroups; i++)
    {
      if (kmeans->num_elements_groups->data[i] == 0)
	{
	  kmeans->trials++;
	  if (kmeans->trials >= 3)
	    break;
	  redo = true;
	  break;
	}
    }
  if (redo)
    goto cluster;

}

/* Reports centers of clusters.
   Initial parameter is optional for future use.
   If initial is true, initial cluster centers are reported.  Otherwise,
   resulted centers are reported. */
static void
quick_cluster_show_centers (struct Kmeans *kmeans, bool initial, const struct qc *qc)
{
  struct tab_table *t;
  int nc, nr, currow;
  int i, j;
  nc = qc->ngroups + 1;
  nr = qc->n_vars + 4;
  t = tab_create (nc, nr);
  tab_headers (t, 0, nc - 1, 0, 1);
  currow = 0;
  if (!initial)
    {
      tab_title (t, _("Final Cluster Centers"));
    }
  else
    {
      tab_title (t, _("Initial Cluster Centers"));
    }
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, nc - 1, nr - 1);
  tab_joint_text (t, 1, 0, nc - 1, 0, TAB_CENTER, _("Cluster"));
  tab_hline (t, TAL_1, 1, nc - 1, 2);
  currow += 2;

  for (i = 0; i < qc->ngroups; i++)
    {
      tab_text_format (t, (i + 1), currow, TAB_CENTER, "%d", (i + 1));
    }
  currow++;
  tab_hline (t, TAL_1, 1, nc - 1, currow);
  currow++;
  for (i = 0; i < qc->n_vars; i++)
    {
      tab_text (t, 0, currow + i, TAB_LEFT,
		var_to_string (qc->vars[i]));
    }

  for (i = 0; i < qc->ngroups; i++)
    {
      for (j = 0; j < qc->n_vars; j++)
	{
	  if (!initial)
	    {
	      tab_double (t, i + 1, j + 4, TAB_CENTER,
			  gsl_matrix_get (kmeans->centers,
					  kmeans->group_order->data[i], j),
			  var_get_print_format (qc->vars[j]));
	    }
	  else
	    {
	      tab_double (t, i + 1, j + 4, TAB_CENTER,
			  gsl_matrix_get (kmeans->initial_centers,
					  kmeans->group_order->data[i], j),
			  var_get_print_format (qc->vars[j]));
	    }
	}
    }
  tab_submit (t);
}

/* Reports number of cases of each single cluster. */
static void
quick_cluster_show_number_cases (struct Kmeans *kmeans, const struct qc *qc)
{
  struct tab_table *t;
  int nc, nr;
  int i, numelem;
  long int total;
  nc = 3;
  nr = qc->ngroups + 1;
  t = tab_create (nc, nr);
  tab_headers (t, 0, nc - 1, 0, 0);
  tab_title (t, _("Number of Cases in each Cluster"));
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, nc - 1, nr - 1);
  tab_text (t, 0, 0, TAB_LEFT, _("Cluster"));

  total = 0;
  for (i = 0; i < qc->ngroups; i++)
    {
      tab_text_format (t, 1, i, TAB_CENTER, "%d", (i + 1));
      numelem =
	kmeans->num_elements_groups->data[kmeans->group_order->data[i]];
      tab_text_format (t, 2, i, TAB_CENTER, "%d", numelem);
      total += numelem;
    }

  tab_text (t, 0, qc->ngroups, TAB_LEFT, _("Valid"));
  tab_text_format (t, 2, qc->ngroups, TAB_LEFT, "%ld", total);
  tab_submit (t);
}

/* Reports. */
static void
quick_cluster_show_results (struct Kmeans *kmeans, const struct qc *qc)
{
  kmeans_order_groups (kmeans, qc);
  /* Uncomment the line below for reporting initial centers. */
  /* quick_cluster_show_centers (kmeans, true); */
  quick_cluster_show_centers (kmeans, false, qc);
  quick_cluster_show_number_cases (kmeans, qc);
}

int
cmd_quick_cluster (struct lexer *lexer, struct dataset *ds)
{
  struct qc qc;
  struct Kmeans *kmeans;
  bool ok;
  const struct dictionary *dict = dataset_dict (ds);
  qc.ngroups = 2;
  qc.maxiter = 2;
  qc.missing_type = MISS_LISTWISE;
  qc.exclude = MV_ANY;

  if (!parse_variables_const (lexer, dict, &qc.vars, &qc.n_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    {
      return (CMD_FAILURE);
    }

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "MISSING"))
	{
	  lex_match (lexer, T_EQUALS);
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "LISTWISE") || lex_match_id (lexer, "DEFAULT"))
		{
		  qc.missing_type = MISS_LISTWISE;
		}
	      else if (lex_match_id (lexer, "PAIRWISE"))
		{
		  qc.missing_type = MISS_PAIRWISE;
		}
	      else if (lex_match_id (lexer, "INCLUDE"))
		{
		  qc.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  qc.exclude = MV_ANY;
		}
	      else
		goto error;
	    }	  
	}
      else if (lex_match_id (lexer, "CRITERIA"))
	{
	  lex_match (lexer, T_EQUALS);
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "CLUSTERS"))
		{
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      lex_force_int (lexer);
		      qc.ngroups = lex_integer (lexer);
		      if (qc.ngroups <= 0)
			{
			  lex_error (lexer, _("The number of clusters must be positive"));
			  goto error;
			}
		      lex_get (lexer);
		      lex_force_match (lexer, T_RPAREN);
		    }
		}
	      else if (lex_match_id (lexer, "MXITER"))
		{
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      lex_force_int (lexer);
		      qc.maxiter = lex_integer (lexer);
		      if (qc.maxiter <= 0)
			{
			  lex_error (lexer, _("The number of iterations must be positive"));
			  goto error;
			}
		      lex_get (lexer);
		      lex_force_match (lexer, T_RPAREN);
		    }
		}
	      else
                goto error;
	    }
	}
    }

  qc.wv = dict_get_weight (dict);

  {
    struct casereader *group;
    struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), dict);

    while (casegrouper_get_next_group (grouper, &group))
      {
	if ( qc.missing_type == MISS_LISTWISE )
	  {
	    group  = casereader_create_filter_missing (group, qc.vars, qc.n_vars,
						     qc.exclude,
						     NULL,  NULL);
	  }

	kmeans = kmeans_create (&qc);
	kmeans_cluster (kmeans, group, &qc);
	quick_cluster_show_results (kmeans, &qc);
	kmeans_destroy (kmeans);
	casereader_destroy (group);
      }
    ok = casegrouper_destroy (grouper);
  }
  ok = proc_commit (ds) && ok;

  free (qc.vars);

  return (ok);

 error:
  free (qc.vars);
  return CMD_FAILURE;
}
