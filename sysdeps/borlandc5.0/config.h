/* config.h.in.  Generated automatically from configure.in by autoheader.  */
/* Special definitions, to process by autoheader.
	 Copyright (C) 1997 Free Software Foundation. */

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

/* The concatenation of the strings "GNU ", and PACKAGE.  */
#define GNU_PACKAGE "GNU PSPP"

/* Define to the name of the distribution.  */
#define PACKAGE "PSPP"

/* Define to 1 if ANSI function prototypes are usable.  */
#define PROTOTYPES 1

/* Define to the version of the distribution.  */
#define VERSION "0.1.0"

/* Define if using alloca.c.  */
#undef C_ALLOCA

/* Define to empty if the keyword does not work.  */
#undef const

/* Define to one of _getb67, GETB67, getb67 for Cray-2 and Cray-YMP systems.
	 This function is required for alloca.c support on those systems.  */
#define CRAY_STACKSEG_END

/* Define if you have alloca, as a function or macro.  */
#undef HAVE_ALLOCA

/* Define if you have <alloca.h> and it should be used (not on Ultrix).  */
#undef HAVE_ALLOCA_H

/* Define if you don't have vprintf but do have _doprnt.  */
#undef HAVE_DOPRNT

/* Define if you have a working `mmap' system call.  */
#undef HAVE_MMAP

/* Define if you have the vprintf function.  */
#define HAVE_VPRINTF 1

/* Define as __inline if that's what the C compiler calls it.  */
#define inline

/* Define to `long' if <sys/types.h> doesn't define.  */
#undef off_t

/* Define if you need to in order for stat and other things to work.  */
#undef _POSIX_SOURCE

/* Define to `unsigned' if <sys/types.h> doesn't define.  */
#undef size_t

/* If using the C implementation of alloca, define if you know the
	 direction of stack growth for your system; otherwise it will be
	 automatically deduced at run-time.
 STACK_DIRECTION > 0 => grows toward higher addresses
 STACK_DIRECTION < 0 => grows toward lower addresses
 STACK_DIRECTION = 0 => direction of growth unknown
 */
#undef STACK_DIRECTION

/* Define if the `S_IS*' macros in <sys/stat.h> do not work properly.  */
#undef STAT_MACROS_BROKEN

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/* Define if you can safely include both <sys/time.h> and <time.h>.  */
#undef TIME_WITH_SYS_TIME

/* Define if your <sys/time.h> declares struct tm.  */
#undef TM_IN_SYS_TIME

/* Define if sprintf() returns the number of characters written to
	 the destination string, excluding the null terminator. */
#define HAVE_GOOD_SPRINTF 1

/* Define endianness of computer here as BIG or LITTLE, if known.
	 If not known, define as UNKNOWN. */
#define ENDIAN LITTLE

/* Define as floating-point representation of this computer.  For
	 i386, m68k, and other common chips, this is FPREP_IEEE754. */
#define FPREP FPREP_IEEE754

/* Number of digits in longest `long' value, including sign.  This is
	 usually 11, for 32-bit `long's, or 19, for 64-bit `long's. */
#define INT_DIGITS 11

/* Define if you have the history library (-lhistory).  */
#undef HAVE_LIBHISTORY

/* Define if you have the termcap library (-ltermcap).  */
#undef HAVE_LIBTERMCAP

/* Define if your locale.h file contains LC_MESSAGES.  */
#define HAVE_LC_MESSAGES 1

/* Define to 1 if NLS is requested.  */
#undef ENABLE_NLS

/* Define as 1 if you have catgets and don't want to use GNU gettext.  */
#undef HAVE_CATGETS

/* Define as 1 if you have gettext and don't want to use GNU gettext.  */
#undef HAVE_GETTEXT

/* Define as 1 if you have the stpcpy function.  */
#define HAVE_STPCPY 1

/* The number of bytes in a double.  */
#define SIZEOF_DOUBLE 8

/* The number of bytes in a float.  */
#define SIZEOF_FLOAT 4

/* The number of bytes in a int.  */
#define SIZEOF_INT 4

/* The number of bytes in a long.  */
#define SIZEOF_LONG 4

/* The number of bytes in a long double.  */
#define SIZEOF_LONG_DOUBLE 12

/* The number of bytes in a long long.  */
#define SIZEOF_LONG_LONG

/* The number of bytes in a short.  */
#define SIZEOF_SHORT 2

/* Define if you have the __argz_count function.  */
#undef HAVE___ARGZ_COUNT

/* Define if you have the __argz_next function.  */
#undef HAVE___ARGZ_NEXT

/* Define if you have the __argz_stringify function.  */
#undef HAVE___ARGZ_STRINGIFY

/* Define if you have the __setfpucw function.  */
#undef HAVE___SETFPUCW

/* Define if you have the dcgettext function.  */
#undef HAVE_DCGETTEXT

/* Define if you have the finite function.  */
#undef HAVE_FINITE

/* Define if you have the getcwd function.  */
#undef HAVE_GETCWD

/* Define if you have the getdelim function.  */
#undef HAVE_GETDELIM

/* Define if you have the gethostname function.  */
#undef HAVE_GETHOSTNAME

/* Define if you have the getline function.  */
#undef HAVE_GETLINE

/* Define if you have the getpagesize function.  */
#undef HAVE_GETPAGESIZE

/* Define if you have the getpid function.  */
#define HAVE_GETPID 1

/* Define if you have the isinf function.  */
#undef HAVE_ISINF

/* Define if you have the isnan function.  */
#undef HAVE_ISNAN

/* Define if you have the memchr function.  */
#define HAVE_MEMCHR 1

/* Define if you have the memmem function.  */
#define HAVE_MEMMEM 0

/* Define if you have the memmove function.  */
#define HAVE_MEMMOVE 1

/* Define if you have the memset function.  */
#define HAVE_MEMSET 1

/* Define if you have the munmap function.  */
#undef HAVE_MUNMAP

/* Define if you have the putenv function.  */
#define HAVE_PUTENV 1

/* Define if you have the setenv function.  */
#undef HAVE_SETENV

/* Define if you have the setlocale function.  */
#define HAVE_SETLOCALE 1

/* Define if you have the stpcpy function.  */
#define HAVE_STPCPY 1

/* Define if you have the strcasecmp function.  */
#undef HAVE_STRCASECMP

/* Define if you have the strchr function.  */
#undef HAVE_STRCHR

/* Define if you have the strerror function.  */
#define HAVE_STRERROR 1

/* Define if you have the strncasecmp function.  */
#undef HAVE_STRNCASECMP

/* Define if you have the strpbrk function.  */
#define HAVE_STRPBRK 1

/* Define if you have the strstr function.  */
#define HAVE_STRSTR 1

/* Define if you have the strtod function.  */
#define HAVE_STRTOD 1

/* Define if you have the strtol function.  */
#define HAVE_STRTOL 1

/* Define if you have the strtoul function.  */
#define HAVE_STRTOUL 1

/* Define if you have the <argz.h> header file.  */
#undef HAVE_ARGZ_H

/* Define if you have the <fpu_control.h> header file.  */
#undef HAVE_FPU_CONTROL_H

/* Define if you have the <limits.h> header file.  */
#define HAVE_LIMITS_H 1

/* Define if you have the <locale.h> header file.  */
#define HAVE_LOCALE_H 1

/* Define if you have the <malloc.h> header file.  */
#define HAVE_MALLOC_H 1

/* Define if you have the <memory.h> header file.  */
#define HAVE_MEMORY_H 1

/* Define if you have the <nl_types.h> header file.  */
#undef HAVE_NL_TYPES_H

/* Define if you have the <readline/history.h> header file.  */
#undef HAVE_READLINE_HISTORY_H

/* Define if you have the <readline/readline.h> header file.  */
#undef HAVE_READLINE_READLINE_H

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H 1

/* Define if you have the <sys/time.h> header file.  */
#undef HAVE_SYS_TIME_H

/* Define if you have the <sys/types.h> header file.  */
#define HAVE_SYS_TYPES_H 1

/* Define if you have the <termcap.h> header file.  */
#undef HAVE_TERMCAP_H

/* Define if you have the <unistd.h> header file.  */
#undef HAVE_UNISTD_H

/* Define if you have the <values.h> header file.  */
#define HAVE_VALUES_H 1

/* Define if you have the i library (-li).  */
#undef HAVE_LIBI

/* Define if you have the m library (-lm).  */
#undef HAVE_LIBM

/* Define if you have the readline library (-lreadline).  */
#undef HAVE_LIBREADLINE

#include <pref.h>

/* Local Variables: */
/* mode:c */
/* End: */
