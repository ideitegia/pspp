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

#include <time.h>

/* This is the time at which vfm was last invoked. */
extern time_t last_vfm_invocation;

/* This is the case that is to be filled in by input programs. */
extern struct ccase *temp_case;

typedef struct write_case_data *write_case_data;
typedef int write_case_func (write_case_data);

/* The current active file, from which cases are read. */
extern struct case_source *vfm_source;

/* A case source. */
struct case_source 
  {
    const struct case_source_class *class;      /* Class. */
    void *aux;                                  /* Auxiliary data. */
  };

/* A case source class. */
struct case_source_class
  {
    const char *name;                   /* Identifying name. */
    
    /* Returns the exact number of cases that READ will pass to
       WRITE_CASE, if known, or -1 otherwise. */
    int (*count) (const struct case_source *);

    /* Reads all the cases and calls WRITE_CASE passing the given
       AUX data for each one. */
    void (*read) (struct case_source *, write_case_func *, write_case_data);

    /* Destroys the source. */
    void (*destroy) (struct case_source *);
  };

extern const struct case_source_class memory_source_class;
extern const struct case_source_class disk_source_class;
extern const struct case_source_class data_list_source_class;
extern const struct case_source_class file_type_source_class;
extern const struct case_source_class input_program_source_class;
extern const struct case_source_class get_source_class;
extern const struct case_source_class import_source_class;
extern const struct case_source_class sort_source_class;

struct case_source *create_case_source (const struct case_source_class *,
                                        void *);
int case_source_is_complex (const struct case_source *);
int case_source_is_class (const struct case_source *,
                          const struct case_source_class *);
struct case_list *memory_source_get_cases (const struct case_source *);
void memory_source_set_cases (const struct case_source *,
                                     struct case_list *);

/* The replacement active file, to which cases are written. */
extern struct case_sink *vfm_sink;

/* A case sink. */
struct case_sink 
  {
    const struct case_sink_class *class;        /* Class. */
    void *aux;                                  /* Auxiliary data. */
  };

/* A case sink class. */
struct case_sink_class
  {
    const char *name;                   /* Identifying name. */
    
    /* Creates the sink and opens it for writing. */
    void (*open) (struct case_sink *);
                  
    /* Writes a case to the sink. */
    void (*write) (struct case_sink *, struct ccase *);
    
    /* Closes and destroys the sink. */
    void (*destroy) (struct case_sink *);

    /* Closes and destroys the sink and returns a source that can
       read back the cases that were written, perhaps transformed
       in some way. */
    struct case_source *(*make_source) (struct case_sink *);
  };

extern const struct case_sink_class memory_sink_class;
extern const struct case_sink_class disk_sink_class;
extern const struct case_sink_class sort_sink_class;

struct case_sink *create_case_sink (const struct case_sink_class *, void *);

/* Number of cases to lag. */
extern int n_lag;

void procedure (void (*beginfunc) (void *aux),
		int (*procfunc) (struct ccase *curcase, void *aux),
		void (*endfunc) (void *aux),
                void *aux);
struct ccase *lagged_case (int n_before);
void compact_case (struct ccase *dest, const struct ccase *src);
void write_active_file_to_disk (void);

void process_active_file (void (*beginfunc) (void *),
			  int (*casefunc) (struct ccase *curcase, void *),
			  void (*endfunc) (void *),
                          void *aux);
void process_active_file_output_case (void);

#endif /* !vfm_h */
