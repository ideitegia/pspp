/* PSPP - a program for statistical analysis.
   Copyright (C) 2004 Free Software Foundation, Inc.

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

#include <output/chart.h>

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <plot.h>

#include <libpspp/str.h>
#include <output/manager.h>
#include <output/output.h>

#include "error.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

extern struct som_table_class tab_table_class;

struct chart *
chart_create(void)
{
  struct chart *chart;
  struct outp_driver *d;

  d = outp_drivers (NULL);
  if (d == NULL)
    return NULL;

  chart = xmalloc (sizeof *chart);
  chart->lp = NULL;
  d->class->initialise_chart(d, chart);
  if (!chart->lp)
    {
      free (chart);
      return NULL;
    }

  if (pl_openpl_r (chart->lp) < 0)      /* open Plotter */
    return NULL;

  pl_fspace_r (chart->lp, 0.0, 0.0, 1000.0, 1000.0); /* set coordinate system */
  pl_flinewidth_r (chart->lp, 0.25);    /* set line thickness */
  pl_pencolorname_r (chart->lp, "black");

  pl_erase_r (chart->lp);               /* erase graphics display */
  pl_filltype_r(chart->lp,0);

  pl_savestate_r(chart->lp);

  /* Set default chartetry */
  chart->data_top =   900;
  chart->data_right = 800;
  chart->data_bottom = 120;
  chart->data_left = 150;
  chart->abscissa_top = 70;
  chart->ordinate_right = 120;
  chart->title_bottom = 920;
  chart->legend_left = 810;
  chart->legend_right = 1000;
  chart->font_size = 0;
  chart->in_path = false;
  chart->dataset = NULL;
  chart->n_datasets = 0;
  strcpy(chart->fill_colour,"red");

  /* Get default font size */
  if ( !chart->font_size)
    chart->font_size = pl_fontsize_r(chart->lp, -1);

  /* Draw the data area */
  pl_box_r(chart->lp,
	   chart->data_left, chart->data_bottom,
	   chart->data_right, chart->data_top);

  return chart;
}

void
chart_submit(struct chart *chart)
{
  int i;
  struct som_entity s;
  struct outp_driver *d;

  if ( ! chart )
     return ;

  pl_restorestate_r(chart->lp);

  s.class = &tab_table_class;
  s.ext = chart;
  s.type = SOM_CHART;
  som_submit (&s);

  if (pl_closepl_r (chart->lp) < 0)     /* close Plotter */
    {
      fprintf (stderr, "Couldn't close Plotter\n");
    }

  pl_deletepl_r(chart->lp);

  pl_deleteplparams(chart->pl_params);

  d = outp_drivers (NULL);
  d->class->finalise_chart(d, chart);

  for (i = 0 ; i < chart->n_datasets; ++i)
    free (chart->dataset[i]);
  free (chart->dataset);

  free(chart);
}

void
chart_init_separate (struct chart *ch, const char *type,
                     const char *file_name_tmpl, int number)
{
  FILE *fp;
  int number_pos;

  number_pos = strchr (file_name_tmpl, '#') - file_name_tmpl;
  ch->file_name = xasprintf ("%.*s%d%s",
                             number_pos, file_name_tmpl,
                             number,
                             file_name_tmpl + number_pos + 1);
  fp = fopen (ch->file_name, "wb");
  if (fp == NULL)
    {
      error (0, errno, _("creating \"%s\""), ch->file_name);
      free (ch->file_name);
      ch->file_name = NULL;
      return;
    }

  ch->pl_params = pl_newplparams ();
  ch->lp = pl_newpl_r (type, 0, fp, stderr, ch->pl_params);
}

void
chart_finalise_separate (struct chart *ch)
{
  free (ch->file_name);
}
