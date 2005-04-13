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

#if !format_h
#define format_h 1

/* Display format types. */

#include "bool.h"

/* See the definitions of these functions and variables when modifying
   this list:
   misc.c:convert_fmt_ItoO()
   sfm-read.c:parse_format_spec()
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

/* Describes one of the display formats above. */
struct fmt_desc
  {
    char name[9];		/* `DATETIME' is the longest name. */
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
    ALIGN_CENTRE = 2
  };


enum measure
  {
    MEASURE_NOMINAL=1,
    MEASURE_ORDINAL=2,
    MEASURE_SCALE=3
  };



/* Descriptions of all the display formats above. */
extern struct fmt_desc formats[];

union value;

/* Maximum length of formatted value, in characters. */
#define MAX_FORMATTED_LEN 256

/* Flags for parsing formats. */
enum fmt_parse_flags
  {
    FMTP_ALLOW_XT = 001,                /* 1=Allow X and T formats. */
    FMTP_SUPPRESS_ERRORS = 002          /* 1=Do not emit error messages. */
  };

int parse_format_specifier (struct fmt_spec *input, enum fmt_parse_flags);
int parse_format_specifier_name (const char **cp, enum fmt_parse_flags);
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
void data_out (char *s, const struct fmt_spec *fp, const union value *v);
char *fmt_to_string (const struct fmt_spec *);
void num_to_string (double v, char *s, int w, int d);

#endif /* !format_h */
