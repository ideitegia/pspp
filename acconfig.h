/* Special definitions, to process by autoheader.
   Copyright (C) 1997-9, 2000 Free Software Foundation. */

/* Definitions for byte order, according to significance of bytes, from low
   addresses to high addresses.  The value is what you get by putting '4'
   in the most significant byte, '3' in the second most significant byte,
   '2' in the second least significant byte, and '1' in the least
   significant byte.  These definitions never need to be modified. */
#define BIG	4321	/* 68k */
#define LITTLE  1234	/* i[3456]86 */
#define UNKNOWN 0000	/* Endianness must be determined at runtime. */

/* Definitions for floating-point representation. */
#define FPREP_IEEE754	754	/* The usual IEEE-754 format. */
#define FPREP_UNKNOWN	666	/* Triggers an error at compile time. */

/* We want prototypes for all the GNU extensions. */
#define _GNU_SOURCE	1

/* Name of the distribution. */
#define PACKAGE "PSPP"

/* Version of the distribution. */
#undef VERSION

/* The concatenation of the strings "GNU ", and PACKAGE.  */
#define GNU_PACKAGE "GNU PSPP"

/* Define to 1 if ANSI function prototypes are usable.  */
#undef PROTOTYPES


@TOP@

/* Define if sprintf() returns the number of characters written to
   the destination string, excluding the null terminator. */
#undef HAVE_GOOD_SPRINTF

/* Define if rand() and company work according to ANSI. */
#undef HAVE_GOOD_RANDOM

/* Define endianness of computer here as BIG or LITTLE, if known.
   If not known, define as UNKNOWN. */
#define ENDIAN BIG

/* Define as floating-point representation of this computer.  For
   i386, m68k, and other common chips, this is FPREP_IEEE754. */
#define FPREP FPREP_IEEE754

/* Number of digits in longest `long' value, including sign.  This is
   usually 11, for 32-bit `long's, or 19, for 64-bit `long's. */
#define INT_DIGITS 19

/* Define if you have the history library (-lhistory).  */
#undef HAVE_LIBHISTORY

/* Define if you have the termcap library (-ltermcap).  */
#undef HAVE_LIBTERMCAP

/* Stolen from Ulrich Drepper, <drepper@gnu.org> gettext-0.10,
   1995.  */

/* Define if your locale.h file contains LC_MESSAGES.  */
#undef HAVE_LC_MESSAGES

/* Define to 1 if NLS is requested.  */
#undef ENABLE_NLS

/* Define as 1 if you have catgets and don't want to use GNU gettext.  */
#undef HAVE_CATGETS

/* Define as 1 if you have gettext and don't want to use GNU gettext.  */
#undef HAVE_GETTEXT

/* Define as 1 if you have the stpcpy function.  */
#undef HAVE_STPCPY

@BOTTOM@

#include <pref.h>

/* Local Variables: */
/* mode:c */
/* End: */
