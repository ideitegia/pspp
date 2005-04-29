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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#ifndef INCLUDED_DATA_LIST_H
#define INCLUDED_DATA_LIST_H

/* FIXME: This header is a kluge and should go away when we come
   up with a less-klugy solution. */

#include "var.h"
#include "vfm.h"

trns_proc_func repeating_data_trns_proc;
void repeating_data_set_write_case (struct trns_header *,
                                    write_case_func *, write_case_data);

#endif /* data-list.h */
