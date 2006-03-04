/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

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
#include <stdio.h>
#include <stdlib.h>
#include <plot.h>
#include <stdarg.h>
#include <string.h>
#include <float.h>
#include <assert.h>
#include <math.h>

#include "chart.h"
#include "str.h"
#include "alloc.h"
#include "manager.h"
#include "output.h"

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
  free(chart);
}

