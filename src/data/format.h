/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#ifndef DATA_FORMAT_H
#define DATA_FORMAT_H 1

/* Display format types. */

#include <stdbool.h>
#include "data/val-type.h"
#include "libpspp/str.h"

/* How a format is going to be used. */
enum fmt_use
  {
    FMT_FOR_INPUT,           /* For parsing data input, e.g. data_in(). */
    FMT_FOR_OUTPUT           /* For formatting data output, e.g. data_out(). */
  };

/* Format type categories.

   Each format is in exactly one category.  We give categories
   bitwise disjoint values only to enable bitwise comparisons
   against a mask of FMT_CAT_* values, not to allow multiple
   categories per format. */
enum fmt_category
  {
    /* Numeric formats. */
    FMT_CAT_BASIC          = 0x001,     /* Basic numeric formats. */
    FMT_CAT_CUSTOM         = 0x002,     /* Custom currency formats. */
    FMT_CAT_LEGACY         = 0x004,     /* Legacy numeric formats. */
    FMT_CAT_BINARY         = 0x008,     /* Binary formats. */
    FMT_CAT_HEXADECIMAL    = 0x010,     /* Hexadecimal formats. */
    FMT_CAT_DATE           = 0x020,     /* Date formats. */
    FMT_CAT_TIME           = 0x040,     /* Time formats. */
    FMT_CAT_DATE_COMPONENT = 0x080,     /* Date component formats. */

    /* String formats. */
    FMT_CAT_STRING         = 0x100      /* String formats. */
  };

/* Format type. */
enum fmt_type
  {
#define FMT(NAME, METHOD, IMIN, OMIN, IO, CATEGORY) FMT_##NAME,
#include "format.def"
    FMT_NUMBER_OF_FORMATS,
  };

/* Length of longest format specifier name,
   not including terminating null. */
#define FMT_TYPE_LEN_MAX 8

/* Length of longest string representation of fmt_spec,
   not including terminating null. */
#define FMT_STRING_LEN_MAX 32

/* Display format. */
struct fmt_spec
  {
    enum fmt_type type;		/* One of FMT_*. */
    int w;			/* Width. */
    int d;			/* Number of decimal places. */
  };

/* Maximum width of any numeric format. */
#define FMT_MAX_NUMERIC_WIDTH 40

/* Constructing formats. */
struct fmt_spec fmt_for_input (enum fmt_type, int w, int d) PURE_FUNCTION;
struct fmt_spec fmt_for_output (enum fmt_type, int w, int d) PURE_FUNCTION;
struct fmt_spec fmt_for_output_from_input (const struct fmt_spec *);
struct fmt_spec fmt_default_for_width (int width);

/* Verifying formats. */
bool fmt_check (const struct fmt_spec *, enum fmt_use);
bool fmt_check_input (const struct fmt_spec *);
bool fmt_check_output (const struct fmt_spec *);
bool fmt_check_type_compat (const struct fmt_spec *, enum val_type);
bool fmt_check_width_compat (const struct fmt_spec *, int var_width);

/* Working with formats. */
int fmt_var_width (const struct fmt_spec *);
char *fmt_to_string (const struct fmt_spec *, char s[FMT_STRING_LEN_MAX + 1]);
bool fmt_equal (const struct fmt_spec *, const struct fmt_spec *);
bool fmt_resize (struct fmt_spec *, int new_width);

void fmt_fix (struct fmt_spec *, enum fmt_use);
void fmt_fix_input (struct fmt_spec *);
void fmt_fix_output (struct fmt_spec *);

void fmt_change_width (struct fmt_spec *, int width, enum fmt_use);
void fmt_change_decimals (struct fmt_spec *, int decimals, enum fmt_use);

/* Format types. */
bool is_fmt_type (enum fmt_type);

const char *fmt_name (enum fmt_type) PURE_FUNCTION;
bool fmt_from_name (const char *name, enum fmt_type *);

bool fmt_takes_decimals (enum fmt_type) PURE_FUNCTION;

int fmt_min_width (enum fmt_type, enum fmt_use) PURE_FUNCTION;
int fmt_max_width (enum fmt_type, enum fmt_use) PURE_FUNCTION;
int fmt_max_decimals (enum fmt_type, int width, enum fmt_use) PURE_FUNCTION;
int fmt_min_input_width (enum fmt_type) PURE_FUNCTION;
int fmt_max_input_width (enum fmt_type) PURE_FUNCTION;
int fmt_max_input_decimals (enum fmt_type, int width) PURE_FUNCTION;
int fmt_min_output_width (enum fmt_type) PURE_FUNCTION;
int fmt_max_output_width (enum fmt_type) PURE_FUNCTION;
int fmt_max_output_decimals (enum fmt_type, int width) PURE_FUNCTION;
int fmt_step_width (enum fmt_type) PURE_FUNCTION;

bool fmt_is_string (enum fmt_type) PURE_FUNCTION;
bool fmt_is_numeric (enum fmt_type) PURE_FUNCTION;
enum fmt_category fmt_get_category (enum fmt_type) PURE_FUNCTION;

enum fmt_type fmt_input_to_output (enum fmt_type) PURE_FUNCTION;
bool fmt_usable_for_input (enum fmt_type) PURE_FUNCTION;

int fmt_to_io (enum fmt_type) PURE_FUNCTION;
bool fmt_from_io (int io, enum fmt_type *);

const char *fmt_date_template (enum fmt_type, int width) PURE_FUNCTION;
const char *fmt_gui_name (enum fmt_type);

/* Format settings.

   A fmt_settings is really just a collection of one "struct fmt_number_style"
   for each format type. */
struct fmt_settings *fmt_settings_create (void);
void fmt_settings_destroy (struct fmt_settings *);
struct fmt_settings *fmt_settings_clone (const struct fmt_settings *);

void fmt_settings_set_decimal (struct fmt_settings *, char);

const struct fmt_number_style *fmt_settings_get_style (
  const struct fmt_settings *, enum fmt_type);
void fmt_settings_set_style (struct fmt_settings *, enum fmt_type,
                             char decimal, char grouping,
                             const char *neg_prefix, const char *prefix,
                             const char *suffix, const char *neg_suffix);

/* A prefix or suffix for a numeric output format. */
struct fmt_affix
  {
    char *s;                    /* String contents of affix, in UTF-8. */
    int width;                  /* Display width in columns (see wcwidth()). */
  };

/* A numeric output style. */
struct fmt_number_style
  {
    struct fmt_affix neg_prefix; /* Negative prefix. */
    struct fmt_affix prefix;     /* Prefix. */
    struct fmt_affix suffix;     /* Suffix. */
    struct fmt_affix neg_suffix; /* Negative suffix. */
    char decimal;                /* Decimal point: '.' or ','. */
    char grouping;               /* Grouping character: ',', '.', or 0. */

    /* A fmt_affix may require more bytes than its display width; for example,
       U+00A5 (¥) is 3 bytes in UTF-8 but occupies only one display column.
       This member is the sum of the number of bytes required by all of the
       fmt_affix members in this struct, minus their display widths.  Thus, it
       can be used to size memory allocations: for example, the formatted
       result of CCA20.5 requires no more than (20 + extra_bytes) bytes in
       UTF-8. */
    int extra_bytes;
  };

int fmt_affix_width (const struct fmt_number_style *);
int fmt_neg_affix_width (const struct fmt_number_style *);

extern const struct fmt_spec F_8_0 ;
extern const struct fmt_spec F_8_2 ;
extern const struct fmt_spec F_4_3 ;
extern const struct fmt_spec F_5_1 ;

#endif /* data/format.h */
