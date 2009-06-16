/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009 Free Software Foundation, Inc.

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

#if !som_h
#define som_h 1

/* Structured Output Manager.

   som considers the output stream to be a series of tables.  Each
   table is made up of a rectangular grid of cells.  Cells can be
   joined to form larger cells.  Rows and columns can be separated by
   rules of various types.  Tables too large to fit on a single page
   will be divided into sections.  Rows and columns can be designated
   as headers, which causes them to be repeated in each section.

   Every table is an instance of a particular table class.  A table
   class is responsible for keeping track of cell data, for handling
   requests from the som, and finally for rendering cell data to the
   output drivers.  Tables may implement these operations in any way
   desired, and in fact almost every operation performed by som may be
   overridden in a table class.  */

#include <stdbool.h>

enum som_type
  {
    SOM_TABLE,
    SOM_CHART
  } ;

/* Entity (Table or Chart) . */
struct som_entity
  {
    const struct som_table_class *class;	/* Table class. */
    enum som_type type;                 /* Table or Chart */
    void *ext;				/* Owned by table or chart class. */
    int table_num;                      /* Table number. */
    int subtable_num;                   /* Sub-table number. */
  };

/* Group styles. */
enum
  {
    SOM_COL_NONE,			/* No columns. */
    SOM_COL_DOWN			/* Columns down first. */
  };

/* Cumulation types. */
enum
  {
    SOM_ROWS,                   /* Rows. */
    SOM_COLUMNS                 /* Columns. */
  };

/* Flags. */
enum
  {
    SOMF_NONE = 0,
    SOMF_NO_SPACING = 01,	/* No spacing before the table. */
    SOMF_NO_TITLE = 02		/* No title. */
  };

/* Table class. */
struct outp_driver;
struct som_table_class
  {
    /* Operations on tables. */
    void (*count) (struct som_entity *, int *n_columns, int *n_rows);
    void (*columns) (struct som_entity *, int *style);
    void (*headers) (struct som_entity *, int *l, int *r, int *t, int *b);
    void (*flags) (struct som_entity *, unsigned *);

    /* Creating and freeing driver-specific table rendering data. */
    void *(*render_init) (struct som_entity *, struct outp_driver *,
                          int l, int r, int t, int b);
    void (*render_free) (void *);

    /* Rendering operations. */
    void (*area) (void *, int *horiz, int *vert);
    void (*cumulate) (void *, int cumtype, int start, int *end,
                      int max, int *actual);
    void (*title) (void *, int x, int y, int table_num, int subtable_num);
    void (*render) (void *, int x1, int y1, int x2, int y2);
  };

/* Submission. */
void som_new_series (void);
void som_submit (struct som_entity *t);

/* Miscellaneous. */
void som_eject_page (void);
void som_blank_line (void);
void som_flush (void);

#endif /* som_h */
