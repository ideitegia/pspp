/* psppire-var-store.c
 
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

#include <config.h>
#include <string.h>
#include <stdlib.h>

#include <gobject/gvaluecollector.h>

#include <gtksheet/gsheetmodel.h>

#include "psppire-variable.h"
#include "psppire-var-store.h"
#include "var-sheet.h"

#include <data/dictionary.h>
#include <data/variable.h>
#include <data/missing-values.h>

#include "val-labs-dialog.h"
#include "missing-val-dialog.h"
#include <data/value-labels.h>

#define _(A) A
#define N_(A) A


static void         psppire_var_store_init            (PsppireVarStore      *var_store);
static void         psppire_var_store_class_init      (PsppireVarStoreClass *class);
static void         psppire_var_store_sheet_model_init (GSheetModelIface *iface);
static void         psppire_var_store_finalize        (GObject           *object);

static const gchar *const psppire_var_store_get_string(GSheetModel *sheet_model, gint row, gint column);

static gboolean  psppire_var_store_clear(GSheetModel *model,  gint row, gint col);


static gboolean psppire_var_store_set_string(GSheetModel *model, 
					  const gchar *text, gint row, gint column);


static const gchar *const text_for_column(const struct PsppireVariable *pv, gint c);


static GObjectClass *parent_class = NULL;

GType
psppire_var_store_get_type (void)
{
  static GType var_store_type = 0;

  if (!var_store_type)
    {
      static const GTypeInfo var_store_info =
      {
	sizeof (PsppireVarStoreClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
        (GClassInitFunc) psppire_var_store_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
        sizeof (PsppireVarStore),
	0,
        (GInstanceInitFunc) psppire_var_store_init,
      };

      static const GInterfaceInfo sheet_model_info =
      {
	(GInterfaceInitFunc) psppire_var_store_sheet_model_init,
	NULL,
	NULL
      };

      var_store_type = g_type_register_static (G_TYPE_OBJECT, "PsppireVarStore",
						&var_store_info, 0);

      g_type_add_interface_static (var_store_type,
				   G_TYPE_SHEET_MODEL,
				   &sheet_model_info);
    }

  return var_store_type;
}

static void
psppire_var_store_class_init (PsppireVarStoreClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = psppire_var_store_finalize;
}


static void
psppire_var_store_init (PsppireVarStore *var_store)
{
  GdkColormap *colormap = gdk_colormap_get_system();

  g_assert(gdk_color_parse("gray", &var_store->disabled));

  gdk_colormap_alloc_color (colormap, &var_store->disabled, FALSE, TRUE);

  var_store->dict = 0;
}

static gboolean
psppire_var_store_item_editable(PsppireVarStore *var_store, gint row, gint column)
{
  const struct fmt_spec *write_spec ;

  struct PsppireVariable *pv = psppire_var_store_get_variable(var_store, row);

  if ( !pv ) 
    return TRUE;

  if ( ALPHA == psppire_variable_get_type(pv) && column == COL_DECIMALS ) 
    return FALSE;

  write_spec = psppire_variable_get_write_spec(pv);

  switch ( write_spec->type ) 
    {
    case FMT_DATE:	
    case FMT_EDATE:	
    case FMT_SDATE:	
    case FMT_ADATE:	
    case FMT_JDATE:	
    case FMT_QYR:	
    case FMT_MOYR:	
    case FMT_WKYR:	
    case FMT_DATETIME:	
    case FMT_TIME:	
    case FMT_DTIME:	
    case FMT_WKDAY:	
    case FMT_MONTH:	
      if ( column == COL_DECIMALS || column == COL_WIDTH)
	return FALSE;
      break;
    default:
      break;
    }

  return TRUE;
}

static gboolean
psppire_var_store_is_editable(GSheetModel *model, gint row, gint column)
{
  PsppireVarStore *store = PSPPIRE_VAR_STORE(model);
  return psppire_var_store_item_editable(store, row, column);
}


static const GdkColor *
psppire_var_store_get_foreground(GSheetModel *model, gint row, gint column)
{
  PsppireVarStore *store = PSPPIRE_VAR_STORE(model);

  if ( ! psppire_var_store_item_editable(store, row, column) ) 
    return &store->disabled;
  
  return NULL;
}


const PangoFontDescription *
psppire_var_store_get_font_desc(GSheetModel *model,
			      gint row, gint column)
{
  PsppireVarStore *store = PSPPIRE_VAR_STORE(model);
  
  return store->font_desc;
}



static void
psppire_var_store_sheet_model_init (GSheetModelIface *iface)
{
  iface->get_string = psppire_var_store_get_string;
  iface->set_string = psppire_var_store_set_string;
  iface->clear_datum = psppire_var_store_clear;
  iface->is_editable = psppire_var_store_is_editable;
  iface->is_visible = NULL;
  iface->get_foreground = psppire_var_store_get_foreground;
  iface->get_background = NULL;
  iface->get_font_desc = psppire_var_store_get_font_desc;
  iface->get_cell_border = NULL;
}



/**
 * psppire_var_store_new:
 * @dict: The dictionary for this var_store.
 *
 *
 * Return value: a new #PsppireVarStore
 **/
PsppireVarStore *
psppire_var_store_new (PsppireDict *dict)
{
  PsppireVarStore *retval;

  retval = g_object_new (GTK_TYPE_VAR_STORE, NULL);

  psppire_var_store_set_dictionary(retval, dict);

  return retval;
}

static void 
var_change_callback(GtkWidget *w, gint n, gpointer data)
{
  GSheetModel *model = G_SHEET_MODEL(data);
  g_sheet_model_range_changed (model,
				 n, 0, n, n_COLS);
}


static void 
var_delete_callback(GtkWidget *w, gint first, gint n, gpointer data)
{
  GSheetModel *model = G_SHEET_MODEL(data);
  
  g_sheet_model_rows_deleted (model, first, n);
}



static void 
var_insert_callback(GtkWidget *w, gint row, gpointer data)
{
  GSheetModel *model = G_SHEET_MODEL(data);

  g_sheet_model_rows_inserted (model, row, 1);
}



/**
 * psppire_var_store_replace_set_dictionary:
 * @var_store: The variable store
 * @dict: The dictionary to set
 *
 * If a dictionary is already associated with the var-store, then it will be
 * destroyed.
 **/
void
psppire_var_store_set_dictionary(PsppireVarStore *var_store, PsppireDict *dict)
{
  if ( var_store->dict ) g_object_unref(var_store->dict);

  var_store->dict = dict;

  g_signal_connect(dict, "variable-changed", G_CALLBACK(var_change_callback), 
		   var_store);

  g_signal_connect(dict, "variables-deleted", G_CALLBACK(var_delete_callback), 
		   var_store);

  g_signal_connect(dict, "variable-inserted", G_CALLBACK(var_insert_callback), 
		   var_store);


  /* The entire model has changed */
  g_sheet_model_range_changed (G_SHEET_MODEL(var_store), -1, -1, -1, -1);
}

static void
psppire_var_store_finalize (GObject *object)
{
  /* must chain up */
  (* parent_class->finalize) (object);
}

static const gchar *const 
psppire_var_store_get_string(GSheetModel *model, gint row, gint column)
{
  const gchar *s ;

  PsppireVarStore *store = PSPPIRE_VAR_STORE(model);

  struct PsppireVariable *pv;

  if ( row >= psppire_dict_get_var_cnt(store->dict))
    return 0;
  
  pv = psppire_dict_get_variable (store->dict, row);
  
  s = text_for_column(pv, column);

  return s;
}


struct PsppireVariable *
psppire_var_store_get_variable(PsppireVarStore *store, gint row)
{
  g_return_val_if_fail(store, NULL);
  g_return_val_if_fail(store->dict, NULL);

  if ( row >= psppire_dict_get_var_cnt(store->dict))
    return 0;

  return psppire_dict_get_variable (store->dict, row);
}

/* Clears that part of the variable store, if possible, which corresponds 
   to ROW, COL.
   Returns true if anything was updated, false otherwise.
*/
static gboolean 
psppire_var_store_clear(GSheetModel *model,  gint row, gint col)
{
  struct PsppireVariable *pv ;

  PsppireVarStore *var_store = PSPPIRE_VAR_STORE(model);

  if ( row >= psppire_dict_get_var_cnt(var_store->dict))
      return FALSE;

  pv = psppire_var_store_get_variable(var_store, row);

  if ( !pv ) 
    return FALSE;

  switch (col)
    {
    case COL_LABEL:
      psppire_variable_set_label(pv, 0);
      return TRUE;
      break;
    }

  return FALSE;
}

/* Attempts to update that part of the variable store which corresponds 
   to ROW, COL with  the value TEXT.
   Returns true if anything was updated, false otherwise.
*/
static gboolean 
psppire_var_store_set_string(GSheetModel *model, 
			  const gchar *text, gint row, gint col)
{
  struct PsppireVariable *pv ;

  PsppireVarStore *var_store = PSPPIRE_VAR_STORE(model);

  if ( row >= psppire_dict_get_var_cnt(var_store->dict))
      return FALSE;

  pv = psppire_var_store_get_variable(var_store, row);
  if ( !pv ) 
    return FALSE;

  switch (col)
    {
    case COL_NAME:
      return psppire_variable_set_name(pv, text);
      break;
    case COL_COLUMNS:
      if ( ! text) return FALSE;
      return psppire_variable_set_columns(pv, atoi(text));
      break;
    case COL_WIDTH:
      if ( ! text) return FALSE;
      return psppire_variable_set_width(pv, atoi(text));
      break;
    case COL_DECIMALS:
      if ( ! text) return FALSE;
      return psppire_variable_set_decimals(pv, atoi(text));
      break;
    case COL_LABEL:
      psppire_variable_set_label(pv, text);
      return TRUE;
      break;
    case COL_TYPE:
    case COL_VALUES:
    case COL_MISSING:
    case COL_ALIGN:
    case COL_MEASURE:
      /* These can be modified only by their respective dialog boxes */
      return FALSE;
      break;
    default:
      g_assert_not_reached();
      return FALSE;
    }

  return TRUE;
}


#define MAX_CELL_TEXT_LEN 255

static const gchar *const
text_for_column(const struct PsppireVariable *pv, gint c)
{
  static gchar buf[MAX_CELL_TEXT_LEN];

  static gchar none[]=_("None");

  static const gchar *const type_label[] = 
    {
      _("Numeric"),
      _("Comma"),
      _("Dot"),
      _("Scientific"),
      _("Date"),
      _("Dollar"),
      _("Custom"),
      _("String")
    };
  enum {VT_NUMERIC, VT_COMMA, VT_DOT, VT_SCIENTIFIC, VT_DATE, VT_DOLLAR, 
	VT_CUSTOM, VT_STRING};

  const struct fmt_spec *write_spec = psppire_variable_get_write_spec(pv);

  switch (c)
    {
    case COL_NAME:
      return psppire_variable_get_name(pv);
      break;
    case COL_TYPE:
      {
	switch ( write_spec->type ) 
	  {
	  case FMT_F:
	    return type_label[VT_NUMERIC];
	    break;
	  case FMT_COMMA:
	    return type_label[VT_COMMA];
	    break;
	  case FMT_DOT:
	    return type_label[VT_DOT];
	    break;
	  case FMT_E:
	    return type_label[VT_SCIENTIFIC];
	    break;
	  case FMT_DATE:	
	  case FMT_EDATE:	
	  case FMT_SDATE:	
	  case FMT_ADATE:	
	  case FMT_JDATE:	
	  case FMT_QYR:	
	  case FMT_MOYR:	
	  case FMT_WKYR:	
	  case FMT_DATETIME:	
	  case FMT_TIME:	
	  case FMT_DTIME:	
	  case FMT_WKDAY:	
	  case FMT_MONTH:	
	    return type_label[VT_DATE];
	    break;
	  case FMT_DOLLAR:
	    return type_label[VT_DOLLAR];
	    break;
	  case FMT_CCA:
	  case FMT_CCB:
	  case FMT_CCC:
	  case FMT_CCD:
	  case FMT_CCE:
	    return type_label[VT_CUSTOM];
	    break;
	  case FMT_A:
	    return type_label[VT_STRING];
	    break;
	  default:
	    g_warning("Unknown format: \"%s\"\n", 
		      fmt_to_string(write_spec));
	    break;
	  }
      }
      break;
    case COL_WIDTH:
      {
	g_snprintf(buf, MAX_CELL_TEXT_LEN, "%d", write_spec->w);
	return buf;
      }
      break;
    case COL_DECIMALS:
      {
	g_snprintf(buf, MAX_CELL_TEXT_LEN, "%d", write_spec->d);
	return buf;
      }
      break;
    case COL_COLUMNS:
      {
	g_snprintf(buf, MAX_CELL_TEXT_LEN, 
		   "%d", psppire_variable_get_columns(pv));
	return buf;
      }
      break;
    case COL_LABEL:
      return psppire_variable_get_label(pv);
      break;
    case COL_MISSING:
      {
      const struct missing_values *miss = psppire_variable_get_missing(pv);
      if ( mv_is_empty(miss)) 
	return none;
      else
	{
	  if ( ! mv_has_range (miss))
	    {
	      const int n = mv_n_values(miss);
	      gchar *mv[4] = {0,0,0,0};
	      gint i;
	      for(i = 0 ; i < n; ++i ) 
		{
		  union value v;
		  mv_peek_value(miss, &v, i);
		  mv[i] = value_to_text(v, *write_spec);
		}
	      g_stpcpy(buf, "");
	      for(i = 0 ; i < n; ++i ) 
		{
		  if ( i > 0) 
		    g_strlcat(buf, ", ", MAX_CELL_TEXT_LEN);
		  g_strlcat(buf, mv[i], MAX_CELL_TEXT_LEN);
		  g_free(mv[i]);
		}
	    }
	  else
	    {
	      gchar *l, *h;
	      union value low, high;
	      mv_peek_range(miss, &low.f, &high.f);
		  
	      l = value_to_text(low, *write_spec);
	      h = value_to_text(high, *write_spec);

	      g_snprintf(buf, MAX_CELL_TEXT_LEN, "%s - %s", l, h);
	      g_free(l);
	      g_free(h);

	      if ( mv_has_value(miss)) 
		{
		  gchar buf2[MAX_CELL_TEXT_LEN];
		  gchar *s = 0;
		  union value v;
		  mv_peek_value(miss, &v, 0);

		  s = value_to_text(v, *write_spec);

		  g_snprintf(buf2, MAX_CELL_TEXT_LEN, "%s, %s", buf, s);
		  free(s);
		  g_stpcpy(buf, buf2);
		}
	    }

	  return buf;
	}
      }
      break;
    case COL_VALUES:
      {
	const struct val_labs *vls = psppire_variable_get_value_labels(pv);
	if ( ! vls || 0 == val_labs_count(vls)) 
	  return none;
	else
	  {
	    struct val_labs_iterator *ip=0;
	    struct val_lab *vl = val_labs_first_sorted (vls, &ip);

	    g_assert(vl);

	    {
	      gchar *const vstr = value_to_text(vl->value, *write_spec);

	      g_snprintf(buf, MAX_CELL_TEXT_LEN, "{%s,\"%s\"}_", vstr, vl->label);
	      g_free(vstr);
	    }

	    val_labs_done(&ip);

	    return buf;
	  }
      }
      break;
    case COL_ALIGN:
      return alignments[psppire_variable_get_alignment(pv)];
      break;
    case COL_MEASURE:
      return measures[psppire_variable_get_measure(pv)];
      break;
    }
  return 0;
}



/* Return the number of variables */
gint
psppire_var_store_get_var_cnt(PsppireVarStore  *store)
{
  return psppire_dict_get_var_cnt(store->dict);
}


void
psppire_var_store_set_font(PsppireVarStore *store, PangoFontDescription *fd)
{
  g_return_if_fail (store);
  g_return_if_fail (PSPPIRE_IS_VAR_STORE (store));

  store->font_desc = fd;

  g_sheet_model_range_changed (G_SHEET_MODEL(store), -1, -1, -1, -1);
}



