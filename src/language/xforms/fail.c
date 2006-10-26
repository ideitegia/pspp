/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.
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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>

#include <stdlib.h>

#include <data/case.h>
#include <data/procedure.h>
#include <data/transformations.h>
#include <language/command.h>
#include <language/lexer/lexer.h>

static int trns_fail (void *x, struct ccase *c, casenumber n);



/* A transformation which is guaranteed to fail. */

static int 
trns_fail (void *x UNUSED, struct ccase *c UNUSED, 
	   casenumber n UNUSED)
{
  return TRNS_ERROR;
}


int
cmd_debug_xform_fail (struct dataset *ds)
{

  add_transformation (ds, trns_fail, NULL, NULL);

  return lex_end_of_command ();
}
