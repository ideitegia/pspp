/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#if !npar_h
#define npar_h 1

#include <stddef.h>
#include <data/missing-values.h>

#include <stddef.h>
#include <data/missing-values.h>
 
typedef struct variable *variable_pair[2];

struct hsh_table;
struct const_hsh_table;
struct casefilter;
struct casereader;
struct dataset;

struct npar_test
{
  void (*execute) (const struct dataset *, 
		   struct casereader *,
                   enum mv_class exclude,
		   const struct npar_test *
		   );

  void (*insert_variables) (const struct npar_test *, 
			    struct const_hsh_table *);
};

struct one_sample_test
{
  struct npar_test parent;
  const struct variable **vars;
  size_t n_vars;
};

struct two_sample_test
{
  struct npar_test parent;
  variable_pair *pairs;
  size_t n_pairs;
};


#endif
