/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
   and Ben Pfaff <blp@gnu.org>.

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

#ifndef __PSPP_ASSERT_H
#define __PSPP_ASSERT_H

#include <stdlib.h>
#include "compiler.h"

#define NOT_REACHED() do { assert (0); abort (); } while (0)

#endif

#include <assert.h>

#ifndef ASSERT_LEVEL
#define ASSERT_LEVEL 2
#endif

#undef expensive_assert
#if ASSERT_LEVEL >= 5
#define expensive_assert(EXPR) assert (EXPR)
#else
#define expensive_assert(EXPR) ((void) 0)
#endif
