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
#include "error.h"
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
#include "linked-list.h"

/* (headers) */

/* File handle. */
struct file_handle 
  {
    struct file_handle *next;   /* Next in global list. */
    char *name;                 /* File handle identifier. */
    char *filename;		/* Filename as provided by user. */
    struct file_identity *identity; /* For checking file identity. */
    struct file_locator where;	/* Used for reporting error messages. */
    enum file_handle_mode mode;	/* File mode. */
    size_t length;		/* Length of fixed-format records. */
    size_t tab_width;           /* Tab width, 0=do not expand tabs. */

    int open_cnt;               /* 0=not open, otherwise # of openers. */
    const char *type;           /* If open, type of file. */
    const char *open_mode;      /* "[rw][se]". */
    void *aux;                  /* Aux data pointer for owner if any. */
  };

static struct file_handle *file_handles;

static struct file_handle *create_file_handle (const char *handle_name,
                                               const char *filename);

/* (specification)
   "FILE HANDLE" (fh_):
     name=string;
     lrecl=integer;
     tabwidth=integer "x>=0" "%s must be nonnegative";
     mode=mode:!character/image.
*/
/* (declarations) */
/* (functions) */

static struct file_handle *
get_handle_with_name (const char *handle_name) 
{
  struct file_handle *iter;

  for (iter = file_handles; iter != NULL; iter = iter->next)
    if (!strcmp (handle_name, iter->name))
      return iter;
  return NULL;
}

static struct file_handle *
get_handle_for_filename (const char *filename)
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
	   tokid, handle->filename);
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
      handle->mode = MODE_TEXT;
      if (cmd.sbc_tabwidth)
        handle->tab_width = cmd.n_tabwidth[0];
      else
        handle->tab_width = 4;
      break;
    case FH_IMAGE:
      handle->mode = MODE_BINARY;
      if (cmd.n_lrecl[0] == NOT_LONG)
	{
	  msg (SE, _("Fixed-length records were specified on /RECFORM, but "
                     "record length was not specified on /LRECL.  "
                     "Assuming 1024-character records."));
          handle->length = 1024;
	}
      else if (cmd.n_lrecl[0] < 1)
	{
	  msg (SE, _("Record length (%ld) must be at least one byte.  "
		     "1-character records will be assumed."), cmd.n_lrecl);
          handle->length = 1;
	}
      else
        handle->length = cmd.n_lrecl[0];
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

  /* Create and initialize file handle. */
  handle = xmalloc (sizeof *handle);
  handle->next = file_handles;
  handle->name = xstrdup (handle_name);
  handle->filename = xstrdup (filename);
  handle->identity = fn_get_identity (filename);
  handle->where.filename = handle->filename;
  handle->where.line_number = 0;
  handle->mode = MODE_TEXT;
  handle->length = 1024;
  handle->tab_width = 4;
  handle->open_cnt = 0;
  handle->type = NULL;
  handle->open_mode = NULL;
  handle->aux = NULL;
  file_handles = handle;

  return handle;
}

void
destroy_file_handle(struct file_handle *fh, void *aux UNUSED)
{
  free (fh->name);
  free (fh->filename);
  fn_free_identity (fh->identity);
  free (fh);
}

static const char *
mode_name (const char *mode) 
{
  assert (mode != NULL);
  assert (mode[0] == 'r' || mode[0] == 'w');

  return mode[0] == 'r' ? "reading" : "writing";
}


/* Tries to open FILE with the given TYPE and MODE.

   TYPE is the sort of file, e.g. "system file".  Only one given
   type of access is allowed on a given file handle at once.

   MODE combines the read or write mode with the sharing mode.
   The first character is 'r' for read, 'w' for write.  The
   second character is 's' to permit sharing, 'e' to require
   exclusive access.

   Returns the address of a void * that the caller can use for
   data specific to the file handle if successful, or a null
   pointer on failure.  For exclusive access modes the void *
   will always be a null pointer at return.  In shared access
   modes the void * will necessarily be null only if no other
   sharers are active.

   If successful, references to type and mode are retained, so
   they should probably be string literals. */
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
        msg (SE, _("Can't open %s as a %s because it is "
                   "already open as a %s"),
             handle_get_name (h), type, h->type);
      else if (strcmp (h->open_mode, mode))
        msg (SE, _("Can't open %s as a %s for %s because it is "
                   "already open for %s"),
             handle_get_name (h), type,
             mode_name (mode), mode_name (h->open_mode));
      else if (h->open_mode[1] == 'e')
        msg (SE, _("Can't re-open %s as a %s for %s"),
             handle_get_name (h), type, mode_name (mode));
    }
  else 
    {
      h->type = type;
      h->open_mode = mode;
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
      h->open_mode = NULL;
      h->aux = NULL;
    }
  return h->open_cnt;
}


static struct linked_list *handle_list;


/* Parses a file handle name, which may be a filename as a string or
   a file handle name as an identifier.  Returns the file handle or
   NULL on failure. */
struct file_handle *
fh_parse (void)
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
    handle = get_handle_for_filename (ds_c_str (&tokstr));
  if (handle == NULL) 
    {
      char *filename = ds_c_str (&tokstr);
      char *handle_name = xmalloc (strlen (filename) + 3);
      sprintf (handle_name, "\"%s\"", filename);
      handle = create_file_handle (handle_name, filename);
      ll_push_front(handle_list, handle);
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
  assert (handle != NULL);
  return handle->name;
}

/* Returns the name of the file associated with HANDLE. */
const char *
handle_get_filename (const struct file_handle *handle) 
{
  assert (handle != NULL);
  return handle->filename;
}

/* Returns the mode of HANDLE. */
enum file_handle_mode
handle_get_mode (const struct file_handle *handle) 
{
  assert (handle != NULL);
  return handle->mode;
}

/* Returns the width of a logical record on HANDLE.  Applicable
   only to MODE_BINARY files.  */
size_t
handle_get_record_width (const struct file_handle *handle)
{
  assert (handle != NULL);
  return handle->length;
}

/* Returns the number of characters per tab stop for HANDLE, or
   zero if tabs are not to be expanded.  Applicable only to
   MODE_TEXT files. */
size_t
handle_get_tab_width (const struct file_handle *handle) 
{
  assert (handle != NULL);
  return handle->tab_width;
}


void 
fh_init(void)
{
  handle_list = ll_create(destroy_file_handle,0);
}

void 
fh_done(void)
{
  assert(handle_list);
  
  ll_destroy(handle_list);
  handle_list = 0;
}


/*
   Local variables:
   mode: c
   End:
*/
