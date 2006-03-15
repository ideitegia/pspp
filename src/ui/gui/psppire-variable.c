/* 
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2004  Free Software Foundation
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

#include <string.h>
#include <stdlib.h>

#include <data/missing-values.h>
#include <data/value-labels.h>
#include <data/format.h>

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

  if ( 0 == strcmp(pv->v->name, text))
    return FALSE;

  if ( ! psppire_dict_check_name(pv->dict, text, TRUE) )
    return FALSE;

  dict_rename_var(pv->dict->dict, pv->v, text);

  psppire_dict_var_changed(pv->dict, pv->v->index);

  return TRUE;
}


gboolean
psppire_variable_set_columns(struct PsppireVariable *pv, gint columns)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  pv->v->display_width = columns;
  
  psppire_dict_var_changed(pv->dict, pv->v->index);

  return TRUE;
}

gboolean
psppire_variable_set_label(struct PsppireVariable *pv, const gchar *label)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  g_free(pv->v->label);
  pv->v->label = g_strdup(label);

  psppire_dict_var_changed(pv->dict, pv->v->index);

  return TRUE;
}


gboolean
psppire_variable_set_decimals(struct PsppireVariable *pv, gint decimals)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  struct fmt_spec fmt = pv->v->write;

  fmt.d = decimals;

  return psppire_variable_set_format(pv, &fmt);
}



gboolean
psppire_variable_set_width(struct PsppireVariable *pv, gint width)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  struct fmt_spec fmt = pv->v->write;

  fmt.w = width;

  if ( pv->v->type == ALPHA ) 
    pv->v->width = width;

  return psppire_variable_set_format(pv, &fmt);
}


gboolean
psppire_variable_set_type(struct PsppireVariable *pv, int type)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  pv->v->type = type; 

  if ( type == NUMERIC ) 
    pv->v->width = 0;

  psppire_dict_var_changed(pv->dict, pv->v->index);
  return TRUE;
}


gboolean
psppire_variable_set_format(struct PsppireVariable *pv, struct fmt_spec *fmt)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  if ( check_output_specifier(fmt, false) 
       && 
       check_specifier_type(fmt, pv->v->type, false)
       && 
       check_specifier_width(fmt, pv->v->width, false)
       ) 
    {
      pv->v->write = pv->v->print = *fmt;
      psppire_dict_var_changed(pv->dict, pv->v->index);
      return TRUE;
    }

  return FALSE;
}


gboolean
psppire_variable_set_value_labels(const struct PsppireVariable *pv,
			       const struct val_labs *vls)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  val_labs_destroy(pv->v->val_labs);
  pv->v->val_labs = val_labs_copy(vls);

  psppire_dict_var_changed(pv->dict, pv->v->index);
  return TRUE;
}

gboolean 
psppire_variable_set_missing(const struct PsppireVariable *pv,
			  const struct missing_values *miss)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  mv_copy(&pv->v->miss, miss);

  psppire_dict_var_changed(pv->dict, pv->v->index);
  return TRUE;
}

gboolean
psppire_variable_set_write_spec(const struct PsppireVariable *pv, struct fmt_spec fmt)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  pv->v->write = fmt;

  psppire_dict_var_changed(pv->dict, pv->v->index);
  return TRUE;
}

gboolean
psppire_variable_set_print_spec(const struct PsppireVariable *pv, struct fmt_spec fmt)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  pv->v->print = fmt;

  psppire_dict_var_changed(pv->dict, pv->v->index);
  return TRUE;
}



gboolean
psppire_variable_set_alignment(struct PsppireVariable *pv, gint align)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  pv->v->alignment = align;

  psppire_dict_var_changed(pv->dict, pv->v->index);
  return TRUE;
}


gboolean
psppire_variable_set_measure(struct PsppireVariable *pv, gint measure)
{
  g_return_val_if_fail(pv, FALSE);
  g_return_val_if_fail(pv->dict, FALSE);
  g_return_val_if_fail(pv->v, FALSE);

  pv->v->measure = measure + 1;

  psppire_dict_var_changed(pv->dict, pv->v->index);
  return TRUE;
}


const struct fmt_spec *
psppire_variable_get_write_spec(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, NULL);
  g_return_val_if_fail(pv->v, NULL);


  return &pv->v->write;
}


const gchar *
psppire_variable_get_name(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, NULL);
  g_return_val_if_fail(pv->v, NULL);

  return pv->v->name;
}


gint
psppire_variable_get_columns(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return pv->v->display_width;
}



const gchar *
psppire_variable_get_label(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, NULL);
  g_return_val_if_fail(pv->v, NULL);

  return pv->v->label;
}


const struct missing_values *
psppire_variable_get_missing(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, NULL);
  g_return_val_if_fail(pv->v, NULL);

  return &pv->v->miss;
}


const struct val_labs *
psppire_variable_get_value_labels(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, NULL);
  g_return_val_if_fail(pv->v, NULL);

  return pv->v->val_labs;
}


gint
psppire_variable_get_alignment(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return pv->v->alignment;
}



gint
psppire_variable_get_measure(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return pv->v->measure - 1;
}

gint
psppire_variable_get_type(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return pv->v->type;
}


gint
psppire_variable_get_width(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return pv->v->width;
}

gint
psppire_variable_get_index(const struct PsppireVariable *pv)
{
  g_return_val_if_fail(pv, -1);
  g_return_val_if_fail(pv->v, -1);

  return pv->v->fv;
}

