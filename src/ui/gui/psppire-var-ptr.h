/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __PSPPIRE_VAR_PTR_TYPE__
#define __PSPPIRE_VAR_PTR_TYPE__ 1

#include <glib-object.h>
#include <glib.h>


/* This module registers a type with Glib to hold pointers
   to a {struct variable}.  It also registers some tranformation functions so
   that variables may be converted to strings and ints.
   Note that the type is just a pointer.  It's the user's responsibility to
   ensure that it always points to something valid.

   The intended use of this module is to assist gui code which has to display
   variables (eg in a GtkTreeView).
*/

G_BEGIN_DECLS

GType psppire_var_ptr_get_type (void);

#define PSPPIRE_VAR_PTR_TYPE            (psppire_var_ptr_get_type ())

G_END_DECLS

#endif /* __PSPPIRE_VAR_PTR_H__ */

