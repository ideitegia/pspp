dnl Process this file with autoconf to produce a configure script.

dnl Initialize.
AC_PREREQ(2.58)
AC_INIT(pspp, 0.3.1,bug-gnu-pspp@gnu.org)
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

dnl Checks for programs.
AC_GNU_SOURCE
AC_PROG_CC

dnl Internationalization macros.
AM_GNU_GETTEXT
AM_GNU_GETTEXT_VERSION	dnl Prevents autoreconf complaint.

dnl Checks for libraries.
AC_CHECK_LIB(m, sin)
AC_CHECK_LIB(gmp, mpf_get_str,,
	AC_CHECK_LIB(gmp, __gmpf_get_str,,
          AC_MSG_ERROR([You must install libgmp])
        )
)

AC_CHECK_FUNC(getopt_long,,
	AC_MSG_ERROR(`This application depends upon getopt_long'))

  
AC_CHECK_LIB(ncurses, tgetent, LIBS="-lncurses $LIBS" termcap=yes,
  AC_CHECK_LIB(termcap, tgetent, LIBS="-ltermcap $LIBS" termcap=yes,
               termcap=no))
if test "$termcap" = yes; then
  AC_CHECK_HEADERS(termcap.h)
  AC_DEFINE(HAVE_LIBTERMCAP, 1, 
	[Define if you have the termcap library (-ltermcap).])
fi

AC_CHECK_LIB(readline, readline)
if test "$ac_cv_lib_readline_readline" = yes; then
  AC_CHECK_HEADERS(readline/readline.h)
  AC_CHECK_LIB(readline, add_history, history=yes,
    AC_CHECK_LIB(history, add_history, LIBS="-lhistory" history=yes,
                 history=no))
  if test "$history" = yes; then
    AC_CHECK_HEADERS(readline/history.h)
    AC_DEFINE(HAVE_LIBHISTORY, 1,
	[Define if you have the history library (-lhistory).])
  fi
fi

dnl Checks for header files.
AC_CHECK_HEADERS([limits.h memory.h sys/stat.h sys/time.h sys/types.h \
                  fpu_control.h sys/mman.h sys/wait.h ieeefp.h fenv.h])
AC_HEADER_STAT
AC_HEADER_STDC
AC_HEADER_TIME

AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_STRUCT_TM

AC_CHECK_SIZEOF(short, 2)
AC_CHECK_SIZEOF(int, 4)
AC_CHECK_SIZEOF(long, 4)
AC_CHECK_SIZEOF(long long, 0)
AC_CHECK_SIZEOF(float, 0)
AC_CHECK_SIZEOF(double, 8)
AC_CHECK_SIZEOF(long double, 0)

AC_DEFINE(FPREP_IEEE754, 1,
	[Define for machines that have IEEE 754 floating point arithmetic,
         the most common format today.])

AC_C_BIGENDIAN

BLP_IS_SPRINTF_GOOD
BLP_INT_DIGITS
BLP_RANDOM

AC_FUNC_ALLOCA
AC_FUNC_MEMCMP
AC_FUNC_VPRINTF
AC_REPLACE_FUNCS([memmove memset stpcpy strpbrk strerror strtol strtoul \
 		  memchr getline getdelim strcasecmp strncasecmp memmem \
		  strtok_r])
AC_CHECK_FUNCS([gethostname strstr strtod __setfpucw isinf isnan finite \
		getpid feholdexcept])

AC_PROG_LN_S

dnl This must be after other tests so warnings don't provoke errors above.
if test "$ac_cv_c_compiler_gnu" = yes; then
	CFLAGS="-g -Wall -W -Wno-uninitialized -Wwrite-strings \
-Wstrict-prototypes -Wpointer-arith -Wno-sign-compare"
	if test "$CC_OPTIONS" != ""; then
		CFLAGS="$CFLAGS $CC_OPTIONS"
	fi
fi
AC_SUBST(CFLAGS)

AH_BOTTOM([#include <pref.h>])

dnl This is needed otherwise --with-included-gettext fails
AH_BOTTOM([#include <locale.h>])

AC_ARG_ENABLE(debug, [  --enable-debug          Turn on diagnostic features in the program])
if test x"$enable_debug" = x"yes"  ; then
  AC_DEFINE(DEBUGGING, 1, [Define to 1 if debugging is enabled.])
fi

AC_CONFIG_FILES([Makefile po/Makefile.in m4/Makefile intl/Makefile 
		 lib/Makefile lib/julcal/Makefile lib/misc/Makefile 
		 lib/dcdflib/Makefile doc/Makefile src/Makefile 
		 config/Makefile tests/Makefile])
AC_CONFIG_COMMANDS([pref.h],[
	   # Copy pref.h from pref.h.orig if prudent
	   if test ! -f pref.h; then
	     echo "creating pref.h"
	     cp $ac_given_srcdir/pref.h.orig pref.h
	   elif test "`ls -t pref.h.orig pref.h 2>/dev/null | sed 1q`" = pref.h.orig; then
	     echo "replacing pref.h with newer pref.h.orig"
	     cp $ac_given_srcdir/pref.h.orig pref.h
	   else
	     echo "pref.h exists"
	   fi
	   if test -f pref.h; then touch pref.h; fi
	  ])
AC_OUTPUT

dnl configure.ac ends here
