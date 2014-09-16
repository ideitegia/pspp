/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2013, 2014 Free Software Foundation, Inc.

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

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/assertion.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "output/render.h"
#include "output/tab.h"
#include "output/table-item.h"
#include "output/table.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

/* A layout for rendering a specific table on a specific device.

   May represent the layout of an entire table presented to
   render_page_create(), or a rectangular subregion of a table broken out using
   render_break_next() to allow a table to be broken across multiple pages.

   A page's size is not limited to the size passed in as part of render_params.
   render_pager breaks a render_page into smaller render_pages that will fit in
   the available space. */
struct render_page
  {
    const struct render_params *params; /* Parameters of the target device. */
    struct table *table;                /* Table rendered. */
    int ref_cnt;

    /* Local copies of table->n and table->h, for convenience. */
    int n[TABLE_N_AXES];
    int h[TABLE_N_AXES][2];

    /* cp[H] represents x positions within the table.
       cp[H][0] = 0.
       cp[H][1] = the width of the leftmost vertical rule.
       cp[H][2] = cp[H][1] + the width of the leftmost column.
       cp[H][3] = cp[H][2] + the width of the second-from-left vertical rule.
       and so on:
       cp[H][2 * nc] = x position of the rightmost vertical rule.
       cp[H][2 * nc + 1] = total table width including all rules.

       Similarly, cp[V] represents y positions within the table.
       cp[V][0] = 0.
       cp[V][1] = the height of the topmost horizontal rule.
       cp[V][2] = cp[V][1] + the height of the topmost row.
       cp[V][3] = cp[V][2] + the height of the second-from-top horizontal rule.
       and so on:
       cp[V][2 * nr] = y position of the bottommost horizontal rule.
       cp[V][2 * nr + 1] = total table height including all rules.

       Rules and columns can have width or height 0, in which case consecutive
       values in this array are equal. */
    int *cp[TABLE_N_AXES];

    /* render_break_next() can break a table such that some cells are not fully
       contained within a render_page.  This will happen if a cell is too wide
       or two tall to fit on a single page, or if a cell spans multiple rows or
       columns and the page only includes some of those rows or columns.

       This hash table contains "struct render_overflow"s that represents each
       such cell that doesn't completely fit on this page.

       Each overflow cell borders at least one header edge of the table and may
       border more.  (A single table cell that is so large that it fills the
       entire page can overflow on all four sides!) */
    struct hmap overflows;

    /* Contains "struct render_footnote"s, one for each cell with one or more
       footnotes.

       'n_footnotes' is the number of footnotes in the table.  There might be
       more than hmap_count(&page->footnotes) because there can be more than
       one footnote in a cell. */
    struct hmap footnotes;
    size_t n_footnotes;

    /* If a single column (or row) is too wide (or tall) to fit on a page
       reasonably, then render_break_next() will split a single row or column
       across multiple render_pages.  This member indicates when this has
       happened:

       is_edge_cutoff[H][0] is true if pixels have been cut off the left side
       of the leftmost column in this page, and false otherwise.

       is_edge_cutoff[H][1] is true if pixels have been cut off the right side
       of the rightmost column in this page, and false otherwise.

       is_edge_cutoff[V][0] and is_edge_cutoff[V][1] are similar for the top
       and bottom of the table.

       The effect of is_edge_cutoff is to prevent rules along the edge in
       question from being rendered.

       When is_edge_cutoff is true for a given edge, the 'overflows' hmap will
       contain a node for each cell along that edge. */
    bool is_edge_cutoff[TABLE_N_AXES][2];

    /* If part of a joined cell would be cut off by breaking a table along
       'axis' at the rule with offset 'z' (where 0 <= z <= n[axis]), then
       join_crossing[axis][z] is the thickness of the rule that would be cut
       off.

       This is used to know to allocate extra space for breaking at such a
       position, so that part of the cell's content is not lost.

       This affects breaking a table only when headers are present.  When
       headers are not present, the rule's thickness is used for cell content,
       so no part of the cell's content is lost (and in fact it is duplicated
       across both pages). */
    int *join_crossing[TABLE_N_AXES];
  };

static struct render_page *render_page_create (const struct render_params *,
                                               const struct table *);

struct render_page *render_page_ref (const struct render_page *page_);
static void render_page_unref (struct render_page *);

/* Returns the offset in struct render_page's cp[axis] array of the rule with
   index RULE_IDX.  That is, if RULE_IDX is 0, then the offset is that of the
   leftmost or topmost rule; if RULE_IDX is 1, then the offset is that of the
   next rule to the right (or below); and so on. */
static int
rule_ofs (int rule_idx)
{
  return rule_idx * 2;
}

/* Returns the offset in struct render_page's cp[axis] array of the rule with
   index RULE_IDX_R, which counts from the right side (or bottom) of the page
   left (or up), according to whether AXIS is H or V, respectively.  That is,
   if RULE_IDX_R is 0, then the offset is that of the rightmost or bottommost
   rule; if RULE_IDX is 1, then the offset is that of the next rule to the left
   (or above); and so on. */
static int
rule_ofs_r (const struct render_page *page, int axis, int rule_idx_r)
{
  return (page->n[axis] - rule_idx_r) * 2;
}

/* Returns the offset in struct render_page's cp[axis] array of the cell with
   index CELL_IDX.  That is, if CELL_IDX is 0, then the offset is that of the
   leftmost or topmost cell; if CELL_IDX is 1, then the offset is that of the
   next cell to the right (or below); and so on. */
static int
cell_ofs (int cell_idx)
{
  return cell_idx * 2 + 1;
}

/* Returns the width of PAGE along AXIS from OFS0 to OFS1, exclusive. */
static int
axis_width (const struct render_page *page, int axis, int ofs0, int ofs1)
{
  return page->cp[axis][ofs1] - page->cp[axis][ofs0];
}

/* Returns the width of the headers in PAGE along AXIS. */
static int
headers_width (const struct render_page *page, int axis)
{
  int h0 = page->h[axis][0];
  int w0 = axis_width (page, axis, rule_ofs (0), cell_ofs (h0));
  int n = page->n[axis];
  int h1 = page->h[axis][1];
  int w1 = axis_width (page, axis, rule_ofs_r (page, axis, h1), cell_ofs (n));
  return w0 + w1;
}

/* Returns the width of cell X along AXIS in PAGE. */
static int
cell_width (const struct render_page *page, int axis, int x)
{
  return axis_width (page, axis, cell_ofs (x), cell_ofs (x) + 1);
}

/* Returns the width of rule X along AXIS in PAGE. */
static int
rule_width (const struct render_page *page, int axis, int x)
{
  return axis_width (page, axis, rule_ofs (x), rule_ofs (x) + 1);
}

/* Returns the width of rule X along AXIS in PAGE. */
static int
rule_width_r (const struct render_page *page, int axis, int x)
{
  int ofs = rule_ofs_r (page, axis, x);
  return axis_width (page, axis, ofs, ofs + 1);
}

/* Returns the width of cells X0 through X1, exclusive, along AXIS in PAGE. */
static int
joined_width (const struct render_page *page, int axis, int x0, int x1)
{
  return axis_width (page, axis, cell_ofs (x0), cell_ofs (x1) - 1);
}

/* Returns the width of the widest cell, excluding headers, along AXIS in
   PAGE. */
static int
max_cell_width (const struct render_page *page, int axis)
{
  int n = page->n[axis];
  int x0 = page->h[axis][0];
  int x1 = n - page->h[axis][1];
  int x, max;

  max = 0;
  for (x = x0; x < x1; x++)
    {
      int w = cell_width (page, axis, x);
      if (w > max)
        max = w;
    }
  return max;
}

/* A cell that doesn't completely fit on the render_page. */
struct render_overflow
  {
    struct hmap_node node;      /* In render_page's 'overflows' hmap. */

    /* Occupied region of page.

       d[H][0] is the leftmost column.
       d[H][1] is the rightmost column, plus 1.
       d[V][0] is the top row.
       d[V][1] is the bottom row, plus 1.

       The cell in its original table might occupy a larger region.  This
       member reflects the size of the cell in the current render_page, after
       trimming off any rows or columns due to page-breaking. */
    int d[TABLE_N_AXES];

    /* The space that has been trimmed off the cell:

       overflow[H][0]: space trimmed off its left side.
       overflow[H][1]: space trimmed off its right side.
       overflow[V][0]: space trimmed off its top.
       overflow[V][1]: space trimmed off its bottom.

       During rendering, this information is used to position the rendered
       portion of the cell within the available space.

       When a cell is rendered, sometimes it is permitted to spill over into
       space that is ordinarily reserved for rules.  Either way, this space is
       still included in overflow values.

       Suppose, for example, that a cell that joins 2 columns has a width of 60
       pixels and content "abcdef", that the 2 columns that it joins have
       widths of 20 and 30 pixels, respectively, and that therefore the rule
       between the two joined columns has a width of 10 (20 + 10 + 30 = 60).
       It might render like this, if each character is 10x10, and showing a few
       extra table cells for context:

                                     +------+
                                     |abcdef|
                                     +--+---+
                                     |gh|ijk|
                                     +--+---+

       If this render_page is broken at the rule that separates "gh" from
       "ijk", then the page that contains the left side of the "abcdef" cell
       will have overflow[H][1] of 10 + 30 = 40 for its portion of the cell,
       and the page that contains the right side of the cell will have
       overflow[H][0] of 20 + 10 = 30.  The two resulting pages would look like
       this:


                                       +---
                                       |abc
                                       +--+
                                       |gh|
                                       +--+

       and:

                                       ----+
                                       cdef|
                                       +---+
                                       |ijk|
                                       +---+
    */
    int overflow[TABLE_N_AXES][2];
  };

/* Returns a hash value for (,Y). */
static unsigned int
hash_cell (int x, int y)
{
  return hash_int (x + (y << 16), 0);
}

/* Searches PAGE's set of render_overflow for one whose top-left cell is
   (X,Y).  Returns it, if there is one, otherwise a null pointer. */
static const struct render_overflow *
find_overflow (const struct render_page *page, int x, int y)
{
  if (!hmap_is_empty (&page->overflows))
    {
      const struct render_overflow *of;

      HMAP_FOR_EACH_WITH_HASH (of, struct render_overflow, node,
                               hash_cell (x, y), &page->overflows)
        if (x == of->d[H] && y == of->d[V])
          return of;
    }

  return NULL;
}

/* A footnote. */
struct render_footnote
  {
    struct hmap_node node;

    /* The area of the table covered by the cell that has the footnote.

       d[H][0] is the leftmost column.
       d[H][1] is the rightmost column, plus 1.
       d[V][0] is the top row.
       d[V][1] is the bottom row, plus 1.

       The cell in its original table might occupy a larger region.  This
       member reflects the size of the cell in the current render_page, after
       trimming off any rows or columns due to page-breaking. */
    int d[TABLE_N_AXES][2];

    /* The index of the first footnote in the cell. */
    int idx;
  };

static int
count_footnotes (const struct table_cell *cell)
{
  size_t i;
  int n;

  n = 0;
  for (i = 0; i < cell->n_contents; i++)
    n += cell->contents[i].n_footnotes;
  return n;
}

static int
find_footnote_idx (const struct table_cell *cell, const struct hmap *footnotes)
{
  const struct render_footnote *f;

  if (!count_footnotes (cell))
    return 0;

  HMAP_FOR_EACH_WITH_HASH (f, struct render_footnote, node,
                           hash_cell (cell->d[H][0], cell->d[V][0]), footnotes)
    if (f->d[H][0] == cell->d[H][0] && f->d[V][0] == cell->d[V][0])
      return f->idx;

  NOT_REACHED ();
}

/* Row or column dimensions.  Used to figure the size of a table in
   render_page_create() and discarded after that. */
struct render_row
  {
    /* Width without considering rows (or columns) that span more than one (or
       column). */
    int unspanned;

    /* Width taking spanned rows (or columns) into consideration. */
    int width;
  };

/* Modifies the 'width' members of the N elements of ROWS so that their sum,
   when added to rule widths RULES[1] through RULES[N - 1] inclusive, is at
   least WIDTH. */
static void
distribute_spanned_width (int width,
                          struct render_row *rows, const int *rules, int n)
{
  int total_unspanned;
  double w, d0, d1, d;
  int x;

  /* Sum up the unspanned widths of the N rows for use as weights. */
  total_unspanned = 0;
  for (x = 0; x < n; x++)
    total_unspanned += rows[x].unspanned;
  for (x = 0; x < n - 1; x++)
    total_unspanned += rules[x + 1];
  if (total_unspanned >= width)
    return;

  /* The algorithm used here is based on the following description from HTML 4:

         For cells that span multiple columns, a simple approach consists of
         apportioning the min/max widths evenly to each of the constituent
         columns.  A slightly more complex approach is to use the min/max
         widths of unspanned cells to weight how spanned widths are
         apportioned.  Experiments suggest that a blend of the two approaches
         gives good results for a wide range of tables.

     We blend the two approaches half-and-half, except that we cannot use the
     unspanned weights when 'total_unspanned' is 0 (because that would cause a
     division by zero).

     This implementation uses floating-point types and operators, but all the
     values involved are integers.  For integers smaller than 53 bits, this
     should not lose any precision, and it should degrade gracefully for larger
     values.

     The calculation we want to do is this:

        w0 = width / n
        w1 = width * (column's unspanned width) / (total unspanned width)
        (column's width) = (w0 + w1) / 2

     We implement it as a precise calculation in integers by multiplying w0 and
     w1 by the common denominator of all three calculations (d), dividing that
     out in the column width calculation, and then keeping the remainder for
     the next iteration.

     (We actually compute the unspanned width of a column as twice the
     unspanned width, plus the width of the rule on the left, plus the width of
     the rule on the right.  That way each rule contributes to both the cell on
     its left and on its right.)
  */
  d0 = n;
  d1 = 2.0 * (total_unspanned > 0 ? total_unspanned : 1.0);
  d = d0 * d1;
  if (total_unspanned > 0)
    d *= 2.0;
  w = floor (d / 2.0);
  for (x = 0; x < n; x++)
    {
      w += width * d1;
      if (total_unspanned > 0)
        {
          double unspanned = rows[x].unspanned * 2.0;
          if (x < n - 1)
            unspanned += rules[x + 1];
          if (x > 0)
            unspanned += rules[x];
          w += width * unspanned * d0;
        }

      rows[x].width = MAX (rows[x].width, w / d);
      w -= rows[x].width * d;
    }
}

/* Initializes PAGE->cp[AXIS] from the row widths in ROWS and the rule widths
   in RULES. */
static void
accumulate_row_widths (const struct render_page *page, enum table_axis axis,
                       const struct render_row *rows, const int *rules)
{
  int n = page->n[axis];
  int *cp;
  int z;

  cp = page->cp[axis];
  cp[0] = 0;
  for (z = 0; z < n; z++)
    {
      cp[1] = cp[0] + rules[z];
      cp[2] = cp[1] + rows[z].width;
      cp += 2;
    }
  cp[1] = cp[0] + rules[n];
}

/* Returns the sum of widths of the N ROWS and N+1 RULES. */
static int
calculate_table_width (int n, const struct render_row *rows, int *rules)
{
  int width;
  int x;

  width = 0;
  for (x = 0; x < n; x++)
    width += rows[x].width;
  for (x = 0; x <= n; x++)
    width += rules[x];

  return width;
}

/* Rendering utility functions. */

/* Returns the line style to use for drawing a rule of the given TYPE. */
static enum render_line_style
rule_to_render_type (unsigned char type)
{
  switch (type)
    {
    case TAL_0:
    case TAL_GAP:
      return RENDER_LINE_NONE;
    case TAL_1:
      return RENDER_LINE_SINGLE;
    case TAL_2:
      return RENDER_LINE_DOUBLE;
    default:
      NOT_REACHED ();
    }
}

/* Returns the width of the rule in TABLE that is at offset Z along axis A, if
   rendered with PARAMS.  */
static int
measure_rule (const struct render_params *params, const struct table *table,
              enum table_axis a, int z)
{
  enum table_axis b = !a;
  unsigned int rules;
  int d[TABLE_N_AXES];
  int width;

  /* Determine all types of rules that are present, as a bitmap in 'rules'
     where rule type 't' is present if bit 2**t is set. */
  rules = 0;
  d[a] = z;
  for (d[b] = 0; d[b] < table->n[b]; d[b]++)
    rules |= 1u << table_get_rule (table, a, d[H], d[V]);

  /* Calculate maximum width of the rules that are present. */
  width = 0;
  if (rules & (1u << TAL_1)
      || (z > 0 && z < table->n[a] && rules & (1u << TAL_GAP)))
    width = params->line_widths[a][RENDER_LINE_SINGLE];
  if (rules & (1u << TAL_2))
    width = MAX (width, params->line_widths[a][RENDER_LINE_DOUBLE]);
  return width;
}

/* Allocates and returns a new render_page using PARAMS and TABLE.  Allocates
   space for all of the members of the new page, but the caller must initialize
   the 'cp' member itself. */
static struct render_page *
render_page_allocate (const struct render_params *params,
                      struct table *table)
{
  struct render_page *page;
  int i;

  page = xmalloc (sizeof *page);
  page->params = params;
  page->table = table;
  page->ref_cnt = 1;
  page->n[H] = table->n[H];
  page->n[V] = table->n[V];
  page->h[H][0] = table->h[H][0];
  page->h[H][1] = table->h[H][1];
  page->h[V][0] = table->h[V][0];
  page->h[V][1] = table->h[V][1];

  for (i = 0; i < TABLE_N_AXES; i++)
    {
      page->cp[i] = xmalloc ((2 * page->n[i] + 2) * sizeof *page->cp[i]);
      page->join_crossing[i] = xzalloc ((page->n[i] + 1) * sizeof *page->join_crossing[i]);
    }

  hmap_init (&page->overflows);
  hmap_init (&page->footnotes);
  page->n_footnotes = 0;
  memset (page->is_edge_cutoff, 0, sizeof page->is_edge_cutoff);

  return page;
}

/* Allocates and returns a new render_page for PARAMS and TABLE, initializing
   cp[H] in the new page from ROWS and RULES.  The caller must still initialize
   cp[V]. */
static struct render_page *
create_page_with_exact_widths (const struct render_params *params,
                               struct table *table,
                               const struct render_row *rows, int *rules)
{
  struct render_page *page = render_page_allocate (params, table);
  accumulate_row_widths (page, H, rows, rules);
  return page;
}

/* Allocates and returns a new render_page for PARAMS and TABLE.

   Initializes cp[H] in the new page by setting the width of each row 'i' to
   somewhere between the minimum cell width ROW_MIN[i].width and the maximum
   ROW_MAX[i].width.  Sets the width of rules to those in RULES.

   W_MIN is the sum of ROWS_MIN[].width.

   W_MAX is the sum of ROWS_MAX[].width.

   The caller must still initialize cp[V]. */
static struct render_page *
create_page_with_interpolated_widths (const struct render_params *params,
                                      struct table *table,
                                      const struct render_row *rows_min,
                                      const struct render_row *rows_max,
                                      int w_min, int w_max, const int *rules)
{
  /* This implementation uses floating-point types and operators, but all the
     values involved are integers.  For integers smaller than 53 bits, this
     should not lose any precision, and it should degrade gracefully for larger
     values. */
  const int n = table->n[H];
  const double avail = params->size[H] - w_min;
  const double wanted = w_max - w_min;
  struct render_page *page;
  double w;
  int *cph;
  int x;

  assert (wanted > 0);

  page = render_page_allocate (params, table);

  cph = page->cp[H];
  *cph = 0;
  w = (int) wanted / 2;
  for (x = 0; x < n; x++)
    {
      int extra;

      w += avail * (rows_max[x].width - rows_min[x].width);
      extra = w / wanted;
      w -= extra * wanted;

      cph[1] = cph[0] + rules[x];
      cph[2] = cph[1] + rows_min[x].width + extra;
      cph += 2;
    }
  cph[1] = cph[0] + rules[n];

  assert (page->cp[H][n * 2 + 1] == params->size[H]);
  return page;
}


static void
set_join_crossings (struct render_page *page, enum table_axis axis,
                    const struct table_cell *cell, int *rules)
{
  int z;

  for (z = cell->d[axis][0] + 1; z <= cell->d[axis][1] - 1; z++)
    page->join_crossing[axis][z] = rules[z];
}

/* Creates and returns a new render_page for rendering TABLE on a device
   described by PARAMS.

   The new render_page will be suitable for rendering on a device whose page
   size is PARAMS->size, but the caller is responsible for actually breaking it
   up to fit on such a device, using the render_break abstraction.  */
static struct render_page *
render_page_create (const struct render_params *params,
                    const struct table *table_)
{
  struct render_page *page;
  struct table *table;
  enum { MIN, MAX };
  struct render_row *columns[2];
  struct render_row *rows;
  int table_widths[2];
  int *rules[TABLE_N_AXES];
  struct hmap footnotes;
  int footnote_idx;
  int nr, nc;
  int x, y;
  int i;
  enum table_axis axis;

  table = table_ref (table_);
  nc = table_nc (table);
  nr = table_nr (table);

  /* Figure out rule widths. */
  for (axis = 0; axis < TABLE_N_AXES; axis++)
    {
      int n = table->n[axis] + 1;
      int z;

      rules[axis] = xnmalloc (n, sizeof *rules);
      for (z = 0; z < n; z++)
        rules[axis][z] = measure_rule (params, table, axis, z);
    }

  /* Calculate minimum and maximum widths of cells that do not
     span multiple columns.  Assign footnote markers. */
  hmap_init (&footnotes);
  footnote_idx = 0;
  for (i = 0; i < 2; i++)
    columns[i] = xzalloc (nc * sizeof *columns[i]);
  for (y = 0; y < nr; y++)
    for (x = 0; x < nc; )
      {
        struct table_cell cell;

        table_get_cell (table, x, y, &cell);
        if (y == cell.d[V][0])
          {
            int n;

            if (table_cell_colspan (&cell) == 1)
              {
                int w[2];
                int i;

                params->measure_cell_width (params->aux, &cell, footnote_idx,
                                            &w[MIN], &w[MAX]);
                for (i = 0; i < 2; i++)
                  if (columns[i][x].unspanned < w[i])
                    columns[i][x].unspanned = w[i];
              }

            n = count_footnotes (&cell);
            if (n > 0)
              {
                struct render_footnote *f = xmalloc (sizeof *f);
                f->d[H][0] = cell.d[H][0];
                f->d[H][1] = cell.d[H][1];
                f->d[V][0] = cell.d[V][0];
                f->d[V][1] = cell.d[V][1];
                f->idx = footnote_idx;
                hmap_insert (&footnotes, &f->node, hash_cell (x, y));

                footnote_idx += n;
              }
          }
        x = cell.d[H][1];
        table_cell_free (&cell);
      }

  /* Distribute widths of spanned columns. */
  for (i = 0; i < 2; i++)
    for (x = 0; x < nc; x++)
      columns[i][x].width = columns[i][x].unspanned;
  for (y = 0; y < nr; y++)
    for (x = 0; x < nc; )
      {
        struct table_cell cell;

        table_get_cell (table, x, y, &cell);
        if (y == cell.d[V][0] && table_cell_colspan (&cell) > 1)
          {
            int w[2];

            params->measure_cell_width (params->aux, &cell,
                                        find_footnote_idx (&cell, &footnotes),
                                        &w[MIN], &w[MAX]);
            for (i = 0; i < 2; i++)
              distribute_spanned_width (w[i], &columns[i][cell.d[H][0]],
                                        rules[H], table_cell_colspan (&cell));
          }
        x = cell.d[H][1];
        table_cell_free (&cell);
      }

  /* Decide final column widths. */
  for (i = 0; i < 2; i++)
    table_widths[i] = calculate_table_width (table_nc (table),
                                             columns[i], rules[H]);
  if (table_widths[MAX] <= params->size[H])
    {
      /* Fits even with maximum widths.  Use them. */
      page = create_page_with_exact_widths (params, table, columns[MAX],
                                            rules[H]);
    }
  else if (table_widths[MIN] <= params->size[H])
    {
      /* Fits with minimum widths, so distribute the leftover space. */
      page = create_page_with_interpolated_widths (
        params, table, columns[MIN], columns[MAX],
        table_widths[MIN], table_widths[MAX], rules[H]);
    }
  else
    {
      /* Doesn't fit even with minimum widths.  Assign minimums for now, and
         later we can break it horizontally into multiple pages. */
      page = create_page_with_exact_widths (params, table, columns[MIN],
                                            rules[H]);
    }

  /* Calculate heights of cells that do not span multiple rows. */
  rows = xzalloc (nr * sizeof *rows);
  for (y = 0; y < nr; y++)
    {
      for (x = 0; x < nc; )
        {
          struct render_row *r = &rows[y];
          struct table_cell cell;

          table_get_cell (table, x, y, &cell);
          if (y == cell.d[V][0])
            {
              if (table_cell_rowspan (&cell) == 1)
                {
                  int w = joined_width (page, H, cell.d[H][0], cell.d[H][1]);
                  int h = params->measure_cell_height (
                    params->aux, &cell, find_footnote_idx (&cell, &footnotes), w);
                  if (h > r->unspanned)
                    r->unspanned = r->width = h;
                }
              else
                set_join_crossings (page, V, &cell, rules[V]);

              if (table_cell_colspan (&cell) > 1)
                set_join_crossings (page, H, &cell, rules[H]);
            }
          x = cell.d[H][1];
          table_cell_free (&cell);
        }
    }
  for (i = 0; i < 2; i++)
    free (columns[i]);

  /* Distribute heights of spanned rows. */
  for (y = 0; y < nr; y++)
    for (x = 0; x < nc; )
      {
        struct table_cell cell;

        table_get_cell (table, x, y, &cell);
        if (y == cell.d[V][0] && table_cell_rowspan (&cell) > 1)
          {
            int w = joined_width (page, H, cell.d[H][0], cell.d[H][1]);
            int h = params->measure_cell_height (
              params->aux, &cell, find_footnote_idx (&cell, &footnotes), w);
            distribute_spanned_width (h, &rows[cell.d[V][0]], rules[V],
                                      table_cell_rowspan (&cell));
          }
        x = cell.d[H][1];
        table_cell_free (&cell);
      }

  /* Decide final row heights. */
  accumulate_row_widths (page, V, rows, rules[V]);
  free (rows);

  /* Measure headers.  If they are "too big", get rid of them.  */
  for (axis = 0; axis < TABLE_N_AXES; axis++)
    {
      int hw = headers_width (page, axis);
      if (hw * 2 >= page->params->size[axis]
          || hw + max_cell_width (page, axis) > page->params->size[axis])
        {
          page->table = table_unshare (page->table);
          page->table->h[axis][0] = page->table->h[axis][1] = 0;
          page->h[axis][0] = page->h[axis][1] = 0;
        }
    }

  hmap_swap (&page->footnotes, &footnotes);
  hmap_destroy (&footnotes);
  page->n_footnotes = footnote_idx;

  free (rules[H]);
  free (rules[V]);

  return page;
}

/* Increases PAGE's reference count. */
struct render_page *
render_page_ref (const struct render_page *page_)
{
  struct render_page *page = CONST_CAST (struct render_page *, page_);
  page->ref_cnt++;
  return page;
}

/* Decreases PAGE's reference count and destroys PAGE if this causes the
   reference count to fall to zero. */
static void
render_page_unref (struct render_page *page)
{
  if (page != NULL && --page->ref_cnt == 0)
    {
      int i;
      struct render_overflow *overflow, *next;

      HMAP_FOR_EACH_SAFE (overflow, next, struct render_overflow, node,
                          &page->overflows)
        free (overflow);
      hmap_destroy (&page->overflows);

      table_unref (page->table);
      
      for (i = 0; i < TABLE_N_AXES; ++i)
	{
	  free (page->join_crossing[i]);
	  free (page->cp[i]);
	}

      free (page);
    }
}

/* Returns the size of PAGE along AXIS.  (This might be larger than the page
   size specified in the parameters passed to render_page_create().  Use a
   render_break to break up a render_page into page-sized chunks.) */
int
render_page_get_size (const struct render_page *page, enum table_axis axis)
{
  return page->cp[axis][page->n[axis] * 2 + 1];
}

int
render_page_get_best_breakpoint (const struct render_page *page, int height)
{
  int y;

  /* If there's no room for at least the top row and the rules above and below
     it, don't include any of the table. */
  if (page->cp[V][3] > height)
    return 0;

  /* Otherwise include as many rows and rules as we can. */
  for (y = 5; y <= 2 * page->n[V] + 1; y += 2)
    if (page->cp[V][y] > height)
      return page->cp[V][y - 2];
  return height;
}

/* Drawing render_pages. */

static inline enum render_line_style
get_rule (const struct render_page *page, enum table_axis axis,
          const int d[TABLE_N_AXES])
{
  return rule_to_render_type (table_get_rule (page->table,
                                              axis, d[H] / 2, d[V] / 2));
}

static bool
is_rule (int z)
{
  return !(z & 1);
}

static void
render_rule (const struct render_page *page, const int ofs[TABLE_N_AXES],
             const int d[TABLE_N_AXES])
{
  enum render_line_style styles[TABLE_N_AXES][2];
  enum table_axis a;

  for (a = 0; a < TABLE_N_AXES; a++)
    {
      enum table_axis b = !a;

      styles[a][0] = styles[a][1] = RENDER_LINE_NONE;

      if (!is_rule (d[a])
          || (page->is_edge_cutoff[a][0] && d[a] == 0)
          || (page->is_edge_cutoff[a][1] && d[a] == page->n[a] * 2))
        continue;

      if (is_rule (d[b]))
        {
          if (d[b] > 0)
            {
              int e[TABLE_N_AXES];
              e[H] = d[H];
              e[V] = d[V];
              e[b]--;
              styles[a][0] = get_rule (page, a, e);
            }

          if (d[b] / 2 < page->table->n[b])
            styles[a][1] = get_rule (page, a, d);
        }
      else
        styles[a][0] = styles[a][1] = get_rule (page, a, d);
    }

  if (styles[H][0] != RENDER_LINE_NONE || styles[H][1] != RENDER_LINE_NONE
      || styles[V][0] != RENDER_LINE_NONE || styles[V][1] != RENDER_LINE_NONE)
    {
      int bb[TABLE_N_AXES][2];

      bb[H][0] = ofs[H] + page->cp[H][d[H]];
      bb[H][1] = ofs[H] + page->cp[H][d[H] + 1];
      bb[V][0] = ofs[V] + page->cp[V][d[V]];
      bb[V][1] = ofs[V] + page->cp[V][d[V] + 1];
      page->params->draw_line (page->params->aux, bb, styles);
    }
}

static void
render_cell (const struct render_page *page, const int ofs[TABLE_N_AXES],
             const struct table_cell *cell)
{
  const struct render_overflow *of;
  int bb[TABLE_N_AXES][2];
  int clip[TABLE_N_AXES][2];

  bb[H][0] = clip[H][0] = ofs[H] + page->cp[H][cell->d[H][0] * 2 + 1];
  bb[H][1] = clip[H][1] = ofs[H] + page->cp[H][cell->d[H][1] * 2];
  bb[V][0] = clip[V][0] = ofs[V] + page->cp[V][cell->d[V][0] * 2 + 1];
  bb[V][1] = clip[V][1] = ofs[V] + page->cp[V][cell->d[V][1] * 2];

  of = find_overflow (page, cell->d[H][0], cell->d[V][0]);
  if (of)
    {
      enum table_axis axis;

      for (axis = 0; axis < TABLE_N_AXES; axis++)
        {
          if (of->overflow[axis][0])
            {
              bb[axis][0] -= of->overflow[axis][0];
              if (cell->d[axis][0] == 0 && !page->is_edge_cutoff[axis][0])
                clip[axis][0] = ofs[axis] + page->cp[axis][cell->d[axis][0] * 2];
            }
          if (of->overflow[axis][1])
            {
              bb[axis][1] += of->overflow[axis][1];
              if (cell->d[axis][1] == page->n[axis] && !page->is_edge_cutoff[axis][1])
                clip[axis][1] = ofs[axis] + page->cp[axis][cell->d[axis][1] * 2 + 1];
            }
        }
    }

  page->params->draw_cell (page->params->aux, cell,
                           find_footnote_idx (cell, &page->footnotes), bb, clip);
}

/* Draws the cells of PAGE indicated in BB. */
static void
render_page_draw_cells (const struct render_page *page,
                        int ofs[TABLE_N_AXES], int bb[TABLE_N_AXES][2])
{
  int x, y;

  for (y = bb[V][0]; y < bb[V][1]; y++)
    for (x = bb[H][0]; x < bb[H][1]; )
      if (is_rule (x) || is_rule (y))
        {
          int d[TABLE_N_AXES];
          d[H] = x;
          d[V] = y;
          render_rule (page, ofs, d);
          x++;
        }
      else
        {
          struct table_cell cell;

          table_get_cell (page->table, x / 2, y / 2, &cell);
          if (y / 2 == bb[V][0] / 2 || y / 2 == cell.d[V][0])
            render_cell (page, ofs, &cell);
          x = rule_ofs (cell.d[H][1]);
          table_cell_free (&cell);
        }
}

/* Renders PAGE, by calling the 'draw_line' and 'draw_cell' functions from the
   render_params provided to render_page_create(). */
void
render_page_draw (const struct render_page *page, int ofs[TABLE_N_AXES])
{
  int bb[TABLE_N_AXES][2];

  bb[H][0] = 0;
  bb[H][1] = page->n[H] * 2 + 1;
  bb[V][0] = 0;
  bb[V][1] = page->n[V] * 2 + 1;

  render_page_draw_cells (page, ofs, bb);
}

/* Returns the greatest value i, 0 <= i < n, such that cp[i] <= x0. */
static int
get_clip_min_extent (int x0, const int cp[], int n)
{
  int low, high, best;

  low = 0;
  high = n;
  best = 0;
  while (low < high)
    {
      int middle = low + (high - low) / 2;

      if (cp[middle] <= x0)
        {
          best = middle;
          low = middle + 1;
        }
      else
        high = middle;
    }

  return best;
}

/* Returns the least value i, 0 <= i < n, such that cp[i] >= x1. */
static int
get_clip_max_extent (int x1, const int cp[], int n)
{
  int low, high, best;

  low = 0;
  high = n;
  best = n;
  while (low < high)
    {
      int middle = low + (high - low) / 2;

      if (cp[middle] >= x1)
        best = high = middle;
      else
        low = middle + 1;
    }

  while (best > 0 && cp[best - 1] == cp[best])
    best--;

  return best;
}

/* Renders the cells of PAGE that intersect (X,Y)-(X+W,Y+H), by calling the
   'draw_line' and 'draw_cell' functions from the render_params provided to
   render_page_create(). */
void
render_page_draw_region (const struct render_page *page,
                         int ofs[TABLE_N_AXES], int clip[TABLE_N_AXES][2])
{
  int bb[TABLE_N_AXES][2];

  bb[H][0] = get_clip_min_extent (clip[H][0], page->cp[H], page->n[H] * 2 + 1);
  bb[H][1] = get_clip_max_extent (clip[H][1], page->cp[H], page->n[H] * 2 + 1);
  bb[V][0] = get_clip_min_extent (clip[V][0], page->cp[V], page->n[V] * 2 + 1);
  bb[V][1] = get_clip_max_extent (clip[V][1], page->cp[V], page->n[V] * 2 + 1);

  render_page_draw_cells (page, ofs, bb);
}

/* Breaking up tables to fit on a page. */

/* An iterator for breaking render_pages into smaller chunks. */
struct render_break
  {
    struct render_page *page;   /* Page being broken up. */
    enum table_axis axis;       /* Axis along which 'page' is being broken. */
    int z;                      /* Next cell along 'axis'. */
    int pixel;                  /* Pixel offset within cell 'z' (usually 0). */
    int hw;                     /* Width of headers of 'page' along 'axis'. */
  };

static int needed_size (const struct render_break *, int cell);
static bool cell_is_breakable (const struct render_break *, int cell);
static struct render_page *render_page_select (const struct render_page *,
                                               enum table_axis,
                                               int z0, int p0,
                                               int z1, int p1);

/* Initializes render_break B for breaking PAGE along AXIS. */
static void
render_break_init (struct render_break *b, const struct render_page *page,
                   enum table_axis axis)
{
  b->page = render_page_ref (page);
  b->axis = axis;
  b->z = page->h[axis][0];
  b->pixel = 0;
  b->hw = headers_width (page, axis);
}

/* Initializes B as a render_break structure for which
   render_break_has_next() always returns false. */
static void
render_break_init_empty (struct render_break *b)
{
  b->page = NULL;
  b->axis = TABLE_HORZ;
  b->z = 0;
  b->pixel = 0;
  b->hw = 0;
}

/* Frees B and unrefs the render_page that it owns. */
static void
render_break_destroy (struct render_break *b)
{
  if (b != NULL)
    {
      render_page_unref (b->page);
      b->page = NULL;
    }
}

/* Returns true if B still has cells that are yet to be returned,
   false if all of B's page has been processed. */
static bool
render_break_has_next (const struct render_break *b)
{
  const struct render_page *page = b->page;
  enum table_axis axis = b->axis;

  return page != NULL && b->z < page->n[axis] - page->h[axis][1];
}

/* Returns a new render_page that is up to SIZE pixels wide along B's axis.
   Returns a null pointer if B has already been completely broken up, or if
   SIZE is too small to reasonably render any cells.  The latter will never
   happen if SIZE is at least as large as the page size passed to
   render_page_create() along B's axis. */
static struct render_page *
render_break_next (struct render_break *b, int size)
{
  const struct render_page *page = b->page;
  enum table_axis axis = b->axis;
  struct render_page *subpage;
  int z, pixel;

  if (!render_break_has_next (b))
    return NULL;

  pixel = 0;
  for (z = b->z; z < page->n[axis] - page->h[axis][1]; z++)
    {
      int needed = needed_size (b, z + 1);
      if (needed > size)
        {
          if (cell_is_breakable (b, z))
            {
              /* If there is no right header and we render a partial cell on
                 the right side of the body, then we omit the rightmost rule of
                 the body.  Otherwise the rendering is deceptive because it
                 looks like the whole cell is present instead of a partial
                 cell.

                 This is similar to code for the left side in needed_size(). */
              int rule_allowance = (page->h[axis][1]
                                    ? 0
                                    : rule_width (page, axis, z));

              /* The amount that, if we added cell 'z', the rendering would
                 overfill the allocated 'size'. */
              int overhang = needed - size - rule_allowance;

              /* The width of cell 'z'. */
              int cell_size = cell_width (page, axis, z);

              /* The amount trimmed off the left side of 'z',
                 and the amount left to render. */
              int cell_ofs = z == b->z ? b->pixel : 0;
              int cell_left = cell_size - cell_ofs;

              /* A small but visible width.  */
              int em = page->params->font_size[axis];

              /* If some of the cell remains to render,
                 and there would still be some of the cell left afterward,
                 then partially render that much of the cell. */
              pixel = (cell_left && cell_left > overhang
                       ? cell_left - overhang + cell_ofs
                       : 0);

              /* If there would be only a tiny amount of the cell left after
                 rendering it partially, reduce the amount rendered slightly
                 to make the output look a little better. */
              if (pixel + em > cell_size)
                pixel = MAX (pixel - em, 0);

              /* If we're breaking vertically, then consider whether the cells
                 being broken have a better internal breakpoint than the exact
                 number of pixels available, which might look bad e.g. because
                 it breaks in the middle of a line of text. */
              if (axis == TABLE_VERT && page->params->adjust_break)
                {
                  int x;

                  for (x = 0; x < page->n[H]; )
                    {
                      struct table_cell cell;
                      int better_pixel;
                      int w;

                      table_get_cell (page->table, x, z, &cell);
                      w = joined_width (page, H, cell.d[H][0], cell.d[H][1]);
                      better_pixel = page->params->adjust_break (
                        page->params->aux, &cell,
                        find_footnote_idx (&cell, &page->footnotes), w, pixel);
                      x = cell.d[H][1];
                      table_cell_free (&cell);

                      if (better_pixel < pixel)
                        {
                          if (better_pixel > (z == b->z ? b->pixel : 0))
                            {
                              pixel = better_pixel;
                              break;
                            }
                          else if (better_pixel == 0 && z != b->z)
                            {
                              pixel = 0;
                              break;
                            }
                        }
                    }
                }
            }
          break;
        }
    }

  if (z == b->z && !pixel)
    return NULL;

  subpage = render_page_select (page, axis, b->z, b->pixel,
                                pixel ? z + 1 : z,
                                pixel ? cell_width (page, axis, z) - pixel
                                : 0);
  b->z = z;
  b->pixel = pixel;
  return subpage;
}

/* Returns the width that would be required along B's axis to render a page
   from B's current position up to but not including CELL. */
static int
needed_size (const struct render_break *b, int cell)
{
  const struct render_page *page = b->page;
  enum table_axis axis = b->axis;
  int size;

  /* Width of left header not including its rightmost rule.  */
  size = axis_width (page, axis, 0, rule_ofs (page->h[axis][0]));

  /* If we have a pixel offset and there is no left header, then we omit the
     leftmost rule of the body.  Otherwise the rendering is deceptive because
     it looks like the whole cell is present instead of a partial cell.

     Otherwise (if there are headers) we will be merging two rules: the
     rightmost rule in the header and the leftmost rule in the body.  We assume
     that the width of a merged rule is the larger of the widths of either rule
     invidiually. */
  if (b->pixel == 0 || page->h[axis][0])
    size += MAX (rule_width (page, axis, page->h[axis][0]),
                 rule_width (page, axis, b->z));

  /* Width of body, minus any pixel offset in the leftmost cell. */
  size += joined_width (page, axis, b->z, cell) - b->pixel;

  /* Width of rightmost rule in body merged with leftmost rule in headers. */
  size += MAX (rule_width_r (page, axis, page->h[axis][1]),
               rule_width (page, axis, cell));

  /* Width of right header not including its leftmost rule. */
  size += axis_width (page, axis, rule_ofs_r (page, axis, page->h[axis][1]),
                      rule_ofs_r (page, axis, 0));

  /* Join crossing. */
  if (page->h[axis][0] && page->h[axis][1])
    size += page->join_crossing[axis][b->z];

  return size;
}

/* Returns true if CELL along B's axis may be broken across a page boundary.

   This is just a heuristic.  Breaking cells across page boundaries can save
   space, but it looks ugly. */
static bool
cell_is_breakable (const struct render_break *b, int cell)
{
  const struct render_page *page = b->page;
  enum table_axis axis = b->axis;

  return cell_width (page, axis, cell) >= page->params->min_break[axis];
}

/* render_pager. */

struct render_pager
  {
    const struct render_params *params;

    struct render_page **pages;
    size_t n_pages, allocated_pages;

    size_t cur_page;
    struct render_break x_break;
    struct render_break y_break;
  };

static const struct render_page *
render_pager_add_table (struct render_pager *p, struct table *table)
{
  struct render_page *page;

  if (p->n_pages >= p->allocated_pages)
    p->pages = x2nrealloc (p->pages, &p->allocated_pages, sizeof *p->pages);
  page = p->pages[p->n_pages++] = render_page_create (p->params, table);
  return page;
}

static void
render_pager_start_page (struct render_pager *p)
{
  render_break_init (&p->x_break, p->pages[p->cur_page++], H);
  render_break_init_empty (&p->y_break);
}

static void
add_footnote_page (struct render_pager *p, const struct render_page *body)
{
  const struct table *table = body->table;
  int nc = table_nc (table);
  int nr = table_nr (table);
  int footnote_idx = 0;
  struct tab_table *t;
  int x, y;

  if (!body->n_footnotes)
    return;

  t = tab_create (2, body->n_footnotes);
  for (y = 0; y < nr; y++)
    for (x = 0; x < nc; )
      {
        struct table_cell cell;

        table_get_cell (table, x, y, &cell);
        if (y == cell.d[V][0])
          {
            size_t i;

            for (i = 0; i < cell.n_contents; i++)
              {
                const struct cell_contents *cc = &cell.contents[i];
                size_t j;

                for (j = 0; j < cc->n_footnotes; j++)
                  {
                    const char *f = cc->footnotes[j];

                    tab_text (t, 0, footnote_idx, TAB_LEFT, "");
                    tab_footnote (t, 0, footnote_idx, "(none)");
                    tab_text (t, 1, footnote_idx, TAB_LEFT, f);
                    footnote_idx++;
                  }
              }
          }
        x = cell.d[H][1];
        table_cell_free (&cell);
      }
  render_pager_add_table (p, &t->table);
}

/* Creates and returns a new render_pager for rendering TABLE_ITEM on the
   device with the given PARAMS. */
struct render_pager *
render_pager_create (const struct render_params *params,
                     const struct table_item *table_item)
{
  const char *caption = table_item_get_caption (table_item);
  const char *title = table_item_get_title (table_item);
  const struct render_page *body_page;
  struct render_pager *p;

  p = xzalloc (sizeof *p);
  p->params = params;

  /* Title. */
  if (title)
    render_pager_add_table (p, table_from_string (TAB_LEFT, title));

  /* Body. */
  body_page = render_pager_add_table (p, table_ref (table_item_get_table (
                                                      table_item)));

  /* Caption. */
  if (caption)
    render_pager_add_table (p, table_from_string (TAB_LEFT, caption));

  /* Footnotes. */
  add_footnote_page (p, body_page);

  render_pager_start_page (p);

  return p;
}

/* Destroys P. */
void
render_pager_destroy (struct render_pager *p)
{
  if (p)
    {
      size_t i;

      render_break_destroy (&p->x_break);
      render_break_destroy (&p->y_break);
      for (i = 0; i < p->n_pages; i++)
        render_page_unref (p->pages[i]);
      free (p->pages);
      free (p);
    }
}

/* Returns true if P has content remaining to render, false if rendering is
   done. */
bool
render_pager_has_next (const struct render_pager *p_)
{
  struct render_pager *p = CONST_CAST (struct render_pager *, p_);

  while (!render_break_has_next (&p->y_break))
    {
      render_break_destroy (&p->y_break);
      if (!render_break_has_next (&p->x_break))
        {
          render_break_destroy (&p->x_break);
          if (p->cur_page >= p->n_pages)
            {
              render_break_init_empty (&p->x_break);
              render_break_init_empty (&p->y_break);
              return false;
            }
          render_pager_start_page (p);
        }
      else
        render_break_init (&p->y_break,
                           render_break_next (&p->x_break, p->params->size[H]), V);
    }
  return true;
}

/* Draws a chunk of content from P to fit in a space that has vertical size
   SPACE and the horizontal size specified in the render_params passed to
   render_page_create().  Returns the amount of space actually used by the
   rendered chunk, which will be 0 if SPACE is too small to render anything or
   if no content remains (use render_pager_has_next() to distinguish these
   cases). */
int
render_pager_draw_next (struct render_pager *p, int space)
{
  int ofs[TABLE_N_AXES] = { 0, 0 };
  size_t start_page = SIZE_MAX;

  while (render_pager_has_next (p))
    {
      struct render_page *page;

      if (start_page == p->cur_page)
        break;
      start_page = p->cur_page;

      page = render_break_next (&p->y_break, space - ofs[V]);
      if (!page)
        break;

      render_page_draw (page, ofs);
      ofs[V] += render_page_get_size (page, V);
      render_page_unref (page);
    }
  return ofs[V];
}

/* Draws all of P's content. */
void
render_pager_draw (const struct render_pager *p)
{
  render_pager_draw_region (p, 0, 0, INT_MAX, INT_MAX);
}

/* Draws the region of P's content that lies in the region (X,Y)-(X+W,Y+H).
   Some extra content might be drawn; the device should perform clipping as
   necessary. */
void
render_pager_draw_region (const struct render_pager *p,
                          int x, int y, int w, int h)
{
  int ofs[TABLE_N_AXES] = { 0, 0 };
  int clip[TABLE_N_AXES][2];
  size_t i;

  clip[H][0] = x;
  clip[H][1] = x + w;
  for (i = 0; i < p->n_pages; i++)
    {
      const struct render_page *page = p->pages[i];
      int size = render_page_get_size (page, V);

      clip[V][0] = MAX (y, ofs[V]) - ofs[V];
      clip[V][1] = MIN (y + h, ofs[V] + size) - ofs[V];
      if (clip[V][1] > clip[V][0])
        render_page_draw_region (page, ofs, clip);

      ofs[V] += size;
    }
}

/* Returns the size of P's content along AXIS; i.e. the content's width if AXIS
   is TABLE_HORZ and its length if AXIS is TABLE_VERT. */
int
render_pager_get_size (const struct render_pager *p, enum table_axis axis)
{
  int size = 0;
  size_t i;

  for (i = 0; i < p->n_pages; i++)
    {
      int subsize = render_page_get_size (p->pages[i], axis);
      size = axis == H ? MAX (size, subsize) : size + subsize;
    }

  return size;
}

int
render_pager_get_best_breakpoint (const struct render_pager *p, int height)
{
  int y = 0;
  size_t i;

  for (i = 0; i < p->n_pages; i++)
    {
      int size = render_page_get_size (p->pages[i], V);
      if (y + size >= height)
        return render_page_get_best_breakpoint (p->pages[i], height - y) + y;
      y += size;
    }

  return height;
}

/* render_page_select() and helpers. */

struct render_page_selection
  {
    const struct render_page *page; /* Page whose slice we are selecting. */
    struct render_page *subpage; /* New page under construction. */
    enum table_axis a;   /* Axis of 'page' along which 'subpage' is a slice. */
    enum table_axis b;   /* The opposite of 'a'. */
    int z0;              /* First cell along 'a' being selected. */
    int z1;              /* Last cell being selected, plus 1. */
    int p0;              /* Number of pixels to trim off left side of z0. */
    int p1;              /* Number of pixels to trim off right side of z1-1. */
  };

static void cell_to_subpage (struct render_page_selection *,
                             const struct table_cell *,
                             int subcell[TABLE_N_AXES]);
static const struct render_overflow *find_overflow_for_cell (
  struct render_page_selection *, const struct table_cell *);
static struct render_overflow *insert_overflow (struct render_page_selection *,
                                                const struct table_cell *);

/* Creates and returns a new render_page whose contents are a subregion of
   PAGE's contents.  The new render_page includes cells Z0 through Z1
   (exclusive) along AXIS, plus any headers on AXIS.

   If P0 is nonzero, then it is a number of pixels to exclude from the left or
   top (according to AXIS) of cell Z0.  Similarly, P1 is a number of pixels to
   exclude from the right or bottom of cell Z1 - 1.  (P0 and P1 are used to
   render cells that are too large to fit on a single page.)

   The whole of axis !AXIS is included.  (The caller may follow up with another
   call to render_page_select() to select on !AXIS to select on that axis as
   well.)

   The caller retains ownership of PAGE, which is not modified. */
static struct render_page *
render_page_select (const struct render_page *page, enum table_axis axis,
                    int z0, int p0, int z1, int p1)
{
  const struct render_footnote *f;
  struct render_page_selection s;
  enum table_axis a = axis;
  enum table_axis b = !a;
  struct render_page *subpage;
  struct render_overflow *ro;
  int *dcp, *scp;
  int *jc;
  int z;


  /* Optimize case where all of PAGE is selected by just incrementing the
     reference count. */
  if (z0 == page->h[a][0] && p0 == 0
      && z1 == page->n[a] - page->h[a][1] && p1 == 0)
    {
      struct render_page *page_rw = CONST_CAST (struct render_page *, page);
      page_rw->ref_cnt++;
      return page_rw;
    }

  /* Allocate subpage. */
  subpage = render_page_allocate (page->params,
                                  table_select_slice (
                                    table_ref (page->table),
                                    a, z0, z1, true));

  /* An edge is cut off if it was cut off in PAGE or if we're trimming pixels
     off that side of the page and there are no headers. */
  subpage->is_edge_cutoff[a][0] =
    subpage->h[a][0] == 0 && (p0 || (z0 == 0 && page->is_edge_cutoff[a][0]));
  subpage->is_edge_cutoff[a][1] =
    subpage->h[a][1] == 0 && (p1 || (z1 == page->n[a]
                                     && page->is_edge_cutoff[a][1]));
  subpage->is_edge_cutoff[b][0] = page->is_edge_cutoff[b][0];
  subpage->is_edge_cutoff[b][1] = page->is_edge_cutoff[b][1];

  /* Select join crossings from PAGE into subpage. */
  jc = subpage->join_crossing[a];
  for (z = 0; z < page->h[a][0]; z++)
    *jc++ = page->join_crossing[a][z];
  for (z = z0; z <= z1; z++)
    *jc++ = page->join_crossing[a][z];
  for (z = page->n[a] - page->h[a][1]; z < page->n[a]; z++)
    *jc++ = page->join_crossing[a][z];
  assert (jc == &subpage->join_crossing[a][subpage->n[a] + 1]);

  memcpy (subpage->join_crossing[b], page->join_crossing[b],
          (subpage->n[b] + 1) * sizeof **subpage->join_crossing);

  /* Select widths from PAGE into subpage. */
  scp = page->cp[a];
  dcp = subpage->cp[a];
  *dcp = 0;
  for (z = 0; z <= rule_ofs (subpage->h[a][0]); z++, dcp++)
    {
      if (z == 0 && subpage->is_edge_cutoff[a][0])
        dcp[1] = dcp[0];
      else
        dcp[1] = dcp[0] + (scp[z + 1] - scp[z]);
    }
  for (z = cell_ofs (z0); z <= cell_ofs (z1 - 1); z++, dcp++)
    {
      dcp[1] = dcp[0] + (scp[z + 1] - scp[z]);
      if (z == cell_ofs (z0))
        {
          dcp[1] -= p0;
          if (page->h[a][0] && page->h[a][1])
            dcp[1] += page->join_crossing[a][z / 2];
        }
      if (z == cell_ofs (z1 - 1))
        dcp[1] -= p1;
    }
  for (z = rule_ofs_r (page, a, subpage->h[a][1]);
       z <= rule_ofs_r (page, a, 0); z++, dcp++)
    {
      if (z == rule_ofs_r (page, a, 0) && subpage->is_edge_cutoff[a][1])
        dcp[1] = dcp[0];
      else
        dcp[1] = dcp[0] + (scp[z + 1] - scp[z]);
    }
  assert (dcp == &subpage->cp[a][2 * subpage->n[a] + 1]);

  for (z = 0; z < page->n[b] * 2 + 2; z++)
    subpage->cp[b][z] = page->cp[b][z];

  /* Add new overflows. */
  s.page = page;
  s.a = a;
  s.b = b;
  s.z0 = z0;
  s.z1 = z1;
  s.p0 = p0;
  s.p1 = p1;
  s.subpage = subpage;

  if (!page->h[a][0] || z0 > page->h[a][0] || p0)
    for (z = 0; z < page->n[b]; )
      {
        struct table_cell cell;
        int d[TABLE_N_AXES];
        bool overflow0;
        bool overflow1;

        d[a] = z0;
        d[b] = z;

        table_get_cell (page->table, d[H], d[V], &cell);
        overflow0 = p0 || cell.d[a][0] < z0;
        overflow1 = cell.d[a][1] > z1 || (cell.d[a][1] == z1 && p1);
        if (overflow0 || overflow1)
          {
            ro = insert_overflow (&s, &cell);

            if (overflow0)
              {
                ro->overflow[a][0] += p0 + axis_width (
                  page, a, cell_ofs (cell.d[a][0]), cell_ofs (z0));
                if (page->h[a][0] && page->h[a][1])
                  ro->overflow[a][0] -= page->join_crossing[a][cell.d[a][0]
                                                               + 1];
              }

            if (overflow1)
              {
                ro->overflow[a][1] += p1 + axis_width (
                  page, a, cell_ofs (z1), cell_ofs (cell.d[a][1]));
                if (page->h[a][0] && page->h[a][1])
                  ro->overflow[a][1] -= page->join_crossing[a][cell.d[a][1]];
              }
          }
        z = cell.d[b][1];
        table_cell_free (&cell);
      }

  if (!page->h[a][1] || z1 < page->n[a] - page->h[a][1] || p1)
    for (z = 0; z < page->n[b]; )
      {
        struct table_cell cell;
        int d[TABLE_N_AXES];

        d[a] = z1 - 1;
        d[b] = z;
        table_get_cell (page->table, d[H], d[V], &cell);
        if ((cell.d[a][1] > z1 || (cell.d[a][1] == z1 && p1))
            && find_overflow_for_cell (&s, &cell) == NULL)
          {
            ro = insert_overflow (&s, &cell);
            ro->overflow[a][1] += p1 + axis_width (page, a, cell_ofs (z1),
                                                   cell_ofs (cell.d[a][1]));
          }
        z = cell.d[b][1];
        table_cell_free (&cell);
      }

  /* Copy overflows from PAGE into subpage. */
  HMAP_FOR_EACH (ro, struct render_overflow, node, &page->overflows)
    {
      struct table_cell cell;

      table_get_cell (page->table, ro->d[H], ro->d[V], &cell);
      if (cell.d[a][1] > z0 && cell.d[a][0] < z1
          && find_overflow_for_cell (&s, &cell) == NULL)
        insert_overflow (&s, &cell);
      table_cell_free (&cell);
    }

  /* Copy footnotes from PAGE into subpage. */
  HMAP_FOR_EACH (f, struct render_footnote, node, &page->footnotes)
    if ((f->d[a][0] >= z0 && f->d[a][0] < z1)
        || (f->d[a][1] - 1 >= z0 && f->d[a][1] - 1 < z1))
      {
        struct render_footnote *nf = xmalloc (sizeof *nf);
        nf->d[a][0] = MAX (z0, f->d[a][0]) - z0 + page->h[a][0];
        nf->d[a][1] = MIN (z1, f->d[a][1]) - z0 + page->h[a][0];
        nf->d[b][0] = f->d[b][0];
        nf->d[b][1] = f->d[b][1];
        nf->idx = f->idx;
        hmap_insert (&subpage->footnotes, &nf->node,
                     hash_cell (nf->d[H][0], nf->d[V][0]));
      }

  return subpage;
}

/* Given CELL, a table_cell within S->page, stores in SUBCELL the (x,y)
   coordinates of the top-left cell as it will appear in S->subpage.

   CELL must actually intersect the region of S->page that is being selected
   by render_page_select() or the results will not make any sense. */
static void
cell_to_subpage (struct render_page_selection *s,
                 const struct table_cell *cell, int subcell[TABLE_N_AXES])
{
  enum table_axis a = s->a;
  enum table_axis b = s->b;
  int ha0 = s->subpage->h[a][0];

  subcell[a] = MAX (cell->d[a][0] - s->z0 + ha0, ha0);
  subcell[b] = cell->d[b][0];
}

/* Given CELL, a table_cell within S->page, returns the render_overflow for
   that cell in S->subpage, if there is one, and a null pointer otherwise.

   CELL must actually intersect the region of S->page that is being selected
   by render_page_select() or the results will not make any sense. */
static const struct render_overflow *
find_overflow_for_cell (struct render_page_selection *s,
                        const struct table_cell *cell)
{
  int subcell[2];

  cell_to_subpage (s, cell, subcell);
  return find_overflow (s->subpage, subcell[H], subcell[V]);
}

/* Given CELL, a table_cell within S->page, inserts a render_overflow for that
   cell in S->subpage (which must not already exist).  Initializes the new
   render_overflow's 'overflow' member from the overflow for CELL in S->page,
   if there is one.

   CELL must actually intersect the region of S->page that is being selected
   by render_page_select() or the results will not make any sense. */
static struct render_overflow *
insert_overflow (struct render_page_selection *s,
                 const struct table_cell *cell)
{
  const struct render_overflow *old;
  struct render_overflow *of;

  of = xzalloc (sizeof *of);
  cell_to_subpage (s, cell, of->d);
  hmap_insert (&s->subpage->overflows, &of->node,
               hash_cell (of->d[H], of->d[V]));

  old = find_overflow (s->page, cell->d[H][0], cell->d[V][0]);
  if (old != NULL)
    memcpy (of->overflow, old->overflow, sizeof of->overflow);

  return of;
}
