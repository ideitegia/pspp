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

#if !exprP_h
#define exprP_h 1

#undef DEBUGGING
/*#define DEBUGGING 1*/
#include "debug-print.h"

#if GLOBAL_DEBUGGING
void debug_print_expr (struct expression *);
void debug_print_op (short int *);
#endif

/* Expression types. */
enum
  {
    EX_ERROR,		/* Error value for propagation. */
    EX_BOOLEAN,		/* Numeric value that's 0, 1, or SYSMIS. */
    EX_NUMERIC,		/* Numeric value. */
    EX_STRING		/* String value. */
  };

/* Expression operators.
   The ordering below is important.  Do not change it. */
enum
  {
    OP_ERROR,

    /* Basic operators. */
    OP_PLUS,
    OP_MUL,
    OP_POW,
    OP_AND,
    OP_OR,
    OP_NOT,

    /* Numeric relational operators. */
    OP_EQ,
    OP_GE,
    OP_GT,
    OP_LE,
    OP_LT,
    OP_NE,

    /* String relational operators. */
    OP_STRING_EQ,
    OP_STRING_GE,
    OP_STRING_GT,
    OP_STRING_LE,
    OP_STRING_LT,
    OP_STRING_NE,

    /* Unary functions. */
    OP_NEG,
    OP_ABS,
    OP_ARCOS,
    OP_ARSIN,
    OP_ARTAN,
    OP_COS,
    OP_EXP,
    OP_LG10,
    OP_LN,
    OP_MOD10,
    OP_RND,
    OP_SIN,
    OP_SQRT,
    OP_TAN,
    OP_TRUNC,

    /* N-ary numeric functions. */
    OP_ANY,
    OP_ANY_STRING,
    OP_CFVAR,
    OP_MAX,
    OP_MEAN,
    OP_MIN,
    OP_NMISS,
    OP_NVALID,
    OP_RANGE,
    OP_RANGE_STRING,
    OP_SD,
    OP_SUM,
    OP_VARIANCE,

    /* Time construction & extraction functions. */
    OP_TIME_HMS,

    /* These never appear in a tree or an expression.
       They disappear in parse.c:unary_func(). */
    OP_CTIME_DAYS,
    OP_CTIME_HOURS,
    OP_CTIME_MINUTES,
    OP_CTIME_SECONDS,
    OP_TIME_DAYS,

    /* Date construction functions. */
    OP_DATE_DMY,
    OP_DATE_MDY,
    OP_DATE_MOYR,
    OP_DATE_QYR,
    OP_DATE_WKYR,
    OP_DATE_YRDAY,
    OP_YRMODA,

    /* Date extraction functions. */
    OP_XDATE_DATE,
    OP_XDATE_HOUR,
    OP_XDATE_JDAY,
    OP_XDATE_MDAY,
    OP_XDATE_MINUTE,
    OP_XDATE_MONTH,
    OP_XDATE_QUARTER,
    OP_XDATE_SECOND,
    OP_XDATE_TDAY,
    OP_XDATE_TIME,
    OP_XDATE_WEEK,
    OP_XDATE_WKDAY,
    OP_XDATE_YEAR,

    /* String functions. */
    OP_CONCAT,
    OP_INDEX,
    OP_INDEX_OPT,
    OP_RINDEX,
    OP_RINDEX_OPT,
    OP_LENGTH,
    OP_LOWER,
    OP_UPPER,
    OP_LPAD,
    OP_LPAD_OPT,
    OP_RPAD,
    OP_RPAD_OPT,
    OP_LTRIM,
    OP_LTRIM_OPT,
    OP_RTRIM,
    OP_RTRIM_OPT,
    OP_NUMBER,
    OP_NUMBER_OPT,
    OP_STRING,
    OP_SUBSTR,
    OP_SUBSTR_OPT,

    /* Artificial. */
    OP_INV,			/* Reciprocal. */
    OP_SQUARE,			/* Squares the argument. */
    OP_NUM_TO_BOOL,		/* Converts ~0=>0, ~1=>1, SYSMIS=>SYSMIS,
				   others=>0 with a warning. */

    /* Weirdness. */
    OP_MOD,			/* Modulo function. */
    OP_NORMAL,			/* Normally distributed PRNG. */
    OP_UNIFORM,			/* Uniformly distributed PRNG. */
    OP_SYSMIS,			/* Tests whether for SYSMIS argument. */
    OP_VEC_ELEM_NUM,		/* Element of a numeric vector. */
    OP_VEC_ELEM_STR,		/* Element of a string vector. */

    /* Terminals. */
    OP_TERMINAL,		/* Not a valid type.  Boundary
				   between terminals and nonterminals. */

    OP_NUM_CON,			/* Numeric constant. */
    OP_STR_CON,			/* String literal. */
    OP_NUM_VAR,			/* Numeric variable reference. */
    OP_STR_VAR,			/* String variable reference. */
    OP_NUM_LAG,			/* Numeric variable from an earlier case. */
    OP_STR_LAG,			/* String variable from an earlier case. */
    OP_NUM_SYS,			/* SYSMIS(numvar). */
    OP_NUM_VAL,			/* VALUE(numvar). */
    OP_STR_MIS,			/* MISSING(strvar). */
    OP_CASENUM,			/* $CASENUM. */
    OP_SENTINEL			/* Sentinel. */
  };

/* Flags that describe operators. */
enum
  {
    OP_VAR_ARGS = 001,		/* 1=Variable number of args. */
    OP_MIN_ARGS = 002,		/* 1=Can specific min args with .X. */
    OP_FMT_SPEC = 004,		/* 1=Includes a format specifier. */
    OP_ABSORB_MISS = 010,	/* 1=May return other than SYSMIS if
				   given a SYSMIS argument. */
  };

/* Describes an operator. */
struct op_desc
  {
#if GLOBAL_DEBUGGING
    const char *name;		/* Operator name. */
#endif
    unsigned char flags;	/* Flags. */
    signed char height;		/* Effect on stack height. */
    unsigned char skip;		/* Number of operator item arguments. */
  };

extern struct op_desc ops[];

/* Tree structured expressions. */ 

/* Numeric constant. */
struct num_con_node
  {
    int type;			/* Always OP_NUM_CON. */
    double value;		/* Numeric value. */
  };

/* String literal. */
struct str_con_node
  {
    int type;			/* Always OP_STR_CON. */
    int len;			/* Length of string. */
    char s[1];			/* String value. */
  };

/* Variable or test for missing values or cancellation of
   user-missing. */
struct var_node
  {
    int type;			/* OP_NUM_VAR, OP_NUM_SYS, OP_NUM_VAL,
				   OP_STR_MIS, or OP_STR_VAR. */
    struct variable *v;		/* Variable. */
  };

/* Variable from an earlier case. */
struct lag_node
  {
    int type;			/* Always OP_NUM_LAG. */
    struct variable *v;		/* Relevant variable. */
    int lag;			/* Number of cases to lag. */
  };

/* $CASENUM. */
struct casenum_node
  {
    int type;			/* Always OP_CASENUM. */
  };

/* Any nonterminal node. */
struct nonterm_node
  {
    int type;			/* Always greater than OP_TERMINAL. */
    int n;			/* Number of arguments. */
    union any_node *arg[1];	/* Arguments. */
  };

/* Any node. */
union any_node
  {
    int type;
    struct nonterm_node nonterm;
    struct num_con_node num_con;
    struct str_con_node str_con;
    struct var_node var;
    struct lag_node lag;
    struct casenum_node casenum;
  };

/* An expression. */
struct expression
  {
    int type;			/* Type of expression result. */
    unsigned char *op;		/* Operators. */
    struct variable **var;	/* Variables. */
    double *num;		/* Numeric operands. */
    unsigned char *str;		/* String operands. */
    union value *stack;		/* Evaluation stack. */
    struct pool *pool;          /* Pool for evaluation temporaries. */
  };

struct nonterm_node *optimize_expression (struct nonterm_node *);
void dump_expression (union any_node *, struct expression *);
void free_node (union any_node *);

double yrmoda (double year, double month, double day);

#endif /* exprP.h */
