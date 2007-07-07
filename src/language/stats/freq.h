/* PSPP - a program for statistical analysis.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#ifndef freq_h
#define freq_h

union value ;
/* Frequency table entry. */
struct freq
  {
    const union value *value;	/* The value. */
    double count;		/* The number of occurrences of the value. */
  };

/* Non const version of frequency table entry. */
struct freq_mutable
  {
    union value *value;	        /* The value. */
    double count;		/* The number of occurrences of the value. */
  };


int compare_freq ( const void *_f1, const void *_f2, const void *_var);

unsigned int hash_freq (const void *_f, const void *_var);

/* Free function for struct freq */
void free_freq_hash (void *fr, const void *aux);

/* Free function for struct freq_mutable */
void free_freq_mutable_hash (void *fr, const void *var);



#endif
