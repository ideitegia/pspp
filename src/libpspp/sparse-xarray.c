/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "libpspp/sparse-xarray.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/assertion.h"
#include "libpspp/ext-array.h"
#include "libpspp/misc.h"
#include "libpspp/range-set.h"
#include "libpspp/sparse-array.h"

#include "gl/md4.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

/* A sparse array of arrays of bytes. */
struct sparse_xarray
  {
    size_t n_bytes;                     /* Number of bytes per row. */
    uint8_t *default_row;               /* Defaults for unwritten rows. */
    unsigned long int max_memory_rows;  /* Max rows before dumping to disk. */
    struct sparse_array *memory;        /* Backing, if stored in memory. */
    struct ext_array *disk;             /* Backing, if stored on disk. */
    struct range_set *disk_rows;        /* Allocated rows, if on disk. */
  };

static bool UNUSED
range_is_valid (const struct sparse_xarray *sx, size_t ofs, size_t n)
{
  return n <= sx->n_bytes && ofs <= sx->n_bytes && ofs + n <= sx->n_bytes;
}

/* Creates and returns a new sparse array of arrays of bytes.
   Each row in the sparse array will consist of N_BYTES bytes.
   If fewer than MAX_MEMORY_ROWS rows are added to the array,
   then it will be kept in memory; if more than that are added,
   then it will be stored on disk. */
struct sparse_xarray *
sparse_xarray_create (size_t n_bytes, size_t max_memory_rows)
{
  struct sparse_xarray *sx = xmalloc (sizeof *sx);
  sx->n_bytes = n_bytes;
  sx->default_row = xzalloc (n_bytes);
  sx->max_memory_rows = max_memory_rows;
  sx->memory = sparse_array_create (sizeof (uint8_t *));
  sx->disk = NULL;
  sx->disk_rows = NULL;
  return sx;
}

/* Creates and returns a new sparse array of rows that contains
   the same data as OLD.  Returns a null pointer if cloning
   fails. */
struct sparse_xarray *
sparse_xarray_clone (const struct sparse_xarray *old)
{
  struct sparse_xarray *new = xmalloc (sizeof *new);

  new->n_bytes = old->n_bytes;

  new->default_row = xmemdup (old->default_row, old->n_bytes);

  new->max_memory_rows = old->max_memory_rows;

  if (old->memory != NULL)
    {
      unsigned long int idx;
      uint8_t **old_row;

      new->memory = sparse_array_create (sizeof (uint8_t *));
      for (old_row = sparse_array_first (old->memory, &idx); old_row != NULL;
           old_row = sparse_array_next (old->memory, idx, &idx))
        {
          uint8_t **new_row = sparse_array_insert (new->memory, idx);
          *new_row = xmemdup (*old_row, new->n_bytes);
        }
    }
  else
    new->memory = NULL;

  if (old->disk != NULL)
    {
      const struct range_set_node *node;
      void *tmp = xmalloc (old->n_bytes);

      new->disk = ext_array_create ();
      new->disk_rows = range_set_clone (old->disk_rows, NULL);
      RANGE_SET_FOR_EACH (node, old->disk_rows)
        {
          unsigned long int start = range_set_node_get_start (node);
          unsigned long int end = range_set_node_get_end (node);
          unsigned long int idx;

          for (idx = start; idx < end; idx++)
            {
              off_t offset = (off_t) idx * old->n_bytes;
              if (!ext_array_read (old->disk, offset, old->n_bytes, tmp)
                  || !ext_array_write (new->disk, offset, old->n_bytes, tmp))
                {
                  free (tmp);
                  sparse_xarray_destroy (new);
                  return NULL;
                }
            }
        }
      free (tmp);
    }
  else
    {
      new->disk = NULL;
      new->disk_rows = NULL;
    }

  return new;
}

static void
free_memory_rows (struct sparse_xarray *sx)
{
  if (sx->memory != NULL)
    {
      unsigned long int idx;
      uint8_t **row;
      for (row = sparse_array_first (sx->memory, &idx); row != NULL;
           row = sparse_array_next (sx->memory, idx, &idx))
        free (*row);
      sparse_array_destroy (sx->memory);
      sx->memory = NULL;
    }
}

/* Destroys sparse array of rows SX. */
void
sparse_xarray_destroy (struct sparse_xarray *sx)
{
  if (sx != NULL)
    {
      free (sx->default_row);
      free_memory_rows (sx);
      ext_array_destroy (sx->disk);
      range_set_destroy (sx->disk_rows);
      free (sx);
    }
}

/* Returns the number of bytes in each row in SX. */
size_t
sparse_xarray_get_n_columns (const struct sparse_xarray *sx)
{
  return sx->n_bytes;
}

/* Returns the number of rows in SX. */
size_t
sparse_xarray_get_n_rows (const struct sparse_xarray *sx)
{
  if (sx->memory)
    {
      unsigned long int idx;
      return sparse_array_last (sx->memory, &idx) != NULL ? idx + 1 : 0;
    }
  else
    {
      const struct range_set_node *last = range_set_last (sx->disk_rows);
      return last != NULL ? range_set_node_get_end (last) : 0;
    }
}

/* Dumps the rows in SX, which must currently be stored in
   memory, to disk.  Returns true if successful, false on I/O
   error. */
static bool
dump_sparse_xarray_to_disk (struct sparse_xarray *sx)
{
  unsigned long int idx;
  uint8_t **row;

  assert (sx->memory != NULL);
  assert (sx->disk == NULL);

  sx->disk = ext_array_create ();
  sx->disk_rows = range_set_create ();

  for (row = sparse_array_first (sx->memory, &idx); row != NULL;
       row = sparse_array_next (sx->memory, idx, &idx))
    {
      if (!ext_array_write (sx->disk, (off_t) idx * sx->n_bytes, sx->n_bytes,
                          *row))
        {
          ext_array_destroy (sx->disk);
          sx->disk = NULL;
          range_set_destroy (sx->disk_rows);
          sx->disk_rows = NULL;
          return false;
        }
      range_set_set1 (sx->disk_rows, idx, 1);
    }
  free_memory_rows (sx);
  return true;
}

/* Returns true if any data has ever been written to ROW in SX,
   false otherwise. */
bool
sparse_xarray_contains_row (const struct sparse_xarray *sx,
                            unsigned long int row)
{
  return (sx->memory != NULL
          ? sparse_array_get (sx->memory, row) != NULL
          : range_set_contains (sx->disk_rows, row));
}

/* Reads columns COLUMNS...(COLUMNS + VALUE_CNT), exclusive, in
   the given ROW in SX, into the VALUE_CNT values in VALUES.
   Returns true if successful, false on I/O error. */
bool
sparse_xarray_read (const struct sparse_xarray *sx, unsigned long int row,
                    size_t start, size_t n, void *data)
{
  assert (range_is_valid (sx, start, n));

  if (sx->memory != NULL)
    {
      uint8_t **p = sparse_array_get (sx->memory, row);
      if (p != NULL)
        {
          memcpy (data, *p + start, n);
          return true;
        }
    }
  else
    {
      if (range_set_contains (sx->disk_rows, row))
        return ext_array_read (sx->disk, (off_t) row * sx->n_bytes + start,
                               n, data);
    }

  memcpy (data, sx->default_row + start, n);
  return true;
}

/* Implements sparse_xarray_write for an on-disk sparse_xarray. */
static bool
write_disk_row (struct sparse_xarray *sx, unsigned long int row,
                size_t start, size_t n, const void *data)
{
  off_t ofs = (off_t) row * sx->n_bytes;
  if (range_set_contains (sx->disk_rows, row))
    return ext_array_write (sx->disk, ofs + start, n, data);
  else
    {
      range_set_set1 (sx->disk_rows, row, 1);
      return (ext_array_write (sx->disk, ofs, start, sx->default_row)
              && ext_array_write (sx->disk, ofs + start, n, data)
              && ext_array_write (sx->disk, ofs + start + n,
                                  sx->n_bytes - start - n,
                                  sx->default_row + start + n));
    }
}

/* Writes the VALUE_CNT values in VALUES into columns
   COLUMNS...(COLUMNS + VALUE_CNT), exclusive, in the given ROW
   in SX.
   Returns true if successful, false on I/O error. */
bool
sparse_xarray_write (struct sparse_xarray *sx, unsigned long int row,
                     size_t start, size_t n, const void *data)
{
  assert (range_is_valid (sx, start, n));
  if (sx->memory != NULL)
    {
      uint8_t **p = sparse_array_get (sx->memory, row);
      if (p == NULL)
        {
          if (sparse_array_count (sx->memory) < sx->max_memory_rows)
            {
              p = sparse_array_insert (sx->memory, row);
              *p = xmemdup (sx->default_row, sx->n_bytes);
            }
          else
            {
              if (!dump_sparse_xarray_to_disk (sx))
                return false;
              return write_disk_row (sx, row, start, n, data);
            }
        }
      memcpy (*p + start, data, n);
      return true;
    }
  else
    return write_disk_row (sx, row, start, n, data);
}

/* Writes the VALUE_CNT values in VALUES to columns
   START_COLUMN...(START_COLUMN + VALUE_CNT), exclusive, in every
   row in SX, even those rows that have not yet been written.
   Returns true if successful, false on I/O error.

   The runtime of this function is linear in the number of rows
   in SX that have already been written. */
bool
sparse_xarray_write_columns (struct sparse_xarray *sx, size_t start,
                             size_t n, const void *data)
{
  assert (range_is_valid (sx, start, n));

  /* Set defaults. */
  memcpy (sx->default_row + start, data, n);

  /* Set individual rows. */
  if (sx->memory != NULL)
    {
      unsigned long int idx;
      uint8_t **p;

      for (p = sparse_array_first (sx->memory, &idx); p != NULL;
           p = sparse_array_next (sx->memory, idx, &idx))
        memcpy (*p + start, data, n);
    }
  else
    {
      const struct range_set_node *node;

      RANGE_SET_FOR_EACH (node, sx->disk_rows)
        {
          unsigned long int start_row = range_set_node_get_start (node);
          unsigned long int end_row = range_set_node_get_end (node);
          unsigned long int row;

          for (row = start_row; row < end_row; row++)
            {
              off_t offset = (off_t) row * sx->n_bytes;
              if (!ext_array_write (sx->disk, offset + start, n, data))
                break;
            }
        }

      if (ext_array_error (sx->disk))
        return false;
    }
  return true;
}

static unsigned long int
scan_first (const struct sparse_xarray *sx)
{
  if (sx->memory)
    {
      unsigned long int idx;
      return sparse_array_first (sx->memory, &idx) ? idx : ULONG_MAX;
    }
  else
    return range_set_scan (sx->disk_rows, 0);
}

static unsigned long int
scan_next (const struct sparse_xarray *sx, unsigned long int start)
{
  if (sx->memory)
    {
      unsigned long int idx;
      return sparse_array_next (sx->memory, start, &idx) ? idx : ULONG_MAX;
    }
  else
    return range_set_scan (sx->disk_rows, start + 1);
}

/* Only works for rows for which sparse_xarray_contains_row()
   would return true. */
static uint8_t *
get_row (const struct sparse_xarray *sx, unsigned long int idx,
         uint8_t *buffer)
{
  if (sx->memory)
    {
      uint8_t **p = sparse_array_get (sx->memory, idx);
      return *p;
    }
  else if (ext_array_read (sx->disk, (off_t) idx * sx->n_bytes,
                           sx->n_bytes, buffer))
    return buffer;
  else
    return NULL;
}

/* Iterates over all the rows in SX and DX, passing each pair of
   rows with equal indexes to CB.  CB's modifications, if any, to
   destination rows are written back to DX.

   All rows that are actually in use in SX or in DX or both are
   passed to CB.  If a row is in use in SX but not in DX, or vice
   versa, then the "default" row (as set by
   sparse_xarray_write_columns) is passed as the contents of the
   other row.

   CB is also called once with the default row from SX and the
   default row from DX.  Modifying the data passed as the default
   row from DX will change DX's default row.

   Returns true if successful, false if I/O on SX or DX fails or
   if CB returns false.  On failure, the contents of DX are
   undefined. */
bool
sparse_xarray_copy (const struct sparse_xarray *sx, struct sparse_xarray *dx,
                    bool (*cb) (const void *src, void *dst, void *aux),
                    void *aux)
{
  bool success = true;

  if (!cb (sx->default_row, dx->default_row, aux))
    return false;

  if (sx == dx)
    {
      if (sx->memory)
        {
          unsigned long int idx;
          uint8_t **row;

          for (row = sparse_array_first (sx->memory, &idx); row != NULL;
               row = sparse_array_next (sx->memory, idx, &idx))
            {
              success = cb (*row, *row, aux);
              if (!success)
                break;
            }
        }
      else if (sx->disk)
        {
          const struct range_set_node *node;
          void *tmp = xmalloc (sx->n_bytes);

          RANGE_SET_FOR_EACH (node, sx->disk_rows)
            {
              unsigned long int start = range_set_node_get_start (node);
              unsigned long int end = range_set_node_get_end (node);
              unsigned long int row;

              for (row = start; row < end; row++)
                {
                  off_t offset = (off_t) row * sx->n_bytes;
                  success = (ext_array_read (sx->disk, offset, sx->n_bytes,
                                             tmp)
                             && cb (tmp, tmp, aux)
                             && ext_array_write (dx->disk, offset,
                                                 dx->n_bytes, tmp));
                  if (!success)
                    break;
                }
            }

          free (tmp);
        }
    }
  else
    {
      unsigned long int src_idx = scan_first (sx);
      unsigned long int dst_idx = scan_first (dx);

      uint8_t *tmp_src_row = xmalloc (sx->n_bytes);
      uint8_t *tmp_dst_row = xmalloc (dx->n_bytes);

      for (;;)
        {
          unsigned long int idx;
          const uint8_t *src_row;
          uint8_t *dst_row;

          /* Determine the index of the row to process.  If
             src_idx == dst_idx, then the row has been written in
             both SX and DX.  Otherwise, it has been written in
             only the sparse_xarray corresponding to the smaller
             index, and has the default contents in the other. */
          idx = MIN (src_idx, dst_idx);
          if (idx == ULONG_MAX)
            break;

          /* Obtain a copy of the source row as src_row. */
          if (idx == src_idx)
            src_row = get_row (sx, idx, tmp_src_row);
          else
            src_row = sx->default_row;

          /* Obtain the destination row as dst_row. */
          if (idx == dst_idx)
            dst_row = get_row (dx, idx, tmp_dst_row);
          else if (dx->memory
                   && sparse_array_count (dx->memory) < dx->max_memory_rows)
            {
              uint8_t **p = sparse_array_insert (dx->memory, idx);
              dst_row = *p = xmemdup (dx->default_row, dx->n_bytes);
            }
          else
            {
              memcpy (tmp_dst_row, dx->default_row, dx->n_bytes);
              dst_row = tmp_dst_row;
            }

          /* Run the callback. */
          success = cb (src_row, dst_row, aux);
          if (!success)
            break;

          /* Write back the destination row, if necessary. */
          if (dst_row == tmp_dst_row)
            {
              success = sparse_xarray_write (dx, idx, 0, dx->n_bytes, dst_row);
              if (!success)
                break;
            }
          else
            {
              /* Nothing to do: we modified the destination row in-place. */
            }

          /* Advance to the next row. */
          if (src_idx == idx)
            src_idx = scan_next (sx, src_idx);
          if (dst_idx == idx)
            dst_idx = scan_next (dx, dst_idx);
        }

      free (tmp_src_row);
      free (tmp_dst_row);
    }

  return success;
}

/* Returns a hash value for SX suitable for use with the model
   checker.  The value in BASIS is folded into the hash.

   The returned hash value is *not* suitable for storage and
   retrieval of sparse_xarrays that have identical contents,
   because it will return different hash values for
   sparse_xarrays that have the same contents (and it's slow).

   We use MD4 because it is much faster than MD5 or SHA-1 but its
   collision resistance is just as good. */
unsigned int
sparse_xarray_model_checker_hash (const struct sparse_xarray *sx,
                                  unsigned int basis)
{
  unsigned int hash[DIV_RND_UP (20, sizeof (unsigned int))];
  struct md4_ctx ctx;

  md4_init_ctx (&ctx);
  md4_process_bytes (&basis, sizeof basis, &ctx);
  md4_process_bytes (&sx->n_bytes, sizeof sx->n_bytes, &ctx);
  md4_process_bytes (sx->default_row, sx->n_bytes, &ctx);

  if (sx->memory)
    {
      unsigned long int idx;
      uint8_t **row;

      md4_process_bytes ("m", 1, &ctx);
      md4_process_bytes (&sx->max_memory_rows, sizeof sx->max_memory_rows,
                         &ctx);
      for (row = sparse_array_first (sx->memory, &idx); row != NULL;
           row = sparse_array_next (sx->memory, idx, &idx))
        {
          md4_process_bytes (&idx, sizeof idx, &ctx);
          md4_process_bytes (*row, sx->n_bytes, &ctx);
        }
    }
  else
    {
      const struct range_set_node *node;
      void *tmp = xmalloc (sx->n_bytes);

      md4_process_bytes ("d", 1, &ctx);
      RANGE_SET_FOR_EACH (node, sx->disk_rows)
        {
          unsigned long int start = range_set_node_get_start (node);
          unsigned long int end = range_set_node_get_end (node);
          unsigned long int idx;

          for (idx = start; idx < end; idx++)
            {
              off_t offset = (off_t) idx * sx->n_bytes;
              if (!ext_array_read (sx->disk, offset, sx->n_bytes, tmp))
                NOT_REACHED ();
              md4_process_bytes (&idx, sizeof idx, &ctx);
              md4_process_bytes (tmp, sx->n_bytes, &ctx);
            }
        }
      free (tmp);
    }
  md4_finish_ctx (&ctx, hash);
  return hash[0];
}
