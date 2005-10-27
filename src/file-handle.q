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
#include "error.h"
#include <errno.h>
#include <stdlib.h>
#include "alloc.h"
#include "filename.h"
#include "command.h"
#include "lexer.h"
#include "getl.h"
#include "error.h"
#include "magic.h"
#include "var.h"
#include "linked-list.h"
#include "file-handle-def.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* (headers) */


/* (specification)
   "FILE HANDLE" (fh_):
     name=string;
     lrecl=integer;
     tabwidth=integer "x>=0" "%s must be nonnegative";
     mode=mode:!character/image.
*/
/* (declarations) */
/* (functions) */


int
cmd_file_handle (void)
{
  char handle_name[LONG_NAME_LEN + 1];

  struct cmd_file_handle cmd;
  struct file_handle *handle;

  if (!lex_force_id ())
    return CMD_FAILURE;
  str_copy_trunc (handle_name, sizeof handle_name, tokid);

  handle = get_handle_with_name (handle_name);
  if (handle != NULL)
    {
      msg (SE, _("File handle %s already refers to file %s.  "
                 "File handles cannot be redefined within a session."),
	   handle_name, handle_get_filename(handle));
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


  enum file_handle_mode mode = MODE_TEXT;
  size_t length = 1024;
  size_t tab_width = 4;


  switch (cmd.mode)
    {
    case FH_CHARACTER:
      mode = MODE_TEXT;
      if (cmd.sbc_tabwidth)
        tab_width = cmd.n_tabwidth[0];
      else
        tab_width = 4;
      break;
    case FH_IMAGE:
      mode = MODE_BINARY;
      if (cmd.n_lrecl[0] == NOT_LONG)
	{
	  msg (SE, _("Fixed-length records were specified on /RECFORM, but "
                     "record length was not specified on /LRECL.  "
                     "Assuming 1024-character records."));
          length = 1024;
	}
      else if (cmd.n_lrecl[0] < 1)
	{
	  msg (SE, _("Record length (%ld) must be at least one byte.  "
		     "1-character records will be assumed."), cmd.n_lrecl[0]);
          length = 1;
	}
      else
        length = cmd.n_lrecl[0];
      break;
    default:
      assert (0);
    }

  handle = create_file_handle (handle_name, cmd.s_name, 
			       mode, length, tab_width);


  return CMD_SUCCESS;

 lossage:
  free_file_handle (&cmd);
  return CMD_FAILURE;
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
      handle = create_file_handle_with_defaults (handle_name, filename);
      ll_push_front(handle_list, handle);
      free (handle_name);
    }

  lex_get ();


  return handle;
}



void 
fh_init(void)
{
  handle_list = ll_create(destroy_file_handle,0);
}

void 
fh_done(void)
{
  if ( handle_list )  
  {
    ll_destroy(handle_list);
    handle_list = 0;
  }
}


/*
   Local variables:
   mode: c
   End:
*/
