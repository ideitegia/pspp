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

#include <stdlib.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include "flexifile-factory.h"
#include <ui/flexifile.h>
#include <data/casefile-factory.h>


struct flexifile_factory
 {
   struct casefile_factory parent;
 };


static struct casefile *
produce_flexifile(struct casefile_factory *this UNUSED, size_t value_cnt)
{
  struct casefile *ff =  flexifile_create (value_cnt);

  return ff;
}


struct casefile_factory *
flexifile_factory_create (void)
{
  struct flexifile_factory *fact = xzalloc (sizeof (*fact));

  fact->parent.create_casefile = produce_flexifile;

  return (struct casefile_factory *) fact;
}


void
flexifile_factory_destroy (struct casefile_factory *factory)
{
  free (factory);
}
