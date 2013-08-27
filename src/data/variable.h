/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#ifndef DATA_VARIABLE_H
#define DATA_VARIABLE_H 1

#include <stddef.h>
#include <stdbool.h>
#include "data/dict-class.h"
#include "data/missing-values.h"
#include "data/val-type.h"

/* Bitfields to identify traits of a variable */

#define VAR_TRAIT_NAME             0x0001
#define VAR_TRAIT_WIDTH            0x0002
#define VAR_TRAIT_ROLE             0x0004
#define VAR_TRAIT_LABEL            0x0008
#define VAR_TRAIT_VALUE_LABELS     0x0010
#define VAR_TRAIT_MISSING_VALUES   0x0020
#define VAR_TRAIT_ALIGNMENT        0x0040
#define VAR_TRAIT_MEASURE          0x0080
#define VAR_TRAIT_DISPLAY_WIDTH    0x0100
#define VAR_TRAIT_LEAVE            0x0200
#define VAR_TRAIT_POSITION         0x0400
#define VAR_TRAIT_ATTRIBUTES       0x0800
#define VAR_TRAIT_PRINT_FORMAT     0x1000
#define VAR_TRAIT_WRITE_FORMAT     0x2000


union value;

/* Variables.
   These functions should rarely be called directly: use
   dict_create_var, dict_clone_var, or dict_delete_var
   instead. */
struct variable *var_create (const char *name, int width);
struct variable *var_clone (const struct variable *);
void var_destroy (struct variable *);

/* Variable names. */
const char *var_get_name (const struct variable *);
void var_set_name (struct variable *, const char *);
enum dict_class var_get_dict_class (const struct variable *);

int compare_vars_by_name (const void *, const void *, const void *);
unsigned hash_var_by_name (const void *, const void *);

int compare_var_ptrs_by_name (const void *, const void *, const void *);
unsigned hash_var_ptr_by_name (const void *, const void *);

int compare_var_ptrs_by_dict_index (const void *, const void *, const void *);

struct fmt_spec;

/* Types and widths of values associated with a variable. */
enum val_type var_get_type (const struct variable *);
int var_get_width (const struct variable *);
void var_set_width (struct variable *, int width);
void var_set_width_and_formats (struct variable *v, int new_width,
				const struct fmt_spec *print, const struct fmt_spec *write);

bool var_is_numeric (const struct variable *);
bool var_is_alpha (const struct variable *);

/* Variables' missing values. */
const struct missing_values *var_get_missing_values (const struct variable *);
void var_set_missing_values (struct variable *, const struct missing_values *);
void var_clear_missing_values (struct variable *);
bool var_has_missing_values (const struct variable *);

bool var_is_value_missing (const struct variable *, const union value *,
                           enum mv_class);
bool var_is_num_missing (const struct variable *, double, enum mv_class);
bool var_is_str_missing (const struct variable *, const uint8_t[], enum mv_class);

/* Value labels. */
const char *var_lookup_value_label (const struct variable *,
                                    const union value *);
struct string;
void var_append_value_name (const struct variable *, const union value *,
			    struct string *);

const char *
var_get_value_name (const struct variable *v, const union value *value);


bool var_has_value_labels (const struct variable *);
const struct val_labs *var_get_value_labels (const struct variable *);
void var_set_value_labels (struct variable *, const struct val_labs *);

bool var_add_value_label (struct variable *,
                          const union value *, const char *);
void var_replace_value_label (struct variable *,
                              const union value *, const char *);
void var_clear_value_labels (struct variable *);

/* Print and write formats. */
const struct fmt_spec *var_get_print_format (const struct variable *);
void var_set_print_format (struct variable *, const struct fmt_spec *);
const struct fmt_spec *var_get_write_format (const struct variable *);
void var_set_write_format (struct variable *, const struct fmt_spec *);
void var_set_both_formats (struct variable *, const struct fmt_spec *);

struct fmt_spec var_default_formats (int width);

/* Variable labels. */
const char *var_to_string (const struct variable *);
const char *var_get_label (const struct variable *);
bool var_set_label (struct variable *, const char *label, bool issue_warning);
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
const char *measure_to_string (enum measure);
const char *measure_to_syntax (enum measure);

enum measure var_get_measure (const struct variable *);
void var_set_measure (struct variable *, enum measure);

enum measure var_default_measure (enum val_type);

/* Intended usage of a variable, for populating dialogs. */
enum var_role
  {
    ROLE_INPUT,
    ROLE_OUTPUT,
    ROLE_BOTH,
    ROLE_NONE,
    ROLE_PARTITION,
    ROLE_SPLIT
  };

bool var_role_is_valid (enum var_role);
const char *var_role_to_string (enum var_role);
const char *var_role_to_syntax (enum var_role);

enum var_role var_get_role (const struct variable *);
void var_set_role (struct variable *, enum var_role);

/* GUI display width. */
int var_get_display_width (const struct variable *);
void var_set_display_width (struct variable *, int display_width);

int var_default_display_width (int width);

/* Alignment of data for display. */
enum alignment
  {
    ALIGN_LEFT = 0,
    ALIGN_RIGHT = 1,
    ALIGN_CENTRE = 2
  };

bool alignment_is_valid (enum alignment);
const char *alignment_to_string (enum alignment);
const char *alignment_to_syntax (enum alignment);

enum alignment var_get_alignment (const struct variable *);
void var_set_alignment (struct variable *, enum alignment);

enum alignment var_default_alignment (enum val_type);

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

/* Custom attributes. */
struct attrset *var_get_attributes (const struct variable *);
void var_set_attributes (struct variable *, const struct attrset *);
bool var_has_attributes (const struct variable *);

/* Encoding. */
const char *var_get_encoding (const struct variable *);

/* Function types. */
typedef bool var_predicate_func (const struct variable *);

#endif /* data/variable.h */
