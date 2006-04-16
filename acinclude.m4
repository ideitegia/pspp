dnl Copyright (C) 2005, 2006 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl Check that a new enough version of Perl is available.
AC_DEFUN([PSPP_PERL],
[
  AC_PATH_PROG([PERL], perl, no)
  AC_SUBST([PERL])dnl
  if test "$PERL" = no; then
    AC_MSG_ERROR([perl is not found])
  fi
  $PERL -e 'require 5.005_03;' || {
     AC_MSG_ERROR([Perl 5.005_03 or better is required])
  }
])

dnl Check that libplot is available.
AC_DEFUN([PSPP_LIBPLOT],
[
  AC_ARG_WITH(libplot, [  --without-libplot         don't compile in support of charts (using libplot)])

  if test x"$with_libplot" != x"no" ; then 
	  AC_CHECK_LIB(plot, pl_newpl_r,,
	    AC_MSG_ERROR([You must install libplot development libraries (or use --without-libplot)])
	  )
  fi
])

dnl Check that off_t is defined as an integer type.
dnl Solaris sometimes declares it as a struct, if it
dnl thinks that the compiler does not support `long long'.
AC_DEFUN([PSPP_OFF_T],
[
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
])

dnl Check whether a warning flag is accepted.
dnl If so, add it to CFLAGS.
dnl Example: PSPP_ENABLE_WARNING(-Wdeclaration-after-statement)
AC_DEFUN([PSPP_ENABLE_WARNING],
[
  m4_define([pspp_cv_name], [pspp_cv_[]m4_translit([$1], [-], [_])])dnl
  AC_CACHE_CHECK([whether $CC accepts $1], [pspp_cv_name], 
    [pspp_save_CFLAGS="$CFLAGS"
     CFLAGS="$CFLAGS $1"
     AC_COMPILE_IFELSE([AC_LANG_PROGRAM(,)], [pspp_cv_name[]=yes], [pspp_cv_name[]=no])
     CFLAGS="$pspp_save_CFLAGS"])
  if test $pspp_cv_name = yes; then
    CFLAGS="$CFLAGS $1"
  fi
])

dnl Check for readline and history libraries.

dnl Modified for PSPP by Ben Pfaff, based on readline.m4 serial 3 from
dnl gnulib, which was written by Simon Josefsson, with help from Bruno
dnl Haible and Oskar Liljeblad.

AC_DEFUN([PSPP_READLINE],
[
  dnl Prerequisites of AC_LIB_LINKFLAGS_BODY.
  AC_REQUIRE([AC_LIB_PREPARE_PREFIX])
  AC_REQUIRE([AC_LIB_RPATH])

  dnl Search for libreadline and define LIBREADLINE, LTLIBREADLINE and
  dnl INCREADLINE accordingly.
  AC_LIB_LINKFLAGS_BODY([readline])
  AC_LIB_LINKFLAGS_BODY([history])

  dnl Add $INCREADLINE to CPPFLAGS before performing the following checks,
  dnl because if the user has installed libreadline and not disabled its use
  dnl via --without-libreadline-prefix, he wants to use it. The AC_TRY_LINK
  dnl will then succeed.
  am_save_CPPFLAGS="$CPPFLAGS"
  AC_LIB_APPENDTOVAR([CPPFLAGS], [$INCREADLINE $INCHISTORY])

  AC_CACHE_CHECK(for readline, gl_cv_lib_readline, [
    gl_cv_lib_readline=no
    am_save_LIBS="$LIBS"
    dnl On some systems, -lreadline doesn't link without an additional
    dnl -lncurses or -ltermcap.
    dnl Try -lncurses before -ltermcap, because libtermcap is unsecure
    dnl by design and obsolete since 1994. Try -lcurses last, because
    dnl libcurses is unusable on some old Unices.
    for extra_lib in "" ncurses termcap curses; do
      LIBS="$am_save_LIBS $LIBREADLINE $LIBHISTORY"
      if test -n "$extra_lib"; then
        LIBS="$LIBS -l$extra_lib"
      fi
      AC_TRY_LINK([#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>],
        [readline((char*)0); add_history((char*)0);],
        gl_cv_lib_readline=yes)
      if test "$gl_cv_lib_readline" = yes; then
        if test -n "$extra_lib"; then
          LIBREADLINE="$LIBREADLINE $LIBHISTORY -l$extra_lib"
          LTLIBREADLINE="$LTLIBREADLINE $LTLIBHISTORY -l$extra_lib"
        fi
        break
      fi
    done
    LIBS="$am_save_LIBS"
  ])

  if test "$gl_cv_lib_readline" = yes; then
    AC_DEFINE(HAVE_READLINE, 1, [Define if you have the readline library.])
  fi

  if test "$gl_cv_lib_readline" = yes; then
    AC_MSG_CHECKING([how to link with libreadline])
    AC_MSG_RESULT([$LIBREADLINE])
  else
    dnl If $LIBREADLINE didn't lead to a usable library, we don't
    dnl need $INCREADLINE either.
    CPPFLAGS="$am_save_CPPFLAGS"
    LIBREADLINE=
    LTLIBREADLINE=
    LIBHISTORY=
    LTLIBHISTORY=
  fi
  AC_SUBST(LIBREADLINE)
  AC_SUBST(LTLIBREADLINE)
])

dnl aclocal.m4 ends here
