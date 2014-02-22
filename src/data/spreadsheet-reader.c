/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011, 2013 Free Software Foundation, Inc.

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

#include "spreadsheet-reader.h"

#include <libpspp/assertion.h>
#include "gnumeric-reader.h"
#include "ods-reader.h"

#include <libpspp/str.h>
#include <stdio.h>
#include <string.h>
#include <gl/xalloc.h>
#include <gl/c-xvasprintf.h>
#include <stdlib.h>

#ifdef ODF_READ_SUPPORT
const bool ODF_READING_SUPPORTED = true;
#else
const bool ODF_READING_SUPPORTED = false;
#endif

#ifdef GNM_READ_SUPPORT
const bool GNM_READING_SUPPORTED = true;
#else
const bool GNM_READING_SUPPORTED = false;
#endif

void 
spreadsheet_destroy (struct spreadsheet *s)
{
  switch (s->type)
    {
    case SPREADSHEET_ODS:
      assert (ODF_READING_SUPPORTED);
      ods_destroy (s);
      break;

    case SPREADSHEET_GNUMERIC:
      assert (GNM_READING_SUPPORTED);
      gnumeric_destroy (s);
      break;
    default:
      NOT_REACHED ();
      break;
    }
}


struct casereader * 
spreadsheet_make_reader (struct spreadsheet *s,
                         const struct spreadsheet_read_options *opts)
{
  if (ODF_READING_SUPPORTED)
    if ( s->type == SPREADSHEET_ODS)
      return ods_make_reader (s, opts);

  if (GNM_READING_SUPPORTED)
    if ( s->type == SPREADSHEET_GNUMERIC)
      return gnumeric_make_reader (s, opts);

  return NULL;
}

const char * 
spreadsheet_get_sheet_name (struct spreadsheet *s, int n)
{
  if (ODF_READING_SUPPORTED)
    if ( s->type == SPREADSHEET_ODS)
      return ods_get_sheet_name (s, n);

  if (GNM_READING_SUPPORTED)
    if ( s->type == SPREADSHEET_GNUMERIC)
      return gnumeric_get_sheet_name (s, n);

  return NULL;
}


char * 
spreadsheet_get_sheet_range (struct spreadsheet *s, int n)
{
  if (ODF_READING_SUPPORTED)
    if ( s->type == SPREADSHEET_ODS)
      return ods_get_sheet_range (s, n);

  if (GNM_READING_SUPPORTED)
    if ( s->type == SPREADSHEET_GNUMERIC)
      return gnumeric_get_sheet_range (s, n);

  return NULL;
}


#define RADIX 26

static void
reverse (char *s, int len)
{
  int i;
  for (i = 0; i < len / 2; ++i)
    {
      char tmp = s[len - i - 1];
      s[len - i -1] = s[i];
      s[i] = tmp;
    }
}


/* Convert a string, which is an integer encoded in base26
   IE, A=0, B=1, ... Z=25 to the integer it represents.
   ... except that in this scheme, digits with an exponent
   greater than 1 are implicitly incremented by 1, so
   AA  = 0 + 1*26, AB = 1 + 1*26,
   ABC = 2 + 2*26 + 1*26^2 ....
*/
int
ps26_to_int (const char *str)
{
  int i;
  int multiplier = 1;
  int result = 0;
  int len = strlen (str);

  for (i = len - 1 ; i >= 0; --i)
    {
      int mantissa = (str[i] - 'A');

      assert (mantissa >= 0);
      assert (mantissa < RADIX);

      if (i != len - 1)
	mantissa++;

      result += mantissa * multiplier;
      multiplier *= RADIX;
    }

  return result;
}

char *
int_to_ps26 (int i)
{
  char *ret = NULL;

  int lower = 0;
  long long int base = RADIX;
  int exp = 1;

  assert (i >= 0);

  while (i > lower + base - 1)
    {
      lower += base;
      base *= RADIX;      
      assert (base > 0);
      exp++;
    }

  i -= lower;
  i += base;

  ret = xmalloc (exp + 1);

  exp = 0;
  do
    {
      ret[exp++] = (i % RADIX) + 'A';
      i /= RADIX;
    }
  while (i > 1);

  ret[exp]='\0';

  reverse (ret, exp);
  return ret;
}


char *
create_cell_ref (int col0, int row0)
{
  char *cs0 ;
  char *s ;

  if ( col0 < 0) return NULL;
  if ( row0 < 0) return NULL;

  cs0 =  int_to_ps26 (col0);
  s =  c_xasprintf ("%s%d", cs0, row0 + 1);

  free (cs0);

  return s;
}

char *
create_cell_range (int col0, int row0, int coli, int rowi)
{
  char *s0 = create_cell_ref (col0, row0);
  char *si = create_cell_ref (coli, rowi);

  char *s =  c_xasprintf ("%s:%s", s0, si);

  free (s0);
  free (si);

  return s;
}


/* Convert a cell reference in the form "A1:B2", to
   integers.  A1 means column zero, row zero.
   B1 means column 1 row 0. AA1 means column 26, row 0.
*/
bool
convert_cell_ref (const char *ref,
		  int *col0, int *row0,
		  int *coli, int *rowi)
{
  char startcol[5];
  char stopcol [5];

  int startrow;
  int stoprow;

  int n = sscanf (ref, "%4[a-zA-Z]%d:%4[a-zA-Z]%d",
	      startcol, &startrow,
	      stopcol, &stoprow);
  if ( n != 4)
    return false;

  str_uppercase (startcol);
  *col0 = ps26_to_int (startcol);
  str_uppercase (stopcol);
  *coli = ps26_to_int (stopcol);
  *row0 = startrow - 1;
  *rowi = stoprow - 1 ;

  return true;
}

