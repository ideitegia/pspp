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

/* This casewindow implementation in terms of an class interface
   is undoubtedly a form of over-abstraction.  However, it works
   and the extra abstraction seems to be harmless. */

#include <config.h>

#include <data/casewindow.h>

#include <stdlib.h>

#include <data/case-tmpfile.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/deque.h>
#include <libpspp/taint.h>

#include "xalloc.h"

/* A queue of cases. */
struct casewindow 
  {
    /* Common data. */
    size_t value_cnt;             /* Number of values per case. */
    casenumber max_in_core_cases; /* Max cases before dumping to disk. */
    struct taint *taint;          /* Taint status. */

    /* Implementation data. */
    const struct casewindow_class *class; 
    void *aux;
  };

/* Implementation of a casewindow. */
struct casewindow_class 
  {
    void *(*create) (struct taint *, size_t value_cnt);
    void (*destroy) (void *aux);
    void (*push_head) (void *aux, struct ccase *);
    void (*pop_tail) (void *aux, casenumber cnt);
    bool (*get_case) (void *aux, casenumber ofs, struct ccase *);
    casenumber (*get_case_cnt) (const void *aux);
  };

/* Classes. */
static const struct casewindow_class casewindow_memory_class;
static const struct casewindow_class casewindow_file_class;

/* Creates and returns a new casewindow using the given
   parameters. */
static struct casewindow *
do_casewindow_create (struct taint *taint,
                      size_t value_cnt, casenumber max_in_core_cases) 
{
  struct casewindow *cw = xmalloc (sizeof *cw);
  cw->class = (max_in_core_cases
              ? &casewindow_memory_class
              : &casewindow_file_class);
  cw->aux = cw->class->create (taint, value_cnt);
  cw->value_cnt = value_cnt;
  cw->max_in_core_cases = max_in_core_cases;
  cw->taint = taint;
  return cw;
}

/* Creates and returns a new casewindow for cases with VALUE_CNT
   values each.  If the casewindow holds more than
   MAX_IN_CORE_CASES cases at any time, its cases will be dumped
   to disk; otherwise, its cases will be held in memory. */   
struct casewindow *
casewindow_create (size_t value_cnt, casenumber max_in_core_cases) 
{
  return do_casewindow_create (taint_create (), value_cnt, max_in_core_cases);
}

/* Destroys casewindow CW.
   Returns true if CW was tainted, which is caused by an I/O
   error or by taint propagation to the casewindow. */
bool
casewindow_destroy (struct casewindow *cw) 
{
  bool ok = true;
  if (cw != NULL) 
    {
      cw->class->destroy (cw->aux);
      ok = taint_destroy (cw->taint);
      free (cw);
    }
  return ok;
}

/* Swaps the contents of casewindows A and B. */
static void
casewindow_swap (struct casewindow *a, struct casewindow *b) 
{
  struct casewindow tmp = *a;
  *a = *b;
  *b = tmp;
}

/* Dumps the contents of casewindow OLD to disk. */
static void
casewindow_to_disk (struct casewindow *old) 
{
  struct casewindow *new;
  new = do_casewindow_create (taint_clone (old->taint), old->value_cnt, 0);
  while (casewindow_get_case_cnt (old) > 0 && !casewindow_error (new))
    {
      struct ccase c;
      if (!casewindow_get_case (old, 0, &c))
        break;
      casewindow_pop_tail (old, 1);
      casewindow_push_head (new, &c);
    }
  casewindow_swap (old, new);
  casewindow_destroy (new);
}

/* Pushes case C at the head of casewindow CW.
   Case C becomes owned by the casewindow. */
void
casewindow_push_head (struct casewindow *cw, struct ccase *c) 
{
  if (!casewindow_error (cw))
    {
      cw->class->push_head (cw->aux, c);
      if (!casewindow_error (cw))
        {
          casenumber case_cnt = cw->class->get_case_cnt (cw->aux);
          if (case_cnt > cw->max_in_core_cases
              && cw->class == &casewindow_memory_class)
            casewindow_to_disk (cw); 
        }
    }
  else
    case_destroy (c);
}

/* Deletes CASE_CNT cases at the tail of casewindow CW. */
void
casewindow_pop_tail (struct casewindow *cw, casenumber case_cnt) 
{
  if (!casewindow_error (cw)) 
    cw->class->pop_tail (cw->aux, case_cnt);
}

/* Copies the case that is CASE_IDX cases away from CW's tail
   into C.  Returns true if successful, false on an I/O error or
   if CW is otherwise tainted.  On failure, nullifies case C. */
bool
casewindow_get_case (const struct casewindow *cw_, casenumber case_idx,
                     struct ccase *c) 
{
  struct casewindow *cw = (struct casewindow *) cw_;

  assert (case_idx >= 0 && case_idx < casewindow_get_case_cnt (cw));
  if (!casewindow_error (cw))
    return cw->class->get_case (cw->aux, case_idx, c);
  else 
    {
      case_nullify (c);
      return false;
    }
}

/* Returns the number of cases in casewindow CW. */
casenumber
casewindow_get_case_cnt (const struct casewindow *cw) 
{
  return cw->class->get_case_cnt (cw->aux);
}

/* Returns the number of values per case in casewindow CW. */
size_t
casewindow_get_value_cnt (const struct casewindow *cw) 
{
  return cw->value_cnt;
}

/* Returns true if casewindow CW is tainted.
   A casewindow is tainted by an I/O error or by taint
   propagation to the casewindow. */
bool
casewindow_error (const struct casewindow *cw) 
{
  return taint_is_tainted (cw->taint);
}

/* Marks casewindow CW tainted. */
void
casewindow_force_error (struct casewindow *cw) 
{
  taint_set_taint (cw->taint);
}

/* Returns casewindow CW's taint object. */
const struct taint *
casewindow_get_taint (const struct casewindow *cw) 
{
  return cw->taint;
}

/* In-memory casewindow data. */
struct casewindow_memory 
  {
    struct deque deque;
    struct ccase *cases;
  };

static void *
casewindow_memory_create (struct taint *taint UNUSED, size_t value_cnt UNUSED) 
{
  struct casewindow_memory *cwm = xmalloc (sizeof *cwm);
  cwm->cases = deque_init (&cwm->deque, 4, sizeof *cwm->cases);
  return cwm;
}

static void
casewindow_memory_destroy (void *cwm_) 
{
  struct casewindow_memory *cwm = cwm_;
  while (!deque_is_empty (&cwm->deque)) 
    case_destroy (&cwm->cases[deque_pop_front (&cwm->deque)]);
  free (cwm->cases);
  free (cwm);
}

static void
casewindow_memory_push_head (void *cwm_, struct ccase *c)
{
  struct casewindow_memory *cwm = cwm_;
  if (deque_is_full (&cwm->deque))
    cwm->cases = deque_expand (&cwm->deque, cwm->cases, sizeof *cwm->cases);
  case_move (&cwm->cases[deque_push_back (&cwm->deque)], c);
}

static void
casewindow_memory_pop_tail (void *cwm_, casenumber case_cnt)
{
  struct casewindow_memory *cwm = cwm_;
  assert (deque_count (&cwm->deque) >= case_cnt);
  while (case_cnt-- > 0) 
    case_destroy (&cwm->cases[deque_pop_front (&cwm->deque)]);
}

static bool
casewindow_memory_get_case (void *cwm_, casenumber ofs, struct ccase *c) 
{
  struct casewindow_memory *cwm = cwm_;
  case_clone (c, &cwm->cases[deque_front (&cwm->deque, ofs)]);
  return true;
}

static casenumber
casewindow_memory_get_case_cnt (const void *cwm_) 
{
  const struct casewindow_memory *cwm = cwm_;
  return deque_count (&cwm->deque);
}

static const struct casewindow_class casewindow_memory_class = 
  {
    casewindow_memory_create,
    casewindow_memory_destroy,
    casewindow_memory_push_head,
    casewindow_memory_pop_tail,
    casewindow_memory_get_case,
    casewindow_memory_get_case_cnt,
  };

/* On-disk casewindow data. */
struct casewindow_file 
  {
    struct case_tmpfile *file;
    casenumber head, tail;
  };

static void *
casewindow_file_create (struct taint *taint, size_t value_cnt) 
{
  struct casewindow_file *cwf = xmalloc (sizeof *cwf);
  cwf->file = case_tmpfile_create (value_cnt);
  cwf->head = cwf->tail = 0;
  taint_propagate (case_tmpfile_get_taint (cwf->file), taint);
  return cwf;
}

static void
casewindow_file_destroy (void *cwf_) 
{
  struct casewindow_file *cwf = cwf_;
  case_tmpfile_destroy (cwf->file);
  free (cwf);
}

static void
casewindow_file_push_head (void *cwf_, struct ccase *c) 
{
  struct casewindow_file *cwf = cwf_;
  if (case_tmpfile_put_case (cwf->file, cwf->head, c)) 
    cwf->head++;
}

static void
casewindow_file_pop_tail (void *cwf_, casenumber cnt) 
{
  struct casewindow_file *cwf = cwf_;
  assert (cnt <= cwf->head - cwf->tail);
  cwf->tail += cnt;
  if (cwf->head == cwf->tail)
    cwf->head = cwf->tail = 0;
}

static bool
casewindow_file_get_case (void *cwf_, casenumber ofs, struct ccase *c) 
{
  struct casewindow_file *cwf = cwf_;
  return case_tmpfile_get_case (cwf->file, cwf->tail + ofs, c);
}

static casenumber
casewindow_file_get_case_cnt (const void *cwf_) 
{
  const struct casewindow_file *cwf = cwf_;
  return cwf->head - cwf->tail;
}

static const struct casewindow_class casewindow_file_class = 
  {
    casewindow_file_create,
    casewindow_file_destroy,
    casewindow_file_push_head,
    casewindow_file_pop_tail,
    casewindow_file_get_case,
    casewindow_file_get_case_cnt,
  };
