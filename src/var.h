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

#if !var_h
#define var_h 1

#include <stddef.h>
#include "format.h"

/* Values. */

/* Definition of the max length of a short string value, generally
   eight characters.  */
#define MAX_SHORT_STRING ((SIZEOF_DOUBLE)>=8 ? ((SIZEOF_DOUBLE)+1)/2*2 : 8)
#define MIN_LONG_STRING (MAX_SHORT_STRING+1)

/* FYI: It is a bad situation if sizeof(flt64) < MAX_SHORT_STRING:
   then short string missing values can be truncated in system files
   because there's only room for as many characters as can fit in a
   flt64. */
#if MAX_SHORT_STRING > 8
#error MAX_SHORT_STRING must be less than 8.
#endif

/* Special values. */
#define SYSMIS (-DBL_MAX)
#define LOWEST second_lowest_value
#define HIGHEST DBL_MAX

/* Describes one value, which is either a floating-point number or a
   short string. */
union value
  {
    /* A numeric value. */
    double f;

    /* A short-string value. */
    unsigned char s[MAX_SHORT_STRING];

    /* This member is used by data-in.c to return a string result,
       since it may need to return a long string.  As currently
       implemented, it's a pointer to a static internal buffer in
       data-in.c.

       Also used by evaluate_expression() to return a string result.
       As currently implemented, it's a pointer to a dynamic buffer in
       the appropriate expression.

       Also used by the AGGREGATE procedure in handling string
       values. */
    unsigned char *c;

    /* Sometimes we insert value's in a hash table. */
    unsigned long hash[SIZEOF_DOUBLE / SIZEOF_LONG];
  };

/* Frequency tables. */

/* Frequency table entry. */
struct freq
  {
    union value v;		/* The value. */
    double c;			/* The number of occurrences of the value. */
  };

/* Types of frequency tables. */
enum
  {
    FRQM_GENERAL,
    FRQM_INTEGER
  };

/* Entire frequency table. */
struct freq_tab
  {
    int mode;			/* FRQM_GENERAL or FRQM_INTEGER. */

    /* General mode. */
    struct hsh_table *data;	/* Undifferentiated data. */

    /* Integer mode. */
    double *vector;		/* Frequencies proper. */
    int min, max;		/* The boundaries of the table. */
    double out_of_range;	/* Sum of weights of out-of-range values. */
    double sysmis;		/* Sum of weights of SYSMIS values. */

    /* All modes. */
    struct freq *valid;         /* Valid freqs. */
    int n_valid;		/* Number of total freqs. */

    struct freq *missing;	/* Missing freqs. */
    int n_missing;		/* Number of missing freqs. */

    /* Statistics. */
    double total_cases;		/* Sum of weights of all cases. */
    double valid_cases;		/* Sum of weights of valid cases. */
  };

/* Procedures' private per-variable data. */

/* Structure name suffixes for private data:
   _proc: for a procedure (i.e., LIST -> list_proc).
   _trns: for a transformation (i.e., COMPUTE -> compute_trns.
   _pgm: for an input program (i.e., DATA LIST -> data_list_pgm). */

/* CROSSTABS private data. */
struct crosstab_proc
  {
    /* Integer mode only. */
    int min;			/* Minimum value. */
    int max;			/* Maximum value + 1. */
    int count;			/* max - min. */
  };


/* T-TEST private data */
struct t_test_proc
  {
    double mean;

    double std_dev;
    
    /* count */
    double n;

    double sum;

    /* Sum of squares */
    double ssq;

    /* Std Err of Mean */
    double se_mean;

    /* Sum of differnces */
    double sum_diff;

    /* Mean of differences */
    double mean_diff ;
  };


/* FREQUENCIES private data. */
enum
  {
    frq_mean = 0, frq_semean, frq_median, frq_mode, frq_stddev, frq_variance,
    frq_kurt, frq_sekurt, frq_skew, frq_seskew, frq_range, frq_min, frq_max,
    frq_sum, frq_n_stats
  };

struct frequencies_proc
  {
    int used;                   /* 1=This variable already used. */

    /* Freqency table. */
    struct freq_tab tab;	/* Frequencies table to use. */

    /* Percentiles. */
    int n_groups;		/* Number of groups. */
    double *groups;		/* Groups. */

    /* Statistics. */
    double stat[frq_n_stats];
  };

/* LIST private data. */
struct list_proc
  {
    int newline;		/* Whether a new line begins here. */
    int width;			/* Field width. */
    int vert;			/* Whether to print the varname vertically. */
  };

/* DESCRIPTIVES private data.  Note that the DESCRIPTIVES procedure also
   has a transformation, descriptives_trns. */
enum
  {
    /* As these are used as bit indexes, there must be 32 or fewer.
       Be very careful in adjusting these, see the structure below
       and the table in descriptives.q. */
    dsc_mean = 0, dsc_semean, dsc_stddev, dsc_variance, dsc_kurt,
    dsc_sekurt, dsc_skew, dsc_seskew, dsc_range, dsc_min,
    dsc_max, dsc_sum, dsc_n_stats
  };

struct descriptives_proc
  {
    /* Miscellaneous. */
    int dup;			/* Finds duplicates in list of
				   variables. */
    char zname[10];		/* Name for z-score variable. */

    /* Counts. */
    double valid, miss;		/* Valid, missing--general. */

    /* Mean, moments about the mean. */
    double X_bar, M2, M3, M4;
    double min, max;

    /* Statistics. */
    double stats[dsc_n_stats];	/* Everything glommed together. */
  };

/* GET private data. */
struct get_proc
  {
    int fv, nv;			/* First, # of values. */
  };

/* Sort order. */
enum
  {
    SRT_ASCEND,			/* A, B, C, ..., X, Y, Z. */
    SRT_DESCEND			/* Z, Y, X, ..., C, B, A. */
  };

/* SORT CASES private data. */
struct sort_cases_proc
  {
    int order;			/* SRT_ASCEND or SRT_DESCEND. */
  };

/* MEANS private data. */
struct means_proc
  {
    double min, max;		/* Range for integer mode. */
  };

/* Different types of variables for MATRIX DATA procedure.  Order is
   important: these are used for sort keys. */
enum
  {
    MXD_SPLIT,			/* SPLIT FILE variables. */
    MXD_ROWTYPE,		/* ROWTYPE_. */
    MXD_FACTOR,			/* Factor variables. */
    MXD_VARNAME,		/* VARNAME_. */
    MXD_CONTINUOUS,		/* Continuous variables. */

    MXD_COUNT
  };

/* MATRIX DATA private data. */
struct matrix_data_proc
  {
    int vartype;		/* Variable type. */
    int subtype;		/* Subtype. */
  };

/* MATCH FILES private data. */
struct match_files_proc
  {
    struct variable *master;	/* Corresponding master file variable. */
  };


/* Script variables. */

/* Variable type. */
enum
  {
    NUMERIC,			/* A numeric variable. */
    ALPHA			/* A string variable.  (STRING is pre-empted by lexer.h) */
  };

/* Types of missing values.  Order is significant, see
   mis-val.c:parse_numeric(), sfm-read.c:sfm_read_dictionary()
   sfm-write.c:sfm_write_dictionary(),
   sysfile-info.c:cmd_sysfile_info(), mis-val.c:copy_missing_values(),
   pfm-read.c:read_variables(), pfm-write.c:write_variables(),
   apply-dict.c:cmd_apply_dictionary(), and more (?). */
enum
  {
    MISSING_NONE,		/* No user-missing values. */
    MISSING_1,			/* One user-missing value. */
    MISSING_2,			/* Two user-missing values. */
    MISSING_3,			/* Three user-missing values. */
    MISSING_RANGE,		/* [a,b]. */
    MISSING_LOW,		/* (-inf,a]. */
    MISSING_HIGH,		/* (a,+inf]. */
    MISSING_RANGE_1,		/* [a,b], c. */
    MISSING_LOW_1,		/* (-inf,a], b. */
    MISSING_HIGH_1,		/* (a,+inf), b. */
    MISSING_COUNT
  };

/* A variable's dictionary entry.  Note: don't reorder name[] from the
   first element; a pointer to `variable' should be a pointer to
   member `name'.*/
struct variable
  {
    /* Required by parse_variables() to be in this order.  */
    char name[9];		/* As a string. */
    int index;			/* Index into its dictionary's var[]. */
    int type;			/* NUMERIC or ALPHA. */

    /* Also important but parse_variables() doesn't need it.  Still,
       check before reordering. */
    int width;			/* Size of string variables in chars. */
    int fv, nv;			/* Index into `value's, number of values. */
    int left;			/* 0=reinitialize each case, 1=don't. */

    /* Missing values. */
    int miss_type;		/* One of the MISSING_* constants. */
    union value missing[3];	/* User-missing value. */

    /* Display formats. */
    struct fmt_spec print;	/* Default format for PRINT. */
    struct fmt_spec write;	/* Default format for WRITE. */

    /* Labels. */
    struct val_labs *val_labs;
    char *label;		/* Variable label. */

    /* Per-procedure info. */
    void *aux;
    struct get_proc get;
    union
      {
	struct crosstab_proc crs;
	struct descriptives_proc dsc;
	struct frequencies_proc frq;
	struct list_proc lst;
	struct means_proc mns;
	struct sort_cases_proc srt;
	struct matrix_data_proc mxd;
	struct match_files_proc mtf;
	struct t_test_proc t_t;
      }
    p;
  };

int compare_variables (const void *, const void *, void *);
unsigned hash_variable (const void *, void *);

/* Classes of variables. */
enum dict_class 
  {
    DC_ORDINARY,                /* Ordinary identifier. */
    DC_SYSTEM,                  /* System variable. */
    DC_SCRATCH                  /* Scratch variable. */
  };

enum dict_class dict_class_from_id (const char *name);
const char *dict_class_to_name (enum dict_class dict_class);

/* Vector of variables. */
struct vector
  {
    int idx;                    /* Index for dict_get_vector(). */
    char name[9];		/* Name. */
    struct variable **var;	/* Vector of variables. */
    int cnt;			/* Number of variables. */
  };

/* Cases. */

/* A single case.  (This doesn't need to be a struct anymore, but it
   remains so for hysterical raisins.) */
struct ccase
  {
    union value data[1];
  };

/* Dictionary. */ 

/* Complete dictionary state. */
struct dictionary;

struct dictionary *dict_create (void);
struct dictionary *dict_clone (const struct dictionary *);
void dict_clear (struct dictionary *);
void dict_destroy (struct dictionary *);

size_t dict_get_var_cnt (const struct dictionary *);
struct variable *dict_get_var (const struct dictionary *, size_t idx);
void dict_get_vars (const struct dictionary *,
                    struct variable ***vars, size_t *cnt,
                    unsigned exclude_classes);

struct variable *dict_create_var (struct dictionary *, const char *,
                                  int width);
struct variable *dict_clone_var (struct dictionary *, const struct variable *,
                                 const char *);
void dict_rename_var (struct dictionary *, struct variable *, const char *);

struct variable *dict_lookup_var (const struct dictionary *, const char *);
int dict_contains_var (const struct dictionary *, const struct variable *);
void dict_delete_var (struct dictionary *, struct variable *);
void dict_delete_vars (struct dictionary *,
                       struct variable *const *, size_t count);
void dict_reorder_vars (struct dictionary *,
                        struct variable *const *, size_t count);
int dict_rename_vars (struct dictionary *,
                      struct variable **, char **new_names,
                      size_t count, char **err_name);

struct variable *dict_get_weight (const struct dictionary *);
double dict_get_case_weight (const struct dictionary *, const struct ccase *);
void dict_set_weight (struct dictionary *, struct variable *);

struct variable *dict_get_filter (const struct dictionary *);
void dict_set_filter (struct dictionary *, struct variable *);

int dict_get_case_limit (const struct dictionary *);
void dict_set_case_limit (struct dictionary *, int);

int dict_get_value_cnt (const struct dictionary *);
void dict_compact_values (struct dictionary *);

struct variable *const *dict_get_split_vars (const struct dictionary *);
size_t dict_get_split_cnt (const struct dictionary *);
void dict_set_split_vars (struct dictionary *,
                          struct variable *const *, size_t cnt);

const char *dict_get_label (const struct dictionary *);
void dict_set_label (struct dictionary *, const char *);

const char *dict_get_documents (const struct dictionary *);
void dict_set_documents (struct dictionary *, const char *);

int dict_create_vector (struct dictionary *,
                        const char *name,
                        struct variable **, size_t cnt);
const struct vector *dict_get_vector (const struct dictionary *,
                                      size_t idx);
size_t dict_get_vector_cnt (const struct dictionary *);
const struct vector *dict_lookup_vector (const struct dictionary *,
                                         const char *name);
void dict_clear_vectors (struct dictionary *);

void discard_variables (void);

/* This is the active file dictionary. */
extern struct dictionary *default_dict;

/* Transformation state. */

/* Default file handle for DATA LIST, REREAD, REPEATING DATA
   commands. */
extern struct file_handle *default_handle;

/* PROCESS IF expression. */
extern struct expression *process_if_expr;

/* TEMPORARY support. */

/* 1=TEMPORARY has been executed at some point. */
extern int temporary;

/* If temporary!=0, the saved dictionary. */
extern struct dictionary *temp_dict;

/* If temporary!=0, index into t_trns[] (declared far below) that
   gives the point at which data should be written out.  -1 means that
   the data shouldn't be changed since all transformations are
   temporary. */
extern int temp_trns;

/* If FILTER is active, whether it was executed before or after
   TEMPORARY. */
extern int FILTER_before_TEMPORARY;

void cancel_temporary (void);

/* Functions. */

void dump_split_vars (const struct ccase *);

int is_num_user_missing (double, const struct variable *);
int is_str_user_missing (const unsigned char[], const struct variable *);
int is_missing (const union value *, const struct variable *);
int is_system_missing (const union value *, const struct variable *);
int is_user_missing (const union value *, const struct variable *);
void copy_missing_values (struct variable *dest, const struct variable *src);

#if GLOBAL_DEBUGGING
struct variable *force_create_variable (struct dictionary *, const char *name,
					int type, int width);
struct variable *force_dup_variable (struct dictionary *,
				     const struct variable *src,
				     const char *name);
#else
#define force_create_variable(A, B, C, D)	\
	create_variable (A, B, C, D)
#define force_dup_variable(A, B, C)		\
	dup_variable (A, B, C)
#endif


/* Transformations. */

/* Header for all transformations. */
struct trns_header
  {
    /* Index into t_trns[]. */
    int index;

    /* Transformation proc. */
    int (*proc) (struct trns_header *, struct ccase *);

    /* Garbage collector proc. */
    void (*free) (struct trns_header *);
  };

/* Array of transformations */
extern struct trns_header **t_trns;

/* Number of transformations, maximum number in array currently. */
extern int n_trns, m_trns;

/* Index of first transformation that is really a transformation.  Any
   transformations before this belong to INPUT PROGRAM. */
extern int f_trns;

void add_transformation (struct trns_header *trns);
void cancel_transformations (void);

struct var_set;

struct var_set *var_set_create_from_dict (struct dictionary *d);
struct var_set *var_set_create_from_array (struct variable **var, size_t);

size_t var_set_get_cnt (struct var_set *vs);
struct variable *var_set_get_var (struct var_set *vs, size_t idx);
struct variable *var_set_lookup_var (struct var_set *vs, const char *name);
void var_set_destroy (struct var_set *vs);


/* Variable parsers. */

enum
  {
    PV_NONE = 0,		/* No options. */
    PV_SINGLE = 0001,		/* Restrict to a single name or TO use. */
    PV_DUPLICATE = 0002,	/* Don't merge duplicates. */
    PV_APPEND = 0004,		/* Append to existing list. */
    PV_NO_DUPLICATE = 0010,	/* Error on duplicates. */
    PV_NUMERIC = 0020,		/* Vars must be numeric. */
    PV_STRING = 0040,		/* Vars must be string. */
    PV_SAME_TYPE = 00100,	/* All vars must be the same type. */
    PV_NO_SCRATCH = 00200,	/* Disallow scratch variables. */
  };

struct variable *parse_variable (void);
struct variable *parse_dict_variable (struct dictionary *);
int parse_variables (struct dictionary *, struct variable ***, int *,
                     int opts);
int parse_var_set_vars (struct var_set *, struct variable ***, int *,
                        int opts);
int parse_DATA_LIST_vars (char ***names, int *cnt, int opts);
int parse_mixed_vars (char ***names, int *cnt, int opts);

#endif /* !var_h */
