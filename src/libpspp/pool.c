/* PSPP - a program for statistical analysis.
   Copyright (C) 2000, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "libpspp/pool.h"

#include <stdint.h>
#include <stdlib.h>

#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/temp-file.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

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
    POOL_GIZMO_TEMP_FILE,
    POOL_GIZMO_SUBPOOL,
    POOL_GIZMO_REGISTERED,
  };

/* Pool routines can maintain objects (`gizmos') as well as doing
   suballocation.
   This structure is used to keep track of them. */
struct pool_gizmo
  {
    struct pool *pool;
    struct pool_gizmo *prev;
    struct pool_gizmo *next;

    long serial;		/* Serial number. */
    int type;			/* Type of this gizmo. */

    /* Type-dependent info. */
    union
      {
	FILE *file;		/* POOL_GIZMO_FILE, POOL_GIZMO_TEMP_FILE. */
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

    /* glibc jmp_buf on ia64 requires 16-byte alignment.  This ensures it. */
    size_t s[2];
  };

/* This should be the alignment size used by malloc().  The size of
   the union above is correct, if not optimal, in all known cases.

   This is normally 8 bytes for 32-bit architectures and 16 bytes for 64-bit
   architectures. */
#define ALIGN_SIZE sizeof (union align)

/* DISCRETE_BLOCKS may be declared as nonzero to prevent
   suballocation of blocks.  This is useful under memory
   debuggers like valgrind because it allows the source location
   of bugs to be more accurately pinpointed.

   On the other hand, if we're testing the library, then we want to
   test the library's real functionality, not its crippled, slow,
   simplified functionality. */
/*#define DISCRETE_BLOCKS 1*/

/* Size of each block allocated in the pool, in bytes.
   Should be at least 1k. */
#ifndef BLOCK_SIZE
#define BLOCK_SIZE 1024
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
static void check_gizmo (struct pool *, struct pool_gizmo *);

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

/* Creates a pool, allocates a block STRUCT_SIZE bytes in
   length from it, stores the pool's address at offset
   POOL_MEMBER_OFFSET within the block, and returns the allocated
   block.

   Meant for use indirectly via pool_create_container(). */
void *
pool_create_at_offset (size_t struct_size, size_t pool_member_offset)
{
  struct pool *pool;
  char *struct_;

  assert (struct_size >= sizeof pool);
  assert (pool_member_offset <= struct_size - sizeof pool);

  pool = pool_create ();
  struct_ = pool_alloc (pool, struct_size);
  *(struct pool **) (struct_ + pool_member_offset) = pool;
  return struct_;
}

/* Destroy the specified pool, including all subpools. */
void
pool_destroy (struct pool *pool)
{
  if (pool == NULL)
    return;

  /* Remove this pool from its parent's list of gizmos. */
  if (pool->parent)
    delete_gizmo (pool->parent, (void *) (((char *) pool) + POOL_SIZE));

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
          {
            cur->ofs += POOL_SIZE;
            if (pool->parent != NULL)
              cur->ofs += POOL_GIZMO_SIZE;
          }
        cur = cur->next;
      }
    while (cur != pool->blocks);
  }
}

/* Suballocation routines. */

/* Allocates a memory region AMT bytes in size from POOL and returns a
   pointer to the region's start.
   The region is properly aligned for storing any object. */
void *
pool_alloc (struct pool *pool, size_t amt)
{
  assert (pool != NULL);

  if (amt == 0)
    return NULL;

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

/* Allocates a memory region AMT bytes in size from POOL and
   returns a pointer to the region's start.  The region is not
   necessarily aligned, so it is most suitable for storing
   strings. */
void *
pool_alloc_unaligned (struct pool *pool, size_t amt)
{
  if (pool == NULL)
    return xmalloc (amt);

#ifndef DISCRETE_BLOCKS
  /* Strings need not be aligned on any boundary, but some
     operations may be more efficient when they are.  However,
     that's only going to help with reasonably long strings. */
  if (amt < ALIGN_SIZE)
    {
      if (amt == 0)
        return NULL;
      else
        {
          struct pool_block *const b = pool->blocks;

          if (b->ofs + amt <= BLOCK_SIZE)
            {
              void *p = ((char *) b) + b->ofs;
              b->ofs += amt;
              return p;
            }
        }
    }
#endif

  return pool_alloc (pool, amt);
}

/* Allocates a memory region N * S bytes in size from POOL and
   returns a pointer to the region's start.
   N must be nonnegative, S must be positive.
   Terminates the program if the memory cannot be obtained,
   including the case where N * S overflows the range of size_t. */
void *
pool_nalloc (struct pool *pool, size_t n, size_t s)
{
  if (xalloc_oversized (n, s))
    xalloc_die ();
  return pool_alloc (pool, n * s);
}

/* Allocates SIZE bytes in POOL, copies BUFFER into it, and
   returns the new copy. */
void *
pool_clone (struct pool *pool, const void *buffer, size_t size)
{
  void *block = pool_alloc (pool, size);
  memcpy (block, buffer, size);
  return block;
}

/* Allocates SIZE bytes of unaligned data in POOL, copies BUFFER
   into it, and returns the new copy. */
void *
pool_clone_unaligned (struct pool *pool, const void *buffer, size_t size)
{
  void *block = pool_alloc_unaligned (pool, size);
  memcpy (block, buffer, size);
  return block;
}

/* Duplicates null-terminated STRING, within POOL, and returns a
   pointer to the duplicate.  For use only with strings, because
   the returned pointere may not be aligned properly for other
   types. */
char *
pool_strdup (struct pool *pool, const char *string)
{
  return pool_clone_unaligned (pool, string, strlen (string) + 1);
}

/* Duplicates the SIZE bytes of STRING, plus a trailing 0 byte,
   and returns a pointer to the duplicate.  For use only with
   strings, because the returned pointere may not be aligned
   properly for other types. */
char *
pool_strdup0 (struct pool *pool, const char *string, size_t size)
{
  char *new_string = pool_alloc_unaligned (pool, size + 1);
  memcpy (new_string, string, size);
  new_string[size] = '\0';
  return new_string;
}

/* Formats FORMAT with the given ARGS in memory allocated from
   POOL and returns the formatted string. */
char *
pool_vasprintf (struct pool *pool, const char *format, va_list args_)
{
  struct pool_block *b;
  va_list args;
  int needed, avail;
  char *s;

  va_copy (args, args_);
  b = pool->blocks;
  avail = BLOCK_SIZE - b->ofs;
  s = ((char *) b) + b->ofs;
  needed = vsnprintf (s, avail, format, args);
  va_end (args);

  if (needed >= 0)
    {
      if (needed < avail)
        {
          /* Success.  Reserve the space that was actually used. */
          b->ofs += needed + 1;
        }
      else
        {
          /* Failure, but now we know how much space is needed.
             Allocate that much and reformat. */
          s = pool_alloc (pool, needed + 1);

          va_copy (args, args_);
          vsprintf (s, format, args);
          va_end (args);
        }
    }
  else
    {
      /* Some old libc's returned -1 when the destination string
         was too short.  This should be uncommon these days and
         it's a rare case anyhow.  Use the easiest solution: punt
         to dynamic allocation. */
      va_copy (args, args_);
      s = xvasprintf (format, args);
      va_end (args);

      pool_register (pool, free, s);
    }

  return s;
}

/* Formats FORMAT in memory allocated from POOL
   and returns the formatted string. */
char *
pool_asprintf (struct pool *pool, const char *format, ...)
{
  va_list args;
  char *string;

  va_start (args, format);
  string = pool_vasprintf (pool, format, args);
  va_end (args);

  return string;
}

/* Standard allocation routines. */

/* Allocates AMT bytes using malloc(), to be managed by POOL, and
   returns a pointer to the beginning of the block.
   If POOL is a null pointer, then allocates a normal memory block
   with xmalloc().  */
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

/* Allocates and returns N elements of S bytes each, to be
   managed by POOL.
   If POOL is a null pointer, then allocates a normal memory block
   with malloc().
   N must be nonnegative, S must be positive.
   Terminates the program if the memory cannot be obtained,
   including the case where N * S overflows the range of size_t. */
void *
pool_nmalloc (struct pool *pool, size_t n, size_t s)
{
  if (xalloc_oversized (n, s))
    xalloc_die ();
  return pool_malloc (pool, n * s);
}

/* Allocates AMT bytes using malloc(), to be managed by POOL,
   zeros the block, and returns a pointer to the beginning of the
   block.
   If POOL is a null pointer, then allocates a normal memory block
   with xmalloc().  */
void *
pool_zalloc (struct pool *pool, size_t amt)
{
  void *p = pool_malloc (pool, amt);
  memset (p, 0, amt);
  return p;
}

/* Allocates and returns N elements of S bytes each, to be
   managed by POOL, and zeros the block.
   If POOL is a null pointer, then allocates a normal memory block
   with malloc().
   N must be nonnegative, S must be positive.
   Terminates the program if the memory cannot be obtained,
   including the case where N * S overflows the range of size_t. */
void *
pool_calloc (struct pool *pool, size_t n, size_t s)
{
  void *p = pool_nmalloc (pool, n, s);
  memset (p, 0, n * s);
  return p;
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
	      struct pool_gizmo *g = (void *) (((char *) p) - POOL_GIZMO_SIZE);
              check_gizmo (pool, g);

	      g = xrealloc (g, amt + POOL_GIZMO_SIZE);
	      if (g->next)
		g->next->prev = g;
	      if (g->prev)
		g->prev->next = g;
	      else
		pool->gizmos = g;
              check_gizmo (pool, g);

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

/* Changes the allocation size of the specified memory block P
   managed by POOL to N * S bytes and returns a pointer to the
   beginning of the block.
   N must be nonnegative, S must be positive.
   If POOL is a null pointer, then the block is reallocated in
   the usual way with xrealloc().
   Terminates the program if the memory cannot be obtained,
   including the case where N * S overflows the range of size_t. */
void *
pool_nrealloc (struct pool *pool, void *p, size_t n, size_t s)
{
  if (xalloc_oversized (n, s))
    xalloc_die ();
  return pool_realloc (pool, p, n * s);
}

/* If P is null, allocate a block of at least *PN such objects;
   otherwise, reallocate P so that it contains more than *PN
   objects each of S bytes.  *PN must be nonzero unless P is
   null, and S must be nonzero.  Set *PN to the new number of
   objects, and return the pointer to the new block.  *PN is
   never set to zero, and the returned pointer is never null.

   The block returned is managed by POOL.  If POOL is a null
   pointer, then the block is reallocated in the usual way with
   x2nrealloc().

   Terminates the program if the memory cannot be obtained,
   including the case where the memory required overflows the
   range of size_t.

   Repeated reallocations are guaranteed to make progress, either by
   allocating an initial block with a nonzero size, or by allocating a
   larger block.

   In the following implementation, nonzero sizes are doubled so that
   repeated reallocations have O(N log N) overall cost rather than
   O(N**2) cost, but the specification for this function does not
   guarantee that sizes are doubled.

   Here is an example of use:

     int *p = NULL;
     struct pool *pool;
     size_t used = 0;
     size_t allocated = 0;

     void
     append_int (int value)
       {
	 if (used == allocated)
	   p = pool_2nrealloc (pool, p, &allocated, sizeof *p);
	 p[used++] = value;
       }

   This causes x2nrealloc to allocate a block of some nonzero size the
   first time it is called.

   To have finer-grained control over the initial size, set *PN to a
   nonzero value before calling this function with P == NULL.  For
   example:

     int *p = NULL;
     struct pool *pool;
     size_t used = 0;
     size_t allocated = 0;
     size_t allocated1 = 1000;

     void
     append_int (int value)
       {
	 if (used == allocated)
	   {
	     p = pool_2nrealloc (pool, p, &allocated1, sizeof *p);
	     allocated = allocated1;
	   }
	 p[used++] = value;
       }

   This function implementation is from gnulib. */
void *
pool_2nrealloc (struct pool *pool, void *p, size_t *pn, size_t s)
{
  size_t n = *pn;

  if (p == NULL)
    {
      if (n == 0)
	{
	  /* The approximate size to use for initial small allocation
	     requests, when the invoking code specifies an old size of
	     zero.  64 bytes is the largest "small" request for the
	     GNU C library malloc.  */
	  enum { DEFAULT_MXFAST = 64 };

	  n = DEFAULT_MXFAST / s;
	  n += !n;
	}
    }
  else
    {
      if (SIZE_MAX / 2 / s < n)
	xalloc_die ();
      n *= 2;
    }

  *pn = n;
  return pool_realloc (pool, p, n * s);
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
      check_gizmo (pool, g);
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

  g = (void *) (((char *) subpool->blocks) + subpool->blocks->ofs);
  subpool->blocks->ofs += POOL_GIZMO_SIZE;

  g->type = POOL_GIZMO_SUBPOOL;
  g->p.subpool = subpool;

  add_gizmo (pool, g);

  return subpool;
}

/* Makes SUBPOOL a subpool of POOL.
   SUBPOOL must not already have a parent pool.
   The subpool will be destroyed automatically when POOL is destroyed.
   It may also be destroyed explicitly in advance. */
void
pool_add_subpool (struct pool *pool, struct pool *subpool)
{
  struct pool_gizmo *g;

  assert (pool != NULL);
  assert (subpool != NULL);
  assert (subpool->parent == NULL);

  g = pool_alloc (subpool, sizeof *g);
  g->type = POOL_GIZMO_SUBPOOL;
  g->p.subpool = subpool;
  add_gizmo (pool, g);

  subpool->parent = pool;
}

/* Opens file FILE_NAME with mode MODE and returns a handle to it
   if successful or a null pointer if not.
   The file will be closed automatically when POOL is destroyed, or it
   may be closed explicitly in advance using pool_fclose(), or
   detached from the pool with pool_detach_file(). */
FILE *
pool_fopen (struct pool *pool, const char *file_name, const char *mode)
{
  FILE *f;

  assert (pool && file_name && mode);
  f = fopen (file_name, mode);
  if (f != NULL)
    pool_attach_file (pool, f);

  return f;
}

/* Closes file FILE managed by POOL.
   Returns 0 if successful, EOF if an I/O error occurred. */
int
pool_fclose (struct pool *pool, FILE *file)
{
  assert (pool && file);
  pool_detach_file (pool, file);
  return fclose (file);
}

/* Attaches FILE to POOL.
   The file will be closed automatically when POOL is destroyed, or it
   may be closed explicitly in advance using pool_fclose(), or
   detached from the pool with pool_detach_file(). */
void
pool_attach_file (struct pool *pool, FILE *file)
{
  struct pool_gizmo *g = pool_alloc (pool, sizeof *g);
  g->type = POOL_GIZMO_FILE;
  g->p.file = file;
  add_gizmo (pool, g);
}

/* Detaches FILE from POOL. */
void
pool_detach_file (struct pool *pool, FILE *file)
{
  struct pool_gizmo *g;

  for (g = pool->gizmos; g; g = g->next)
    if (g->type == POOL_GIZMO_FILE && g->p.file == file)
      {
        delete_gizmo (pool, g);
        return;
      }
}

/* Creates a temporary file with create_temp_file() and returns a handle to it
   if successful or a null pointer if not.
   The file will be closed automatically when POOL is destroyed, or it
   may be closed explicitly in advance using pool_fclose_temp_file(), or
   detached from the pool with pool_detach_temp_file(). */
FILE *
pool_create_temp_file (struct pool *pool)
{
  FILE *file = create_temp_file ();
  if (file != NULL)
    pool_attach_temp_file (pool, file);
  return file;
}

/* Closes file FILE managed by POOL.
   FILE must have been opened with create_temp_file(). */
void
pool_fclose_temp_file (struct pool *pool, FILE *file)
{
  assert (pool && file);
  pool_detach_temp_file (pool, file);
  close_temp_file (file);
}

/* Attaches FILE, which must have been opened with create_temp_file(), to POOL.
   The file will be closed automatically when POOL is destroyed, or it
   may be closed explicitly in advance using pool_fclose_temp_file(), or
   detached from the pool with pool_detach_temp_file(). */
void
pool_attach_temp_file (struct pool *pool, FILE *file)
{
  struct pool_gizmo *g = pool_alloc (pool, sizeof *g);
  g->type = POOL_GIZMO_TEMP_FILE;
  g->p.file = file;
  add_gizmo (pool, g);
}

/* Detaches FILE that was opened with create_temp_file() from POOL. */
void
pool_detach_temp_file (struct pool *pool, FILE *file)
{
  struct pool_gizmo *g;

  for (g = pool->gizmos; g; g = g->next)
    if (g->type == POOL_GIZMO_TEMP_FILE && g->p.file == file)
      {
        delete_gizmo (pool, g);
        return;
      }
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
   Returns true only if P was found to be registered in POOL. */
bool
pool_unregister (struct pool *pool, void *p)
{
  assert (pool && p);

  {
    struct pool_gizmo *g;

    for (g = pool->gizmos; g; g = g->next)
      if (g->type == POOL_GIZMO_REGISTERED && g->p.registered.p == p)
	{
	  delete_gizmo (pool, g);
	  return true;
	}
  }

  return false;
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
          {
            cur->ofs += POOL_SIZE;
            if (pool->parent != NULL)
              cur->ofs += POOL_GIZMO_SIZE;
          }
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

  gizmo->pool = pool;
  gizmo->next = pool->gizmos;
  gizmo->prev = NULL;
  if (pool->gizmos)
    pool->gizmos->prev = gizmo;
  pool->gizmos = gizmo;

  gizmo->serial = serial++;

  check_gizmo (pool, gizmo);
}

/* Removes GIZMO from POOL's gizmo list. */
static void
delete_gizmo (struct pool *pool, struct pool_gizmo *gizmo)
{
  assert (pool && gizmo);

  check_gizmo (pool, gizmo);

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
    case POOL_GIZMO_TEMP_FILE:
      close_temp_file (gizmo->p.file); /* Ignore errors. */
      break;
    case POOL_GIZMO_SUBPOOL:
      gizmo->p.subpool->parent = NULL;
      pool_destroy (gizmo->p.subpool);
      break;
    case POOL_GIZMO_REGISTERED:
      gizmo->p.registered.free (gizmo->p.registered.p);
      break;
    default:
      NOT_REACHED ();
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
  pool->gizmos = NULL;
}

static void
check_gizmo (struct pool *p, struct pool_gizmo *g)
{
  assert (g->pool == p);
  assert (g->next == NULL || g->next->prev == g);
  assert ((g->prev != NULL && g->prev->next == g)
          || (g->prev == NULL && p->gizmos == g));

}
