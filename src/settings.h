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

#if !settings_h
#define settings_h 1

/* Table of mode settings (x=X, w=Windows, p=PC+, f=has relevance for
   PSPP):

   AUTOMENU: p
   BEEP: p
   BLANKS: xwpf
   BLKSIZE: x (only on SHOW, not on SET)
   BLOCK: xwp
   BOX/BOXSTRING: xwp
   BUFNO: x (only on SHOW, not on SET)
   CASE: xw
   CCA...CCE: xwf
   COLOR: p
   COMP/COMPRESSION: xwpf (meaning varies between p and xw)
   CPI: xwp
   CPROMPT: pf
   DECIMAL: wf
   DPROMPT: f
   ECHO: pf
   EJECT: p
   EMULATION: f
   ENDCMD: xpf
   ERRORBREAK: pf
   ERRORS: wf
   FORMAT: xwf
   HEADERS: xwf
   HELPWINDOWS: p
   HIGHRES: w
   HISTOGRAM: xp
   INCLUDE: pf
   JOURNAL: wf (equivalent to LOG)
   LENGTH: xwp
   LISTING: xpf
   LOG: pf (equivalent to JOURNAL)
   LOWRES: w
   LPI: xwp
   MENUS: p
   MESSAGES: wf
   MEXPAND: xwf
   MITERATE: xwf
   MNEST: xwf
   MORE: pf
   MPRINT: xwf
   MXERRS: xf
   MXLOOPS: xwf
   MXMEMORY: w
   MXWARNS: xwf
   N: xw (only on SHOW, not on SET)
   NULLINE: xpf
   NUMBERED: x (only on SHOW, not on SET)
   PAGER: f
   PRINTBACK: xwf
   PRINTER: pf
   PROMPT: pf
   PTRANSLATE: p
   RCOLOR: p
   RESULTS: wpf (semantics differ)
   RUNREVIEW: p
   SCOMP/SCOMPRESSION: xwf
   SCREEN: pf
   SCRIPTTAB: xw
   SEED: xwpf (semantics differ)
   SYSMIS: xwf (only on SHOW, not on SET)
   TBFONTS: xw
   TB1: xw
   TB2: x
   UNDEFINED: xwf
   VIEWLENGTH: pf
   VIEWWIDTH: f
   WEIGHT: xwf (only on SHOW, not on SET)
   WIDTH: xwp
   WORKDEV: p
   WORKSPACE: w
   XSORT: x
   $VARS: wf (only on SHOW, not on SET)

 */

#include <float.h>

/* The value that blank numeric fields are set to when read in;
   normally SYSMIS. */
extern double set_blanks;

/* Describes one custom currency specification. */
struct set_cust_currency
  {
    char buf[32];		/* Buffer for strings. */
    char *neg_prefix;		/* Negative prefix. */
    char *prefix;		/* Prefix. */
    char *suffix;		/* Suffix. */
    char *neg_suffix;		/* Negative suffix. */
    int decimal;		/* Decimal point. */
    int grouping;		/* Grouping character. */
  };

/* CCA through CCE. */
extern struct set_cust_currency set_cc[5];

/* Whether the active file should be compressed. */
extern int set_compression;

/* Characters per inch (horizontal). */
extern int set_cpi;

/* Continuation prompt. */
extern char *set_cprompt;

/* The character used for a decimal point: ',' or '.'.  Only respected
   for data input and output. */
extern int set_decimal;

/* The character used for grouping in numbers: '.' or ','; the
   opposite of set_decimal.  Only used in COMMA data input and
   output. */
extern int set_grouping;

/* Prompt used for lines between BEGIN DATA and END DATA. */
extern char *set_dprompt;

/* Whether we echo commands to the listing file/printer; 0=no, 1=yes. */
extern int set_echo;

/* The character used to terminate commands. */
extern int set_endcmd;

/* Types of routing. */
enum
  {
    SET_ROUTE_SCREEN = 001,	/* Output to screen devices? */
    SET_ROUTE_LISTING = 002,	/* Output to listing devices? */
    SET_ROUTE_OTHER = 004,	/* Output to other devices? */
    SET_ROUTE_DISABLE = 010	/* Disable output--overrides all other bits. */
  };

/* Routing for errors, messages, and procedure results. */
extern int set_errors, set_messages, set_results;

/* Whether an error stops execution; 0=no, 1=yes. */
extern int set_errorbreak;

/* Default format for variables created by transformations and by DATA
   LIST {FREE,LIST}. */
extern struct fmt_spec set_format;

/* I don't know what this setting means; 0=no, 1=yes, 2=blank. */
extern int set_headers;

/* If set_echo is on, whether commands from include files are echoed;
 * 0=no, 1=yes. */
extern int set_include;

/* Journal file's name. */
extern char *set_journal;

/* Whether we're journaling. */
extern int set_journaling;

/* Lines per inch (vertical). */
extern int set_lpi;

/* 0=macro expansion is disabled, 1=macro expansion is enabled. */
extern int set_mexpand;

/* Maximum number of iterations in a macro loop. */
extern int set_miterate;

/* Maximum nesting level for macros. */
extern int set_mnest;

/* Whether we pause after each screen of output; 0=no, 1=yes. */
extern int set_more;

/* Independent of set_printback, controls whether the commands
   generated by macro invocations are displayed. */
extern int set_mprint;

/* Maximum number of errors. */
extern int set_mxerrs;

/* Implied limit of unbounded loop. */
extern int set_mxloops;

/* Maximum number of warnings + errors. */
extern int set_mxwarns;

/* Whether a blank line is a command terminator; 0=no, 1=yes. */
extern int set_nullline;

/* Whether commands are written to the display; 0=off, 1=on. */
extern int set_printback;

#if !USE_INTERNAL_PAGER
/* Name of the pager program. */
extern char *set_pager;
#endif /* !USE_INTERNAL_PAGER */

/* The command prompt. */
extern char *set_prompt;

/* Name of the results file. */
extern char *set_results_file;

/* Whether to allow certain unsafe operations.  Cannot be unset after
   it is set. */
extern int set_safer;

/* Whether save files should be compressed by default. */
extern int set_scompression;

/* The random number seed; NOT_LONG if we want a "random" random
   number seed.  */
extern long set_seed;

/* 1=The user has modified or made use of the random number seed. */
extern int set_seed_used;

/* 1=Turn on some heuristics that make testing PSPP for correct
   workings a little easier. */
extern int set_testing_mode;

/* Whether to warn on undefined values in numeric data. */
extern int set_undefined;

/* Requested "view length" in lines. */
extern int set_viewlength;

/* Screen width. */
extern int set_viewwidth;

#endif /* !settings_h */
