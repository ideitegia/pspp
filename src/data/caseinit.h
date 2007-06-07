/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

/* Case initializer.
   
   The procedure code has to resize cases provided by the active
   file data source, to provide room for any other variables that
   should go in the case, fill in the values of "left" variables,
   and initialize the values of other non-left variable to zero
   or spaces.  Then, when we're done with that case, we have to
   save the values of "left" variables to copy into the next case
   read from the active file.

   The caseinit code helps with this. */

#ifndef DATA_CASEINIT_H
#define DATA_CASEINIT_H 1

struct dictionary;
struct ccase;

struct caseinit *caseinit_create (void);
void caseinit_clear (struct caseinit *);
void caseinit_destroy (struct caseinit *);

void caseinit_mark_as_preinited (struct caseinit *, const struct dictionary *);
void caseinit_mark_for_init (struct caseinit *, const struct dictionary *);

void caseinit_init_reinit_vars (const struct caseinit *, struct ccase *);
void caseinit_init_left_vars (const struct caseinit *, struct ccase *);
void caseinit_update_left_vars (struct caseinit *, const struct ccase *);

#endif /* data/caseinit.h */
