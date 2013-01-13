/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include <libpspp/str.h>
#include <stdio.h>
#include <string.h>


struct spreadsheet * 
spreadsheet_open (const char *filename)
{
  struct spreadsheet *ss = NULL;

  ss = gnumeric_probe (filename);
  
  return ss;
}

void 
spreadsheet_close (struct spreadsheet *spreadsheet)
{
}



/* Convert a string, which is an integer encoded in base26
   IE, A=0, B=1, ... Z=25 to the integer it represents.
   ... except that in this scheme, digits with an exponent
   greater than 1 are implicitly incremented by 1, so
   AA  = 0 + 1*26, AB = 1 + 1*26,
   ABC = 2 + 2*26 + 1*26^2 ....
*/
int
pseudo_base26 (const char *str)
{
  int i;
  int multiplier = 1;
  int result = 0;
  int len = strlen (str);

  for ( i = len - 1 ; i >= 0; --i)
    {
      int mantissa = (str[i] - 'A');

      if ( mantissa < 0 || mantissa > 25 )
	return -1;

      if ( i != len - 1)
	mantissa++;

      result += mantissa * multiplier;

      multiplier *= 26;
    }

  return result;
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
  *col0 = pseudo_base26 (startcol);
  str_uppercase (stopcol);
  *coli = pseudo_base26 (stopcol);
  *row0 = startrow - 1;
  *rowi = stoprow - 1 ;

  return true;
}

