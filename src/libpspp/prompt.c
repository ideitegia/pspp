/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#include <config.h>

#include "libpspp/prompt.h"

const char *
prompt_style_to_string (enum prompt_style style)
{
  switch (style)
    {
    case PROMPT_FIRST:
      return "first";
    case PROMPT_LATER:
      return "later";
    case PROMPT_DATA:
      return "data";
    case PROMPT_COMMENT:
      return "COMMENT";
    case PROMPT_DOCUMENT:
      return "DOCUMENT";
    case PROMPT_DO_REPEAT:
      return "DO REPEAT";
    default:
      return "unknown prompt";
    }
}

