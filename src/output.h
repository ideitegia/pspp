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

#if !output_h
#define output_h 1

#include "str.h"

/* A rectangle. */
struct rect
  {
    int x1, y1;			/* Upper left. */
    int x2, y2;			/* Lower right, not part of the rectangle. */
  };

#if __GNUC__ > 1 && defined(__OPTIMIZE__)
extern inline int width (rect r) __attribute__ ((const));
extern inline int height (rect r) __attribute__ ((const));

extern inline int
width (rect r)
{
  return r.x2 - r.x1 + 1;
}

extern inline int 
height (rect r)
{
  return r.y2 - r.y1 + 1;
}
#else /* !__GNUC__ */
#define width(R) 				\
	((R).x2 - (R).x1 + 1)
#define height(R) 				\
	((R).y2 - (R).y1 + 1)
#endif /* !__GNUC__ */

/* Color descriptor. */
struct color
  {
    int flags;			/* 0=normal, 1=transparent (ignore r,g,b). */
    int r;			/* Red component, 0-65535. */
    int g;			/* Green component, 0-65535. */
    int b;			/* Blue component, 0-65535. */
  };

/* Mount positions for the four basic fonts.  Do not change the values. */
enum
  {
    OUTP_F_R,			/* Roman font. */
    OUTP_F_I,			/* Italic font. */
    OUTP_F_B,			/* Bold font. */
    OUTP_F_BI			/* Bold-italic font. */
  };

/* Line styles.  These must match:
   som.h:SLIN_*
   ascii.c:ascii_line_*() 
   postscript.c:ps_line_*() */
enum
  {
    OUTP_L_NONE = 0,		/* No line. */
    OUTP_L_SINGLE = 1,		/* Single line. */
    OUTP_L_DOUBLE = 2,		/* Double line. */
    OUTP_L_SPECIAL = 3,		/* Special line of driver-defined style. */

    OUTP_L_COUNT		/* Number of line styles. */
  };

/* Contains a line style for each part of an intersection. */
struct outp_styles
  {
    int l;			/* left */
    int t;			/* top */
    int r;			/* right */
    int b;			/* bottom */
  };

/* Text display options. */
enum
  {
    OUTP_T_NONE = 0,

    /* Must match tab.h:TAB_*. */
    OUTP_T_JUST_MASK = 00003,	/* Justification mask. */
    OUTP_T_JUST_RIGHT = 00000,	/* Right justification. */
    OUTP_T_JUST_LEFT = 00001,	/* Left justification. */
    OUTP_T_JUST_CENTER = 00002,	/* Center justification. */

    OUTP_T_HORZ = 00010,	/* Horizontal size is specified. */
    OUTP_T_VERT = 00020,	/* (Max) vertical size is specified. */

    OUTP_T_0 = 00140,		/* Normal orientation. */
    OUTP_T_CC90 = 00040,	/* 90 degrees counterclockwise. */
    OUTP_T_CC180 = 00100,	/* 180 degrees counterclockwise. */
    OUTP_T_CC270 = 00140,	/* 270 degrees counterclockwise. */
    OUTP_T_C90 = 00140,		/* 90 degrees clockwise. */
    OUTP_T_C180 = 00100,	/* 180 degrees clockwise. */
    OUTP_T_C270 = 00040,	/* 270 degrees clockwise. */

    /* Internal use by drivers only. */
    OUTP_T_INTERNAL_DRAW = 01000	/* 1=Draw the text, 0=Metrics only. */
  };

/* Describes text output. */
struct outp_text
  {
    /* Public. */
    int options;		/* What is specified. */
    struct len_string s;	/* String. */
    int h, v;			/* Horizontal, vertical size. */
    int x, y;			/* Position. */

    /* Internal use only. */
    int w, l;			/* Width, length. */
  };

struct som_table;
struct outp_driver;

/* Defines a class of output driver. */
struct outp_class
  {
    /* Basic class information. */
    const char *name;		/* Name of this driver class. */
    int magic;			/* Driver-specific constant. */
    int special;		/* Boolean value. */

    /* Static member functions. */
    int (*open_global) (struct outp_class *);
    int (*close_global) (struct outp_class *);
    int *(*font_sizes) (struct outp_class *, int *n_valid_sizes);

    /* Virtual member functions. */
    int (*preopen_driver) (struct outp_driver *);
    void (*option) (struct outp_driver *, const char *key,
		    const struct string *value);
    int (*postopen_driver) (struct outp_driver *);
    int (*close_driver) (struct outp_driver *);

    int (*open_page) (struct outp_driver *);
    int (*close_page) (struct outp_driver *);

    /* special != 0: Used to submit tables for output. */
    void (*submit) (struct outp_driver *, struct som_table *);
    
    /* special != 0: Methods below need not be defined. */
    
    /* Line methods. */
    void (*line_horz) (struct outp_driver *, const struct rect *,
		       const struct color *, int style);
    void (*line_vert) (struct outp_driver *, const struct rect *,
		       const struct color *, int style);
    void (*line_intersection) (struct outp_driver *, const struct rect *,
			       const struct color *,
			       const struct outp_styles *style);

    /* Drawing methods. */
    void (*box) (struct outp_driver *, const struct rect *,
		 const struct color *bord, const struct color *fill);
    void (*polyline_begin) (struct outp_driver *, const struct color *);
    void (*polyline_point) (struct outp_driver *, int, int);
    void (*polyline_end) (struct outp_driver *);

    /* Text methods. */
    void (*text_set_font_by_name) (struct outp_driver *, const char *s);
    void (*text_set_font_by_position) (struct outp_driver *, int);
    void (*text_set_font_family) (struct outp_driver *, const char *s);
    const char *(*text_get_font_name) (struct outp_driver *);
    const char *(*text_get_font_family) (struct outp_driver *);
    int (*text_set_size) (struct outp_driver *, int);
    int (*text_get_size) (struct outp_driver *, int *em_width);
    void (*text_metrics) (struct outp_driver *, struct outp_text *);
    void (*text_draw) (struct outp_driver *, struct outp_text *);
  };

/* Device types. */
enum
  {
    OUTP_DEV_NONE = 0,		/* None of the below. */
    OUTP_DEV_LISTING = 001,	/* Listing device. */
    OUTP_DEV_SCREEN = 002,	/* Screen device. */
    OUTP_DEV_PRINTER = 004,	/* Printer device. */
    OUTP_DEV_DISABLED = 010	/* Broken device. */
  };

/* Defines the configuration of an output driver. */
struct outp_driver
  {
    struct outp_class *class;		/* Driver class. */
    char *name;			/* Name of this driver. */
    int driver_open;		/* 1=driver is open, 0=driver is closed. */
    int page_open;		/* 1=page is open, 0=page is closed. */

    struct outp_driver *next, *prev;	/* Next, previous output driver in list. */

    int device;			/* Zero or more of OUTP_DEV_*. */
    int res, horiz, vert;	/* Device resolution. */
    int width, length;		/* Page size. */

    int cp_x, cp_y;		/* Current position. */
    int font_height;		/* Default font character height. */
    int prop_em_width;		/* Proportional font em width. */
    int fixed_width;		/* Fixed-pitch font character width. */
    int horiz_line_width[OUTP_L_COUNT];	/* Width of horizontal lines. */
    int vert_line_width[OUTP_L_COUNT];	/* Width of vertical lines. */
    int horiz_line_spacing[1 << OUTP_L_COUNT];
    int vert_line_spacing[1 << OUTP_L_COUNT];

    void *ext;			/* Private extension record. */
    void *prc;			/* Per-procedure extension record. */
  };

/* Option structure for the keyword recognizer. */
struct outp_option
  {
    const char *keyword;	/* Keyword name. */
    int cat;			/* Category. */
    int subcat;			/* Subcategory. */
  };

/* Information structure for the keyword recognizer. */
struct outp_option_info
  {
    char *initial;			/* Initial characters. */
    struct outp_option **options;	/* Search starting points. */
  };

/* A list of driver classes. */
struct outp_driver_class_list
  {
    int ref_count;
    struct outp_class *class;
    struct outp_driver_class_list *next;
  };

/* List of known output driver classes. */
extern struct outp_driver_class_list *outp_class_list;

/* List of configured output drivers. */
extern struct outp_driver *outp_driver_list;

/* Title, subtitle. */
extern char *outp_title;
extern char *outp_subtitle;

int outp_init (void);
int outp_read_devices (void);
int outp_done (void);

void outp_configure_clear (void);
void outp_configure_add (char *);
void outp_configure_macro (char *);

void outp_list_classes (void);

void outp_enable_device (int enable, int device);
struct outp_driver *outp_drivers (struct outp_driver *);

int outp_match_keyword (const char *, struct outp_option *,
			struct outp_option_info *, int *);

int outp_evaluate_dimension (char *, char **);
int outp_get_paper_size (char *, int *h, int *v);

int outp_eject_page (struct outp_driver *);

int outp_string_width (struct outp_driver *, const char *);

/* Imported from som-frnt.c. */
void som_destroy_driver (struct outp_driver *);

#endif /* output.h */
