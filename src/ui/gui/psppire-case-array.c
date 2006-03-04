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

#include "psppire-object.h"
#include "psppire-case-array.h"
#include "gtkextra-marshal.h"

#include "case.h"

/* --- prototypes --- */
static void psppire_case_array_class_init	(PsppireCaseArrayClass	*class);
static void psppire_case_array_init	(PsppireCaseArray	*case_array);
static void psppire_case_array_finalize	(GObject		*object);


/* --- variables --- */
static GObjectClass     *parent_class = NULL;

enum  {CASE_CHANGED, 
       CASE_INSERTED,
       CASES_DELETED, 
       n_SIGNALS};

static guint signal[n_SIGNALS];


/* --- functions --- */
/**
 * psppire_case_array_get_type:
 * @returns: the type ID for accelerator groups.
 */
GType
psppire_case_array_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info = {
	sizeof (PsppireCaseArrayClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) psppire_case_array_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (PsppireCaseArray),
	0,      /* n_preallocs */
	(GInstanceInitFunc) psppire_case_array_init,
      };

      object_type = g_type_register_static (G_TYPE_PSPPIRE_OBJECT, "PsppireCaseArray",
					    &object_info, 0);
    }

  return object_type;
}


static void
psppire_case_array_class_init (PsppireCaseArrayClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = psppire_case_array_finalize;

  signal[CASE_CHANGED] =
    g_signal_new ("case_changed",
		  G_TYPE_FROM_CLASS(class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE, 
		  1,
		  G_TYPE_INT);


  signal[CASE_INSERTED] =
    g_signal_new ("case_inserted",
		  G_TYPE_FROM_CLASS(class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE, 
		  1,
		  G_TYPE_INT);


  signal[CASES_DELETED] =
    g_signal_new ("cases_deleted",
		  G_TYPE_FROM_CLASS(class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  gtkextra_VOID__INT_INT,
		  G_TYPE_NONE, 
		  2,
		  G_TYPE_INT,
		  G_TYPE_INT);
}

static void
psppire_case_array_finalize (GObject *object)
{
  PsppireCaseArray *ca = PSPPIRE_CASE_ARRAY (object);
  
  gint i;
  for (i = 0 ; i < ca->size; ++i ) 
    case_destroy(&ca->cases[i]);

  g_free (ca->cases);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
psppire_case_array_init (PsppireCaseArray *ca)
{
  ca->cases = 0;
  ca->size = 0;
}

/**
 * psppire_case_array_new:
 * @returns: a new #PsppireCaseArray object
 * 
 * Creates a new #PsppireCaseArray. 
 */
PsppireCaseArray*
psppire_case_array_new (gint capacity, gint width)
{
  PsppireCaseArray *ca = g_object_new (G_TYPE_PSPPIRE_CASE_ARRAY, NULL);

  ca->capacity = capacity;
  ca->width = width;
  
  ca->cases = g_new0(struct ccase, capacity);

  return ca;
}


void
psppire_case_array_resize(PsppireCaseArray *ca,  gint new_size)
{		       
  gint c;

  for (c = 0 ; c < ca->size ; ++c ) 
    case_resize(&ca->cases[c], ca->width, new_size);
  
  ca->width = new_size;
}

/* FIXME: add_case and insert_case need to be merged/refactored */
gboolean
psppire_case_array_add_case(PsppireCaseArray *ca, 
			 psppire_case_array_fill_case_func fill_case_func,
			 gpointer aux)
{
  g_return_val_if_fail(ca->size < ca->capacity, FALSE);

  case_create(&ca->cases[ca->size], ca->width);

  if ( !fill_case_func(&ca->cases[ca->size], aux))
    return FALSE;

  ca->size++;

  g_signal_emit(ca, signal[CASE_INSERTED], 0, ca->size - 1);  

  return TRUE;
}


gboolean
psppire_case_array_iterate_case(PsppireCaseArray *ca, 
			 psppire_case_array_use_case_func use_case_func,
			 gpointer aux)
{
  gint i;
  g_return_val_if_fail(ca->size < ca->capacity, FALSE);

  for (i = 0 ; i < ca->size ; ++i ) 
    {
      if ( !use_case_func(&ca->cases[i], aux))
	return FALSE;
    }

  return TRUE;
}


void
psppire_case_array_insert_case(PsppireCaseArray *ca, gint posn)
{
  g_return_if_fail(posn >= 0);
  g_return_if_fail(posn <= ca->size);

  g_assert(ca->size + 1 <= ca->capacity);

  gint i;

  for(i = ca->size; i > posn ; --i)
      case_move(&ca->cases[i], &ca->cases[i - 1]);

  case_create(&ca->cases[posn], ca->width);

  ca->size++;
  g_signal_emit(ca, signal[CASE_INSERTED], 0, posn);
}

void
psppire_case_array_delete_cases(PsppireCaseArray *ca, gint first, gint n_cases)
{
  g_return_if_fail(n_cases > 0);
  g_return_if_fail(first >= 0);
  g_return_if_fail(first + n_cases < ca->size);
  
  gint i;

  /* FIXME: Is this right ?? */
  for ( i = first; i < first + n_cases ; ++i ) 
    case_destroy(&ca->cases[i]);

  for ( ; i < ca->size; ++i ) 
    case_move(&ca->cases[i - n_cases], &ca->cases[i]);

  ca->size -= n_cases;
  g_signal_emit(ca, signal[CASES_DELETED], 0, first, n_cases);  
}


gint
psppire_case_array_get_n_cases(const PsppireCaseArray *ca)
{
  return ca->size;
}

/* Clears the contents of CA */
void 
psppire_case_array_clear(PsppireCaseArray *ca)
{
  gint c;
  for (c = 0 ; c < ca->size ; ++c ) 
    case_destroy(&ca->cases[c]);

  ca->size = 0;

  g_signal_emit(ca, signal[CASES_DELETED], 0, 0, c);  
}

/* Return the IDXth value from case C */
const union value *
psppire_case_array_get_value(const PsppireCaseArray *ca, gint c, gint idx)
{
  g_return_val_if_fail(c < ca->size, NULL);

  return case_data(&ca->cases[c], idx);
}


/* Set the IDXth value of case C using FF and DATA */
void
psppire_case_array_set_value(PsppireCaseArray *ca, gint c, gint idx,
			  value_fill_func_t ff,
			  gpointer data)
{
  g_return_if_fail(c < ca->size);

  struct ccase *cc = &ca->cases[c];

  union value *val = case_data_rw(cc, idx);

  gboolean changed = ff(val, data);

  case_unshare(cc);

  if ( changed ) 
    g_signal_emit(ca, signal[CASE_CHANGED], 0, c);
  
}
