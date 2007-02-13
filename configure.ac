dnl Process this file with autoconf to produce a configure script.

dnl Initialize.
AX_PREREQ(2.57)
AC_INIT(pspp, 0.4.2,bug-gnu-pspp@gnu.org)
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE(1.9)

dnl Checks for programs.
AC_GNU_SOURCE
AC_PROG_CC
gl_EARLY
AM_PROG_CC_C_O
AC_LIBTOOL_DLOPEN
AC_PROG_LIBTOOL

PSPP_ENABLE_WARNING(-Wdeclaration-after-statement)

AM_CONDITIONAL(cc_is_gcc, test x"$GCC" = x"yes" )


PSPP_PERL

dnl Internationalization macros.
AM_GNU_GETTEXT
AM_GNU_GETTEXT_VERSION([0.14.5])

dnl Checks for libraries.
AC_SYS_LARGEFILE
AC_FUNC_FSEEKO
AC_CHECK_LIB(m, sin)
PSPP_LIBPLOT
AM_CONDITIONAL(WITHCHARTS, test x"$with_libplot" != x"no")


AC_ARG_WITH(gui, [  --without-gui         don't build the PSPPIRE gui])

if test x"$with_gui" != x"no" ; then 
	PKG_CHECK_MODULES(GTK, gtk+-2.0 >= 2.6.0)
	PKG_CHECK_MODULES(GLADE, libglade-2.0)
fi
AM_CONDITIONAL(WITHGUI, test x"$with_gui" != x"no")

AC_ARG_WITH(gui_tools, [  --with-gui-tools          build the gui developer tools])
if test x"$with_gui_tools" = x"yes" ; then 
	PKG_CHECK_MODULES(GLADE_UI, libgladeui-1.0)
fi
AM_CONDITIONAL(WITH_GUI_TOOLS, test x"$with_gui_tools" = x"yes")

PSPP_OFF_T

AC_CHECK_LIB(gslcblas,main,,[PSPP_REQUIRED_PREREQ([libgslcblas])])
AC_CHECK_LIB(gsl, gsl_cdf_chisq_Q,,[PSPP_REQUIRED_PREREQ([libgsl (version 1.4 or later)])])

AC_CHECK_LIB(ncurses, tgetent)
AC_CHECK_HEADERS(termcap.h)

PSPP_READLINE

dnl Checks for header files.
AC_CHECK_HEADERS([limits.h memory.h sys/stat.h sys/time.h sys/types.h \
                  fpu_control.h sys/mman.h sys/wait.h ieeefp.h fenv.h] )

# For gnulib.
gl_INIT

AC_C_CONST
AC_C_INLINE

dnl  Dont use AC_TYPE_OFF_T --- it doesnt generate the HAVE_TYPE macro
AC_CHECK_TYPES(off_t) 
AC_CHECK_SIZEOF(float, 0)
AC_CHECK_SIZEOF(double, 8)
AC_CHECK_SIZEOF(long double, 0)

AC_DEFINE(FPREP_IEEE754, 1,
	[Define for machines that have IEEE 754 floating point arithmetic,
         the most common format today.])

AC_C_BIGENDIAN

AC_FUNC_VPRINTF
AC_CHECK_FUNCS([__setfpucw execl fork isinf isnan finite getpid feholdexcept popen round trunc])

AC_PROG_LN_S

dnl This is needed otherwise --with-included-gettext fails
AH_BOTTOM([#include <locale.h>])

AC_ARG_ENABLE(debug, [  --enable-debug          Turn on diagnostic features in the program])
if test x"$enable_debug" = x"yes"  ; then
  AC_DEFINE(DEBUGGING, 1, [Define to 1 if debugging is enabled.])
fi

# iconv is required
if test "$am_cv_func_iconv" != "yes"; then
   PSPP_REQUIRED_PREREQ([iconv (see http://www.gnu.org/software/libiconv/)])
fi

PSPP_CHECK_PREREQS

AC_CONFIG_FILES([Makefile gl/Makefile intl/Makefile po/Makefile.in])

AC_OUTPUT

dnl configure.ac ends here
