/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_TAINT_H
#define LIBPSPP_TAINT_H 1

/* Tainting and taint propagation.

   Properly handling I/O errors and other hard errors in data
   handling is important.  At a minimum, we must notify the user
   that an error occurred and refrain from presenting possibly
   corrupted output.  It is unacceptable, however, to simply
   terminate PSPP when an I/O error occurs, because of the
   unfriendliness of that approach, especially in a GUI
   environment.  We should also propagate the error to the top
   level of command execution; that is, ensure that the command
   procedure returns CMD_CASCADING_FAILURE to its caller.

   Usually in C we propagate errors via return values, or by
   maintaining an error state on an object (e.g. the error state
   that the ferror function tests on C streams).  But neither
   approach is ideal for PSPP.  Using return values requires the
   programmer to pay more attention to error handling than one
   would like, especially given how difficult it can be to test
   error paths.  Maintaining error states on important PSPP
   objects (e.g. casereaders, casewriters) is a step up, but it
   still requires more attention than one would like, because
   quite often there are many such objects in use at any given
   time, and an I/O error encountered by any of them indicates
   that the final result of any computation that depends on that
   object is incorrect.

   The solution implemented here is an attempt to automate as
   much as possible of PSPP's error-detection problem.  It is
   based on use of "taint" objects, created with taint_create or
   taint_clone.  Each taint object represents a state of
   correctness or corruption (taint) in an associated object
   whose correctness must be established.  The taint_set_taint
   function is used to mark a taint object as tainted.  The taint
   status of a taint object can be queried with taint_is_tainted.

   The benefit of taint objects lies in the ability to connect
   them together in propagation relationships, using
   taint_propagate.  The existence of a propagation relationship
   from taint object A to taint object B means that, should
   object A ever become tainted, then object B will automatically
   be marked tainted as well.  This models the situation where
   the data represented by B are derived from data obtained from
   A.  This is a common situation in PSPP; for example, the data
   in one casereader or casewriter are often derived from data in
   another casereader or casewriter.

   Taint propagation is transitive: if A propagates to B and B
   propagates to C, then tainting A taints both B and C.  Taint
   propagation is not commutative: propagation from A to B does
   not imply propagation from B to A.  However, taint propagation
   is robust against loops, so that if A propagates to B and vice
   versa, whether directly or indirectly, then tainting either A
   or B will cause the other to be tainted, without producing an
   infinite loop.

   The implementation is robust against destruction of taints in
   propagation relationships.  When this happens, taint
   propagation through the destroyed taint object is preserved,
   that is, if A taints B and B taints C, then destroying B will
   preserve the transitive relationship, so that tainting A will
   still taint C.

   Taint objects actually propagate two different types of taints
   across the taint graph.  The first type of taint is the one
   already described, which indicates that an associated object
   has corrupted state.  The second type of taint, called a
   "successor-taint" does not necessarily indicate that the
   associated object is corrupted.  Rather, it indicates some
   successor of the associated object is corrupted, or was
   corrupted some time in the past before it was destroyed.  (A
   "successor" of a taint object X is any taint object that can
   be reached by following propagation relationships starting
   from X.)  Stated another way, when a taint object is marked
   tainted, all the taint objects that are reachable by following
   propagation relationships *backward* are marked with a
   successor-taint.  In addition, any object that is marked
   tainted is also marked successor-tainted.

   The value of a successor-taint is in summarizing the history
   of the taint objects derived from a common parent.  For
   example, consider a casereader that represents the active
   dataset.  A statistical procedure can clone this casereader any
   number of times and pass it to analysis functions, which may
   themselves in turn clone it themselves, pass it to sort or
   merge functions, etc.  Conventionally, all of these functions
   would have to carefully check for I/O errors and propagate
   them upward, which is error-prone and inconvenient.  However,
   given the successor-taint feature, the statistical procedure
   may simply check the successor-taint on the top-level
   casereader after calling the analysis functions and, if a
   successor-taint is present, skip displaying the procedure's
   output.  Thus, error checking is centralized, simplified, and
   made convenient.  This feature is now used in a number of the
   PSPP statistical procedures; search the source tree for
   "taint_has_tainted_successor" for details. */

#include <stdbool.h>

struct taint *taint_create (void);
struct taint *taint_clone (const struct taint *);
bool taint_destroy (struct taint *);

void taint_propagate (const struct taint *from, const struct taint *to);

bool taint_is_tainted (const struct taint *);
void taint_set_taint (const struct taint *);

bool taint_has_tainted_successor (const struct taint *);
void taint_reset_successor_taint (const struct taint *);

#endif /* libpspp/taint.h */
