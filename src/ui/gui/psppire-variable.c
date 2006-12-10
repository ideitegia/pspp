/* 
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2004, 2006  Free Software Foundation
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

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <data/missing-values.h>
#include <data/value-labels.h>
#include <data/format.h>
#include <libpspp/message.h>

#include <libpspp/misc.h>

#include "psppire-variable.h"
#include "psppire-dict.h"



gboolean
psppire_variable_set_name(struct PsppireVariable *pv, const gchar *text)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  if ( !text) 
    return FALSE;

  if ( 0 == strcmp(var_get_name (pv->v), text))
    return FALSE;

  if ( ! psppire_dict_check_name(pv->dict, text, TRUE) )
    return FALSE;

  dict_rename_var(pv->dict->dict, pv->v, text);

  psppire_dict_var_changed(pv->dict, var_get_dict_index (pv->v));

  return TRUE;
}


gboolean
psppire_variable_set_columns(struct PsppireVariable *pv, gint columns)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  var_set_display_width (pv->v, columns);
  
  psppire_dict_var_changed(pv->dict, var_get_dict_index (pv->v));

  return TRUE;
}

gboolean
psppire_variable_set_label(struct PsppireVariable *pv, const gchar *label)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  var_set_label (pv->v, label);

  psppire_dict_var_changed(pv->dict, var_get_dict_index (pv->v));

  return TRUE;
}


gboolean
psppire_variable_set_decimals(struct PsppireVariable *pv, gint decimals)
{
  struct fmt_spec fmt;

  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  fmt = *var_get_write_format (pv->v);
  fmt.d = decimals;

  return psppire_variable_set_format(pv, &fmt);
}



gboolean
psppire_variable_set_width(struct PsppireVariable *pv, gint width)
{
  struct fmt_spec fmt ;
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  fmt = *var_get_write_format (pv->v);
  fmt.w = width;

  if (var_is_alpha (pv->v))
    {
      gint old_var_cnt , new_var_cnt ;

      if ( var_get_width (pv->v) == 0 ) 
	old_var_cnt = 1;
      else
	old_var_cnt = DIV_RND_UP(var_get_width (pv->v), MAX_SHORT_STRING);
      
      new_var_cnt = DIV_RND_UP(width, MAX_SHORT_STRING);
      var_set_width (pv->v, width);

      psppire_dict_resize_variable(pv->dict, pv,
				   old_var_cnt, new_var_cnt);
    }

  return psppire_variable_set_format(pv, &fmt);
}


gboolean
psppire_variable_set_type(struct PsppireVariable *pv, int type)
{
  gint old_var_cnt , new_var_cnt ;

  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  if ( var_get_width (pv->v) ) 
    old_var_cnt = 1;
  else
    old_var_cnt = DIV_RND_UP (var_get_width (pv->v), MAX_SHORT_STRING);

  var_set_width (pv->v, type == VAR_NUMERIC ? 0 : 1);

  if ( var_get_width (pv->v) == 0 ) 
    new_var_cnt = 1;
  else
    new_var_cnt = DIV_RND_UP (var_get_width (pv->v), MAX_SHORT_STRING);

  psppire_dict_resize_variable(pv->dict, pv,
			       old_var_cnt, new_var_cnt);

  psppire_dict_var_changed(pv->dict, var_get_dict_index (pv->v));
  return TRUE;
}


gboolean
psppire_variable_set_format(struct PsppireVariable *pv, struct fmt_spec *fmt)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  msg_disable ();
  if ( fmt_check_output(fmt) 
       && fmt_check_type_compat (fmt, var_get_type (pv->v))
       && fmt_check_width_compat (fmt, var_get_width (pv->v))) 
    {
      msg_enable ();
      var_set_both_formats (pv->v, fmt);
      psppire_dict_var_changed(pv->dict, var_get_dict_index (pv->v));
      return TRUE;
    }
  msg_enable ();

  return FALSE;
}


gboolean
psppire_variable_set_value_labels(const struct PsppireVariable *pv,
			       const struct val_labs *vls)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  var_set_value_labels (pv->v, vls);

  psppire_dict_var_changed(pv->dict, var_get_dict_index (pv->v));
  return TRUE;
}

gboolean 
psppire_variable_set_missing(const struct PsppireVariable *pv,
			  const struct missing_values *miss)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  var_set_missing_values (pv->v, miss);

  psppire_dict_var_changed(pv->dict, var_get_dict_index (pv->v));
  return TRUE;
}

gboolean
psppire_variable_set_write_spec(const struct PsppireVariable *pv, struct fmt_spec fmt)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  var_set_write_format (pv->v, &fmt);

  psppire_dict_var_changed(pv->dict, var_get_dict_index (pv->v));
  return TRUE;
}

gboolean
psppire_variable_set_print_spec(const struct PsppireVariable *pv, struct fmt_spec fmt)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  var_set_print_format (pv->v, &fmt);

  psppire_dict_var_changed(pv->dict, var_get_dict_index (pv->v));
  return TRUE;
}



gboolean
psppire_variable_set_alignment(struct PsppireVariable *pv, gint align)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  var_set_alignment (pv->v, align);

  psppire_dict_var_changed(pv->dict, var_get_dict_index (pv->v));
  return TRUE;
}


gboolean
psppire_variable_set_measure(struct PsppireVariable *pv, gint measure)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  var_set_measure (pv->v, measure + 1);

  psppire_dict_var_changed(pv->dict, var_get_dict_index (pv->v));
  return TRUE;
}


const struct fmt_spec *
psppire_variable_get_write_spec(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, NULL);
  g_return_val_if_fail(pv->v, NULL);

  return var_get_write_format (pv->v);
}


const gchar *
psppire_variable_get_name(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, NULL);
  g_return_val_if_fail(pv->v, NULL);

  return var_get_name (pv->v);
}


gint
psppire_variable_get_columns(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return var_get_display_width (pv->v);
}



const gchar *
psppire_variable_get_label(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, NULL);
  g_return_val_if_fail(pv->v, NULL);

  return var_get_label (pv->v);
}


const struct missing_values *
psppire_variable_get_missing(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, NULL);
  g_return_val_if_fail(pv->v, NULL);

  return var_get_missing_values (pv->v);
}


const struct val_labs *
psppire_variable_get_value_labels(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, NULL);
  g_return_val_if_fail(pv->v, NULL);

  return var_get_value_labels (pv->v);
}


gint
psppire_variable_get_alignment(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return var_get_alignment (pv->v);
}



gint
psppire_variable_get_measure(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return var_get_measure (pv->v) - 1;
}

gint
psppire_variable_get_type(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return var_get_type (pv->v);
}


gint
psppire_variable_get_width(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return var_get_width (pv->v);
}


gint
psppire_variable_get_fv(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return var_get_case_index (pv->v);
}



gint
psppire_variable_get_index(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return var_get_dict_index (pv->v);
}

