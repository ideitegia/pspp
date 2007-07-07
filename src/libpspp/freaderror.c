/* Detect read error on a stream.
   Copyright (C) 2003-2005, 2006 Free Software Foundation, Inc.
   Written by Bruno Haible <bruno@clisp.org>, 2003.
   Modified by Ben Pfaff <blp@cs.stanford.edu> for PSPP.

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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* Specification.  */
#include "freaderror.h"

#include <errno.h>
#include <stdbool.h>

/* Close the stream FP, and test whether some error occurred on
   the stream FP.
   FP must be a stream opened for reading.
   Return 0 if no error occurred and fclose (fp) succeeded.
   Return -1 and set errno if there was an error.  The errno value will be 0
   if the cause of the error cannot be determined. */
int
freaderror (FILE *fp)
{
  /* Need to
     1. test the error indicator of the stream,
     2. flush the buffers both in userland and in the kernel, through fclose,
        testing for error again.  */

  /* Clear errno, so that on non-POSIX systems the caller doesn't see a
     wrong value of errno when we return -1.  */
  errno = 0;

  if (ferror (fp))
    {
      /* The stream had an error earlier, but its errno was lost.
	 If the error was not temporary, we can get the same
	 errno by reading one more byte.  */
      getc (fp);
      fclose (fp);
      return -1;
    }

  if (fclose (fp))
    return -1; /* errno is set here */

  return 0;
}
