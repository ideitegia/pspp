/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2007 Free Software Foundation, Inc.

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

#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <stdbool.h>
#include <stddef.h>

/* Dictionary. */

struct variable;
struct dictionary;
struct string;

struct dict_callbacks
 {
  void (*var_added) (struct dictionary *, int, void *);
  void (*var_deleted) (struct dictionary *, int, int, int, void *);
  void (*var_changed) (struct dictionary *, int, void *);
  void (*var_resized) (struct dictionary *, int, int, void *);
  void (*weight_changed) (struct dictionary *, int, void *);
  void (*filter_changed) (struct dictionary *, int, void *);
  void (*split_changed) (struct dictionary *, void *);
 };


struct dictionary *dict_create (void);
struct dictionary *dict_clone (const struct dictionary *);
void dict_set_callbacks (struct dictionary *, const struct dict_callbacks *,
			 void *);
void dict_copy_callbacks (struct dictionary *, const struct dictionary *);


void dict_clear (struct dictionary *);
void dict_clear_aux (struct dictionary *);
void dict_destroy (struct dictionary *);

size_t dict_get_var_cnt (const struct dictionary *);
struct variable *dict_get_var (const struct dictionary *, size_t idx);
inline void dict_get_vars (const struct dictionary *,
                    const struct variable ***vars, size_t *cnt,
                    unsigned exclude_classes);
void dict_get_vars_mutable (const struct dictionary *,
                    struct variable ***vars, size_t *cnt,
                    unsigned exclude_classes);

struct variable *dict_create_var (struct dictionary *, const char *,
                                  int width);

struct variable *dict_create_var_assert (struct dictionary *, const char *,
                                  int width);
struct variable *dict_clone_var (struct dictionary *, const struct variable *,
                                 const char *);
struct variable *dict_clone_var_assert (struct dictionary *,
                                        const struct variable *, const char *);

struct variable *dict_lookup_var (const struct dictionary *, const char *);
struct variable *dict_lookup_var_assert (const struct dictionary *,
                                         const char *);
bool dict_contains_var (const struct dictionary *, const struct variable *);
void dict_delete_var (struct dictionary *, struct variable *);
void dict_delete_vars (struct dictionary *,
                       struct variable *const *, size_t count);
void dict_delete_consecutive_vars (struct dictionary *d,
                                   size_t idx, size_t count);
void dict_delete_scratch_vars (struct dictionary *);
void dict_reorder_var (struct dictionary *d, struct variable *v,
                       size_t new_index);
void dict_reorder_vars (struct dictionary *,
                        struct variable *const *, size_t count);
void dict_rename_var (struct dictionary *, struct variable *, const char *);
bool dict_rename_vars (struct dictionary *,
                       struct variable **, char **new_names,
                       size_t count, char **err_name);

struct ccase;
struct variable *dict_get_weight (const struct dictionary *);
double dict_get_case_weight (const struct dictionary *,
			     const struct ccase *, bool *);
void dict_set_weight (struct dictionary *, struct variable *);

struct variable *dict_get_filter (const struct dictionary *);
void dict_set_filter (struct dictionary *, struct variable *);

size_t dict_get_case_limit (const struct dictionary *);
void dict_set_case_limit (struct dictionary *, size_t);

int dict_get_next_value_idx (const struct dictionary *);
size_t dict_get_case_size (const struct dictionary *);

size_t dict_count_values (const struct dictionary *,
                          unsigned int exclude_classes);
void dict_compact_values (struct dictionary *);

const struct variable *const *dict_get_split_vars (const struct dictionary *);
size_t dict_get_split_cnt (const struct dictionary *);
void dict_set_split_vars (struct dictionary *,
                          struct variable *const *, size_t cnt);
void dict_unset_split_var (struct dictionary *d,
			   struct variable *v);

const char *dict_get_label (const struct dictionary *);
void dict_set_label (struct dictionary *, const char *);

/* Fixed length of lines in dictionary documents. */
#define DOC_LINE_LENGTH 80

const char *dict_get_documents (const struct dictionary *);
void dict_set_documents (struct dictionary *, const char *);
void dict_clear_documents (struct dictionary *);
void dict_add_document_line (struct dictionary *, const char *);
size_t dict_get_document_line_cnt (const struct dictionary *);
void dict_get_document_line (const struct dictionary *,
                             size_t, struct string *);

bool dict_create_vector (struct dictionary *,
                         const char *name,
                         struct variable **, size_t cnt);
void dict_create_vector_assert (struct dictionary *,
                                const char *name,
                                struct variable **, size_t cnt);
const struct vector *dict_get_vector (const struct dictionary *,
                                      size_t idx);
size_t dict_get_vector_cnt (const struct dictionary *);
const struct vector *dict_lookup_vector (const struct dictionary *,
                                         const char *name);
void dict_clear_vectors (struct dictionary *);

void dict_dump (const struct dictionary *);

#endif /* dictionary.h */
