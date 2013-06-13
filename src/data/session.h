/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#ifndef SESSION_H
#define SESSION_H 1

#include <stddef.h>

struct dataset;

struct session *session_create (struct session *parent);
void session_destroy (struct session *);

struct dataset *session_active_dataset (struct session *);
void session_set_active_dataset (struct session *, struct dataset *);

void session_add_dataset (struct session *, struct dataset *);
void session_remove_dataset (struct session *, struct dataset *);
struct dataset *session_lookup_dataset (const struct session *, const char *);
struct dataset *session_lookup_dataset_assert (const struct session *,
                                               const char *);

void session_set_default_syntax_encoding (struct session *, const char *);
const char *session_get_default_syntax_encoding (const struct session *);

size_t session_n_datasets (const struct session *);
void session_for_each_dataset (const struct session *,
                               void (*cb) (struct dataset *, void *aux),
                               void *aux);

struct dataset *session_get_dataset_by_seqno (const struct session *,
                                              unsigned int seqno);

char *session_generate_dataset_name (struct session *);

#endif /* session.h */
