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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include "glob.h"
#include <time.h>
#include "str.h"
#include "strftime.h"

/* var.h */
struct dictionary *default_dict;
struct expression *process_if_expr;

struct transformation *t_trns;
size_t n_trns, m_trns, f_trns;

int FILTER_before_TEMPORARY;

struct file_handle *default_handle;

/* Functions. */

static void
get_cur_date (char cur_date[12])
{
  time_t now = time (NULL);

  if (now != (time_t) -1) 
    {
      struct tm *tm = localtime (&now);
      if (tm != NULL) 
        {
          strftime (cur_date, 12, "%d %b %Y", tm);
          return;
        }
    }
  strcpy (cur_date, "?? ??? 2???");
}

const char *
get_start_date (void)
{
  static char start_date[12];

  if (start_date[0] == '\0')
    get_cur_date (start_date);
  return start_date; 
}
