/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2009 Free Software Foundation, Inc.

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
#include <output/chart-provider.h>

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <plot.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/str.h>
#include <output/manager.h>
#include <output/output.h>

#include "error.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

extern struct som_table_class tab_table_class;

void
chart_init (struct chart *chart, const struct chart_class *class)
{
  chart->class = class;
  chart->ref_cnt = 1;
}

void
chart_geometry_init (plPlotter *lp, struct chart_geometry *geom)
{
  /* Start output page. */
  pl_openpl_r (lp);

  /* Set coordinate system. */
  pl_fspace_r (lp, 0.0, 0.0, 1000.0, 1000.0);

  /* Set line thickness. */
  pl_flinewidth_r (lp, 0.25);
  pl_pencolorname_r (lp, "black");

  /* Erase graphics display. */
  pl_erase_r (lp);

  pl_filltype_r (lp, 0);
  pl_savestate_r(lp);

  /* Set default chartetry. */
  geom->data_top = 900;
  geom->data_right = 800;
  geom->data_bottom = 120;
  geom->data_left = 150;
  geom->abscissa_top = 70;
  geom->ordinate_right = 120;
  geom->title_bottom = 920;
  geom->legend_left = 810;
  geom->legend_right = 1000;
  geom->font_size = 0;
  strcpy (geom->fill_colour, "red");

  /* Get default font size */
  if (!geom->font_size)
    geom->font_size = pl_fontsize_r (lp, -1);

  /* Draw the data area */
  pl_box_r (lp,
	    geom->data_left, geom->data_bottom,
	    geom->data_right, geom->data_top);
}

void
chart_geometry_free (plPlotter *lp)
{
  if (pl_closepl_r (lp) < 0)
    fprintf (stderr, "Couldn't close Plotter\n");
}

void
chart_draw (const struct chart *chart, plPlotter *lp)
{
  chart->class->draw (chart, lp);
}

struct chart *
chart_ref (const struct chart *chart_)
{
  struct chart *chart = (struct chart *) chart_;
  chart->ref_cnt++;
  return chart;
}

void
chart_unref (struct chart *chart)
{
  assert (chart->ref_cnt > 0);
  if (--chart->ref_cnt == 0)
    chart->class->destroy (chart);
}

void
chart_submit (struct chart *chart)
{
  struct outp_driver *d;

  for (d = outp_drivers (NULL); d; d = outp_drivers (d))
    if (d->class->output_chart != NULL)
      d->class->output_chart (d, chart);

  chart_unref (chart);
}

bool
chart_create_file (const char *type, const char *file_name_tmpl, int number,
                   plPlotterParams *params, char **file_namep, plPlotter **lpp)
{
  char *file_name = NULL;
  FILE *fp = NULL;
  int number_pos;
  plPlotter *lp;

  number_pos = strchr (file_name_tmpl, '#') - file_name_tmpl;
  file_name = xasprintf ("%.*s%d%s", number_pos, file_name_tmpl,
                         number, file_name_tmpl + number_pos + 1);

  fp = fopen (file_name, "wb");
  if (fp == NULL)
    {
      error (0, errno, _("creating \"%s\""), file_name);
      goto error;
    }

  if (params != NULL)
    lp = pl_newpl_r (type, 0, fp, stderr, params);
  else
    {
      params = pl_newplparams ();
      lp = pl_newpl_r (type, 0, fp, stderr, params);
      pl_deleteplparams (params);
    }
  if (lp == NULL)
    goto error;

  *file_namep = file_name;
  *lpp = lp;
  return true;

error:
  if (fp != NULL)
    {
      fclose (fp);
      if (file_name != NULL)
        unlink (file_name);
    }
  free (file_name);
  *file_namep = NULL;
  *lpp = NULL;
  return false;
}
