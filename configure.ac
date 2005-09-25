dnl Process this file with autoconf to produce a configure script.

dnl Initialize.
AX_PREREQ(2.57)
AC_INIT(pspp, 0.4.0,bug-gnu-pspp@gnu.org)
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

dnl Checks for programs.
AC_GNU_SOURCE
AC_PROG_CC
gl_EARLY
AC_PROG_RANLIB

AM_CONDITIONAL(cc_is_gcc, test x"$GCC" = x"yes" )

dnl Check that Perl is available.
AC_PATH_PROG([PERL], perl, no)
AC_SUBST([PERL])dnl
if test "$PERL" = no; then
  AC_MSG_ERROR([perl is not found])
fi
$PERL -e 'require 5.005_03;' || {
   AC_MSG_ERROR([Perl 5.005_03 or better is required])
}

dnl Internationalization macros.
AM_GNU_GETTEXT
AM_GNU_GETTEXT_VERSION([0.14.5])

dnl Checks for libraries.
AC_SYS_LARGEFILE
AC_FUNC_FSEEKO
AC_CHECK_LIB(m, sin)
AC_ARG_WITH(libplot, [  --without-libplot         don't compile in support of charts (using libplot)])

if test x"$with_libplot" != x"no" ; then 
	AC_CHECK_LIB(plot, pl_newpl_r,,
	  AC_MSG_ERROR([You must install libplot (or use --without-libplot)])
	)
fi
AM_CONDITIONAL(WITHCHARTS, test x"$with_libplot" != x"no")

dnl Check that off_t is defined as an integer type.
dnl Solaris sometimes declares it as a struct, if it
dnl thinks that the compiler does not support `long long'.
AC_COMPILE_IFELSE([#include <sys/types.h>
#include <unistd.h>
off_t x = 0;
int main (void) 
{ 
  lseek (0, 1, 2);
  return 0;
}], [], [AC_MSG_ERROR(
[Your system's definition of off_t is broken.  You are probably
using Solaris.  You can probably fix the problem with
`--disable-largefile' or `CFLAGS=-ansi'.])])

AC_CHECK_LIB(gslcblas,main,,AC_MSG_ERROR([You must install libgslcblas]))
AC_CHECK_LIB(gsl, gsl_cdf_chisq_Q,,
  AC_MSG_ERROR([You must install libgsl version 1.4 or later]))

AC_ARG_WITH(ncurses,
[  --without-ncurses         don't compile in ncurses command line editing])

if test "x$with_ncurses" = x"yes"; then 
AC_CHECK_LIB(ncurses, tgetent, LIBS="-lncurses $LIBS" termcap=yes,
  AC_CHECK_LIB(termcap, tgetent, LIBS="-ltermcap $LIBS" termcap=yes,
               termcap=no))
fi

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
AC_ARG_WITH(valgrind, [  --without-valgrind        don't compile in valgrind])
if test "x$with_valgrind" != x"no"; then 
 AC_CHECK_HEADERS([valgrind/valgrind.h])
fi
AC_CHECK_HEADERS([limits.h memory.h sys/stat.h sys/time.h sys/types.h \
                  fpu_control.h sys/mman.h sys/wait.h ieeefp.h fenv.h] )

# For gnulib.
gl_INIT

AC_C_CONST
AC_C_INLINE

dnl  Dont use AC_TYPE_OFF_T --- it doesnt generate the HAVE_TYPE macro
AC_CHECK_TYPES(off_t) 
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

AC_FUNC_VPRINTF
AC_CHECK_FUNCS([strchr strrchr __setfpucw isinf isnan finite getpid feholdexcept])

AC_PROG_LN_S


AH_BOTTOM([#include <pref.h>])


AM_CONDITIONAL(unix, test x"$host_os" != x"msdos" )
AM_CONDITIONAL(msdos, test x"$host_os" = x"msdos" )

dnl This is needed otherwise --with-included-gettext fails
AH_BOTTOM([#include <locale.h>])

AC_ARG_ENABLE(debug, [  --enable-debug          Turn on diagnostic features in the program])
if test x"$enable_debug" = x"yes"  ; then
  AC_DEFINE(DEBUGGING, 1, [Define to 1 if debugging is enabled.])
fi

AC_CONFIG_FILES([Makefile m4/Makefile gl/Makefile intl/Makefile po/Makefile.in
		 lib/Makefile lib/gsl-extras/Makefile lib/linreg/Makefile
		 doc/Makefile 
		 src/Makefile src/expressions/Makefile
		 config/Makefile
		 tests/Makefile
		 examples/Makefile])
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
