/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
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

#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <stddef.h>

/* Dictionary. */ 

struct variable;
struct dictionary *dict_create (void);
struct dictionary *dict_clone (const struct dictionary *);
void dict_clear (struct dictionary *);
void dict_clear_aux (struct dictionary *);
void dict_destroy (struct dictionary *);

size_t dict_get_var_cnt (const struct dictionary *);
struct variable *dict_get_var (const struct dictionary *, size_t idx);
void dict_get_vars (const struct dictionary *,
                    struct variable ***vars, size_t *cnt,
                    unsigned exclude_classes);

struct variable *dict_create_var (struct dictionary *, const char *,
                                  int width);
struct variable *dict_create_var_assert (struct dictionary *, const char *,
                                  int width);
struct variable *dict_clone_var (struct dictionary *, const struct variable *,
                                 const char *);
void dict_rename_var (struct dictionary *, struct variable *, const char *);

struct variable *dict_lookup_var (const struct dictionary *, const char *);
struct variable *dict_lookup_var_assert (const struct dictionary *,
                                         const char *);
int dict_contains_var (const struct dictionary *, const struct variable *);
void dict_delete_var (struct dictionary *, struct variable *);
void dict_delete_vars (struct dictionary *,
                       struct variable *const *, size_t count);
void dict_reorder_vars (struct dictionary *,
                        struct variable *const *, size_t count);
int dict_rename_vars (struct dictionary *,
                      struct variable **, char **new_names,
                      size_t count, char **err_name);

struct ccase;
struct variable *dict_get_weight (const struct dictionary *);
double dict_get_case_weight (const struct dictionary *, 
			     const struct ccase *, int *);
void dict_set_weight (struct dictionary *, struct variable *);

struct variable *dict_get_filter (const struct dictionary *);
void dict_set_filter (struct dictionary *, struct variable *);

int dict_get_case_limit (const struct dictionary *);
void dict_set_case_limit (struct dictionary *, int);

int dict_get_next_value_idx (const struct dictionary *);
size_t dict_get_case_size (const struct dictionary *);

void dict_compact_values (struct dictionary *);
void dict_compact_case (const struct dictionary *,
                        struct ccase *, const struct ccase *);
size_t dict_get_compacted_value_cnt (const struct dictionary *);
int *dict_get_compacted_idx_to_fv (const struct dictionary *);

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

#endif /* dictionary.h */
