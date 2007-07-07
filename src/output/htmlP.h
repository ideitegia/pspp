/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#if !htmlP_h
#define htmlP_h 1

#include <data/file-name.h>

/* HTML output driver extension record. */
struct html_driver_ext
  {
    char *file_name;
    char *chart_file_name;
    FILE *file;

    size_t chart_cnt;
  };

extern const struct outp_class html_class;

struct outp_driver;
void html_put_cell_contents (struct outp_driver *this,
                             unsigned int opts, struct substring text);

#endif /* !htmlP_h */
