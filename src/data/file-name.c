/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006, 2007 Free Software Foundation, Inc.

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

#include <data/file-name.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "intprops.h"
#include "minmax.h"
#include "dirname.h"

#include <libpspp/alloc.h>
#include <libpspp/message.h>
#include <data/settings.h>
#include <libpspp/str.h>
#include <libpspp/verbose-msg.h>
#include <libpspp/version.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include <unistd.h>
#include <sys/stat.h>

#if defined _WIN32 || defined __WIN32__
#define WIN32_LEAN_AND_MEAN  /* avoid including junk */
#include <windows.h>
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

/* Searches for a configuration file with name NAME in the path
   given by PATH, which is environment-interpolated.
   Directories in PATH are delimited by ':'.  Returns the
   malloc'd full name of the first file found, or NULL if none is
   found. */
char *
fn_search_path (const char *base_name, const char *path_)
{
  struct string path;
  struct substring dir;
  struct string file = DS_EMPTY_INITIALIZER;
  size_t save_idx = 0;

  if (fn_is_absolute (base_name))
    return xstrdup (base_name);

  /* Interpolate environment variables. */
  ds_init_cstr (&path, path_);
  fn_interp_vars (ds_ss (&path), fn_getenv, &path);

  verbose_msg (2, _("searching for \"%s\" in path \"%s\""),
               base_name, ds_cstr (&path));
  while (ds_separate (&path, ss_cstr (":"), &save_idx, &dir))
    {
      /* Construct file name. */
      ds_clear (&file);
      ds_put_substring (&file, dir);
      if (!ds_is_empty (&file) && !ISSLASH (ds_last (&file)))
	ds_put_char (&file, '/');
      ds_put_cstr (&file, base_name);

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

/* Returns the directory part of FILE_NAME, as a malloc()'d
   string. */
char *
fn_dir_name (const char *file_name)
{
  return dir_name (file_name);
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

/* Find out information about files. */

/* Returns true iff NAME specifies an absolute file name. */
bool
fn_is_absolute (const char *name)
{
  return IS_ABSOLUTE_FILE_NAME (name);
}

/* Returns true if FILE_NAME is a virtual file that doesn't
   really exist on disk, false if it's a real file name. */
bool
fn_is_special (const char *file_name)
{
  if (!strcmp (file_name, "-") || !strcmp (file_name, "stdin")
      || !strcmp (file_name, "stdout") || !strcmp (file_name, "stderr")
#ifdef HAVE_POPEN
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
  struct stat temp;
  return stat (name, &temp) == 0;
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

#if HAVE_POPEN
/* Used for giving an error message on a set_safer security
   violation. */
static FILE *
safety_violation (const char *fn)
{
  msg (SE, _("Not opening pipe file `%s' because SAFER option set."), fn);
  errno = EPERM;
  return NULL;
}
#endif

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

#if HAVE_POPEN
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
#if HAVE_POPEN
  else if (fn[0] == '|' || (*fn && fn[strlen (fn) - 1] == '|'))
    {
      pclose (f);
      return 0;
    }
#endif
  else
    return fclose (f);
}

#if !(defined _WIN32 || defined __WIN32__)
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
#else /* Windows */
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
  char cname[PATH_MAX];

  if (GetFullPathName (file_name, sizeof cname, cname, NULL))
    identity->normalized_file_name = xstrdup (cname);
  else
    identity->normalized_file_name = xstrdup (file_name);

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
  return strcasecmp (a->normalized_file_name, b->normalized_file_name);
}
#endif /* Windows */
