/* 
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2006  Free Software Foundation
    Written by John Darrington

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA. */


#ifndef __PSPPIRE_VARIABLE_H__
#define __PSPPIRE_VARIABLE_H__

#include <glib-object.h>
#include <glib.h>

#include <variable.h>
#include "psppire-dict.h"

/* Don't use any of these members.
   Use accessor functions instead.
*/
struct PsppireVariable
{
  /* The payload */
  struct variable *v;

  /* The dictionary to which this variable belongs */
  PsppireDict *dict;
};



gboolean psppire_variable_set_name(struct PsppireVariable *pv, const gchar *text);

gboolean psppire_variable_set_columns(struct PsppireVariable *pv, gint columns);
gboolean psppire_variable_set_label(struct PsppireVariable *pv, const gchar *label);
gboolean psppire_variable_set_format(struct PsppireVariable *pv, struct fmt_spec *fmt);
gboolean psppire_variable_set_decimals(struct PsppireVariable *pv, gint decimals);
gboolean psppire_variable_set_width(struct PsppireVariable *pv, gint width);
gboolean psppire_variable_set_alignment(struct PsppireVariable *pv, gint align);
gboolean psppire_variable_set_measure(struct PsppireVariable *pv, gint measure);
gboolean psppire_variable_set_value_labels(const struct PsppireVariable *pv,
					const struct val_labs *vls);

gboolean psppire_variable_set_missing(const struct PsppireVariable *pv,
				   const struct missing_values *miss);
 
gboolean psppire_variable_set_print_spec(const struct PsppireVariable *pv, struct fmt_spec fmt);
gboolean psppire_variable_set_write_spec(const struct PsppireVariable *pv, struct fmt_spec fmt);

gboolean psppire_variable_set_type(struct PsppireVariable *pv, int type);



const struct fmt_spec *psppire_variable_get_write_spec(const struct PsppireVariable *pv);
const gchar * psppire_variable_get_name(const struct PsppireVariable *pv);

gint psppire_variable_get_columns(const struct PsppireVariable *pv);

const gchar * psppire_variable_get_label(const struct PsppireVariable *pv);


const struct missing_values *psppire_variable_get_missing
                                     (const struct PsppireVariable *pv);

const struct val_labs * psppire_variable_get_value_labels
                                     (const struct PsppireVariable *pv);

gint psppire_variable_get_alignment(const struct PsppireVariable *pv);

gint psppire_variable_get_measure(const struct PsppireVariable *pv);

gint psppire_variable_get_index(const struct PsppireVariable *pv);

gint psppire_variable_get_type(const struct PsppireVariable *pv);

gint psppire_variable_get_width(const struct PsppireVariable *pv);



#endif /* __PSPPIRE_VARIABLE_H__ */
