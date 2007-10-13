dnl Copyright (C) 2005, 2006, 2007 Free Software Foundation, Inc.
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
  AC_ARG_WITH(
    libplot, 
    [AS_HELP_STRING([--without-libplot],
                    [don't compile in support of charts (using libplot)])])

  if test x"$with_libplot" != x"no" ; then 
    # Check whether we can link against libplot without any extra libraries.
    AC_CHECK_LIB(plot, pl_newpl_r, [LIBPLOT_LIBS="-lplot"])

    # Check whether we can link against libplot if we also link X.
    if test x"$LIBPLOT_LIBS" = x""; then
      AC_PATH_XTRA
      extra_libs="-lXaw -lXmu -lXt $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS -lm"
      AC_CHECK_LIB(plot, pl_newpl_r,
      		   [LIBPLOT_LIBS="-lplot $extra_libs"
                    LDFLAGS="$LDFLAGS $X_LIBS"],,
      		   [$extra_libs])
    fi

    # Still can't link?
    if test x"$LIBPLOT_LIBS" = x""; then
      PSPP_REQUIRED_PREREQ([libplot (or use --without-libplot)])
    fi

    # Set up to make everything work.
    LIBS="$LIBPLOT_LIBS $LIBS"
    AC_DEFINE(HAVE_LIBPLOT, 1,
              [Define to 1 if you have the `libplot' library (-lplot).])
  fi
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

dnl Check for LC_PAPER, _NL_PAPER_WIDTH, _NL_PAPER_HEIGHT.
AC_DEFUN([PSPP_LC_PAPER],
[AC_CACHE_CHECK(for LC_PAPER, pspp_cv_lc_paper, [
    pspp_cv_lc_paper=no
    AC_COMPILE_IFELSE(
      [AC_LANG_PROGRAM(
        [#include <locale.h>
#include <langinfo.h>
],
        [(void) LC_PAPER; (void) _NL_PAPER_WIDTH; (void) _NL_PAPER_HEIGHT])],
      [pspp_cv_lc_paper=yes])
  ])
  if test "$pspp_cv_lc_paper" = yes; then
    AC_DEFINE(HAVE_LC_PAPER, 1, [Define if you have LC_PAPER.])
  fi
])

dnl acinclude.m4 ends here
