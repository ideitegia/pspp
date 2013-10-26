/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include "data/datasheet.h"

#include <stdlib.h>
#include <string.h>

#include "data/casereader-provider.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/lazy-casereader.h"
#include "data/settings.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/misc.h"
#include "libpspp/range-map.h"
#include "libpspp/range-set.h"
#include "libpspp/sparse-xarray.h"
#include "libpspp/taint.h"
#include "libpspp/tower.h"

#include "gl/minmax.h"
#include "gl/md4.h"
#include "gl/xalloc.h"

struct column;

static struct axis *axis_create (void);
static struct axis *axis_clone (const struct axis *);
static void axis_destroy (struct axis *);

static void axis_hash (const struct axis *, struct md4_ctx *);

static bool axis_allocate (struct axis *, unsigned long int request,
                           unsigned long int *start,
                           unsigned long int *width);
static void axis_make_available (struct axis *,
				 unsigned long int start,
				 unsigned long int width);
static unsigned long int axis_extend (struct axis *, unsigned long int width);

static unsigned long int axis_map (const struct axis *, unsigned long log_pos);

static unsigned long axis_get_size (const struct axis *);
static void axis_insert (struct axis *,
                         unsigned long int log_start,
                         unsigned long int phy_start,
                         unsigned long int cnt);
static void axis_remove (struct axis *,
                         unsigned long int start, unsigned long int cnt);
static void axis_move (struct axis *,
                       unsigned long int old_start,
                       unsigned long int new_start,
                       unsigned long int cnt);

static struct source *source_create_empty (size_t n_bytes);
static struct source *source_create_casereader (struct casereader *);
static struct source *source_clone (const struct source *);
static void source_destroy (struct source *);

static casenumber source_get_backing_n_rows (const struct source *);

static int source_allocate_column (struct source *, int width);
static void source_release_column (struct source *, int ofs, int width);
static bool source_in_use (const struct source *);

static bool source_read (const struct column *, casenumber row, union value *,
                         size_t n);
static bool source_write (const struct column *, casenumber row,
                          const union value *, size_t n);
static bool source_write_column (struct column *, const union value *);
static bool source_has_backing (const struct source *);

static int get_source_index (const struct datasheet *ds, const struct source *source);

/* A datasheet is internally composed from a set of data files,
   called "sources".  The sources that make up a datasheet must
   have the same number of rows (cases), but their numbers of
   columns (variables) may vary.

   A datasheet's external view is produced by mapping (permuting
   and selecting) its internal data.  Thus, we can rearrange or
   delete rows or columns simply by modifying the mapping.  We
   add rows by adding rows to each source and to the row mapping.
   We add columns by adding a new source, then adding that source
   to the column mapping.

   Each source in a datasheet can be a casereader or a
   sparse_xarray.  Casereaders are read-only, so when sources
   made from casereaders need to be modified, it is done
   "virtually" through being overlaid by a sparse_xarray. */
struct source
  {
    struct range_set *avail;    /* Free bytes are set to 1s. */
    struct sparse_xarray *data; /* Data at top level, atop the backing. */
    struct casereader *backing; /* Backing casereader (or null). */
    casenumber backing_rows;    /* Number of rows in backing (if backed). */
    size_t n_used;              /* Number of column in use (if backed). */
  };

/* A logical column. */
struct column
  {
    struct source *source;      /* Source of the underlying physical column. */
    int value_ofs;              /* If 'source' has a backing casereader,
                                   column's value offset in its cases. */
    int byte_ofs;               /* Byte offset in source's sparse_xarray. */
    int width;                  /* 0=numeric, otherwise string width. */
  };

/* A datasheet. */
struct datasheet
  {
    /* Data sources. */
    struct source **sources;    /* Sources, in no particular order. */
    size_t n_sources;           /* Number of sources. */

    /* Columns. */
    struct caseproto *proto;    /* Prototype for rows (initialized lazily). */
    struct column *columns;     /* Logical to physical column mapping. */
    size_t n_columns;           /* Number of logical columns. */
    unsigned column_min_alloc;  /* Min. # of columns to put in a new source. */

    /* Rows. */
    struct axis *rows;          /* Logical to physical row mapping. */

    /* Tainting. */
    struct taint *taint;        /* Indicates corrupted data. */
  };

/* Is this operation a read or a write? */
enum rw_op
  {
    OP_READ,
    OP_WRITE
  };

static void allocate_column (struct datasheet *, int width, struct column *);
static void release_source (struct datasheet *, struct source *);
static bool rw_case (struct datasheet *ds, enum rw_op op,
                     casenumber lrow, size_t start_column, size_t n_columns,
                     union value data[]);

/* Returns the number of bytes needed to store a value with the
   given WIDTH on disk. */
static size_t
width_to_n_bytes (int width)
{
  return width == 0 ? sizeof (double) : width;
}

/* Returns the address of the data in VALUE (for reading or
   writing to/from disk).  VALUE must have the given WIDTH. */
static void *
value_to_data (const union value *value_, int width)
{
  union value *value = (union value *) value_;
  assert (sizeof value->f == sizeof (double));
  if (width == 0)
    return &value->f;
  else
    return value_str_rw (value, width);
}

/* Returns the number of bytes needed to store all the values in
   PROTO on disk. */
static size_t
caseproto_to_n_bytes (const struct caseproto *proto)
{
  size_t n_bytes;
  size_t i;

  n_bytes = 0;
  for (i = 0; i < caseproto_get_n_widths (proto); i++)
    {
      int width = caseproto_get_width (proto, i);
      if (width >= 0)
        n_bytes += width_to_n_bytes (width);
    }
  return n_bytes;
}

/* Creates and returns a new datasheet.

   If READER is nonnull, then the datasheet initially contains
   the contents of READER. */
struct datasheet *
datasheet_create (struct casereader *reader)
{
  struct datasheet *ds = xmalloc (sizeof *ds);
  ds->sources = NULL;
  ds->n_sources = 0;
  ds->proto = NULL;
  ds->columns = NULL;
  ds->n_columns = 0;
  ds->column_min_alloc = 8;
  ds->rows = axis_create ();
  ds->taint = taint_create ();

  if (reader != NULL)
    {
      casenumber n_rows;
      size_t byte_ofs;
      size_t i;

      taint_propagate (casereader_get_taint (reader), ds->taint);

      ds->proto = caseproto_ref (casereader_get_proto (reader));

      ds->sources = xmalloc (sizeof *ds->sources);
      ds->sources[0] = source_create_casereader (reader);
      ds->n_sources = 1;

      ds->n_columns = caseproto_get_n_widths (ds->proto);
      ds->columns = xnmalloc (ds->n_columns, sizeof *ds->columns);
      byte_ofs = 0;
      for (i = 0; i < ds->n_columns; i++)
        {
          struct column *column = &ds->columns[i];
          int width = caseproto_get_width (ds->proto, i);
          column->source = ds->sources[0];
          column->width = width;
          if (width >= 0)
            {
              column->value_ofs = i;
              column->byte_ofs = byte_ofs;
              byte_ofs += width_to_n_bytes (column->width);
            }
        }

      n_rows = source_get_backing_n_rows (ds->sources[0]);
      if (n_rows > 0)
        axis_insert (ds->rows, 0, axis_extend (ds->rows, n_rows), n_rows);
    }

  return ds;
}

/* Destroys datasheet DS. */
void
datasheet_destroy (struct datasheet *ds)
{
  size_t i;

  if (ds == NULL)
    return;

  for (i = 0; i < ds->n_sources; i++)
    source_destroy (ds->sources[i]);
  free (ds->sources);
  caseproto_unref (ds->proto);
  free (ds->columns);
  axis_destroy (ds->rows);
  taint_destroy (ds->taint);
  free (ds);
}

/* Returns the prototype for the cases in DS.  The caller must
   not unref the returned prototype. */
const struct caseproto *
datasheet_get_proto (const struct datasheet *ds_)
{
  struct datasheet *ds = CONST_CAST (struct datasheet *, ds_);
  if (ds->proto == NULL)
    {
      size_t i;

      ds->proto = caseproto_create ();
      for (i = 0; i < ds->n_columns; i++)
        ds->proto = caseproto_add_width (ds->proto, ds->columns[i].width);
    }
  return ds->proto;
}

/* Returns the width of the given COLUMN within DS.
   COLUMN must be less than the number of columns in DS. */
int
datasheet_get_column_width (const struct datasheet *ds, size_t column)
{
  assert (column < datasheet_get_n_columns (ds));
  return ds->columns[column].width;
}

/* Moves datasheet DS to a new location in memory, and returns
   the new location.  Afterward, the datasheet must not be
   accessed at its former location.

   This function is useful for ensuring that all references to a
   datasheet have been dropped, especially in conjunction with
   tools like Valgrind. */
struct datasheet *
datasheet_rename (struct datasheet *ds)
{
  struct datasheet *new = xmemdup (ds, sizeof *ds);
  free (ds);
  return new;
}

/* Returns true if datasheet DS is tainted.
   A datasheet is tainted by an I/O error or by taint
   propagation to the datasheet. */
bool
datasheet_error (const struct datasheet *ds)
{
  return taint_is_tainted (ds->taint);
}

/* Marks datasheet DS tainted. */
void
datasheet_force_error (struct datasheet *ds)
{
  taint_set_taint (ds->taint);
}

/* Returns datasheet DS's taint object. */
const struct taint *
datasheet_get_taint (const struct datasheet *ds)
{
  return ds->taint;
}

/* Returns the number of rows in DS. */
casenumber
datasheet_get_n_rows (const struct datasheet *ds)
{
  return axis_get_size (ds->rows);
}

/* Returns the number of columns in DS. */
size_t
datasheet_get_n_columns (const struct datasheet *ds)
{
  return ds->n_columns;
}

/* Inserts a column of the given WIDTH into datasheet DS just
   before column BEFORE.  Initializes the contents of each row in
   the inserted column to VALUE (which must have width WIDTH).

   Returns true if successful, false on failure.  In case of
   failure, the datasheet is unchanged. */
bool
datasheet_insert_column (struct datasheet *ds,
                         const union value *value, int width, size_t before)
{
  struct column *col;

  assert (before <= ds->n_columns);

  ds->columns = xnrealloc (ds->columns,
                           ds->n_columns + 1, sizeof *ds->columns);
  insert_element (ds->columns, ds->n_columns, sizeof *ds->columns, before);
  col = &ds->columns[before];
  ds->n_columns++;

  allocate_column (ds, width, col);

  if (width >= 0 && !source_write_column (col, value))
    {
      datasheet_delete_columns (ds, before, 1);
      taint_set_taint (ds->taint);
      return false;
    }

  return true;
}

/* Deletes the N columns in DS starting from column START. */
void
datasheet_delete_columns (struct datasheet *ds, size_t start, size_t n)
{
  assert (start + n <= ds->n_columns);

  if (n > 0)
    {
      size_t i;

      for (i = start; i < start + n; i++)
        {
          struct column *column = &ds->columns[i];
          struct source *source = column->source;
          source_release_column (source, column->byte_ofs, column->width);
          release_source (ds, source);
        }

      remove_range (ds->columns, ds->n_columns, sizeof *ds->columns, start, n);
      ds->n_columns -= n;

      caseproto_unref (ds->proto);
      ds->proto = NULL;
    }
}

/* Moves the N columns in DS starting at position OLD_START so
   that they then start at position NEW_START.  Equivalent to
   deleting the column rows, then inserting them at what becomes
   position NEW_START after the deletion. */
void
datasheet_move_columns (struct datasheet *ds,
                        size_t old_start, size_t new_start,
                        size_t n)
{
  assert (old_start + n <= ds->n_columns);
  assert (new_start + n <= ds->n_columns);

  move_range (ds->columns, ds->n_columns, sizeof *ds->columns,
              old_start, new_start, n);

  caseproto_unref (ds->proto);
  ds->proto = NULL;
}

struct resize_datasheet_value_aux
  {
    union value src_value;
    size_t src_ofs;
    int src_width;

    void (*resize_cb) (const union value *, union value *, const void *aux);
    const void *resize_cb_aux;

    union value dst_value;
    size_t dst_ofs;
    int dst_width;
  };

static bool
resize_datasheet_value (const void *src, void *dst, void *aux_)
{
  struct resize_datasheet_value_aux *aux = aux_;

  memcpy (value_to_data (&aux->src_value, aux->src_width),
          (uint8_t *) src + aux->src_ofs,
          width_to_n_bytes (aux->src_width));

  aux->resize_cb (&aux->src_value, &aux->dst_value, aux->resize_cb_aux);

  memcpy ((uint8_t *) dst + aux->dst_ofs,
          value_to_data (&aux->dst_value, aux->dst_width),
          width_to_n_bytes (aux->dst_width));

  return true;
}

bool
datasheet_resize_column (struct datasheet *ds, size_t column, int new_width,
                         void (*resize_cb) (const union value *,
                                            union value *, const void *aux),
                         const void *resize_cb_aux)
{
  struct column old_col;
  struct column *col;
  int old_width;

  assert (column < datasheet_get_n_columns (ds));

  col = &ds->columns[column];
  old_col = *col;
  old_width = old_col.width;

  if (new_width == -1)
    {
      if (old_width != -1)
        {
          datasheet_delete_columns (ds, column, 1);
          datasheet_insert_column (ds, NULL, -1, column);
        }
    }
  else if (old_width == -1)
    {
      union value value;
      value_init (&value, new_width);
      value_set_missing (&value, new_width);
      if (resize_cb != NULL)
        resize_cb (NULL, &value, resize_cb_aux);
      datasheet_delete_columns (ds, column, 1);
      datasheet_insert_column (ds, &value, new_width, column);
      value_destroy (&value, new_width);
    }
  else if (source_has_backing (col->source))
    {
      unsigned long int n_rows = axis_get_size (ds->rows);
      unsigned long int lrow;
      union value src, dst;

      source_release_column (col->source, col->byte_ofs, col->width);
      allocate_column (ds, new_width, col);

      value_init (&src, old_width);
      value_init (&dst, new_width);
      for (lrow = 0; lrow < n_rows; lrow++)
        {
          unsigned long int prow = axis_map (ds->rows, lrow);
          if (!source_read (&old_col, prow, &src, 1))
            {
              /* FIXME: back out col changes. */
              break;
            }
          resize_cb (&src, &dst, resize_cb_aux);
          if (!source_write (col, prow, &dst, 1))
            {
              /* FIXME: back out col changes. */
              break;
            }
        }
      value_destroy (&src, old_width);
      value_destroy (&dst, new_width);
      if (lrow < n_rows)
	return false;

      release_source (ds, old_col.source);
    }
  else
    {
      struct resize_datasheet_value_aux aux;

      source_release_column (col->source, col->byte_ofs, col->width);
      allocate_column (ds, new_width, col);

      value_init (&aux.src_value, old_col.width);
      aux.src_ofs = old_col.byte_ofs;
      aux.src_width = old_col.width;
      aux.resize_cb = resize_cb;
      aux.resize_cb_aux = resize_cb_aux;
      value_init (&aux.dst_value, new_width);
      aux.dst_ofs = col->byte_ofs;
      aux.dst_width = new_width;
      sparse_xarray_copy (old_col.source->data, col->source->data,
                          resize_datasheet_value, &aux);
      value_destroy (&aux.src_value, old_width);
      value_destroy (&aux.dst_value, new_width);

      release_source (ds, old_col.source);
    }
  return true;
}

/* Retrieves and returns the contents of the given ROW in
   datasheet DS.  The caller owns the returned case and must
   unref it when it is no longer needed.  Returns a null pointer
   on I/O error. */
struct ccase *
datasheet_get_row (const struct datasheet *ds, casenumber row)
{
  size_t n_columns = datasheet_get_n_columns (ds);
  struct ccase *c = case_create (datasheet_get_proto (ds));
  if (rw_case (CONST_CAST (struct datasheet *, ds), OP_READ,
               row, 0, n_columns, case_data_all_rw (c)))
    return c;
  else
    {
      case_unref (c);
      return NULL;
    }
}

/* Stores the contents of case C, which is destroyed, into the
   given ROW in DS.  Returns true on success, false on I/O error.
   On failure, the given ROW might be partially modified or
   corrupted. */
bool
datasheet_put_row (struct datasheet *ds, casenumber row, struct ccase *c)
{
  size_t n_columns = datasheet_get_n_columns (ds);
  bool ok = rw_case (ds, OP_WRITE, row, 0, n_columns,
                     (union value *) case_data_all (c));
  case_unref (c);
  return ok;
}

/* Stores the values of COLUMN in DS in the given ROW in DS into
   VALUE.  The caller must have already initialized VALUE as a
   value of the appropriate width (as returned by
   datasheet_get_column_width (DS, COLUMN)).  Returns true if
   successful, false on I/O error. */
bool
datasheet_get_value (const struct datasheet *ds, casenumber row,
                     size_t column, union value *value)
{
  assert (row >= 0);
  return rw_case (CONST_CAST (struct datasheet *, ds), OP_READ,
                  row, column, 1, value);
}

/* Stores VALUE into DS in the given ROW and COLUMN.  VALUE must
   have the correct width for COLUMN (as returned by
   datasheet_get_column_width (DS, COLUMN)).  Returns true if
   successful, false on I/O error.  On failure, ROW might be
   partially modified or corrupted. */
bool
datasheet_put_value (struct datasheet *ds UNUSED, casenumber row UNUSED,
                     size_t column UNUSED, const union value *value UNUSED)
{
  return rw_case (ds, OP_WRITE, row, column, 1, (union value *) value);
}

/* Inserts the CNT cases at C into datasheet DS just before row
   BEFORE.  Returns true if successful, false on I/O error.  On
   failure, datasheet DS is not modified.

   Regardless of success, this function unrefs all of the cases
   in C. */
bool
datasheet_insert_rows (struct datasheet *ds,
                       casenumber before, struct ccase *c[],
                       casenumber cnt)
{
  casenumber added = 0;
  while (cnt > 0)
    {
      unsigned long first_phy;
      unsigned long phy_cnt;
      unsigned long i;

      /* Allocate physical rows from the pool of available
         rows. */
      if (!axis_allocate (ds->rows, cnt, &first_phy, &phy_cnt))
        {
          /* No rows were available.  Extend the row axis to make
             some new ones available. */
          phy_cnt = cnt;
          first_phy = axis_extend (ds->rows, cnt);
        }

      /* Insert the new rows into the row mapping. */
      axis_insert (ds->rows, before, first_phy, phy_cnt);

      /* Initialize the new rows. */
      for (i = 0; i < phy_cnt; i++)
        if (!datasheet_put_row (ds, before + i, c[i]))
          {
            while (++i < cnt)
              case_unref (c[i]);
            datasheet_delete_rows (ds, before - added, phy_cnt + added);
            return false;
          }

      /* Advance. */
      c += phy_cnt;
      cnt -= phy_cnt;
      before += phy_cnt;
      added += phy_cnt;
    }
  return true;
}

/* Deletes the CNT rows in DS starting from row FIRST. */
void
datasheet_delete_rows (struct datasheet *ds,
                       casenumber first, casenumber cnt)
{
  size_t lrow;

  /* Free up rows for reuse.
     FIXME: optimize. */
  for (lrow = first; lrow < first + cnt; lrow++)
    axis_make_available (ds->rows, axis_map (ds->rows, lrow), 1);

  /* Remove rows from logical-to-physical mapping. */
  axis_remove (ds->rows, first, cnt);
}

/* Moves the CNT rows in DS starting at position OLD_START so
   that they then start at position NEW_START.  Equivalent to
   deleting the given rows, then inserting them at what becomes
   position NEW_START after the deletion. */
void
datasheet_move_rows (struct datasheet *ds,
                     size_t old_start, size_t new_start,
                     size_t cnt)
{
  axis_move (ds->rows, old_start, new_start, cnt);
}

static const struct casereader_random_class datasheet_reader_class;

/* Creates and returns a casereader whose input cases are the
   rows in datasheet DS.  From the caller's perspective, DS is
   effectively destroyed by this operation, such that the caller
   must not reference it again. */
struct casereader *
datasheet_make_reader (struct datasheet *ds)
{
  struct casereader *reader;
  ds = datasheet_rename (ds);
  reader = casereader_create_random (datasheet_get_proto (ds),
                                     datasheet_get_n_rows (ds),
                                     &datasheet_reader_class, ds);
  taint_propagate (datasheet_get_taint (ds), casereader_get_taint (reader));
  return reader;
}

/* "read" function for the datasheet random casereader. */
static struct ccase *
datasheet_reader_read (struct casereader *reader UNUSED, void *ds_,
                       casenumber case_idx)
{
  struct datasheet *ds = ds_;
  if (case_idx < datasheet_get_n_rows (ds))
    {
      struct ccase *c = datasheet_get_row (ds, case_idx);
      if (c == NULL)
        taint_set_taint (ds->taint);
      return c;
    }
  else
    return NULL;
}

/* "destroy" function for the datasheet random casereader. */
static void
datasheet_reader_destroy (struct casereader *reader UNUSED, void *ds_)
{
  struct datasheet *ds = ds_;
  datasheet_destroy (ds);
}

/* "advance" function for the datasheet random casereader. */
static void
datasheet_reader_advance (struct casereader *reader UNUSED, void *ds_,
                          casenumber case_cnt)
{
  struct datasheet *ds = ds_;
  datasheet_delete_rows (ds, 0, case_cnt);
}

/* Random casereader class for a datasheet. */
static const struct casereader_random_class datasheet_reader_class =
  {
    datasheet_reader_read,
    datasheet_reader_destroy,
    datasheet_reader_advance,
  };

static void
allocate_column (struct datasheet *ds, int width, struct column *column)
{
  caseproto_unref (ds->proto);
  ds->proto = NULL;

  column->value_ofs = -1;
  column->width = width;
  if (width >= 0)
    {
      int n_bytes;
      size_t i;

      n_bytes = width_to_n_bytes (width);
      for (i = 0; i < ds->n_sources; i++)
        {
          column->source = ds->sources[i];
          column->byte_ofs = source_allocate_column (column->source, n_bytes);
          if (column->byte_ofs >= 0)
            return;
        }

      column->source = source_create_empty (MAX (n_bytes,
                                                 ds->column_min_alloc));
      ds->sources = xnrealloc (ds->sources,
                               ds->n_sources + 1, sizeof *ds->sources);
      ds->sources[ds->n_sources++] = column->source;

      ds->column_min_alloc = MIN (65536, ds->column_min_alloc * 2);

      column->byte_ofs = source_allocate_column (column->source, n_bytes);
      assert (column->byte_ofs >= 0);
    }
  else
    {
      column->source = NULL;
      column->byte_ofs = -1;
    }
}

static void
release_source (struct datasheet *ds, struct source *source)
{
  if (source_has_backing (source) && !source_in_use (source))
    {
      /* Since only the first source to be added ever
         has a backing, this source must have index
         0.  */
      assert (source == ds->sources[0]);
      ds->sources[0] = ds->sources[--ds->n_sources];
      source_destroy (source);
    }
}

/* Reads (if OP is OP_READ) or writes (if op is OP_WRITE) the
   N_COLUMNS columns starting from column START_COLUMN in row
   LROW to/from the N_COLUMNS values in DATA. */
static bool
rw_case (struct datasheet *ds, enum rw_op op,
         casenumber lrow, size_t start_column, size_t n_columns,
         union value data[])
{
  struct column *columns = &ds->columns[start_column];
  casenumber prow;
  size_t i;

  assert (lrow < datasheet_get_n_rows (ds));
  assert (n_columns <= datasheet_get_n_columns (ds));
  assert (start_column + n_columns <= datasheet_get_n_columns (ds));

  prow = axis_map (ds->rows, lrow);
  for (i = 0; i < n_columns; )
    {
      struct source *source = columns[i].source;
      size_t j;
      bool ok;

      if (columns[i].width < 0)
        {
          i++;
          continue;
        }

      for (j = i + 1; j < n_columns; j++)
        if (columns[j].width < 0 || columns[j].source != source)
          break;

      if (op == OP_READ)
        ok = source_read (&columns[i], prow, &data[i], j - i);
      else
        ok = source_write (&columns[i], prow, &data[i], j - i);

      if (!ok)
        {
          taint_set_taint (ds->taint);
          return false;
        }

      i = j;
    }
  return true;
}

/* An axis.

   An axis has two functions.  First, it maintains a mapping from
   logical (client-visible) to physical (storage) ordinates.  The
   axis_map and axis_get_size functions inspect this mapping, and
   the axis_insert, axis_remove, and axis_move functions modify
   it.  Second, it tracks the set of ordinates that are unused
   and available for reuse.  The axis_allocate,
   axis_make_available, and axis_extend functions affect the set
   of available ordinates. */
struct axis
{
  struct tower log_to_phy;     /* Map from logical to physical ordinates;
				  contains "struct axis_group"s. */
  struct range_set *available; /* Set of unused, available ordinates. */
  unsigned long int phy_size;  /* Current physical length of axis. */
};

/* A mapping from logical to physical ordinates. */
struct axis_group
{
  struct tower_node logical;  /* Range of logical ordinates. */
  unsigned long phy_start;    /* First corresponding physical ordinate. */
};

static struct axis_group *axis_group_from_tower_node (struct tower_node *);
static struct tower_node *make_axis_group (unsigned long int phy_start);
static struct tower_node *split_axis (struct axis *, unsigned long int where);
static void merge_axis_nodes (struct axis *, struct tower_node *,
                              struct tower_node **other_node);
static void check_axis_merged (const struct axis *axis UNUSED);

/* Creates and returns a new, initially empty axis. */
static struct axis *
axis_create (void)
{
  struct axis *axis = xmalloc (sizeof *axis);
  tower_init (&axis->log_to_phy);
  axis->available = range_set_create ();
  axis->phy_size = 0;
  return axis;
}

/* Returns a clone of existing axis OLD.

   Currently this is used only by the datasheet model checker
   driver, but it could be otherwise useful. */
static struct axis *
axis_clone (const struct axis *old)
{
  const struct tower_node *node;
  struct axis *new;

  new = xmalloc (sizeof *new);
  tower_init (&new->log_to_phy);
  new->available = range_set_clone (old->available, NULL);
  new->phy_size = old->phy_size;

  for (node = tower_first (&old->log_to_phy); node != NULL;
       node = tower_next (&old->log_to_phy, node))
    {
      unsigned long int size = tower_node_get_size (node);
      struct axis_group *group = tower_data (node, struct axis_group, logical);
      tower_insert (&new->log_to_phy, size, make_axis_group (group->phy_start),
                    NULL);
    }

  return new;
}

/* Adds the state of AXIS to the MD4 hash context CTX.

   This is only used by the datasheet model checker driver.  It
   is unlikely to be otherwise useful. */
static void
axis_hash (const struct axis *axis, struct md4_ctx *ctx)
{
  const struct tower_node *tn;
  const struct range_set_node *rsn;

  for (tn = tower_first (&axis->log_to_phy); tn != NULL;
       tn = tower_next (&axis->log_to_phy, tn))
    {
      struct axis_group *group = tower_data (tn, struct axis_group, logical);
      unsigned long int phy_start = group->phy_start;
      unsigned long int size = tower_node_get_size (tn);

      md4_process_bytes (&phy_start, sizeof phy_start, ctx);
      md4_process_bytes (&size, sizeof size, ctx);
    }

  RANGE_SET_FOR_EACH (rsn, axis->available)
    {
      unsigned long int start = range_set_node_get_start (rsn);
      unsigned long int end = range_set_node_get_end (rsn);

      md4_process_bytes (&start, sizeof start, ctx);
      md4_process_bytes (&end, sizeof end, ctx);
    }

  md4_process_bytes (&axis->phy_size, sizeof axis->phy_size, ctx);
}

/* Destroys AXIS. */
static void
axis_destroy (struct axis *axis)
{
  if (axis == NULL)
    return;

  while (!tower_is_empty (&axis->log_to_phy))
    {
      struct tower_node *node = tower_first (&axis->log_to_phy);
      struct axis_group *group = tower_data (node, struct axis_group,
                                             logical);
      tower_delete (&axis->log_to_phy, node);
      free (group);
    }

  range_set_destroy (axis->available);
  free (axis);
}

/* Allocates up to REQUEST contiguous unused and available
   ordinates from AXIS.  If successful, stores the number
   obtained into *WIDTH and the ordinate of the first into
   *START, marks the ordinates as now unavailable return true.
   On failure, which occurs only if AXIS has no unused and
   available ordinates, returns false without modifying AXIS. */
static bool
axis_allocate (struct axis *axis, unsigned long int request,
               unsigned long int *start, unsigned long int *width)
{
  return range_set_allocate (axis->available, request, start, width);
}

/* Marks the WIDTH contiguous ordinates in AXIS, starting from
   START, as unused and available. */
static void
axis_make_available (struct axis *axis,
                     unsigned long int start, unsigned long int width)
{
  range_set_set1 (axis->available, start, width);
}

/* Extends the total physical length of AXIS by WIDTH and returns
   the first ordinate in the new physical region. */
static unsigned long int
axis_extend (struct axis *axis, unsigned long int width)
{
  unsigned long int start = axis->phy_size;
  axis->phy_size += width;
  return start;
}

/* Returns the physical ordinate in AXIS corresponding to logical
   ordinate LOG_POS.  LOG_POS must be less than the logical
   length of AXIS. */
static unsigned long int
axis_map (const struct axis *axis, unsigned long log_pos)
{
  struct tower_node *node;
  struct axis_group *group;
  unsigned long int group_start;

  node = tower_lookup (&axis->log_to_phy, log_pos, &group_start);
  group = tower_data (node, struct axis_group, logical);
  return group->phy_start + (log_pos - group_start);
}

/* Returns the logical length of AXIS. */
static unsigned long
axis_get_size (const struct axis *axis)
{
  return tower_height (&axis->log_to_phy);
}

/* Inserts the CNT contiguous physical ordinates starting at
   PHY_START into AXIS's logical-to-physical mapping, starting at
   logical position LOG_START. */
static void
axis_insert (struct axis *axis,
             unsigned long int log_start, unsigned long int phy_start,
             unsigned long int cnt)
{
  struct tower_node *before = split_axis (axis, log_start);
  struct tower_node *new = make_axis_group (phy_start);
  tower_insert (&axis->log_to_phy, cnt, new, before);
  merge_axis_nodes (axis, new, NULL);
  check_axis_merged (axis);
}

/* Removes CNT ordinates from AXIS's logical-to-physical mapping
   starting at logical position START. */
static void
axis_remove (struct axis *axis,
             unsigned long int start, unsigned long int cnt)
{
  if (cnt > 0)
    {
      struct tower_node *last = split_axis (axis, start + cnt);
      struct tower_node *cur, *next;
      for (cur = split_axis (axis, start); cur != last; cur = next)
        {
          next = tower_delete (&axis->log_to_phy, cur);
          free (axis_group_from_tower_node (cur));
        }
      merge_axis_nodes (axis, last, NULL);
      check_axis_merged (axis);
    }
}

/* Moves the CNT ordinates in AXIS's logical-to-mapping starting
   at logical position OLD_START so that they then start at
   position NEW_START. */
static void
axis_move (struct axis *axis,
           unsigned long int old_start, unsigned long int new_start,
           unsigned long int cnt)
{
  if (cnt > 0 && old_start != new_start)
    {
      struct tower_node *old_first, *old_last, *new_first;
      struct tower_node *merge1, *merge2;
      struct tower tmp_array;

      /* Move ordinates OLD_START...(OLD_START + CNT) into new,
         separate TMP_ARRAY. */
      old_first = split_axis (axis, old_start);
      old_last = split_axis (axis, old_start + cnt);
      tower_init (&tmp_array);
      tower_splice (&tmp_array, NULL,
                    &axis->log_to_phy, old_first, old_last);
      merge_axis_nodes (axis, old_last, NULL);
      check_axis_merged (axis);

      /* Move TMP_ARRAY to position NEW_START. */
      new_first = split_axis (axis, new_start);
      merge1 = tower_first (&tmp_array);
      merge2 = tower_last (&tmp_array);
      if (merge2 == merge1)
        merge2 = NULL;
      tower_splice (&axis->log_to_phy, new_first, &tmp_array, old_first, NULL);
      merge_axis_nodes (axis, merge1, &merge2);
      merge_axis_nodes (axis, merge2, NULL);
      check_axis_merged (axis);
    }
}

/* Returns the axis_group in which NODE is embedded. */
static struct axis_group *
axis_group_from_tower_node (struct tower_node *node)
{
  return tower_data (node, struct axis_group, logical);
}

/* Creates and returns a new axis_group at physical position
   PHY_START. */
static struct tower_node *
make_axis_group (unsigned long phy_start)
{
  struct axis_group *group = xmalloc (sizeof *group);
  group->phy_start = phy_start;
  return &group->logical;
}

/* Returns the tower_node in AXIS's logical-to-physical map whose
   bottom edge is at exact level WHERE.  If there is no such
   tower_node in AXIS's logical-to-physical map, then split_axis
   creates one by breaking an existing tower_node into two
   separate ones, unless WHERE is equal to the tower height, in
   which case it simply returns a null pointer. */
static struct tower_node *
split_axis (struct axis *axis, unsigned long int where)
{
  unsigned long int group_start;
  struct tower_node *group_node;
  struct axis_group *group;

  assert (where <= tower_height (&axis->log_to_phy));
  if (where >= tower_height (&axis->log_to_phy))
    return NULL;

  group_node = tower_lookup (&axis->log_to_phy, where, &group_start);
  group = axis_group_from_tower_node (group_node);
  if (where > group_start)
    {
      unsigned long int size_1 = where - group_start;
      unsigned long int size_2 = tower_node_get_size (group_node) - size_1;
      struct tower_node *next = tower_next (&axis->log_to_phy, group_node);
      struct tower_node *new = make_axis_group (group->phy_start + size_1);
      tower_resize (&axis->log_to_phy, group_node, size_1);
      tower_insert (&axis->log_to_phy, size_2, new, next);
      return new;
    }
  else
    return &group->logical;
}

/* Within AXIS, attempts to merge NODE (or the last node in AXIS,
   if NODE is null) with its neighbor nodes.  This is possible
   when logically adjacent nodes are also adjacent physically (in
   the same order).

   When a merge occurs, and OTHER_NODE is non-null and points to
   the node to be deleted, this function also updates
   *OTHER_NODE, if necessary, to ensure that it remains a valid
   pointer. */
static void
merge_axis_nodes (struct axis *axis, struct tower_node *node,
                  struct tower_node **other_node)
{
  struct tower *t = &axis->log_to_phy;
  struct axis_group *group;
  struct tower_node *next, *prev;

  /* Find node to potentially merge with neighbors. */
  if (node == NULL)
    node = tower_last (t);
  if (node == NULL)
    return;
  group = axis_group_from_tower_node (node);

  /* Try to merge NODE with successor. */
  next = tower_next (t, node);
  if (next != NULL)
    {
      struct axis_group *next_group = axis_group_from_tower_node (next);
      unsigned long this_height = tower_node_get_size (node);

      if (group->phy_start + this_height == next_group->phy_start)
        {
          unsigned long next_height = tower_node_get_size (next);
          tower_resize (t, node, this_height + next_height);
          if (other_node != NULL && *other_node == next)
            *other_node = tower_next (t, *other_node);
          tower_delete (t, next);
          free (next_group);
        }
    }

  /* Try to merge NODE with predecessor. */
  prev = tower_prev (t, node);
  if (prev != NULL)
    {
      struct axis_group *prev_group = axis_group_from_tower_node (prev);
      unsigned long prev_height = tower_node_get_size (prev);

      if (prev_group->phy_start + prev_height == group->phy_start)
        {
          unsigned long this_height = tower_node_get_size (node);
          group->phy_start = prev_group->phy_start;
          tower_resize (t, node, this_height + prev_height);
          if (other_node != NULL && *other_node == prev)
            *other_node = tower_next (t, *other_node);
          tower_delete (t, prev);
          free (prev_group);
        }
    }
}

/* Verify that all potentially merge-able nodes in AXIS are
   actually merged. */
static void
check_axis_merged (const struct axis *axis UNUSED)
{
#if ASSERT_LEVEL >= 10
  struct tower_node *prev, *node;

  for (prev = NULL, node = tower_first (&axis->log_to_phy); node != NULL;
       prev = node, node = tower_next (&axis->log_to_phy, node))
    if (prev != NULL)
      {
        struct axis_group *prev_group = axis_group_from_tower_node (prev);
        unsigned long prev_height = tower_node_get_size (prev);
        struct axis_group *node_group = axis_group_from_tower_node (node);
        assert (prev_group->phy_start + prev_height != node_group->phy_start);
      }
#endif
}

/* A source. */

/* Creates and returns an empty, unbacked source with N_BYTES
   bytes per case, none of which are initially in use. */
static struct source *
source_create_empty (size_t n_bytes)
{
  struct source *source = xmalloc (sizeof *source);
  size_t row_size = n_bytes + 4 * sizeof (void *);
  size_t max_memory_rows = settings_get_workspace () / row_size;
  source->avail = range_set_create ();
  range_set_set1 (source->avail, 0, n_bytes);
  source->data = sparse_xarray_create (n_bytes, MAX (max_memory_rows, 4));
  source->backing = NULL;
  source->backing_rows = 0;
  source->n_used = 0;
  return source;
}

/* Creates and returns a new source backed by READER and with the
   same initial dimensions and content. */
static struct source *
source_create_casereader (struct casereader *reader)
{
  const struct caseproto *proto = casereader_get_proto (reader);
  size_t n_bytes = caseproto_to_n_bytes (proto);
  struct source *source = source_create_empty (n_bytes);
  size_t n_columns;
  size_t i;

  range_set_set0 (source->avail, 0, n_bytes);
  source->backing = reader;
  source->backing_rows = casereader_count_cases (reader);

  source->n_used = 0;
  n_columns = caseproto_get_n_widths (proto);
  for (i = 0; i < n_columns; i++)
    if (caseproto_get_width (proto, i) >= 0)
      source->n_used++;

  return source;
}

/* Returns a clone of source OLD with the same data and backing
   (if any).

   Currently this is used only by the datasheet model checker
   driver, but it could be otherwise useful. */
static struct source *
source_clone (const struct source *old)
{
  struct source *new = xmalloc (sizeof *new);
  new->avail = range_set_clone (old->avail, NULL);
  new->data = sparse_xarray_clone (old->data);
  new->backing = old->backing != NULL ? casereader_clone (old->backing) : NULL;
  new->backing_rows = old->backing_rows;
  new->n_used = old->n_used;
  if (new->data == NULL)
    {
      source_destroy (new);
      new = NULL;
    }
  return new;
}

static int
source_allocate_column (struct source *source, int width)
{
  unsigned long int start;
  int n_bytes;

  assert (width >= 0);
  n_bytes = width_to_n_bytes (width);
  if (source->backing == NULL
      && range_set_allocate_fully (source->avail, n_bytes, &start))
    return start;
  else
    return -1;
}

static void
source_release_column (struct source *source, int ofs, int width)
{
  assert (width >= 0);
  range_set_set1 (source->avail, ofs, width_to_n_bytes (width));
  if (source->backing != NULL)
    source->n_used--;
}

/* Returns true if SOURCE has any columns in use,
   false otherwise. */
static bool
source_in_use (const struct source *source)
{
  return source->n_used > 0;
}

/* Destroys SOURCE and its data and backing, if any. */
static void
source_destroy (struct source *source)
{
  if (source != NULL)
    {
      range_set_destroy (source->avail);
      sparse_xarray_destroy (source->data);
      casereader_destroy (source->backing);
      free (source);
    }
}

/* Returns the number of rows in SOURCE's backing casereader
   (SOURCE must have a backing casereader). */
static casenumber
source_get_backing_n_rows (const struct source *source)
{
  assert (source_has_backing (source));
  return source->backing_rows;
}

/* Reads the N COLUMNS in the given ROW, into the N VALUES.  Returns true if
   successful, false on I/O error.

   All of the COLUMNS must have the same source.

   The caller must have initialized VALUES with the proper width. */
static bool
source_read (const struct column columns[], casenumber row,
             union value values[], size_t n)
{
  struct source *source = columns[0].source;
  size_t i;

  if (source->backing == NULL
      || sparse_xarray_contains_row (source->data, row))
    {
      bool ok = true;

      for (i = 0; i < n && ok; i++)
        ok = sparse_xarray_read (source->data, row, columns[i].byte_ofs,
                                 width_to_n_bytes (columns[i].width),
                                 value_to_data (&values[i], columns[i].width));
      return ok;
    }
  else
    {
      struct ccase *c = casereader_peek (source->backing, row);
      bool ok = c != NULL;
      if (ok)
        {
          for (i = 0; i < n; i++)
            value_copy (&values[i], case_data_idx (c, columns[i].value_ofs),
                        columns[i].width);
          case_unref (c);
        }
      return ok;
    }
}

static bool
copy_case_into_source (struct source *source, struct ccase *c, casenumber row)
{
  const struct caseproto *proto = casereader_get_proto (source->backing);
  size_t n_widths = caseproto_get_n_widths (proto);
  size_t ofs;
  size_t i;

  ofs = 0;
  for (i = 0; i < n_widths; i++)
    {
      int width = caseproto_get_width (proto, i);
      if (width >= 0)
        {
          int n_bytes = width_to_n_bytes (width);
          if (!sparse_xarray_write (source->data, row, ofs, n_bytes,
                                    value_to_data (case_data_idx (c, i),
                                                   width)))
            return false;
          ofs += n_bytes;
        }
    }
  return true;
}

/* Writes the N VALUES to their source in the given ROW and COLUMNS.  Returns
   true if successful, false on I/O error.  On error, the row's data may be
   completely or partially corrupted, both inside and outside the region to be
   written.

   All of the COLUMNS must have the same source. */
static bool
source_write (const struct column columns[], casenumber row,
              const union value values[], size_t n)
{
  struct source *source = columns[0].source;
  struct casereader *backing = source->backing;
  size_t i;

  if (backing != NULL
      && !sparse_xarray_contains_row (source->data, row)
      && row < source->backing_rows)
    {
      struct ccase *c;
      bool ok;

      c = casereader_peek (backing, row);
      if (c == NULL)
        return false;

      ok = copy_case_into_source (source, c, row);
      case_unref (c);
      if (!ok)
        return false;
    }

  for (i = 0; i < n; i++)
    if (!sparse_xarray_write (source->data, row, columns[i].byte_ofs,
                              width_to_n_bytes (columns[i].width),
                              value_to_data (&values[i], columns[i].width)))
      return false;
  return true;
}

/* Within SOURCE, which must not have a backing casereader,
   writes the VALUE_CNT values in VALUES_CNT to the VALUE_CNT
   columns starting from START_COLUMN, in every row, even in rows
   not yet otherwise initialized.  Returns true if successful,
   false if an I/O error occurs.

   We don't support backing != NULL because (1) it's harder and
   (2) this function is only called by
   datasheet_insert_column, which doesn't reuse columns from
   sources that are backed by casereaders. */
static bool
source_write_column (struct column *column, const union value *value)
{
  int width = column->width;

  assert (column->source->backing == NULL);
  assert (width >= 0);

  return sparse_xarray_write_columns (column->source->data, column->byte_ofs,
                                      width_to_n_bytes (width),
                                      value_to_data (value, width));
}

/* Returns true if SOURCE has a backing casereader, false
   otherwise. */
static bool
source_has_backing (const struct source *source)
{
  return source->backing != NULL;
}

/* Datasheet model checker test driver. */

static int
get_source_index (const struct datasheet *ds, const struct source *source)
{
  size_t i;

  for (i = 0; i < ds->n_sources; i++)
    if (ds->sources[i] == source)
      return i;
  NOT_REACHED ();
}

/* Clones the structure and contents of ODS into a new datasheet,
   and returns the new datasheet. */
struct datasheet *
clone_datasheet (const struct datasheet *ods)
{
  struct datasheet *ds;
  size_t i;

  ds = xmalloc (sizeof *ds);

  ds->sources = xmalloc (ods->n_sources * sizeof *ds->sources);
  for (i = 0; i < ods->n_sources; i++)
    ds->sources[i] = source_clone (ods->sources[i]);
  ds->n_sources = ods->n_sources;

  ds->proto = ods->proto != NULL ? caseproto_ref (ods->proto) : NULL;
  ds->columns = xmemdup (ods->columns, ods->n_columns * sizeof *ods->columns);
  for (i = 0; i < ods->n_columns; i++)
    ds->columns[i].source
      = ds->sources[get_source_index (ods, ods->columns[i].source)];
  ds->n_columns = ods->n_columns;
  ds->column_min_alloc = ods->column_min_alloc;

  ds->rows = axis_clone (ods->rows);

  ds->taint = taint_create ();

  return ds;
}

/* Hashes the structure of datasheet DS and returns the hash.
   We use MD4 because it is much faster than MD5 or SHA-1 but its
   collision resistance is just as good. */
unsigned int
hash_datasheet (const struct datasheet *ds)
{
  unsigned int hash[DIV_RND_UP (20, sizeof (unsigned int))];
  struct md4_ctx ctx;
  size_t i;

  md4_init_ctx (&ctx);
  for (i = 0; i < ds->n_columns; i++)
    {
      const struct column *column = &ds->columns[i];
      int source_n_bytes = sparse_xarray_get_n_columns (column->source->data);
      md4_process_bytes (&source_n_bytes, sizeof source_n_bytes, &ctx);
      /*md4_process_bytes (&column->byte_ofs, sizeof column->byte_ofs, &ctx);*/
      md4_process_bytes (&column->value_ofs, sizeof column->value_ofs, &ctx);
      md4_process_bytes (&column->width, sizeof column->width, &ctx);
    }
  axis_hash (ds->rows, &ctx);
  md4_process_bytes (&ds->column_min_alloc, sizeof ds->column_min_alloc, &ctx);
  md4_finish_ctx (&ctx, hash);
  return hash[0];
}
