/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#if !vfmP_h
#define vfmP_h 1

#include "var.h"

/* Linked list of cases. */
struct case_list 
  {
    struct case_list *next;
    struct ccase c;
  };

/* Describes a data stream, either a source or a sink. */
struct stream_info
  {
    int case_size;		/* Size of one case in bytes. */
    int ncases;			/* Number of cases. */
    int nval;			/* Number of `value' elements per case. */
  };

/* Information about the data source. */
extern struct stream_info vfm_source_info;

/* Information about the data sink. */
extern struct stream_info vfm_sink_info;

/* Memory case stream. */

/* List of cases stored in the stream. */
extern struct case_list *memory_source_cases;
extern struct case_list *memory_sink_cases;

/* Current case. */
extern struct case_list *memory_sink_iter;

/* Maximum number of cases. */
extern int memory_sink_max_cases;

/* Nonzero if the case needs to have values deleted before being
   stored, zero otherwise. */
extern int compaction_necessary;

/* Number of values after compaction, or the same as
   vfm_sink_info.nval, if compaction is not necessary. */
extern int compaction_nval;

/* Temporary case buffer with enough room for `compaction_nval'
   `value's. */
extern struct ccase *compaction_case;

void compact_case (struct ccase *dest, const struct ccase *src);

#endif /* !vfmP_h */
