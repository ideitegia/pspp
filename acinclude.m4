dnl Copyright (C) 2005, 2006, 2007, 2009, 2014 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl Prerequisites.

dnl Summarize all the missing prerequisites at the end of the run to
dnl increase user-friendliness.
AC_DEFUN([PSPP_REQUIRED_PREREQ], 
  [AC_MSG_WARN([You must install $1 before building PSPP.])
pspp_required_prereqs="$pspp_required_prereqs
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

  # The PSPP autobuilder appends a build number to the PSPP version number,
  # e.g. "0.7.2-build40".  But Perl won't parse version numbers that contain
  # anything other than digits and periods, so "-build" causes an error.  So we
  # define $(VERSION_FOR_PERL) that drops everything from the hyphen onward.
  VERSION_FOR_PERL=`echo "$VERSION" | sed 's/-.*//'`
  AC_SUBST([VERSION_FOR_PERL])
])

dnl PSPP_CHECK_CC_OPTION([OPTION], [ACTION-IF-ACCEPTED], [ACTION-IF-REJECTED])
dnl Check whether the given C compiler OPTION is accepted.
dnl If so, execute ACTION-IF-ACCEPTED, otherwise ACTION-IF-REJECTED.
AC_DEFUN([PSPP_CHECK_CC_OPTION],
[
  m4_define([pspp_cv_name], [pspp_cv_[]m4_translit([$1], [-], [_])])dnl
  AC_CACHE_CHECK([whether $CC accepts $1], [pspp_cv_name], 
    [pspp_save_CFLAGS="$CFLAGS"
     CFLAGS="$CFLAGS $1"
     AC_COMPILE_IFELSE([AC_LANG_PROGRAM(,)], [pspp_cv_name[]=yes], [pspp_cv_name[]=no])
     CFLAGS="$pspp_save_CFLAGS"])
  if test $pspp_cv_name = yes; then
    m4_if([$2], [], [;], [$2])
  else
    m4_if([$3], [], [:], [$3])
  fi
])

dnl PSPP_ENABLE_OPTION([OPTION])
dnl Check whether the given C compiler OPTION is accepted.
dnl If so, add it to CFLAGS.
dnl Example: PSPP_ENABLE_OPTION([-Wdeclaration-after-statement])
AC_DEFUN([PSPP_ENABLE_OPTION], 
  [PSPP_CHECK_CC_OPTION([$1], [CFLAGS="$CFLAGS $1"])])

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
  dnl via --without-libreadline-prefix, he wants to use it. The AC_LINK_IFELSE
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
      AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>]], [[readline((char*)0); add_history((char*)0);]])],[gl_cv_lib_readline=yes],[])
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
    AC_SEARCH_LIBS([rl_echo_signal_char], [readline],
        AC_DEFINE(HAVE_RL_ECHO_SIGNAL_CHAR, 1, [Define if the readline library provides rl_echo_signal_char.]),[],[$LIBREADLINE])
    AC_SEARCH_LIBS([rl_outstream], [readline],
        AC_DEFINE(HAVE_RL_OUTSTREAM, 1, [Define if the readline library provides rl_outstream.]),[],[$LIBREADLINE])
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


# PSPP_LINK2_IFELSE(SOURCE1, SOURCE2, [ACTION-IF-TRUE], [ACTION-IF-FALSE])
# -------------------------------------------------------------
# Based on AC_LINK_IFELSE, but tries to link both SOURCE1 and SOURCE2
# into a program.
#
# Test that resulting file is executable; see the problem reported by mwoehlke
# in <http://lists.gnu.org/archive/html/bug-coreutils/2006-10/msg00048.html>.
# But skip the test when cross-compiling, to prevent problems like the one
# reported by Chris Johns in
# <http://lists.gnu.org/archive/html/autoconf/2007-03/msg00085.html>.
#
m4_define([PSPP_LINK2_IFELSE],
[m4_ifvaln([$1], [AC_LANG_CONFTEST([$1])])dnl
mv conftest.$ac_ext conftest1.$ac_ext
m4_ifvaln([$2], [AC_LANG_CONFTEST([$2])])dnl
mv conftest.$ac_ext conftest2.$ac_ext
rm -f conftest1.$ac_objext conftest2.$ac_objext conftest$ac_exeext
pspp_link2='$CC -o conftest$ac_exeext $CFLAGS $CPPFLAGS $LDFLAGS conftest1.$ac_ext conftest2.$ac_ext $LIBS >&5'
AS_IF([_AC_DO_STDERR($pspp_link2) && {
	 test -z "$ac_[]_AC_LANG_ABBREV[]_werror_flag" ||
	 test ! -s conftest.err
       } && test -s conftest$ac_exeext && {
	 test "$cross_compiling" = yes ||
	 AS_TEST_X([conftest$ac_exeext])
       }],
      [$3],
      [echo "$as_me: failed source file 1 of 2 was:" >&5
sed 's/^/| /' conftest1.$ac_ext >&5
echo "$as_me: failed source file 2 of 2 was:" >&5
sed 's/^/| /' conftest2.$ac_ext >&5
	$4])
dnl Delete also the IPA/IPO (Inter Procedural Analysis/Optimization)
dnl information created by the PGI compiler (conftest_ipa8_conftest.oo),
dnl as it would interfere with the next link command.
rm -rf conftest.dSYM conftest1.dSYM conftest2.dSYM
rm -f core conftest.err conftest1.err conftest2.err
rm -f conftest1.$ac_objext conftest2.$ac_objext conftest*_ipa8_conftest*.oo
rm -f conftest$ac_exeext
rm -f m4_ifval([$1], [conftest1.$ac_ext]) m4_ifval([$2], [conftest1.$ac_ext])[]dnl
])# PSPP_LINK2_IFELSE

# GSL uses "extern inline" without determining whether the compiler uses
# GCC inline rules or C99 inline rules.  If it uses the latter then GSL
# will be broken without passing -fgnu89-inline to GCC.
AC_DEFUN([PSPP_GSL_NEEDS_FGNU89_INLINE],
[# GSL only uses "inline" at all if HAVE_INLINE is defined as a macro.
 # In turn, gnulib's gl_INLINE is one macro that does that.  We need to
 # make sure that it has run by the time we run this test, otherwise we'll
 # get a false result.
 AC_REQUIRE([gl_INLINE])
 PSPP_CHECK_CC_OPTION(
   [-fgnu89-inline],
   [AC_CACHE_CHECK([whether GSL needs -fgnu89-inline to link],
		    pspp_cv_gsl_needs_fgnu89_inline, [
		    PSPP_LINK2_IFELSE(
		      [AC_LANG_PROGRAM([#include <gsl/gsl_math.h>
				       ], [GSL_MAX_INT(1,2);])],
		      [AC_LANG_SOURCE([#include <gsl/gsl_math.h>
				       void x (void) {}])],
		      [pspp_cv_gsl_needs_fgnu89_inline=no],
		      [pspp_cv_gsl_needs_fgnu89_inline=yes])])
     if test "$pspp_cv_gsl_needs_fgnu89_inline" = "yes"; then
	 CFLAGS="$CFLAGS -fgnu89-inline"
     fi])
])

AC_DEFUN([PSPP_CHECK_CLICKSEQUENCE],
  [AC_REQUIRE([AM_INIT_AUTOMAKE])  # Defines MAKEINFO
   AC_CACHE_CHECK([whether makeinfo supports @clicksequence],
     [pspp_cv_have_clicksequence],
     [cat > conftest.texi  <<EOF
@setfilename conftest.info
@clicksequence{File @click{} Open}
EOF
      echo "configure:__oline__: running $MAKEINFO conftest.texi >&AS_MESSAGE_LOG_FD" >&AS_MESSAGE_LOG_FD
      eval "$MAKEINFO conftest.texi >&AS_MESSAGE_LOG_FD 2>&1"
      retval=$?
      echo "configure:__oline__: \$? = $retval" >&AS_MESSAGE_LOG_FD
      if test $retval = 0; then
	pspp_cv_have_clicksequence=yes
      else
	pspp_cv_have_clicksequence=no
      fi
      rm -f conftest.texi conftest.info])
   if test $pspp_cv_have_clicksequence = no; then
       AM_MAKEINFOFLAGS="$AM_MAKEINFOFLAGS -DMISSING_CLICKSEQUENCE"
       AC_SUBST([AM_MAKEINFOFLAGS])
   fi])

# The following comes from Open vSwitch:
# ----------------------------------------------------------------------
# Copyright (c) 2008, 2009, 2010, 2011 Nicira Networks.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at:
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

dnl PSPP_ENABLE_WERROR
AC_DEFUN([PSPP_ENABLE_WERROR],
  [AC_ARG_ENABLE(
     [Werror],
     [AC_HELP_STRING([--enable-Werror], [Add -Werror to CFLAGS])],
     [], [enable_Werror=no])
   AC_CONFIG_COMMANDS_PRE(
     [if test "X$enable_Werror" = Xyes; then
        CFLAGS="$CFLAGS -Werror"
      fi])])

