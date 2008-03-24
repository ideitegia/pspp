dnl Process this file with autoconf to produce a configure script.

dnl Initialize.
AX_PREREQ(2.60)
AC_INIT([pspp],[0.4.3],[bug-gnu-pspp@gnu.org])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

dnl Checks for programs.
AC_GNU_SOURCE
AC_PROG_CC
gl_EARLY
AM_PROG_CC_C_O
AC_LIBTOOL_DLOPEN
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG

PSPP_ENABLE_OPTION(-Wdeclaration-after-statement)

AM_CONDITIONAL(cc_is_gcc, test x"$GCC" = x"yes" )

PSPP_CC_FOR_BUILD
PSPP_PERL

dnl Internationalization macros.
AM_GNU_GETTEXT([external], [need-ngettext])
AM_GNU_GETTEXT_VERSION([0.16.1])

dnl Checks for libraries.
AC_SYS_LARGEFILE
AC_CHECK_LIB(m, sin)
PSPP_LIBPLOT
PSPP_LC_PAPER
AM_CONDITIONAL(WITHCHARTS, test x"$with_libplot" != x"no")


AC_ARG_WITH(
  gui, 
  [AS_HELP_STRING([--without-gui], [don't build the PSPPIRE gui])])

if test x"$with_gui" != x"no" ; then 
  PKG_CHECK_MODULES(GTK, gtk+-2.0 >= 2.12.0,,
    [PSPP_REQUIRED_PREREQ([gtk+ 2.0 v2.12.0 or later (or use --without-gui)])])
  PKG_CHECK_MODULES(GLADE, libglade-2.0 >= 2.6.0,,
    [PSPP_REQUIRED_PREREQ([libglade 2.0 (or use --without-gui)])])
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


AC_CHECK_LIB([pq], [PQexec], [libpq_in_std_lib_path=yes])

AC_ARG_VAR([PG_CONFIG], [Full pathname of the pg_config program shipped with Postgresql])
AC_PATH_PROG([PG_CONFIG], [pg_config], [], [$PATH:/usr/local/pgsql/bin:/usr/pgsql/bin])
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
AM_CONDITIONAL(PSQL_SUPPORT, test -n "$PG_CONFIG")

dnl Checks needed for gnumeric reader
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
  [AS_HELP_STRING([--with-gui-tools], [build the gui developer tools])])
if test x"$with_gui_tools" = x"yes" ; then 
	PKG_CHECK_MODULES(GLADE_UI, gladeui-1.0)
fi
AM_CONDITIONAL(WITH_GUI_TOOLS, test x"$with_gui_tools" = x"yes")

AC_CHECK_LIB(gslcblas,main,,[PSPP_REQUIRED_PREREQ([libgslcblas])])
AC_CHECK_LIB(gsl, gsl_cdf_chisq_Q,,[PSPP_REQUIRED_PREREQ([libgsl (version 1.4 or later)])])

AC_CHECK_LIB(ncurses, tgetent)
AC_CHECK_HEADERS(termcap.h)

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

AC_CONFIG_FILES([Makefile gl/Makefile po/Makefile.in])

AC_OUTPUT
echo "PSPP configured successfully."

dnl configure.ac ends here
