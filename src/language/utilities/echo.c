/* PSPP - computes sample statistics. -*-c-*-

   Copyright (C) 2005 Free Software Foundation, Inc.

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
#include <libpspp/message.h>
#include <libpspp/alloc.h>
#include <libpspp/str.h>
#include <language/lexer/lexer.h>
#include <language/command.h>
#include <output/table.h>
#include <output/manager.h>

/* Echos a string to the output stream */
int
cmd_echo (struct lexer *lexer, struct dataset *ds UNUSED)
{
  struct tab_table *tab;

  if (lex_token (lexer) != T_STRING) 
    return CMD_FAILURE;
  
  tab = tab_create(1, 1, 0);

  tab_dim (tab, tab_natural_dimensions);
  tab_flags (tab, SOMF_NO_TITLE );

  tab_text(tab, 0, 0, 0, ds_cstr (lex_tokstr (lexer)));

  tab_submit(tab);

  return CMD_SUCCESS;
}
