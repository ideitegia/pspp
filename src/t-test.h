/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

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

#ifndef T_TEST_H
#define T_TEST_H

#include "val.h"

#include "group.h"

/* T-TEST private data */
struct t_test_proc
{
  /* Stats for the `universal group' */
  struct group_statistics ugs;

  /* Number of groups */
  int n_groups ;

  /* Stats for individual groups */
  struct group_statistics *gs;

  /* The levene statistic */
  double levene ;
};

#endif
