/* 
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


#include <string.h>
#include <stdlib.h>

#include "psppire-object.h"
#include "psppire-case-file.h"

#include <gtksheet/gtkextra-marshal.h>

#include <data/case.h>
#include <ui/flexifile.h>
#include <data/casefile.h>
#include <data/data-in.h>
#include <math/sort.h>
#include <libpspp/misc.h>

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

static guint signal[n_SIGNALS];


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

      object_type = g_type_register_static (G_TYPE_PSPPIRE_OBJECT, "PsppireCaseFile",
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
		  G_TYPE_INT, G_TYPE_INT);
}

static void
psppire_case_file_finalize (GObject *object)
{
  PsppireCaseFile *cf = PSPPIRE_CASE_FILE (object);
  
  if ( cf->flexifile) 
    casefile_destroy(cf->flexifile);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
psppire_case_file_init (PsppireCaseFile *cf)
{
  cf->flexifile = 0;
}

/**
 * psppire_case_file_new:
 * @returns: a new #PsppireCaseFile object
 * 
 * Creates a new #PsppireCaseFile. 
 */
PsppireCaseFile*
psppire_case_file_new (gint val_cnt)
{
  PsppireCaseFile *cf = g_object_new (G_TYPE_PSPPIRE_CASE_FILE, NULL);

  cf->flexifile = flexifile_create(val_cnt);

  return cf;
}


gboolean
psppire_case_file_delete_cases(PsppireCaseFile *cf, gint n_cases, gint first)
{
  int result;

  g_return_val_if_fail(cf, FALSE);
  g_return_val_if_fail(cf->flexifile, FALSE);

  result =  flexifile_delete_cases(FLEXIFILE(cf->flexifile), n_cases,  first);

  g_signal_emit(cf, signal[CASES_DELETED], 0, n_cases, first);

  return result;
}

/* Insert a blank case to the case file */
gboolean
psppire_case_file_insert_case(PsppireCaseFile *cf, 
			       gint posn)
{
  bool result ;
  struct ccase cc;

  g_return_val_if_fail(cf, FALSE);
  g_return_val_if_fail(cf->flexifile, FALSE);

  case_create (&cc, casefile_get_value_cnt(cf->flexifile));

  result = flexifile_insert_case(FLEXIFILE(cf->flexifile), &cc, posn);
  
  case_destroy (&cc);

  if ( result ) 
    g_signal_emit(cf, signal[CASE_INSERTED], 0, posn);
  else
    g_warning("Cannot insert case at position %d\n", posn);
		
  return result;
}

/* Append a case to the case file */
gboolean
psppire_case_file_append_case(PsppireCaseFile *cf, 
			      struct ccase *c)
{
  bool result ;
  gint posn ;

  g_return_val_if_fail(cf, FALSE);
  g_return_val_if_fail(cf->flexifile, FALSE);

  posn = casefile_get_case_cnt(cf->flexifile);

  result = casefile_append(cf->flexifile, c);
  
  g_signal_emit(cf, signal[CASE_INSERTED], 0, posn);
		
  return result;
}


inline gint
psppire_case_file_get_case_count(const PsppireCaseFile *cf)
{
  g_return_val_if_fail(cf, FALSE);
  
  if ( ! cf->flexifile) 
    return 0;

  return casefile_get_case_cnt(cf->flexifile);
}

/* Return the IDXth value from case CASENUM.
   The return value must not be freed or written to
 */
const union value *
psppire_case_file_get_value(const PsppireCaseFile *cf, gint casenum, gint idx)
{
  const union value *v; 
  struct ccase c;

  g_return_val_if_fail(cf, NULL);
  g_return_val_if_fail(cf->flexifile, NULL);

  g_return_val_if_fail(idx < casefile_get_value_cnt(cf->flexifile), NULL);

  flexifile_get_case(FLEXIFILE(cf->flexifile), casenum, &c);

  v = case_data(&c, idx);

  case_destroy(&c);

  return v;
}

void
psppire_case_file_clear(PsppireCaseFile *cf)
{
  casefile_destroy(cf->flexifile);
  cf->flexifile = 0;
  g_signal_emit(cf, signal[CASES_DELETED], 0, 0, -1);
}

/* Set the IDXth value of case C to SYSMIS/EMPTY */
gboolean
psppire_case_file_set_value(PsppireCaseFile *cf, gint casenum, gint idx,
			    union value *v, gint width)
{
  struct ccase cc ;
  int bytes;

  g_return_val_if_fail(cf, FALSE);
  g_return_val_if_fail(cf->flexifile, FALSE);

  g_return_val_if_fail(idx < casefile_get_value_cnt(cf->flexifile), FALSE);

  if ( ! flexifile_get_case(FLEXIFILE(cf->flexifile), casenum, &cc) )
    return FALSE;

  if ( width == 0 ) 
    bytes = MAX_SHORT_STRING;
  else
    bytes = DIV_RND_UP(width, MAX_SHORT_STRING) * MAX_SHORT_STRING ;

  /* Cast away const in flagrant abuse of the casefile */
  memcpy((union value *)case_data(&cc, idx), v, bytes);

  g_signal_emit(cf, signal[CASE_CHANGED], 0, casenum);

  return TRUE;
}



/* Set the IDXth value of case C using D_IN */
gboolean
psppire_case_file_data_in(PsppireCaseFile *cf, gint casenum, gint idx,
			    struct data_in *d_in)
{
  struct ccase cc ;

  g_return_val_if_fail(cf, FALSE);
  g_return_val_if_fail(cf->flexifile, FALSE);

  g_return_val_if_fail(idx < casefile_get_value_cnt(cf->flexifile), FALSE);

  if ( ! flexifile_get_case(FLEXIFILE(cf->flexifile), casenum, &cc) )
    return FALSE;

  /* Cast away const in flagrant abuse of the casefile */
  d_in->v = (union value *) case_data(&cc, idx);

  if ( ! data_in(d_in) ) 
    g_warning("Cant set value\n");

  g_signal_emit(cf, signal[CASE_CHANGED], 0, casenum);

  return TRUE;
}


void
psppire_case_file_sort(PsppireCaseFile *cf, const struct sort_criteria *sc)
{
  struct ccase cc;
  gint c;
  struct casefile *cfile;
  struct casereader *reader = casefile_get_reader(cf->flexifile);
  const int value_cnt = casefile_get_value_cnt(cf->flexifile);

  cfile = sort_execute(reader, sc);

  casefile_destroy(cf->flexifile);

  /* Copy casefile into flexifile */

  reader = casefile_get_destructive_reader(cfile);
  cf->flexifile = flexifile_create(value_cnt);
  while(casereader_read(reader, &cc))
      casefile_append(cf->flexifile, &cc);
  

  /* FIXME: Need to have a signal to change a range of cases, instead of
     calling a signal many times */
  for ( c = 0 ; c < casefile_get_case_cnt(cf->flexifile) ; ++c ) 
    g_signal_emit(cf, signal[CASE_CHANGED], 0, c);
}


/* Resize the cases in the casefile, by inserting N_VALUES into every 
   one of them. */
gboolean 
psppire_case_file_insert_values(PsppireCaseFile *cf, 
				gint n_values, gint before)
{
  g_return_val_if_fail(cf, FALSE);

  if ( ! cf->flexifile ) 
    {
      cf->flexifile = flexifile_create(n_values);
      return TRUE;
    }

  return flexifile_resize(FLEXIFILE(cf->flexifile), n_values, before);
}

