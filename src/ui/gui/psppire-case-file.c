/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2006  Free Software Foundation

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

#include "psppire-case-file.h"

#include <gtksheet/gtkextra-marshal.h>

#include <data/case.h>
#include <data/data-in.h>
#include <data/datasheet.h>
#include <math/sort.h>
#include <libpspp/misc.h>

#include "xalloc.h"
#include "xallocsa.h"

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


static void
psppire_case_file_class_init (PsppireCaseFileClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = psppire_case_file_finalize;

  signals [CASE_CHANGED] =
    g_signal_new ("case_changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  signals [CASE_INSERTED] =
    g_signal_new ("case_inserted",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  signals [CASES_DELETED] =
    g_signal_new ("cases_deleted",
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

  datasheet_destroy (cf->datasheet);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
psppire_case_file_init (PsppireCaseFile *cf)
{
  cf->datasheet = NULL;
}


/**
 * psppire_case_file_new:
 * @returns: a new #PsppireCaseFile object
 *
 * Creates a new #PsppireCaseFile.
 */
PsppireCaseFile*
psppire_case_file_new (void)
{
  PsppireCaseFile *cf = g_object_new (G_TYPE_PSPPIRE_CASE_FILE, NULL);

  cf->datasheet = datasheet_create (NULL);

  return cf;
}


void
psppire_case_file_replace_datasheet (PsppireCaseFile *cf, struct datasheet *ds)
{
  cf->datasheet = ds;
}



gboolean
psppire_case_file_delete_cases (PsppireCaseFile *cf, gint n_cases, gint first)
{
  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->datasheet, FALSE);

  datasheet_delete_rows (cf->datasheet, first, n_cases);

  g_signal_emit (cf, signals [CASES_DELETED], 0, n_cases, first);

  return TRUE;
}

/* Insert case CC into the case file before POSN */
gboolean
psppire_case_file_insert_case (PsppireCaseFile *cf,
			      struct ccase *cc,
			      gint posn)
{
  struct ccase tmp;
  bool result ;

  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->datasheet, FALSE);

  case_clone (&tmp, cc);
  result = datasheet_insert_rows (cf->datasheet, posn, &tmp, 1);

  if ( result )
    g_signal_emit (cf, signals [CASE_INSERTED], 0, posn);
  else
    g_warning ("Cannot insert case at position %d\n", posn);

  return result;
}


/* Append a case to the case file */
gboolean
psppire_case_file_append_case (PsppireCaseFile *cf,
			      struct ccase *c)
{
  struct ccase tmp;
  bool result ;
  gint posn ;

  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->datasheet, FALSE);

  posn = datasheet_get_row_cnt (cf->datasheet);

  case_clone (&tmp, c);
  result = datasheet_insert_rows (cf->datasheet, posn, &tmp, 1);

  g_signal_emit (cf, signals [CASE_INSERTED], 0, posn);

  return result;
}


inline gint
psppire_case_file_get_case_count (const PsppireCaseFile *cf)
{
  g_return_val_if_fail (cf, FALSE);

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
psppire_case_file_set_value (PsppireCaseFile *cf, gint casenum, gint idx,
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
psppire_case_file_data_in (PsppireCaseFile *cf, gint casenum, gint idx,
                          struct substring input, const struct fmt_spec *fmt)
{
  union value *value;
  int width;
  bool ok;

  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->datasheet, FALSE);

  g_return_val_if_fail (idx < datasheet_get_column_cnt (cf->datasheet), FALSE);

  width = fmt_var_width (fmt);
  value = xallocsa (value_cnt_from_width (width) * sizeof *value);
  ok = (datasheet_get_value (cf->datasheet, casenum, idx, value, width)
        && data_in (input, fmt->type, 0, 0, value, width)
        && datasheet_put_value (cf->datasheet, casenum, idx, value, width));

  if (ok)
    g_signal_emit (cf, signals [CASE_CHANGED], 0, casenum);

  freesa (value);

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
   one of them. */
gboolean
psppire_case_file_insert_values (PsppireCaseFile *cf,
				 gint n_values, gint before)
{
  union value *values;
  g_return_val_if_fail (cf, FALSE);

  if ( ! cf->datasheet )
    cf->datasheet = datasheet_create (NULL);

  values = xcalloc (n_values, sizeof *values);
  datasheet_insert_columns (cf->datasheet, values, n_values, before);
  free (values);

  return TRUE;
}

/* Fills C with the CASENUMth case.
   Returns true on success, false otherwise.
 */
gboolean
psppire_case_file_get_case (const PsppireCaseFile *cf, gint casenum,
			   struct ccase *c)
{
  g_return_val_if_fail (cf, FALSE);
  g_return_val_if_fail (cf->datasheet, FALSE);

  return datasheet_get_row (cf->datasheet, casenum, c);
}
