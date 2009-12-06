/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include <data/file-name.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "intprops.h"
#include "minmax.h"
#include "dirname.h"
#include "xmalloca.h"

#include <data/settings.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/str.h>
#include <libpspp/verbose-msg.h>
#include <libpspp/version.h>

#include "xalloc.h"

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


/* Copies from SRC to DST, calling INSERT_VARIABLE to handle each
   instance of $var or ${var} in SRC.  $$ is replaced by $. */
void
fn_interp_vars (struct substring src,
                void (*insert_variable) (const char *var,
                                         struct string *dst, void *aux),
                void *aux, struct string *dst_)
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
            char *var;

            if (ss_match_char (&src, '('))
              ss_get_until (&src, ')', &var_name);
            else if (ss_match_char (&src, '{'))
              ss_get_until (&src, '}', &var_name);
            else
              ss_get_chars (&src, MAX (1, ss_span (src, ss_cstr (CC_ALNUM))),
                            &var_name);

            var = ss_xstrdup (var_name);
            insert_variable (var, &dst, aux);
            free (var);
          }
      }

  ds_swap (&dst, dst_);
  ds_destroy (&dst);
}

static void
insert_env_var (const char *var, struct string *dst, void *aux UNUSED)
{
  const char *value = fn_getenv (var);
  if (value != NULL)
    ds_put_cstr (dst, value);
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
  fn_interp_vars (ds_ss (&path), insert_env_var, NULL, &path);

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
      ds_relocate (&file);

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
  assert (mode[0] == 'r' || mode[0] == 'w' || mode[0] == 'a');

  if (mode[0] == 'r' && (!strcmp (fn, "stdin") || !strcmp (fn, "-")))
    return stdin;
  else if (mode[0] == 'w' && (!strcmp (fn, "stdout") || !strcmp (fn, "-")))
    return stdout;
  else if (mode[0] == 'w' && !strcmp (fn, "stderr"))
    return stderr;

#if HAVE_POPEN
  if (fn[0] == '|')
    {
      if (settings_get_safer_mode ())
	return safety_violation (fn);

      return popen (&fn[1], mode[0] == 'r' ? "r" : "w");
    }
  else if (*fn && fn[strlen (fn) - 1] == '|')
    {
      char *s;
      FILE *f;

      if (settings_get_safer_mode ())
	return safety_violation (fn);

      s = xmalloca (strlen (fn));
      memcpy (s, fn, strlen (fn) - 1);
      s[strlen (fn) - 1] = 0;

      f = popen (s, mode[0] == 'r' ? "r" : "w");

      freea (s);

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
  if (fileno (f) == STDIN_FILENO
      || fileno (f) == STDOUT_FILENO
      || fileno (f) == STDERR_FILENO)
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

/* Creates a new file named FN with the given PERMISSIONS bits,
   and returns a stream for it or a null pointer on failure.
   MODE should be "w" or "wb". */
FILE *
create_stream (const char *fn, const char *mode, mode_t permissions)
{
  int fd;
  FILE *stream;

  fd = open (fn, O_WRONLY | O_CREAT | O_TRUNC, permissions);
  if (fd < 0)
    return NULL;

  stream = fdopen (fd, mode);
  if (stream == NULL)
    {
      int save_errno = errno;
      close (fd);
      errno = save_errno;
    }

  return stream;
}

/* A file's identity:

   - For a file that exists, this is its device and inode.

   - For a file that does not exist, but which has a directory
     name that exists, this is the device and inode of the
     directory, plus the file's base name.

   - For a file that does not exist and has a nonexistent
     directory, this is the file name.

   Windows doesn't have inode numbers, so we just use the name
   there. */
struct file_identity
{
  dev_t device;               /* Device number. */
  ino_t inode;                /* Inode number. */
  char *name;                 /* File name, where needed, otherwise NULL. */
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

#if !(defined _WIN32 || defined __WIN32__)
  struct stat s;
  if (lstat (file_name, &s) == 0)
    {
      identity->device = s.st_dev;
      identity->inode = s.st_ino;
      identity->name = NULL;
    }
  else
    {
      char *dir = dir_name (file_name);
      if (last_component (file_name) != NULL && stat (dir, &s) == 0)
        {
          identity->device = s.st_dev;
          identity->inode = s.st_ino;
          identity->name = base_name (file_name);
        }
      else
        {
          identity->device = 0;
          identity->inode = 0;
          identity->name = xstrdup (file_name);
        }
      free (dir);
    }
#else /* Windows */
  char cname[PATH_MAX];
  int ok = GetFullPathName (file_name, sizeof cname, cname, NULL);
  identity->device = 0;
  identity->inode = 0;
  identity->name = xstrdup (ok ? cname : file_name);
  str_lowercase (identity->name);
#endif /* Windows */

  return identity;
}

/* Frees IDENTITY obtained from fn_get_identity(). */
void
fn_free_identity (struct file_identity *identity)
{
  if (identity != NULL)
    {
      free (identity->name);
      free (identity);
    }
}

/* Compares A and B, returning a strcmp()-type result. */
int
fn_compare_file_identities (const struct file_identity *a,
                            const struct file_identity *b)
{
  if (a->device != b->device)
    return a->device < b->device ? -1 : 1;
  else if (a->inode != b->inode)
    return a->inode < b->inode ? -1 : 1;
  else if (a->name != NULL)
    return b->name != NULL ? strcmp (a->name, b->name) : 1;
  else
    return b->name != NULL ? -1 : 0;
}

/* Returns a hash value for IDENTITY. */
unsigned int
fn_hash_identity (const struct file_identity *identity)
{
  unsigned int hash = hash_int (identity->device, identity->inode);
  if (identity->name != NULL)
    hash = hash_string (identity->name, hash);
  return hash;
}

#ifdef WIN32

/* Apparently windoze users like to see output dumped into their home directory,
   not the current directory (!) */
const char *
default_output_path (void)
{
  static char *path = NULL;

  if ( path == NULL)
    {
      /* Windows NT defines HOMEDRIVE and HOMEPATH.  But give preference
	 to HOME, because the user can change HOME.  */

      const char *home_dir = getenv ("HOME");
      int i;

      if (home_dir == NULL)
	{
	  const char *home_drive = getenv ("HOMEDRIVE");
	  const char *home_path = getenv ("HOMEPATH");

	  if (home_drive != NULL && home_path != NULL)
	    home_dir = xasprintf ("%s%s",
				  home_drive, home_path);
	}

      if (home_dir == NULL)
	home_dir = "c:/users/default"; /* poor default */

      /* Copy home_dir into path.  Add a slash at the end but
         only if there isn't already one there, because Windows
         treats // specially. */
      if (home_dir[0] == '\0'
          || strchr ("/\\", home_dir[strlen (home_dir) - 1]) == NULL)
        path = xasprintf ("%s%c", home_dir, '/');
      else
        path = xstrdup (home_dir);

      for(i = 0; i < strlen (path); i++)
	if (path[i] == '\\') path[i] = '/';
    }

  return path;
}

#else

/* ... whereas the rest of the world just likes it to be
   put "here" for easy access. */
const char *
default_output_path (void)
{
  static char current_dir[]  = "";

  return current_dir;
}

#endif

