/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2008, 2009, 2010, 2011, 2012, 2013  Free Software Foundation

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

#include <ui/gui/psppire-marshal.h>

#include <pango/pango-context.h>

#include "psppire-data-store.h"
#include <libpspp/i18n.h>
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

static void psppire_data_store_finalize        (GObject           *object);
static void psppire_data_store_dispose        (GObject           *object);

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

      data_store_type = g_type_register_static (G_TYPE_OBJECT,
						"PsppireDataStore",
						&data_store_info, 0);
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
psppire_data_store_insert_value (PsppireDataStore *ds,
				  gint width, gint where);

casenumber
psppire_data_store_get_case_count (const PsppireDataStore *store)
{
  return datasheet_get_n_rows (store->datasheet);
}

size_t
psppire_data_store_get_value_count (const PsppireDataStore *store)
{
  return psppire_dict_get_value_cnt (store->dict);
}

const struct caseproto *
psppire_data_store_get_proto (const PsppireDataStore *store)
{
  return psppire_dict_get_proto (store->dict);
}

static void
psppire_data_store_init (PsppireDataStore *data_store)
{
  data_store->dict = NULL;
  data_store->datasheet = NULL;
  data_store->dispose_has_run = FALSE;
}

/*
   A callback which occurs after a variable has been deleted.
 */
static void
delete_variable_callback (GObject *obj, const struct variable *var UNUSED,
                          gint dict_index, gint case_index,
                          gpointer data)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (data);

  g_return_if_fail (store->datasheet);

  datasheet_delete_columns (store->datasheet, case_index, 1);
  datasheet_insert_column (store->datasheet, NULL, -1, case_index);
}

struct resize_datum_aux
  {
    const struct dictionary *dict;
    const struct variable *new_variable;
    const struct variable *old_variable;
  };

static void
resize_datum (const union value *old, union value *new, const void *aux_)
{
  const struct resize_datum_aux *aux = aux_;
  int new_width = var_get_width (aux->new_variable);
  const char *enc = dict_get_encoding (aux->dict);
  const struct fmt_spec *newfmt = var_get_print_format (aux->new_variable);
  char *s = data_out (old, enc, var_get_print_format (aux->old_variable));
  enum fmt_type type = (fmt_usable_for_input (newfmt->type)
                        ? newfmt->type
                        : FMT_DOLLAR);
  free (data_in (ss_cstr (s), enc, type, new, new_width, enc));
  free (s);
}

static void
variable_changed_callback (GObject *obj, gint var_num, guint what, const struct variable *oldvar,
			   gpointer data)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (data);
  struct variable *variable = psppire_dict_get_variable (store->dict, var_num);

  if (what & VAR_TRAIT_WIDTH)
    {
      int posn = var_get_case_index (variable);
      struct resize_datum_aux aux;
      aux.old_variable = oldvar;
      aux.new_variable = variable;
      aux.dict = store->dict->dict;
      datasheet_resize_column (store->datasheet, posn, var_get_width (variable),
                               resize_datum, &aux);
    }
}

static void
insert_variable_callback (GObject *obj, gint var_num, gpointer data)
{
  struct variable *variable;
  PsppireDataStore *store;
  gint posn;

  g_return_if_fail (data);

  store  = PSPPIRE_DATA_STORE (data);

  variable = psppire_dict_get_variable (store->dict, var_num);
  posn = var_get_case_index (variable);
  psppire_data_store_insert_value (store, var_get_width (variable), posn);
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

  retval = g_object_new (PSPPIRE_TYPE_DATA_STORE, NULL);

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
    }



  /* The entire model has changed */

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
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (object);

  if (ds->datasheet)
    {
      datasheet_destroy (ds->datasheet);
      ds->datasheet = NULL;
    }

  /* must chain up */
  (* parent_class->finalize) (object);
}


static void
psppire_data_store_dispose (GObject *object)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (object);

  if (ds->dispose_has_run)
    return;

  psppire_data_store_set_dictionary (ds, NULL);

  /* must chain up */
  (* parent_class->dispose) (object);

  ds->dispose_has_run = TRUE;
}



/* Insert a blank case before POSN */
gboolean
psppire_data_store_insert_new_case (PsppireDataStore *ds, casenumber posn)
{
  gboolean result;
  const struct caseproto *proto;
  struct ccase *cc;
  g_return_val_if_fail (ds, FALSE);

  proto = datasheet_get_proto (ds->datasheet);
  g_return_val_if_fail (caseproto_get_n_widths (proto) > 0, FALSE);
  g_return_val_if_fail (posn <= psppire_data_store_get_case_count (ds), FALSE);

  cc = case_create (proto);
  case_set_missing (cc);

  result = psppire_data_store_insert_case (ds, cc, posn);

  case_unref (cc);

  return result;
}

gchar *
psppire_data_store_get_string (PsppireDataStore *store,
                               glong row, const struct variable *var,
                               bool use_value_label)
{
  gchar *string;
  union value v;
  int width;

  g_return_val_if_fail (store != NULL, NULL);
  g_return_val_if_fail (store->datasheet != NULL, NULL);
  g_return_val_if_fail (var != NULL, NULL);

  if (row < 0 || row >= datasheet_get_n_rows (store->datasheet))
    return NULL;

  width = var_get_width (var);
  value_init (&v, width);
  datasheet_get_value (store->datasheet, row, var_get_case_index (var), &v);

  string = NULL;
  if (use_value_label)
    {
      const char *label = var_lookup_value_label (var, &v);
      if (label != NULL)
        string = g_strdup (label);
    }
  if (string == NULL)
    string = value_to_text (v, var);

  value_destroy (&v, width);

  return string;
}


/* Attempts to update that part of the variable store which corresponds to VAR
   within ROW with the value TEXT.

   If USE_VALUE_LABEL is true, and TEXT is a value label for the column's
   variable, then stores the value from that value label instead of the literal
   TEXT.

   Returns true if anything was updated, false otherwise.  */
gboolean
psppire_data_store_set_string (PsppireDataStore *store,
			       const gchar *text,
                               glong row, const struct variable *var,
                               gboolean use_value_label)
{
  gint case_index;
  glong n_cases;
  gboolean ok;

  n_cases = psppire_data_store_get_case_count (store);
  if (row > n_cases)
    return FALSE;
  if (row == n_cases)
    psppire_data_store_insert_new_case (store, row);

  case_index = var_get_case_index (var);
  if (use_value_label)
    {
      const struct val_labs *vls = var_get_value_labels (var);
      const union value *value = vls ? val_labs_find_value (vls, text) : NULL;
      if (value)
        ok = datasheet_put_value (store->datasheet, row, case_index, value);
      else
        ok = FALSE;
    }
  else
    ok = psppire_data_store_data_in (store, row, case_index, ss_cstr (text),
                                     var_get_print_format (var));

  if (ok)
    g_signal_emit (store, signals [CASE_CHANGED], 0, row);
  return ok;
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





/* Returns the CASENUMth case, or a null pointer on failure.
 */
struct ccase *
psppire_data_store_get_case (const PsppireDataStore *ds,
			     casenumber casenum)
{
  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  return datasheet_get_row (ds->datasheet, casenum);
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
  bool result ;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  cc = case_ref (cc);
  result = datasheet_insert_rows (ds->datasheet, posn, &cc, 1);

  if ( result )
    g_signal_emit (ds, signals [CASE_INSERTED], 0, posn);
  else
    g_warning ("Cannot insert case at position %ld\n", posn);

  return result;
}


/* Set the value of VAR in case CASENUM to V.
   V must be the correct width for IDX.
   Returns true if successful, false on I/O error. */
gboolean
psppire_data_store_set_value (PsppireDataStore *ds, casenumber casenum,
			      const struct variable *var, const union value *v)
{
  glong n_cases;
  bool ok;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  n_cases = psppire_data_store_get_case_count (ds);
  if ( casenum > n_cases)
    return FALSE;

  if (casenum == n_cases)
    psppire_data_store_insert_new_case (ds, casenum);

  ok = datasheet_put_value (ds->datasheet, casenum, var_get_case_index (var),
                            v);
  if (ok)
    g_signal_emit (ds, signals [CASE_CHANGED], 0, casenum);

  return ok;
}




/* Set the IDXth value of case C using D_IN */
static gboolean
psppire_data_store_data_in (PsppireDataStore *ds, casenumber casenum, gint idx,
			    struct substring input, const struct fmt_spec *fmt)
{
  union value value;
  int width;
  bool ok;

  PsppireDict *dict;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  g_return_val_if_fail (idx < datasheet_get_n_columns (ds->datasheet), FALSE);

  dict = ds->dict;

  width = fmt_var_width (fmt);
  g_return_val_if_fail (caseproto_get_width (
                          datasheet_get_proto (ds->datasheet), idx) == width,
                        FALSE);
  value_init (&value, width);
  ok = (datasheet_get_value (ds->datasheet, casenum, idx, &value)
        && data_in_msg (input, UTF8, fmt->type, &value, width,
                        dict_get_encoding (dict->dict))
        && datasheet_put_value (ds->datasheet, casenum, idx, &value));
  value_destroy (&value, width);

  return ok;
}

/* Resize the cases in the casefile, by inserting a value of the
   given WIDTH into every one of them at the position immediately
   preceding WHERE.
*/
static gboolean
psppire_data_store_insert_value (PsppireDataStore *ds,
                                 gint width, gint where)
{
  union value value;

  g_return_val_if_fail (ds, FALSE);

  g_assert (width >= 0);

  if ( ! ds->datasheet )
    ds->datasheet = datasheet_create (NULL);

  value_init (&value, width);
  value_set_missing (&value, width);

  datasheet_insert_column (ds->datasheet, &value, width, where);
  value_destroy (&value, width);

  return TRUE;
}

gboolean
psppire_data_store_filtered (PsppireDataStore *ds,
                             glong row)
{
  union value val;

  const struct dictionary *dict;
  const struct variable *filter;

  if ( row < 0 || row >= datasheet_get_n_rows (ds->datasheet))
    return FALSE;

  dict = ds->dict->dict;
  g_return_val_if_fail (dict, FALSE);
  filter = dict_get_filter (dict);
  if ( ! filter)
    return FALSE;

  g_return_val_if_fail (var_is_numeric (filter), FALSE);
  value_init (&val, 0);
  if ( ! datasheet_get_value (ds->datasheet, row,
                              var_get_case_index (filter),
                              &val) )
    return FALSE;

  return (val.f == 0.0);
}
