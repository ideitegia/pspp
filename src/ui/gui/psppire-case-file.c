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

#include "psppire-case-file.h"

#include <gtksheet/gtkextra-marshal.h>

#include <data/case.h>
#include <data/data-in.h>
#include <data/datasheet.h>
#include <data/casereader.h>
#include <math/sort.h>
#include <libpspp/misc.h>

#include "xalloc.h"
#include "xmalloca.h"

/* --- prototypes --- */
static void psppire_case_file_class_init	(PsppireCaseFileClass	*class);
static void psppire_case_file_init	(PsppireCaseFile	*case_file);
static void psppire_case_file_finalize	(GObject		*object);


/* --- variables --- */
static GObjectClass     *parent_class = NULL;

enum  {CASE_CHANGED,
       CASE_INSERTED,
       CASES_DELETED,
       n_SIGNALS};

static guint signals [n_SIGNALS];


/* --- functions --- */
/**
 * psppire_case_file_get_type:
 * @returns: the type ID for accelerator groups.
 */
GType
psppire_case_file_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info = {
	sizeof (PsppireCaseFileClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) psppire_case_file_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (PsppireCaseFile),
	0,      /* n_preallocs */
	(GInstanceInitFunc) psppire_case_file_init,
      };

      object_type = g_type_register_static (G_TYPE_OBJECT, "PsppireCaseFile",
					    &object_info, 0);
    }

  return object_type;
}

/* Properties */
enum
{
  PROP_0,
  PROP_DATASHEET,
  PROP_READER
};




static void
psppire_case_file_set_property (GObject         *object,
				guint            prop_id,
				const GValue    *value,
				GParamSpec      *pspec)

{
  PsppireCaseFile *cf = PSPPIRE_CASE_FILE (object);

  switch (prop_id)
    {
    case PROP_READER:
      cf->datasheet = datasheet_create (g_value_get_pointer (value));
      cf->accessible = TRUE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
psppire_case_file_get_property (GObject         *object,
				guint            prop_id,
				GValue          *value,
				GParamSpec      *pspec)
{
  PsppireCaseFile *cf = PSPPIRE_CASE_FILE (object);

  switch (prop_id)
    {
    case PROP_DATASHEET:
      g_value_set_pointer (value, cf->datasheet);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_case_file_class_init (PsppireCaseFileClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GParamSpec *datasheet_spec ;
  GParamSpec *reader_spec ;

  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = psppire_case_file_finalize;

  datasheet_spec =
    g_param_spec_pointer ("datasheet",
			  "Datasheet",
			  "A pointer to the datasheet belonging to this object",
			  G_PARAM_READABLE );
  reader_spec =
    g_param_spec_pointer ("casereader",
			  "CaseReader",
			  "A pointer to the case reader from which this object is constructed",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE );

  object_class->set_property = psppire_case_file_set_property;
  object_class->get_property = psppire_case_file_get_property;

  g_object_class_install_property (object_class,
                                   PROP_DATASHEET,
                                   datasheet_spec);

  g_object_class_install_property (object_class,
                                   PROP_READER,
                                   reader_spec);

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


  signals [CASES_DELETED] =
    g_signal_new ("cases-deleted",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  gtkextra_VOID__INT_INT,
		  G_TYPE_NONE,
		  2,
		  G_TYPE_INT, G_TYPE_INT);
}

static void
psppire_case_file_finalize (GObject *object)
{
  PsppireCaseFile *cf = PSPPIRE_CASE_FILE (object);

  if ( cf->accessible)
    datasheet_destroy (cf->datasheet);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
psppire_case_file_init (PsppireCaseFile *cf)
{
  cf->datasheet = NULL;
  cf->accessible = FALSE;
}


/**
 * psppire_case_file_new:
 * @returns: a new #PsppireCaseFile object
 *
 * Creates a new #PsppireCaseFile.
 */
PsppireCaseFile*
psppire_case_file_new (struct casereader *reader)
{
  return g_object_new (G_TYPE_PSPPIRE_CASE_FILE,
		       "casereader", reader,
		       NULL);
}


gboolean
psppire_case_file_delete_cases (PsppireCaseFile *cf, casenumber n_cases, casenumber first)
{
  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->datasheet, FALSE);
  g_return_val_if_fail (cf->accessible, FALSE);

  g_return_val_if_fail (first + n_cases <=
			psppire_case_file_get_case_count (cf), FALSE);

  datasheet_delete_rows (cf->datasheet, first, n_cases);

  g_signal_emit (cf, signals [CASES_DELETED], 0, first, n_cases);

  return TRUE;
}

/* Insert case CC into the case file before POSN */
gboolean
psppire_case_file_insert_case (PsppireCaseFile *cf,
			       struct ccase *cc,
			       casenumber posn)
{
  struct ccase tmp;
  bool result ;

  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->datasheet, FALSE);
  g_return_val_if_fail (cf->accessible, FALSE);

  case_clone (&tmp, cc);
  result = datasheet_insert_rows (cf->datasheet, posn, &tmp, 1);

  if ( result )
    g_signal_emit (cf, signals [CASE_INSERTED], 0, posn);
  else
    g_warning ("Cannot insert case at position %ld\n", posn);

  return result;
}


inline casenumber
psppire_case_file_get_case_count (const PsppireCaseFile *cf)
{
  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->accessible, FALSE);

  if ( ! cf->datasheet)
    return 0;

  return datasheet_get_row_cnt (cf->datasheet);
}

/* Copies the IDXth value from case CASENUM into VALUE.
   If VALUE is null, then memory is allocated is allocated with
   malloc.  Returns the value if successful, NULL on failure. */
union value *
psppire_case_file_get_value (const PsppireCaseFile *cf,
                             casenumber casenum, size_t idx,
                             union value *value, int width)
{
  bool allocated;

  g_return_val_if_fail (cf, false);
  g_return_val_if_fail (cf->datasheet, false);

  g_return_val_if_fail (idx < datasheet_get_column_cnt (cf->datasheet), false);

  if (value == NULL)
    {
      value = xnmalloc (value_cnt_from_width (width), sizeof *value);
      allocated = true;
    }
  else
    allocated = false;
  if (!datasheet_get_value (cf->datasheet, casenum, idx, value, width))
    {
      if (allocated)
        free (value);
      value = NULL;
    }
  return value;
}

void
psppire_case_file_clear (PsppireCaseFile *cf)
{
  datasheet_destroy (cf->datasheet);
  cf->datasheet = NULL;
  g_signal_emit (cf, signals [CASES_DELETED], 0, 0, -1);
}

/* Set the IDXth value of case C to V.
   Returns true if successful, false on I/O error. */
gboolean
psppire_case_file_set_value (PsppireCaseFile *cf, casenumber casenum, gint idx,
			    union value *v, gint width)
{
  bool ok;

  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->datasheet, FALSE);

  g_return_val_if_fail (idx < datasheet_get_column_cnt (cf->datasheet), FALSE);

  ok = datasheet_put_value (cf->datasheet, casenum, idx, v, width);
  if (ok)
    g_signal_emit (cf, signals [CASE_CHANGED], 0, casenum);
  return ok;
}



/* Set the IDXth value of case C using D_IN */
gboolean
psppire_case_file_data_in (PsppireCaseFile *cf, casenumber casenum, gint idx,
                          struct substring input, const struct fmt_spec *fmt)
{
  union value *value = NULL;
  int width;
  bool ok;

  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->datasheet, FALSE);

  g_return_val_if_fail (idx < datasheet_get_column_cnt (cf->datasheet), FALSE);

  width = fmt_var_width (fmt);
  value = xmalloca (value_cnt_from_width (width) * sizeof *value);
  ok = (datasheet_get_value (cf->datasheet, casenum, idx, value, width)
        && data_in (input, LEGACY_NATIVE, fmt->type, 0, 0, value, width)
        && datasheet_put_value (cf->datasheet, casenum, idx, value, width));

  if (ok)
    g_signal_emit (cf, signals [CASE_CHANGED], 0, casenum);

  freea (value);

  return TRUE;
}


void
psppire_case_file_sort (PsppireCaseFile *cf, struct case_ordering *ordering)
{
  struct casereader *sorted_data;
  gint c;

  sorted_data = sort_execute (datasheet_make_reader (cf->datasheet), ordering);
  cf->datasheet = datasheet_create (sorted_data);

  /* FIXME: Need to have a signal to change a range of cases, instead of
     calling a signal many times */
  for ( c = 0 ; c < datasheet_get_row_cnt (cf->datasheet) ; ++c )
    g_signal_emit (cf, signals [CASE_CHANGED], 0, c);
}


/* Resize the cases in the casefile, by inserting N_VALUES into every
   one of them at the position immediately preceeding WHERE.
*/
gboolean
psppire_case_file_insert_values (PsppireCaseFile *cf,
				 gint n_values, gint where)
{
  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->accessible, FALSE);

  if ( n_values == 0 )
    return FALSE;

  g_assert (n_values > 0);

  if ( ! cf->datasheet )
    cf->datasheet = datasheet_create (NULL);

  {
    union value *values = xcalloc (n_values, sizeof *values);
    datasheet_insert_columns (cf->datasheet, values, n_values, where);
    free (values);
  }

  return TRUE;
}


/* Fills C with the CASENUMth case.
   Returns true on success, false otherwise.
 */
gboolean
psppire_case_file_get_case (const PsppireCaseFile *cf, casenumber casenum,
			   struct ccase *c)
{
  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->datasheet, FALSE);

  return datasheet_get_row (cf->datasheet, casenum, c);
}



struct casereader *
psppire_case_file_make_reader (PsppireCaseFile *cf)
{
  struct casereader *r = datasheet_make_reader (cf->datasheet);
  cf->accessible = FALSE;
  return r;
}

