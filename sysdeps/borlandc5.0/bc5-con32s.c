/* con32s - emulates console under Windows.
   Copyright (C) 1997, 1998 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

/* This replaces a few of the Borland C++ library functions.  It does
   not use any of the runtime library header files, so you do not need
   the runtime library source in order to compile it. */

#include <io.h>
#include <windef.h>
#include <wincon.h>

/* 1=It is necessary to emulate the console window. */
int _emu_console;

/* Exported by con32s.c. */
extern int _blp_console_read (void *buf, unsigned len);

/* Exported by Borland runtime library. */
extern long _handles[];
extern int __IOerror (int);
extern int __NTerror (void);

/* Replaces Borland library function. */
int
_rtl_read (int fd, void *buf, unsigned len)
{
  DWORD nread;

  if ((unsigned) fd >= _nfile)
    return __IOerror (ERROR_INVALID_HANDLE);

  /* Redirect stdin to the faked console window. */
  if (_emu_console && fd < 3)
    return _blp_console_read (buf, len);

  if (ReadFile ((HANDLE) _handles[fd], buf, (DWORD) len, &nread, NULL) != 1)
    return __NTerror ();
  else
    return (int) nread;
}

/* Replaces Borland library function. */
int
_rtl_write (int fd, const void *buf, unsigned int len)
{
  DWORD written;

  if ((unsigned) fd >= _nfile)
    return __IOerror (ERROR_INVALID_HANDLE);

  /* Redirect stdout, stderr to the faked console window. */
  if (_emu_console && fd < 3)
    return _blp_console_write (buf, len);

  if (WriteFile ((HANDLE) _handles[fd], (PVOID) buf, (DWORD) len, &written,
		 NULL) != 1)
    return __NTerror ();
  else
    return (int) written;
}

void
determine_os (void)
{
#pragma startup determine_os 64
  DWORD nButtons;

  /* Try out a random console function.  If it fails then we must not
     have a console.

     Believe it or not, this seems to be the only way to determine
     reliably whether we're running under 3.1.  If you know a better
     way, let me know. */
  if (GetNumberOfConsoleMouseButtons (&nButtons))
    _emu_console = 0;
  else
    _emu_console = 1;
}

