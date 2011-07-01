/* PSPP - a program for statistical analysis.
   Copyright (C) 2000, 2011 Free Software Foundation, Inc.

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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libpspp/pool.h"
#include "language/command.h"

#define N_ITERATIONS 8192
#define N_FILES 16

/* Self-test routine.
   This is not exhaustive, but it can be useful. */
int
cmd_debug_pool (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  int seed = time (0) * 257 % 32768;

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

  return CMD_SUCCESS;
}

