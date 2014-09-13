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

/* Parameters for rendering a table_item to a device.


   Coordinate system
   =================

   The rendering code assumes that larger 'x' is to the right and larger 'y'
   toward the bottom of the page.

   The rendering code assumes that the table being rendered has its upper left
   corner at (0,0) in device coordinates.  This is usually not the case from
   the driver's perspective, so the driver should expect to apply its own
   offset to coordinates passed to callback functions.


   Callback functions
   ==================

   For each of the callback functions, AUX is passed as the 'aux' member of the
   render_params structure.

   The device is expected to transform numerical footnote index numbers into
   footnote markers.  The existing drivers use str_format_26adic() to transform
   index 0 to "a", index 1 to "b", and so on.  The FOOTNOTE_IDX supplied to
   each function is the footnote index number for the first footnote in the
   cell.  If a cell contains more than one footnote, then the additional
   footnote indexes increase sequentially, e.g. the second footnote has index
   FOOTNOTE_IDX + 1.
*/
struct render_params
  {
    /* Measures CELL's width.  Stores in *MIN_WIDTH the minimum width required
       to avoid splitting a single word across multiple lines (normally, this
       is the width of the longest word in the cell) and in *MAX_WIDTH the
       minimum width required to avoid line breaks other than at new-lines.
       */
    void (*measure_cell_width) (void *aux, const struct table_cell *cell,
                                int footnote_idx,
                                int *min_width, int *max_width);

    /* Returns the height required to render CELL given a width of WIDTH. */
    int (*measure_cell_height) (void *aux, const struct table_cell *cell,
                                int footnote_idx, int width);

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
                         int footnote_idx, int width, int height);

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
                       int footnote_idx,
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
