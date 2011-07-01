/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2011 Free Software Foundation, Inc.

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

#ifndef DATA_CASEWRITER_PROVIDER_H
#define DATA_CASEWRITER_PROVIDER_H 1

#include "data/casewriter.h"

struct casewriter_class
  {
    /* Mandatory.

       Writes case C to WRITER.  Ownership of C is transferred to
       WRITER.

       If an I/O error occurs, this function should call
       casewriter_force_error on WRITER.  Some I/O error
       detection may be deferred to the "destroy" member function
       (e.g. writes to disk need not be flushed by "write") . */
    void (*write) (struct casewriter *writer, void *aux, struct ccase *c);

    /* Mandatory.

       Finalizes output and destroys WRITER.

       If an I/O error is detected while finalizing output
       (e.g. while flushing output to disk), this function should
       call casewriter_force_error on WRITER. */
    void (*destroy) (struct casewriter *writer, void *aux);

    /* Optional: supply if practical and desired by clients.

       Finalizes output to WRITER, destroys WRITER, and in its
       place returns a casereader that can be used to read back
       the data written to WRITER.  WRITER will not be used again
       after calling this function, even as an argument to
       casewriter_destroy.

       If an I/O error is detected while finalizing output
       (e.g. while flushing output to disk), this function should
       call casewriter_force_error on WRITER.  The caller will
       ensure that the error is propagated to the returned
       casereader. */
    struct casereader *(*convert_to_reader) (struct casewriter *, void *aux);
  };

struct casewriter *casewriter_create (const struct caseproto *,
                                      const struct casewriter_class *, void *);

#endif /* data/casewriter-provider.h */
