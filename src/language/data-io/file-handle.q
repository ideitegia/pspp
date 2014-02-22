/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include "data/file-handle-def.h"

#include <limits.h>
#include <errno.h>
#include <stdlib.h>

#include "data/file-name.h"
#include "data/session.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/data-io/file-handle.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* (headers) */


/* (specification)
   "FILE HANDLE" (fh_):
     name=string;
     lrecl=integer;
     tabwidth=integer;
     mode=mode:!character/binary/image/360;
     ends=ends:lf/crlf;
     recform=recform:fixed/f/variable/v/spanned/vs;
     encoding=string.
*/
/* (declarations) */
/* (functions) */

int
cmd_file_handle (struct lexer *lexer, struct dataset *ds)
{
  struct fh_properties properties;
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
                 "Use %s before redefining a file handle."),
	   handle_name, "CLOSE FILE HANDLE");
      goto exit_free_handle_name;
    }

  lex_get (lexer);
  if (!lex_force_match (lexer, T_SLASH))
    goto exit_free_handle_name;

  if (!parse_file_handle (lexer, ds, &cmd, NULL))
    goto exit_free_handle_name;

  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto exit_free_cmd;

  properties = *fh_default_properties ();
  if (cmd.s_name == NULL)
    {
      lex_sbc_missing ("NAME");
      goto exit_free_cmd;
    }

  switch (cmd.mode)
    {
    case FH_CHARACTER:
      properties.mode = FH_MODE_TEXT;
      if (cmd.sbc_tabwidth)
        {
          if (cmd.n_tabwidth[0] >= 0)
            properties.tab_width = cmd.n_tabwidth[0];
          else
            msg (SE, _("%s must not be negative."), "TABWIDTH");
        }
      if (cmd.ends == FH_LF)
        properties.line_ends = FH_END_LF;
      else if (cmd.ends == FH_CRLF)
        properties.line_ends = FH_END_CRLF;
      break;
    case FH_IMAGE:
      properties.mode = FH_MODE_FIXED;
      break;
    case FH_BINARY:
      properties.mode = FH_MODE_VARIABLE;
      break;
    case FH_360:
      properties.encoding = CONST_CAST (char *, "EBCDIC-US");
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
          msg (SE, _("%s must be specified with %s."), "RECFORM", "MODE=360");
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

  if (cmd.s_encoding != NULL)
    properties.encoding = cmd.s_encoding;

  fh_create_file (handle_name, cmd.s_name, &properties);

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
    case FH_REF_DATASET:
      return _("dataset");
    default:
      NOT_REACHED ();
    }
}

/* Parses a file handle name:

      - If SESSION is nonnull, then the parsed syntax may be the name of a
        dataset within SESSION.  Dataset names take precedence over file handle
        names.

      - If REFERENT_MASK includes FH_REF_FILE, the parsed syntax may be a file
        name as a string or a file handle name as an identifier.

      - If REFERENT_MASK includes FH_REF_INLINE, the parsed syntax may be the
        identifier INLINE to represent inline data.

   Returns the file handle when successful, a null pointer on failure.

   The caller is responsible for fh_unref()'ing the returned file handle when
   it is no longer needed. */
struct file_handle *
fh_parse (struct lexer *lexer, enum fh_referent referent_mask,
          struct session *session)
{
  struct file_handle *handle;

  if (session != NULL && lex_token (lexer) == T_ID)
    {
      struct dataset *ds;

      ds = session_lookup_dataset (session, lex_tokcstr (lexer));
      if (ds != NULL)
        {
          lex_get (lexer);
          return fh_create_dataset (ds);
        }
    }

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
            handle = fh_create_file (NULL, lex_tokcstr (lexer),
                                     fh_default_properties ());
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
