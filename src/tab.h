/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#if !tab_h
#define tab_h 1

#include <limits.h>
#include "str.h"

/* Cell options. */
enum
  {
    TAB_NONE = 0,

    /* Must match output.h: OUTP_T_JUST_*. */
    TAB_ALIGN_MASK = 03,	/* Alignment mask. */
    TAB_RIGHT = 00,		/* Right justify. */
    TAB_LEFT = 01,		/* Left justify. */
    TAB_CENTER = 02,		/* Center. */

    /* Oddball cell types. */
    TAB_JOIN = 010,		/* Joined cell. */
    TAB_EMPTY = 020		/* Empty cell. */
  };

/* Line styles.  These must match output.h:OUTP_L_*. */
enum
  {
    TAL_0 = 0,			/* No line. */
    TAL_1 = 1,			/* Single line. */
    TAL_2 = 2,			/* Double line. */
    TAL_3 = 3,			/* Special line of driver-defined style. */
    TAL_COUNT,			/* Number of line styles. */

    TAL_SPACING = 0200		/* Don't draw the line, just reserve space. */
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
    int hit;
    struct fixed_string contents;
  };

struct outp_driver;
struct tab_table;
typedef void tab_dim_func (struct tab_table *, struct outp_driver *);

/* A table. */
struct tab_table
  {
    struct pool *container;
    
    /* Contents. */
    int col_style;		/* Columns: One of TAB_COL_*. */
    int col_group;		/* Number of rows per column group. */
    struct fixed_string title;	/* Table title. */
    unsigned flags;		/* SOMF_*. */
    int nc, nr;			/* Number of columns, rows. */
    int cf;			/* Column factor for indexing purposes. */
    int l, r, t, b;		/* Number of header rows on each side. */
    struct fixed_string *cc;	/* Cell contents; fixed_string *[nr][nc]. */
    unsigned char *ct;		/* Cell types; unsigned char[nr][nc]. */
    unsigned char *rh;		/* Horiz rules; unsigned char[nr+1][nc]. */
    unsigned char *trh;		/* Types of horiz rules; [nr+1]. */
    unsigned char *rv;		/* Vert rules; unsigned char[nr][nc+1]. */
    unsigned char *trv;		/* Types of vert rules; [nc+1]. */
    tab_dim_func *dim;		/* Calculates cell widths and heights. */

    /* Calculated during output. */
    int *w;			/* Column widths; [nc]. */
    int *h;			/* Row heights; [nr]. */
    int *hrh;			/* Heights of horizontal rules; [nr+1]. */
    int *wrv;			/* Widths of vertical rules; [nc+1]. */
    int wl, wr, ht, hb;		/* Width/height of header rows/columns. */
    int hr_tot, vr_tot;		/* Hrules total height, vrules total width. */

    /* Editing info. */
    int col_ofs, row_ofs;	/* X and Y offsets. */
#if GLOBAL_DEBUGGING
    int reallocable;		/* Can table be reallocated? */
#endif
  };

extern int tab_hit;

/* Number of rows in TABLE. */
#define tab_nr(TABLE) ((TABLE)->nr)

/* Number of columns in TABLE. */
#define tab_nc(TABLE) ((TABLE)->nc)

/* Number of left header columns in TABLE. */
#define tab_l(TABLE) ((TABLE)->l)

/* Number of right header columns in TABLE. */
#define tab_r(TABLE) ((TABLE)->r)

/* Number of top header rows in TABLE. */
#define tab_t(TABLE) ((TABLE)->t)

/* Number of bottom header rows in TABLE. */
#define tab_b(TABLE) ((TABLE)->b)

/* Tables. */
struct tab_table *tab_create (int nc, int nr, int reallocable);
void tab_destroy (struct tab_table *);
void tab_resize (struct tab_table *, int nc, int nr);
void tab_realloc (struct tab_table *, int nc, int nr);
void tab_headers (struct tab_table *, int l, int r, int t, int b);
void tab_columns (struct tab_table *, int style, int group);
void tab_title (struct tab_table *, int format, const char *, ...);
void tab_flags (struct tab_table *, unsigned);
void tab_submit (struct tab_table *);

/* Dimensioning. */
tab_dim_func tab_natural_dimensions;
int tab_natural_width (struct tab_table *t, struct outp_driver *d, int c);
int tab_natural_height (struct tab_table *t, struct outp_driver *d, int r);
void tab_dim (struct tab_table *, tab_dim_func *);

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
    TAT_TITLE = 0x0204,		/* Title attributes. */
    TAT_FIX = 0x0400,		/* Use fixed-pitch font. */
    TAT_NOWRAP = 0x0800         /* No text wrap (tab_output_text() only). */
  };

/* Cells. */
struct fmt_spec;
union value;
void tab_value (struct tab_table *, int c, int r, unsigned char opt,
		const union value *, const struct fmt_spec *);
void tab_float (struct tab_table *, int c, int r, unsigned char opt,
		double v, int w, int d);
void tab_text (struct tab_table *, int c, int r, unsigned opt,
	       const char *, ...)
     PRINTF_FORMAT (5, 6);
void tab_joint_text (struct tab_table *, int x1, int y1, int x2, int y2,
		     unsigned opt, const char *, ...)
     PRINTF_FORMAT (7, 8);

/* Cell low-level access. */
#define tab_alloc(TABLE, AMT) pool_alloc ((TABLE)->container, (AMT))
void tab_raw (struct tab_table *, int c, int r, unsigned opt,
	      struct fixed_string *);

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

