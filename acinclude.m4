dnl Copyright (C) 2005, 2006 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl Prerequisites.

dnl Instead of giving an error about each prerequisite as we encounter it, 
dnl group them all together at the end of the run, to be user-friendly.
AC_DEFUN([PSPP_REQUIRED_PREREQ], [pspp_required_prereqs="$pspp_required_prereqs
	$1"])
AC_DEFUN([PSPP_OPTIONAL_PREREQ], [pspp_optional_prereqs="$pspp_optional_prereqs
	$1"])
AC_DEFUN([PSPP_CHECK_PREREQS], 
[
  if test "$pspp_optional_prereqs" != ""; then
    AC_MSG_WARN([The following optional prerequisites are not installed.
You may wish to install them to obtain additional functionality:$pspp_optional_prereqs])
fi
  if test "$pspp_required_prereqs" != ""; then
    AC_MSG_ERROR([The following required prerequisites are not installed.
You must install them before PSPP can be built:$pspp_required_prereqs])
fi
])
    

dnl Check that a new enough version of Perl is available.
AC_DEFUN([PSPP_PERL],
[
  AC_PATH_PROG([PERL], perl, no)
  AC_SUBST([PERL])dnl
  if test "$PERL" != no && $PERL -e 'require 5.005_03;'; then :; else
    PSPP_REQUIRED_PREREQ([Perl 5.005_03 (or later)])
  fi
])

dnl Check that libplot is available.
AC_DEFUN([PSPP_LIBPLOT],
[
  AC_ARG_WITH(libplot, [  --without-libplot         don't compile in support of charts (using libplot)])

  if test x"$with_libplot" != x"no" ; then 
    AC_CHECK_LIB(plot, pl_newpl_r,,
	         [PSPP_REQUIRED_PREREQ([libplot (or use --without-libplot)])])
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

dnl Check whether a C compiler option is accepted.
dnl If so, add it to CFLAGS.
dnl Example: PSPP_ENABLE_OPTION(-Wdeclaration-after-statement)
AC_DEFUN([PSPP_ENABLE_OPTION],
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

dnl Modified for PSPP, based on readline.m4 serial 3 from
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
    PSPP_OPTIONAL_PREREQ([libreadline (which may itself require libncurses or libtermcap)])
  fi
  AC_SUBST(LIBREADLINE)
  AC_SUBST(LTLIBREADLINE)
])

dnl Check for build tools.  Adapted from bfd library.

AC_DEFUN([PSPP_CC_FOR_BUILD],
[# Put a plausible default for CC_FOR_BUILD in Makefile.
if test -z "$CC_FOR_BUILD"; then
  if test "x$cross_compiling" = "xno"; then
    CC_FOR_BUILD='$(CC)'
  else
    CC_FOR_BUILD=cc
  fi
fi
AC_SUBST(CC_FOR_BUILD)
# Also set EXEEXT_FOR_BUILD.
if test "x$cross_compiling" = "xno"; then
  EXEEXT_FOR_BUILD='$(EXEEXT)'
else
  AC_CACHE_CHECK([for build system executable suffix], pspp_cv_build_exeext,
    [rm -f conftest*
     echo 'int main () { return 0; }' > conftest.c
     pspp_cv_build_exeext=
     ${CC_FOR_BUILD} -o conftest conftest.c 1>&5 2>&5
     for file in conftest.*; do
       case $file in # (
       *.c | *.o | *.obj | *.ilk | *.pdb) ;; # (
       *) pspp_cv_build_exeext=`echo $file | sed -e s/conftest//` ;;
       esac
     done
     rm -f conftest*
     test x"${pspp_cv_build_exeext}" = x && pspp_cv_build_exeext=no])
  EXEEXT_FOR_BUILD=""
  test x"${pspp_cv_build_exeext}" != xno && EXEEXT_FOR_BUILD=${pspp_cv_build_exeex
t}
fi
AC_SUBST(EXEEXT_FOR_BUILD)])

dnl aclocal.m4 ends here
