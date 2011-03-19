/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011 Free Software Foundation, Inc.

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

/* Lazy casereader.

   A "lazy casereader" is a casereader that saves an underlying
   casereader from the need to be instantiated in the case where
   it is never used.  If any casereader operation is ever
   performed on a lazy casereader, it invokes a callback function
   (provided by the lazy casereader's creator) to instantiate the
   underlying reader. */

#ifndef DATA_LAZY_CASEREADER_H
#define DATA_LAZY_CASEREADER_H 1

#include <stdbool.h>
#include "data/case.h"

struct casereader *lazy_casereader_create (const struct caseproto *,
                                           casenumber case_cnt,
                                           struct casereader *(*) (void *aux),
                                           void *aux,
                                           unsigned long int *serial);
bool lazy_casereader_destroy (struct casereader *, unsigned long int serial);

#endif /* data/lazy-casereader.h */
