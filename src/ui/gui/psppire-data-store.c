/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2008  Free Software Foundation

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

#include <data/datasheet.h>
#include <data/data-out.h>
#include <data/variable.h>

#include <gtksheet/gsheetmodel.h>
#include <gtksheet/psppire-marshal.h>

#include <pango/pango-context.h>

#include "psppire-data-store.h"
#include "helper.h"

#include <data/dictionary.h>
#include <data/missing-values.h>
#include <data/value-labels.h>
#include <data/data-in.h>
#include <data/format.h>

#include <math/sort.h>

#include "xalloc.h"
#include "xmalloca.h"



static void psppire_data_store_init            (PsppireDataStore      *data_store);
static void psppire_data_store_class_init      (PsppireDataStoreClass *class);
static void psppire_data_store_sheet_model_init (GSheetModelIface *iface);

static void psppire_data_store_finalize        (GObject           *object);
static void psppire_data_store_dispose        (GObject           *object);

static gboolean psppire_data_store_clear_datum (GSheetModel *model,
					  glong row, glong column);


static gboolean psppire_data_store_insert_case (PsppireDataStore *ds,
						struct ccase *cc,
						casenumber posn);


static gboolean psppire_data_store_data_in (PsppireDataStore *ds,
					    casenumber casenum, gint idx,
					    struct substring input,
					    const struct fmt_spec *fmt);



static GObjectClass *parent_class = NULL;


enum
  {
    BACKEND_CHANGED,
    CASES_DELETED,
    CASE_INSERTED,
    CASE_CHANGED,
    n_SIGNALS
  };

static guint signals [n_SIGNALS];


GType
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


      data_store_type = g_type_register_static (G_TYPE_OBJECT,
						"PsppireDataStore",
						&data_store_info, 0);

      g_type_add_interface_static (data_store_type,
				   G_TYPE_SHEET_MODEL,
				   &sheet_model_info);

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
  object_class->dispose = psppire_data_store_dispose;

  signals [BACKEND_CHANGED] =
    g_signal_new ("backend-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);

  signals [CASE_INSERTED] =
    g_signal_new ("case-inserted",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  signals [CASE_CHANGED] =
    g_signal_new ("case-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);

  signals [CASES_DELETED] =
    g_signal_new ("cases-deleted",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  psppire_marshal_VOID__INT_INT,
		  G_TYPE_NONE,
		  2,
		  G_TYPE_INT,
		  G_TYPE_INT);
}



static gboolean
psppire_data_store_insert_values (PsppireDataStore *ds,
				  gint n_values, gint where);

static union value *
psppire_data_store_get_value (const PsppireDataStore *ds,
			      casenumber casenum, size_t idx,
			      union value *value, int width);


static gboolean
psppire_data_store_set_value (PsppireDataStore *ds, casenumber casenum,
			      gint idx, union value *v, gint width);




static glong
psppire_data_store_get_var_count (const GSheetModel *model)
{
  const PsppireDataStore *store = PSPPIRE_DATA_STORE (model);

  return psppire_dict_get_var_cnt (store->dict);
}

casenumber
psppire_data_store_get_case_count (const PsppireDataStore *store)
{
  return datasheet_get_row_cnt (store->datasheet);
}

size_t
psppire_data_store_get_value_count (const PsppireDataStore *store)
{
  return psppire_dict_get_value_cnt (store->dict);
}

static casenumber
psppire_data_store_get_case_count_wrapper (const GSheetModel *model)
{
  const PsppireDataStore *store = PSPPIRE_DATA_STORE (model);
  return psppire_data_store_get_case_count (store);
}

static void
psppire_data_store_init (PsppireDataStore *data_store)
{
  data_store->dict = 0;
  data_store->datasheet = NULL;
  data_store->dispose_has_run = FALSE;
}

static inline gchar *
psppire_data_store_get_string_wrapper (const GSheetModel *model, glong row,
				       glong column)
{
  return psppire_data_store_get_string (PSPPIRE_DATA_STORE (model), row, column);
}


static inline gboolean
psppire_data_store_set_string_wrapper (GSheetModel *model,
				       const gchar *text,
				       glong row, glong column)
{
  return psppire_data_store_set_string (PSPPIRE_DATA_STORE (model), text,
					row, column);
}



static gchar * get_column_subtitle (const GSheetModel *model, gint col);
static gchar * get_column_button_label (const GSheetModel *model, gint col);
static gboolean get_column_sensitivity (const GSheetModel *model, gint col);
static GtkJustification get_column_justification (const GSheetModel *model, gint col);

static gchar * get_row_button_label (const GSheetModel *model, gint row);
static gboolean get_row_sensitivity (const GSheetModel *model, gint row);


static void
psppire_data_store_sheet_model_init (GSheetModelIface *iface)
{
  iface->free_strings = TRUE;
  iface->get_string = psppire_data_store_get_string_wrapper;
  iface->set_string = psppire_data_store_set_string_wrapper;
  iface->clear_datum = psppire_data_store_clear_datum;
  iface->is_editable = NULL;
  iface->get_foreground = NULL;
  iface->get_background = NULL;
  iface->get_cell_border = NULL;
  iface->get_column_count = psppire_data_store_get_var_count;
  iface->get_row_count = psppire_data_store_get_case_count_wrapper;

  iface->get_column_subtitle = get_column_subtitle;
  iface->get_column_title = get_column_button_label;
  iface->get_column_sensitivity = get_column_sensitivity;
  iface->get_column_justification = get_column_justification;

  iface->get_row_title = get_row_button_label;
  iface->get_row_sensitivity = get_row_sensitivity;
}


/*
   A callback which occurs after a variable has been deleted.
 */
static void
delete_variable_callback (GObject *obj, gint dict_index,
			  gint case_index, gint val_cnt,
			  gpointer data)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (data);

#if AXIS_TRANSITION
  g_sheet_model_columns_deleted (G_SHEET_MODEL (store), dict_index, 1);

  g_sheet_column_columns_changed (G_SHEET_COLUMN (store),
				   dict_index, -1);
#endif
}



static void
variable_changed_callback (GObject *obj, gint var_num, gpointer data)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (data);

#if AXIS_TRANSITION
  g_sheet_column_columns_changed (G_SHEET_COLUMN (store),
				  var_num, 1);


  g_sheet_model_range_changed (G_SHEET_MODEL (store),
			       -1, var_num,
			       -1, var_num);
#endif
}

static void
insert_variable_callback (GObject *obj, gint var_num, gpointer data)
{
  PsppireDataStore *store;
  gint posn;

  g_return_if_fail (data);

  store  = PSPPIRE_DATA_STORE (data);

  if ( var_num > 0 )
    {
      struct variable *variable =
	psppire_dict_get_variable (store->dict, var_num);

      g_assert (variable != NULL);

      posn = var_get_case_index (variable);
    }
  else
    {
      posn = 0;
    }

  psppire_data_store_insert_values (store, 1, posn);

#if AXIS_TRANSITION
  g_sheet_column_columns_changed (G_SHEET_COLUMN (store),
				  var_num, 1);
#endif

  g_sheet_model_columns_inserted (G_SHEET_MODEL (store), var_num, 1);
}


static void
dict_size_change_callback (GObject *obj,
			  gint posn, gint adjustment, gpointer data)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (data);

  const struct variable *v = psppire_dict_get_variable (store->dict, posn);

  const gint new_val_width = value_cnt_from_width (var_get_width (v));

  if ( adjustment > 0 )
    psppire_data_store_insert_values (store, adjustment,
				     new_val_width - adjustment +
				     var_get_case_index(v));
}



/**
 * psppire_data_store_new:
 * @dict: The dictionary for this data_store.
 *
 *
 * Return value: a new #PsppireDataStore
 **/
PsppireDataStore *
psppire_data_store_new (PsppireDict *dict)
{
  PsppireDataStore *retval;

  retval = g_object_new (GTK_TYPE_DATA_STORE, NULL);

  psppire_data_store_set_dictionary (retval, dict);

  return retval;
}

void
psppire_data_store_set_reader (PsppireDataStore *ds,
			       struct casereader *reader)
{
  gint i;

  if ( ds->datasheet)
    datasheet_destroy (ds->datasheet);

  ds->datasheet = datasheet_create (reader);

  g_sheet_model_range_changed (G_SHEET_MODEL (ds),
			       -1, -1, -1, -1);

  if ( ds->dict )
    for (i = 0 ; i < n_dict_signals; ++i )
      {
	if ( ds->dict_handler_id [i] > 0)
	  {
	    g_signal_handler_unblock (ds->dict,
				      ds->dict_handler_id[i]);
	  }
      }

  g_signal_emit (ds, signals[BACKEND_CHANGED], 0);
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
psppire_data_store_set_dictionary (PsppireDataStore *data_store, PsppireDict *dict)
{
  int i;

  /* Disconnect any existing handlers */
  if ( data_store->dict )
    for (i = 0 ; i < n_dict_signals; ++i )
      {
	g_signal_handler_disconnect (data_store->dict,
				     data_store->dict_handler_id[i]);
      }

  data_store->dict = dict;

  if ( dict != NULL)
    {

      data_store->dict_handler_id [VARIABLE_INSERTED] =
	g_signal_connect (dict, "variable-inserted",
			  G_CALLBACK (insert_variable_callback),
			  data_store);

      data_store->dict_handler_id [VARIABLE_DELETED] =
	g_signal_connect (dict, "variable-deleted",
			  G_CALLBACK (delete_variable_callback),
			  data_store);

      data_store->dict_handler_id [VARIABLE_CHANGED] =
	g_signal_connect (dict, "variable-changed",
			  G_CALLBACK (variable_changed_callback),
			  data_store);

      data_store->dict_handler_id [SIZE_CHANGED] =
	g_signal_connect (dict, "dict-size-changed",
			  G_CALLBACK (dict_size_change_callback),
			  data_store);
    }



  /* The entire model has changed */
  g_sheet_model_range_changed (G_SHEET_MODEL (data_store), -1, -1, -1, -1);

#if AXIS_TRANSITION
  g_sheet_column_columns_changed (G_SHEET_COLUMN (data_store), 0, -1);
#endif

  if ( data_store->dict )
    for (i = 0 ; i < n_dict_signals; ++i )
      {
	if ( data_store->dict_handler_id [i] > 0)
	  {
	    g_signal_handler_block (data_store->dict,
				    data_store->dict_handler_id[i]);
	  }
      }
}

static void
psppire_data_store_finalize (GObject *object)
{

  /* must chain up */
  (* parent_class->finalize) (object);
}


static void
psppire_data_store_dispose (GObject *object)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (object);

  if (ds->dispose_has_run)
    return;

  if (ds->datasheet)
    {
      datasheet_destroy (ds->datasheet);
      ds->datasheet = NULL;
    }

  /* must chain up */
  (* parent_class->dispose) (object);

  ds->dispose_has_run = TRUE;
}



/* Insert a blank case before POSN */
gboolean
psppire_data_store_insert_new_case (PsppireDataStore *ds, casenumber posn)
{
  gboolean result;
  gint val_cnt, v;
  struct ccase cc;
  g_return_val_if_fail (ds, FALSE);

  val_cnt = datasheet_get_column_cnt (ds->datasheet) ;

  g_return_val_if_fail (val_cnt > 0, FALSE);

  g_return_val_if_fail (posn <= psppire_data_store_get_case_count (ds), FALSE);

  case_create (&cc, val_cnt);

  memset ( case_data_rw_idx (&cc, 0), 0, val_cnt * MAX_SHORT_STRING);

  for (v = 0 ; v < psppire_dict_get_var_cnt (ds->dict) ; ++v)
    {
      const struct variable *pv = psppire_dict_get_variable (ds->dict, v);
      if ( var_is_alpha (pv))
	continue;

      case_data_rw (&cc, pv)->f = SYSMIS;
    }

  result = psppire_data_store_insert_case (ds, &cc, posn);

  case_destroy (&cc);

  return result;
}


gchar *
psppire_data_store_get_string (PsppireDataStore *store, glong row, glong column)
{
  gint idx;
  char *text;
  const struct fmt_spec *fp ;
  const struct variable *pv ;
  union value *v ;
  GString *s;

  g_return_val_if_fail (store->dict, NULL);
  g_return_val_if_fail (store->datasheet, NULL);

  if (column >= psppire_dict_get_var_cnt (store->dict))
    return NULL;

  if ( row >= psppire_data_store_get_case_count (store))
    return NULL;

  pv = psppire_dict_get_variable (store->dict, column);

  g_assert (pv);

  idx = var_get_case_index (pv);

  g_assert (idx >= 0);

  v = psppire_data_store_get_value (store, row, idx, NULL,
                                   var_get_width (pv));

  g_return_val_if_fail (v, NULL);

  if ( store->show_labels)
    {
      const gchar *label = var_lookup_value_label (pv, v);
      if (label)
        {
          free (v);
	  return pspp_locale_to_utf8 (label, -1, 0);
        }
    }

  fp = var_get_write_format (pv);

  s = g_string_sized_new (fp->w + 1);
  g_string_set_size (s, fp->w);

  memset (s->str, 0, fp->w);

  g_assert (fp->w == s->len);

  /* Converts binary value V into printable form in the exactly
     FP->W character in buffer S according to format specification
     FP.  No null terminator is appended to the buffer.  */
  data_out (v, fp, s->str);

  text = pspp_locale_to_utf8 (s->str, fp->w, 0);
  g_string_free (s, TRUE);

  g_strchomp (text);

  free (v);
  return text;
}


static gboolean
psppire_data_store_clear_datum (GSheetModel *model,
					  glong row, glong col)
{
  PsppireDataStore *store = PSPPIRE_DATA_STORE (model);

  union value v;
  const struct variable *pv = psppire_dict_get_variable (store->dict, col);

  const gint index = var_get_case_index (pv) ;

  if ( var_is_numeric (pv))
    v.f = SYSMIS;
  else
    memcpy (v.s, "", MAX_SHORT_STRING);

  psppire_data_store_set_value (store, row, index, &v,
				var_get_width (pv));

  return TRUE;
}


/* Attempts to update that part of the variable store which corresponds
   to ROW, COL with  the value TEXT.
   Returns true if anything was updated, false otherwise.
*/
gboolean
psppire_data_store_set_string (PsppireDataStore *store,
			       const gchar *text, glong row, glong col)
{
  glong n_cases;
  const struct variable *pv = psppire_dict_get_variable (store->dict, col);
  g_return_val_if_fail (pv, FALSE);

  n_cases = psppire_data_store_get_case_count (store);

  if ( row > n_cases)
    return FALSE;

  if (row == n_cases)
    psppire_data_store_insert_new_case (store, row);

  psppire_data_store_data_in (store, row,
			      var_get_case_index (pv), ss_cstr (text),
			      var_get_write_format (pv));

  return TRUE;
}



void
psppire_data_store_show_labels (PsppireDataStore *store, gboolean show_labels)
{
  g_return_if_fail (store);
  g_return_if_fail (PSPPIRE_IS_DATA_STORE (store));

  store->show_labels = show_labels;

  g_sheet_model_range_changed (G_SHEET_MODEL (store),
				 -1, -1, -1, -1);
}


void
psppire_data_store_clear (PsppireDataStore *ds)
{
  datasheet_destroy (ds->datasheet);
  ds->datasheet = NULL;

  psppire_dict_clear (ds->dict);

  g_signal_emit (ds, signals [CASES_DELETED], 0, 0, -1);
}



/* Return a casereader made from this datastore */
struct casereader *
psppire_data_store_get_reader (PsppireDataStore *ds)
{
  int i;
  struct casereader *reader ;

  if ( ds->dict )
    for (i = 0 ; i < n_dict_signals; ++i )
      {
	g_signal_handler_block (ds->dict,
				ds->dict_handler_id[i]);
      }

  reader = datasheet_make_reader (ds->datasheet);

  /* We must not reference this again */
  ds->datasheet = NULL;

  return reader;
}



/* Column related funcs */


static const gchar null_var_name[]=N_("var");



/* Row related funcs */

static gchar *
get_row_button_label (const GSheetModel *model, gint unit)
{
  gchar *s = g_strdup_printf (_("%d"), unit + FIRST_CASE_NUMBER);

  gchar *text =  pspp_locale_to_utf8 (s, -1, 0);

  g_free (s);

  return text;
}


static gboolean
get_row_sensitivity (const GSheetModel *model, gint unit)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);

  return (unit < psppire_data_store_get_case_count (ds));
}




/* Column related stuff */

static gchar *
get_column_subtitle (const GSheetModel *model, gint col)
{
  gchar *text;
  const struct variable *v ;
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);

  if ( col >= psppire_dict_get_var_cnt (ds->dict) )
    return NULL;

  v = psppire_dict_get_variable (ds->dict, col);

  if ( ! var_has_label (v))
    return NULL;

  text =  pspp_locale_to_utf8 (var_get_label (v), -1, 0);

  return text;
}

static gchar *
get_column_button_label (const GSheetModel *model, gint col)
{
  gchar *text;
  struct variable *pv ;
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);

  if ( col >= psppire_dict_get_var_cnt (ds->dict) )
    return g_locale_to_utf8 (null_var_name, -1, 0, 0, 0);

  pv = psppire_dict_get_variable (ds->dict, col);

  text =  pspp_locale_to_utf8 (var_get_name (pv), -1, 0);

  return text;
}

static gboolean
get_column_sensitivity (const GSheetModel *model, gint col)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);

  return (col < psppire_dict_get_var_cnt (ds->dict));
}



static GtkJustification
get_column_justification (const GSheetModel *model, gint col)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (model);
  const struct variable *pv ;

  if ( col >= psppire_dict_get_var_cnt (ds->dict) )
    return GTK_JUSTIFY_LEFT;

  pv = psppire_dict_get_variable (ds->dict, col);

  return (var_get_alignment (pv) == ALIGN_LEFT ? GTK_JUSTIFY_LEFT
          : var_get_alignment (pv) == ALIGN_RIGHT ? GTK_JUSTIFY_RIGHT
          : GTK_JUSTIFY_CENTER);
}






/* Fills C with the CASENUMth case.
   Returns true on success, false otherwise.
 */
gboolean
psppire_data_store_get_case (const PsppireDataStore *ds,
			     casenumber casenum,
			     struct ccase *c)
{
  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  return datasheet_get_row (ds->datasheet, casenum, c);
}


gboolean
psppire_data_store_delete_cases (PsppireDataStore *ds, casenumber first,
				 casenumber n_cases)
{
  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  g_return_val_if_fail (first + n_cases <=
			psppire_data_store_get_case_count (ds), FALSE);


  datasheet_delete_rows (ds->datasheet, first, n_cases);

  g_signal_emit (ds, signals [CASES_DELETED], 0, first, n_cases);

  return TRUE;
}



/* Insert case CC into the case file before POSN */
static gboolean
psppire_data_store_insert_case (PsppireDataStore *ds,
				struct ccase *cc,
				casenumber posn)
{
  struct ccase tmp;
  bool result ;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  case_clone (&tmp, cc);
  result = datasheet_insert_rows (ds->datasheet, posn, &tmp, 1);

  if ( result )
    g_signal_emit (ds, signals [CASE_INSERTED], 0, posn);
  else
    g_warning ("Cannot insert case at position %ld\n", posn);

  return result;
}


/* Copies the IDXth value from case CASENUM into VALUE.
   If VALUE is null, then memory is allocated is allocated with
   malloc.  Returns the value if successful, NULL on failure. */
static union value *
psppire_data_store_get_value (const PsppireDataStore *ds,
			      casenumber casenum, size_t idx,
			      union value *value, int width)
{
  bool allocated;

  g_return_val_if_fail (ds, false);
  g_return_val_if_fail (ds->datasheet, false);

  g_return_val_if_fail (idx < datasheet_get_column_cnt (ds->datasheet), false);

  if (value == NULL)
    {
      value = xnmalloc (value_cnt_from_width (width), sizeof *value);
      allocated = true;
    }
  else
    allocated = false;
  if (!datasheet_get_value (ds->datasheet, casenum, idx, value, width))
    {
      if (allocated)
        free (value);
      value = NULL;
    }
  return value;
}



/* Set the IDXth value of case C to V.
   Returns true if successful, false on I/O error. */
static gboolean
psppire_data_store_set_value (PsppireDataStore *ds, casenumber casenum,
			      gint idx, union value *v, gint width)
{
  bool ok;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  g_return_val_if_fail (idx < datasheet_get_column_cnt (ds->datasheet), FALSE);

  ok = datasheet_put_value (ds->datasheet, casenum, idx, v, width);
  if (ok)
    g_signal_emit (ds, signals [CASE_CHANGED], 0, casenum);
  return ok;
}




/* Set the IDXth value of case C using D_IN */
static gboolean
psppire_data_store_data_in (PsppireDataStore *ds, casenumber casenum, gint idx,
			    struct substring input, const struct fmt_spec *fmt)
{
  union value *value = NULL;
  int width;
  bool ok;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  g_return_val_if_fail (idx < datasheet_get_column_cnt (ds->datasheet), FALSE);

  width = fmt_var_width (fmt);
  value = xmalloca (value_cnt_from_width (width) * sizeof *value);
  ok = (datasheet_get_value (ds->datasheet, casenum, idx, value, width)
        && data_in (input, LEGACY_NATIVE, fmt->type, 0, 0, 0, value, width)
        && datasheet_put_value (ds->datasheet, casenum, idx, value, width));

  if (ok)
    g_signal_emit (ds, signals [CASE_CHANGED], 0, casenum);

  freea (value);

  return TRUE;
}

/* Resize the cases in the casefile, by inserting N_VALUES into every
   one of them at the position immediately preceeding WHERE.
*/
static gboolean
psppire_data_store_insert_values (PsppireDataStore *ds,
				  gint n_values, gint where)
{
  g_return_val_if_fail (ds, FALSE);

  if ( n_values == 0 )
    return FALSE;

  g_assert (n_values > 0);

  if ( ! ds->datasheet )
    ds->datasheet = datasheet_create (NULL);

  {
    union value *values = xcalloc (n_values, sizeof *values);
    datasheet_insert_columns (ds->datasheet, values, n_values, where);
    free (values);
  }

  return TRUE;
}
