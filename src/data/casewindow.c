/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011 Free Software Foundation, Inc.

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

/* This casewindow implementation in terms of an class interface
   is undoubtedly a form of over-abstraction.  However, it works
   and the extra abstraction seems to be harmless. */

#include <config.h>

#include "data/casewindow.h"

#include <stdlib.h>

#include "data/case-tmpfile.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/deque.h"
#include "libpspp/taint.h"

#include "gl/xalloc.h"

/* A queue of cases. */
struct casewindow
  {
    /* Common data. */
    struct caseproto *proto;      /* Prototype of cases in window. */
    casenumber max_in_core_cases; /* Max cases before dumping to disk. */
    struct taint *taint;          /* Taint status. */

    /* Implementation data. */
    const struct casewindow_class *class;
    void *aux;
  };

/* Implementation of a casewindow. */
struct casewindow_class
  {
    void *(*create) (struct taint *, const struct caseproto *);
    void (*destroy) (void *aux);
    void (*push_head) (void *aux, struct ccase *);
    void (*pop_tail) (void *aux, casenumber cnt);
    struct ccase *(*get_case) (void *aux, casenumber ofs);
    casenumber (*get_case_cnt) (const void *aux);
  };

/* Classes. */
static const struct casewindow_class casewindow_memory_class;
static const struct casewindow_class casewindow_file_class;

/* Creates and returns a new casewindow using the given
   parameters. */
static struct casewindow *
do_casewindow_create (struct taint *taint, const struct caseproto *proto,
                      casenumber max_in_core_cases)
{
  struct casewindow *cw = xmalloc (sizeof *cw);
  cw->class = (max_in_core_cases
              ? &casewindow_memory_class
              : &casewindow_file_class);
  cw->aux = cw->class->create (taint, proto);
  cw->proto = caseproto_ref (proto);
  cw->max_in_core_cases = max_in_core_cases;
  cw->taint = taint;
  return cw;
}

/* Creates and returns a new casewindow for cases that take the
   form specified by PROTO.  If the casewindow holds more than
   MAX_IN_CORE_CASES cases at any time, its cases will be dumped
   to disk; otherwise, its cases will be held in memory.

   The caller retains its reference to PROTO. */
struct casewindow *
casewindow_create (const struct caseproto *proto, casenumber max_in_core_cases)
{
  return do_casewindow_create (taint_create (), proto, max_in_core_cases);
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
      caseproto_unref (cw->proto);
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
  new = do_casewindow_create (taint_clone (old->taint), old->proto, 0);
  while (casewindow_get_case_cnt (old) > 0 && !casewindow_error (new))
    {
      struct ccase *c = casewindow_get_case (old, 0);
      if (c == NULL)
        break;
      casewindow_pop_tail (old, 1);
      casewindow_push_head (new, c);
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
    case_unref (c);
}

/* Deletes CASE_CNT cases at the tail of casewindow CW. */
void
casewindow_pop_tail (struct casewindow *cw, casenumber case_cnt)
{
  if (!casewindow_error (cw))
    cw->class->pop_tail (cw->aux, case_cnt);
}

/* Returns the case that is CASE_IDX cases away from CW's tail
   into C, or a null pointer on an I/O error or if CW is
   otherwise tainted.  The caller must call case_unref() on the
   returned case when it is no longer needed. */
struct ccase *
casewindow_get_case (const struct casewindow *cw_, casenumber case_idx)
{
  struct casewindow *cw = CONST_CAST (struct casewindow *, cw_);

  assert (case_idx >= 0 && case_idx < casewindow_get_case_cnt (cw));
  if (casewindow_error (cw))
    return NULL;
  return cw->class->get_case (cw->aux, case_idx);
}

/* Returns the number of cases in casewindow CW. */
casenumber
casewindow_get_case_cnt (const struct casewindow *cw)
{
  return cw->class->get_case_cnt (cw->aux);
}

/* Returns the case prototype for the cases in casewindow CW.
   The caller must not unref the returned prototype. */
const struct caseproto *
casewindow_get_proto (const struct casewindow *cw)
{
  return cw->proto;
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
    struct ccase **cases;
  };

static void *
casewindow_memory_create (struct taint *taint UNUSED,
                          const struct caseproto *proto UNUSED)
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
    case_unref (cwm->cases[deque_pop_front (&cwm->deque)]);
  free (cwm->cases);
  free (cwm);
}

static void
casewindow_memory_push_head (void *cwm_, struct ccase *c)
{
  struct casewindow_memory *cwm = cwm_;
  if (deque_is_full (&cwm->deque))
    cwm->cases = deque_expand (&cwm->deque, cwm->cases, sizeof *cwm->cases);
  cwm->cases[deque_push_back (&cwm->deque)] = c;
}

static void
casewindow_memory_pop_tail (void *cwm_, casenumber case_cnt)
{
  struct casewindow_memory *cwm = cwm_;
  assert (deque_count (&cwm->deque) >= case_cnt);
  while (case_cnt-- > 0)
    case_unref (cwm->cases[deque_pop_front (&cwm->deque)]);
}

static struct ccase *
casewindow_memory_get_case (void *cwm_, casenumber ofs)
{
  struct casewindow_memory *cwm = cwm_;
  return case_ref (cwm->cases[deque_front (&cwm->deque, ofs)]);
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
casewindow_file_create (struct taint *taint, const struct caseproto *proto)
{
  struct casewindow_file *cwf = xmalloc (sizeof *cwf);
  cwf->file = case_tmpfile_create (proto);
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

static struct ccase *
casewindow_file_get_case (void *cwf_, casenumber ofs)
{
  struct casewindow_file *cwf = cwf_;
  return case_tmpfile_get_case (cwf->file, cwf->tail + ofs);
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
