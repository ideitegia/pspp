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

/* PORTME: There might easily be alignment problems with some of these
   structures. */

/* This attribute might avoid some problems.  On the other hand... */
#define P __attribute__((packed))

#if __BORLANDC__
#pragma option -a-		/* Turn off alignment. */
#endif

/* Record Type 1: General Information. */
struct sysfile_header
  {
    char rec_type[4] P;		/* Record-type code, "$FL2". */
    char prod_name[60] P;	/* Product identification. */
    int32 layout_code P;	/* 2. */
    int32 case_size P;		/* Number of `value's per case. */
    int32 compressed P;		/* 1=compressed, 0=not compressed. */
    int32 weight_index P;	/* 1-based index of weighting var, or zero. */
    int32 ncases P;		/* Number of cases, -1 if unknown. */
    flt64 bias P;		/* Compression bias (100.0). */
    char creation_date[9] P;	/* `dd mmm yy' creation date of file. */
    char creation_time[8] P;	/* `hh:mm:ss' 24-hour creation time. */
    char file_label[64] P;	/* File label. */
    char padding[3] P;		/* Ignored padding. */
  };

/* Record Type 2: Variable. */
struct sysfile_variable
  {
    int32 rec_type P;		/* 2. */
    int32 type P;		/* 0=numeric, 1-255=string width,
				   -1=continued string. */
    int32 has_var_label P;	/* 1=has a variable label, 0=doesn't. */
    int32 n_missing_values P;	/* Missing value code of -3,-2,0,1,2, or 3. */
    int32 print P;	/* Print format. */
    int32 write P;	/* Write format. */
    char name[8] P;		/* Variable name. */
    /* The rest of the structure varies. */
  };

#if __BORLANDC__
#pragma -a4
#endif
