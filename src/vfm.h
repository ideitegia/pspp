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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#if !vfm_h
#define vfm_h 1

#include <time.h>

struct ccase;
typedef struct write_case_data *write_case_data;
typedef int write_case_func (write_case_data);

/* The current active file, from which cases are read. */
extern struct case_source *vfm_source;

/* A case source. */
struct case_source 
  {
    const struct case_source_class *class;      /* Class. */
    void *aux;          /* Auxiliary data. */
  };

/* A case source class. */
struct case_source_class
  {
    const char *name;                   /* Identifying name. */
    
    /* Returns the exact number of cases that READ will pass to
       WRITE_CASE, if known, or -1 otherwise. */
    int (*count) (const struct case_source *);

    /* Reads the cases one by one into C and for each one calls
       WRITE_CASE passing the given AUX data. */
    void (*read) (struct case_source *,
                  struct ccase *c,
                  write_case_func *write_case, write_case_data aux);

    /* Destroys the source. */
    void (*destroy) (struct case_source *);
  };

extern const struct case_source_class storage_source_class;
extern const struct case_source_class file_type_source_class;
extern const struct case_source_class input_program_source_class;

struct dictionary;
struct case_source *create_case_source (const struct case_source_class *,
                                        void *);
void free_case_source (struct case_source *);

int case_source_is_complex (const struct case_source *);
int case_source_is_class (const struct case_source *,
                          const struct case_source_class *);

struct casefile *storage_source_get_casefile (struct case_source *);
struct case_source *storage_source_create (struct casefile *);

/* The replacement active file, to which cases are written. */
extern struct case_sink *vfm_sink;

/* A case sink. */
struct case_sink 
  {
    const struct case_sink_class *class;        /* Class. */
    void *aux;          /* Auxiliary data. */
    size_t value_cnt;   /* Number of `union value's in case. */
  };

/* A case sink class. */
struct case_sink_class
  {
    const char *name;                   /* Identifying name. */
    
    /* Opens the sink for writing. */
    void (*open) (struct case_sink *);
                  
    /* Writes a case to the sink. */
    void (*write) (struct case_sink *, const struct ccase *);
    
    /* Closes and destroys the sink. */
    void (*destroy) (struct case_sink *);

    /* Closes the sink and returns a source that can read back
       the cases that were written, perhaps transformed in some
       way.  The sink must still be separately destroyed by
       calling destroy(). */
    struct case_source *(*make_source) (struct case_sink *);
  };

extern const struct case_sink_class storage_sink_class;
extern const struct case_sink_class null_sink_class;

struct case_sink *create_case_sink (const struct case_sink_class *,
                                    const struct dictionary *,
                                    void *);
void case_sink_open (struct case_sink *);
void case_sink_write (struct case_sink *, const struct ccase *);
void case_sink_destroy (struct case_sink *);
void free_case_sink (struct case_sink *);

/* Number of cases to lag. */
extern int n_lag;

void procedure (int (*proc_func) (struct ccase *, void *aux), void *aux);
void procedure_with_splits (void (*begin_func) (void *aux),
                            int (*proc_func) (struct ccase *, void *aux),
                            void (*end_func) (void *aux),
                            void *aux);
struct ccase *lagged_case (int n_before);

void multipass_procedure_with_splits (void (*) (const struct casefile *,
                                                void *),
                                      void *aux);

time_t vfm_last_invocation (void);

#endif /* !vfm_h */
