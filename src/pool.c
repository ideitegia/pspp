/* PSPP - computes sample statistics.
   Copyright (C) 2000 Free Software Foundation, Inc.
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

#if HAVE_CONFIG_H
#include <config.h>
#endif
#include "pool.h"
#include "error.h"
#include <stdlib.h>
#include "alloc.h"
#include "str.h"

/* Fast, low-overhead memory block suballocator. */
struct pool
  {
    struct pool *parent;	/* Pool of which this pool is a subpool. */
    struct pool_block *blocks;	/* Blocks owned by the pool. */
    struct pool_gizmo *gizmos;	/* Other stuff owned by the pool. */
  };

/* Pool block. */
struct pool_block 
  {
    struct pool_block *prev;
    struct pool_block *next;
    size_t ofs;
  };

/* Gizmo types. */
enum
  {
    POOL_GIZMO_MALLOC,
    POOL_GIZMO_FILE,
    POOL_GIZMO_SUBPOOL,
    POOL_GIZMO_REGISTERED,
  };

/* Pool routines can maintain objects (`gizmos') as well as doing
   suballocation.  
   This structure is used to keep track of them. */
struct pool_gizmo
  {
    struct pool_gizmo *prev;
    struct pool_gizmo *next;

    long serial;		/* Serial number. */
    int type;			/* Type of this gizmo. */

    /* Type-dependent info. */
    union
      {
	FILE *file;		/* POOL_GIZMO_FILE. */
	struct pool *subpool;	/* POOL_GIZMO_SUBPOOL. */

	/* POOL_GIZMO_REGISTERED. */
	struct
	  {
	    void (*free) (void *p);
	    void *p;
	  }
	registered;
      }
    p;
  };

/* Rounds X up to the next multiple of Y. */
#ifndef ROUND_UP
#define ROUND_UP(X, Y) 				\
	(((X) + ((Y) - 1)) / (Y) * (Y))
#endif

/* Types that provide typically useful alignment sizes. */
union align
  {
    void *op;
    void (*fp) (void);
    long l;
    double d;
  };

/* This should be the alignment size used by malloc().  The size of
   the union above is correct, if not optimal, in all known cases. */
#if defined (i386) || defined (__i386__)
#define ALIGN_SIZE 4		/* Save some extra memory. */
#else
#define ALIGN_SIZE sizeof (union align)
#endif

/* DISCRETE_BLOCKS may be declared as nonzero to prevent
   suballocation of blocks.  This is useful under memory
   debuggers like Checker or valgrind because it allows the
   source location of bugs to be more accurately pinpointed.

   On the other hand, if we're testing the library, then we want to
   test the library's real functionality, not its crippled, slow,
   simplified functionality. */
/*#define DISCRETE_BLOCKS 1*/

/* Enable debug code if appropriate. */
#if SELF_TEST
#endif

/* Size of each block allocated in the pool, in bytes.
   Should be at least 1k. */
#ifndef BLOCK_SIZE
#define BLOCK_SIZE 1024
#endif

/* Maximum size of a suballocated block.  Larger blocks are allocated
   directly with malloc() to avoid memory wastage at the end of a
   suballocation block. */
#ifndef MAX_SUBALLOC
#define MAX_SUBALLOC 64
#endif

/* Sizes of some structures with alignment padding included. */
#define POOL_BLOCK_SIZE ROUND_UP (sizeof (struct pool_block), ALIGN_SIZE)
#define POOL_GIZMO_SIZE ROUND_UP (sizeof (struct pool_gizmo), ALIGN_SIZE)
#define POOL_SIZE ROUND_UP (sizeof (struct pool), ALIGN_SIZE)

/* Serial number used to keep track of gizmos for mark/release. */
static long serial = 0;

/* Prototypes. */
static void add_gizmo (struct pool *, struct pool_gizmo *);
static void free_gizmo (struct pool_gizmo *);
static void free_all_gizmos (struct pool *pool);
static void delete_gizmo (struct pool *, struct pool_gizmo *);

#if !PSPP
static void *xmalloc (size_t);
static void *xrealloc (void *, size_t);
#endif

/* General routines. */

/* Creates and returns a new memory pool, which allows malloc()'d
   blocks to be suballocated in a time- and space-efficient manner.
   The entire contents of the memory pool are freed at once.

   In addition, other objects can be associated with a memory pool.
   These are released when the pool is destroyed. */
struct pool *
pool_create (void)
{
  struct pool_block *block;
  struct pool *pool;

  block = xmalloc (BLOCK_SIZE);
  block->prev = block->next = block;
  block->ofs = POOL_BLOCK_SIZE + POOL_SIZE;
  
  pool = (struct pool *) (((char *) block) + POOL_BLOCK_SIZE);
  pool->parent = NULL;
  pool->blocks = block;
  pool->gizmos = NULL;

  return pool;
}

/* Destroy the specified pool, including all subpools. */
void
pool_destroy (struct pool *pool)
{
  if (pool == NULL)
    return;

  /* Remove this pool from its parent's list of gizmos. */
  if (pool->parent) 
    delete_gizmo (pool->parent,
		  (void *) (((char *) pool) + POOL_SIZE + POOL_BLOCK_SIZE));

  free_all_gizmos (pool);

  /* Free all the memory. */
  {
    struct pool_block *cur, *next;

    pool->blocks->prev->next = NULL;
    for (cur = pool->blocks; cur; cur = next)
      {
	next = cur->next;
	free (cur);
      }
  }
}

/* Release all the memory and gizmos in POOL.
   Blocks are not given back with free() but kept for later
   allocations.  To give back memory, use a subpool instead. */ 
void
pool_clear (struct pool *pool) 
{
  free_all_gizmos (pool);

  /* Zero out block sizes. */
  {
    struct pool_block *cur;
    
    cur = pool->blocks;
    do
      {
        cur->ofs = POOL_BLOCK_SIZE;
        if ((char *) cur + POOL_BLOCK_SIZE == (char *) pool)
          cur->ofs += POOL_SIZE;
        cur = cur->next;
      }
    while (cur != pool->blocks);
  }
}

/* Suballocation routines. */

/* Allocates a memory region AMT bytes in size from POOL and returns a
   pointer to the region's start. */
void *
pool_alloc (struct pool *pool, size_t amt)
{
  assert (pool != NULL);
  
#ifndef DISCRETE_BLOCKS
  if (amt <= MAX_SUBALLOC)
    {
      /* If there is space in this block, take it. */
      struct pool_block *b = pool->blocks;
      b->ofs = ROUND_UP (b->ofs, ALIGN_SIZE);
      if (b->ofs + amt <= BLOCK_SIZE)
	{
	  void *const p = ((char *) b) + b->ofs;
	  b->ofs += amt;
	  return p;
	}

      /* No space in this block, so we must make other
         arrangements. */
      if (b->next->ofs == 0) 
        {
          /* The next block is empty.  Use it. */
          b = b->next;
          b->ofs = POOL_BLOCK_SIZE;
          if ((char *) b + POOL_BLOCK_SIZE == (char *) pool)
            b->ofs += POOL_SIZE;
        }
      else 
        {
          /* Create a new block at the start of the list. */
          b = xmalloc (BLOCK_SIZE);
          b->next = pool->blocks;
          b->prev = pool->blocks->prev;
          b->ofs = POOL_BLOCK_SIZE;
          pool->blocks->prev->next = b;
          pool->blocks->prev = b;
        }
      pool->blocks = b;

      /* Allocate space from B. */
      b->ofs += amt;
      return ((char *) b) + b->ofs - amt;
    }
  else
#endif
    return pool_malloc (pool, amt);
}

/* Duplicates STRING, which has LENGTH characters, within POOL,
   and returns a pointer to the duplicate.  LENGTH should not
   include the null terminator, which is always added to the
   duplicate.  For use only with strings, because the returned
   pointere may not be aligned properly for other types. */
char *
pool_strndup (struct pool *pool, const char *string, size_t length)
{
  size_t size;
  char *copy;

  assert (pool && string);
  size = length + 1;

  /* Note that strings need not be aligned on any boundary. */
#ifndef DISCRETE_BLOCKS
  {
    struct pool_block *const b = pool->blocks;

    if (b->ofs + size <= BLOCK_SIZE)
      {
        copy = ((char *) b) + b->ofs;
        b->ofs += size;
      }
    else
      copy = pool_alloc (pool, size);
  }
#else
  copy = pool_alloc (pool, size);
#endif

  memcpy (copy, string, length);
  copy[length] = '\0';
  return copy;
}

/* Duplicates null-terminated STRING, within POOL, and returns a
   pointer to the duplicate.  For use only with strings, because
   the returned pointere may not be aligned properly for other
   types. */
char *
pool_strdup (struct pool *pool, const char *string) 
{
  return pool_strndup (pool, string, strlen (string));
}

/* Standard allocation routines. */

/* Allocates AMT bytes using malloc(), to be managed by POOL, and
   returns a pointer to the beginning of the block.
   If POOL is a null pointer, then allocates a normal memory block
   with malloc().  */
void *
pool_malloc (struct pool *pool, size_t amt)
{
  if (pool != NULL)
    {
      if (amt != 0)
	{
	  struct pool_gizmo *g = xmalloc (amt + POOL_GIZMO_SIZE);
	  g->type = POOL_GIZMO_MALLOC;
	  add_gizmo (pool, g);

	  return ((char *) g) + POOL_GIZMO_SIZE;
	}
      else
	return NULL;
    }
  else
    return xmalloc (amt);
}

/* Changes the allocation size of the specified memory block P managed
   by POOL to AMT bytes and returns a pointer to the beginning of the
   block.
   If POOL is a null pointer, then the block is reallocated in the
   usual way with realloc(). */
void *
pool_realloc (struct pool *pool, void *p, size_t amt)
{
  if (pool != NULL)
    {
      if (p != NULL)
	{
	  if (amt != 0)
	    {
	      struct pool_gizmo *g;

	      g = xrealloc (((char *) p) - POOL_GIZMO_SIZE,
			    amt + POOL_GIZMO_SIZE);
	      if (g->next)
		g->next->prev = g;
	      if (g->prev)
		g->prev->next = g;
	      else
		pool->gizmos = g;

	      return ((char *) g) + POOL_GIZMO_SIZE;
	    }
	  else
	    {
	      pool_free (pool, p);
	      return NULL;
	    }
	}
      else
	return pool_malloc (pool, amt);
    }
  else
    return xrealloc (p, amt);
}

/* Frees block P managed by POOL.
   If POOL is a null pointer, then the block is freed as usual with
   free(). */
void
pool_free (struct pool *pool, void *p)
{
  if (pool != NULL && p != NULL)
    {
      struct pool_gizmo *g = (void *) (((char *) p) - POOL_GIZMO_SIZE);
      delete_gizmo (pool, g);
      free (g);
    }
  else
    free (p);
}

/* Gizmo allocations. */

/* Creates and returns a pool as a subpool of POOL.
   The subpool will be destroyed automatically when POOL is destroyed.
   It may also be destroyed explicitly in advance. */
struct pool *
pool_create_subpool (struct pool *pool)
{
  struct pool *subpool;
  struct pool_gizmo *g;

  assert (pool != NULL);
  subpool = pool_create ();
  subpool->parent = pool;

  g = (void *) (((char *) subpool) + subpool->blocks->ofs);
  subpool->blocks->ofs += POOL_GIZMO_SIZE;
  
  g->type = POOL_GIZMO_SUBPOOL;
  g->p.subpool = subpool;

  add_gizmo (pool, g);

  return subpool;
}

/* Opens file FILENAME with mode MODE and returns a handle to it
   if successful or a null pointer if not.
   The file will be closed automatically when POOL is destroyed, or it
   may be closed explicitly in advance using pool_fclose. */
FILE *
pool_fopen (struct pool *pool, const char *filename, const char *mode)
{
  FILE *f;

  assert (pool && filename && mode);
  f = fopen (filename, mode);
  if (f == NULL)
    return NULL;

  {
    struct pool_gizmo *g = pool_alloc (pool, sizeof *g);
    g->type = POOL_GIZMO_FILE;
    g->p.file = f;
    add_gizmo (pool, g);
  }

  return f;
}

/* Closes file FILE managed by POOL. */
int
pool_fclose (struct pool *pool, FILE *file)
{
  assert (pool && file);
  if (fclose (file) == EOF)
    return EOF;
  
  {
    struct pool_gizmo *g;

    for (g = pool->gizmos; g; g = g->next)
      if (g->type == POOL_GIZMO_FILE && g->p.file == file)
	{
	  delete_gizmo (pool, g);
	  break;
	}
  }
  
  return 0;
}

/* Registers FREE to be called with argument P.
   P should be unique among those registered in POOL so that it can be
   uniquely identified by pool_unregister().
   If not unregistered, FREE will be called with argument P when POOL
   is destroyed. */
void
pool_register (struct pool *pool, void (*free) (void *), void *p)
{
  assert (pool && free && p);

  {
    struct pool_gizmo *g = pool_alloc (pool, sizeof *g);
    g->type = POOL_GIZMO_REGISTERED;
    g->p.registered.free = free;
    g->p.registered.p = p;
    add_gizmo (pool, g);
  }
}

/* Unregisters previously registered P from POOL.
   Returns nonzero only if P was found to be registered in POOL. */
int
pool_unregister (struct pool *pool, void *p)
{
  assert (pool && p);
  
  {
    struct pool_gizmo *g;

    for (g = pool->gizmos; g; g = g->next)
      if (g->type == POOL_GIZMO_REGISTERED && g->p.registered.p == p)
	{
	  delete_gizmo (pool, g);
	  return 1;
	}
  }
  
  return 0;
}

/* Partial freeing. */

/* Notes the state of POOL into MARK so that it may be restored
   by a call to pool_release(). */
void
pool_mark (struct pool *pool, struct pool_mark *mark)
{
  assert (pool && mark);

  mark->block = pool->blocks;
  mark->ofs = pool->blocks->ofs;

  mark->serial = serial;
}

/* Restores to POOL the state recorded in MARK.
   Emptied blocks are not given back with free() but kept for
   later allocations.  To get that behavior, use a subpool
   instead. */ 
void
pool_release (struct pool *pool, const struct pool_mark *mark)
{
  assert (pool && mark);
  
  {
    struct pool_gizmo *cur, *next;

    for (cur = pool->gizmos; cur && cur->serial >= mark->serial; cur = next)
      {
	next = cur->next;
	free_gizmo (cur);
      }

    if (cur != NULL)
      {
	cur->prev = NULL;
	pool->gizmos = cur;
      }
    else
      pool->gizmos = NULL;
  }
  
  {
    struct pool_block *cur;

    for (cur = pool->blocks; cur != mark->block; cur = cur->next) 
      {
        cur->ofs = POOL_BLOCK_SIZE;
        if ((char *) cur + POOL_BLOCK_SIZE == (char *) pool)
          cur->ofs += POOL_SIZE; 
      }
    pool->blocks = mark->block;
    pool->blocks->ofs = mark->ofs;
  }
}

/* Private functions. */

/* Adds GIZMO at the beginning of POOL's gizmo list. */
static void
add_gizmo (struct pool *pool, struct pool_gizmo *gizmo)
{
  assert (pool && gizmo);
  
  gizmo->next = pool->gizmos;
  gizmo->prev = NULL;
  if (pool->gizmos)
    pool->gizmos->prev = gizmo;
  pool->gizmos = gizmo;

  gizmo->serial = serial++;
}
 
/* Removes GIZMO from POOL's gizmo list. */
static void
delete_gizmo (struct pool *pool, struct pool_gizmo *gizmo)
{
  assert (pool && gizmo);
  
  if (gizmo->prev)
    gizmo->prev->next = gizmo->next;
  else
    pool->gizmos = gizmo->next;
  if (gizmo->next)
    gizmo->next->prev = gizmo->prev;
}

/* Frees any of GIZMO's internal state.
   GIZMO's data must not be referenced after calling this function. */
static void
free_gizmo (struct pool_gizmo *gizmo)
{
  assert (gizmo != NULL);
  
  switch (gizmo->type)
    {
    case POOL_GIZMO_MALLOC:
      free (gizmo);
      break;
    case POOL_GIZMO_FILE:
      fclose (gizmo->p.file);	/* Ignore errors. */
      break;
    case POOL_GIZMO_SUBPOOL:
      gizmo->p.subpool->parent = NULL;
      pool_destroy (gizmo->p.subpool);
      break;
    case POOL_GIZMO_REGISTERED:
      gizmo->p.registered.free (gizmo->p.registered.p);
      break;
    default:
      assert (0);
    }
}

/* Free all the gizmos in POOL. */
static void
free_all_gizmos (struct pool *pool) 
{
  struct pool_gizmo *cur, *next;

  for (cur = pool->gizmos; cur; cur = next)
    {
      next = cur->next;
      free_gizmo (cur);
    }
  pool->gizmos=NULL;
}

/* Memory allocation. */

#if !PSPP
/* Allocates SIZE bytes of space using malloc().  Aborts if out of
   memory. */
static void *
xmalloc (size_t size)
{
  void *vp;
  if (size == 0)
    return NULL;
  vp = malloc (size);
  assert (vp != NULL);
  if (vp == NULL)
    abort ();
  return vp;
}

/* Reallocates P to be SIZE bytes long using realloc().  Aborts if out
   of memory. */
static void *
xrealloc (void *p, size_t size)
{
  if (p == NULL)
    return xmalloc (size);
  if (size == 0)
    {
      free (p);
      return NULL;
    }
  p = realloc (p, size);
  if (p == NULL)
    abort ();
  return p;
}
#endif /* !PSPP */

/* Self-test routine. */

#if SELF_TEST
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N_ITERATIONS 8192
#define N_FILES 16

/* Self-test routine.
   This is not exhaustive, but it can be useful. */
int
main (int argc, char **argv)
{
  int seed;
  
  if (argc == 2)
    seed = atoi (argv[1]);
  else
    seed = time (0) * 257 % 32768;

  for (;;)
    {
      struct pool *pool;
      struct pool_mark m1, m2;
      FILE *files[N_FILES];
      int cur_file;
      long i;

      printf ("Random number seed: %d\n", seed);
      srand (seed++);

      printf ("Creating pool...\n");
      pool = pool_create ();

      printf ("Marking pool state...\n");
      pool_mark (pool, &m1);

      printf ("    Populating pool with random-sized small objects...\n");
      for (i = 0; i < N_ITERATIONS; i++)
	{
	  size_t size = rand () % MAX_SUBALLOC;
	  void *p = pool_alloc (pool, size);
	  memset (p, 0, size);
	}

      printf ("    Marking pool state...\n");
      pool_mark (pool, &m2);
      
      printf ("       Populating pool with random-sized small "
	      "and large objects...\n");
      for (i = 0; i < N_ITERATIONS; i++)
	{
	  size_t size = rand () % (2 * MAX_SUBALLOC);
	  void *p = pool_alloc (pool, size);
	  memset (p, 0, size);
	}

      printf ("    Releasing pool state...\n");
      pool_release (pool, &m2);

      printf ("    Populating pool with random objects and gizmos...\n");
      for (i = 0; i < N_FILES; i++)
	files[i] = NULL;
      cur_file = 0;
      for (i = 0; i < N_ITERATIONS; i++)
	{
	  int type = rand () % 32;

	  if (type == 0)
	    {
	      if (files[cur_file] != NULL
		  && EOF == pool_fclose (pool, files[cur_file]))
		printf ("error on fclose: %s\n", strerror (errno));

	      files[cur_file] = pool_fopen (pool, "/dev/null", "r");

	      if (++cur_file >= N_FILES)
		cur_file = 0;
	    }
	  else if (type == 1)
	    pool_create_subpool (pool);
	  else 
	    {
	      size_t size = rand () % (2 * MAX_SUBALLOC);
	      void *p = pool_alloc (pool, size);
	      memset (p, 0, size);
	    }
	}
      
      printf ("Releasing pool state...\n");
      pool_release (pool, &m1);

      printf ("Destroying pool...\n");
      pool_destroy (pool);

      putchar ('\n');
    }
}

#endif /* SELF_TEST */

/* 
   Local variables:
   compile-command: "gcc -DSELF_TEST=1 -W -Wall -I. -o pool_test pool.c"
   End:
*/
