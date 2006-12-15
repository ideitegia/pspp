/* PSPP - computes sample statistics.
   Copyright (C) 2004, 2006 Free Software Foundation, Inc.

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
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#include "case.h"
#include "casefile.h"
#include "casefile-private.h"
#include "casefilter.h"


struct ccase;

/* A casefile is an abstract class representing an array of cases.  In
   general, cases are accessible sequentially,  and are immutable once
   appended to the casefile.  However some implementations may provide
   special methods for  case mutation or random access.

   Use casefile_append or casefile_append_xfer to append a case to a
   casefile. 

   The casefile may be read sequentially,
   starting from the beginning, by "casereaders".  Any
   number of casereaders may be created, at any time.
   Each casereader has an independent position in the casefile.

   Casereaders may only move forward.  They cannot move backward to
   arbitrary records or seek randomly.  Cloning casereaders is
   possible, but it is not yet implemented.

   Use casereader_read() or casereader_read_xfer() to read
   a case from a casereader.  Use casereader_destroy() to
   discard a casereader when it is no longer needed.

   When a casefile is no longer needed, it may be destroyed with
   casefile_destroy().  This function will also destroy any
   remaining casereaders. */

static struct ll_list all_casefiles = LL_INITIALIZER (all_casefiles);

static struct casefile *
ll_to_casefile (const struct ll *ll)
{
  return ll_data (ll, struct casefile, ll);
}

static struct casereader *
ll_to_casereader (const struct ll *ll)
{
  return ll_data (ll, struct casereader, ll);
}


/* atexit() handler that closes and deletes our temporary
   files. */
static void
exit_handler (void) 
{
  while (!ll_is_empty (&all_casefiles))
    casefile_destroy (ll_to_casefile (ll_head (&all_casefiles)));
}

/* Insert CF into the global list of casefiles */
void
casefile_register (struct casefile *cf, const struct class_casefile *class)
{
  static bool initialised ;
  if ( !initialised ) 
    {
      atexit (exit_handler);
      initialised = true;
    }

  cf->class = class;
  ll_push_head (&all_casefiles, &cf->ll);
  ll_init (&cf->reader_list);
}

/* Remove CF from the global list */
static void
casefile_unregister(struct casefile *cf)
{
  ll_remove (&cf->ll);
}

/* Return the casefile corresponding to this reader */
struct casefile *
casereader_get_casefile (const struct casereader *r)
{
  return r->cf;
}

/* Return the case number of the current case */
unsigned long
casereader_cnum(const struct casereader *r)
{
  return r->class->cnum(r);
}

static struct ccase *
get_next_case(struct casereader *reader)
{
  struct ccase *read_case = NULL;
  struct casefile *cf = casereader_get_casefile (reader);

  do 
    { 
      if ( casefile_error (cf) )
	return NULL;
  
      read_case = reader->class->get_next_case (reader);
    } 
  while ( read_case && reader->filter 
	  && casefilter_skip_case (reader->filter, read_case) ) ;

  return read_case;
}

/* Reads a copy of the next case from READER into C.
   Caller is responsible for destroying C.
   Returns true if successful, false at end of file. */
bool
casereader_read (struct casereader *reader, struct ccase *c)
{
  struct ccase * read_case = get_next_case (reader) ;

  if ( NULL == read_case ) 
    return false;

  case_clone (c, read_case );

  return true;
}


/* Reads the next case from READER into C and transfers ownership
   to the caller.  Caller is responsible for destroying C.
   Returns true if successful, false at end of file or on I/O
   error. */
bool
casereader_read_xfer (struct casereader *reader, struct ccase *c)
{
  struct casefile *cf = casereader_get_casefile (reader);
  struct ccase *read_case ;
  case_nullify (c);

  read_case = get_next_case (reader) ;

  if ( NULL == read_case )
    return false;

  if ( reader->destructive && casefile_in_core (cf) )
    case_move (c, read_case);
  else
    case_clone (c, read_case);

  return true;
}

/* Destroys R. */
void 
casereader_destroy (struct casereader *r)
{
  ll_remove (&r->ll);

  r->class->destroy(r);
}

/* Creates a copy of R and returns it */
struct casereader *
casereader_clone(const struct casereader *r)
{
  struct casereader *r2;

  /* Would we ever want to clone a destructive reader ?? */
  assert ( ! r->destructive ) ;

  r2 = r->class->clone (r);

  r2->filter = r->filter;

  return r2;
}

/* Destroys casefile CF. */
void
casefile_destroy(struct casefile *cf)
{
  if (!cf) return;
  
  assert(cf->class->destroy);

  while (!ll_is_empty (&cf->reader_list))
    casereader_destroy (ll_to_casereader (ll_head (&cf->reader_list)));
      
  casefile_unregister(cf);

  cf->class->destroy(cf);
}

/* Returns true if an I/O error has occurred in casefile CF. */
bool 
casefile_error (const struct casefile *cf)
{
  return cf->class->error(cf);
}

/* Returns the number of cases in casefile CF. */
unsigned long 
casefile_get_case_cnt (const struct casefile *cf)
{
  return cf->class->get_case_cnt(cf);
}

/* Returns the number of `union value's in a case for CF. */
size_t 
casefile_get_value_cnt (const struct casefile *cf)
{
  return cf->class->get_value_cnt(cf);
}

/* Creates and returns a casereader for CF.  A casereader can be used to
   sequentially read the cases in a casefile. */
struct casereader *
casefile_get_reader  (const struct casefile *cf, struct casefilter *filter)
{
  struct casereader *r = cf->class->get_reader(cf);
  r->cf = (struct casefile *) cf;
  r->filter = filter;

  assert (r->class);
  
  return r;
}

/* Creates and returns a destructive casereader for CF.  Like a
   normal casereader, a destructive casereader sequentially reads
   the cases in a casefile.  Unlike a normal casereader, a
   destructive reader cannot operate concurrently with any other
   reader.  (This restriction could be relaxed in a few ways, but
   it is so far unnecessary for other code.) */
struct casereader *
casefile_get_destructive_reader (struct casefile *cf) 
{
  struct casereader *r = cf->class->get_reader (cf);
  r->cf = cf;
  r->destructive = true;
  cf->being_destroyed = true;

  return r;
}

/* Appends a copy of case C to casefile CF. 
   Returns true if successful, false if an I/O error occurred. */
bool 
casefile_append (struct casefile *cf, const struct ccase *c)
{
  assert (c->case_data->value_cnt >= casefile_get_value_cnt (cf));

  return cf->class->append(cf, c);
}

/* Appends case C to casefile CF, which takes over ownership of
   C.  
   Returns true if successful, false if an I/O error occurred. */
bool 
casefile_append_xfer (struct casefile *cf, struct ccase *c)
{
  assert (c->case_data->value_cnt >= casefile_get_value_cnt (cf));

  cf->class->append (cf, c);
  case_destroy (c);

  return cf->class->error (cf);
}




/* Puts a casefile to "sleep", that is, minimizes the resources
   needed for it by closing its file descriptor and freeing its
   buffer.  This is useful if we need so many casefiles that we
   might not have enough memory and file descriptors to go
   around.
  
   Implementations may choose to silently ignore this function.

   Returns true if successful, false if an I/O error occurred. */
bool
casefile_sleep (const struct casefile *cf)
{
  return cf->class->sleep ? cf->class->sleep(cf) : true;
}

/* Returns true only if casefile CF is stored in memory (instead of on
   disk), false otherwise. 
*/
bool
casefile_in_core (const struct casefile *cf)
{
  return cf->class->in_core(cf);
}

/* If CF is currently stored in memory, writes it to disk.  Readers, if any,
   retain their current positions.

   Implementations may choose to silently ignore this function.

   Returns true if successful, false if an I/O error occurred. */
bool 
casefile_to_disk (const struct casefile *cf)
{
  return cf->class->to_disk ? cf->class->to_disk(cf) : true;
}

void
casereader_register(struct casefile *cf, 
		    struct casereader *reader, 
		    const struct class_casereader *class)
{
  reader->class = class;
  reader->cf = cf;
      
  ll_push_head (&cf->reader_list, &reader->ll);
}
