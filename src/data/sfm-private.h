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

/* PORTME: There might easily be alignment problems with some of these
   structures. */

#include <libpspp/compiler.h>
#include <stdint.h>
#include "variable.h"

/* This attribute might avoid some problems.  On the other hand... */
#define P ATTRIBUTE ((packed))

#if __BORLANDC__
#pragma option -a-		/* Turn off alignment. */
#endif

/* Find 64-bit floating-point type. */
#if SIZEOF_FLOAT == 8
  #define flt64 float
  #define FLT64_MAX FLT_MAX
#elif SIZEOF_DOUBLE == 8
  #define flt64 double
  #define FLT64_MAX DBL_MAX
#elif SIZEOF_LONG_DOUBLE == 8
  #define flt64 long double
  #define FLT64_MAX LDBL_MAX
#else
  #error Which one of your basic types is 64-bit floating point?
#endif

/* Figure out SYSMIS value for flt64. */
#include <libpspp/magic.h>
#if SIZEOF_DOUBLE == 8
#define second_lowest_flt64 second_lowest_value
#else
#error Must define second_lowest_flt64 for your architecture.
#endif

/* Record Type 1: General Information. */
struct sysfile_header
  {
    char rec_type[4] P;		/* 00: Record-type code, "$FL2". */
    char prod_name[60] P;	/* 04: Product identification. */
    int32_t layout_code P;	/* 40: 2. */
    int32_t case_size P;	/* 44: Number of `value's per case. 
				   Note: some systems set this to -1 */
    int32_t compress P;		/* 48: 1=compressed, 0=not compressed. */
    int32_t weight_idx P;         /* 4c: 1-based index of weighting var, or 0. */
    int32_t case_cnt P;		/* 50: Number of cases, -1 if unknown. */
    flt64 bias P;		/* 54: Compression bias (100.0). */
    char creation_date[9] P;	/* 5c: `dd mmm yy' creation date of file. */
    char creation_time[8] P;	/* 65: `hh:mm:ss' 24-hour creation time. */
    char file_label[64] P;	/* 6d: File label. */
    char padding[3] P;		/* ad: Ignored padding. */
  };

/* Record Type 2: Variable. */
struct sysfile_variable
  {
    int32_t rec_type P;		/* 2. */
    int32_t type P;		/* 0=numeric, 1-255=string width,
				   -1=continued string. */
    int32_t has_var_label P;	/* 1=has a variable label, 0=doesn't. */
    int32_t n_missing_values P;	/* Missing value code of -3,-2,0,1,2, or 3. */
    int32_t print P;	/* Print format. */
    int32_t write P;	/* Write format. */
    char name[SHORT_NAME_LEN] P; /* Variable name. */
    /* The rest of the structure varies. */
  };

#if __BORLANDC__
#pragma -a4
#endif
