/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <ctype.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "getline.h"
#include "lexer.h"
#include "str.h"

int
cmd_include_at (void)
{
  char *incfn, *s, *bp, *ep;

  s = bp = lex_entire_line ();
  while (isspace ((unsigned char) *bp))
    bp++;
  bp++;				/* skip `@' */
  while (isspace ((unsigned char) *bp))
    bp++;
  if (*bp == '\'')
    bp++;

  ep = bp + strlen (bp);
  while (isspace ((unsigned char) *--ep));
  if (*ep != '\'')
    ep++;

  if (ep <= bp)
    {
      msg (SE, _("Unrecognized filename format."));
      return CMD_FAILURE;
    }

  /* Now the filename is trapped between bp and ep. */
  incfn = xmalloc (ep - bp + 1);
  strncpy (incfn, bp, ep - bp);
  incfn[ep - bp] = 0;
  getl_include (incfn);
  free (incfn);

  return CMD_SUCCESS;
}

int
cmd_include (void)
{
  lex_get ();

  if (!lex_force_string ())
    return CMD_SUCCESS;
  getl_include (ds_value (&tokstr));

  lex_get ();
  return lex_end_of_command ();
}
