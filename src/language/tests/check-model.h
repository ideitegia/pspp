/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

/* PSPP syntax interface to model checker.

   A model checker is a software testing tool.  PSPP includes a
   generic model checker in libpspp/model-checker.[ch].  This
   module layers a PSPP syntax interface on top of the model
   checker's options. */

#ifndef LANGUAGE_TESTS_CHECK_MODEL
#define LANGUAGE_TESTS_CHECK_MODEL 1

#include <stdbool.h>

struct lexer;
struct mc_options;
struct mc_results;

bool check_model (struct lexer *lexer,
                  struct mc_results *(*checker) (struct mc_options *, void *),
                  void *aux);

#endif /* check-model.h */
