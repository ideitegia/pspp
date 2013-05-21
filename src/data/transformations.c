/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2011, 2013 Free Software Foundation, Inc.

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

#include "data/transformations.h"

#include <assert.h>
#include <stdlib.h>

#include "libpspp/str.h"

#include "gl/xalloc.h"

/* A single transformation. */
struct transformation
  {
    /* Offset to add to EXECUTE's return value, if it returns a
       transformation index.  Normally 0 but set to the starting
       index of a spliced chain after splicing. */
    int idx_ofs;
    trns_finalize_func *finalize;       /* Finalize proc. */
    trns_proc_func *execute;            /* Executes the transformation. */
    trns_free_func *free;               /* Garbage collector proc. */
    void *aux;                          /* Auxiliary data. */
  };

/* A chain of transformations. */
struct trns_chain
  {
    struct transformation *trns;        /* Array of transformations. */
    size_t trns_cnt;                    /* Number of transformations. */
    size_t trns_cap;                    /* Allocated capacity. */
    bool finalized;                     /* Finalize functions called? */
  };

/* Allocates and returns a new transformation chain. */
struct trns_chain *
trns_chain_create (void)
{
  struct trns_chain *chain = xmalloc (sizeof *chain);
  chain->trns = NULL;
  chain->trns_cnt = 0;
  chain->trns_cap = 0;
  chain->finalized = false;
  return chain;
}

/* Finalizes all the un-finalized transformations in CHAIN.
   Any given transformation is only finalized once. */
void
trns_chain_finalize (struct trns_chain *chain)
{
  while (!chain->finalized)
    {
      size_t i;

      chain->finalized = true;
      for (i = 0; i < chain->trns_cnt; i++)
        {
          struct transformation *trns = &chain->trns[i];
          trns_finalize_func *finalize = trns->finalize;

          trns->finalize = NULL;
          if (finalize != NULL)
            finalize (trns->aux);
        }
    }
}

/* Destroys CHAIN, finalizing it in the process if it has not
   already been finalized. */
bool
trns_chain_destroy (struct trns_chain *chain)
{
  bool ok = true;

  if (chain != NULL)
    {
      size_t i;

      /* Needed to ensure that the control stack gets cleared. */
      trns_chain_finalize (chain);

      for (i = 0; i < chain->trns_cnt; i++)
        {
          struct transformation *trns = &chain->trns[i];
          if (trns->free != NULL)
            ok = trns->free (trns->aux) && ok;
        }
      free (chain->trns);
      free (chain);
    }

  return ok;
}

/* Returns true if CHAIN contains any transformations,
   false otherwise. */
bool
trns_chain_is_empty (const struct trns_chain *chain)
{
  return chain->trns_cnt == 0;
}

/* Adds a transformation to CHAIN with finalize function
   FINALIZE, execute function EXECUTE, free function FREE, and
   auxiliary data AUX. */
void
trns_chain_append (struct trns_chain *chain, trns_finalize_func *finalize,
                   trns_proc_func *execute, trns_free_func *free,
                   void *aux)
{
  struct transformation *trns;

  chain->finalized = false;

  if (chain->trns_cnt == chain->trns_cap)
    chain->trns = x2nrealloc (chain->trns, &chain->trns_cap,
                              sizeof *chain->trns);

  trns = &chain->trns[chain->trns_cnt++];
  trns->idx_ofs = 0;
  trns->finalize = finalize;
  trns->execute = execute;
  trns->free = free;
  trns->aux = aux;
}

/* Appends the transformations in SRC to those in DST,
   and destroys SRC.
   Both DST and SRC must already be finalized. */
void
trns_chain_splice (struct trns_chain *dst, struct trns_chain *src)
{
  size_t i;

  assert (dst->finalized);
  assert (src->finalized);

  if (dst->trns_cnt + src->trns_cnt > dst->trns_cap)
    {
      dst->trns_cap = dst->trns_cnt + src->trns_cnt;
      dst->trns = xnrealloc (dst->trns, dst->trns_cap, sizeof *dst->trns);
    }

  for (i = 0; i < src->trns_cnt; i++)
    {
      struct transformation *d = &dst->trns[i + dst->trns_cnt];
      const struct transformation *s = &src->trns[i];
      *d = *s;
      d->idx_ofs += src->trns_cnt;
    }
  dst->trns_cnt += src->trns_cnt;

  src->trns_cnt = 0;
  trns_chain_destroy (src);
}

/* Returns the index that a transformation execution function may
   return to "jump" to the next transformation to be added. */
size_t
trns_chain_next (struct trns_chain *chain)
{
  return chain->trns_cnt;
}

/* Executes the given CHAIN of transformations on *C,
   passing CASE_NR as the case number.
   *C may be replaced by a new case.
   Returns the result code that caused the transformations to
   terminate, or TRNS_CONTINUE if the transformations finished
   due to "falling off the end" of the set of transformations. */
enum trns_result
trns_chain_execute (const struct trns_chain *chain, enum trns_result start,
                    struct ccase **c, casenumber case_nr)
{
  size_t i;

  assert (chain->finalized);
  for (i = start < 0 ? 0 : start; i < chain->trns_cnt; )
    {
      struct transformation *trns = &chain->trns[i];
      int retval = trns->execute (trns->aux, c, case_nr);
      if (retval == TRNS_CONTINUE)
        i++;
      else if (retval >= 0)
        i = retval + trns->idx_ofs;
      else
        return retval == TRNS_END_CASE ? i + 1 : retval;
    }

  return TRNS_CONTINUE;
}
