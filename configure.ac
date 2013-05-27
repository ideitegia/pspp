dnl Process this file with autoconf to produce a configure script.

dnl Initialize.
AC_PREREQ(2.63)
AC_INIT([GNU PSPP], [0.7.10], [bug-gnu-pspp@gnu.org], [pspp])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_TESTDIR([tests])
AM_INIT_AUTOMAKE

dnl Checks for programs.
AC_GNU_SOURCE
AC_PROG_CC
gl_EARLY
AM_PROG_CC_C_O
AC_LIBTOOL_WIN32_DLL
AC_LIBTOOL_DLOPEN
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG
m4_pattern_forbid([PKG_CHECK_MODULES])
PSPP_CHECK_CLICKSEQUENCE

PSPP_ENABLE_OPTION(-Wdeclaration-after-statement)
PSPP_ENABLE_WERROR

AM_CONDITIONAL(cc_is_gcc, test x"$GCC" = x"yes" )

PSPP_CC_FOR_BUILD
PSPP_PERL

dnl Disable automatic po/ support, because PSPP provides its own po/ support.
AC_PROVIDE([AM_PO_SUBDIRS])
AM_GNU_GETTEXT([external], [need-ngettext])
LIBS="$LIBINTL $LIBS"

dnl Checks for libraries.
AC_SYS_LARGEFILE
AC_SEARCH_LIBS([sin], [m])
PSPP_LC_PAPER


AC_ARG_VAR([PSPP_LDFLAGS], [linker flags to be used for linking the pspp binary only])
AC_ARG_VAR([PSPPIRE_LDFLAGS], [linker flags to be used for linking the psppire binary only])

# Support for Cairo and Pango.
AC_ARG_WITH([cairo],
  [AS_HELP_STRING(
    [--without-cairo], 
    [Don't build support for charts (using Cairo and Pango);
     implies --without-gui])],
  [], [with_cairo=yes])
AM_CONDITIONAL([HAVE_CAIRO], [test "$with_cairo" != no])
if test "$with_cairo" != no; then
  PKG_CHECK_MODULES([CAIRO], [cairo >= 1.5 pango >= 1.22 pangocairo],
    [CPPFLAGS="$CPPFLAGS $CAIRO_CFLAGS"
     AC_DEFINE([HAVE_CAIRO], 1, 
       [Define to 1 if Cairo and Pango are available.])],
    [PSPP_REQUIRED_PREREQ([cairo 1.5 or later and pango 1.22 or later (or use --without-cairo)])])
  AC_PATH_PROG([XMLLINT], [xmllint], [echo], [$PATH])
  AC_SUBST(XMLLINT)
fi

# Support for GUI.
AC_ARG_WITH([gui], 
  [AS_HELP_STRING([--without-gui], 
                  [Don't build the PSPPIRE GUI (using GTK+)])],
  [], [with_gui=yes])
AM_CONDITIONAL([HAVE_GUI], 
               [test "$with_cairo" != no && test "$with_gui" != "no"])
if test "$with_cairo" != no && test "$with_gui" != "no"; then
  PKG_CHECK_MODULES([GTHREAD], [gthread-2.0], [],
    [PSPP_REQUIRED_PREREQ([gthread 2.0 (or use --without-gui)])])

  PKG_CHECK_MODULES([GTK], [gtk+-3.0 >= 3.4.2], [],
    [PSPP_REQUIRED_PREREQ([gtk+ 3.0 version 3.4.2 or later (or use --without-gui)])])

  PKG_CHECK_MODULES([GTKSOURCEVIEW], [gtksourceview-3.0 >= 3.4.2], [],
    [PSPP_REQUIRED_PREREQ([gtksourceview 3.0 version 3.4.2 or later (or use --without-gui)])])

  AC_ARG_VAR([GLIB_GENMARSHAL])
  AC_CHECK_PROGS([GLIB_GENMARSHAL], [glib-genmarshal])
  if test "x$GLIB_GENMARSHAL" = x; then
    PSPP_REQUIRED_PREREQ([glib-genmarshal (or use --without-gui)])
  fi

fi

gl_NEXT_HEADERS([gtk/gtk.h])

dnl Checks needed for psql reader

dnl The 8.x series postgres tarballs installs with a default prefix of
dnl /usr/local/pgsql whereas, OSes (such as debian)  install into /usr .

dnl If the libpq.so file is in a non standard path, (one which the linker
dnl doesn't search) then we must set the PG_LDFLAGS to -R/usr/wherever/
dnl However, if the system happens to have more than one version of other
dnl dependent libraries (eg. libgtk) in different standard paths. (eg:
dnl /usr and /usr/local) then the wrong one may get linked. 

dnl To overcome this, we first check for libpq in the standard places, and
dnl only set PG_LDFLAGS if it's not found.


AC_ARG_WITH(
  libpq, 
  [AS_HELP_STRING([--without-libpq], [don't compile in Postgres capability])])
AC_ARG_VAR([PG_CONFIG], 
           [Full pathname of the pg_config program shipped with Postgresql])
AC_PATH_PROG([PG_CONFIG], [pg_config], [], 
             [$PATH:/usr/local/pgsql/bin:/usr/pgsql/bin])

if test x"$with_libpq" != x"no" && test -n "$PG_CONFIG" ; then 
  AC_SUBST(PG_CONFIG)
  PG_CFLAGS=-I`$PG_CONFIG --includedir` 
  AC_SUBST(PG_CFLAGS)

  AC_CHECK_LIB([pq], [PQexec],
     [:], 
     [PG_LDFLAGS="-L`$PG_CONFIG --libdir` -R`$PG_CONFIG --libdir`"])
  AC_SUBST(PG_LDFLAGS)

  PG_LIBS=-lpq
  AC_SUBST(PG_LIBS)

  PKG_CHECK_EXISTS(libssl,
	AC_DEFINE([USE_SSL],1,
		   [Define to 1 if the openssl library is present.])
	   ,)

  # Now verify that we can actually link against libpq.  If we are
  # cross-compiling and picked up a host libpq, or if libpq is just
  # badly installed, this will fail.
  AC_CACHE_CHECK(
    [whether -lpq links successfully],
    [pspp_cv_have_libpq],
    [save_CPPFLAGS=$CPPFLAGS
     save_LIBS=$LIBS
     save_LDFLAGS=$LDFLAGS
     CPPFLAGS="$PG_CFLAGS $CPPFLAGS"
     LIBS="$PG_LIBS $LIBS"
     LDFLAGS="$PG_LDFLAGS $LDFLAGS"
     AC_LINK_IFELSE(
       [AC_LANG_PROGRAM(
	 [#include <libpq-fe.h>
	 ], [PQfinish (0);])],
       [pspp_cv_have_libpq=yes],
       [pspp_cv_have_libpq=no])
     CPPFLAGS=$save_CPPFLAGS
     LIBS=$save_LIBS
     LDFLAGS=$save_LDFLAGS])

  if test $pspp_cv_have_libpq = yes; then
    PSQL_SUPPORT=yes
    AC_DEFINE([PSQL_SUPPORT], [1],
      [Define to 1 if building in support for reading from postgres
       databases.])
  else
    PSQL_SUPPORT=no
    PG_CONFIG=
    PG_CFLAGS=
    PG_LDFLAGS=
    PG_LIBS=
  fi
else
  PSQL_SUPPORT=no
  pspp_cv_have_libpq=no
fi
AC_SUBST([PSQL_SUPPORT])

dnl Check for libxml2
PKG_CHECK_MODULES(
  [LIBXML2], [libxml-2.0], 
  [HAVE_LIBXML2=yes],
  [HAVE_LIBXML2=no
   PSPP_OPTIONAL_PREREQ([libxml2])])

dnl Check for zlib.
AC_SEARCH_LIBS(
  [gzopen], [z],
  [HAVE_ZLIB=yes],
  [HAVE_ZLIB=no
   PSPP_OPTIONAL_PREREQ([zlib])])
AC_CHECK_HEADERS(
  [zlib.h], 
  [],
  [HAVE_ZLIB=no
   PSPP_OPTIONAL_PREREQ([zlib])])

dnl Avoid interference between zlib's crc32() function and gnulib's
dnl crc32() function, which have different signatures.  PSPP calls
dnl only gnulib's crc32() directly, but it calls zlib's crc32()
dnl indirectly through other zlib calls.  If we don't rename gnulib's
dnl crc32(), then zlib will call gnulib's version instead of its own,
dnl causing failures.  (This can happen even if zlib isn't detected
dnl directly, because other libraries that PSPP uses, e.g. libpng,
dnl sometimes use zlib themselves.)
AC_DEFINE(
  [crc32], [gl_crc32],
  [Avoid making zlib call gnulib's crc32() instead of its own.])

dnl Gnumeric and OpenDocument (read) support requires libxml2 and zlib.
if test $HAVE_LIBXML2 = yes && test $HAVE_ZLIB = yes; then
  GNM_SUPPORT=yes
  ODF_READ_SUPPORT=yes
  AC_DEFINE(
    [GNM_SUPPORT], [1],
    [Define to 1 if building in support for reading Gnumeric files.])
  AC_DEFINE(
    [ODF_READ_SUPPORT], [1],
    [Define to 1 if building in support for reading OpenDocument files.])
else
  GNM_SUPPORT=no
  ODF_READ_SUPPORT=no
fi
AC_SUBST([GNM_SUPPORT])
AC_SUBST([ODF_READ_SUPPORT])

dnl ODF support requires libxml2 (zlib is optional).
if test $HAVE_LIBXML2 = yes; then
  AC_DEFINE(
    [ODF_WRITE_SUPPORT], [1],
    [Define to 1 if building in support for writing OpenDocument files.])
fi
AM_CONDITIONAL([ODF_WRITE_SUPPORT], [test $HAVE_LIBXML2 = yes])

AC_ARG_WITH(
  gui_tools,
  [AS_HELP_STRING([--with-gui-tools], [build the gui developer tools.  For DEVELOPERS only! There is no reason why users will need this flag.])])
if test x"$with_gui_tools" = x"yes" ; then 
	PKG_CHECK_MODULES(GLADE_UI, gladeui-1.0)
fi
AM_CONDITIONAL(WITH_GUI_TOOLS, test x"$with_gui_tools" = x"yes")

AC_ARG_WITH(
  [perl-module],
  [AS_HELP_STRING([--without-perl-module], [do not build the Perl module])],
  [case $with_perl_module in # (
     yes|no) ;; # (
     *) AC_MSG_FAILURE([--with-perl-module argument must be 'yes' or 'no']) ;;
   esac
   WITH_PERL_MODULE=$with_perl_module],
  [if test x"$cross_compiling" != x"yes"; then
     WITH_PERL_MODULE=yes
   else
     WITH_PERL_MODULE=no
   fi])
AC_SUBST([WITH_PERL_MODULE])
AM_CONDITIONAL(WITH_PERL_MODULE, test $WITH_PERL_MODULE = yes)

AC_SEARCH_LIBS([cblas_dsdot], [gslcblas],,[PSPP_REQUIRED_PREREQ([libgslcblas])])
PKG_CHECK_MODULES([GSL], [gsl >= 1.12], [],
			 AC_SEARCH_LIBS([gsl_linalg_cholesky_invert], [gsl],,[PSPP_REQUIRED_PREREQ([gsl 2.0 version 1.12 or later])]))


PSPP_GSL_NEEDS_FGNU89_INLINE

dnl Recent versions of GNU ncurses install the curses header files into
dnl /usr/include/ncurses, and provide a 'ncurses5-config' program which
dnl enables us to discover where they are. Earlier versions don't have
dnl this, so we can't rely on it.  So if ncurses5-config is present,
dnl we'll trust it to find the right information.  Otherwise, we'll
dnl try to discover it ourselves.
dnl To confound things further, Cygwin has decided to rename ncurses5-config
dnl to ncurses8-config !!!
AC_ARG_WITH(
  libncurses, 
  [AS_HELP_STRING([--without-libncurses], [don't compile in ncurses functions])])

if test x"$with_libncurses" != x"no" ; then
  if test x"$cross_compiling" != x"yes" ; then
	  AC_CHECK_PROGS([NCURSES_CONFIG], [ncurses5-config ncurses8-config])
  fi
  if test x"$NCURSES_CONFIG" = x ; then
	  AC_SEARCH_LIBS([tgetent], [ncurses],
	      [curses_available=yes; AC_CHECK_HEADERS([term.h curses.h],,[curses_available=no])])
  else
          old_cflags=$CFLAGS
          CFLAGS="$CFLAGS `$NCURSES_CONFIG --cflags`"
	  AC_CHECK_HEADERS([term.h curses.h],[curses_available=yes])
	  CFLAGS=$old_cflags
	  if test x"$curses_available" = x"yes" ; then
	   NCURSES_LIBS=`$NCURSES_CONFIG --libs`
	   NCURSES_CFLAGS=`$NCURSES_CONFIG --cflags`
	   AC_SUBST(NCURSES_CFLAGS)
	   AC_SUBST(NCURSES_LIBS)
          fi
  fi
  if test x"$curses_available" = x"yes" ; then
	  AC_DEFINE([LIBNCURSES_USABLE], 1,
	      [Define to 1 if the libncurses is both present and usable.])
  fi
  AC_CHECK_HEADERS([termcap.h])
fi

PSPP_READLINE

dnl Checks for header files.
AC_CHECK_HEADERS([sys/wait.h fpu_control.h ieeefp.h fenv.h pwd.h])

dnl Some systems dont have SIGWINCH
AC_CHECK_DECLS([SIGWINCH], [], [],
          [#include <signal.h>
          /* NetBSD declares sys_siglist in unistd.h.  */
          #ifdef HAVE_UNISTD_H
          # include <unistd.h>
          #endif
          ])


# For gnulib.
gl_INIT

AC_C_INLINE

AC_CHECK_SIZEOF([size_t])
SIZEOF_SIZE_T=$ac_cv_sizeof_size_t
AC_SUBST([SIZEOF_SIZE_T])

AC_C_BIGENDIAN

AC_CHECK_FUNCS([__setfpucw fork execl isinf isnan finite getpid feholdexcept fpsetmask popen round])

AC_PROG_LN_S

AC_ARG_ENABLE(
  debug, 
  [AS_HELP_STRING([--enable-debug], 
                  [Turn on diagnostic features in the program])])
if test x"$enable_debug" = x"yes"  ; then
  AC_DEFINE(DEBUGGING, 1, [Define to 1 if debugging is enabled.])
fi

# iconv is required
AM_ICONV
if test "$am_cv_func_iconv" != "yes"; then
   PSPP_REQUIRED_PREREQ([iconv (see http://www.gnu.org/software/libiconv/)])
fi
LIBS="$LIBICONV $LIBS"

dnl Required by the gnulib 'relocatable-prog' module.
dnl See doc/relocatable-maint.texi in the gnulib tree for details.
RELOCATABLE_LIBRARY_PATH='$(pkglibdir)'
RELOCATABLE_STRIP=:

PSPP_CHECK_PREREQS

AC_CONFIG_FILES(
  [Makefile gl/Makefile po/Makefile tests/atlocal perl-module/lib/PSPP.pm])

AC_OUTPUT
echo "PSPP configured successfully."

dnl configure.ac ends here
