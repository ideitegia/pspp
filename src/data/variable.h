/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#if !variable_h
#define variable_h 1

#include <stddef.h>
#include <stdbool.h>
#include <data/missing-values.h>

union value;

/* Variable type. */
enum var_type
  {
    VAR_NUMERIC,                /* A numeric variable. */
    VAR_STRING			/* A string variable. */
  };

bool var_type_is_valid (enum var_type);
enum var_type var_type_from_width (int width);

/* Variables. */
struct variable *var_create (const char *name, int width);
struct variable *var_clone (const struct variable *);
void var_destroy (struct variable *);

/* Variable names.
   Long variable names can be used in most contexts, but a few
   procedures and file formats are limited to short names. */
#define SHORT_NAME_LEN 8
#define LONG_NAME_LEN 64

const char *var_get_name (const struct variable *);
void var_set_name (struct variable *, const char *);
bool var_is_valid_name (const char *, bool issue_error);
bool var_is_plausible_name (const char *name, bool issue_error);

int compare_vars_by_name (const void *, const void *, const void *);
unsigned hash_var_by_name (const void *, const void *);

int compare_var_ptrs_by_name (const void *, const void *, const void *);
unsigned hash_var_ptr_by_name (const void *, const void *);

/* Variable types and widths. */
enum var_type var_get_type (const struct variable *);
int var_get_width (const struct variable *);
void var_set_width (struct variable *, int width);

typedef bool var_predicate_func (const struct variable *);

bool var_is_numeric (const struct variable *);
bool var_is_alpha (const struct variable *);
bool var_is_short_string (const struct variable *);
bool var_is_long_string (const struct variable *);
size_t var_get_value_cnt (const struct variable *);

/* Variables' missing values. */
const struct missing_values *var_get_missing_values (const struct variable *);
void var_set_missing_values (struct variable *, const struct missing_values *);
void var_clear_missing_values (struct variable *);
bool var_has_missing_values (const struct variable *);

bool var_is_value_missing (const struct variable *, const union value *,
                           enum mv_class);
bool var_is_num_missing (const struct variable *, double, enum mv_class);
bool var_is_str_missing (const struct variable *, const char[], enum mv_class);

/* Value labels. */
const struct val_labs *var_get_value_labels (const struct variable *);
bool var_has_value_labels (const struct variable *);
void var_set_value_labels (struct variable *, const struct val_labs *);
bool var_add_value_label (struct variable *,
                          const union value *, const char *);
void var_replace_value_label (struct variable *,
                              const union value *, const char *);
void var_clear_value_labels (struct variable *);
const char *var_lookup_value_label (const struct variable *,
                                    const union value *);
const char *var_get_value_name (const struct variable *, const union value *);

/* Print and write formats. */
const struct fmt_spec *var_get_print_format (const struct variable *);
void var_set_print_format (struct variable *, const struct fmt_spec *);
const struct fmt_spec *var_get_write_format (const struct variable *);
void var_set_write_format (struct variable *, const struct fmt_spec *);
void var_set_both_formats (struct variable *, const struct fmt_spec *);

/* Variable labels. */
const char *var_to_string (const struct variable *);
const char *var_get_label (const struct variable *);
void var_set_label (struct variable *, const char *);
void var_clear_label (struct variable *);
bool var_has_label (const struct variable *);

/* How data is measured. */
enum measure
  {
    MEASURE_NOMINAL = 0,
    MEASURE_ORDINAL = 1,
    MEASURE_SCALE = 2,
    n_MEASURES
  };

bool measure_is_valid (enum measure);
enum measure var_get_measure (const struct variable *);
void var_set_measure (struct variable *, enum measure);

/* GUI display width. */
int var_get_display_width (const struct variable *);
void var_set_display_width (struct variable *, int display_width);

int var_default_display_width (int width);

/* Alignment of data for display. */
enum alignment
  {
    ALIGN_LEFT = 0,
    ALIGN_RIGHT = 1,
    ALIGN_CENTRE = 2,
    n_ALIGN
  };

bool alignment_is_valid (enum alignment);
enum alignment var_get_alignment (const struct variable *);
void var_set_alignment (struct variable *, enum alignment);

/* Whether variables' values should be preserved from case to
   case. */
bool var_get_leave (const struct variable *);
void var_set_leave (struct variable *, bool leave);
bool var_must_leave (const struct variable *);

/* Short names. */
size_t var_get_short_name_cnt (const struct variable *);
const char *var_get_short_name (const struct variable *, size_t idx);
void var_set_short_name (struct variable *, size_t, const char *);
void var_clear_short_names (struct variable *);

/* Relationship with dictionary. */
size_t var_get_dict_index (const struct variable *);
size_t var_get_case_index (const struct variable *);

/* Variable auxiliary data. */
void *var_get_aux (const struct variable *);
void *var_attach_aux (const struct variable *,
                      void *aux, void (*aux_dtor) (struct variable *));
void var_clear_aux (struct variable *);
void *var_detach_aux (struct variable *);
void var_dtor_free (struct variable *);

/* Observed categorical values. */
struct cat_vals *var_get_obs_vals (const struct variable *);
void var_set_obs_vals (const struct variable *, struct cat_vals *);
bool var_has_obs_vals (const struct variable *);

/* Classes of variables. */
enum dict_class
  {
    DC_ORDINARY,                /* Ordinary identifier. */
    DC_SYSTEM,                  /* System variable. */
    DC_SCRATCH                  /* Scratch variable. */
  };

enum dict_class dict_class_from_id (const char *name);
const char *dict_class_to_name (enum dict_class dict_class);

#endif /* !variable.h */
