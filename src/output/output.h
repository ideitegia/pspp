/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009 Free Software Foundation, Inc.

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

#ifndef OUTPUT_OUTPUT_H
#define OUTPUT_OUTPUT_H 1

#include <libpspp/ll.h>
#include <libpspp/str.h>

/* Line styles.  */
enum outp_line_style
  {
    OUTP_L_NONE,		/* No line. */
    OUTP_L_SINGLE,		/* Single line. */
    OUTP_L_DOUBLE,		/* Double line. */
    OUTP_L_COUNT
  };

/* Text justification. */
enum outp_justification
  {
    OUTP_RIGHT,                 /* Right justification. */
    OUTP_LEFT,                  /* Left justification. */
    OUTP_CENTER,                /* Center justification. */
  };

enum outp_font
  {
    OUTP_FIXED,                 /* Fixed-width font. */
    OUTP_PROPORTIONAL,          /* Proportional font. */
    OUTP_EMPHASIS,              /* Proportional font used for emphasis. */
    OUTP_FONT_CNT               /* Number of fonts. */
  };

/* Describes text output. */
struct outp_text
  {
    enum outp_font font;
    enum outp_justification justification;
    struct substring string;
    int h, v;			/* Horizontal, vertical size. */
    int x, y;			/* Position. */
  };

struct som_entity;
struct outp_driver;
struct chart;

/* Defines a class of output driver. */
struct outp_class
  {
    const char *name;		/* Name of this driver class. */
    int special;		/* Boolean value. */

    bool (*open_driver) (const char *name, int types,
                         struct substring options);
    bool (*close_driver) (struct outp_driver *);

    void (*open_page) (struct outp_driver *);
    void (*close_page) (struct outp_driver *);

    void (*flush) (struct outp_driver *);

    /* special != 0 only. */
    void (*submit) (struct outp_driver *, struct som_entity *);

    /* special == 0 only.  */
    void (*line) (struct outp_driver *, int x0, int y0, int x1, int y1,
                  enum outp_line_style top, enum outp_line_style left,
                  enum outp_line_style bottom, enum outp_line_style right);
    void (*text_metrics) (struct outp_driver *, const struct outp_text *,
                          int *width, int *height);
    void (*text_draw) (struct outp_driver *, const struct outp_text *);
    void (*initialise_chart)(struct outp_driver *, struct chart *);
    void (*finalise_chart)(struct outp_driver *, struct chart *);
  };

/* Device types. */
enum
  {
    OUTP_DEV_NONE = 0,		/* None of the below. */
    OUTP_DEV_LISTING = 001,	/* Listing device. */
    OUTP_DEV_SCREEN = 002,	/* Screen device. */
    OUTP_DEV_PRINTER = 004,	/* Printer device. */
  };

/* Defines the configuration of an output driver. */
struct outp_driver
  {
    struct ll node;             /* Node in list of drivers. */
    const struct outp_class *class;	/* Driver class. */
    char *name;			/* Name of this driver. */
    bool page_open;		/* 1=page is open, 0=page is closed. */
    int device;			/* Zero or more of OUTP_DEV_*. */
    int cp_x, cp_y;		/* Current position. */

    int width, length;		/* Page size. */
    int font_height;		/* Default font character height. */
    int prop_em_width;		/* Proportional font em width. */
    int fixed_width;		/* Fixed-pitch font character width. */
    int horiz_line_width[OUTP_L_COUNT];	/* Width of horizontal lines. */
    int vert_line_width[OUTP_L_COUNT];	/* Width of vertical lines. */

    void *ext;			/* Private extension record. */
  };

/* Option structure for the keyword recognizer. */
struct outp_option
  {
    const char *keyword;	/* Keyword name. */
    int cat;			/* Category. */
    int subcat;			/* Subcategory. */
  };


/* Title, subtitle. */
extern char *outp_title;
extern char *outp_subtitle;

void outp_init (void);
void outp_done (void);
void outp_read_devices (void);
void outp_configure_driver_line (struct substring);

struct outp_driver *outp_allocate_driver (const struct outp_class *class,
                                          const char *name, int types);
void outp_free_driver (struct outp_driver *);
void outp_register_driver (struct outp_driver *);
void outp_unregister_driver (struct outp_driver *);

void outp_configure_clear (void);
void outp_configure_add (char *);
void outp_configure_macro (char *);

void outp_list_classes (void);

void outp_enable_device (bool enable, int device);
struct outp_driver *outp_drivers (struct outp_driver *);

bool outp_parse_options (const char *driver_name, struct substring options,
                         bool (*callback) (void *aux, const char *key,
                                           const struct string *value),
                         void *aux);
int outp_match_keyword (const char *, const struct outp_option *, int *);

int outp_evaluate_dimension (const char *);
bool outp_get_paper_size (const char *, int *h, int *v);

void outp_open_page (struct outp_driver *);
void outp_close_page (struct outp_driver *);
void outp_eject_page (struct outp_driver *);
void outp_flush (struct outp_driver *);

int outp_string_width (struct outp_driver *, const char *, enum outp_font);

/* Imported from som-frnt.c. */
void som_destroy_driver (struct outp_driver *);

/* Common drivers. */
extern const struct outp_class ascii_class;
extern const struct outp_class postscript_class;
#ifdef HAVE_CAIRO
extern const struct outp_class cairo_class;
#endif

#endif /* output/output.h */
