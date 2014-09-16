/* PSPP - a program for statistical analysis.
   Copyright (C) 1997, 1998, 1999, 2000, 2009, 2011, 2014 Free Software Foundation, Inc.

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

#ifndef OUTPUT_TAB_H
#define OUTPUT_TAB_H

/* Simple table class.

   This is a type of table (see output/table.h) whose content is composed
   manually by the code that generates it, by filling in cells one by one.

   Some of the features of this type of table are obsolete but have not yet
   been removed, because some code still uses them.  These features are:

       - The title and caption.  These are properties of the table_item (see
         output/table-item.h) in which a table is embedded, not properties of
         the table itself.

       - Row and columns offsets (via tab_offset(), tab_next_row()).  This
         feature simply isn't used enough to justify keeping it.

       - Table resizing.  The code that does use this feature is just as well
         served by creating multiple tables and pasting them together with
         table_paste().  Eliminating this feature would also slightly simplify
         the table code here.
*/

#include "libpspp/compiler.h"
#include "output/table.h"
#include "data/format.h"

enum result_class
  {
    RC_INTEGER,
    RC_WEIGHT,
    RC_PVALUE,
    RC_OTHER,
    n_RC
  };

/* A table. */
struct tab_table
  {
    struct table table;
    struct pool *container;

    /* Table title and caption, or null. */
    char *title, *caption;
    int cf;			/* Column factor for indexing purposes. */

    /* Table contents.

       Each array element in cc[] is ordinarily a "char *" pointer to a
       string.  If TAB_JOIN (defined in tab.c) is set in ct[] for the element,
       however, it is a joined cell and the corresponding element of cc[]
       points to a struct tab_joined_cell. */
    void **cc;                  /* Cell contents; void *[nr][nc]. */
    unsigned char *ct;		/* Cell types; unsigned char[nr][nc]. */

    /* Rules. */
    unsigned char *rh;		/* Horiz rules; unsigned char[nr+1][nc]. */
    unsigned char *rv;		/* Vert rules; unsigned char[nr][nc+1]. */

    /* X and Y offsets. */
    int col_ofs, row_ofs;

    struct fmt_spec fmtmap [n_RC];
  };

struct tab_table *tab_cast (const struct table *);

/* Number of rows or columns in TABLE. */
static inline int tab_nr (const struct tab_table *table)
        { return table_nr (&table->table); }
static inline int tab_nc (const struct tab_table *table)
        { return table_nc (&table->table); }

/* Number of left/right/top/bottom header columns/rows in TABLE. */
static inline int tab_l (const struct tab_table *table)
        { return table_hl (&table->table); }
static inline int tab_r (const struct tab_table *table)
        { return table_hr (&table->table); }
static inline int tab_t (const struct tab_table *table)
        { return table_ht (&table->table); }
static inline int tab_b (const struct tab_table *table)
        { return table_hb (&table->table); }

/* Tables. */
struct tab_table *tab_create (int nc, int nr);
void tab_resize (struct tab_table *, int nc, int nr);
void tab_realloc (struct tab_table *, int nc, int nr);
void tab_headers (struct tab_table *, int l, int r, int t, int b);
void tab_title (struct tab_table *, const char *, ...)
     PRINTF_FORMAT (2, 3);
void tab_caption (struct tab_table *, const char *, ...)
     PRINTF_FORMAT (2, 3);
void tab_submit (struct tab_table *);

/* Rules. */
void tab_hline (struct tab_table *, int style, int x1, int x2, int y);
void tab_vline (struct tab_table *, int style, int x, int y1, int y2);
void tab_box (struct tab_table *, int f_h, int f_v, int i_h, int i_v,
	      int x1, int y1, int x2, int y2);

/* Obsolete cell options. */
#define TAT_TITLE TAB_EMPH      /* Title attributes. */

void tab_set_format (struct tab_table *, enum result_class, const struct fmt_spec *);


/* Cells. */
struct fmt_spec;
struct dictionary;
union value;
void tab_value (struct tab_table *, int c, int r, unsigned char opt,
		const union value *, const struct variable *,
		const struct fmt_spec *);

void tab_double (struct tab_table *, int c, int r, unsigned char opt,
		 double v, const struct fmt_spec *, enum result_class );

void tab_text (struct tab_table *, int c, int r, unsigned opt, const char *);
void tab_text_format (struct tab_table *, int c, int r, unsigned opt,
                      const char *, ...)
     PRINTF_FORMAT (5, 6);

void tab_joint_text (struct tab_table *, int x1, int y1, int x2, int y2,
		     unsigned opt, const char *);
void tab_joint_text_format (struct tab_table *, int x1, int y1, int x2, int y2,
                            unsigned opt, const char *, ...)
     PRINTF_FORMAT (7, 8);

void tab_footnote (struct tab_table *, int x, int y, const char *format, ...)
  PRINTF_FORMAT (4, 5);

void tab_subtable (struct tab_table *, int x1, int y1, int x2, int y2,
                   unsigned opt, struct table_item *subtable);
void tab_subtable_bare (struct tab_table *, int x1, int y1, int x2, int y2,
                        unsigned opt, struct table_item *subtable);

bool tab_cell_is_empty (const struct tab_table *, int c, int r);

/* Editing. */
void tab_offset (struct tab_table *, int col, int row);
void tab_next_row (struct tab_table *);

/* Current row/column offset. */
#define tab_row(TABLE) ((TABLE)->row_ofs)
#define tab_col(TABLE) ((TABLE)->col_ofs)

/* Simple output. */
void tab_output_text (int options, const char *string);
void tab_output_text_format (int options, const char *, ...)
     PRINTF_FORMAT (2, 3);

#endif /* output/tab.h */

