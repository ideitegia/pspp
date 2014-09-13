/* PSPP - a program for statistical analysis.
   Copyright (C) 1997, 1998, 1999, 2000, 2009, 2011, 2013, 2014 Free Software Foundation, Inc.

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

#ifndef OUTPUT_TABLE_PROVIDER
#define OUTPUT_TABLE_PROVIDER 1

#include "output/table.h"

/* An item of contents within a table cell. */
struct cell_contents
  {
    unsigned int options;       /* TAB_*. */

    /* Exactly one of these must be nonnull. */
    char *text;                 /* A paragraph of text. */
    struct table_item *table;   /* A table nested within the cell. */

    /* Optional footnote(s). */
    char **footnotes;
    size_t n_footnotes;
  };

/* A cell in a table. */
struct table_cell
  {
    /* Occupied table region.

       d[TABLE_HORZ][0] is the leftmost column.
       d[TABLE_HORZ][1] is the rightmost column, plus 1.
       d[TABLE_VERT][0] is the top row.
       d[TABLE_VERT][1] is the bottom row, plus 1.

       For an ordinary cell:
           d[TABLE_HORZ][1] == d[TABLE_HORZ][0] + 1
       and d[TABLE_VERT][1] == d[TABLE_VERT][0] + 1

       For a joined cell:
          d[TABLE_HORZ][1] > d[TABLE_HORZ][0] + 1
       or d[TABLE_VERT][1] > d[TABLE_VERT][0] + 1
       or both. */
    int d[TABLE_N_AXES][2];

    /* The cell's contents.

       Most table cells contain only one item (a paragraph of text), but cells
       are allowed to be empty (n_contents == 0) or contain a nested table, or
       multiple items.

       'inline_contents' provides a place to store a single item to handle the
       common case.
    */
    const struct cell_contents *contents;
    size_t n_contents;
    struct cell_contents inline_contents;

    /* Called to free the cell's data, if nonnull. */
    void (*destructor) (void *destructor_aux);
    void *destructor_aux;
  };

void table_cell_free (struct table_cell *);

/* Returns the number of columns that CELL spans.  This is 1 for an ordinary
   cell and greater than one for a cell that joins multiple columns. */
static inline int
table_cell_colspan (const struct table_cell *cell)
{
  return cell->d[TABLE_HORZ][1] - cell->d[TABLE_HORZ][0];
}

/* Returns the number of rows that CELL spans.  This is 1 for an ordinary cell
   and greater than one for a cell that joins multiple rows. */
static inline int
table_cell_rowspan (const struct table_cell *cell)
{
  return cell->d[TABLE_VERT][1] - cell->d[TABLE_VERT][0];
}

/* Returns true if CELL is a joined cell, that is, if it spans multiple rows
   or columns.  Otherwise, returns false. */
static inline bool
table_cell_is_joined (const struct table_cell *cell)
{
  return table_cell_colspan (cell) > 1 || table_cell_rowspan (cell) > 1;
}

/* Declarations to allow defining table classes. */

struct table_class
  {
    /* Frees TABLE.

       The table class may assume that any cells that were retrieved by calling
       the 'get_cell' function have been freed (by calling their destructors)
       before this function is called. */
    void (*destroy) (struct table *table);

    /* Initializes CELL with the contents of the table cell at column X and row
       Y within TABLE.  All members of CELL must be initialized, except that if
       'destructor' is set to a null pointer, then 'destructor_aux' need not be
       initialized.  The 'contents' member of CELL must be set to a nonnull
       value.

       The table class must allow any number of cells in the table to be
       retrieved simultaneously; that is, TABLE must not assume that a given
       cell will be freed before another one is retrieved using 'get_cell'.

       The table class must allow joined cells to be retrieved, with identical
       contents, using any (X,Y) location inside the cell.

       The table class must not allow cells to overlap.

       The table class should not allow a joined cell to cross the border
       between header rows/columns and the interior of the table.  That is, a
       joined cell should be entirely within headers rows and columns or
       entirely outside them.

       The table class may assume that CELL will be freed before TABLE is
       destroyed. */
    void (*get_cell) (const struct table *table, int x, int y,
                      struct table_cell *cell);

    /* Returns one of the TAL_* enumeration constants (declared in
       output/table.h) representing a rule running alongside one of the cells
       in TABLE.

       See table_get_rule() in table.c for a detailed explanation of the
       meaning of AXIS and X and Y, including a diagram. */
    int (*get_rule) (const struct table *table,
                     enum table_axis axis, int x, int y);

    /* This function is optional and most table classes will not implement it.

       If provided, this function must take ownership of A and B and return a
       table that consists of tables A and B "pasted together", that is, a
       table whose size is the sum of the sizes of A and B along the axis
       specified by ORIENTATION.  A and B will ordinarily have the same size
       along the axis opposite ORIENTATION; no particular handling of tables
       that have different sizes along that axis is required.

       The handling of rules at the seam between A and B is not specified, but
       table_rule_combine() is one reasonable way to do it.

       Called only if neither A and B is shared (as returned by
       table_is_shared()).

       Called if A or B or both is of the class defined by this table class.
       That is, the implementation must be prepared to deal with the case where
       A or B is not the ordinarily expected table class.

       This function may return a null pointer if it cannot implement the paste
       operation, in which case the caller will use a fallback
       implementation.

       This function is used to implement table_paste(). */
    struct table *(*paste) (struct table *a, struct table *b,
                            enum table_axis orientation);

    /* This function is optional and most table classes will not implement it.

       If provided, this function must take ownership of TABLE and return a new
       table whose contents are the TABLE's rows RECT[TABLE_VERT][0] through
       RECT[TABLE_VERT][1], exclusive, and the TABLE's columns
       RECT[TABLE_HORZ][0] through RECT[TABLE_HORZ][1].

       Called only if TABLE is not shared (as returned by table_is_shared()).

       This function may return a null pointer if it cannot implement the
       select operation, in which case the caller will use a fallback
       implementation.

       This function is used to implement table_select(). */
    struct table *(*select) (struct table *table, int rect[TABLE_N_AXES][2]);
  };

void table_init (struct table *, const struct table_class *);

/* Table class implementations can call these functions or just set the
   table's n[] and h[][] members directly. */
void table_set_nc (struct table *, int nc);
void table_set_nr (struct table *, int nr);

/* For use primarily by output drivers. */

void table_get_cell (const struct table *, int x, int y, struct table_cell *);
int table_get_rule (const struct table *, enum table_axis, int x, int y);

#endif /* output/table-provider.h */
