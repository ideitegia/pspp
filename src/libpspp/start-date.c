/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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
#include "start-date.h"
#include <time.h>
#include "str.h"
#include "strftime.h"

/* Writes the current date into CUR_DATE in the format DD MMM
   YYYY. */
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

/* Returns the date at which PSPP was started, as a string in the
   format DD MMM YYYY. */
const char *
get_start_date (void)
{
  static char start_date[12];
  if (start_date[0] == '\0')
    get_cur_date (start_date);
  return start_date;
}
