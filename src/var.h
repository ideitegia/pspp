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

#if !var_h
#define var_h 1


#include <stddef.h>
#include "config.h"
#include <stdbool.h>

#include "format.h"
#include "missing-values.h"

/* Script variables. */

/* Variable type. */
enum
  {
    NUMERIC,			/* A numeric variable. */
    ALPHA			/* A string variable.
                                   (STRING is pre-empted by lexer.h.) */
  };

/* A variable's dictionary entry.  */
struct variable
  {
    /* Basic information. */
    char name[LONG_NAME_LEN + 1]; /* Variable name.  Mixed case. */
    int type;                   /* NUMERIC or ALPHA. */
    int width;			/* Size of string variables in chars. */
    int fv, nv;			/* Index into `value's, number of values. */
    unsigned init : 1;          /* 1=VFM must init and possibly reinit. */
    unsigned reinit : 1;        /* Cases are: 1=reinitialized; 0=left. */

    /* Data for use by containing dictionary. */
    int index;			/* Dictionary index. */

    /* Missing values. */
    struct missing_values miss; /* Missing values. */

    /* Display formats. */
    struct fmt_spec print;	/* Default format for PRINT. */
    struct fmt_spec write;	/* Default format for WRITE. */

    /* Labels. */
    struct val_labs *val_labs;  /* Value labels. */
    char *label;		/* Variable label. */

    /* GUI display parameters. */
    enum measure measure;       /* Nominal ordinal or continuous */
    int display_width;          /* Width of data editor column */
    enum alignment alignment;   /* Alignment of data in gui */

    /* Short name, used only for system and portable file input
       and output.  Upper case only.  There is no index for short
       names.  Short names are not necessarily unique.  Any
       variable may have no short name, indicated by an empty
       string. */
    char short_name[SHORT_NAME_LEN + 1];

    /* Per-command info. */
    void *aux;
    void (*aux_dtor) (struct variable *);
  };

/* Variable names. */
bool var_is_valid_name (const char *, bool issue_error);
int compare_var_names (const void *, const void *, void *);
unsigned hash_var_name (const void *, void *);

/* Short names. */
void var_set_short_name (struct variable *, const char *);
void var_set_short_name_suffix (struct variable *, const char *, int suffix);
void var_clear_short_name (struct variable *);

/* Pointers to `struct variable', by name. */
int compare_var_ptr_names (const void *, const void *, void *);
unsigned hash_var_ptr_name (const void *, void *);

/* Variable auxiliary data. */
void *var_attach_aux (struct variable *,
                      void *aux, void (*aux_dtor) (struct variable *));
void var_clear_aux (struct variable *);
void *var_detach_aux (struct variable *);
void var_dtor_free (struct variable *);

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
    char name[LONG_NAME_LEN + 1]; /* Name. */
    struct variable **var;	/* Vector of variables. */
    int cnt;			/* Number of variables. */
  };

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

struct ccase;
void dump_split_vars (const struct ccase *);

/* Transformations. */

struct trns_header;
typedef int trns_proc_func (struct trns_header *, struct ccase *, int);
typedef void trns_free_func (struct trns_header *);

/* Header for all transformations. */
struct trns_header
  {
    int index;                  /* Index into t_trns[]. */
    trns_proc_func *proc;       /* Transformation proc. */
    trns_free_func *free;       /* Garbage collector proc. */
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

struct var_set *var_set_create_from_dict (const struct dictionary *d);
struct var_set *var_set_create_from_array (struct variable *const *var,
                                           size_t);

size_t var_set_get_cnt (const struct var_set *vs);
struct variable *var_set_get_var (const struct var_set *vs, size_t idx);
struct variable *var_set_lookup_var (const struct var_set *vs,
                                     const char *name);
bool var_set_lookup_var_idx (const struct var_set *vs, const char *name,
                             size_t *idx);
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
    PV_NO_SCRATCH = 00200 	/* Disallow scratch variables. */
  };

struct variable *parse_variable (void);
struct variable *parse_dict_variable (const struct dictionary *);
int parse_variables (const struct dictionary *, struct variable ***, size_t *,
                     int opts);
int parse_var_set_vars (const struct var_set *, struct variable ***, size_t *,
                        int opts);
int parse_DATA_LIST_vars (char ***names, size_t *cnt, int opts);
int parse_mixed_vars (char ***names, size_t *cnt, int opts);



/* Return a string representing this variable, in the form most 
   appropriate from a human factors perspective.
   (IE: the label if it has one, otherwise the name )
*/
const char * var_to_string(const struct variable *var);


#endif /* !var_h */
