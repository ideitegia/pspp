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

/* Sliding window over a set of cases.

   A casewindow is a queue of cases: cases may be added at the
   head of the queue and deleted from the tail.  A casewindow is
   initially maintained in memory and then, should it grow too
   large, is dumped to disk.

   Any case in the casewindow may be accessed, not just the case
   at the head.  Cases are numbered relative to the tail: the
   least recently added case is number 0, and so on. */

#ifndef DATA_CASEWINDOW_H
#define DATA_CASEWINDOW_H 1

#include "data/case.h"

struct caseproto;

struct casewindow *casewindow_create (const struct caseproto *,
                                      casenumber max_in_core_cases);
bool casewindow_destroy (struct casewindow *);

void casewindow_push_head (struct casewindow *, struct ccase *);
void casewindow_pop_tail (struct casewindow *, casenumber cnt);
struct ccase *casewindow_get_case (const struct casewindow *,
                                   casenumber case_idx);
const struct caseproto *casewindow_get_proto (const struct casewindow *);
casenumber casewindow_get_case_cnt (const struct casewindow *);

bool casewindow_error (const struct casewindow *);
void casewindow_force_error (struct casewindow *);
const struct taint *casewindow_get_taint (const struct casewindow *);

#endif /* data/casewindow.h */
