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
#include "glob.h"
#include "error.h"
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
#elif defined (__WIN32__) && defined (__BORLANDC__)
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
#include "calendar.h"
#include "command.h"
#include "dictionary.h"
#include "do-ifP.h"
#include "error.h"
#include "file-handle.h"
#include "filename.h"
#include "getline.h"
#include "hash.h"
#include "lexer.h"
#include "magic.h"
#include "main.h"
#include "settings.h"
#include "str.h"
#include "var.h"
#include "version.h"
#include "vfm.h"

/* var.h */
struct dictionary *default_dict;
struct expression *process_if_expr;

struct trns_header **t_trns;
int n_trns;
int m_trns;
int f_trns;

int FILTER_before_TEMPORARY;

struct file_handle *default_handle;

struct ctl_stmt *ctl_stack;

/* log.h */
char *logfn;
FILE *logfile;
int logging;

/* Functions. */

static void get_date (void);


void
init_glob (int argc UNUSED, char **argv)
{
  /* FIXME: Allow i18n of other locale items (besides LC_MESSAGES). */
#if ENABLE_NLS
#if HAVE_LC_MESSAGES
  setlocale (LC_MESSAGES, "");
#endif
  setlocale (LC_MONETARY, "");
  bindtextdomain (PACKAGE, locale_dir);
  textdomain (PACKAGE);
#endif /* ENABLE_NLS */

  fn_init ();
  fh_init ();
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

  /* var.h */
  default_dict = dict_create ();

  last_vfm_invocation = time (NULL);

  /* lexer.h */
  ds_init (&tokstr, 64);

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


  init_settings();

  /* log.h */
  logging = 1;
  logfn = xstrdup ("pspp.log");
  logfile = NULL;

  get_date ();
}

void
done_glob(void)
{
  cancel_transformations ();
  dict_destroy (default_dict);
  free (logfn);
  done_settings ();
  ds_destroy (&tokstr);

  fh_done();
}

static void
get_date (void)
{

  time_t t;
  struct tm *tmp;

  if ((time_t) -1 == time (&t))
    {
      strcpy (curdate, "?? ??? 2???");
      return;
    }
  tmp = localtime (&t);

  strftime (curdate, 12, "%d %b %Y",tmp);
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
