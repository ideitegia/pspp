dnl Process this file with autoconf to produce a configure script.

dnl Initialize.
AC_PREREQ(2.60)
AC_INIT([pspp],[0.7.2],[bug-gnu-pspp@gnu.org])
AC_CONFIG_HEADERS([config.h])
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

AC_ARG_ENABLE(
  anachronistic-dependencies, 
  [AS_HELP_STRING([--enable-anachronistic-dependencies], 
                  [Attempt to allow building against older versions of dependent libraries])])

PSPP_ENABLE_OPTION(-Wdeclaration-after-statement)

AM_CONDITIONAL(cc_is_gcc, test x"$GCC" = x"yes" )

PSPP_CC_FOR_BUILD
PSPP_PERL

dnl Internationalization macros.
AC_ARG_ENABLE(nls, [AS_HELP_STRING([--disable-nls], [do not use Native Language Support])])
if  test x"$enable_nls" != x"no"  ; then
 AC_DEFINE(ENABLE_NLS, 1, [Define to 1 if translation of program messages to the user's native language is requested.])
fi


dnl Checks for libraries.
AC_SYS_LARGEFILE
AC_SEARCH_LIBS([sin], [m])
PSPP_LIBPLOT
PSPP_LC_PAPER
AM_CONDITIONAL(WITHCHARTS, test x"$with_libplot" != x"no")


AC_ARG_VAR([PSPP_LDFLAGS], [linker flags to be used for linking the pspp binary only])
AC_ARG_VAR([PSPPIRE_LDFLAGS], [linker flags to be used for linking the psppire binary only])


AC_ARG_WITH(
  gui, 
  [AS_HELP_STRING([--without-gui], [don't build the PSPPIRE gui])])

required_gtk_version=2.12

if test x"$with_gui" != x"no" ; then 
  PKG_CHECK_MODULES(GTK, gtk+-2.0 >= $required_gtk_version,,
    [PSPP_REQUIRED_PREREQ([gtk+ 2.0 v$required_gtk_version or later (or use --without-gui)])])
fi
AM_CONDITIONAL(WITHGUI, test x"$with_gui" != x"no")


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

if test x"$with_libpq" != x"no" ; then
 AC_SEARCH_LIBS([PQexec], [pq], [libpq_in_std_lib_path=yes])

 AC_ARG_VAR([PG_CONFIG], [Full pathname of the pg_config program shipped with Postgresql])
 if test x"$cross_compiling" != x"yes" ; then
   AC_PATH_PROG([PG_CONFIG], [pg_config], [], [$PATH:/usr/local/pgsql/bin:/usr/pgsql/bin])
 fi 
 if test -n "$PG_CONFIG" ; then 
   AC_SUBST(PG_CONFIG)
   AC_DEFINE([PSQL_SUPPORT], 1,
   [Define to 1 if building in support for reading from postgres databases.])
   PG_CFLAGS=-I`$PG_CONFIG --includedir` 
   AC_SUBST(PG_CFLAGS)
   
   if test "x$libpq_in_std_lib_path" != x"yes" ; then
      PG_LDFLAGS="-L`$PG_CONFIG --libdir` -R`$PG_CONFIG --libdir`"
   fi
   AC_SUBST(PG_LDFLAGS)

   PG_LIBS=-lpq
   AC_SUBST(PG_LIBS)
   PKG_CHECK_EXISTS(libssl,
         AC_DEFINE([USE_SSL],1,
	 	    [Define to 1 if the openssl library is present.])
	    ,)
 fi
fi
AM_CONDITIONAL(PSQL_SUPPORT, test -n "$PG_CONFIG")

dnl Checks needed for Gnumeric reader
gnm_support=yes;
PKG_CHECK_MODULES(LIBXML2, libxml-2.0,,
			   [PSPP_OPTIONAL_PREREQ([libxml2]); gnm_support=no;]);
AC_SEARCH_LIBS(gzopen,z,,[PSPP_OPTIONAL_PREREQ([zlib]); gnm_support=no;])
AC_CHECK_HEADERS(zlib.h,,[PSPP_OPTIONAL_PREREQ([zlib]); gnm_support=no;])

if test x"$gnm_support" = x"yes" ; then 
   AC_DEFINE([GNM_SUPPORT], 1,
   [Define to 1 if building in support for reading Gnumeric files.])
fi
AM_CONDITIONAL(GNM_SUPPORT, test x"$gnm_support" = x"yes")

AC_ARG_WITH(
  gui_tools,
  [AS_HELP_STRING([--with-gui-tools], [build the gui developer tools.  For DEVELOPERS only! There is no reason why users will need this flag.])])
if test x"$with_gui_tools" = x"yes" ; then 
	PKG_CHECK_MODULES(GLADE_UI, gladeui-1.0)
fi
AM_CONDITIONAL(WITH_GUI_TOOLS, test x"$with_gui_tools" = x"yes")

AM_CONDITIONAL(WITH_PERL_MODULE, test x"$cross_compiling" != x"yes")

AC_SEARCH_LIBS([cblas_dsdot], [gslcblas],,[PSPP_REQUIRED_PREREQ([libgslcblas])])
AC_SEARCH_LIBS([gsl_cdf_binomial_P], [gsl],,[PSPP_REQUIRED_PREREQ([libgsl (version 1.8 or later)])])
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
AC_CHECK_HEADERS([sys/wait.h fpu_control.h ieeefp.h fenv.h])

# For gnulib.
gl_INIT

AC_C_INLINE

AC_CHECK_SIZEOF(double)

AC_C_BIGENDIAN

AC_CHECK_FUNCS([__setfpucw fork execl execlp isinf isnan finite getpid feholdexcept fpsetmask popen round])

AC_PROG_LN_S

AC_ARG_ENABLE(
  debug, 
  [AS_HELP_STRING([--enable-debug], 
                  [Turn on diagnostic features in the program])])
if test x"$enable_debug" = x"yes"  ; then
  AC_DEFINE(DEBUGGING, 1, [Define to 1 if debugging is enabled.])
fi

# iconv is required
if test "$am_cv_func_iconv" != "yes"; then
   PSPP_REQUIRED_PREREQ([iconv (see http://www.gnu.org/software/libiconv/)])
fi

PSPP_CHECK_PREREQS

AC_CONFIG_FILES([Makefile gl/Makefile])

AC_OUTPUT
echo "PSPP configured successfully."

dnl configure.ac ends here
