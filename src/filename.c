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
#include "filename.h"
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include "alloc.h"
#include "error.h"
#include "settings.h"
#include "str.h"
#include "version.h"

#include "debug-print.h"

/* PORTME: Everything in this file is system dependent. */

#if unix
#include <pwd.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "stat.h"
#endif

#if __WIN32__
#define NOGDI
#define NOUSER
#define NONLS
#include <win32/windows.h>
#endif

#if __DJGPP__
#include <sys/stat.h>
#endif

/* Initialization. */

const char *config_path;

void
fn_init (void)
{
  config_path = fn_getenv_default ("STAT_CONFIG_PATH", default_config_path);
}

/* Functions for performing operations on filenames. */

/* Substitutes $variables as defined by GETENV into INPUT and returns
   a copy of the resultant string.  Supports $var and ${var} syntaxes;
   $$ substitutes as $. */
char *
fn_interp_vars (const char *input, const char *(*getenv) (const char *))
{
  struct string output;

  if (NULL == strchr (input, '$'))
    return xstrdup (input);

  ds_init (NULL, &output, strlen (input));

  for (;;)
    switch (*input)
      {
      case '\0':
	return ds_value (&output);
	
      case '$':
	input++;

	if (*input == '$')
	  {
	    ds_putchar (&output, '$');
	    input++;
	  }
	else
	  {
	    int stop;
	    int start;
	    const char *value;

	    start = ds_length (&output);

	    if (*input == '(')
	      {
		stop = ')';
		input++;
	      }
	    else if (*input == '{')
	      {
		stop = '}';
		input++;
	      }
	    else
	      stop = 0;

	    while (*input && *input != stop
		   && (stop || isalpha ((unsigned char) *input)))
	      ds_putchar (&output, *input++);
	    
	    value = getenv (ds_value (&output) + start);
	    ds_truncate (&output, start);
	    ds_concat (&output, value);

	    if (stop && *input == stop)
	      input++;
	  }

      default:
	ds_putchar (&output, *input++);
      }
}

#if unix
/* Expands csh tilde notation from the path INPUT into a malloc()'d
   returned string. */
char *
fn_tilde_expand (const char *input)
{
  const char *ip;
  struct string output;

  if (NULL == strchr (input, '~'))
    return xstrdup (input);
  ds_init (NULL, &output, strlen (input));

  ip = input;

  for (ip = input; *ip; )
    if (*ip != '~' || (ip != input && ip[-1] != PATH_DELIMITER))
      ds_putchar (&output, *ip++);
    else
      {
	static const char stop_set[3] = {DIR_SEPARATOR, PATH_DELIMITER, 0};
	const char *cp;
	
	ip++;

	cp = ip + strcspn (ip, stop_set);

	if (cp > ip)
	  {
	    struct passwd *pwd;
	    char username[9];

	    strncpy (username, ip, cp - ip + 1);
	    username[8] = 0;
	    pwd = getpwnam (username);

	    if (!pwd || !pwd->pw_dir)
	      ds_putchar (&output, *ip++);
	    else
	      ds_concat (&output, pwd->pw_dir);
	  }
	else
	  {
	    const char *home = fn_getenv ("HOME");
	    if (!home)
	      ds_putchar (&output, *ip++);
	    else
	      ds_concat (&output, home);
	  }

	ip = cp;
      }

  return ds_value (&output);
}
#else /* !unix */
char *
fn_tilde_expand (char *input)
{
  return xstrdup (input);
}
#endif /* !unix */

/* Searches for a configuration file with name NAME in the path given
   by PATH, which is tilde- and environment-interpolated.  Directories
   in PATH are delimited by PATH_DELIMITER, defined in <pref.h>.
   Returns the malloc'd full name of the first file found, or NULL if
   none is found.

   If PREPEND is non-NULL, then it is prepended to each filename;
   i.e., it looks like PREPEND/PATH_COMPONENT/NAME.  This is not done
   with absolute directories in the path. */
#if unix || __MSDOS__ || __WIN32__
char *
fn_search_path (const char *basename, const char *path, const char *prepend)
{
  char *subst_path;
  struct string filename;
  const char *bp;

  if (fn_absolute_p (basename))
    return fn_tilde_expand (basename);
  
  {
    char *temp = fn_interp_vars (path, fn_getenv);
    bp = subst_path = fn_tilde_expand (temp);
    free (temp);
  }

  msg (VM (4), _("Searching for `%s'..."), basename);
  ds_init (NULL, &filename, 64);

  for (;;)
    {
      const char *ep;
      if (0 == *bp)
	{
	  msg (VM (4), _("Search unsuccessful!"));
	  ds_destroy (&filename);
	  free (subst_path);
	  return NULL;
	}

      for (ep = bp; *ep && *ep != PATH_DELIMITER; ep++)
	;

      /* Paste together PREPEND/PATH/BASENAME. */
      ds_clear (&filename);
      if (prepend && !fn_absolute_p (bp))
	{
	  ds_concat (&filename, prepend);
	  ds_putchar (&filename, DIR_SEPARATOR);
	}
      ds_concat_buffer (&filename, bp, ep - bp);
      if (ep - bp
	  && ds_value (&filename)[ds_length (&filename) - 1] != DIR_SEPARATOR)
	ds_putchar (&filename, DIR_SEPARATOR);
      ds_concat (&filename, basename);
      
      msg (VM (5), " - %s", ds_value (&filename));
      if (fn_exists_p (ds_value (&filename)))
	{
	  msg (VM (4), _("Found `%s'."), ds_value (&filename));
	  free (subst_path);
	  return ds_value (&filename);
	}

      if (0 == *ep)
	{
	  msg (VM (4), _("Search unsuccessful!"));
	  free (subst_path);
	  ds_destroy (&filename);
	  return NULL;
	}
      bp = ep + 1;
    }
}
#else /* not unix, msdog, lose32 */
char *
fn_search_path (const char *basename, const char *path, const char *prepend)
{
  size_t size = strlen (path) + 1 + strlen (basename) + 1;
  char *string;
  char *cp;
  
  if (prepend)
    size += strlen (prepend) + 1;
  string = xmalloc (size);
  
  cp = string;
  if (prepend)
    {
      cp = stpcpy (cp, prepend);
      *cp++ = DIR_SEPARATOR;
    }
  cp = stpcpy (cp, path);
  *cp++ = DIR_SEPARATOR;
  strcpy (cp, basename);

  return string;
}
#endif /* not unix, msdog, lose32 */

/* Prepends directory DIR to filename FILE and returns a malloc()'d
   copy of it. */
char *
fn_prepend_dir (const char *file, const char *dir)
{
  char *temp;
  char *cp;
  
  if (fn_absolute_p (file))
    return xstrdup (file);

  temp = xmalloc (strlen (file) + 1 + strlen (dir) + 1);
  cp = stpcpy (temp, dir);
  if (cp != temp && cp[-1] != DIR_SEPARATOR)
    *cp++ = DIR_SEPARATOR;
  cp = stpcpy (cp, file);

  return temp;
}

/* fn_normalize(): This very OS-dependent routine canonicalizes
   filename FN1.  The filename should not need to be the name of an
   existing file.  Returns a malloc()'d copy of the canonical name.
   This function must always succeed; if it needs to bail out then it
   should return xstrdup(FN1).  */
#if unix
char *
fn_normalize (const char *filename)
{
  const char *src;
  char *fn1, *fn2, *dest;
  int maxlen;

  if (fn_special_p (filename))
    return xstrdup (filename);
  
  fn1 = fn_tilde_expand (filename);

  /* Follow symbolic links. */
  for (;;)
    {
      fn2 = fn1;
      fn1 = fn_readlink (fn1);
      if (!fn1)
	{
	  fn1 = fn2;
	  break;
	}
      free (fn2);
    }

  maxlen = strlen (fn1) * 2;
  if (maxlen < 31)
    maxlen = 31;
  dest = fn2 = xmalloc (maxlen + 1);
  src = fn1;

  if (*src == DIR_SEPARATOR)
    *dest++ = *src++;
  else
    {
      errno = 0;
      while (getcwd (dest, maxlen - (dest - fn2)) == NULL && errno == ERANGE)
	{
	  maxlen *= 2;
	  dest = fn2 = xrealloc (fn2, maxlen + 1);
	  errno = 0;
	}
      if (errno)
	{
	  free (fn1);
	  free (fn2);
	  return NULL;
	}
      dest = strchr (fn2, '\0');
      if (dest - fn2 >= maxlen)
	{
	  int ofs = dest - fn2;
	  maxlen *= 2;
	  fn2 = xrealloc (fn2, maxlen + 1);
	  dest = fn2 + ofs;
	}
      if (dest[-1] != DIR_SEPARATOR)
	*dest++ = DIR_SEPARATOR;
    }

  for (;;)
    {
      int c, f;

      c = *src++;

      f = 0;
      if (c == DIR_SEPARATOR || c == 0)
	{
	  /* remove `./', `../' from directory */
	  if (dest[-1] == '.' && dest[-2] == DIR_SEPARATOR)
	    dest--;
	  else if (dest[-1] == '.' && dest[-2] == '.' && dest[-3] == DIR_SEPARATOR)
	    {
	      dest -= 3;
	      if (dest == fn2)
		dest++;
	      while (dest[-1] != DIR_SEPARATOR)
		dest--;
	    }
	  else if (dest[-1] != DIR_SEPARATOR)	/* remove extra slashes */
	    f = 1;

	  if (c == 0)
	    {
	      if (dest[-1] == DIR_SEPARATOR && dest > fn2 + 1)
		dest--;
	      *dest = 0;
	      free (fn1);

	      return xrealloc (fn2, strlen (fn2) + 1);
	    }
	}
      else
	f = 1;

      if (f)
	{
	  if (dest - fn2 >= maxlen)
	    {
	      int ofs = dest - fn2;
	      maxlen *= 2;
	      fn2 = xrealloc (fn2, maxlen + 1);
	      dest = fn2 + ofs;
	    }
	  *dest++ = c;
	}
    }
}
#elif __WIN32__
char *
fn_normalize (const char *fn1)
{
  DWORD len;
  DWORD success;
  char *fn2;

  /* Don't change special filenames. */
  if (is_special_filename (filename))
    return xstrdup (filename);

  /* First find the required buffer length. */
  len = GetFullPathName (fn1, 0, NULL, NULL);
  if (!len)
    {
      fn2 = xstrdup (fn1);
      return fn2;
    }

  /* Then make a buffer that big. */
  fn2 = xmalloc (len);
  success = GetFullPathName (fn1, len, fn2, NULL);
  if (success >= len || success == 0)
    {
      free (fn2);
      fn2 = xstrdup (fn1);
      return fn2;
    }
  return fn2;
}
#elif __BORLANDC__
char *
fn_normalize (const char *fn1)
{
  char *fn2 = _fullpath (NULL, fn1, 0);
  if (fn2)
    {
      char *cp;
      for (cp = fn2; *cp; cp++)
	*cp = toupper ((unsigned char) (*cp));
      return fn2;
    }
  return xstrdup (fn1);
}
#elif __DJGPP__
char *
fn_normalize (const char *fn1)
{
  char *fn2 = xmalloc (1024);
  _fixpath (fn1, fn2);
  fn2 = xrealloc (fn2, strlen (fn2) + 1);
  return fn2;
}
#else /* not Lose32, Unix, or DJGPP */
char *
fn_normalize (const char *fn)
{
  return xstrdup (fn);
}
#endif /* not Lose32, Unix, or DJGPP */

/* Returns the directory part of FILENAME, as a malloc()'d
   string. */
char *
fn_dirname (const char *filename)
{
  const char *p;
  char *s;
  size_t len;

  len = strlen (filename);
  if (len == 1 && filename[0] == '/')
    p = filename + 1;
  else if (len && filename[len - 1] == DIR_SEPARATOR)
    p = mm_find_reverse (filename, len - 1, filename + len - 1, 1);
  else
    p = strrchr (filename, DIR_SEPARATOR);
  if (p == NULL)
    p = filename;

  s = xmalloc (p - filename + 1);
  memcpy (s, filename, p - filename);
  s[p - filename] = 0;

  return s;
}

/* Returns the basename part of FILENAME as a malloc()'d string. */
#if 0
char *
fn_basename (const char *filename)
{
  /* Not used, not implemented. */
  abort ();
}
#endif

/* Returns the current working directory, as a malloc()'d string.
   From libc.info. */
char *
fn_get_cwd (void)
{
  int size = 100;
  char *buffer = xmalloc (size);
     
  for (;;)
    {
      char *value = getcwd (buffer, size);
      if (value != 0)
	return buffer;

      size *= 2;
      free (buffer);
      buffer = xmalloc (size);
    }
}

/* Find out information about files. */

/* Returns nonzero iff NAME specifies an absolute filename. */
int
fn_absolute_p (const char *name)
{
#if unix
  if (name[0] == '/'
      || !strncmp (name, "./", 2)
      || !strncmp (name, "../", 3)
      || name[0] == '~')
    return 1;
#elif __MSDOS__
  if (name[0] == '\\'
      || !strncmp (name, ".\\", 2)
      || !strncmp (name, "..\\", 3)
      || (name[0] && name[1] == ':'))
    return 1;
#endif
  
  return 0;
}
  
/* Returns 1 if the filename specified is a virtual file that doesn't
   really exist on disk, 0 if it's a real filename. */
int
fn_special_p (const char *filename)
{
  if (!strcmp (filename, "-") || !strcmp (filename, "stdin")
      || !strcmp (filename, "stdout") || !strcmp (filename, "stderr")
#if unix
      || filename[0] == '|'
      || (*filename && filename[strlen (filename) - 1] == '|')
#endif
      )
    return 1;

  return 0;
}

/* Returns nonzero if file with name NAME exists. */
int
fn_exists_p (const char *name)
{
#if unix
  struct stat temp;

  return stat (name, &temp) == 0;
#else
  FILE *f = fopen (name, "r");
  if (!f)
    return 0;
  fclose (f);
  return 1;
#endif
}

#if unix
/* Stolen from libc.info but heavily modified, this is a wrapper
   around readlink() that allows for arbitrary filename length. */
char *
fn_readlink (const char *filename)
{
  int size = 128;

  for (;;)
    {
      char *buffer = xmalloc (size);
      int nchars  = readlink (filename, buffer, size);
      if (nchars == -1)
	{
	  free (buffer);
	  return NULL;
	}

      if (nchars < size - 1)
	{
	  buffer[nchars] = 0;
	  return buffer;
	}
      free (buffer);
      size *= 2;
    }
}
#else /* Not UNIX. */
char *
fn_readlink (const char *filename)
{
  return NULL;
}
#endif /* Not UNIX. */

/* Environment variables. */

/* Simulates $VER and $ARCH environment variables. */
const char *
fn_getenv (const char *s)
{
  if (!strcmp (s, "VER"))
    return fn_getenv_default ("STAT_VER", bare_version);
  else if (!strcmp (s, "ARCH"))
    return fn_getenv_default ("STAT_ARCH", host_system);
  else
    return getenv (s);
}

/* Returns getenv(KEY) if that's non-NULL; else returns DEF. */
const char *
fn_getenv_default (const char *key, const char *def)
{
  const char *value = getenv (key);
  return value ? value : def;
}

/* Basic file handling. */

/* Used for giving an error message on a set_safer security
   violation. */
static FILE *
safety_violation (const char *fn)
{
  msg (SE, _("Not opening pipe file `%s' because SAFER option set."), fn);
  errno = EPERM;
  return NULL;
}

/* As a general comment on the following routines, a `sensible value'
   for errno includes 0 if there is no associated system error.  The
   routines will only set errno to 0 if there is an error in a
   callback that sets errno to 0; they themselves won't. */

/* File open routine that understands `-' as stdin/stdout and `|cmd'
   as a pipe to command `cmd'.  Returns resultant FILE on success,
   NULL on failure.  If NULL is returned then errno is set to a
   sensible value.  */
FILE *
fn_open (const char *fn, const char *mode)
{
  assert (mode[0] == 'r' || mode[0] == 'w');

  if (mode[0] == 'r' && (!strcmp (fn, "stdin") || !strcmp (fn, "-"))) 
    return stdin;
  else if (mode[0] == 'w' && (!strcmp (fn, "stdout") || !strcmp (fn, "-")))
    return stdout;
  else if (mode[0] == 'w' && !strcmp (fn, "stderr"))
    return stderr;
  
#if unix
  if (fn[0] == '|')
    {
      if (set_safer)
	return safety_violation (fn);

      return popen (&fn[1], mode);
    }
  else if (*fn && fn[strlen (fn) - 1] == '|')
    {
      char *s;
      FILE *f;

      if (set_safer)
	return safety_violation (fn);
      
      s = local_alloc (strlen (fn));
      memcpy (s, fn, strlen (fn) - 1);
      s[strlen (fn) - 1] = 0;
      
      f = popen (s, mode);

      local_free (s);

      return f;
    }
  else
#endif
    {
      FILE *f = fopen (fn, mode);

      if (f && mode[0] == 'w')
	setvbuf (f, NULL, _IOLBF, 0);

      return f;
    }
}

/* Counterpart to fn_open that closes file F with name FN; returns 0
   on success, EOF on failure.  If EOF is returned, errno is set to a
   sensible value. */
int
fn_close (const char *fn, FILE *f)
{
  if (!strcmp (fn, "-"))
    return 0;
#if unix
  else if (fn[0] == '|' || (*fn && fn[strlen (fn) - 1] == '|'))
    {
      pclose (f);
      return 0;
    }
#endif
  else
    return fclose (f);
}

/* More extensive file handling. */

/* File open routine that extends fn_open().  Opens or reopens a
   file according to the contents of file_ext F.  Returns nonzero on
   success.  If 0 is returned, errno is set to a sensible value. */
int
fn_open_ext (struct file_ext *f)
{
  char *p;

  p = strstr (f->filename, "%d");
  if (p)
    {
      char *s = local_alloc (strlen (f->filename) + INT_DIGITS - 1);
      char *cp;

      memcpy (s, f->filename, p - f->filename);
      cp = spprintf (&s[p - f->filename], "%d", *f->sequence_no);
      strcpy (cp, &p[2]);

      if (f->file)
	{
	  int error = 0;

	  if (f->preclose)
	    if (f->preclose (f) == 0)
	      error = errno;

	  if (EOF == fn_close (f->filename, f->file) || error)
	    {
	      f->file = NULL;
	      local_free (s);

	      if (error)
		errno = error;

	      return 0;
	    }

	  f->file = NULL;
	}

      f->file = fn_open (s, f->mode);
      local_free (s);

      if (f->file && f->postopen)
	if (f->postopen (f) == 0)
	  {
	    int error = errno;
	    fn_close (f->filename, f->file);
	    errno = error;

	    return 0;
	  }

      return (f->file != NULL);
    }
  else if (f->file)
    return 1;
  else
    {
      f->file = fn_open (f->filename, f->mode);

      if (f->file && f->postopen)
	if (f->postopen (f) == 0)
	  {
	    int error = errno;
	    fn_close (f->filename, f->file);
	    errno = error;

	    return 0;
	  }

      return (f->file != NULL);
    }
}

/* Properly closes the file associated with file_ext F, if any.
   Return nonzero on success.  If zero is returned, errno is set to a
   sensible value. */
int
fn_close_ext (struct file_ext *f)
{
  if (f->file)
    {
      int error = 0;

      if (f->preclose)
	if (f->preclose (f) == 0)
	  error = errno;

      if (EOF == fn_close (f->filename, f->file) || error)
	{
	  f->file = NULL;

	  if (error)
	    errno = error;

	  return 0;
	}

      f->file = NULL;
    }
  return 1;
}
