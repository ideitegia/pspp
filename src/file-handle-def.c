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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include "file-handle.h"
#include "file-handle-def.h"
#include "error.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "alloc.h"
#include "filename.h"
#include "command.h"
#include "getl.h"
#include "error.h"
#include "magic.h"
#include "var.h"
#include "file-handle-def.h"

#include "gettext.h"

#define _(msgid) gettext (msgid)

/* (headers) */

/* File handle. */
struct file_handle 
  {
    struct file_handle *next;   /* Next in global list. */
    char *name;                 /* File handle identifier. */
    char *filename;		/* Filename as provided by user. */
    struct file_identity *identity; /* For checking file identity. */
    struct file_locator where;	/* Used for reporting error messages. */
    enum fh_mode mode;  	/* File mode. */
    size_t record_width;        /* Length of fixed-format records. */
    size_t tab_width;           /* Tab width, 0=do not expand tabs. */

    int open_cnt;               /* 0=not open, otherwise # of openers. */
    const char *type;           /* If open, type of file. */
    char open_mode[3];          /* "[rw][se]". */
    void *aux;                  /* Aux data pointer for owner if any. */
  };

static struct file_handle *file_handles;

/* File handle initialization routine. */
void 
fh_init (void)
{
  /* Currently nothing to do. */
}


/* Destroy file handle.
   Normally needed only if a file_handle needs to be re-assigned.
   Otherwise, just let fh_done clean destroy the handle.
 */
void 
fh_free(struct file_handle *fh)
{
  if ( !fh->name ) 
    return ;

  free (fh->name);
  fh->name = 0;
  free (fh->filename);
  fn_free_identity (fh->identity);
  free (fh);
}


/* Frees all the file handles. */
void 
fh_done (void)
{
  struct file_handle *fh, *next;
  
  for (fh = file_handles; fh != NULL; fh = next)
    {
      next = fh->next;
      fh_free(fh);
    }
  file_handles = NULL;
}

/* Returns the handle named HANDLE_NAME, or a null pointer if
   there is none. */
struct file_handle *
fh_from_name (const char *handle_name) 
{
  struct file_handle *iter;

  for (iter = file_handles; iter != NULL; iter = iter->next)
    if (!strcasecmp (handle_name, iter->name))
      return iter;
  return NULL;
}

/* Returns the handle for the file named FILENAME,
   or a null pointer if none exists.
   Different names for the same file (e.g. "x" and "./x") are
   considered equivalent. */
struct file_handle *
fh_from_filename (const char *filename)
{
  struct file_identity *identity;
  struct file_handle *iter;
      
  /* First check for a file with the same identity. */
  identity = fn_get_identity (filename);
  if (identity != NULL) 
    {
      for (iter = file_handles; iter != NULL; iter = iter->next)
        if (iter->identity != NULL
            && !fn_compare_file_identities (identity, iter->identity))
          {
            fn_free_identity (identity);
            return iter; 
          }
      fn_free_identity (identity);
    }

  /* Then check for a file with the same name. */
  for (iter = file_handles; iter != NULL; iter = iter->next)
    if (!strcmp (filename, iter->filename))
      return iter; 

  return NULL;
}

/* Creates and returns a new file handle with the given values
   and defaults for other values.  Adds the created file handle
   to the global list. */
struct file_handle *
fh_create (const char *handle_name, const char *filename,
           const struct fh_properties *properties)
{
  struct file_handle *handle;

  assert(filename);
  assert(handle_name);

  /* Create and initialize file handle. */
  handle = xmalloc (sizeof *handle);
  handle->next = file_handles;
  handle->name = xstrdup (handle_name);
  handle->filename = xstrdup (filename);
  handle->identity = fn_get_identity (filename);
  handle->where.filename = handle->filename;
  handle->where.line_number = 0;
  handle->mode = properties->mode;
  handle->record_width = properties->record_width;
  handle->tab_width = properties->tab_width;
  handle->open_cnt = 0;
  handle->type = NULL;
  handle->aux = NULL;
  file_handles = handle;

  return handle;
}

/* Returns a set of default properties for a file handle. */
const struct fh_properties *
fh_default_properties (void)
{
  static const struct fh_properties default_properties = {MODE_TEXT, 1024, 4};
  return &default_properties;
}

/* Returns an English description of MODE,
   which is in the format of the MODE argument to fh_open(). */
static const char *
mode_name (const char *mode) 
{
  assert (mode != NULL);
  assert (mode[0] == 'r' || mode[0] == 'w');

  return mode[0] == 'r' ? "reading" : "writing";
}

/* Tries to open handle H with the given TYPE and MODE.

   TYPE is the sort of file, e.g. "system file".  Only one given
   type of access is allowed on a given file handle at once.
   If successful, a reference to TYPE is retained, so it should
   probably be a string literal.

   MODE combines the read or write mode with the sharing mode.
   The first character is 'r' for read, 'w' for write.  The
   second character is 's' to permit sharing, 'e' to require
   exclusive access.

   Returns the address of a void * that the caller can use for
   data specific to the file handle if successful, or a null
   pointer on failure.  For exclusive access modes the void *
   will always be a null pointer at return.  In shared access
   modes the void * will necessarily be null only if no other
   sharers are active. */
void **
fh_open (struct file_handle *h, const char *type, const char *mode) 
{
  assert (h != NULL);
  assert (type != NULL);
  assert (mode != NULL);
  assert (mode[0] == 'r' || mode[0] == 'w');
  assert (mode[1] == 's' || mode[1] == 'e');
  assert (mode[2] == '\0');

  if (h->open_cnt != 0) 
    {
      if (strcmp (h->type, type)) 
        {
          msg (SE, _("Can't open %s as a %s because it is "
                     "already open as a %s"),
               fh_get_name (h), type, h->type);
          return NULL; 
        }
      else if (strcmp (h->open_mode, mode)) 
        {
          msg (SE, _("Can't open %s as a %s for %s because it is "
                     "already open for %s"),
               fh_get_name (h), type, mode_name (mode),
               mode_name (h->open_mode));
          return NULL;
        }
      else if (h->open_mode[1] == 'e')
        {
          msg (SE, _("Can't re-open %s as a %s for %s"),
               fh_get_name (h), type, mode_name (mode));
          return NULL;
        }
    }
  else 
    {
      h->type = type;
      strcpy (h->open_mode, mode);
      assert (h->aux == NULL);
    }
  h->open_cnt++;

  return &h->aux;
}

/* Closes file handle H, which must have been open for the
   specified TYPE and MODE of access provided to fh_open().
   Returns zero if the file is now closed, nonzero if it is still
   open due to another reference. */
int
fh_close (struct file_handle *h, const char *type, const char *mode)
{
  assert (h != NULL);
  assert (h->open_cnt > 0);
  assert (type != NULL);
  assert (!strcmp (type, h->type));
  assert (mode != NULL);
  assert (!strcmp (mode, h->open_mode));

  h->open_cnt--;
  if (h->open_cnt == 0) 
    {
      h->type = NULL;
      h->aux = NULL;
    }
  return h->open_cnt;
}

/* Returns the identifier of file HANDLE.  If HANDLE was created
   by referring to a filename instead of a handle name, returns
   the filename, enclosed in double quotes.  Return value is
   owned by the file handle. 

   Useful for printing error messages about use of file handles.  */
const char *
fh_get_name (const struct file_handle *handle)
{
  assert (handle != NULL);
  return handle->name;
}

/* Returns the name of the file associated with HANDLE. */
const char *
fh_get_filename (const struct file_handle *handle) 
{
  assert (handle != NULL);
  return handle->filename;
}

/* Returns the mode of HANDLE. */
enum fh_mode
fh_get_mode (const struct file_handle *handle) 
{
  assert (handle != NULL);
  return handle->mode;
}

/* Returns the width of a logical record on HANDLE. */
size_t
fh_get_record_width (const struct file_handle *handle)
{
  assert (handle != NULL);
  return handle->record_width;
}

/* Returns the number of characters per tab stop for HANDLE, or
   zero if tabs are not to be expanded.  Applicable only to
   MODE_TEXT files. */
size_t
fh_get_tab_width (const struct file_handle *handle) 
{
  assert (handle != NULL);
  return handle->tab_width;
}
