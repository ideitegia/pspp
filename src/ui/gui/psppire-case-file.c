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
#include <data/casefile.h>
#include <data/data-in.h>

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
  
  if ( cf->casefile) 
    casefile_destroy(cf->casefile);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
psppire_case_file_init (PsppireCaseFile *cf)
{
  cf->casefile = 0;
}

/**
 * psppire_case_file_new:
 * @returns: a new #PsppireCaseFile object
 * 
 * Creates a new #PsppireCaseFile. 
 */
PsppireCaseFile*
psppire_case_file_new (gint var_cnt)
{
  PsppireCaseFile *cf = g_object_new (G_TYPE_PSPPIRE_CASE_FILE, NULL);

  cf->casefile = casefile_create(var_cnt);

  return cf;
}



/* Append a case to the case file */
gboolean
psppire_case_file_append_case(PsppireCaseFile *cf, 
			      struct ccase *c)
{
  bool result ;
  gint posn ;

  g_return_val_if_fail(cf, FALSE);
  g_return_val_if_fail(cf->casefile, FALSE);

  posn = casefile_get_case_cnt(cf->casefile);

  result = casefile_append(cf->casefile, c);
  
  g_signal_emit(cf, signal[CASE_INSERTED], 0, posn);
		
  return result;
}


inline gint
psppire_case_file_get_case_count(const PsppireCaseFile *cf)
{
  g_return_val_if_fail(cf, FALSE);
  
  if ( ! cf->casefile) 
    return 0;

  return casefile_get_case_cnt(cf->casefile);
}

/* Return the IDXth value from case CASENUM.
   The return value must not be freed or written to
 */
const union value *
psppire_case_file_get_value(const PsppireCaseFile *cf, gint casenum, gint idx)
{
  const union value *v; 
  struct ccase c;
  struct casereader *reader =  casefile_get_random_reader (cf->casefile);

  casereader_seek(reader, casenum);

  casereader_read(reader, &c);

  v = case_data(&c, idx);
  casereader_destroy(reader);
  case_destroy(&c);

  return v;
}

void
psppire_case_file_clear(PsppireCaseFile *cf)
{
  casefile_destroy(cf->casefile);
  cf->casefile = 0;
  g_signal_emit(cf, signal[CASES_DELETED], 0, 0, -1);
}

/* Set the IDXth value of case C using FF and DATA */
gboolean
psppire_case_file_set_value(PsppireCaseFile *cf, gint casenum, gint idx,
			    struct data_in *d_in)
{
  struct ccase cc ;

  struct casereader *reader =  casefile_get_random_reader (cf->casefile);

  casereader_seek(reader, casenum);
  casereader_read(reader, &cc);

  /* Cast away const in flagrant abuse of the casefile */
  d_in->v = (union value *) case_data(&cc, idx);

  if ( ! data_in(d_in) ) 
    g_warning("Cant set value\n");

  case_destroy(&cc);
  casereader_destroy(reader);

  g_signal_emit(cf, signal[CASE_CHANGED], 0, casenum);

  return TRUE;
}
