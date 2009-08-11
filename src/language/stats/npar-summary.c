/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009 Free Software Foundation, Inc.

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

#include <data/format.h>
#include <output/table.h>
#include <data/casereader.h>
#include <libpspp/hash.h>
#include <data/variable.h>
#include "npar-summary.h"
#include <math/moments.h>
#include <data/case.h>
#include <data/dictionary.h>
#include <math.h>
#include <minmax.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)


void
npar_summary_calc_descriptives (struct descriptives *desc,
				struct casereader *input,
				const struct dictionary *dict,
				const struct variable *const *vv,
				int n_vars UNUSED,
                                enum mv_class filter)
{
  int i = 0;
  while (*vv)
    {
      double minimum = DBL_MAX;
      double maximum = -DBL_MAX;
      double var;
      struct moments1 *moments = moments1_create (MOMENT_VARIANCE);
      struct ccase *c;
      const struct variable *v = *vv++;
      struct casereader *pass;

      pass = casereader_clone (input);
      pass = casereader_create_filter_missing (pass,
                                               &v, 1,
                                               filter, NULL, NULL);
      pass = casereader_create_filter_weight (pass, dict, NULL, NULL);
      while ((c = casereader_read (pass)) != NULL)
	{
          double val = case_num (c, v);
          double w = dict_get_case_weight (dict, c, NULL);
          minimum = MIN (minimum, val);
          maximum = MAX (maximum, val);
          moments1_add (moments, val, w);
	  case_unref (c);
	}
      casereader_destroy (pass);

      moments1_calculate (moments,
			  &desc[i].n,
			  &desc[i].mean,
			  &var,
			  NULL, NULL);

      desc[i].std_dev = sqrt (var);

      moments1_destroy (moments);

      desc[i].min = minimum;
      desc[i].max = maximum;

      i++;
    }
  casereader_destroy (input);
}



void
do_summary_box (const struct descriptives *desc,
		const struct variable *const *vv,
		int n_vars)
{
  int v;
  bool quartiles = false;

  int col;
  int columns = 1 ;
  struct tab_table *table ;

  if ( desc ) columns += 5;
  if ( quartiles ) columns += 3;

  table = tab_create (columns, 2 + n_vars);

  tab_dim (table, tab_natural_dimensions, NULL, NULL);

  tab_title (table, _("Descriptive Statistics"));

  tab_headers (table, 1, 0, 1, 0);

  tab_box (table, TAL_1, TAL_1, -1, TAL_1,
	   0, 0, tab_nc (table) - 1, tab_nr(table) - 1 );

  tab_hline (table, TAL_2, 0, tab_nc (table) -1, 2);
  tab_vline (table, TAL_2, 1, 0, tab_nr (table) - 1);

  col = 1;
  if ( desc )
    {
      tab_joint_text (table, col, 0, col, 1, TAT_TITLE | TAB_CENTER,
		      _("N"));
      col++;
      tab_joint_text (table, col, 0, col, 1, TAT_TITLE | TAB_CENTER,
		      _("Mean"));
      col++;
      tab_joint_text (table, col, 0, col, 1, TAT_TITLE | TAB_CENTER,
		      _("Std. Deviation"));
      col++;
      tab_joint_text (table, col, 0, col, 1, TAT_TITLE | TAB_CENTER,
		      _("Minimum"));
      col++;
      tab_joint_text (table, col, 0, col, 1, TAT_TITLE | TAB_CENTER,
		      _("Maximum"));
      col++;
    }

  if ( quartiles )
    {
      tab_joint_text (table, col, 0, col + 2, 0, TAT_TITLE | TAB_CENTER,
		      _("Percentiles"));
      tab_hline (table, TAL_1, col, col + 2, 1);

      tab_text (table, col, 1, TAT_TITLE | TAB_CENTER,
		_("25th"));
      col++;
      tab_text (table, col, 1, TAT_TITLE | TAB_CENTER,
		_("50th (Median)"));
      col++;
      tab_text (table, col, 1, TAT_TITLE | TAB_CENTER,
		_("75th"));
      col++;
    }


  for ( v = 0 ; v < n_vars ; ++v )
    {
      const struct variable *var = vv[v];
      const struct fmt_spec *fmt = var_get_print_format (var);

      tab_text (table, 0, 2 + v, TAT_NONE, var_to_string (var));

      tab_double (table, 1, 2 + v, TAT_NONE, desc[v].n, fmt);
      tab_double (table, 2, 2 + v, TAT_NONE, desc[v].mean, fmt);
      tab_double (table, 3, 2 + v, TAT_NONE, desc[v].std_dev, fmt);
      tab_double (table, 4, 2 + v, TAT_NONE, desc[v].min, fmt);
      tab_double (table, 5, 2 + v, TAT_NONE, desc[v].max, fmt);
    }


  tab_submit (table);
}
