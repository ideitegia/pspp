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
#include "file-handle-def.h"
#include "message.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "alloc.h"
#include "filename.h"
#include "message.h"
#include "magic.h"
#include "variable.h"
#include "scratch-handle.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* (headers) */

/* File handle. */
struct file_handle 
  {
    struct file_handle *next;   /* Next in global list. */
    int open_cnt;               /* 0=not open, otherwise # of openers. */
    bool deleted;               /* Destroy handle when open_cnt goes to 0? */

    char *name;                 /* File handle identifier. */
    const char *type;           /* If open, type of file. */
    char open_mode[3];          /* "[rw][se]". */
    void *aux;                  /* Aux data pointer for owner if any. */
    enum fh_referent referent;  /* What the file handle refers to. */

    /* FH_REF_FILE only. */
    char *filename;		/* Filename as provided by user. */
    struct file_identity *identity; /* For checking file identity. */
    enum fh_mode mode;  	/* File mode. */

    /* FH_REF_FILE and FH_REF_INLINE only. */
    size_t record_width;        /* Length of fixed-format records. */
    size_t tab_width;           /* Tab width, 0=do not expand tabs. */

    /* FH_REF_SCRATCH only. */
    struct scratch_handle *sh;  /* Scratch file data. */
  };

/* List of all handles. */
static struct file_handle *file_handles;

/* Default file handle for DATA LIST, REREAD, REPEATING DATA
   commands. */
static struct file_handle *default_handle;

/* The "file" that reads from BEGIN DATA...END DATA. */
static struct file_handle *inline_file;

static struct file_handle *create_handle (const char *name, enum fh_referent);

/* File handle initialization routine. */
void 
fh_init (void)
{
  inline_file = create_handle ("INLINE", FH_REF_INLINE);
  inline_file->record_width = 80;
  inline_file->tab_width = 8;
}

/* Free HANDLE and remove it from the global list. */
static void
free_handle (struct file_handle *handle) 
{
  /* Remove handle from global list. */
  if (file_handles == handle)
    file_handles = handle->next;
  else 
    {
      struct file_handle *iter = file_handles;
      while (iter->next != handle)
        iter = iter->next;
      iter->next = handle->next;
    }

  /* Free data. */
  free (handle->name);
  free (handle->filename);
  fn_free_identity (handle->identity);
  scratch_handle_destroy (handle->sh);
  free (handle);
}

/* Frees all the file handles. */
void 
fh_done (void)
{
  while (file_handles != NULL) 
    free_handle (file_handles);
}

/* Returns the handle named HANDLE_NAME, or a null pointer if
   there is none. */
struct file_handle *
fh_from_name (const char *handle_name) 
{
  struct file_handle *iter;

  for (iter = file_handles; iter != NULL; iter = iter->next)
    if (!iter->deleted && !strcasecmp (handle_name, iter->name))
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
        if (!iter->deleted
            && iter->referent == FH_REF_FILE
            && iter->identity != NULL
            && !fn_compare_file_identities (identity, iter->identity))
          {
            fn_free_identity (identity);
            return iter; 
          }
      fn_free_identity (identity);
    }

  /* Then check for a file with the same name. */
  for (iter = file_handles; iter != NULL; iter = iter->next)
    if (!iter->deleted
        && iter->referent == FH_REF_FILE && !strcmp (filename, iter->filename))
      return iter; 

  return NULL;
}

/* Creates a new handle with name HANDLE_NAME that refers to
   REFERENT.  Links the new handle into the global list.  Returns
   the new handle.

   The new handle is not fully initialized.  The caller is
   responsible for completing its initialization. */
static struct file_handle *
create_handle (const char *handle_name, enum fh_referent referent) 
{
  struct file_handle *handle = xzalloc (sizeof *handle);
  handle->next = file_handles;
  handle->open_cnt = 0;
  handle->deleted = false;
  handle->name = xstrdup (handle_name);
  handle->type = NULL;
  handle->aux = NULL;
  handle->referent = referent;
  file_handles = handle;
  return handle;
}

/* Returns the unique handle of referent type FH_REF_INLINE,
   which refers to the "inline file" that represents character
   data in the command file between BEGIN DATA and END DATA. */
struct file_handle *
fh_inline_file (void) 
{
  return inline_file;
}

/* Creates a new file handle named HANDLE_NAME, which must not be
   the name of an existing file handle.  The new handle is
   associated with file FILENAME and the given PROPERTIES. */
struct file_handle *
fh_create_file (const char *handle_name, const char *filename,
                const struct fh_properties *properties)
{
  struct file_handle *handle;
  assert (fh_from_name (handle_name) == NULL);
  handle = create_handle (handle_name, FH_REF_FILE);
  handle->filename = xstrdup (filename);
  handle->identity = fn_get_identity (filename);
  handle->mode = properties->mode;
  handle->record_width = properties->record_width;
  handle->tab_width = properties->tab_width;
  return handle;
}

/* Creates a new file handle named HANDLE_NAME, which must not be
   the name of an existing file handle.  The new handle is
   associated with a scratch file (initially empty). */
struct file_handle *
fh_create_scratch (const char *handle_name) 
{
  struct file_handle *handle = create_handle (handle_name, FH_REF_SCRATCH);
  handle->sh = NULL;
  return handle;
}

/* Returns a set of default properties for a file handle. */
const struct fh_properties *
fh_default_properties (void)
{
  static const struct fh_properties default_properties
    = {FH_MODE_TEXT, 1024, 4};
  return &default_properties;
}

/* Deletes FH from the global list of file handles.  Afterward,
   attempts to search for it will fail.  Unless the file handle
   is currently open, it will be destroyed; otherwise, it will be
   destroyed later when it is closed.
   Normally needed only if a file_handle needs to be re-assigned.
   Otherwise, just let fh_done() destroy the handle. */
void 
fh_free (struct file_handle *handle)
{
  if (handle == fh_inline_file () || handle == NULL || handle->deleted)
    return;
  handle->deleted = true;

  if (handle == default_handle)
    default_handle = fh_inline_file ();

  if (handle->open_cnt == 0)
    free_handle (handle);
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

   H's referent type must be one of the bits in MASK.  The caller
   must verify this ahead of time; we simply assert it here.

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
fh_open (struct file_handle *h, enum fh_referent mask UNUSED,
         const char *type, const char *mode) 
{
  assert (h != NULL);
  assert ((fh_get_referent (h) & mask) != 0);
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
                     "already open as a %s."),
               fh_get_name (h), type, h->type);
          return NULL; 
        }
      else if (strcmp (h->open_mode, mode)) 
        {
          msg (SE, _("Can't open %s as a %s for %s because it is "
                     "already open for %s."),
               fh_get_name (h), type, mode_name (mode),
               mode_name (h->open_mode));
          return NULL;
        }
      else if (h->open_mode[1] == 'e')
        {
          msg (SE, _("Can't re-open %s as a %s for %s."),
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
   open due to another reference.

   After fh_close() returns zero for a handle, it is unsafe to
   reference that file handle again in any way, because its
   storage may have been freed. */
int
fh_close (struct file_handle *h, const char *type, const char *mode)
{
  assert (h != NULL);
  assert (h->open_cnt > 0);
  assert (type != NULL);
  assert (!strcmp (type, h->type));
  assert (mode != NULL);
  assert (!strcmp (mode, h->open_mode));

  if (--h->open_cnt == 0) 
    {
      h->type = NULL;
      h->aux = NULL;
      if (h->deleted)
        free_handle (h);
      return 0;
    }
  return 1;
}

/* Is the file open?  BEGIN DATA...END DATA uses this to detect
   whether the inline file is actually in use. */
bool
fh_is_open (const struct file_handle *handle) 
{
  return handle->open_cnt > 0;
}

/* Returns the identifier of file HANDLE.  If HANDLE was created
   by referring to a filename instead of a handle name, returns
   the filename, enclosed in double quotes.  Return value is
   owned by the file handle. 

   Useful for printing error messages about use of file handles.  */
const char *
fh_get_name (const struct file_handle *handle)
{
  return handle->name;
}

/* Returns the type of object that HANDLE refers to. */
enum fh_referent
fh_get_referent (const struct file_handle *handle) 
{
  return handle->referent;
}

/* Returns the name of the file associated with HANDLE. */
const char *
fh_get_filename (const struct file_handle *handle) 
{
  assert (handle->referent == FH_REF_FILE);
  return handle->filename;
}

/* Returns the mode of HANDLE. */
enum fh_mode
fh_get_mode (const struct file_handle *handle) 
{
  assert (handle->referent == FH_REF_FILE);
  return handle->mode;
}

/* Returns the width of a logical record on HANDLE. */
size_t
fh_get_record_width (const struct file_handle *handle)
{
  assert (handle->referent & (FH_REF_FILE | FH_REF_INLINE));
  return handle->record_width;
}

/* Returns the number of characters per tab stop for HANDLE, or
   zero if tabs are not to be expanded.  Applicable only to
   FH_MODE_TEXT files. */
size_t
fh_get_tab_width (const struct file_handle *handle) 
{
  assert (handle->referent & (FH_REF_FILE | FH_REF_INLINE));
  return handle->tab_width;
}

/* Returns the scratch file handle associated with HANDLE.
   Applicable to only FH_REF_SCRATCH files. */
struct scratch_handle *
fh_get_scratch_handle (struct file_handle *handle) 
{
  assert (handle->referent == FH_REF_SCRATCH);
  return handle->sh;
}

/* Sets SH to be the scratch file handle associated with HANDLE.
   Applicable to only FH_REF_SCRATCH files. */
void
fh_set_scratch_handle (struct file_handle *handle, struct scratch_handle *sh)
{
  assert (handle->referent == FH_REF_SCRATCH);
  handle->sh = sh;
}

/* Returns the current default handle. */
struct file_handle *
fh_get_default_handle (void) 
{
  return default_handle ? default_handle : fh_inline_file ();
}

/* Sets NEW_DEFAULT_HANDLE as the default handle. */
void
fh_set_default_handle (struct file_handle *new_default_handle) 
{
  assert (new_default_handle == NULL
          || (new_default_handle->referent & (FH_REF_INLINE | FH_REF_FILE)));
  default_handle = new_default_handle;
}
