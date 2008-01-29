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

#include <config.h>
#include <data/variable.h>
#include "psppire-var-ptr.h"


/* This module registers a type with Glib to hold pointers
   to a {struct variable}.  It also registers some tranformation functions so
   that variables may be converted to strings and ints.
   Note that the type is just a pointer.  It's the user's responsibility to
   ensure that it always points to something valid.
*/


/* Shallow copy the pointer */
static gpointer
variable_copy (gpointer var)
{
  return var;
}

/* Do nothing. It's a pointer only! */
static void
variable_free (gpointer var)
{
}


/* Convert to a string, by using the variable's name */
static void
variable_to_string (const GValue *src,
		    GValue *dest)
{
  const struct variable *v = g_value_get_boxed (src);

  if ( v == NULL)
    g_value_set_string (dest, "");
  else
    g_value_set_string (dest, var_get_name (v));
}


/* Convert to an int, using the dictionary index. */
static void
variable_to_int (const GValue *src,
		 GValue *dest)
{
  const struct variable *v = g_value_get_boxed (src);

  if ( v == NULL)
    g_value_set_int (dest, -1);
  else
    g_value_set_int (dest, var_get_dict_index (v));
}




GType
psppire_var_ptr_get_type (void)
{
  static GType t = 0;

  if (t == 0 )
    {
      t = g_boxed_type_register_static  ("psppire-var-ptr",
					 (GBoxedCopyFunc) variable_copy,
					 (GBoxedFreeFunc) variable_free);

      g_value_register_transform_func (t, G_TYPE_STRING,
				       variable_to_string);

      g_value_register_transform_func (t, G_TYPE_INT,
				       variable_to_int);

    }

  return t;
}


