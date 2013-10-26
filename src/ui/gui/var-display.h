/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2011, 2013  Free Software Foundation

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


#ifndef VAR_DISPLAY_H
#define VAR_DISPLAY 1

#include <glib.h>
#include <data/variable.h>
#include "psppire-dict.h"

struct variable;

#define n_ALIGNMENTS 3

gchar *missing_values_to_string (const struct variable *pv, GError **err);

#endif
