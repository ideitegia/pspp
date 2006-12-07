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

#if !variable_h
#define variable_h 1


#include <stddef.h>
#include "config.h"
#include <stdbool.h>
#include "category.h"
#include "format.h"
#include "missing-values.h"

/* Variable type. */
enum var_type
  {
    NUMERIC,			/* A numeric variable. */
    ALPHA			/* A string variable. */
  };

bool var_type_is_valid (enum var_type);
const char *var_type_adj (enum var_type);
const char *var_type_noun (enum var_type);

/* Alignment of data for display. */
enum alignment 
  {
    ALIGN_LEFT = 0,
    ALIGN_RIGHT = 1,
    ALIGN_CENTRE = 2,
    n_ALIGN
  };

bool alignment_is_valid (enum alignment);

/* How data is measured. */
enum measure
  {
    MEASURE_NOMINAL = 1,
    MEASURE_ORDINAL = 2,
    MEASURE_SCALE = 3,
    n_MEASURES
  };

bool measure_is_valid (enum measure);

/* Maximum lengths of short and long variable names.
   Most operations support long variable names,
   but some file formats are limited to short names. */
#define SHORT_NAME_LEN 8        /* Short name length. */
#define LONG_NAME_LEN 64        /* Long name length. */

/* A variable's dictionary entry.  */
struct variable
  {
    /* Dictionary information. */
    char name[LONG_NAME_LEN + 1]; /* Variable name.  Mixed case. */
    int width;			/* 0 for numeric, otherwise string width. */
    struct missing_values miss; /* Missing values. */
    struct fmt_spec print;	/* Default format for PRINT. */
    struct fmt_spec write;	/* Default format for WRITE. */
    struct val_labs *val_labs;  /* Value labels. */
    char *label;		/* Variable label. */

    /* GUI information. */
    enum measure measure;       /* Nominal, ordinal, or continuous. */
    int display_width;          /* Width of data editor column. */
    enum alignment alignment;   /* Alignment of data in GUI. */

    /* Case information. */
    int fv;			/* Index into `value's. */
    bool leave;                 /* Leave value from case to case? */

    /* Data for use by containing dictionary. */
    int index;			/* Dictionary index. */

    /* Short name, used only for system and portable file input
       and output.  Upper case only.  There is no index for short
       names.  Short names are not necessarily unique.  Any
       variable may have no short name, indicated by an empty
       string. */
    char short_name[SHORT_NAME_LEN + 1];

    /* Each command may use these fields as needed. */
    void *aux;
    void (*aux_dtor) (struct variable *);

    /* Values of a categorical variable.  Procedures need
       vectors with binary entries, so any variable of type ALPHA will
       have its values stored here. */
    struct cat_vals *obs_vals;
  };

/* Variable names. */
const char *var_get_name (const struct variable *);
void var_set_name (struct variable *, const char *);
bool var_is_valid_name (const char *, bool issue_error);
bool var_is_plausible_name (const char *name, bool issue_error);
int  compare_var_names (const void *, const void *, const void *);
unsigned hash_var_name (const void *, const void *);

/* Variable types and widths. */
enum var_type var_get_type (const struct variable *);
int var_get_width (const struct variable *);
void var_set_width (struct variable *, int width);
bool var_is_numeric (const struct variable *);
bool var_is_alpha (const struct variable *);
bool var_is_short_string (const struct variable *);
bool var_is_long_string (const struct variable *);
bool var_is_very_long_string (const struct variable *);

/* Variables' missing values. */
const struct missing_values *var_get_missing_values (const struct variable *);
void var_set_missing_values (struct variable *, const struct missing_values *);
void var_clear_missing_values (struct variable *);
bool var_has_missing_values (const struct variable *);

typedef bool var_is_missing_func (const struct variable *,
                                  const union value *);
bool var_is_value_missing (const struct variable *, const union value *);
bool var_is_num_missing (const struct variable *, double);
bool var_is_str_missing (const struct variable *, const char[]);
bool var_is_value_user_missing (const struct variable *,
                                const union value *);
bool var_is_num_user_missing (const struct variable *, double);
bool var_is_str_user_missing (const struct variable *, const char[]);
bool var_is_value_system_missing (const struct variable *,
                                  const union value *);

/* Print and write formats. */
const struct fmt_spec *var_get_print_format (const struct variable *);
void var_set_print_format (struct variable *, const struct fmt_spec *);
const struct fmt_spec *var_get_write_format (const struct variable *);
void var_set_write_format (struct variable *, const struct fmt_spec *);
void var_set_both_formats (struct variable *, const struct fmt_spec *);

/* Variable labels. */
const char *var_get_label (const struct variable *);
void var_set_label (struct variable *, const char *);
void var_clear_label (struct variable *);
bool var_has_label (const struct variable *);

/* GUI information. */
enum measure var_get_measure (const struct variable *);
void var_set_measure (struct variable *, enum measure);

int var_get_display_width (const struct variable *);
void var_set_display_width (struct variable *, int display_width);

enum alignment var_get_alignment (const struct variable *);
void var_set_alignment (struct variable *, enum alignment);

/* Variable location in cases. */
size_t var_get_value_cnt (const struct variable *);

/* Whether variables' values should be preserved from case to
   case. */
bool var_get_leave (const struct variable *);

/* Short names. */
const char *var_get_short_name (const struct variable *);
void var_set_short_name (struct variable *, const char *);
void var_set_short_name_suffix (struct variable *, const char *, int suffix);
void var_clear_short_name (struct variable *);

/* Pointers to `struct variable', by name. */
int compare_var_ptr_names (const void *, const void *, const void *);
unsigned hash_var_ptr_name (const void *, const void *);

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


/* Return a string representing this variable, in the form most 
   appropriate from a human factors perspective.
   (IE: the label if it has one, otherwise the name )
*/
const char * var_to_string(const struct variable *var);


int width_to_bytes(int width);

union value * value_dup (const union value *val, int width);


#endif /* !variable.h */
