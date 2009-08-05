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

#if !tab_h
#define tab_h 1

#include <limits.h>
#include <libpspp/str.h>

/* Cell options. */
enum
  {
    TAB_NONE = 0,

    TAB_ALIGN_MASK = 03,	/* Alignment mask. */
    TAB_RIGHT = 00,		/* Right justify. */
    TAB_LEFT = 01,		/* Left justify. */
    TAB_CENTER = 02,		/* Center. */

    /* Cell types. */
    TAB_JOIN = 004,		/* Joined cell. */
    TAB_EMPTY = 010,		/* Empty cell. */

    /* Flags. */
    TAB_EMPH = 020,             /* Emphasize cell contents. */
    TAB_FIX = 040,              /* Use fixed font. */
  };

/* Line styles. */
enum
  {
    TAL_0 = 0,			/* No line. */
    TAL_1 = 1,			/* Single line. */
    TAL_2 = 2,			/* Double line. */
    TAL_GAP = 3,                /* Spacing but no line. */
    TAL_COUNT,			/* Number of line styles. */
  };

/* Column styles.  Must correspond to SOM_COL_*. */
enum
  {
    TAB_COL_NONE,			/* No columns. */
    TAB_COL_DOWN			/* Columns down first. */
  };

/* Joined cell. */
struct tab_joined_cell
  {
    int x1, y1;
    int x2, y2;
    struct substring contents;
  };

struct outp_driver;
struct tab_table;
struct tab_rendering;

typedef void tab_dim_func (struct tab_rendering *, void *aux);
typedef void tab_dim_free_func (void *aux);

/* A table. */
struct tab_table
  {
    struct pool *container;
    int ref_cnt;                /* Reference count. */

    /* Contents. */
    int col_style;		/* Columns: One of TAB_COL_*. */
    int col_group;		/* Number of rows per column group. */
    char *title;                /* Table title. */
    unsigned flags;		/* SOMF_*. */
    int nc, nr;			/* Number of columns, rows. */
    int cf;			/* Column factor for indexing purposes. */
    int l, r, t, b;		/* Number of header rows on each side. */
    struct substring *cc;	/* Cell contents; substring *[nr][nc]. */
    unsigned char *ct;		/* Cell types; unsigned char[nr][nc]. */
    unsigned char *rh;		/* Horiz rules; unsigned char[nr+1][nc]. */
    unsigned char *rv;		/* Vert rules; unsigned char[nr][nc+1]. */

    /* Calculating row and column dimensions. */
    tab_dim_func *dim;		/* Calculates cell widths and heights. */
    tab_dim_free_func *dim_free; /* Frees space allocated for dim function. */
    void *dim_aux;              /* Auxiliary data for dim function. */

    /* Editing info. */
    int col_ofs, row_ofs;	/* X and Y offsets. */
  };

/* Number of rows or columns in TABLE. */
static inline int tab_nr (const struct tab_table *table) { return table->nr; }
static inline int tab_nc (const struct tab_table *table) { return table->nc; }

/* Number of left/right/top/bottom header columns/rows in TABLE. */
static inline int tab_l (const struct tab_table *table) { return table->l; }
static inline int tab_r (const struct tab_table *table) { return table->r; }
static inline int tab_t (const struct tab_table *table) { return table->t; }
static inline int tab_b (const struct tab_table *table) { return table->b; }

struct tab_rendering
  {
    const struct tab_table *table;
    struct outp_driver *driver;

    int *w;			/* Column widths; [nc]. */
    int *h;			/* Row heights; [nr]. */
    int *hrh;			/* Heights of horizontal rules; [nr+1]. */
    int *wrv;			/* Widths of vertical rules; [nc+1]. */

    /* These fields would be redundant with those in struct tab_table, except
       that a table will be rendered with fewer header rows or columns than
       requested when we are pressed for space. */
    int l, r, t, b;		/* Number of header rows/columns. */
    int wl, wr, ht, hb;		/* Width/height of header rows/columns. */
  };

/* Tables. */
struct tab_table *tab_create (int nc, int nr, int reallocable);
void tab_destroy (struct tab_table *);
void tab_ref (struct tab_table *);
void tab_resize (struct tab_table *, int nc, int nr);
void tab_realloc (struct tab_table *, int nc, int nr);
void tab_headers (struct tab_table *, int l, int r, int t, int b);
void tab_columns (struct tab_table *, int style, int group);
void tab_title (struct tab_table *, const char *, ...)
     PRINTF_FORMAT (2, 3);
void tab_flags (struct tab_table *, unsigned);
void tab_submit (struct tab_table *);

/* Dimensioning. */
tab_dim_func tab_natural_dimensions;
int tab_natural_width (const struct tab_rendering *, int c);
int tab_natural_height (const struct tab_rendering *, int r);
void tab_dim (struct tab_table *,
              tab_dim_func *, tab_dim_free_func *, void *aux);

/* Rules. */
void tab_hline (struct tab_table *, int style, int x1, int x2, int y);
void tab_vline (struct tab_table *, int style, int x, int y1, int y2);
void tab_box (struct tab_table *, int f_h, int f_v, int i_h, int i_v,
	      int x1, int y1, int x2, int y2);

/* Text options, passed in the `opt' argument. */
enum
  {
    TAT_NONE = 0,		/* No options. */
    TAT_PRINTF = 0x0100,	/* Format the text string with sprintf. */
    TAT_TITLE = 0x0200 | TAB_EMPH, /* Title attributes. */
    TAT_NOWRAP = 0x0800         /* No text wrap (tab_output_text() only). */
  };

/* Cells. */
struct fmt_spec;
union value;
void tab_value (struct tab_table *, int c, int r, unsigned char opt,
		const union value *, const struct fmt_spec *);

void tab_fixed (struct tab_table *, int c, int r, unsigned char opt,
		double v, int w, int d);

void tab_double (struct tab_table *, int c, int r, unsigned char opt,
		double v, const struct fmt_spec *);

void tab_text (struct tab_table *, int c, int r, unsigned opt,
	       const char *, ...)
     PRINTF_FORMAT (5, 6);
void tab_joint_text (struct tab_table *, int x1, int y1, int x2, int y2,
		     unsigned opt, const char *, ...)
     PRINTF_FORMAT (7, 8);

/* Cell low-level access. */
#define tab_alloc(TABLE, AMT) pool_alloc ((TABLE)->container, (AMT))
void tab_raw (struct tab_table *, int c, int r, unsigned opt,
	      struct substring *);

/* Editing. */
void tab_offset (struct tab_table *, int col, int row);
void tab_next_row (struct tab_table *);

/* Current row/column offset. */
#define tab_row(TABLE) ((TABLE)->row_ofs)
#define tab_col(TABLE) ((TABLE)->col_ofs)

/* Simple output. */
void tab_output_text (int options, const char *string, ...)
     PRINTF_FORMAT (2, 3);

#endif /* tab_h */

