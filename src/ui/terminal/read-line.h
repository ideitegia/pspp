/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2011 Free Software Foundation, Inc.

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

#ifndef READLN_H
#define READLN_H

#include "libpspp/str.h"
#include "libpspp/getl.h"

void readln_initialize (void);
void readln_uninitialize (void);

struct getl_interface *create_readln_source (void);



#endif /* READLN_H */

