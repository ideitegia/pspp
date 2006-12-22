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
#include "fastfile-factory.h"
#include "fastfile.h"


struct fastfile_factory
 {
   struct casefile_factory parent;
 };


static struct casefile *
produce_fastfile(struct casefile_factory *this UNUSED, size_t value_cnt)
{
  return fastfile_create (value_cnt);
}


struct casefile_factory *
fastfile_factory_create (void)
{
  struct fastfile_factory *fact = xzalloc (sizeof (*fact));

  fact->parent.create_casefile = produce_fastfile;

  return (struct casefile_factory *) fact;
}


void
fastfile_factory_destroy (struct casefile_factory *factory)
{
  free (factory);
}
