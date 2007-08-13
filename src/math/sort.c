/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#include <config.h>

#include "sort.h"

#include <stdio.h>

#include <data/case-ordering.h>
#include <data/case.h>
#include <data/casereader.h>
#include <data/casewriter.h>
#include <data/casewriter-provider.h>
#include <data/settings.h>
#include <libpspp/alloc.h>
#include <libpspp/array.h>
#include <libpspp/assertion.h>
#include <math/merge.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* These should only be changed for testing purposes. */
int min_buffers = 64;
int max_buffers = INT_MAX;

struct sort_writer
  {
    struct case_ordering *ordering;
    struct merge *merge;
    struct pqueue *pqueue;

    struct casewriter *run;
    casenumber run_id;
    struct ccase run_end;
  };

static struct casewriter_class sort_casewriter_class;

static struct pqueue *pqueue_create (const struct case_ordering *);
static void pqueue_destroy (struct pqueue *);
static bool pqueue_is_full (const struct pqueue *);
static bool pqueue_is_empty (const struct pqueue *);
static void pqueue_push (struct pqueue *, struct ccase *, casenumber);
static void pqueue_pop (struct pqueue *, struct ccase *, casenumber *);

static void output_record (struct sort_writer *);

struct casewriter *
sort_create_writer (struct case_ordering *ordering)
{
  size_t value_cnt = case_ordering_get_value_cnt (ordering);
  struct sort_writer *sort;

  sort = xmalloc (sizeof *sort);
  sort->ordering = case_ordering_clone (ordering);
  sort->merge = merge_create (ordering);
  sort->pqueue = pqueue_create (ordering);
  sort->run = NULL;
  sort->run_id = 0;
  case_nullify (&sort->run_end);

  case_ordering_destroy (ordering);

  return casewriter_create (value_cnt, &sort_casewriter_class, sort);
}

static void
sort_casewriter_write (struct casewriter *writer UNUSED, void *sort_,
                       struct ccase *c)
{
  struct sort_writer *sort = sort_;
  bool next_run;

  if (pqueue_is_full (sort->pqueue))
    output_record (sort);

  next_run = (case_is_null (&sort->run_end)
              || case_ordering_compare_cases (c, &sort->run_end,
                                              sort->ordering) < 0);
  pqueue_push (sort->pqueue, c, sort->run_id + (next_run ? 1 : 0));
}

static void
sort_casewriter_destroy (struct casewriter *writer UNUSED, void *sort_)
{
  struct sort_writer *sort = sort_;

  case_ordering_destroy (sort->ordering);
  merge_destroy (sort->merge);
  pqueue_destroy (sort->pqueue);
  casewriter_destroy (sort->run);
  case_destroy (&sort->run_end);
  free (sort);
}

static struct casereader *
sort_casewriter_convert_to_reader (struct casewriter *writer, void *sort_)
{
  struct sort_writer *sort = sort_;
  struct casereader *output;

  if (sort->run == NULL && sort->run_id == 0)
    {
      /* In-core sort. */
      sort->run = mem_writer_create (case_ordering_get_value_cnt (
                                       sort->ordering));
      sort->run_id = 1;
    }
  while (!pqueue_is_empty (sort->pqueue))
    output_record (sort);

  merge_append (sort->merge, casewriter_make_reader (sort->run));
  sort->run = NULL;

  output = merge_make_reader (sort->merge);
  sort_casewriter_destroy (writer, sort);
  return output;
}

static void
output_record (struct sort_writer *sort)
{
  struct ccase min_case;
  casenumber min_run_id;

  pqueue_pop (sort->pqueue, &min_case, &min_run_id);
#if 0
  printf ("\toutput: %f to run %d\n", case_num_idx (&min_case, 0), min_run_id);
#endif

  if (sort->run_id != min_run_id && sort->run != NULL)
    {
      merge_append (sort->merge, casewriter_make_reader (sort->run));
      sort->run = NULL;
    }
  if (sort->run == NULL)
    {
      sort->run = tmpfile_writer_create (case_ordering_get_value_cnt (
                                           sort->ordering));
      sort->run_id = min_run_id;
    }

  case_destroy (&sort->run_end);
  case_clone (&sort->run_end, &min_case);

  casewriter_write (sort->run, &min_case);
}

static struct casewriter_class sort_casewriter_class =
  {
    sort_casewriter_write,
    sort_casewriter_destroy,
    sort_casewriter_convert_to_reader,
  };

/* Reads all the cases from INPUT.  Sorts the cases according to
   ORDERING.  Returns the sorted cases in a new casereader, or a
   null pointer if an I/O error occurs.  Both INPUT and ORDERING
   are destroyed upon return, regardless of success. */
struct casereader *
sort_execute (struct casereader *input, struct case_ordering *ordering)
{
  struct casewriter *output = sort_create_writer (ordering);
  casereader_transfer (input, output);
  return casewriter_make_reader (output);
}

struct pqueue
  {
    struct case_ordering *ordering;
    struct pqueue_record *records;
    size_t record_cnt;
    size_t record_cap;
    casenumber idx;
  };

struct pqueue_record
  {
    casenumber id;
    struct ccase c;
    casenumber idx;
  };

static int compare_pqueue_records_minheap (const void *a, const void *b,
                                           const void *pq_);

static struct pqueue *
pqueue_create (const struct case_ordering *ordering)
{
  struct pqueue *pq;

  pq = xmalloc (sizeof *pq);
  pq->ordering = case_ordering_clone (ordering);
  pq->record_cap
    = get_workspace_cases (case_ordering_get_value_cnt (ordering));
  if (pq->record_cap > max_buffers)
    pq->record_cap = max_buffers;
  else if (pq->record_cap < min_buffers)
    pq->record_cap = min_buffers;
  pq->record_cnt = 0;
  pq->records = xnmalloc (pq->record_cap, sizeof *pq->records);
  pq->idx = 0;

  return pq;
}

static void
pqueue_destroy (struct pqueue *pq)
{
  if (pq != NULL)
    {
      while (!pqueue_is_empty (pq))
        {
          struct ccase c;
          casenumber id;
          pqueue_pop (pq, &c, &id);
          case_destroy (&c);
        }
      case_ordering_destroy (pq->ordering);
      free (pq->records);
      free (pq);
    }
}

static bool
pqueue_is_full (const struct pqueue *pq)
{
  return pq->record_cnt >= pq->record_cap;
}

static bool
pqueue_is_empty (const struct pqueue *pq)
{
  return pq->record_cnt == 0;
}

static void
pqueue_push (struct pqueue *pq, struct ccase *c, casenumber id)
{
  struct pqueue_record *r;

  assert (!pqueue_is_full (pq));

  r = &pq->records[pq->record_cnt++];
  r->id = id;
  case_move (&r->c, c);
  r->idx = pq->idx++;

  push_heap (pq->records, pq->record_cnt, sizeof *pq->records,
             compare_pqueue_records_minheap, pq);
}

static void
pqueue_pop (struct pqueue *pq, struct ccase *c, casenumber *id)
{
  struct pqueue_record *r;

  assert (!pqueue_is_empty (pq));

  pop_heap (pq->records, pq->record_cnt--, sizeof *pq->records,
            compare_pqueue_records_minheap, pq);

  r = &pq->records[pq->record_cnt];
  *id = r->id;
  case_move (c, &r->c);
}

/* Compares record-run tuples A and B on id, then on case data,
   then on insertion order, in descending order. */
static int
compare_pqueue_records_minheap (const void *a_, const void *b_,
                                const void *pq_)
{
  const struct pqueue_record *a = a_;
  const struct pqueue_record *b = b_;
  const struct pqueue *pq = pq_;
  int result = a->id < b->id ? -1 : a->id > b->id;
  if (result == 0)
    result = case_ordering_compare_cases (&a->c, &b->c, pq->ordering);
  if (result == 0)
    result = a->idx < b->idx ? -1 : a->idx > b->idx;
  return -result;
}
