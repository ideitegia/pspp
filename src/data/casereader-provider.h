/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011 Free Software Foundation, Inc.

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

/* Definitions needed to implement a new type of casereader.
   Code that only uses casereaders does not need this header.

   Two functions to create casereaders are supplied:

        - casereader_create_sequential, to create a casereader
          for a data source that is naturally sequential.  The
          casereader layer will automatically, as needed,
          simulate the ability to access cases randomly.

        - casereader_create_random, to create a casereader for a
          data source that supports random access to cases.  (This
          function is in fact implemented as a set of wrappers
          around casereader_create_sequential.)

   Which function is used has no effect on the set of operations
   that may be performed on the resulting casereader, only on how
   the casereader is implemented internally. */

#ifndef DATA_CASEREADER_PROVIDER_H
#define DATA_CASEREADER_PROVIDER_H 1

#include "data/casereader.h"

/* Casereader class for sequential data sources. */
struct casereader_class
  {
    /* Mandatory.

       Reads the next case from READER.  If successful, returns
       the case and advances READER, so that the next call to
       this function will read the following case.  The case just
       read will never be read again by a call to this function
       for READER.

       If a case is successfully returned, the client is
       responsible for calling case_unref upon it when it is no
       longer needed.

       At end of file or upon an I/O error, returns a null
       pointer.  After null is returned once, this function will
       not be called again for the given READER.

       If an I/O error occurs, this function should call
       casereader_force_error on READER. */
    struct ccase *(*read) (struct casereader *reader, void *aux);

    /* Mandatory.

       Destroys READER.

       If an I/O error is detected during destruction, this
       function should call casereader_force_error on READER. */
    void (*destroy) (struct casereader *reader, void *aux);

    /* Optional: if convenient and efficiently implementable,
       supply this function as an optimization for use by
       casereader_clone.  (But it might be easier to use the
       random-access casereader wrapper instead.)

       Creates and returns a clone of READER.  The clone must
       read the same case data in the same sequence as READER,
       starting from the same position.  The only allowable
       exception to this rule is that I/O errors may force the
       clone or the original casereader to stop reading after
       differing numbers of cases.

       The clone should have a clone of READER's taint object,
       accomplished by passing casereader_get_taint (READER) to
       casereader_create. */
    struct casereader *(*clone) (struct casereader *reader, void *aux);

    /* Optional: if convenient and efficiently implementable,
       supply as an optimization for use by casereader_peek.
       (But it might be easier to use the random-access
       casereader wrapper instead.)

       Reads and returns the case at 0-based offset IDX from the
       beginning of READER.  If a case is successfully returned,
       the client is responsible for calling case_unref upon it
       when it is no longer needed.

       At end of file or upon an I/O error, returns a null
       pointer.  If this function returns null, then it will
       never be called again for an equal or greater value of
       IDX, and the "read" member function will never be called
       to advance as far as IDX cases further into the
       casereader.  That is, returning null indicates that the
       casereader has fewer than IDX cases left.

       If an I/O error occurs, this function should call
       casereader_force_error on READER. */
    struct ccase *(*peek) (struct casereader *reader, void *aux,
                           casenumber idx);
  };

struct casereader *
casereader_create_sequential (const struct taint *,
                              const struct caseproto *, casenumber case_cnt,
                              const struct casereader_class *, void *);

void *casereader_dynamic_cast (struct casereader *, const struct casereader_class *);

/* Casereader class for random-access data sources. */
struct casereader_random_class
  {
    /* Mandatory.

       Reads the case at 0-based offset IDX from the beginning of
       READER.  If a case is successfully returned, the client is
       responsible for calling case_unref upon it when it is no
       longer needed.

       At end of file or upon an I/O error, returns a null
       pointer.  If this function returns null, then it will
       never be called again for an equal or greater value of
       IDX, and the "read" member function will never be called
       to advance as far as IDX cases further into the
       casereader.  That is, returning null indicates that the
       casereader has fewer than IDX cases.

       If an I/O error occurs, this function should call
       casereader_force_error on READER. */
    struct ccase *(*read) (struct casereader *reader, void *aux,
                           casenumber idx);

    /* Mandatory.

       Destroys READER.

       If an I/O error is detected during destruction, this
       function should call casereader_force_error on READER. */
    void (*destroy) (struct casereader *reader, void *aux);

    /* Mandatory.

       A call to this function tells the callee that the CNT
       cases at the beginning of READER will never be read again.
       The casereader implementation should free any resources
       associated with those cases.  After this function returns,
       the IDX argument in future calls to the "read" function
       will be relative to remaining cases. */
    void (*advance) (struct casereader *reader, void *aux, casenumber cnt);
  };

struct casereader *
casereader_create_random (const struct caseproto *, casenumber case_cnt,
                          const struct casereader_random_class *, void *aux);

#endif /* data/casereader-provider.h */
