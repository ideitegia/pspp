/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

#include <data/datasheet.h>

#include <stdlib.h>
#include <string.h>

#include <data/casereader-provider.h>
#include <data/casereader.h>
#include <data/casewriter.h>
#include <data/sparse-cases.h>
#include <libpspp/array.h>
#include <libpspp/assertion.h>
#include <libpspp/model-checker.h>
#include <libpspp/range-map.h>
#include <libpspp/range-set.h>
#include <libpspp/taint.h>
#include <libpspp/tower.h>

#include "minmax.h"
#include "md4.h"
#include "xalloc.h"

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

static struct source *source_create_empty (size_t column_cnt);
static struct source *source_create_casereader (struct casereader *);
static struct source *source_clone (const struct source *);
static void source_destroy (struct source *);

static casenumber source_get_backing_row_cnt (const struct source *);
static size_t source_get_column_cnt (const struct source *);

static bool source_read (const struct source *,
                         casenumber row, size_t column,
                         union value[], size_t value_cnt);
static bool source_write (struct source *,
                          casenumber row, size_t column,
                          const union value[], size_t value_cnt);
static bool source_write_columns (struct source *, size_t start_column,
                                  const union value[], size_t value_cnt);
static bool source_has_backing (const struct source *);
static void source_increase_use (struct source *, size_t delta);
static void source_decrease_use (struct source *, size_t delta);
static bool source_in_use (const struct source *);

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
   sparse_cases.  Casereaders are read-only, so when sources made
   from casereaders need to be modified, it is done "virtually"
   through being overlaid by a sparse_cases. */

/* A datasheet. */
struct datasheet
  {
    /* Mappings from logical to physical columns/rows. */
    struct axis *columns;
    struct axis *rows;

    /* Mapping from physical columns to "source_info"s. */
    struct range_map sources;

    /* Minimum number of columns to put in a new source when we
       need new columns and none are free.  We double it whenever
       we add a new source to keep the number of file descriptors
       needed by the datasheet to a minimum, reducing the
       likelihood of running out. */
    unsigned column_min_alloc;

    /* Indicates corrupted data in the datasheet. */
    struct taint *taint;
  };

/* Maps from a range of physical columns to a source. */
struct source_info
  {
    struct range_map_node column_range;
    struct source *source;
  };

/* Is this operation a read or a write? */
enum rw_op
  {
    OP_READ,
    OP_WRITE
  };

static void free_source_info (struct datasheet *, struct source_info *);
static struct source_info *source_info_from_range_map (
  struct range_map_node *);
static bool rw_case (struct datasheet *ds, enum rw_op op,
                     casenumber lrow, size_t start_column, size_t column_cnt,
                     union value data[]);

/* Creates and returns a new datasheet.

   If READER is nonnull, then the datasheet initially contains
   the contents of READER. */
struct datasheet *
datasheet_create (struct casereader *reader)
{
  /* Create datasheet. */
  struct datasheet *ds = xmalloc (sizeof *ds);
  ds->columns = axis_create ();
  ds->rows = axis_create ();
  range_map_init (&ds->sources);
  ds->column_min_alloc = 1;
  ds->taint = taint_create ();

  /* Add backing. */
  if (reader != NULL)
    {
      size_t column_cnt;
      casenumber row_cnt;
      struct source_info *si;

      si = xmalloc (sizeof *si);
      si->source = source_create_casereader (reader);
      column_cnt = source_get_column_cnt (si->source);
      row_cnt = source_get_backing_row_cnt (si->source);
      source_increase_use (si->source, column_cnt);

      if ( column_cnt > 0 )
	{
	  unsigned long int column_start;
      column_start = axis_extend (ds->columns, column_cnt);
      axis_insert (ds->columns, 0, column_start, column_cnt);
      range_map_insert (&ds->sources, column_start, column_cnt,
                        &si->column_range);
	}

      if ( row_cnt > 0 )
	{
	  unsigned long int row_start;
      row_start = axis_extend (ds->rows, row_cnt);
      axis_insert (ds->rows, 0, row_start, row_cnt);
	}
    }

  return ds;
}

/* Destroys datasheet DS. */
void
datasheet_destroy (struct datasheet *ds)
{
  if (ds == NULL)
    return;

  axis_destroy (ds->columns);
  axis_destroy (ds->rows);
  while (!range_map_is_empty (&ds->sources))
    {
      struct range_map_node *r = range_map_first (&ds->sources);
      struct source_info *si = source_info_from_range_map (r);
      free_source_info (ds, si);
    }
  taint_destroy (ds->taint);
  free (ds);
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
datasheet_get_row_cnt (const struct datasheet *ds)
{
  return axis_get_size (ds->rows);
}

/* Returns the number of columns in DS. */
size_t
datasheet_get_column_cnt (const struct datasheet *ds)
{
  return axis_get_size (ds->columns);
}

/* Inserts CNT columns into datasheet DS just before column
   BEFORE.  Initializes the contents of each row in the inserted
   columns to the CNT values in INIT_VALUES.

   Returns true if successful, false on failure.  In case of
   failure, the datasheet is unchanged. */
bool
datasheet_insert_columns (struct datasheet *ds,
                          const union value init_values[], size_t cnt,
                          size_t before)
{
  size_t added = 0;
  while (cnt > 0)
    {
      unsigned long first_phy; /* First allocated physical column. */
      unsigned long phy_cnt;   /* Number of allocated physical columns. */

      /* Allocate physical columns from the pool of available
         columns. */
      if (!axis_allocate (ds->columns, cnt, &first_phy, &phy_cnt))
        {
          /* No columns were available.  Create a new source and
             extend the axis to make some new ones available. */
          struct source_info *si;

          phy_cnt = MAX (cnt, ds->column_min_alloc);
          first_phy = axis_extend (ds->columns, phy_cnt);
          ds->column_min_alloc = MIN (65536, ds->column_min_alloc * 2);

          si = xmalloc (sizeof *si);
          si->source = source_create_empty (phy_cnt);
          range_map_insert (&ds->sources, first_phy, phy_cnt,
                            &si->column_range);
          if (phy_cnt > cnt)
            {
              axis_make_available (ds->columns, first_phy + cnt,
                                   phy_cnt - cnt);
              phy_cnt = cnt;
            }
        }

      /* Initialize the columns and insert them into the columns
         axis. */
      while (phy_cnt > 0)
        {
          struct range_map_node *r; /* Range map holding FIRST_PHY column. */
          struct source_info *s;    /* Source containing FIRST_PHY column. */
          size_t source_avail;      /* Number of phys columns available. */
          size_t source_cnt;        /* Number of phys columns to use. */

          /* Figure out how many columns we can and want to take
             starting at FIRST_PHY, and then insert them into the
             columns axis. */
          r = range_map_lookup (&ds->sources, first_phy);
          s = source_info_from_range_map (r);
          source_avail = range_map_node_get_end (r) - first_phy;
          source_cnt = MIN (phy_cnt, source_avail);
          axis_insert (ds->columns, before, first_phy, source_cnt);

          /* Initialize the data for those columns in the
             source. */
          if (!source_write_columns (s->source,
                                     first_phy - range_map_node_get_start (r),
                                     init_values, source_cnt))
            {
              datasheet_delete_columns (ds, before - added,
                                        source_cnt + added);
              taint_set_taint (ds->taint);
              return false;
            }
          source_increase_use (s->source, source_cnt);

          /* Advance. */
          phy_cnt -= source_cnt;
          first_phy += source_cnt;
          init_values += source_cnt;
          cnt -= source_cnt;
          before += source_cnt;
          added += source_cnt;
        }
    }
  return true;
}

/* Deletes the CNT columns in DS starting from column START. */
void
datasheet_delete_columns (struct datasheet *ds, size_t start, size_t cnt)
{
  size_t lcol;

  assert ( start + cnt <= axis_get_size (ds->columns) );

  /* Free up columns for reuse. */
  for (lcol = start; lcol < start + cnt; lcol++)
    {
      size_t pcol = axis_map (ds->columns, lcol);
      struct range_map_node *r = range_map_lookup (&ds->sources, pcol);
      struct source_info *si = source_info_from_range_map (r);

      source_decrease_use (si->source, 1);
      if (source_has_backing (si->source))
        {
          if (!source_in_use (si->source))
            free_source_info (ds, si);
        }
      else
        axis_make_available (ds->columns, pcol, 1);
    }

  /* Remove columns from logical-to-physical mapping. */
  axis_remove (ds->columns, start, cnt);
}

/* Moves the CNT columns in DS starting at position OLD_START so
   that they then start at position NEW_START.  Equivalent to
   deleting the column rows, then inserting them at what becomes
   position NEW_START after the deletion.*/
void
datasheet_move_columns (struct datasheet *ds,
                        size_t old_start, size_t new_start,
                        size_t cnt)
{
  axis_move (ds->columns, old_start, new_start, cnt);
}

/* Retrieves the contents of the given ROW in datasheet DS into
   newly created case C.  Returns true if successful, false on
   I/O error. */
bool
datasheet_get_row (const struct datasheet *ds, casenumber row, struct ccase *c)
{
  size_t column_cnt = datasheet_get_column_cnt (ds);
  case_create (c, column_cnt);
  if (rw_case ((struct datasheet *) ds, OP_READ,
               row, 0, column_cnt, case_data_all_rw (c)))
    return true;
  else
    {
      case_destroy (c);
      return false;
    }
}

/* Stores the contents of case C, which is destroyed, into the
   given ROW in DS.  Returns true on success, false on I/O error.
   On failure, the given ROW might be partially modified or
   corrupted. */
bool
datasheet_put_row (struct datasheet *ds, casenumber row, struct ccase *c)
{
  size_t column_cnt = datasheet_get_column_cnt (ds);
  bool ok = rw_case (ds, OP_WRITE, row, 0, column_cnt,
                     (union value *) case_data_all (c));
  case_destroy (c);
  return ok;
}

/* Stores the values of the WIDTH columns in DS in the given ROW
   starting at COLUMN in DS into VALUES.  Returns true if
   successful, false on I/O error. */
bool
datasheet_get_value (const struct datasheet *ds, casenumber row, size_t column,
                     union value *value, int width)
{
  assert ( row >= 0 );
  return rw_case ((struct datasheet *) ds,
                  OP_READ, row, column, value_cnt_from_width (width), value);
}

/* Stores the WIDTH given VALUES into the given ROW in DS
   starting at COLUMN.  Returns true if successful, false on I/O
   error.  On failure, the given ROW might be partially modified
   or corrupted. */
bool
datasheet_put_value (struct datasheet *ds, casenumber row, size_t column,
                     const union value *value, int width)
{
  return rw_case (ds, OP_WRITE, row, column, value_cnt_from_width (width),
                  (union value *) value);
}

/* Inserts the CNT cases at C, which are destroyed, into
   datasheet DS just before row BEFORE.  Returns true if
   successful, false on I/O error.  On failure, datasheet DS is
   not modified. */
bool
datasheet_insert_rows (struct datasheet *ds,
                       casenumber before, struct ccase c[],
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
        if (!datasheet_put_row (ds, before + i, &c[i]))
          {
            while (++i < cnt)
              case_destroy (&c[i]);
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
  reader = casereader_create_random (datasheet_get_column_cnt (ds),
                                     datasheet_get_row_cnt (ds),
                                     &datasheet_reader_class, ds);
  taint_propagate (datasheet_get_taint (ds), casereader_get_taint (reader));
  return reader;
}

/* "read" function for the datasheet random casereader. */
static bool
datasheet_reader_read (struct casereader *reader UNUSED, void *ds_,
                       casenumber case_idx, struct ccase *c)
{
  struct datasheet *ds = ds_;
  if (case_idx >= datasheet_get_row_cnt (ds))
    return false;
  else if (datasheet_get_row (ds, case_idx, c))
    return true;
  else
    {
      taint_set_taint (ds->taint);
      return false;
    }
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

/* Removes SI from DS's set of sources and destroys its
   source. */
static void
free_source_info (struct datasheet *ds, struct source_info *si)
{
  range_map_delete (&ds->sources, &si->column_range);
  source_destroy (si->source);
  free (si);
}

static struct source_info *
source_info_from_range_map (struct range_map_node *node)
{
  return range_map_data (node, struct source_info, column_range);
}

/* Reads (if OP is OP_READ) or writes (if op is OP_WRITE) the
   COLUMN_CNT columns starting from column START_COLUMN in row
   LROW to/from the COLUMN_CNT values in DATA. */
static bool
rw_case (struct datasheet *ds, enum rw_op op,
         casenumber lrow, size_t start_column, size_t column_cnt,
         union value data[])
{
  casenumber prow;
  size_t lcol;

  assert (lrow < datasheet_get_row_cnt (ds));
  assert (column_cnt <= datasheet_get_column_cnt (ds));
  assert (start_column + column_cnt <= datasheet_get_column_cnt (ds));

  prow = axis_map (ds->rows, lrow);
  for (lcol = start_column; lcol < start_column + column_cnt; lcol++, data++)
    {
      size_t pcol = axis_map (ds->columns, lcol);
      struct range_map_node *r = range_map_lookup (&ds->sources, pcol);
      struct source_info *s = source_info_from_range_map (r);
      size_t pcol_ofs = pcol - range_map_node_get_start (r);
      if (!(op == OP_READ
            ? source_read (s->source, prow, pcol_ofs, data, 1)
            : source_write (s->source, prow, pcol_ofs, data, 1)))
        {
          taint_set_taint (ds->taint);
          return false;
        }
    }
  return true;
}

/* An axis.

   An axis has two functions.  First, it maintains a mapping from
   logical (client-visible) to physical (storage) ordinates.  The
   axis_map and axis_get_size functions inspect this mapping, and
   the axis_insert, axis_remove, and axis_move functions modify
   it.  Second, it tracks the set of ordinates that are unused
   and available for reuse.  (Not all unused ordinates are
   available for reuse: in particular, unused columns that are
   backed by a casereader are never reused.)  The axis_allocate,
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
      unsigned long int size = tower_node_get_height (node);
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
      unsigned long int size = tower_node_get_height (tn);

      md4_process_bytes (&phy_start, sizeof phy_start, ctx);
      md4_process_bytes (&size, sizeof size, ctx);
    }

  for (rsn = range_set_first (axis->available); rsn != NULL;
       rsn = range_set_next (axis->available, rsn))
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
  range_set_insert (axis->available, start, width);
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
      unsigned long int size_2 = tower_node_get_height (group_node) - size_1;
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
      unsigned long this_height = tower_node_get_height (node);

      if (group->phy_start + this_height == next_group->phy_start)
        {
          unsigned long next_height = tower_node_get_height (next);
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
      unsigned long prev_height = tower_node_get_height (prev);

      if (prev_group->phy_start + prev_height == group->phy_start)
        {
          unsigned long this_height = tower_node_get_height (node);
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
        unsigned long prev_height = tower_node_get_height (prev);
        struct axis_group *node_group = axis_group_from_tower_node (node);
        assert (prev_group->phy_start + prev_height != node_group->phy_start);
      }
#endif
}

/* A source. */
struct source
  {
    size_t columns_used;        /* Number of columns in use by client. */
    struct sparse_cases *data;  /* Data at top level, atop the backing. */
    struct casereader *backing; /* Backing casereader (or null). */
    casenumber backing_rows;    /* Number of rows in backing (if nonnull). */
  };

/* Creates and returns an empty, unbacked source with COLUMN_CNT
   columns and an initial "columns_used" of 0. */
static struct source *
source_create_empty (size_t column_cnt)
{
  struct source *source = xmalloc (sizeof *source);
  source->columns_used = 0;
  source->data = sparse_cases_create (column_cnt);
  source->backing = NULL;
  source->backing_rows = 0;
  return source;
}

/* Creates and returns a new source backed by READER and with the
   same initial dimensions and content. */
static struct source *
source_create_casereader (struct casereader *reader)
{
  struct source *source
    = source_create_empty (casereader_get_value_cnt (reader));
  source->backing = reader;
  source->backing_rows = casereader_count_cases (reader);
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
  new->columns_used = old->columns_used;
  new->data = sparse_cases_clone (old->data);
  new->backing = old->backing != NULL ? casereader_clone (old->backing) : NULL;
  new->backing_rows = old->backing_rows;
  if (new->data == NULL)
    {
      source_destroy (new);
      new = NULL;
    }
  return new;
}

/* Increases the columns_used count of SOURCE by DELTA.
   The new value must not exceed SOURCE's number of columns. */
static void
source_increase_use (struct source *source, size_t delta)
{
  source->columns_used += delta;
  assert (source->columns_used <= sparse_cases_get_value_cnt (source->data));
}

/* Decreases the columns_used count of SOURCE by DELTA.
   This must not attempt to decrease the columns_used count below
   zero. */
static void
source_decrease_use (struct source *source, size_t delta)
{
  assert (delta <= source->columns_used);
  source->columns_used -= delta;
}

/* Returns true if SOURCE has any columns in use,
   false otherwise. */
static bool
source_in_use (const struct source *source)
{
  return source->columns_used > 0;
}

/* Destroys SOURCE and its data and backing, if any. */
static void
source_destroy (struct source *source)
{
  if (source != NULL)
    {
      sparse_cases_destroy (source->data);
      casereader_destroy (source->backing);
      free (source);
    }
}

/* Returns the number of rows in SOURCE's backing casereader
   (SOURCE must have a backing casereader). */
static casenumber
source_get_backing_row_cnt (const struct source *source)
{
  assert (source_has_backing (source));
  return source->backing_rows;
}

/* Returns the number of columns in SOURCE. */
static size_t
source_get_column_cnt (const struct source *source)
{
  return sparse_cases_get_value_cnt (source->data);
}

/* Reads VALUE_CNT columns from SOURCE in the given ROW, starting
   from COLUMN, into VALUES.  Returns true if successful, false
   on I/O error. */
static bool
source_read (const struct source *source,
             casenumber row, size_t column,
             union value values[], size_t value_cnt)
{
  if (source->backing == NULL || sparse_cases_contains_row (source->data, row))
    return sparse_cases_read (source->data, row, column, values, value_cnt);
  else
    {
      struct ccase c;
      bool ok;

      assert (source->backing != NULL);
      ok = casereader_peek (source->backing, row, &c);
      if (ok)
        {
          case_copy_out (&c, column, values, value_cnt);
          case_destroy (&c);
        }
      return ok;
    }
}

/* Writes the VALUE_CNT values in VALUES to SOURCE in the given
   ROW, starting at ROW.  Returns true if successful, false on
   I/O error.  On error, the row's data may be completely or
   partially corrupted, both inside and outside the region to be
   written.  */
static bool
source_write (struct source *source,
              casenumber row, size_t column,
              const union value values[], size_t value_cnt)
{
  size_t column_cnt = sparse_cases_get_value_cnt (source->data);
  bool ok;

  if (source->backing == NULL
      || (column == 0 && value_cnt == column_cnt)
      || sparse_cases_contains_row (source->data, row))
    ok = sparse_cases_write (source->data, row, column, values, value_cnt);
  else
    {
      struct ccase c;
      if (row < source->backing_rows)
        ok = casereader_peek (source->backing, row, &c);
      else
        {
          /* It's not one of the backed rows.  Ideally, this
             should never happen: we'd always be writing the full
             contents of new, unbacked rows in a single call to
             this function, so that the first case above would
             trigger.  But that's a little difficult at higher
             levels, so that we in fact usually write the full
             contents of new, unbacked rows in multiple calls to
             this function.  Make this work. */
          case_create (&c, column_cnt);
          ok = true;
        }
      if (ok)
        {
          case_copy_in (&c, column, values, value_cnt);
          ok = sparse_cases_write (source->data, row, 0,
                                   case_data_all (&c), column_cnt);
          case_destroy (&c);
        }
    }
  return ok;
}

/* Within SOURCE, which must not have a backing casereader,
   writes the VALUE_CNT values in VALUES_CNT to the VALUE_CNT
   columns starting from START_COLUMN, in every row, even in rows
   not yet otherwise initialized.  Returns true if successful,
   false if an I/O error occurs.

   We don't support backing != NULL because (1) it's harder and
   (2) source_write_columns is only called by
   datasheet_insert_columns, which doesn't reuse columns from
   sources that are backed by casereaders. */
static bool
source_write_columns (struct source *source, size_t start_column,
                      const union value values[], size_t value_cnt)
{
  assert (source->backing == NULL);

  return sparse_cases_write_columns (source->data, start_column,
                                     values, value_cnt);
}

/* Returns true if SOURCE has a backing casereader, false
   otherwise. */
static bool
source_has_backing (const struct source *source)
{
  return source->backing != NULL;
}

/* Datasheet model checker test driver. */

/* Maximum size of datasheet supported for model checking
   purposes. */
#define MAX_ROWS 5
#define MAX_COLS 5

/* Hashes the structure of datasheet DS and returns the hash.
   We use MD4 because it is much faster than MD5 or SHA-1 but its
   collision resistance is just as good. */
static unsigned int
hash_datasheet (const struct datasheet *ds)
{
  unsigned int hash[DIV_RND_UP (20, sizeof (unsigned int))];
  struct md4_ctx ctx;
  struct range_map_node *r;

  md4_init_ctx (&ctx);
  axis_hash (ds->columns, &ctx);
  axis_hash (ds->rows, &ctx);
  for (r = range_map_first (&ds->sources); r != NULL;
       r = range_map_next (&ds->sources, r))
    {
      unsigned long int start = range_map_node_get_start (r);
      unsigned long int end = range_map_node_get_end (r);
      md4_process_bytes (&start, sizeof start, &ctx);
      md4_process_bytes (&end, sizeof end, &ctx);
    }
  md4_process_bytes (&ds->column_min_alloc, sizeof ds->column_min_alloc,
                      &ctx);
  md4_finish_ctx (&ctx, hash);
  return hash[0];
}

/* Checks that datasheet DS contains has ROW_CNT rows, COLUMN_CNT
   columns, and the same contents as ARRAY, reporting any
   mismatches via mc_error.  Then, adds DS to MC as a new state. */
static void
check_datasheet (struct mc *mc, struct datasheet *ds,
                 double array[MAX_ROWS][MAX_COLS],
                 size_t row_cnt, size_t column_cnt)
{
  assert (row_cnt < MAX_ROWS);
  assert (column_cnt < MAX_COLS);

  /* If it is a duplicate hash, discard the state before checking
     its consistency, to save time. */
  if (mc_discard_dup_state (mc, hash_datasheet (ds)))
    {
      datasheet_destroy (ds);
      return;
    }

  if (row_cnt != datasheet_get_row_cnt (ds))
    mc_error (mc, "row count (%lu) does not match expected (%zu)",
              (unsigned long int) datasheet_get_row_cnt (ds), row_cnt);
  else if (column_cnt != datasheet_get_column_cnt (ds))
    mc_error (mc, "column count (%lu) does not match expected (%zu)",
              (unsigned long int) datasheet_get_column_cnt (ds), column_cnt);
  else
    {
      size_t row, col;

      for (row = 0; row < row_cnt; row++)
        for (col = 0; col < column_cnt; col++)
          {
            union value v;
            if (!datasheet_get_value (ds, row, col, &v, 1))
              NOT_REACHED ();
            if (v.f != array[row][col])
              mc_error (mc, "element %zu,%zu (of %zu,%zu) differs: %g != %g",
                        row, col, row_cnt, column_cnt, v.f, array[row][col]);
          }
    }

  mc_add_state (mc, ds);
}

/* Extracts the contents of DS into DATA. */
static void
extract_data (const struct datasheet *ds, double data[MAX_ROWS][MAX_COLS])
{
  size_t column_cnt = datasheet_get_column_cnt (ds);
  size_t row_cnt = datasheet_get_row_cnt (ds);
  size_t row, col;

  assert (row_cnt < MAX_ROWS);
  assert (column_cnt < MAX_COLS);
  for (row = 0; row < row_cnt; row++)
    for (col = 0; col < column_cnt; col++)
      {
        union value v;
        if (!datasheet_get_value (ds, row, col, &v, 1))
          NOT_REACHED ();
        data[row][col] = v.f;
      }
}

/* Clones the structure and contents of ODS into *DS,
   and the contents of ODATA into DATA. */
static void
clone_model (const struct datasheet *ods, double odata[MAX_ROWS][MAX_COLS],
             struct datasheet **ds_, double data[MAX_ROWS][MAX_COLS])
{
  struct datasheet *ds;
  struct range_map_node *r;

  /* Clone ODS into DS. */
  ds = *ds_ = xmalloc (sizeof *ds);
  ds->columns = axis_clone (ods->columns);
  ds->rows = axis_clone (ods->rows);
  range_map_init (&ds->sources);
  for (r = range_map_first (&ods->sources); r != NULL;
       r = range_map_next (&ods->sources, r))
    {
      const struct source_info *osi = source_info_from_range_map (r);
      struct source_info *si = xmalloc (sizeof *si);
      si->source = source_clone (osi->source);
      range_map_insert (&ds->sources, range_map_node_get_start (r),
                        range_map_node_get_width (r), &si->column_range);
    }
  ds->column_min_alloc = ods->column_min_alloc;
  ds->taint = taint_create ();

  /* Clone ODATA into DATA. */
  memcpy (data, odata, MAX_ROWS * MAX_COLS * sizeof **data);
}

/* "init" function for struct mc_class. */
static void
datasheet_mc_init (struct mc *mc)
{
  struct datasheet_test_params *params = mc_get_aux (mc);
  struct datasheet *ds;

  if (params->backing_rows == 0 && params->backing_cols == 0)
    {
      /* Create unbacked datasheet. */
      ds = datasheet_create (NULL);
      mc_name_operation (mc, "empty datasheet");
      check_datasheet (mc, ds, NULL, 0, 0);
    }
  else
    {
      /* Create datasheet with backing. */
      struct casewriter *writer;
      struct casereader *reader;
      double data[MAX_ROWS][MAX_COLS];
      int row;

      assert (params->backing_rows > 0 && params->backing_rows <= MAX_ROWS);
      assert (params->backing_cols > 0 && params->backing_cols <= MAX_COLS);

      writer = mem_writer_create (params->backing_cols);
      for (row = 0; row < params->backing_rows; row++)
        {
          struct ccase c;
          int col;

          case_create (&c, params->backing_cols);
          for (col = 0; col < params->backing_cols; col++)
            {
              double value = params->next_value++;
              data[row][col] = value;
              case_data_rw_idx (&c, col)->f = value;
            }
          casewriter_write (writer, &c);
        }
      reader = casewriter_make_reader (writer);
      assert (reader != NULL);

      ds = datasheet_create (reader);
      mc_name_operation (mc, "datasheet with (%d,%d) backing",
                         params->backing_rows, params->backing_cols);
      check_datasheet (mc, ds, data,
                       params->backing_rows, params->backing_cols);
    }
}

/* "mutate" function for struct mc_class. */
static void
datasheet_mc_mutate (struct mc *mc, const void *ods_)
{
  struct datasheet_test_params *params = mc_get_aux (mc);

  const struct datasheet *ods = ods_;
  double odata[MAX_ROWS][MAX_COLS];
  double data[MAX_ROWS][MAX_COLS];
  size_t column_cnt = datasheet_get_column_cnt (ods);
  size_t row_cnt = datasheet_get_row_cnt (ods);
  size_t pos, new_pos, cnt;

  extract_data (ods, odata);

  /* Insert all possible numbers of columns in all possible
     positions. */
  for (pos = 0; pos <= column_cnt; pos++)
    for (cnt = 0; cnt <= params->max_cols - column_cnt; cnt++)
      if (mc_include_state (mc))
        {
          struct datasheet *ds;
          union value new[MAX_COLS];
          size_t i, j;

          mc_name_operation (mc, "insert %zu columns at %zu", cnt, pos);
          clone_model (ods, odata, &ds, data);

          for (i = 0; i < cnt; i++)
            new[i].f = params->next_value++;

          if (!datasheet_insert_columns (ds, new, cnt, pos))
            mc_error (mc, "datasheet_insert_columns failed");

          for (i = 0; i < row_cnt; i++)
            {
              insert_range (&data[i][0], column_cnt, sizeof data[i][0],
                            pos, cnt);
              for (j = 0; j < cnt; j++)
                data[i][pos + j] = new[j].f;
            }

          check_datasheet (mc, ds, data, row_cnt, column_cnt + cnt);
        }

  /* Delete all possible numbers of columns from all possible
     positions. */
  for (pos = 0; pos < column_cnt; pos++)
    for (cnt = 0; cnt < column_cnt - pos; cnt++)
      if (mc_include_state (mc))
        {
          struct datasheet *ds;
          size_t i;

          mc_name_operation (mc, "delete %zu columns at %zu", cnt, pos);
          clone_model (ods, odata, &ds, data);

          datasheet_delete_columns (ds, pos, cnt);

          for (i = 0; i < row_cnt; i++)
            remove_range (&data[i], column_cnt, sizeof *data[i], pos, cnt);

          check_datasheet (mc, ds, data, row_cnt, column_cnt - cnt);
        }

  /* Move all possible numbers of columns from all possible
     existing positions to all possible new positions. */
  for (pos = 0; pos < column_cnt; pos++)
    for (cnt = 0; cnt < column_cnt - pos; cnt++)
      for (new_pos = 0; new_pos < column_cnt - cnt; new_pos++)
        if (mc_include_state (mc))
          {
            struct datasheet *ds;
            size_t i;

            clone_model (ods, odata, &ds, data);
            mc_name_operation (mc, "move %zu columns from %zu to %zu",
                               cnt, pos, new_pos);

            datasheet_move_columns (ds, pos, new_pos, cnt);

            for (i = 0; i < row_cnt; i++)
              move_range (&data[i], column_cnt, sizeof data[i][0],
                          pos, new_pos, cnt);

            check_datasheet (mc, ds, data, row_cnt, column_cnt);
          }

  /* Insert all possible numbers of rows in all possible
     positions. */
  for (pos = 0; pos <= row_cnt; pos++)
    for (cnt = 0; cnt <= params->max_rows - row_cnt; cnt++)
      if (mc_include_state (mc))
        {
          struct datasheet *ds;
          struct ccase c[MAX_ROWS];
          size_t i, j;

          clone_model (ods, odata, &ds, data);
          mc_name_operation (mc, "insert %zu rows at %zu", cnt, pos);

          for (i = 0; i < cnt; i++)
            {
              case_create (&c[i], column_cnt);
              for (j = 0; j < column_cnt; j++)
                case_data_rw_idx (&c[i], j)->f = params->next_value++;
            }

          insert_range (data, row_cnt, sizeof data[pos], pos, cnt);
          for (i = 0; i < cnt; i++)
            for (j = 0; j < column_cnt; j++)
              data[i + pos][j] = case_num_idx (&c[i], j);

          if (!datasheet_insert_rows (ds, pos, c, cnt))
            mc_error (mc, "datasheet_insert_rows failed");

          check_datasheet (mc, ds, data, row_cnt + cnt, column_cnt);
        }

  /* Delete all possible numbers of rows from all possible
     positions. */
  for (pos = 0; pos < row_cnt; pos++)
    for (cnt = 0; cnt < row_cnt - pos; cnt++)
      if (mc_include_state (mc))
        {
          struct datasheet *ds;

          clone_model (ods, odata, &ds, data);
          mc_name_operation (mc, "delete %zu rows at %zu", cnt, pos);

          datasheet_delete_rows (ds, pos, cnt);

          remove_range (&data[0], row_cnt, sizeof data[0], pos, cnt);

          check_datasheet (mc, ds, data, row_cnt - cnt, column_cnt);
        }

  /* Move all possible numbers of rows from all possible existing
     positions to all possible new positions. */
  for (pos = 0; pos < row_cnt; pos++)
    for (cnt = 0; cnt < row_cnt - pos; cnt++)
      for (new_pos = 0; new_pos < row_cnt - cnt; new_pos++)
        if (mc_include_state (mc))
          {
            struct datasheet *ds;

            clone_model (ods, odata, &ds, data);
            mc_name_operation (mc, "move %zu rows from %zu to %zu",
                               cnt, pos, new_pos);

            datasheet_move_rows (ds, pos, new_pos, cnt);

            move_range (&data[0], row_cnt, sizeof data[0],
                        pos, new_pos, cnt);

            check_datasheet (mc, ds, data, row_cnt, column_cnt);
          }
}

/* "destroy" function for struct mc_class. */
static void
datasheet_mc_destroy (const struct mc *mc UNUSED, void *ds_)
{
  struct datasheet *ds = ds_;
  datasheet_destroy (ds);
}

/* Executes the model checker on the datasheet test driver with
   the given OPTIONS and passing in the given PARAMS, which must
   point to a modifiable "struct datasheet_test_params".  If any
   value in PARAMS is out of range, it will be adjusted into the
   valid range before running the test.

   Returns the results of the model checking run. */
struct mc_results *
datasheet_test (struct mc_options *options, void *params_)
{
  struct datasheet_test_params *params = params_;
  static const struct mc_class datasheet_mc_class =
    {
      datasheet_mc_init,
      datasheet_mc_mutate,
      datasheet_mc_destroy,
    };

  params->next_value = 1;
  params->max_rows = MIN (params->max_rows, MAX_ROWS);
  params->max_cols = MIN (params->max_cols, MAX_COLS);
  params->backing_rows = MIN (params->backing_rows, params->max_rows);
  params->backing_cols = MIN (params->backing_cols, params->max_cols);

  mc_options_set_aux (options, params);
  return mc_run (&datasheet_mc_class, options);
}
