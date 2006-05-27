/* psppire-data-store.c
 
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
#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid



#include <gtksheet/gtksheet.h>
#include <gtksheet/gsheetmodel.h>
#include <gtksheet/gsheet-column-iface.h>

#include "psppire-variable.h"
#include "psppire-data-store.h"
#include "helper.h"

#include <data/dictionary.h>
#include <data/missing-values.h>
#include <data/value-labels.h>
#include <data/data-in.h>

#include <data/file-handle-def.h>
#include <data/sys-file-writer.h>



static void psppire_data_store_init            (PsppireDataStore      *data_store);
static void psppire_data_store_class_init      (PsppireDataStoreClass *class);
static void psppire_data_store_sheet_model_init (GSheetModelIface *iface);
static void psppire_data_store_sheet_column_init (GSheetColumnIface *iface);
static void psppire_data_store_finalize        (GObject           *object);

static const gchar *psppire_data_store_get_string(GSheetModel *sheet_model, gint row, gint column);

static gboolean psppire_data_store_set_string(GSheetModel *model, 
					  const gchar *text, gint row, gint column);

static gboolean psppire_data_store_clear_datum(GSheetModel *model, 
					  gint row, gint column);


#define MIN_COLUMNS 10


static GObjectClass *parent_class = NULL;

inline GType
psppire_data_store_get_type (void)
{
  static GType data_store_type = 0;

  if (!data_store_type)
    {
      static const GTypeInfo data_store_info =
      {
	sizeof (PsppireDataStoreClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
        (GClassInitFunc) psppire_data_store_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
        sizeof (PsppireDataStore),
	0,
        (GInstanceInitFunc) psppire_data_store_init,
      };

      static const GInterfaceInfo sheet_model_info =
      {
	(GInterfaceInitFunc) psppire_data_store_sheet_model_init,
	NULL,
	NULL
      };

      static const GInterfaceInfo sheet_column_info =
      {
	(GInterfaceInitFunc) psppire_data_store_sheet_column_init,
	NULL,
	NULL
      };



      data_store_type = g_type_register_static (G_TYPE_OBJECT, "PsppireDataStore",
						&data_store_info, 0);

      g_type_add_interface_static (data_store_type,
				   G_TYPE_SHEET_MODEL,
				   &sheet_model_info);

      g_type_add_interface_static (data_store_type,
				   G_TYPE_SHEET_COLUMN,
				   &sheet_column_info);

    }

  return data_store_type;
}

static void
psppire_data_store_class_init (PsppireDataStoreClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = psppire_data_store_finalize;
}



static gint
psppire_data_store_get_var_count (const GSheetModel *model)
{
  const PsppireDataStore *store = PSPPIRE_DATA_STORE(model);
  
  return psppire_dict_get_var_cnt(store->dict);
}

static gint
psppire_data_store_get_case_count (const GSheetModel *model)
{
  const PsppireDataStore *store = PSPPIRE_DATA_STORE(model);

  return psppire_case_array_get_n_cases(store->cases);
}


static void
psppire_data_store_init (PsppireDataStore *data_store)
{
  data_store->dict = 0;
  data_store->cases = 0;
}

const PangoFontDescription *
psppire_data_store_get_font_desc(GSheetModel *model,
			      gint row, gint column)
{
  PsppireDataStore *store = PSPPIRE_DATA_STORE(model);
  
  return store->font_desc;
}


static void
psppire_data_store_sheet_model_init (GSheetModelIface *iface)
{
  iface->free_strings = TRUE;
  iface->get_string = psppire_data_store_get_string;
  iface->set_string = psppire_data_store_set_string;
  iface->clear_datum = psppire_data_store_clear_datum;
  iface->is_editable = NULL;
  iface->is_visible = NULL;
  iface->get_foreground = NULL;
  iface->get_background = NULL;
  iface->get_font_desc = psppire_data_store_get_font_desc;
  iface->get_cell_border = NULL;
  iface->get_column_count = psppire_data_store_get_var_count;
  iface->get_row_count = psppire_data_store_get_case_count;
}

static
gboolean always_true()
{
  return TRUE;
}


static void
delete_cases_callback(GtkWidget *w, gint first, gint n_cases, gpointer data)
{
  PsppireDataStore *store  ;

  g_return_if_fail (data);

  store  = PSPPIRE_DATA_STORE(data);

  g_assert(first >= 0);

  g_sheet_model_rows_deleted (G_SHEET_MODEL(store), first, n_cases);
}


static void
insert_case_callback(GtkWidget *w, gint casenum, gpointer data)
{
  PsppireDataStore *store  ;

  g_return_if_fail (data);

  store  = PSPPIRE_DATA_STORE(data);
  
  g_sheet_model_range_changed (G_SHEET_MODEL(store),
			       casenum, -1,
			       psppire_case_array_get_n_cases(store->cases),
			       -1);
}


static void
changed_case_callback(GtkWidget *w, gint casenum, gpointer data)
{
  PsppireDataStore *store  ;
  g_return_if_fail (data);

  store  = PSPPIRE_DATA_STORE(data);
  
  g_sheet_model_range_changed (G_SHEET_MODEL(store),
				 casenum, -1,
				 casenum, -1);

}


static void
delete_variables_callback(GObject *obj, gint var_num, gint n_vars, gpointer data)
{
  PsppireDataStore *store ;

  g_return_if_fail (data);

  store  = PSPPIRE_DATA_STORE(data);

  g_sheet_column_columns_deleted(G_SHEET_COLUMN(store),
				   var_num, n_vars);

  g_sheet_model_columns_deleted (G_SHEET_MODEL(store), var_num, n_vars);
}


static void
insert_variable_callback(GObject *obj, gint var_num, gpointer data)
{
  PsppireDataStore *store;

  g_return_if_fail (data);

  store  = PSPPIRE_DATA_STORE(data);
  
  /* 
  g_sheet_model_range_changed (G_SHEET_MODEL(store),
				 casenum, -1,
				 psppire_case_array_get_n_cases(store->cases),
				 -1);
  */

  psppire_case_array_resize(store->cases, 
			 dict_get_next_value_idx (store->dict->dict));

  g_sheet_model_columns_inserted (G_SHEET_MODEL(store), var_num, 1);
}




/**
 * psppire_data_store_new:
 * @dict: The dictionary for this data_store.
 *
 *
 * Return value: a new #PsppireDataStore
 **/
PsppireDataStore *
psppire_data_store_new (PsppireDict *dict, PsppireCaseArray *cases)
{
  PsppireDataStore *retval;

  retval = g_object_new (GTK_TYPE_DATA_STORE, NULL);

  retval->cases = cases;
  g_signal_connect(cases, "cases-deleted", G_CALLBACK(delete_cases_callback), 
		   retval);

  g_signal_connect(cases, "case-inserted", G_CALLBACK(insert_case_callback), 
		   retval);


  g_signal_connect(cases, "case-changed", G_CALLBACK(changed_case_callback), 
		   retval);

  psppire_data_store_set_dictionary(retval, dict);


  return retval;
}



/**
 * psppire_data_store_replace_set_dictionary:
 * @data_store: The variable store
 * @dict: The dictionary to set
 *
 * If a dictionary is already associated with the data-store, then it will be
 * destroyed.
 **/
void
psppire_data_store_set_dictionary(PsppireDataStore *data_store, PsppireDict *dict)
{
#if 0
  if ( data_store->dict ) g_object_unref(data_store->dict);
#endif

  data_store->dict = dict;

  psppire_case_array_resize(data_store->cases, 
			 dict_get_next_value_idx (data_store->dict->dict));


  g_signal_connect(dict, "variable-inserted", 
		   G_CALLBACK(insert_variable_callback), 
		   data_store);

  g_signal_connect(dict, "variables-deleted", 
		   G_CALLBACK(delete_variables_callback), 
		   data_store);

  /* The entire model has changed */
  g_sheet_model_range_changed (G_SHEET_MODEL(data_store), -1, -1, -1, -1);
}

static void
psppire_data_store_finalize (GObject *object)
{

  /* must chain up */
  (* parent_class->finalize) (object);
}


static const gchar *
psppire_data_store_get_string(GSheetModel *model, gint row, gint column)
{
  const char *text;
  const struct fmt_spec *fp ;
  const struct PsppireVariable *pv ;
  const union value *v ;
  GString *s;
  PsppireDataStore *store = PSPPIRE_DATA_STORE(model);

  g_return_val_if_fail(store->dict, NULL);
  g_return_val_if_fail(store->cases, NULL);

  if (column >= psppire_dict_get_var_cnt(store->dict))
    return NULL;

  if ( row >= psppire_case_array_get_n_cases(store->cases))
    return NULL;


  pv = psppire_dict_get_variable(store->dict, column);

  v =  psppire_case_array_get_value(store->cases, row, 
			      psppire_variable_get_index(pv));

  if ( store->show_labels) 
    {
      const struct val_labs * vl = psppire_variable_get_value_labels(pv);

      const gchar *label;
      if ( (label = val_labs_find(vl, *v)) )
	{
	  return pspp_locale_to_utf8(label, -1, 0);
	}
    }

  fp = psppire_variable_get_write_spec(pv);

  s = g_string_sized_new (fp->w + 1);
  g_string_set_size(s, fp->w);
  
  memset(s->str, 0, fp->w);

  g_assert(fp->w == s->len);
    
  /* Converts binary value V into printable form in the exactly
     FP->W character in buffer S according to format specification
     FP.  No null terminator is appended to the buffer.  */
  data_out (s->str, fp, v);

  
  text = pspp_locale_to_utf8(s->str, fp->w, 0);
  g_string_free(s, TRUE);

  return text;
}


static gboolean
set_null_string_value(union value *val, gpointer data)
{
  strcpy(val->s, "");
  return TRUE;
}

static gboolean
set_sysmis_value(union value *val, gpointer data)
{
  val->f = SYSMIS;
  return TRUE;
}


static gboolean 
psppire_data_store_clear_datum(GSheetModel *model, 
					  gint row, gint col)

{
  PsppireDataStore *store = PSPPIRE_DATA_STORE(model);

  const struct PsppireVariable *pv = psppire_dict_get_variable(store->dict, col);

  const gint index = psppire_variable_get_index(pv) ;

  if ( psppire_variable_get_type(pv) == NUMERIC) 
    psppire_case_array_set_value(store->cases, row, index, set_sysmis_value,0);
  else
    psppire_case_array_set_value(store->cases, row, index, set_null_string_value,0);
  return TRUE;
}


static gboolean
fillit(union value *val, gpointer data)
{
  struct data_in *d_in = data;

  d_in->v = val;

  if ( ! data_in(d_in) ) 
    {
      g_warning("Cant encode string\n");
      return FALSE;
    }

  return TRUE;
}


/* Attempts to update that part of the variable store which corresponds 
   to ROW, COL with  the value TEXT.
   Returns true if anything was updated, false otherwise.
*/
static gboolean 
psppire_data_store_set_string(GSheetModel *model, 
			  const gchar *text, gint row, gint col)
{
  gint r;
  PsppireDataStore *store = PSPPIRE_DATA_STORE(model);

  const struct PsppireVariable *pv = psppire_dict_get_variable(store->dict, col);
  g_return_val_if_fail(pv, FALSE);

  for(r = psppire_case_array_get_n_cases(store->cases) ; r <= row ; ++r ) 
    {
      gint c;
      psppire_case_array_insert_case(store->cases, r, 0, 0);

      for (c = 0 ; c < psppire_dict_get_var_cnt(store->dict); ++c ) 
	psppire_data_store_clear_datum(model, r, c);
    }

  {
    const gint index = psppire_variable_get_index(pv);

    struct data_in d_in;
    d_in.s = text;
    d_in.e = text + strlen(text);
    d_in.v = 0;
    d_in.f1 = d_in.f2 = 0;
    d_in.format = * psppire_variable_get_write_spec(pv);
    d_in.flags = 0;

    psppire_case_array_set_value(store->cases, row, index, fillit, &d_in);
  }

  return TRUE;
}


void
psppire_data_store_set_font(PsppireDataStore *store, PangoFontDescription *fd)
{
  g_return_if_fail (store);
  g_return_if_fail (PSPPIRE_IS_DATA_STORE (store));

  store->font_desc = fd;
  g_sheet_model_range_changed (G_SHEET_MODEL(store),
				 -1, -1, -1, -1);
}


void
psppire_data_store_show_labels(PsppireDataStore *store, gboolean show_labels)
{
  g_return_if_fail (store);
  g_return_if_fail (PSPPIRE_IS_DATA_STORE (store));

  store->show_labels = show_labels;

  g_sheet_model_range_changed (G_SHEET_MODEL(store),
				 -1, -1, -1, -1);
}



static gboolean 
write_case(const struct ccase *cc, 
	   gpointer aux)
{
  struct sfm_writer *writer = aux;

  if ( ! sfm_write_case(writer, cc) )
    return FALSE;


  return TRUE;
}

void
psppire_data_store_create_system_file(PsppireDataStore *store,
			      struct file_handle *handle)
{
  const struct sfm_write_options wo = {
    true, /* writeable */
    false, /* dont compress */
    3 /* version */
  }; 

  struct sfm_writer *writer ;

  g_assert(handle);

  writer = sfm_open_writer(handle, store->dict->dict, wo);

  if ( ! writer) 
    return;

  psppire_case_array_iterate_case(store->cases, write_case, writer);

  sfm_close_writer(writer);
}



/* Column related funcs */

static gint
geometry_get_column_count(const GSheetColumn *geom)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE(geom);

  return MAX(MIN_COLUMNS, psppire_dict_get_var_cnt(ds->dict));
}

/* Return the width that an  'M' character would occupy when typeset at
   row, col */
static guint 
M_width(GtkSheet *sheet, gint row, gint col)
{
  GtkSheetCellAttr attributes;
  PangoRectangle rect;
  /* FIXME: make this a member of the data store */
  static PangoLayout *layout = 0;

  gtk_sheet_get_attributes(sheet, row, col, &attributes);

  if (! layout ) 
    layout = gtk_widget_create_pango_layout (GTK_WIDGET(sheet), "M");

  g_assert(layout);
  
  pango_layout_set_font_description (layout, 
				     attributes.font_desc);

  pango_layout_get_extents (layout, NULL, &rect);

#if 0
  g_object_unref(G_OBJECT(layout));
#endif

  return PANGO_PIXELS(rect.width);
}


/* Return the number of pixels corresponding to a column of 
   WIDTH characters */
static inline guint 
columnWidthToPixels(GtkSheet *sheet, gint column, guint width)
{
  return (M_width(sheet, 0, column) * width);
}


static gint
geometry_get_width(const GSheetColumn *geom, gint unit, GtkSheet *sheet)
{
  const struct PsppireVariable *pv ;
  PsppireDataStore *ds = PSPPIRE_DATA_STORE(geom);

  if ( unit >= psppire_dict_get_var_cnt(ds->dict) )
    return 75;

  /* FIXME: We can optimise this by caching the widths until they're resized */
  pv = psppire_dict_get_variable(ds->dict, unit);

  return columnWidthToPixels(sheet, unit, psppire_variable_get_columns(pv));
}




static void
geometry_set_width(GSheetColumn *geom, gint unit, gint width, GtkSheet *sheet)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE(geom);

  struct PsppireVariable *pv = psppire_dict_get_variable(ds->dict, unit);

  psppire_variable_set_columns(pv, width / M_width(sheet, 1, unit));
}



static GtkJustification
geometry_get_justification(const GSheetColumn *geom, gint unit)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE(geom);
  const struct PsppireVariable *pv ;


  if ( unit >= psppire_dict_get_var_cnt(ds->dict) )
    return GTK_JUSTIFY_LEFT;

  pv = psppire_dict_get_variable(ds->dict, unit);

  /* Kludge: Happily GtkJustification is defined similarly
     to enum alignment from pspp/variable.h */
  return psppire_variable_get_alignment(pv);
}


static const gchar null_var_name[]=N_("var");
 
static const gchar *
geometry_get_button_label(const GSheetColumn *geom, gint unit)
{
  const gchar *text;
  struct PsppireVariable *pv ;
  PsppireDataStore *ds = PSPPIRE_DATA_STORE(geom);

  if ( unit >= psppire_dict_get_var_cnt(ds->dict) )
    return pspp_locale_to_utf8(null_var_name, -1, 0);

  pv = psppire_dict_get_variable(ds->dict, unit);

  text =  pspp_locale_to_utf8(psppire_variable_get_name(pv), -1, 0);

  return text;
}


static gboolean
geometry_get_sensitivity(const GSheetColumn *geom, gint unit)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE(geom);


  return (unit < psppire_dict_get_var_cnt(ds->dict));
}


static void
psppire_data_store_sheet_column_init (GSheetColumnIface *iface)
{
  iface->get_column_count = geometry_get_column_count;
  iface->get_width = geometry_get_width;
  iface->set_width = geometry_set_width;
  iface->get_visibility = always_true;
  iface->get_sensitivity = geometry_get_sensitivity;
  iface->get_justification = geometry_get_justification;

  iface->get_button_label = geometry_get_button_label;
}
