/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#if TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#if HAVE_LIBTERMCAP
#if HAVE_TERMCAP_H
#include <termcap.h>
#else /* !HAVE_TERMCAP_H */
int tgetent (char *, char *);
int tgetnum (char *);
#endif /* !HAVE_TERMCAP_H */
#endif /* !HAVE_LIBTERMCAP */

#if HAVE_LIBHISTORY
#if HAVE_READLINE_HISTORY_H
#include <readline/history.h>
#else /* no readline/history.h */
extern void using_history ();
extern int read_history ();
extern void stifle_history ();
#endif /* no readline/history.h */
#endif /* -lhistory */

#if HAVE_FPU_CONTROL_H
#include <fpu_control.h>
#elif __BORLANDC__
#include <float.h>
#include <math.h>
#endif

#if __DJGPP__
#include <conio.h>
#elif __WIN32__ && __BORLANDC__
#undef gettext
#include <conio.h>
#define gettext(STRING)				\
	STRING
#endif

#if HAVE_LOCALE_H
#include <locale.h>
#endif

#if HAVE_FENV_H
#include <fenv.h>
#endif

#include "alloc.h"
#include "avl.h"
#include "command.h"
#include "do-ifP.h"
#include "error.h"
#include "expr.h"
#include "filename.h"
#include "getline.h"
#include "julcal/julcal.h"
#include "lexer.h"
#include "main.h"
#include "settings.h"
#include "str.h"
#include "var.h"
#include "version.h"
#include "vfm.h"

/* var.h */
struct dictionary default_dict;
struct expression *process_if_expr;

struct ccase *temp_case;

struct trns_header **t_trns;
int n_trns;
int m_trns;
int f_trns;

int FILTER_before_TEMPORARY;

struct file_handle *default_handle;

void (*read_active_file) (void);
void (*cancel_input_pgm) (void);

struct ctl_stmt *ctl_stack;

/* log.h */
char *logfn;
FILE *logfile;
int logging;

/* Functions. */

static void get_date (void);

#if HAVE_LIBTERMCAP && !__CHECKER__
static char *term_buffer;
#endif

void
init_glob (int argc unused, char **argv)
{
  /* FIXME: Allow i18n of other locale items (besides LC_MESSAGES). */
#if ENABLE_NLS
#if LC_MESSAGE
  setlocale (LC_MESSAGES, "");
#endif
  setlocale (LC_MONETARY, "");
  bindtextdomain (PACKAGE, locale_dir);
  textdomain (PACKAGE);
#endif /* ENABLE_NLS */

  /* Workable defaults before we determine the real terminal size. */
  set_viewwidth = 79;
  set_viewlength = 24;

  fn_init ();
  getl_initialize ();

  /* PORTME: If your system/OS has the nasty tendency to halt with a
     SIGFPE whenever there's a floating-point overflow (or other
     exception), be sure to mask off those bits in the FPU here.
     PSPP wants a guarantee that, no matter what boneheaded
     floating-point operation it performs, the process will not halt.  */
#if HAVE_FEHOLDEXCEPT
  {
    fenv_t foo;

    feholdexcept (&foo);
  }
#elif HAVE___SETFPUCW && defined(_FPU_IEEE)
  __setfpucw (_FPU_IEEE);
#elif __BORLANDC__
  _control87 (0xffff, 0x137f);
#endif

#if ENDIAN==UNKNOWN
  {
    /* Test for endianness borrowed from acspecific.m4, which was in
     turn borrowed from Harbison&Steele. */
    union
      {
	long l;
	char c[sizeof (long)];
      }
    u;

    u.l = 1;
    if (u.c[sizeof u.l - 1] == 1)
      endian = BIG;
    else if (u.c[0] == 1)
      endian = LITTLE;
    else
      msg (FE, _("Your machine does not appear to be either big- or little-"
		 "endian.  At the moment, PSPP only supports machines of "
		 "these standard endiannesses.  If you want to hack in "
		 "others, contact the author."));
  }
#endif

  /* PORTME: Set the value for second_lowest_value, which is the
     "second lowest" possible value for a double.  This is the value
     for LOWEST on MISSING VALUES, etc. */
#ifndef SECOND_LOWEST_VALUE
#if FPREP == FPREP_IEEE754
  {
    union
      {
	unsigned char c[8];
	double d;
      }
    second_lowest_little = {{0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xff}},
    second_lowest_big = {{0xff, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe}};

    if (endian == LITTLE)
      second_lowest_value = second_lowest_little.d;
    else if (endian == BIG)
      second_lowest_value = second_lowest_big.d;
  }
#else /* FPREP != FPREP_IEEE754 */
#error Unknown floating-point representation.
#endif /* FPREP != FPREP_IEEE754 */
#endif /* !SECOND_LOWEST_VALUE */

  /* var.h */
  default_dict.var_by_name = avl_create (NULL, cmp_variable, NULL);

  vec_init (&reinit_sysmis);
  vec_init (&reinit_blanks);
  vec_init (&init_zero);
  vec_init (&init_blanks);

  last_vfm_invocation = time (NULL);

  /* lexer.h */
  ds_init (NULL, &tokstr, 64);

  /* common.h */
  {
    char *cp;
    
    pgmname = argv[0];
    for (;;)
      {
	cp = strchr (pgmname, DIR_SEPARATOR);
	if (!cp)
	  break;
	pgmname = &cp[1];
      }
    cur_proc = NULL;
  }

  /* settings.h */
#if !USE_INTERNAL_PAGER
  {
    char *pager;

    pager = getenv ("STAT_PAGER");
    if (!pager)
      pager = getenv ("PAGER");
    if (pager)
      set_pager = xstrdup (pager);
#if DEFAULT_PAGER
    else
      set_pager = xstrdup (DEFAULT_PAGER);
#endif /* DEFAULT_PAGER */
  }
#endif /* !USE_INTERNAL_PAGER */

  set_blanks = SYSMIS;
  set_scompression = 1;
  set_format.type = FMT_F;
  set_format.w = 8;
  set_format.d = 2;
  set_cpi = 6;
  set_lpi = 10;
  set_results_file = xstrdup ("pspp.prc");
  set_dprompt = xstrdup (_("data> "));
  
  {
    int i;
    
    for (i = 0; i < 5; i++)
      {
	struct set_cust_currency *cc = &set_cc[i];
	strcpy (cc->buf, "-");
	cc->neg_prefix = cc->buf;
	cc->prefix = &cc->buf[1];
	cc->suffix = &cc->buf[1];
	cc->neg_suffix = &cc->buf[1];
	cc->decimal = '.';
	cc->grouping = ',';
      }
  }
  
  set_decimal = '.';
  set_grouping = ',';
  set_headers = 1;
  set_journaling = 1;
  set_journal = xstrdup ("pspp.jnl");
  set_messages = 1;
  set_mexpand = 1;
  set_mprint = 1;
  set_mxerrs = 50;
  set_mxwarns = 100;
  set_printback = 1;
  set_undefined = 1;

  set_cprompt = xstrdup ("    > ");
  set_echo = 0;
  set_endcmd = '.';
  set_errorbreak = 0;
  set_include = 1;
  set_nullline = 1;
  set_more = 1;
  set_prompt = xstrdup ("PSPP> ");
  set_seed = 2000000;

#if __DJGPP__ || __BORLANDC__
  {
    struct text_info ti;

    gettextinfo (&ti);
    set_viewlength = max (ti.screenheight, 25);
    set_viewwidth = max (ti.screenwidth, 79);
  }
#elif HAVE_LIBTERMCAP
  {
    char *termtype;
    int success;

    /* This code stolen from termcap.info, though modified. */
#if !__CHECKER__
    term_buffer = xmalloc (2048);
#endif

    termtype = getenv ("TERM");
    if (!termtype)
      msg (FE, _("Specify a terminal type with `setenv TERM <yourtype>'."));

#if __CHECKER__
    success = tgetent (NULL, termtype);
#else
    success = tgetent (term_buffer, termtype);
#endif

    if (success <= 0)
      {
	if (success < 0)
	  msg (IE, _("Could not access the termcap data base."));
	else
	  msg (IE, _("Terminal type `%s' is not defined."), termtype);
	msg (MM, _("Assuming screen of size 79x25."));
	set_viewlength = 25;
	set_viewwidth = 79;
      }
    else
      {
	set_viewlength = tgetnum ("li");
	set_viewwidth = tgetnum ("co") - 1;
      }
  }
#else /* !HAVE_LIBTERMCAP */
  set_viewlength = 25;
  set_viewwidth = 79;
#endif /* !HAVE_LIBTERMCAP */

  /* log.h */
  logging = 1;
  logfn = xstrdup ("pspp.log");
  logfile = NULL;

  /* file-handle.h */
  {
    extern void fh_init_files (void);
    
    fh_init_files ();
  }
  
  get_date ();
}

static void
get_date ()
{
  static const char *months[12] =
    {
      N_("Jan"), N_("Feb"), N_("Mar"), N_("Apr"), N_("May"), N_("Jun"),
      N_("Jul"), N_("Aug"), N_("Sep"), N_("Oct"), N_("Nov"), N_("Dec"),
    };

  time_t t;
  int mn, dy, yr;
  struct tm *tmp;

  if ((time_t) -1 == time (&t))
    {
      strcpy (curdate, "1 Jan 1970");
      return;
    }
  tmp = localtime (&t);

  mn = tmp->tm_mon;
  if (mn < 0)
    mn = 0;
  if (mn > 11)
    mn = 11;

  dy = tmp->tm_mday;
  if (dy < 0)
    dy = 0;
  if (dy > 99)
    dy = 99;

  yr = tmp->tm_year + 1900;
  if (yr < 0)
    yr = 0;
  if (yr > 9999)
    yr = 9999;

  sprintf (curdate, "%2d %s %04d", dy, gettext (months[mn]), yr);
}

int
cmp_variable (const void *a, const void *b, void *foo unused)
{
  return strcmp (((struct variable *) a)->name, ((struct variable *) b)->name);
}

#if __BORLANDC__
int
_RTLENTRY _EXPFUNC _matherr (struct exception _FAR *__e)
{
  return 1;
}

int
_RTLENTRY _EXPFUNC _matherrl (struct _exceptionl _FAR *__e)
{
  return 1;
}
#endif
