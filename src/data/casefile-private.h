/* PSPP - computes sample statistics.
   Copyright (C) 2004, 2006 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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

#ifndef CASEFILE_PRIVATE_H
#define CASEFILE_PRIVATE_H

#include <config.h>
#include <stdbool.h>
#include <libpspp/ll.h>

struct ccase;
struct casereader;
struct casefile;

struct class_casefile
{
  void (*destroy) (struct casefile *) ;

  bool (*error) (const struct casefile *) ;

  size_t (*get_value_cnt) (const struct casefile *) ;
  unsigned long (*get_case_cnt) (const struct casefile *) ;

  struct casereader * (*get_reader) (const struct casefile *) ; 

  bool (*append) (struct casefile *, const struct ccase *) ;


  bool (*in_core) (const struct casefile *) ;
  bool (*to_disk) (const struct casefile *) ;
  bool (*sleep) (const struct casefile *) ;
};

struct casefile
{
  const struct class_casefile *class ;   /* Class pointer */

  struct ll_list reader_list ;       /* List of our readers. */
  struct ll ll ;                    /* Element in the class' list 
				       of casefiles. */
  bool being_destroyed;            /* A destructive reader exists */
};


struct class_casereader
{
  struct ccase * (*get_next_case) (struct casereader *);

  unsigned long (*cnum) (const struct casereader *);

  void (*destroy) (struct casereader * r);

  struct casereader * (*clone) (const struct casereader *);
};


#define CLASS_CASEREADER(K) ( (struct class_casereader *) K)

struct casereader
{
  const struct class_casereader *class;  /* Class pointer */

  struct casefile *cf;   /* The casefile to which this reader belongs */
  struct ll ll;          /* Element in the casefile's list of readers */
  bool destructive;      /* True if this reader is destructive */
};


#define CASEFILE(C)        ( (struct casefile *) C)
#define CONST_CASEFILE(C) ( (const struct casefile *) C)

#define CASEFILEREADER(CR) ((struct casereader *) CR)


/* Functions for implementations' use  only */

void casefile_register (struct casefile *cf, 
			const struct class_casefile *k);

void casereader_register (struct casefile *cf, 
			  struct casereader *reader, 
			  const struct class_casereader *k);

#endif
