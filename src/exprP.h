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

void debug_print_op (short int *);


/* Expression operators. */
#define DEFINE_OPERATOR(NAME, STACK_DELTA, FLAGS, ARGS) \
        OP_##NAME,
enum
  {
#include "expr.def"
    OP_SENTINEL
  };

#define IS_TERMINAL(OPERATOR) (ops[OPERATOR].height > 0)
#define IS_NONTERMINAL(OPERATOR) !IS_TERMINAL (OPERATOR)

/* Flags that describe operators. */
enum
  {
    OP_NO_FLAGS = 0,            /* No flags. */
    OP_VAR_ARGS = 001,		/* 1=Variable number of args. */
    OP_MIN_ARGS = 002,		/* 1=Can specific min args with .X. */
    OP_FMT_SPEC = 004,		/* 1=Includes a format specifier. */
    OP_ABSORB_MISS = 010,	/* 1=May return other than SYSMIS if
				   given a SYSMIS argument. */
  };

/* Describes an operator. */
struct op_desc
  {
    const char *name;		/* Operator name. */
    signed char height;		/* Effect on stack height. */
    unsigned char flags;	/* Flags. */
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
				   or OP_STR_VAR. */
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
    enum expr_type type;	/* Type of expression result. */
    unsigned char *op;		/* Operators. */
    struct variable **var;	/* Variables. */
    double *num;		/* Numeric operands. */
    unsigned char *str;		/* String operands. */
    union value *stack;		/* Evaluation stack. */
    struct pool *pool;          /* Pool for evaluation temporaries. */
  };

void optimize_expression (union any_node **);
void dump_expression (union any_node *, struct expression *);
void free_node (union any_node *);

double yrmoda (double year, double month, double day);

#endif /* exprP.h */
