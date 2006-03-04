dnl Copyright (C) 2005, 2006 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl Check longest integer in digits.

AC_DEFUN([BLP_INT_DIGITS],
[
AC_MSG_CHECKING(number of digits in LONG_MIN (incl. sign))
AC_CACHE_VAL(blp_int_digits,
	     [AC_TRY_RUN([#include <stdio.h>
                          #include <limits.h>
			  int
			  main()
			  {
			    int len;
			    char s[80];
			    sprintf(s, "%ld", LONG_MAX);
			    len = strlen(s);
			    sprintf(s, "%ld", LONG_MIN);
			    if(strlen(s)>len) len=strlen(s);
			    sprintf(s, "%lu", ULONG_MAX);
			    if(strlen(s)>len) len=strlen(s);
			    exit(len);
			  }
			 ],
			 eval "blp_int_digits=19",
			 eval "blp_int_digits=$?"
			 if test "$blp_int_digits" -lt 11; then
			   blp_int_digits=11
			 fi,
			 eval "blp_int_digits=19")
	     ])
AC_DEFINE_UNQUOTED([INT_DIGITS], $blp_int_digits,
	[Number of digits in longest `long' value, including sign.
         This is usually 11, for 32-bit `long's, or 19, for 64-bit
         `long's.])
AC_MSG_RESULT($blp_int_digits) ])dnl

dnl Check quality of this machine's sprintf implementation.

AC_DEFUN([BLP_IS_SPRINTF_GOOD],
[
AC_MSG_CHECKING(if sprintf returns a char count)
AC_CACHE_VAL(blp_is_sprintf_good,
             [AC_TRY_RUN([#include <stdio.h>
                          int 
                          main()
                          {
                            char s[8];
                            exit((int)sprintf(s, "abcdefg")!=7);
                          }
                         ], 
                         eval "blp_is_sprintf_good=yes",
			 eval "blp_is_sprintf_good=no",
			 eval "blp_is_sprintf_good=no")
             ])
if test "$blp_is_sprintf_good" = yes; then
  AC_DEFINE([HAVE_GOOD_SPRINTF], 1, 
	[Define if sprintf() returns the number of characters written
         to the destination string, excluding the null terminator.])
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi
])dnl

dnl Check for proper random number generator.

AC_DEFUN([BLP_RANDOM],
[
AC_MSG_CHECKING(random number generator)
AC_CACHE_VAL(blp_random_good, 
  AC_TRY_COMPILE([#include <stdlib.h>], [int x=RAND_MAX;], 
    blp_random_good=yes, blp_random_good=no))
if test "$blp_random_good" = yes; then
  AC_DEFINE([HAVE_GOOD_RANDOM], 1, 
	[Define if rand() and company work according to ANSI.])
  AC_MSG_RESULT(good)
else
  AC_MSG_RESULT(bad)
fi
])dnl

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
