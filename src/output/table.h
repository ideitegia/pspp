/* PSPP - a program for statistical analysis.
   Copyright (C) 1997, 1998, 1999, 2000, 2009, 2013, 2014 Free Software Foundation, Inc.

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

#ifndef OUTPUT_TABLE_H
#define OUTPUT_TABLE_H 1

/* Tables.

.  A table is a rectangular grid of cells.  Cells can be joined to form larger
   cells.  Rows and columns can be separated by rules of various types.  Rows
   at the top and bottom of a table and columns at the left and right edges of
   a table can be designated as headers, which means that if the table must be
   broken across more than one page, those rows or columns are repeated on each
   page.

   Every table is an instance of a particular table class that is responsible
   for keeping track of cell data.  By far the most common table class is
   struct tab_table (see output/tab.h).  This header also declares some other
   kinds of table classes, near the end of the file.

   A table is not itself an output_item, and thus a table cannot by itself be
   used for output, but they can be embedded inside struct table_item (see
   table-item.h) for that purpose. */

#include <stdbool.h>
#include <stddef.h>

struct casereader;
struct fmt_spec;
struct table_item;
struct variable;

/* Properties of a table cell. */
enum
  {
    TAB_NONE = 0,

    /* Alignment of cell contents. */
    TAB_RIGHT      = 0 << 0,    /* Right justify. */
    TAB_LEFT       = 1 << 0,    /* Left justify. */
    TAB_CENTER     = 2 << 0,    /* Centered. */
    TAB_ALIGNMENT  = 3 << 0,	/* Alignment mask. */

    /* These flags may be combined with any alignment. */
    TAB_EMPH       = 1 << 2,    /* Emphasize cell contents. */
    TAB_FIX        = 1 << 3,    /* Use fixed font. */

    /* Bits with values (1 << TAB_FIRST_AVAILABLE) and higher are
       not used, so they are available for subclasses to use as
       they wish. */
    TAB_FIRST_AVAILABLE = 4
  };

/* Styles for the rules around table cells. */
enum
  {
    TAL_0,			/* No line. */
    TAL_GAP,                    /* Spacing but no line. */
    TAL_1,			/* Single line. */
    TAL_2,			/* Double line. */
    N_LINES
  };

/* Given line styles A and B (each one of the TAL_* enumeration constants
   above), returns a line style that "combines" them, that is, that gives a
   reasonable line style choice for a rule for different reasons should have
   both styles A and B.

   Used especially for pasting tables together (see table_paste()). */
static inline int table_rule_combine (int a, int b)
{
  return a > b ? a : b;
}

/* A table axis.

   Many table-related declarations use 2-element arrays in place of "x" and "y"
   variables.  This reduces code duplication significantly, because much table
   code has treat rows and columns the same way.

   A lot of code that uses these enumerations assumes that the two values are 0
   and 1, so don't change them to other values. */
enum table_axis
  {
    TABLE_HORZ,
    TABLE_VERT,
    TABLE_N_AXES
  };

/* A table. */
struct table
  {
    const struct table_class *klass;

    /* Table size.

       n[TABLE_HORZ]: Number of columns.
       n[TABLE_VERT]: Number of rows. */
    int n[TABLE_N_AXES];

    /* Table headers.

       Rows at the top and bottom of a table and columns at the left and right
       edges of a table can be designated as headers.  If the table must be
       broken across more than one page for output, headers rows and columns
       are repeated on each page.

       h[TABLE_HORZ][0]: Left header columns.
       h[TABLE_HORZ][1]: Right header columns.
       h[TABLE_VERT][0]: Top header rows.
       h[TABLE_VERT][1]: Bottom header rows. */
    int h[TABLE_N_AXES][2];

    /* Reference count.  A table may be shared between multiple owners,
       indicated by a reference count greater than 1.  When this is the case,
       the table must not be modified. */
    int ref_cnt;
  };

/* Reference counting. */
struct table *table_ref (const struct table *);
void table_unref (struct table *);
bool table_is_shared (const struct table *);
struct table *table_unshare (struct table *);

/* Returns the number of columns or rows, respectively, in T. */
static inline int table_nc (const struct table *t)
        { return t->n[TABLE_HORZ]; }
static inline int table_nr (const struct table *t)
        { return t->n[TABLE_VERT]; }

/* Returns the number of left, right, top, or bottom headers, respectively, in
   T.  */
static inline int table_hl (const struct table *t)
        { return t->h[TABLE_HORZ][0]; }
static inline int table_hr (const struct table *t)
        { return t->h[TABLE_HORZ][1]; }
static inline int table_ht (const struct table *t)
        { return t->h[TABLE_VERT][0]; }
static inline int table_hb (const struct table *t)
        { return t->h[TABLE_VERT][1]; }

/* Set headers. */
void table_set_hl (struct table *, int hl);
void table_set_hr (struct table *, int hr);
void table_set_ht (struct table *, int ht);
void table_set_hb (struct table *, int hb);

/* Table classes. */

/* Simple kinds of tables. */
struct table *table_from_string (unsigned int options, const char *);
struct table *table_from_string_span (unsigned int options, const char *,
                                      int colspan, int rowspan);
struct table *table_from_variables (unsigned int options,
                                    struct variable **, size_t);
struct table *table_from_casereader (const struct casereader *,
                                     size_t column,
                                     const char *heading,
                                     const struct fmt_spec *);
struct table *table_create_nested (struct table *);
struct table *table_create_nested_item (struct table_item *);

/* Combining tables. */
struct table *table_paste (struct table *, struct table *,
                           enum table_axis orientation);
struct table *table_hpaste (struct table *left, struct table *right);
struct table *table_vpaste (struct table *top, struct table *bottom);
struct table *table_stomp (struct table *);

/* Taking subsets of tables. */
struct table *table_select (struct table *, int rect[TABLE_N_AXES][2]);
struct table *table_select_slice (struct table *, enum table_axis,
                                  int z0, int z1, bool add_headers);
struct table *table_select_columns (struct table *,
                                    int x0, int x1, bool add_headers);
struct table *table_select_rows (struct table *,
                                 int y0, int y1, bool add_headers);

/* Miscellaneous table operations. */
struct table *table_transpose (struct table *);

#endif /* output/table.h */
