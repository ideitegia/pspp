/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2014 Free Software Foundation, Inc.

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

#ifndef OUTPUT_RENDER_H
#define OUTPUT_RENDER_H 1

#include <stdbool.h>
#include <stddef.h>
#include "output/table-provider.h"

struct table_item;

enum render_line_style
  {
    RENDER_LINE_NONE,           /* No line. */
    RENDER_LINE_SINGLE,         /* Single line. */
    RENDER_LINE_DOUBLE,         /* Double line. */
    RENDER_N_LINES
  };

struct render_params
  {
    /* Measures CELL's width.  Stores in *MIN_WIDTH the minimum width required
       to avoid splitting a single word across multiple lines (normally, this
       is the width of the longest word in the cell) and in *MAX_WIDTH the
       minimum width required to avoid line breaks other than at new-lines. */
    void (*measure_cell_width) (void *aux, const struct table_cell *cell,
                                int *min_width, int *max_width);

    /* Returns the height required to render CELL given a width of WIDTH. */
    int (*measure_cell_height) (void *aux, const struct table_cell *cell,
                                int width);

    /* Given that there is space measuring WIDTH by HEIGHT to render CELL,
       where HEIGHT is insufficient to render the entire height of the cell,
       returns the largest height less than HEIGHT at which it is appropriate
       to break the cell.  For example, if breaking at the specified HEIGHT
       would break in the middle of a line of text, the return value would be
       just sufficiently less that the breakpoint would be between lines of
       text.

       Optional.  If NULL, the rendering engine assumes that all breakpoints
       are acceptable. */
    int (*adjust_break) (void *aux, const struct table_cell *cell,
                         int width, int height);

    /* Draws a generalized intersection of lines in the rectangle whose
       top-left corner is (BB[TABLE_HORZ][0], BB[TABLE_VERT][0]) and whose
       bottom-right corner is (BB[TABLE_HORZ][1], BB[TABLE_VERT][1]).

       STYLES is interpreted this way:

       STYLES[TABLE_HORZ][0]: style of line from top of BB to its center.
       STYLES[TABLE_HORZ][1]: style of line from bottom of BB to its center.
       STYLES[TABLE_VERT][0]: style of line from left of BB to its center.
       STYLES[TABLE_VERT][1]: style of line from right of BB to its center. */
    void (*draw_line) (void *aux, int bb[TABLE_N_AXES][2],
                       enum render_line_style styles[TABLE_N_AXES][2]);

    /* Draws CELL within bounding box BB.  CLIP is the same as BB (the common
       case) or a subregion enclosed by BB.  In the latter case only the part
       of the cell that lies within CLIP should actually be drawn, although BB
       should used to determine the layout of the cell. */
    void (*draw_cell) (void *aux, const struct table_cell *cell,
                       int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2]);

    /* Auxiliary data passed to each of the above functions. */
    void *aux;

    /* Page size to try to fit the rendering into.  Some tables will, of
       course, overflow this size. */
    int size[TABLE_N_AXES];

    /* Nominal size of a character in the most common font:
       font_size[TABLE_HORZ]: Em width.
       font_size[TABLE_VERT]: Line spacing. */
    int font_size[TABLE_N_AXES];

    /* Width of different kinds of lines. */
    int line_widths[TABLE_N_AXES][RENDER_N_LINES];

    /* Minimum cell width or height before allowing the cell to be broken
       across two pages.  (Joined cells may always be broken at join
       points.) */
    int min_break[TABLE_N_AXES];
  };


/* An iterator for breaking render_pages into smaller chunks. */
struct render_pager *render_pager_create (const struct render_params *,
                                          const struct table_item *);
void render_pager_destroy (struct render_pager *);

bool render_pager_has_next (const struct render_pager *);
int render_pager_draw_next (struct render_pager *, int space);

void render_pager_draw (const struct render_pager *);
void render_pager_draw_region (const struct render_pager *,
                               int x, int y, int w, int h);
int render_pager_get_size (const struct render_pager *, enum table_axis);
int render_pager_get_best_breakpoint (const struct render_pager *, int height);

#endif /* output/render.h */
