/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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


#ifndef AGGREGATE_H
#define AGGREGATE_H

#include <stddef.h>

#include "data/format.h"
#include "data/val-type.h"

enum agr_src_vars
  {
    AGR_SV_NO,
    AGR_SV_YES,
    AGR_SV_OPT
  };

/* Aggregation functions. */
enum
  {
    SUM, MEAN, MEDIAN, SD, MAX, MIN, PGT, PLT, PIN, POUT, FGT, FLT, FIN,
    FOUT, N, NU, NMISS, NUMISS, FIRST, LAST,

    FUNC = 0x1f, /* Function mask. */
    FSTRING = 1<<5, /* String function bit. */
  };

/* Attributes of an aggregation function. */
struct agr_func
  {
    const char *name;		/* Aggregation function name. */
    const char *description;    /* Translatable string describing the function. */
    enum agr_src_vars src_vars; /* Whether source variables are a parameter of the function */
    size_t n_args;              /* Number of arguments (not including src vars). */
    enum val_type alpha_type;   /* When given ALPHA arguments, output type. */
    struct fmt_spec format;	/* Format spec if alpha_type != ALPHA. */
  };

extern const struct agr_func agr_func_tab[];


#endif
