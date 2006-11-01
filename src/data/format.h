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

#if !format_h
#define format_h 1

/* Display format types. */

#include <stdbool.h>

/* See the definitions of these functions and variables when modifying
   this list:
   misc.c:convert_fmt_ItoO()
   sys-file-reader.c:parse_format_spec()
   data-in.c:parse_string_as_format() */
#define DEFFMT(LABEL, NAME, N_ARGS, IMIN_W, IMAX_W, OMIN_W, OMAX_W,	\
	       CAT, OUTPUT, SPSS_FMT)					\
	LABEL,
enum
  {
#include "format.def"
    FMT_NUMBER_OF_FORMATS
  };
#undef DEFFMT

/* Length of longest format specifier name,
   not including terminating null. */
#define FMT_TYPE_LEN_MAX 8

/* Describes one of the display formats above. */
struct fmt_desc
  {
    char name[FMT_TYPE_LEN_MAX + 1]; /* Name, in all caps. */
    int n_args;			/* 1=width; 2=width.decimals. */
    int Imin_w, Imax_w;		/* Bounds on input width. */
    int Omin_w, Omax_w;		/* Bounds on output width. */
    int cat;			/* Categories. */
    int output;			/* Output format. */
    int spss;			/* Equivalent SPSS output format. */
  };

/* Display format categories. */
enum
  {
    FCAT_BLANKS_SYSMIS = 001,	/* 1=All-whitespace means SYSMIS. */
    FCAT_EVEN_WIDTH = 002,	/* 1=Width must be even. */
    FCAT_STRING = 004,		/* 1=String input/output format. */
    FCAT_SHIFT_DECIMAL = 010,	/* 1=Automatically shift decimal point
				   on output--used for fixed-point
				   formats. */
    FCAT_OUTPUT_ONLY = 020	/* 1=This is not an input format. */
  };

/* Display format. */
struct fmt_spec
  {
    int type;			/* One of the above constants. */
    int w;			/* Width. */
    int d;			/* Number of implied decimal places. */
  };


enum alignment 
  {
    ALIGN_LEFT = 0,
    ALIGN_RIGHT = 1,
    ALIGN_CENTRE = 2, 
    n_ALIGN
  };


enum measure
  {
    MEASURE_NOMINAL=1,
    MEASURE_ORDINAL=2,
    MEASURE_SCALE=3,
    n_MEASURES
  };

bool measure_is_valid(enum measure m);
bool alignment_is_valid(enum alignment a);


/* Descriptions of all the display formats above. */
extern const struct fmt_desc formats[];

union value;

/* Maximum length of formatted value, in characters. */
#define MAX_FORMATTED_LEN 256

/* Common formats. */
extern const struct fmt_spec f8_2;      /* F8.2. */

int check_input_specifier (const struct fmt_spec *spec, int emit_error);
int check_output_specifier (const struct fmt_spec *spec, int emit_error);
bool check_specifier_type (const struct fmt_spec *, int type, bool emit_error);
bool check_specifier_width (const struct fmt_spec *,
                            int width, bool emit_error);
void convert_fmt_ItoO (const struct fmt_spec *input, struct fmt_spec *output);
int get_format_var_width (const struct fmt_spec *);
int parse_string_as_format (const char *s, int len, const struct fmt_spec *fp,
			    int fc, union value *v);
int translate_fmt (int spss);
bool data_out (char *s, const struct fmt_spec *fp, const union value *v);
bool fmt_type_from_string (const char *name, int *type);
char *fmt_to_string (const struct fmt_spec *);
struct fmt_spec make_input_format (int type, int w, int d);
struct fmt_spec make_output_format (int type, int w, int d);
bool fmt_is_binary (int type);

#endif /* !format_h */
