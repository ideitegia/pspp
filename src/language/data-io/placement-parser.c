/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
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

#include <config.h>

#include <language/data-io/placement-parser.h>

#include <assert.h>

#include <language/lexer/format-parser.h>
#include <language/lexer/lexer.h>
#include <libpspp/message.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>

#include "xalloc.h"
#include "xsize.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Extensions to the format specifiers used only for
   placement. */
enum 
  {
    PRS_TYPE_T = SCHAR_MAX - 3, /* Tab to absolute column. */
    PRS_TYPE_X,                 /* Skip columns. */
    PRS_TYPE_NEW_REC            /* Next record. */
  };

static bool fixed_parse_columns (struct pool *, size_t var_cnt, bool for_input,
                                 struct fmt_spec **, size_t *);
static bool fixed_parse_fortran (struct pool *, bool for_input,
                                 struct fmt_spec **, size_t *);

/* Parses Fortran-like or column-based specifications for placing
   variable data in fixed positions in columns and rows, that is,
   formats like those parsed by DATA LIST or PRINT.  Returns true
   only if successful.

   If successful, formats for VAR_CNT variables are stored in
   *FORMATS, and the number of formats required is stored in
   *FORMAT_CNT.  *FORMAT_CNT may be greater than VAR_CNT because
   of T, X, and / "formats", but success guarantees that exactly
   VAR_CNT variables will be placed by the output formats.  The
   caller should call execute_placement_format to process those
   "formats" in interpreting the output.

   Uses POOL for allocation.  When the caller is finished
   interpreting *FORMATS, POOL may be destroyed. */
bool
parse_var_placements (struct pool *pool, size_t var_cnt, bool for_input,
                      struct fmt_spec **formats, size_t *format_cnt) 
{
  assert (var_cnt > 0);
  if (lex_is_number ())
    return fixed_parse_columns (pool, var_cnt, for_input, formats, format_cnt);
  else if (lex_match ('(')) 
    {
      size_t assignment_cnt;
      size_t i;

      if (!fixed_parse_fortran (pool, for_input, formats, format_cnt))
        return false; 

      assignment_cnt = 0;
      for (i = 0; i < *format_cnt; i++)
        assignment_cnt += (*formats)[i].type < FMT_NUMBER_OF_FORMATS;

      if (assignment_cnt != var_cnt)
        {
          msg (SE, _("Number of variables specified (%d) "
                     "differs from number of variable formats (%d)."),
               (int) var_cnt, (int) assignment_cnt);
          return false;
        }

      return true;
    }
  else
    {
      msg (SE, _("SPSS-like or Fortran-like format "
                 "specification expected after variable names."));
      return false;
    }
}

/* Implements parse_var_placements for column-based formats. */
static bool
fixed_parse_columns (struct pool *pool, size_t var_cnt, bool for_input,
                     struct fmt_spec **formats, size_t *format_cnt)
{
  struct fmt_spec format;
  int fc, lc;
  size_t i;

  if (!parse_column_range (&fc, &lc, NULL))
    return false;

  /* Divide columns evenly. */    
  format.w = (lc - fc + 1) / var_cnt;
  if ((lc - fc + 1) % var_cnt)
    {
      msg (SE, _("The %d columns %d-%d "
		 "can't be evenly divided into %d fields."),
	   lc - fc + 1, fc, lc, var_cnt);
      return false;
    }

  /* Format specifier. */
  if (lex_match ('('))
    {
      /* Get format type. */
      if (token == T_ID)
	{
	  if (!parse_format_specifier_name (&format.type))
            return false;
	  lex_match (',');
	}
      else
	format.type = FMT_F;

      /* Get decimal places. */
      if (lex_is_integer ())
	{
	  format.d = lex_integer ();
	  lex_get ();
	}
      else
	format.d = 0;

      if (!lex_force_match (')'))
	return false;
    }
  else
    {
      format.type = FMT_F;
      format.d = 0;
    }
  if (!fmt_check (&format, for_input))
    return false;

  *formats = pool_nalloc (pool, var_cnt + 1, sizeof **formats);
  *format_cnt = var_cnt + 1;
  (*formats)[0].type = PRS_TYPE_T;
  (*formats)[0].w = fc;
  for (i = 1; i <= var_cnt; i++)
    (*formats)[i] = format;
  return true;
}

/* Implements parse_var_placements for Fortran-like formats. */
static bool
fixed_parse_fortran (struct pool *pool, bool for_input,
                     struct fmt_spec **formats, size_t *format_cnt)
{
  size_t formats_allocated = 0;
  size_t formats_used = 0;

  *formats = NULL;
  while (!lex_match (')'))
    {
      struct fmt_spec f;
      struct fmt_spec *new_formats;
      size_t new_format_cnt;
      size_t count;
      size_t formats_needed;
      
      /* Parse count. */
      if (lex_is_integer ())
	{
	  count = lex_integer ();
	  lex_get ();
	}
      else
	count = 1;

      /* Parse format specifier. */
      if (lex_match ('('))
        {
          /* Call ourselves recursively to handle parentheses. */
          if (!fixed_parse_fortran (pool, for_input,
                                    &new_formats, &new_format_cnt))
            return false;
        }
      else
        {
          new_formats = &f;
          new_format_cnt = 1;
          if (lex_match ('/'))
            f.type = PRS_TYPE_NEW_REC;
          else
            {
              char type[FMT_TYPE_LEN_MAX + 1];
              
              if (!parse_abstract_format_specifier (type, &f.w, &f.d))
                return false;

              if (!strcasecmp (type, "T")) 
                f.type = PRS_TYPE_T;
              else if (!strcasecmp (type, "X")) 
                {
                  f.type = PRS_TYPE_X;
                  f.w = count;
                  count = 1;
                }
              else 
                {
                  if (!fmt_from_name (type, &f.type)) 
                    {
                      msg (SE, _("Unknown format type \"%s\"."), type);
                      return false;
                    }
                  if (!fmt_check (&f, for_input))
                    return false;
                }
            } 
        }

      /* Add COUNT copies of the NEW_FORMAT_CNT formats in
         NEW_FORMATS to FORMATS. */
      if (new_format_cnt != 0
          && size_overflow_p (xtimes (xsum (formats_used,
                                            xtimes (count, new_format_cnt)),
                                      sizeof *formats)))
        xalloc_die ();
      formats_needed = count * new_format_cnt;
      if (formats_used + formats_needed > formats_allocated) 
        {
          formats_allocated = formats_used + formats_needed;
          *formats = pool_2nrealloc (pool, *formats, &formats_allocated,
                                     sizeof **formats);
        }
      for (; count > 0; count--) 
        {
          memcpy (&(*formats)[formats_used], new_formats,
                  sizeof **formats * new_format_cnt);
          formats_used += new_format_cnt;
        }

      lex_match (',');
    }

  *format_cnt = formats_used;
  return true;
}

/* Checks whether FORMAT represents one of the special "formats"
   for T, X, or /.  If so, updates *RECORD or *COLUMN (or both)
   as appropriate, and returns true.  Otherwise, returns false
   without any side effects. */
bool
execute_placement_format (const struct fmt_spec *format,
                          int *record, int *column) 
{
  switch (format->type) 
    {
    case PRS_TYPE_X:
      *column += format->w;
      return true;
      
    case PRS_TYPE_T:
      *column = format->w;
      return true;
      
    case PRS_TYPE_NEW_REC:
      (*record)++;
      *column = 1;
      return true;

    default:
      assert (format->type < FMT_NUMBER_OF_FORMATS);
      return false;
    }
}

/* Parse a column or a range of columns, specified as a single
   integer or two integer delimited by a dash.  Stores the range
   in *FIRST_COLUMN and *LAST_COLUMN.  (If only a single integer
   is given, it is stored in both.)  If RANGE_SPECIFIED is
   non-null, then *RANGE_SPECIFIED is set to true if the syntax
   contained a dash, false otherwise.  Returns true if
   successful, false if the syntax was invalid or the values
   specified did not make sense. */
bool
parse_column_range (int *first_column, int *last_column,
                    bool *range_specified) 
{
  /* First column. */
  if (!lex_force_int ())
    return false;
  *first_column = lex_integer ();
  if (*first_column < 1)
    {
      msg (SE, _("Column positions for fields must be positive."));
      return false;
    }
  lex_get ();

  /* Last column. */
  lex_negative_to_dash ();
  if (lex_match ('-'))
    {
      if (!lex_force_int ())
	return false;
      *last_column = lex_integer ();
      if (*last_column < 1)
	{
	  msg (SE, _("Column positions for fields must be positive."));
	  return false;
	}
      else if (*last_column < *first_column)
	{
	  msg (SE, _("The ending column for a field must be "
		     "greater than the starting column."));
	  return false;
	}

      if (range_specified)
        *range_specified = true;
      lex_get ();
    }
  else 
    {
      *last_column = *first_column;
      if (range_specified)
        *range_specified = false;
    }

  return true;
}

/* Parses a (possibly empty) sequence of slashes, each of which
   may be followed by an integer.  A slash on its own increases
   *RECORD by 1 and sets *COLUMN to 1.  A slash followed by an
   integer sets *RECORD to the integer, as long as that increases
   *RECORD, and sets *COLUMN to 1.

   Returns true if successful, false on syntax error. */
bool
parse_record_placement (int *record, int *column) 
{
  while (lex_match ('/'))
    {
      if (lex_is_integer ())
        {
          if (lex_integer () <= *record)
            {
              msg (SE, _("The record number specified, %ld, is at or "
                         "before the previous record, %d.  Data "
                         "fields must be listed in order of "
                         "increasing record number."),
                   lex_integer (), *record);
              return false;
            }
          *record = lex_integer ();
          lex_get ();
        }
      else
        (*record)++;
      *column = 1;
    }
  assert (*record >= 1);
  
  return true;
}
