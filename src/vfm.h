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

#if !vfm_h
#define vfm_h 1

#include "cases.h"
#include <time.h>

/* This is the time at which vfm was last invoked. */
extern time_t last_vfm_invocation;

/* This is the case that is to be filled in by input programs. */
extern struct ccase *temp_case;

/* `value' indexes to initialize to particular values for certain cases. */
extern struct long_vec reinit_sysmis;	/* SYSMIS for every case. */
extern struct long_vec reinit_blanks;	/* Blanks for every case. */
extern struct long_vec init_zero;	/* Zero for first case only. */
extern struct long_vec init_blanks;	/* Blanks for first case only. */

typedef struct write_case_data *write_case_data;
typedef int write_case_func (write_case_data);

/* A case stream: either a source or a sink, depending on context. */
struct case_stream
  {
    /* Initializes sink. */
    void (*init) (void);
    
    /* Reads all the cases and calls WRITE_CASE passing the given
       AUX data for each one. */
    void (*read) (write_case_func *, write_case_data);

    /* Writes a single case, temp_case. */
    void (*write) (void);

    /* Switches mode from sink to source. */
    void (*mode) (void);
    
    /* Discards source's internal data. */
    void (*destroy_source) (void);

    /* Discards sink's internal data. */
    void (*destroy_sink) (void);

    /* Identifying name for the stream. */
    const char *name;
  };

/* This is used to read from the active file. */
extern struct case_stream *vfm_source;

/* This is used to write to the replacement active file. */
extern struct case_stream *vfm_sink;

/* General data streams. */
extern struct case_stream vfm_memory_stream;
extern struct case_stream vfm_disk_stream;
extern struct case_stream sort_stream;
extern struct case_stream flip_stream;

/* Streams that are only sources. */
extern struct case_stream data_list_source;
extern struct case_stream input_program_source;
extern struct case_stream file_type_source;
extern struct case_stream get_source;
extern struct case_stream import_source;
extern struct case_stream matrix_data_source;

/* Number of cases to lag. */
extern int n_lag;

void procedure (void (*beginfunc) (void *aux),
		int (*procfunc) (struct ccase *curcase, void *aux),
		void (*endfunc) (void *aux),
                void *aux);
struct ccase *lagged_case (int n_before);
void compact_case (struct ccase *dest, const struct ccase *src);
void page_to_disk (void);

void process_active_file (void (*beginfunc) (void *),
			  int (*casefunc) (struct ccase *curcase, void *),
			  void (*endfunc) (void *),
                          void *aux);
void process_active_file_output_case (void);

#endif /* !vfm_h */
