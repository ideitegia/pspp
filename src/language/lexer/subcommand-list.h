#ifndef SUBCLIST_H
#define SUBCLIST_H

/* subclist - lists for PSPP subcommands

   Copyright (C) 2004 Free Software Foundation, Inc.



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



#include <sys/types.h>

/* This module provides a rudimentary list class
   It is intended for use by the command line parser for list subcommands
*/


struct subc_list_double {
  double *data ;
  size_t sz;
  int n_data;
};

struct subc_list_int {
  int *data ;
  size_t sz;
  int n_data;
};


typedef struct subc_list_double subc_list_double ;
typedef struct subc_list_int subc_list_int ;

/* Create a  list */
void subc_list_double_create(subc_list_double *l) ;
void subc_list_int_create(subc_list_int *l) ;

/* Push a value onto the list */
void subc_list_double_push(subc_list_double *l, double d) ;
void subc_list_int_push(subc_list_int *l, int i) ;

/* Index into the list */
double subc_list_double_at(const subc_list_double *l, int idx);
int subc_list_int_at(const subc_list_int *l, int idx);

/* Return the number of values in the list */
int subc_list_double_count(const subc_list_double *l);
int subc_list_int_count(const subc_list_int *l);

/* Destroy the list */
void subc_list_double_destroy(subc_list_double *l) ;
void subc_list_int_destroy(subc_list_int *l) ;


#endif
