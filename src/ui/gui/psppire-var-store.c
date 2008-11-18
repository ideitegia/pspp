/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006  Free Software Foundation

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
#include <string.h>
#include <stdlib.h>
#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid



#include <gobject/gvaluecollector.h>

#include <gtksheet/gsheetmodel.h>

#include "psppire-var-store.h"
#include <gtksheet/gsheet-row-iface.h>
#include "helper.h"

#include <data/dictionary.h>
#include <data/variable.h>
#include <data/format.h>
#include <data/missing-values.h>

#include "val-labs-dialog.h"
#include "missing-val-dialog.h"
#include <data/value-labels.h>

#include "var-display.h"

enum
  {
    PSPPIRE_VAR_STORE_TRAILING_ROWS = 1,
    PSPPIRE_VAR_STORE_FORMAT_TYPE
  };

static void         psppire_var_store_init            (PsppireVarStore      *var_store);
static void         psppire_var_store_class_init      (PsppireVarStoreClass *class);
static void         psppire_var_store_sheet_model_init (GSheetModelIface *iface);
static void         psppire_var_store_finalize        (GObject           *object);


gchar * missing_values_to_string (const struct variable *pv, GError **err);


static gchar *psppire_var_store_get_string (const GSheetModel *sheet_model, glong row, glong column);

static gboolean  psppire_var_store_clear (GSheetModel *model,  glong row, glong col);


static gboolean psppire_var_store_set_string (GSheetModel *model,
					  const gchar *text, glong row, glong column);

static glong psppire_var_store_get_row_count (const GSheetModel * model);
static glong psppire_var_store_get_column_count (const GSheetModel * model);

static gchar *text_for_column (const struct variable *pv, gint c, GError **err);


static void psppire_var_store_sheet_row_init (GSheetRowIface *iface);



static GObjectClass *parent_class = NULL;

GType
psppire_var_store_format_type_get_type (void)
{
  static GType etype = 0;
  if (etype == 0)
    {
      static const GEnumValue values[] =
	{
	  { PSPPIRE_VAR_STORE_INPUT_FORMATS,
            "PSPPIRE_VAR_STORE_INPUT_FORMATS",
            "input" },
	  { PSPPIRE_VAR_STORE_OUTPUT_FORMATS,
            "PSPPIRE_VAR_STORE_OUTPUT_FORMATS",
            "output" },
	  { 0, NULL, NULL }
	};

      etype = g_enum_register_static
	(g_intern_static_string ("PsppireVarStoreFormatType"), values);

    }
  return etype;
}

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

      static const GInterfaceInfo sheet_row_info =
      {
	(GInterfaceInitFunc) psppire_var_store_sheet_row_init,
	NULL,
	NULL
      };

      var_store_type = g_type_register_static (G_TYPE_OBJECT, "PsppireVarStore", &var_store_info, 0);

      g_type_add_interface_static (var_store_type,
				   G_TYPE_SHEET_MODEL,
				   &sheet_model_info);

      g_type_add_interface_static (var_store_type,
				   G_TYPE_SHEET_ROW,
				   &sheet_row_info);


    }

  return var_store_type;
}

static void
psppire_var_store_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  PsppireVarStore *self = (PsppireVarStore *) object;

  switch (property_id)
    {
    case PSPPIRE_VAR_STORE_TRAILING_ROWS:
      self->trailing_rows = g_value_get_int (value);
      break;

    case PSPPIRE_VAR_STORE_FORMAT_TYPE:
      self->format_type = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
    }
}

static void
psppire_var_store_get_property (GObject      *object,
                        guint         property_id,
                        GValue       *value,
                        GParamSpec   *pspec)
{
  PsppireVarStore *self = (PsppireVarStore *) object;

  switch (property_id)
    {
    case PSPPIRE_VAR_STORE_TRAILING_ROWS:
      g_value_set_int (value, self->trailing_rows);
      break;

    case PSPPIRE_VAR_STORE_FORMAT_TYPE:
      g_value_set_enum (value, self->format_type);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
    }
}


static void
psppire_var_store_class_init (PsppireVarStoreClass *class)
{
  GObjectClass *object_class;
  GParamSpec *pspec;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = psppire_var_store_finalize;
  object_class->set_property = psppire_var_store_set_property;
  object_class->get_property = psppire_var_store_get_property;

  /* The minimum value for trailing-rows is 1 to prevent the
     var-store from ever having 0 rows, which breaks invariants
     in gtksheet. */
  pspec = g_param_spec_int ("trailing-rows",
                            "Trailing rows",
                            "Number of rows displayed after last variable",
                            1  /* minimum value */,
                            100 /* maximum value */,
                            40  /* default value */,
                            G_PARAM_READWRITE);
  g_object_class_install_property (object_class,
                                   PSPPIRE_VAR_STORE_TRAILING_ROWS,
                                   pspec);

  pspec = g_param_spec_enum ("format-type",
                             "Variable format type",
                             ("Whether variables have input or output "
                              "formats"),
                             G_TYPE_PSPPIRE_VAR_STORE_FORMAT_TYPE,
                             PSPPIRE_VAR_STORE_OUTPUT_FORMATS,
                             G_PARAM_READWRITE);
  g_object_class_install_property (object_class,
                                   PSPPIRE_VAR_STORE_FORMAT_TYPE,
                                   pspec);
}

static void
psppire_var_store_init (PsppireVarStore *var_store)
{
  GdkColormap *colormap = gdk_colormap_get_system ();

  g_assert (gdk_color_parse ("gray", &var_store->disabled));

  gdk_colormap_alloc_color (colormap, &var_store->disabled, FALSE, TRUE);

  var_store->dict = 0;
  var_store->trailing_rows = 40;
  var_store->format_type = PSPPIRE_VAR_STORE_OUTPUT_FORMATS;
}

static gboolean
psppire_var_store_item_editable (PsppireVarStore *var_store, glong row, glong column)
{
  const struct fmt_spec *write_spec ;

  struct variable *pv = psppire_var_store_get_var (var_store, row);

  if ( !pv )
    return TRUE;

  if ( var_is_alpha (pv) && column == PSPPIRE_VAR_STORE_COL_DECIMALS )
    return FALSE;

  write_spec = var_get_print_format (pv);

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
      if ( column == PSPPIRE_VAR_STORE_COL_DECIMALS || column == PSPPIRE_VAR_STORE_COL_WIDTH)
	return FALSE;
      break;
    default:
      break;
    }

  return TRUE;
}


struct variable *
psppire_var_store_get_var (PsppireVarStore *store, glong row)
{
  return psppire_dict_get_variable (store->dict, row);
}

static gboolean
psppire_var_store_is_editable (const GSheetModel *model, glong row, glong column)
{
  PsppireVarStore *store = PSPPIRE_VAR_STORE (model);
  return psppire_var_store_item_editable (store, row, column);
}


static const GdkColor *
psppire_var_store_get_foreground (const GSheetModel *model, glong row, glong column)
{
  PsppireVarStore *store = PSPPIRE_VAR_STORE (model);

  if ( ! psppire_var_store_item_editable (store, row, column) )
    return &store->disabled;

  return NULL;
}


const PangoFontDescription *
psppire_var_store_get_font_desc (const GSheetModel *model,
			      glong row, glong column)
{
  PsppireVarStore *store = PSPPIRE_VAR_STORE (model);

  return store->font_desc;
}




static void
psppire_var_store_sheet_model_init (GSheetModelIface *iface)
{
  iface->get_row_count = psppire_var_store_get_row_count;
  iface->get_column_count = psppire_var_store_get_column_count;
  iface->free_strings = TRUE;
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

  psppire_var_store_set_dictionary (retval, dict);

  return retval;
}

static void
var_change_callback (GtkWidget *w, gint n, gpointer data)
{
  GSheetModel *model = G_SHEET_MODEL (data);

  g_sheet_model_range_changed (model,
				 n, 0, n, PSPPIRE_VAR_STORE_n_COLS);
}


static void
var_delete_callback (GtkWidget *w, gint dict_idx, gint case_idx, gint val_cnt, gpointer data)
{
  GSheetModel *model = G_SHEET_MODEL (data);

  g_sheet_model_rows_deleted (model, dict_idx, 1);
}



static void
var_insert_callback (GtkWidget *w, glong row, gpointer data)
{
  GSheetModel *model = G_SHEET_MODEL (data);

  g_sheet_model_rows_inserted (model, row, 1);
}

static void
refresh (PsppireDict  *d, gpointer data)
{
  PsppireVarStore *vs = data;

  g_sheet_model_range_changed (G_SHEET_MODEL (vs), -1, -1, -1, -1);
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
psppire_var_store_set_dictionary (PsppireVarStore *var_store, PsppireDict *dict)
{
  if ( var_store->dict ) g_object_unref (var_store->dict);

  var_store->dict = dict;

  g_signal_connect (dict, "variable-changed", G_CALLBACK (var_change_callback),
		   var_store);

  g_signal_connect (dict, "variable-deleted", G_CALLBACK (var_delete_callback),
		   var_store);

  g_signal_connect (dict, "variable-inserted",
		    G_CALLBACK (var_insert_callback), var_store);

  g_signal_connect (dict, "backend-changed", G_CALLBACK (refresh),
		    var_store);

  /* The entire model has changed */
  g_sheet_model_range_changed (G_SHEET_MODEL (var_store), -1, -1, -1, -1);
}

static void
psppire_var_store_finalize (GObject *object)
{
  /* must chain up */
  (* parent_class->finalize) (object);
}

static gchar *
psppire_var_store_get_string (const GSheetModel *model, glong row, glong column)
{
  PsppireVarStore *store = PSPPIRE_VAR_STORE (model);

  struct variable *pv;

  if ( row >= psppire_dict_get_var_cnt (store->dict))
    return 0;

  pv = psppire_dict_get_variable (store->dict, row);

  return text_for_column (pv, column, 0);
}


/* Clears that part of the variable store, if possible, which corresponds
   to ROW, COL.
   Returns true if anything was updated, false otherwise.
*/
static gboolean
psppire_var_store_clear (GSheetModel *model,  glong row, glong col)
{
  struct variable *pv ;

  PsppireVarStore *var_store = PSPPIRE_VAR_STORE (model);

  if ( row >= psppire_dict_get_var_cnt (var_store->dict))
      return FALSE;

  pv = psppire_var_store_get_var (var_store, row);

  if ( !pv )
    return FALSE;

  switch (col)
    {
    case PSPPIRE_VAR_STORE_COL_LABEL:
      var_set_label (pv, 0);
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
psppire_var_store_set_string (GSheetModel *model,
			  const gchar *text, glong row, glong col)
{
  struct variable *pv ;

  PsppireVarStore *var_store = PSPPIRE_VAR_STORE (model);

  if ( row >= psppire_dict_get_var_cnt (var_store->dict))
      return FALSE;

  pv = psppire_var_store_get_var (var_store, row);

  if ( !pv )
    return FALSE;

  switch (col)
    {
    case PSPPIRE_VAR_STORE_COL_NAME:
      return psppire_dict_rename_var (var_store->dict, pv, text);
      break;
    case PSPPIRE_VAR_STORE_COL_COLUMNS:
      if ( ! text) return FALSE;
      var_set_display_width (pv, atoi (text));
      return TRUE;
      break;
    case PSPPIRE_VAR_STORE_COL_WIDTH:
      {
	int width = atoi (text);
	if ( ! text) return FALSE;
	if ( var_is_alpha (pv))
	    var_set_width (pv, width);
	else
	  {
            bool for_input
              = var_store->format_type == PSPPIRE_VAR_STORE_INPUT_FORMATS;
	    struct fmt_spec fmt ;
	    fmt = *var_get_write_format (pv);
	    if ( width < fmt_min_width (fmt.type, for_input)
		 ||
		 width > fmt_max_width (fmt.type, for_input))
	      return FALSE;

	    fmt.w = width;
	    fmt.d = MIN (fmt_max_decimals (fmt.type, width, for_input), fmt.d);

	    var_set_both_formats (pv, &fmt);
	  }

	return TRUE;
      }
      break;
    case PSPPIRE_VAR_STORE_COL_DECIMALS:
      {
        bool for_input
          = var_store->format_type == PSPPIRE_VAR_STORE_INPUT_FORMATS;
	int decimals;
	struct fmt_spec fmt;
	if ( ! text) return FALSE;
	decimals = atoi (text);
	fmt = *var_get_write_format (pv);
	if ( decimals >
	     fmt_max_decimals (fmt.type,
                               fmt.w,
                               for_input
                               ))
	  return FALSE;

	fmt.d = decimals;
	var_set_both_formats (pv, &fmt);
	return TRUE;
      }
      break;
    case PSPPIRE_VAR_STORE_COL_LABEL:
      var_set_label (pv, text);
      return TRUE;
      break;
    case PSPPIRE_VAR_STORE_COL_TYPE:
    case PSPPIRE_VAR_STORE_COL_VALUES:
    case PSPPIRE_VAR_STORE_COL_MISSING:
    case PSPPIRE_VAR_STORE_COL_ALIGN:
    case PSPPIRE_VAR_STORE_COL_MEASURE:
      /* These can be modified only by their respective dialog boxes */
      return FALSE;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
    }

  return TRUE;
}


const static gchar none[] = N_("None");

static  gchar *
text_for_column (const struct variable *pv, gint c, GError **err)
{
  static const gchar *const type_label[] =
    {
      N_("Numeric"),
      N_("Comma"),
      N_("Dot"),
      N_("Scientific"),
      N_("Date"),
      N_("Dollar"),
      N_("Custom"),
      N_("String")
    };
  enum {VT_NUMERIC, VT_COMMA, VT_DOT, VT_SCIENTIFIC, VT_DATE, VT_DOLLAR,
	VT_CUSTOM, VT_STRING};

  const struct fmt_spec *write_spec = var_get_write_format (pv);

  switch (c)
    {
    case PSPPIRE_VAR_STORE_COL_NAME:
      return pspp_locale_to_utf8 ( var_get_name (pv), -1, err);
      break;
    case PSPPIRE_VAR_STORE_COL_TYPE:
      {
	switch ( write_spec->type )
	  {
	  case FMT_F:
	    return g_locale_to_utf8 (gettext (type_label[VT_NUMERIC]), -1, 0, 0, err);
	    break;
	  case FMT_COMMA:
	    return g_locale_to_utf8 (gettext (type_label[VT_COMMA]), -1, 0, 0, err);
	    break;
	  case FMT_DOT:
	    return g_locale_to_utf8 (gettext (type_label[VT_DOT]), -1, 0, 0, err);
	    break;
	  case FMT_E:
	    return g_locale_to_utf8 (gettext (type_label[VT_SCIENTIFIC]), -1, 0, 0, err);
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
	    return g_locale_to_utf8 (type_label[VT_DATE], -1, 0, 0, err);
	    break;
	  case FMT_DOLLAR:
	    return g_locale_to_utf8 (type_label[VT_DOLLAR], -1, 0, 0, err);
	    break;
	  case FMT_CCA:
	  case FMT_CCB:
	  case FMT_CCC:
	  case FMT_CCD:
	  case FMT_CCE:
	    return g_locale_to_utf8 (gettext (type_label[VT_CUSTOM]), -1, 0, 0, err);
	    break;
	  case FMT_A:
	    return g_locale_to_utf8 (gettext (type_label[VT_STRING]), -1, 0, 0, err);
	    break;
	  default:
            {
              char str[FMT_STRING_LEN_MAX + 1];
              g_warning ("Unknown format: \"%s\"\n",
                        fmt_to_string (write_spec, str));
            }
	    break;
	  }
      }
      break;
    case PSPPIRE_VAR_STORE_COL_WIDTH:
      {
	gchar *s;
	GString *gstr = g_string_sized_new (10);
	g_string_printf (gstr, _("%d"), write_spec->w);
	s = g_locale_to_utf8 (gstr->str, gstr->len, 0, 0, err);
	g_string_free (gstr, TRUE);
	return s;
      }
      break;
    case PSPPIRE_VAR_STORE_COL_DECIMALS:
      {
	gchar *s;
	GString *gstr = g_string_sized_new (10);
	g_string_printf (gstr, _("%d"), write_spec->d);
	s = g_locale_to_utf8 (gstr->str, gstr->len, 0, 0, err);
	g_string_free (gstr, TRUE);
	return s;
      }
      break;
    case PSPPIRE_VAR_STORE_COL_COLUMNS:
      {
	gchar *s;
	GString *gstr = g_string_sized_new (10);
	g_string_printf (gstr, _("%d"), var_get_display_width (pv));
	s = g_locale_to_utf8 (gstr->str, gstr->len, 0, 0, err);
	g_string_free (gstr, TRUE);
	return s;
      }
      break;
    case PSPPIRE_VAR_STORE_COL_LABEL:
      return pspp_locale_to_utf8 (var_get_label (pv), -1, err);
      break;

    case PSPPIRE_VAR_STORE_COL_MISSING:
      {
	return missing_values_to_string (pv, err);
      }
      break;
    case PSPPIRE_VAR_STORE_COL_VALUES:
      {
	if ( ! var_has_value_labels (pv))
	  return g_locale_to_utf8 (gettext (none), -1, 0, 0, err);
	else
	  {
	    gchar *ss;
	    GString *gstr = g_string_sized_new (10);
	    const struct val_labs *vls = var_get_value_labels (pv);
	    struct val_labs_iterator *ip = 0;
	    struct val_lab *vl = val_labs_first_sorted (vls, &ip);

	    g_assert (vl);

	    {
	      gchar *const vstr = value_to_text (vl->value, *write_spec);

	      g_string_printf (gstr, "{%s,\"%s\"}_", vstr, vl->label);
	      g_free (vstr);
	    }

	    val_labs_done (&ip);

	    ss = pspp_locale_to_utf8 (gstr->str, gstr->len, err);
	    g_string_free (gstr, TRUE);
	    return ss;
	  }
      }
      break;
    case PSPPIRE_VAR_STORE_COL_ALIGN:
      {
	const gint align = var_get_alignment (pv);

	g_assert (align < n_ALIGNMENTS);
	return g_locale_to_utf8 (gettext (alignments[align]), -1, 0, 0, err);
      }
      break;
    case PSPPIRE_VAR_STORE_COL_MEASURE:
      {
	return measure_to_string (pv, err);
      }
      break;
    }
  return 0;
}



/* Return the number of variables */
gint
psppire_var_store_get_var_cnt (PsppireVarStore  *store)
{
  return psppire_dict_get_var_cnt (store->dict);
}


void
psppire_var_store_set_font (PsppireVarStore *store, const PangoFontDescription *fd)
{
  g_return_if_fail (store);
  g_return_if_fail (PSPPIRE_IS_VAR_STORE (store));

  store->font_desc = fd;

  g_sheet_model_range_changed (G_SHEET_MODEL (store), -1, -1, -1, -1);
}


static glong
psppire_var_store_get_row_count (const GSheetModel * model)
{
  gint rows = 0;
  PsppireVarStore *vs = PSPPIRE_VAR_STORE (model);

  if (vs->dict)
    rows =  psppire_dict_get_var_cnt (vs->dict);

  return rows ;
}

static glong
psppire_var_store_get_column_count (const GSheetModel * model)
{
  return PSPPIRE_VAR_STORE_n_COLS ;
}


/* Row related funcs */

static glong
geometry_get_row_count (const GSheetRow *geom)
{
  gint rows = 0;
  PsppireVarStore *vs = PSPPIRE_VAR_STORE (geom);

  if (vs->dict)
    rows =  psppire_dict_get_var_cnt (vs->dict);

  return rows + vs->trailing_rows;
}


static gint
geometry_get_height (const GSheetRow *geom, glong row)
{
  return 25;
}


static gboolean
geometry_is_sensitive (const GSheetRow *geom, glong row)
{
  PsppireVarStore *vs = PSPPIRE_VAR_STORE (geom);

  if ( ! vs->dict)
    return FALSE;

  return  row < psppire_dict_get_var_cnt (vs->dict);
}

static
gboolean always_true ()
{
  return TRUE;
}


static gchar *
geometry_get_button_label (const GSheetRow *geom, glong unit)
{
  gchar *label = g_strdup_printf (_("%ld"), unit + 1);

  return label;
}

static void
psppire_var_store_sheet_row_init (GSheetRowIface *iface)
{
  iface->get_row_count =     geometry_get_row_count;
  iface->get_height =        geometry_get_height;
  iface->set_height =        NULL;
  iface->get_sensitivity =   geometry_is_sensitive;

  iface->get_button_label = geometry_get_button_label;
}



