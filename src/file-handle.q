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
#include <errno.h>
#include <stdlib.h>
#include "alloc.h"
#include "avl.h"
#include "filename.h"
#include "file-handle.h"
#include "command.h"
#include "lexer.h"
#include "getline.h"
#include "error.h"
#include "magic.h"
#include "var.h"
/* (headers) */

#undef DEBUGGING
/*#define DEBUGGING 1*/
#include "debug-print.h"

avl_tree *files;
struct file_handle *inline_file;

static void init_file_handle (struct file_handle * handle);

/* (specification)
   "FILE HANDLE" (fh_):
     name=string;
     recform=recform:fixed/!variable/spanned;
     lrecl=integer;
     mode=mode:!character/image/binary/multipunch/_360.
*/
/* (declarations) */
/* (functions) */

int
cmd_file_handle (void)
{
  char handle_name[9];
  char *handle_name_p = handle_name;

  struct cmd_file_handle cmd;
  struct file_handle *fp;

  lex_get ();
  if (!lex_force_id ())
    return CMD_FAILURE;
  strcpy (handle_name, tokid);

  fp = NULL;
  if (files)
    fp = avl_find (files, &handle_name_p);
  if (fp)
    {
      msg (SE, _("File handle %s had already been defined to refer to "
		 "file %s.  It is not possible to redefine a file "
		 "handle within a session."),
	   tokid, fp->fn);
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

  fp = xmalloc (sizeof *fp);
  init_file_handle (fp);

  switch (cmd.recform)
    {
    case FH_FIXED:
      if (cmd.n_lrecl == NOT_LONG)
	{
	  msg (SE, _("Fixed length records were specified on /RECFORM, but "
	       "record length was not specified on /LRECL.  80-character "
	       "records will be assumed."));
	  cmd.n_lrecl = 80;
	}
      else if (cmd.n_lrecl < 1)
	{
	  msg (SE, _("Record length (%ld) must be at least one byte.  "
		     "80-character records will be assumed."), cmd.n_lrecl);
	  cmd.n_lrecl = 80;
	}
      fp->recform = FH_RF_FIXED;
      fp->lrecl = cmd.n_lrecl;
      break;
    case FH_VARIABLE:
      fp->recform = FH_RF_VARIABLE;
      break;
    case FH_SPANNED:
      msg (SE, _("/RECFORM SPANNED is not implemented, as the author doesn't "
	   "know what it is supposed to do.  Send the author a note."));
      break;
    default:
      assert (0);
    }

  switch (cmd.mode)
    {
    case FH_CHARACTER:
      fp->mode = FH_MD_CHARACTER;
      break;
    case FH_IMAGE:
      msg (SE, _("/MODE IMAGE is not implemented, as the author doesn't know "
	   "what it is supposed to do.  Send the author a note."));
      break;
    case FH_BINARY:
      fp->mode = FH_MD_BINARY;
      break;
    case FH_MULTIPUNCH:
      msg (SE, _("/MODE MULTIPUNCH is not implemented.  If you care, "
		 "complain."));
      break;
    case FH__360:
      msg (SE, _("/MODE 360 is not implemented.  If you care, complain."));
      break;
    default:
      assert (0);
    }

  fp->name = xstrdup (handle_name);
  fp->norm_fn = fn_normalize (cmd.s_name);
  fp->where.filename = fp->fn = cmd.s_name;
  avl_force_insert (files, fp);

  return CMD_SUCCESS;

 lossage:
  free_file_handle (&cmd);
  return CMD_FAILURE;
}

/* File handle functions. */

/* Sets up some fields in H; caller should fill in
   H->{NAME,NORM_FN,FN}. */
static void
init_file_handle (struct file_handle *h)
{
  h->recform = FH_RF_VARIABLE;
  h->mode = FH_MD_CHARACTER;
  h->ext = NULL;
  h->class = NULL;
}

/* Returns the handle corresponding to FILENAME.  Creates the handle
   if no handle exists for that file.  All filenames are normalized
   first, so different filenames referring to the same file will
   return the same file handle. */
struct file_handle *
fh_get_handle_by_filename (const char *filename)
{
  struct file_handle f, *fp;
  char *fn;
  char *name;
  int len;

  /* Get filename. */
  fn = fn_normalize (filename);
  len = strlen (fn);

  /* Create handle name with invalid identifier character to prevent
     conflicts with handles created with FILE HANDLE. */
  name = xmalloc (len + 2);
  name[0] = '*';
  strcpy (&name[1], fn);

  f.name = name;
  fp = avl_find (files, &f);
  if (!fp)
    {
      fp = xmalloc (sizeof *fp);
      init_file_handle (fp);
      fp->name = name;
      fp->norm_fn = fn;
      fp->where.filename = fp->fn = xstrdup (filename);
      avl_force_insert (files, fp);
    }
  else
    {
      free (fn);
      free (name);
    }
  return fp;
}

/* Returns the handle with identifier NAME, if it exists; otherwise
   reports error to user and returns NULL. */
struct file_handle *
fh_get_handle_by_name (const char name[9])
{
  struct file_handle f, *fp;
  f.name = (char *) name;
  fp = avl_find (files, &f);

  if (!fp)
    msg (SE, _("File handle `%s' has not been previously declared on "
	 "FILE HANDLE."), name);
  return fp;
}

/* Returns the identifier of file HANDLE.  If HANDLE was created by
   referring to a filename (i.e., DATA LIST FILE='yyy' instead of FILE
   HANDLE XXX='yyy'), returns the filename, enclosed in double quotes.
   Return value is in a static buffer.

   Useful for printing error messages about use of file handles.  */
const char *
fh_handle_name (struct file_handle *h)
{
  static char *buf = NULL;

  if (buf)
    {
      free (buf);
      buf = NULL;
    }
  if (!h)
    return NULL;

  if (h->name[0] == '*')
    {
      int len = strlen (h->fn);

      buf = xmalloc (len + 3);
      strcpy (&buf[1], h->fn);
      buf[0] = buf[len + 1] = '"';
      buf[len + 2] = 0;
      return buf;
    }
  return h->name;
}

/* Closes the stdio FILE associated with handle H.  Frees internal
   buffers associated with that file.  Does *not* destroy the file
   handle H.  (File handles are permanent during a session.)  */
void
fh_close_handle (struct file_handle *h)
{
  if (h == NULL)
    return;

  debug_printf (("Closing %s%s.\n", fh_handle_name (h),
		 h->class == NULL ? " (already closed)" : ""));

  if (h->class)
    h->class->close (h);
  h->class = NULL;
  h->ext = NULL;
}

/* Compares names of file handles A and B. */
static int
cmp_file_handle (const void *a, const void *b, void *foo unused)
{
  return strcmp (((struct file_handle *) a)->name,
		 ((struct file_handle *) b)->name);
}

/* Initialize the AVL tree of file handles; inserts the "inline file"
   inline_file. */
void
fh_init_files (void)
{
  /* Create AVL tree. */
  files = avl_create (NULL, cmp_file_handle, NULL);

  /* Insert inline file. */
  inline_file = xmalloc (sizeof *inline_file);
  init_file_handle (inline_file);
  inline_file->name = "INLINE";
  inline_file->where.filename
    = inline_file->fn = inline_file->norm_fn = (char *) _("<Inline File>");
  inline_file->where.line_number = 0;
  avl_force_insert (files, inline_file);
}

/* Parses a file handle name, which may be a filename as a string or
   a file handle name as an identifier.  Returns the file handle or
   NULL on failure. */
struct file_handle *
fh_parse_file_handle (void)
{
  struct file_handle *handle;

  if (token == T_ID)
    handle = fh_get_handle_by_name (tokid);
  else if (token == T_STRING)
    handle = fh_get_handle_by_filename (ds_value (&tokstr));
  else
    {
      lex_error (_("expecting a file name or handle"));
      return NULL;
    }

  if (!handle)
    return NULL;
  lex_get ();

  return handle;
}

/* Returns the (normalized) filename associated with file handle H. */
char *
fh_handle_filename (struct file_handle * h)
{
  return h->norm_fn;
}

/* Returns the width of a logical record on file handle H. */
size_t
fh_record_width (struct file_handle *h)
{
  if (h == inline_file)
    return 80;
  else if (h->recform == FH_RF_FIXED)
    return h->lrecl;
  else
    return 1024;
}

/*
   Local variables:
   mode: c
   End:
*/
