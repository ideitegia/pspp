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
#include "file-handle.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include "alloc.h"
#include "filename.h"
#include "command.h"
#include "lexer.h"
#include "getline.h"
#include "error.h"
#include "magic.h"
#include "var.h"
/* (headers) */

/* File handle private data. */
struct private_file_handle 
  {
    char *name;                 /* File handle identifier. */
    char *filename;		/* Filename as provided by user. */
    struct file_identity *identity; /* For checking file identity. */
    struct file_locator where;	/* Used for reporting error messages. */
    enum file_handle_mode mode;	/* File mode. */
    size_t length;		/* Length of fixed-format records. */
  };

/* Linked list of file handles. */
struct file_handle_list 
  {
    struct file_handle *handle;
    struct file_handle_list *next;
  };

static struct file_handle_list *file_handles;
struct file_handle *inline_file;

static struct file_handle *create_file_handle (const char *handle_name,
                                               const char *filename);

/* (specification)
   "FILE HANDLE" (fh_):
     name=string;
     recform=recform:fixed/!variable/spanned;
     lrecl=integer;
     mode=mode:!character/image/binary/multipunch/_360.
*/
/* (declarations) */
/* (functions) */

static struct file_handle *
get_handle_with_name (const char *handle_name) 
{
  struct file_handle_list *iter;

  for (iter = file_handles; iter != NULL; iter = iter->next)
    if (!strcmp (handle_name, iter->handle->private->name))
      return iter->handle;
  return NULL;
}

static struct file_handle *
get_handle_for_filename (const char *filename)
{
  struct file_identity *identity;
  struct file_handle_list *iter;
      
  /* First check for a file with the same identity. */
  identity = fn_get_identity (filename);
  if (identity != NULL) 
    {
      for (iter = file_handles; iter != NULL; iter = iter->next)
        if (iter->handle->private->identity != NULL
            && !fn_compare_file_identities (identity,
                                         iter->handle->private->identity)) 
          {
            fn_free_identity (identity);
            return iter->handle; 
          }
      fn_free_identity (identity);
    }

  /* Then check for a file with the same name. */
  for (iter = file_handles; iter != NULL; iter = iter->next)
    if (!strcmp (filename, iter->handle->private->filename))
      return iter->handle; 

  return NULL;
}

int
cmd_file_handle (void)
{
  char handle_name[9];

  struct cmd_file_handle cmd;
  struct file_handle *handle;

  if (!lex_force_id ())
    return CMD_FAILURE;
  strcpy (handle_name, tokid);

  handle = get_handle_with_name (handle_name);
  if (handle != NULL)
    {
      msg (SE, _("File handle %s already refers to "
		 "file %s.  File handle cannot be redefined within a "
                 "session."),
	   tokid, handle->private->filename);
      return CMD_FAILURE;
    }

  lex_get ();
  if (!lex_force_match ('/'))
    return CMD_FAILURE;

  if (!parse_file_handle (&cmd))
    return CMD_FAILURE;

  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      goto lossage;
    }

  if (cmd.s_name == NULL)
    {
      msg (SE, _("The FILE HANDLE required subcommand NAME "
		 "is not present."));
      goto lossage;
    }

  handle = create_file_handle (handle_name, cmd.s_name);
  switch (cmd.mode)
    {
    case FH_CHARACTER:
      handle->private->mode = MODE_TEXT;
      break;
    case FH_IMAGE:
      handle->private->mode = MODE_BINARY;
      if (cmd.n_lrecl == NOT_LONG)
	{
	  msg (SE, _("Fixed-length records were specified on /RECFORM, but "
                     "record length was not specified on /LRECL.  "
                     "Assuming 1024-character records."));
          handle->private->length = 1024;
	}
      else if (cmd.n_lrecl < 1)
	{
	  msg (SE, _("Record length (%ld) must be at least one byte.  "
		     "1-character records will be assumed."), cmd.n_lrecl);
          handle->private->length = 1;
	}
      else
        handle->private->length = cmd.n_lrecl;
      break;
    default:
      assert (0);
    }

  return CMD_SUCCESS;

 lossage:
  free_file_handle (&cmd);
  return CMD_FAILURE;
}

/* File handle functions. */

/* Creates and returns a new file handle with the given values
   and defaults for other values.  Adds the created file handle
   to the global list. */
static struct file_handle *
create_file_handle (const char *handle_name, const char *filename)
{
  struct file_handle *handle;
  struct file_handle_list *list;

  /* Create and initialize file handle. */
  handle = xmalloc (sizeof *handle);
  handle->private = xmalloc (sizeof *handle->private);
  handle->private->name = xstrdup (handle_name);
  handle->private->filename = xstrdup (filename);
  handle->private->identity = fn_get_identity (filename);
  handle->private->where.filename = handle->private->filename;
  handle->private->where.line_number = 0;
  handle->private->mode = MODE_TEXT;
  handle->private->length = 1024;
  handle->ext = NULL;
  handle->class = NULL;

  /* Add file handle to global list. */
  list = xmalloc (sizeof *list);
  list->handle = handle;
  list->next = file_handles;
  file_handles = list;

  return handle;
}

/* Closes the stdio FILE associated with handle H.  Frees internal
   buffers associated with that file.  Does *not* destroy the file
   handle H.  (File handles are permanent during a session.)  */
void
fh_close_handle (struct file_handle *h)
{
  if (h == NULL)
    return;

  if (h->class != NULL)
    h->class->close (h);
  h->class = NULL;
  h->ext = NULL;
}

/* Initialize the hash of file handles; inserts the "inline file"
   inline_file. */
void
fh_init_files (void)
{
  if (inline_file == NULL) 
    {
      inline_file = create_file_handle ("INLINE", _("<Inline File>"));
      inline_file->private->length = 80; 
    }
}

/* Parses a file handle name, which may be a filename as a string or
   a file handle name as an identifier.  Returns the file handle or
   NULL on failure. */
struct file_handle *
fh_parse_file_handle (void)
{
  struct file_handle *handle;

  if (token != T_ID && token != T_STRING)
    {
      lex_error (_("expecting a file name or handle name"));
      return NULL;
    }

  /* Check for named handles first, then go by filename. */
  handle = NULL;
  if (token == T_ID) 
    handle = get_handle_with_name (tokid);
  if (handle == NULL)
    handle = get_handle_for_filename (ds_value (&tokstr));
  if (handle == NULL) 
    {
      char *filename = ds_value (&tokstr);
      char *handle_name = xmalloc (strlen (filename) + 3);
      sprintf (handle_name, "\"%s\"", filename);
      handle = create_file_handle (handle_name, filename);
      free (handle_name);
    }

  lex_get ();

  return handle;
}

/* Returns the identifier of file HANDLE.  If HANDLE was created
   by referring to a filename instead of a handle name, returns
   the filename, enclosed in double quotes.  Return value is
   owned by the file handle. 

   Useful for printing error messages about use of file handles.  */
const char *
handle_get_name (const struct file_handle *handle)
{
  return handle->private->name;
}

/* Returns the name of the file associated with HANDLE. */
const char *
handle_get_filename (const struct file_handle *handle) 
{
  return handle->private->filename;
}

/* Returns the mode of HANDLE. */
enum file_handle_mode
handle_get_mode (const struct file_handle *handle) 
{
  assert (handle != NULL);
  return handle->private->mode;
}

/* Returns the width of a logical record on HANDLE. */
size_t
handle_get_record_width (const struct file_handle *handle)
{
  assert (handle != NULL);
  return handle->private->length;
}

/*
   Local variables:
   mode: c
   End:
*/
