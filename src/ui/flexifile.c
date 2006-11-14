/* PSPP - computes sample statistics.

   Copyright (C) 2006 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include <xalloc.h>
#include <assert.h>
#include "flexifile.h"
#include <data/casefile.h>
#include <data/casefile-private.h>
#include <data/case.h>
#include <libpspp/compiler.h>


struct class_flexifile
{
  struct class_casefile parent;

  bool (*get_case) (const struct flexifile *, unsigned long, struct ccase *);

  bool (*insert_case) (struct flexifile *, struct ccase *, int );
  bool (*delete_cases) (struct flexifile *, int, int );

  bool (*resize) (struct flexifile *, int, int );
};

static const struct class_flexifile class;

#define CLASS_FLEXIFILE(K)  ((struct class_flexifile *) K)
#define CONST_CLASS_FLEXIFILE(K) ((const struct class_flexifile *) K)


/* A flexifile. */
struct flexifile
{
  struct casefile cf;		/* Parent */

  size_t value_cnt;		/* Case size in `union value's. */
  unsigned long case_cnt;	/* Number of cases stored. */


  /* Memory storage. */
  struct ccase *cases;		/* Pointer to array of cases. */
  unsigned long capacity;       /* size of array in cases */
};

struct class_flexifilereader 
{
  struct class_casereader parent ;
};

static const struct class_flexifilereader class_reader;

/* For reading out the cases in a flexifile. */
struct flexifilereader
{
  struct casereader cr;		/* Parent */

  unsigned long case_idx;	/* Case number of current case. */
  bool destructive;		/* Is this a destructive reader? */
};



#define CHUNK_SIZE 10

static bool 
impl_get_case(const struct flexifile *ff, unsigned long casenum, 
	      struct ccase *);
static bool
impl_insert_case (struct flexifile *ff, struct ccase *c, int posn);

static bool 
impl_delete_cases (struct flexifile *ff, int n_cases, int first);

static bool 
impl_resize (struct flexifile *ff, int n_values, int posn);


/* Gets a case, for which writing may not be safe */
bool 
flexifile_get_case(const struct flexifile *ff, unsigned long casenum, 
		   struct ccase *c)
{
  const struct class_flexifile *class = 
    CONST_CLASS_FLEXIFILE (CONST_CASEFILE(ff)->class) ;

  return class->get_case(ff, casenum, c);
}


/* Insert N_VALUES before POSN.
   If N_VALUES is negative, then deleted -N_VALUES instead
*/
bool
flexifile_resize (struct flexifile *ff, int n_values, int posn)
{
  const struct class_flexifile *class = 
    CONST_CLASS_FLEXIFILE (CONST_CASEFILE(ff)->class) ;

  return class->resize(ff, n_values, posn);
}



bool
flexifile_insert_case (struct flexifile *ff, struct ccase *c, int posn)
{
  const struct class_flexifile *class = 
    CONST_CLASS_FLEXIFILE (CONST_CASEFILE(ff)->class) ;

  return class->insert_case(ff, c, posn);
}


bool
flexifile_delete_cases (struct flexifile *ff, int n_cases, int first)
{
  const struct class_flexifile *class = 
    CONST_CLASS_FLEXIFILE (CONST_CASEFILE(ff)->class) ;

  return class->delete_cases (ff, n_cases, first);
}


static unsigned long 
flexifile_get_case_cnt (const struct casefile *cf)
{
  return FLEXIFILE(cf)->case_cnt;
}

static size_t
flexifile_get_value_cnt (const struct casefile *cf)
{
  return FLEXIFILE(cf)->value_cnt;
}


static void
flexifile_destroy (struct casefile *cf)
{
  int i ; 
  for ( i = 0 ; i < FLEXIFILE(cf)->case_cnt; ++i ) 
    case_destroy( &FLEXIFILE(cf)->cases[i]);

  free(FLEXIFILE(cf)->cases);
}

static void
grow(struct flexifile *ff) 
{
  ff->capacity += CHUNK_SIZE;
  ff->cases = xrealloc(ff->cases, ff->capacity * sizeof ( *ff->cases) );
}

static bool
flexifile_append (struct casefile *cf, const struct ccase *c)
{
  struct flexifile *ff =  FLEXIFILE(cf);

  if (ff->case_cnt >= ff->capacity)
    grow(ff);

  case_clone (&ff->cases[ff->case_cnt++], c);

  return true;
}


static struct ccase *
flexifilereader_get_next_case (struct casereader *cr)
{
  struct flexifilereader *ffr = FLEXIFILEREADER(cr);
  struct flexifile *ff = FLEXIFILE(casereader_get_casefile(cr));

  if ( ffr->case_idx >= ff->case_cnt) 
    return NULL;

  return &ff->cases[ffr->case_idx++];
}

static void
flexifilereader_destroy(struct casereader *r)
{
  free(r);
}

static struct casereader * 
flexifile_get_reader (const struct casefile *cf_)
{
  struct casefile *cf = (struct casefile *) cf_;
  struct flexifilereader *ffr = xzalloc (sizeof *ffr);
  struct casereader *reader = (struct casereader *) ffr;

  casereader_register (cf, reader, CLASS_CASEREADER(&class_reader));

  return reader;
}


static struct casereader *
flexifilereader_clone (const struct casereader *cr)
{
  const struct flexifilereader *ffr = (const struct flexifilereader *) cr;
  struct flexifilereader *new_ffr = xzalloc (sizeof *new_ffr);
  struct casereader *new_reader = (struct casereader *) new_ffr;
  struct casefile *cf = casereader_get_casefile (cr);

  casereader_register (cf, new_reader, CLASS_CASEREADER(&class_reader));

  new_ffr->case_idx = ffr->case_idx ;
  new_ffr->destructive = ffr->destructive ;

  return new_reader;
}


static bool
flexifile_in_core(const struct casefile *cf UNUSED)
{
  /* Always in memory */
  return true;
}

static bool
flexifile_error (const struct casefile *cf UNUSED )
{
  return false;
}


struct casefile *
flexifile_create (size_t value_cnt)
{
  struct flexifile *ff = xzalloc (sizeof *ff);
  struct casefile *cf = (struct casefile *) ff;

  casefile_register (cf, (struct class_casefile *) &class);

  ff->value_cnt = value_cnt;
 
  ff->cases = xzalloc(sizeof (struct ccase *) * CHUNK_SIZE);
  ff->capacity = CHUNK_SIZE;
 
  return cf;
}

static const struct class_flexifile class = {
  {
    flexifile_destroy,
    flexifile_error,
    flexifile_get_value_cnt,
    flexifile_get_case_cnt,
    flexifile_get_reader,
    flexifile_append,

    flexifile_in_core,
    0, /* to_disk */
    0 /* sleep */
  },

  impl_get_case ,
  impl_insert_case ,
  impl_delete_cases,
  impl_resize,
};


static const struct class_flexifilereader class_reader = 
  {
    {
      flexifilereader_get_next_case,
      0,  /* cnum */
      flexifilereader_destroy,
      flexifilereader_clone
    }
  };


/* Implementations of class methods */

static bool 
impl_get_case(const struct flexifile *ff, unsigned long casenum, 
	      struct ccase *c)
{
  if ( casenum >= ff->case_cnt) 
    return false;

  case_clone (c, &ff->cases[casenum]);
  
  return true;
}

#if DEBUGGING
static void
dumpcasedata(struct ccase *c)
{
  int i;
  for ( i = 0 ; i < c->case_data->value_cnt * MAX_SHORT_STRING; ++i ) 
    putchar(c->case_data->values->s[i]);
  putchar('\n');
}
#endif

static bool 
impl_resize (struct flexifile *ff, int n_values, int posn)
{
  int i;

  for( i = 0 ; i < ff->case_cnt ; ++i ) 
    {
      struct ccase c;
      case_create (&c, ff->value_cnt + n_values);

      case_copy (&c, 0, &ff->cases[i], 0, posn);
      if ( n_values > 0 ) 
	memset (case_data_rw(&c, posn), ' ', n_values * MAX_SHORT_STRING) ;
      case_copy (&c, posn + n_values, 
		 &ff->cases[i], posn, ff->value_cnt - posn);

      case_destroy (&ff->cases[i]);
      ff->cases[i] = c;
    }

  ff->value_cnt += n_values;

  return true;
}

static bool
impl_insert_case (struct flexifile *ff, struct ccase *c, int posn)
{
  int i;
  struct ccase blank;
  
  assert (ff);

  if ( posn > ff->case_cnt )
    return false;

  if ( posn >= ff->capacity ) 
    grow(ff);

  case_create(&blank, ff->value_cnt);

  flexifile_append(CASEFILE(ff), &blank);

  case_destroy(&blank);

  /* Shift the existing cases down one */
  for ( i = ff->case_cnt ; i > posn; --i)
      case_move(&ff->cases[i], &ff->cases[i-1]);

  case_clone (&ff->cases[posn], c);

  return true;
}


static bool 
impl_delete_cases (struct flexifile *ff, int n_cases, int first)
{
  int i;

  if ( ff->case_cnt < first + n_cases ) 
    return false;

  for ( i = first ; i < first + n_cases; ++i ) 
    case_destroy (&ff->cases[i]);
  
  /* Shift the cases up by N_CASES */
  for ( i = first; i < ff->case_cnt - n_cases; ++i ) 
    {
      case_move (&ff->cases[i], &ff->cases[i+ n_cases]);
    }

  ff->case_cnt -= n_cases;
  
  return true;
}



