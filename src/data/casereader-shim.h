/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010 Free Software Foundation, Inc.

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

#ifndef DATA_CASEREADER_SHIM_H
#define DATA_CASEREADER_SHIM_H 1

/* Buffering shim for implementing clone and peek operations.

   The "clone" and "peek" operations aren't implemented by all types of
   casereaders, but we have to expose a uniform interface anyhow.  The
   casereader buffering shim can do this by interposing a buffer on top of an
   existing casereader.  The shim maintains a window of cases that spans the
   positions of the original casereader and all of its clones (the "clone
   set"), from the position of the casereader that has read the fewest cases to
   the position of the casereader that has read the most.

   Thus, if all of the casereaders in the clone set are at approximately the
   same position, only a few cases are buffered and there is little
   inefficiency.  If, on the other hand, one casereader is not used to read any
   cases at all, but another one is used to read all of the cases, the entire
   contents of the casereader is copied into the buffer.  This still might not
   be so inefficient, given that case data in memory is shared across multiple
   identical copies, but in the worst case the window implementation will write
   cases to disk instead of maintaining them in-memory.

   Casereader buffering shims are inserted automatically on the first call to
   casereader_clone() or casereader_peek() for a casereader that does not
   support those operations natively.  Thus, there is ordinarily little reason
   to intentionally insert a shim. */

struct casereader;
struct casereader_shim *casereader_shim_insert (struct casereader *);
void casereader_shim_slurp (struct casereader_shim *);

#endif /* data/casereader-shim.h */
