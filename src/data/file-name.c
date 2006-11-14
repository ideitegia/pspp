/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>

#include "file-name.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "intprops.h"
#include "minmax.h"
#include "settings.h"
#include "xreadlink.h"

#include <libpspp/alloc.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/str.h>
#include <libpspp/verbose-msg.h>
#include <libpspp/version.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* PORTME: Everything in this file is system dependent. */

#ifdef unix
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include "stat-macros.h"
#endif

#ifdef __WIN32__
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

/* Functions for performing operations on file names. */


/* Substitutes $variables in SRC, putting the result in DST,
   properly handling the case where SRC is a substring of DST.
   Variables are as defined by GETENV. Supports $var and ${var}
   syntaxes; $$ substitutes as $. */
void 
fn_interp_vars (struct substring src, const char *(*getenv) (const char *),
                struct string *dst_)
{
  struct string dst = DS_EMPTY_INITIALIZER;
  int c;

  while ((c = ss_get_char (&src)) != EOF)
    if (c != '$') 
      ds_put_char (&dst, c);
    else
      {
        if (ss_match_char (&src, '$') || ss_is_empty (src))
          ds_put_char (&dst, '$');
        else
          {
            struct substring var_name;
            size_t start;
            const char *value;

            if (ss_match_char (&src, '('))
              ss_get_until (&src, ')', &var_name);
            else if (ss_match_char (&src, '{'))
              ss_get_until (&src, '}', &var_name);
            else
              ss_get_chars (&src, MIN (1, ss_span (src, ss_cstr (CC_ALNUM))),
                            &var_name);

            start = ds_length (&dst);
            ds_put_substring (&dst, var_name);
            value = getenv (ds_cstr (&dst) + start);
            ds_truncate (&dst, start);

            ds_put_cstr (&dst, value);
          } 
      }

  ds_swap (&dst, dst_);
  ds_destroy (&dst);
}

#ifdef unix
/* Expands csh tilde notation from the path INPUT into a malloc()'d
   returned string. */
char *
fn_tilde_expand (const char *input)
{
  struct string output = DS_EMPTY_INITIALIZER;
  if (input[0] == '~')
    {
      const char *home = NULL;
      const char *remainder = NULL;
      if (input[1] == '/' || input[1] == '\0')
        {
          home = fn_getenv ("HOME");
          remainder = input + 1; 
        }
      else
        {
          struct string user_name = DS_EMPTY_INITIALIZER;
          struct passwd *pwd;

          ds_assign_substring (&user_name,
                               ss_buffer (input + 1,
                                          strcspn (input + 1, "/")));
          pwd = getpwnam (ds_cstr (&user_name));
          if (pwd != NULL && pwd->pw_dir[0] != '\0')
            {
              home = xstrdup (pwd->pw_dir);
              remainder = input + 1 + ds_length (&user_name);
            }
          ds_destroy (&user_name);
        }

      if (home != NULL) 
        {
          ds_put_cstr (&output, home);
          if (*remainder != '\0')
            ds_put_cstr (&output, remainder);
        }
    }
  if (ds_is_empty (&output))
    ds_put_cstr (&output, input);
  return ds_cstr (&output);
}
#else /* !unix */
char *
fn_tilde_expand (const char *input)
{
  return xstrdup (input);
}
#endif /* !unix */

/* Searches for a configuration file with name NAME in the path
   given by PATH, which is tilde- and environment-interpolated.
   Directories in PATH are delimited by ':'.  Returns the
   malloc'd full name of the first file found, or NULL if none is
   found.

   If PREFIX is non-NULL, then it is prefixed to each file name;
   i.e., it looks like PREFIX/PATH_COMPONENT/NAME.  This is not
   done with absolute directories in the path. */
char *
fn_search_path (const char *base_name, const char *path_, const char *prefix)
{
  struct string path;
  struct substring dir_;
  struct string file = DS_EMPTY_INITIALIZER;
  size_t save_idx = 0;

  if (fn_is_absolute (base_name))
    return fn_tilde_expand (base_name);

  /* Interpolate environment variables. */
  ds_init_cstr (&path, path_);
  fn_interp_vars (ds_ss (&path), fn_getenv, &path);

  verbose_msg (2, _("searching for \"%s\" in path \"%s\""),
               base_name, ds_cstr (&path));
  while (ds_separate (&path, ss_cstr (":"), &save_idx, &dir_))
    {
      struct string dir;

      /* Do tilde expansion. */
      ds_init_substring (&dir, dir_);
      if (ds_first (&dir) == '~') 
        {
          char *tmp_str = fn_tilde_expand (ds_cstr (&dir));
          ds_assign_cstr (&dir, tmp_str);
          free (tmp_str); 
        }

      /* Construct file name. */
      ds_clear (&file);
      if (prefix != NULL && !fn_is_absolute (ds_cstr (&dir)))
	{
	  ds_put_cstr (&file, prefix);
	  ds_put_char (&file, '/');
	}
      ds_put_cstr (&file, ds_cstr (&dir));
      if (!ds_is_empty (&file) && ds_last (&file) != '/')
	ds_put_char (&file, '/');
      ds_put_cstr (&file, base_name);
      ds_destroy (&dir);

      /* Check whether file exists. */
      if (fn_exists (ds_cstr (&file)))
	{
	  verbose_msg (2, _("...found \"%s\""), ds_cstr (&file));
          ds_destroy (&path);
	  return ds_cstr (&file);
	}
    }

  /* Failure. */
  verbose_msg (2, _("...not found"));
  ds_destroy (&path);
  ds_destroy (&file);
  return NULL;
}

/* fn_normalize(): This very OS-dependent routine canonicalizes
   file name FN1.  The file name should not need to be the name of an
   existing file.  Returns a malloc()'d copy of the canonical name.
   This function must always succeed; if it needs to bail out then it
   should return xstrdup(FN1).  */
#ifdef unix
char *
fn_normalize (const char *file_name)
{
  const char *src;
  char *fn1, *fn2, *dest;
  int maxlen;

  if (fn_is_special (file_name))
    return xstrdup (file_name);
  
  fn1 = fn_tilde_expand (file_name);

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

  if (*src == '/')
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
      if (dest[-1] != '/')
	*dest++ = '/';
    }

  for (;;)
    {
      int c, f;

      c = *src++;

      f = 0;
      if (c == '/' || c == 0)
	{
	  /* remove `./', `../' from directory */
	  if (dest[-1] == '.' && dest[-2] == '/')
	    dest--;
	  else if (dest[-1] == '.' && dest[-2] == '.' && dest[-3] == '/')
	    {
	      dest -= 3;
	      if (dest == fn2)
		dest++;
	      while (dest[-1] != '/')
		dest--;
	    }
	  else if (dest[-1] != '/')	/* remove extra slashes */
	    f = 1;

	  if (c == 0)
	    {
	      if (dest[-1] == '/' && dest > fn2 + 1)
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
#elif defined (__WIN32__)
char *
fn_normalize (const char *fn1)
{
  DWORD len;
  DWORD success;
  char *fn2;

  /* Don't change special file names. */
  if (is_special_file_name (file_name))
    return xstrdup (file_name);

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

/* Returns the directory part of FILE_NAME, as a malloc()'d
   string. */
char *
fn_dir_name (const char *file_name)
{
  const char *p;
  char *s;
  size_t len;

  len = strlen (file_name);
  if (len == 1 && file_name[0] == '/')
    p = file_name + 1;
  else if (len && file_name[len - 1] == '/')
    p = buf_find_reverse (file_name, len - 1, file_name + len - 1, 1);
  else
    p = strrchr (file_name, '/');
  if (p == NULL)
    p = file_name;

  s = xmalloc (p - file_name + 1);
  memcpy (s, file_name, p - file_name);
  s[p - file_name] = 0;

  return s;
}

/* Returns the extension part of FILE_NAME as a malloc()'d string.
   If FILE_NAME does not have an extension, returns an empty
   string. */
char *
fn_extension (const char *file_name) 
{
  const char *extension = strrchr (file_name, '.');
  if (extension == NULL)
    extension = "";
  return xstrdup (extension);
}

#if unix
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
#else
char *
fn_get_cwd (void)
{
  int size = 2;
  char *buffer = xmalloc (size);
  if ( buffer) 
    {
      buffer[0]='.';
      buffer[1]='\0';
    }

  return buffer;
     
}
#endif

/* Find out information about files. */

/* Returns true iff NAME specifies an absolute file name. */
bool
fn_is_absolute (const char *name)
{
#ifdef unix
  if (name[0] == '/'
      || !strncmp (name, "./", 2)
      || !strncmp (name, "../", 3)
      || name[0] == '~')
    return true;
#elif defined (__MSDOS__)
  if (name[0] == '\\'
      || !strncmp (name, ".\\", 2)
      || !strncmp (name, "..\\", 3)
      || (name[0] && name[1] == ':'))
    return true;
#endif
  
  return false;
}
  
/* Returns true if FILE_NAME is a virtual file that doesn't
   really exist on disk, false if it's a real file name. */
bool
fn_is_special (const char *file_name)
{
  if (!strcmp (file_name, "-") || !strcmp (file_name, "stdin")
      || !strcmp (file_name, "stdout") || !strcmp (file_name, "stderr")
#ifdef unix
      || file_name[0] == '|'
      || (*file_name && file_name[strlen (file_name) - 1] == '|')
#endif
      )
    return true;

  return false;
}

/* Returns true if file with name NAME exists. */
bool
fn_exists (const char *name)
{
#ifdef unix
  struct stat temp;

  return stat (name, &temp) == 0;
#else
  FILE *f = fopen (name, "r");
  if (!f)
    return false;
  fclose (f);
  return true;
#endif
}

/* Returns the symbolic link value for FILE_NAME as a dynamically
   allocated buffer, or a null pointer on failure. */
char *
fn_readlink (const char *file_name)
{
  return xreadlink (file_name, 32);
}

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
  
#ifdef unix
  if (fn[0] == '|')
    {
      if (get_safer_mode ())
	return safety_violation (fn);

      return popen (&fn[1], mode);
    }
  else if (*fn && fn[strlen (fn) - 1] == '|')
    {
      char *s;
      FILE *f;

      if (get_safer_mode ())
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
#ifdef unix
  else if (fn[0] == '|' || (*fn && fn[strlen (fn) - 1] == '|'))
    {
      pclose (f);
      return 0;
    }
#endif
  else
    return fclose (f);
}

#ifdef unix
/* A file's identity. */
struct file_identity 
{
  dev_t device;               /* Device number. */
  ino_t inode;                /* Inode number. */
};

/* Returns a pointer to a dynamically allocated structure whose
   value can be used to tell whether two files are actually the
   same file.  Returns a null pointer if no information about the
   file is available, perhaps because it does not exist.  The
   caller is responsible for freeing the structure with
   fn_free_identity() when finished. */  
struct file_identity *
fn_get_identity (const char *file_name) 
{
  struct stat s;

  if (stat (file_name, &s) == 0) 
    {
      struct file_identity *identity = xmalloc (sizeof *identity);
      identity->device = s.st_dev;
      identity->inode = s.st_ino;
      return identity;
    }
  else
    return NULL;
}

/* Frees IDENTITY obtained from fn_get_identity(). */
void
fn_free_identity (struct file_identity *identity) 
{
  free (identity);
}

/* Compares A and B, returning a strcmp()-type result. */
int
fn_compare_file_identities (const struct file_identity *a,
                            const struct file_identity *b) 
{
  assert (a != NULL);
  assert (b != NULL);
  if (a->device != b->device)
    return a->device < b->device ? -1 : 1;
  else
    return a->inode < b->inode ? -1 : a->inode > b->inode;
}
#else /* not unix */
/* A file's identity. */
struct file_identity 
{
  char *normalized_file_name;  /* File's normalized name. */
};

/* Returns a pointer to a dynamically allocated structure whose
   value can be used to tell whether two files are actually the
   same file.  Returns a null pointer if no information about the
   file is available, perhaps because it does not exist.  The
   caller is responsible for freeing the structure with
   fn_free_identity() when finished. */  
struct file_identity *
fn_get_identity (const char *file_name) 
{
  struct file_identity *identity = xmalloc (sizeof *identity);
  identity->normalized_file_name = fn_normalize (file_name);
  return identity;
}

/* Frees IDENTITY obtained from fn_get_identity(). */
void
fn_free_identity (struct file_identity *identity) 
{
  if (identity != NULL) 
    {
      free (identity->normalized_file_name);
      free (identity);
    }
}

/* Compares A and B, returning a strcmp()-type result. */
int
fn_compare_file_identities (const struct file_identity *a,
                            const struct file_identity *b) 
{
  return strcmp (a->normalized_file_name, b->normalized_file_name);
}
#endif /* not unix */
