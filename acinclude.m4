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

dnl aclocal.m4 ends here
