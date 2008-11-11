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
#include <stdio.h>
#include <libpspp/compiler.h>
#include "chart.h"


struct chart *
chart_create(void)
{
  return NULL;
}

void
chart_submit(struct chart *chart UNUSED)
{
}

void
chart_init_separate (struct chart *ch UNUSED, const char *type UNUSED,
                     const char *file_name_tmpl UNUSED, int number UNUSED)
{
}

void
chart_finalise_separate (struct chart *ch UNUSED)
{
}
