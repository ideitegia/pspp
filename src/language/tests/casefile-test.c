/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
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
#include <data/casefile.h>
#include <data/fastfile.h>

#include <data/case.h>

#include <gsl/gsl_randist.h>
#include <gsl/gsl_rng.h>
#include <stdarg.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <libpspp/assertion.h>

#include "xalloc.h"

static void test_casefile (int pattern, size_t value_cnt, size_t case_cnt);
static void get_random_case (struct ccase *, size_t value_cnt,
                             size_t case_idx);
static void write_random_case (struct casefile *cf, size_t case_idx);
static void read_and_verify_random_case (struct casefile *cf,
                                         struct casereader *reader,
                                         size_t case_idx);
static void test_casereader_clone (struct casereader *reader1, size_t case_cnt);
				 

static void fail_test (const char *message, ...);

int
cmd_debug_casefile (struct dataset *ds UNUSED) 
{
  static const size_t sizes[] =
    {
      1, 2, 3, 4, 5, 6, 7, 14, 15, 16, 17, 31, 55, 73,
      100, 137, 257, 521, 1031, 2053
    };
  int size_max;
  int case_max;
  int pattern;

  size_max = sizeof sizes / sizeof *sizes;
  if (lex_match_id ("SMALL")) 
    {
      size_max -= 4;
      case_max = 511; 
    }
  else
    case_max = 4095;
  if (token != '.')
    return lex_end_of_command ();
    
  for (pattern = 0; pattern < 7; pattern++) 
    {
      const size_t *size;

      for (size = sizes; size < sizes + size_max; size++) 
        {
          size_t case_cnt;

          for (case_cnt = 0; case_cnt <= case_max;
               case_cnt = (case_cnt * 2) + 1)
            test_casefile (pattern, *size, case_cnt);
        }
    }
  printf ("Casefile tests succeeded.\n");
  return CMD_SUCCESS;
}

static void
test_casefile (int pattern, size_t value_cnt, size_t case_cnt) 
{
  struct casefile *cf;
  struct casereader *r1, *r2;
  struct ccase c;
  gsl_rng *rng;
  size_t i, j;

  rng = gsl_rng_alloc (gsl_rng_mt19937);
  cf = fastfile_create (value_cnt);
  if (pattern == 5)
    casefile_to_disk (cf);
  for (i = 0; i < case_cnt; i++)
    write_random_case (cf, i);
  if (pattern == 5)
    casefile_sleep (cf);
  r1 = casefile_get_reader (cf, NULL);
  r2 = casefile_get_reader (cf, NULL);
  switch (pattern) 
    {
    case 0:
    case 5:
      for (i = 0; i < case_cnt; i++) 
        {
          read_and_verify_random_case (cf, r1, i);
          read_and_verify_random_case (cf, r2, i);
        } 
      break;
    case 1:
      for (i = 0; i < case_cnt; i++)
        read_and_verify_random_case (cf, r1, i);
      for (i = 0; i < case_cnt; i++) 
        read_and_verify_random_case (cf, r2, i);
      break;
    case 2:
    case 3:
    case 4:
      for (i = j = 0; i < case_cnt; i++) 
        {
          read_and_verify_random_case (cf, r1, i);
          if (gsl_rng_get (rng) % pattern == 0) 
            read_and_verify_random_case (cf, r2, j++); 
          if (i == case_cnt / 2)
            casefile_to_disk (cf);
        }
      for (; j < case_cnt; j++) 
        read_and_verify_random_case (cf, r2, j);
      break;
    case 6:
      test_casereader_clone (r1, case_cnt);
      test_casereader_clone (r2, case_cnt);
      break;
    default:
      NOT_REACHED ();
    }
  if (casereader_read (r1, &c))
    fail_test ("Casereader 1 not at end of file.");
  if (casereader_read (r2, &c))
    fail_test ("Casereader 2 not at end of file.");
  if (pattern != 1)
    casereader_destroy (r1);
  if (pattern != 2)
    casereader_destroy (r2);
  if (pattern > 2) 
    {
      r1 = casefile_get_destructive_reader (cf);
      for (i = 0; i < case_cnt; i++) 
        {
          struct ccase read_case, expected_case;
          
          get_random_case (&expected_case, value_cnt, i);
          if (!casereader_read_xfer (r1, &read_case)) 
            fail_test ("Premature end of casefile.");
          for (j = 0; j < value_cnt; j++) 
            {
              double a = case_num (&read_case, j);
              double b = case_num (&expected_case, j);
              if (a != b)
                fail_test ("Case %lu fails comparison.", (unsigned long) i); 
            }
          case_destroy (&expected_case);
          case_destroy (&read_case);
        }
      casereader_destroy (r1);
    }
  casefile_destroy (cf);
  gsl_rng_free (rng);
}

static void
get_random_case (struct ccase *c, size_t value_cnt, size_t case_idx) 
{
  int i;
  case_create (c, value_cnt);
  for (i = 0; i < value_cnt; i++)
    case_data_rw (c, i)->f = case_idx % 257 + i;
}

static void
write_random_case (struct casefile *cf, size_t case_idx) 
{
  struct ccase c;
  get_random_case (&c, casefile_get_value_cnt (cf), case_idx);
  casefile_append_xfer (cf, &c);
}

static void
read_and_verify_random_case (struct casefile *cf,
                             struct casereader *reader, size_t case_idx) 
{
  struct ccase read_case, expected_case;
  size_t value_cnt;
  size_t i;
  
  value_cnt = casefile_get_value_cnt (cf);
  get_random_case (&expected_case, value_cnt, case_idx);
  if (!casereader_read (reader, &read_case)) 
    fail_test ("Premature end of casefile.");
  for (i = 0; i < value_cnt; i++) 
    {
      double a = case_num (&read_case, i);
      double b = case_num (&expected_case, i);
      if (a != b)
        fail_test ("Case %lu fails comparison.", (unsigned long) case_idx); 
    }
  case_destroy (&read_case);
  case_destroy (&expected_case);
}

static void
test_casereader_clone (struct casereader *reader1, size_t case_cnt)
{
  size_t i;
  size_t cases = 0;
  struct ccase c1;
  struct ccase c2;
  struct casefile *src = casereader_get_casefile (reader1);
  struct casereader *clone = NULL;

  size_t value_cnt = casefile_get_value_cnt (src);

  struct casefile *newfile = fastfile_create (value_cnt);
  struct casereader *newreader;


  /* Read a 3rd of the cases */
  for ( i = 0 ; i < case_cnt / 3 ; ++i ) 
    {
      casereader_read (reader1, &c1);
      case_destroy (&c1);
    }

  clone = casereader_clone (reader1);

  /* Copy all the cases into a new file */
  while( casereader_read (reader1, &c1))
    { 
      casefile_append_xfer (newfile, &c1);
      cases ++;
    }

  newreader = casefile_get_reader (newfile, NULL);

  /* Make sure that the new file's are identical to those returned from 
     the cloned reader */
  while( casereader_read (clone, &c1))
    { 
      const union value *v1;
      const union value *v2;
      cases --;

      if ( ! casereader_read_xfer (newreader, &c2) ) 
        {
          case_destroy (&c1);
          break; 
        }
      
      v1 = case_data_all (&c1) ;
      v2 = case_data_all (&c2) ;

      if ( 0 != memcmp (v1, v2, value_cnt * MAX_SHORT_STRING))
	fail_test ("Cloned reader read different value at case %ld", cases);

      case_destroy (&c1);
      case_destroy (&c2);
    }

  if ( cases > 0 ) 
    fail_test ("Cloned reader reads different number of cases.");

}

static void
fail_test (const char *message, ...) 
{
  va_list args;

  va_start (args, message);
  vprintf (message, args);
  putchar ('\n');
  va_end (args);
  
  exit (1);
}
