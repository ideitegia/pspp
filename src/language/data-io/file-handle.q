/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011 Free Software Foundation, Inc.

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

#include <limits.h>
#include <errno.h>
#include <stdlib.h>

#include "data/file-name.h"
#include "language/command.h"
#include "language/data-io/file-handle.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "data/variable.h"
#include "data/file-handle-def.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* (headers) */


/* (specification)
   "FILE HANDLE" (fh_):
     name=string;
     lrecl=integer;
     tabwidth=integer "x>=0" "%s must be nonnegative";
     mode=mode:!character/binary/image/360/scratch;
     recform=recform:fixed/f/variable/v/spanned/vs.
*/
/* (declarations) */
/* (functions) */

int
cmd_file_handle (struct lexer *lexer, struct dataset *ds)
{
  struct cmd_file_handle cmd;
  struct file_handle *handle;
  enum cmd_result result;
  char *handle_name;

  result = CMD_CASCADING_FAILURE;
  if (!lex_force_id (lexer))
    goto exit;

  handle_name = xstrdup (lex_tokcstr (lexer));
  handle = fh_from_id (handle_name);
  if (handle != NULL)
    {
      msg (SE, _("File handle %s is already defined.  "
                 "Use CLOSE FILE HANDLE before redefining a file handle."),
	   handle_name);
      goto exit_free_handle_name;
    }

  lex_get (lexer);
  if (!lex_force_match (lexer, T_SLASH))
    goto exit_free_handle_name;

  if (!parse_file_handle (lexer, ds, &cmd, NULL))
    goto exit_free_handle_name;

  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto exit_free_cmd;

  if (cmd.mode != FH_SCRATCH)
    {
      struct fh_properties properties = *fh_default_properties ();

      if (cmd.s_name == NULL)
        {
          lex_sbc_missing (lexer, "NAME");
          goto exit_free_cmd;
        }

      switch (cmd.mode)
        {
        case FH_CHARACTER:
          properties.mode = FH_MODE_TEXT;
          if (cmd.sbc_tabwidth)
            properties.tab_width = cmd.n_tabwidth[0];
          break;
        case FH_IMAGE:
          properties.mode = FH_MODE_FIXED;
          break;
        case FH_BINARY:
          properties.mode = FH_MODE_VARIABLE;
          break;
        case FH_360:
          properties.encoding = "EBCDIC-US";
          if (cmd.recform == FH_FIXED || cmd.recform == FH_F)
            properties.mode = FH_MODE_FIXED;
          else if (cmd.recform == FH_VARIABLE || cmd.recform == FH_V)
            {
              properties.mode = FH_MODE_360_VARIABLE;
              properties.record_width = 8192;
            }
          else if (cmd.recform == FH_SPANNED || cmd.recform == FH_VS)
            {
              properties.mode = FH_MODE_360_SPANNED;
              properties.record_width = 8192;
            }
          else
            {
              msg (SE, _("RECFORM must be specified with MODE=360."));
              goto exit_free_cmd;
            }
          break;
        default:
          NOT_REACHED ();
        }

      if (properties.mode == FH_MODE_FIXED || cmd.n_lrecl[0] != LONG_MIN)
        {
          if (cmd.n_lrecl[0] == LONG_MIN)
            msg (SE, _("The specified file mode requires LRECL.  "
                       "Assuming %zu-character records."),
                 properties.record_width);
          else if (cmd.n_lrecl[0] < 1 || cmd.n_lrecl[0] >= (1UL << 31))
            msg (SE, _("Record length (%ld) must be between 1 and %lu bytes.  "
                       "Assuming %zu-character records."),
                 cmd.n_lrecl[0], (1UL << 31) - 1, properties.record_width);
          else
            properties.record_width = cmd.n_lrecl[0];
        }

      fh_create_file (handle_name, cmd.s_name, &properties);
    }
  else
    fh_create_scratch (handle_name);

  result = CMD_SUCCESS;

exit_free_cmd:
  free_file_handle (&cmd);
exit_free_handle_name:
  free (handle_name);
exit:
  return result;
}

int
cmd_close_file_handle (struct lexer *lexer, struct dataset *ds UNUSED)
{
  struct file_handle *handle;

  if (!lex_force_id (lexer))
    return CMD_CASCADING_FAILURE;
  handle = fh_from_id (lex_tokcstr (lexer));
  if (handle == NULL)
    return CMD_CASCADING_FAILURE;

  fh_unname (handle);
  return CMD_SUCCESS;
}

/* Returns the name for REFERENT. */
static const char *
referent_name (enum fh_referent referent)
{
  switch (referent)
    {
    case FH_REF_FILE:
      return _("file");
    case FH_REF_INLINE:
      return _("inline file");
    case FH_REF_SCRATCH:
      return _("scratch file");
    default:
      NOT_REACHED ();
    }
}

/* Parses a file handle name, which may be a file name as a string
   or a file handle name as an identifier.  The allowed types of
   file handle are restricted to those in REFERENT_MASK.  Returns
   the file handle when successful, a null pointer on failure.

   The caller is responsible for fh_unref()'ing the returned
   file handle when it is no longer needed. */
struct file_handle *
fh_parse (struct lexer *lexer, enum fh_referent referent_mask)
{
  struct file_handle *handle;

  if (lex_match_id (lexer, "INLINE"))
    handle = fh_inline_file ();
  else
    {
      if (lex_token (lexer) != T_ID && !lex_is_string (lexer))
        {
          lex_error (lexer, _("expecting a file name or handle name"));
          return NULL;
        }

      handle = NULL;
      if (lex_token (lexer) == T_ID)
        handle = fh_from_id (lex_tokcstr (lexer));
      if (handle == NULL)
        {
          if (lex_token (lexer) != T_ID || lex_tokcstr (lexer)[0] != '#'
              || settings_get_syntax () != ENHANCED)
            handle = fh_create_file (NULL, lex_tokcstr (lexer),
                                     fh_default_properties ());
          else
            handle = fh_create_scratch (lex_tokcstr (lexer));
        }
      lex_get (lexer);
    }

  if (!(fh_get_referent (handle) & referent_mask))
    {
      msg (SE, _("Handle for %s not allowed here."),
           referent_name (fh_get_referent (handle)));
      fh_unref (handle);
      return NULL;
    }

  return handle;
}

/*
   Local variables:
   mode: c
   End:
*/
