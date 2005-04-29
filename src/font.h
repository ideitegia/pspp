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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#if !font_h
#define font_h 1

/* Possible ligatures. */
#define LIG_ff  001
#define LIG_ffi 002
#define LIG_ffl 004
#define LIG_fi  010
#define LIG_fl  020

/* Character type constants. */
#define CTYP_NONE	000	/* Neither ascenders nor descenders. */
#define CTYP_ASCENDER	001	/* Character has an ascender. */
#define CTYP_DESCENDER	002	/* Character has a descender. */

/* Font metrics for a single character.  */
struct char_metrics
  {
    int code;			/* Character code. */
    int type;			/* CTYP_* constants. */
    int width;			/* Width. */
    int height;			/* Height above baseline, never negative. */
    int depth;			/* Depth below baseline, never negative. */

    /* These fields are not yet used, so to save memory, they are left
       out. */
#if 0
    int italic_correction;	/* Italic correction. */
    int left_italic_correction;	/* Left italic correction. */
    int subscript_correction;	/* Subscript correction. */
#endif
  };

/* Kerning for a pair of characters.  */
struct kern_pair
  {
    int ch1;			/* First character. */
    int ch2;			/* Second character. */
    int adjust;			/* Kern amount. */
  };

/* Font description.  */
struct font_desc
  {
    /* Housekeeping data. */
    struct pool *owner;		/* Containing pool. */
    char *name;			/* Font name.  FIXME: this field's
				   role is uncertain. */
    char *filename;		/* Normalized filename. */

    /* PostScript-specific courtesy data. */
    char *internal_name;	/* Font internal name. */
    char *encoding;		/* Name of encoding file. */

    /* Basic font characteristics. */
    int space_width;		/* Width of a space character. */
    double slant;		/* Slant angle, in degrees of forward slant. */
    unsigned ligatures;		/* Characters that have ligatures. */
    int special;		/* 1=This is a special font that will be
				   searched when a character is not present in
				   another font. */
    int ascent, descent;	/* Height above, below the baseline. */

    /* First dereferencing level is font_char_name_to_index(NAME). */
    /* Second dereferencing level. */
    short *deref;		/* Each entry is an index into metric.
				   metric[deref[lookup(NAME)]] is the metric
				   for character with name NAME. */
    int deref_size;		/* Number of spaces for entries in deref. */

    /* Third dereferencing level. */
    struct char_metrics **metric;	/* Metrics for font characters. */
    int metric_size;		/* Number of spaces for entries in metric. */
    int metric_used;		/* Number of spaces used in metric. */

    /* Kern pairs. */
    struct kern_pair *kern;	/* Hash table for kerns. */
    int kern_size;		/* Number of spaces for kerns in kern. */
    int *kern_size_p;		/* Next larger hash table size. */
    int kern_used;		/* Number of used spaces in kern. */
    int kern_max_used;		/* Max number used before rehashing. */
  };

/* Index into deref[] of character with name "space". */
extern int space_index;

/* A set of fonts. */
struct font_set
  {
    struct font_set *next, *prev;	/* Next, previous in chain. */
    struct font_desc *font;		/* Current font. */
  };

/* Functions to work with any font. */
#define destroy_font(FONT) 			\
	pool_destroy (FONT->owner)

int font_char_name_to_index (const char *);
struct char_metrics *font_get_char_metrics (const struct font_desc *font,
					    int ch);
int font_get_kern_adjust (const struct font_desc *font, int ch1, int ch2);

/* groff fonts. */
struct groff_device_info
  {
    /* See groff_font(5). */
    int res, horiz, vert;
    int size_scale, unit_width;
    int (*sizes)[2], n_sizes;
    char *font_name[4];		/* Names of 4 default fonts. */
    char *family;		/* Name of default font family. */
  };

struct outp_driver;
struct font_desc *groff_read_font (const char *fn);
struct font_desc *groff_find_font (const char *dev, const char *name);
int groff_read_DESC (const char *dev_name, struct groff_device_info * dev);
void groff_init (void);
void groff_done (void);

struct font_desc *default_font (void);

#endif /* font_h */
