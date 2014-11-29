/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include "data/any-writer.h"
#include "data/case-map.h"
#include "data/case.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/por-file-writer.h"
#include "data/sys-file-writer.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/data-io/file-handle.h"
#include "language/data-io/trim.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Writing system and portable files. */

/* Type of output file. */
enum writer_type
  {
    SYSFILE_WRITER,     /* System file. */
    PORFILE_WRITER      /* Portable file. */
  };

/* Type of a command. */
enum command_type
  {
    XFORM_CMD,          /* Transformation. */
    PROC_CMD            /* Procedure. */
  };

static int parse_output_proc (struct lexer *, struct dataset *,
                              enum writer_type);
static int parse_output_trns (struct lexer *, struct dataset *,
                              enum writer_type);

int
cmd_save (struct lexer *lexer, struct dataset *ds)
{
  return parse_output_proc (lexer, ds, SYSFILE_WRITER);
}

int
cmd_export (struct lexer *lexer, struct dataset *ds)
{
  return parse_output_proc (lexer, ds, PORFILE_WRITER);
}

int
cmd_xsave (struct lexer *lexer, struct dataset *ds)
{
  return parse_output_trns (lexer, ds, SYSFILE_WRITER);
}

int
cmd_xexport (struct lexer *lexer, struct dataset *ds)
{
  return parse_output_trns (lexer, ds, PORFILE_WRITER);
}

struct output_trns
  {
    struct casewriter *writer;          /* Writer. */
  };

static trns_proc_func output_trns_proc;
static trns_free_func output_trns_free;
static struct casewriter *parse_write_command (struct lexer *,
                                               struct dataset *,
                                               enum writer_type,
                                               enum command_type,
                                               bool *retain_unselected);

/* Parses and performs the SAVE or EXPORT procedure. */
static int
parse_output_proc (struct lexer *lexer, struct dataset *ds,
                   enum writer_type writer_type)
{
  bool retain_unselected;
  struct casewriter *output;
  bool ok;

  output = parse_write_command (lexer, ds, writer_type, PROC_CMD,
                                &retain_unselected);
  if (output == NULL)
    return CMD_CASCADING_FAILURE;

  casereader_transfer (proc_open_filtering (ds, !retain_unselected), output);
  ok = casewriter_destroy (output);
  ok = proc_commit (ds) && ok;

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

/* Parses the XSAVE or XEXPORT transformation command. */
static int
parse_output_trns (struct lexer *lexer, struct dataset *ds, enum writer_type writer_type)
{
  struct output_trns *t = xmalloc (sizeof *t);
  t->writer = parse_write_command (lexer, ds, writer_type, XFORM_CMD, NULL);
  if (t->writer == NULL)
    {
      free (t);
      return CMD_CASCADING_FAILURE;
    }

  add_transformation (ds, output_trns_proc, output_trns_free, t);
  return CMD_SUCCESS;
}

/* Parses SAVE or XSAVE or EXPORT or XEXPORT command.
   WRITER_TYPE identifies the type of file to write,
   and COMMAND_TYPE identifies the type of command.

   On success, returns a writer.
   For procedures only, sets *RETAIN_UNSELECTED to true if cases
   that would otherwise be excluded by FILTER or USE should be
   included.

   On failure, returns a null pointer. */
static struct casewriter *
parse_write_command (struct lexer *lexer, struct dataset *ds,
		     enum writer_type writer_type,
                     enum command_type command_type,
                     bool *retain_unselected)
{
  /* Common data. */
  struct file_handle *handle; /* Output file. */
  struct dictionary *dict;    /* Dictionary for output file. */
  struct casewriter *writer;  /* Writer. */
  struct case_map_stage *stage; /* Preparation for 'map'. */
  struct case_map *map;       /* Map from input data to data for writer. */

  /* Common options. */
  struct sfm_write_options sysfile_opts;
  struct pfm_write_options porfile_opts;

  assert (writer_type == SYSFILE_WRITER || writer_type == PORFILE_WRITER);
  assert (command_type == XFORM_CMD || command_type == PROC_CMD);
  assert ((retain_unselected != NULL) == (command_type == PROC_CMD));

  if (command_type == PROC_CMD)
    *retain_unselected = true;

  handle = NULL;
  dict = dict_clone (dataset_dict (ds));
  writer = NULL;
  stage = NULL;
  map = NULL;
  sysfile_opts = sfm_writer_default_options ();
  porfile_opts = pfm_writer_default_options ();

  stage = case_map_stage_create (dict);
  dict_delete_scratch_vars (dict);

  lex_match (lexer, T_SLASH);
  for (;;)
    {
      if (lex_match_id (lexer, "OUTFILE"))
	{
          if (handle != NULL)
            {
              lex_sbc_only_once ("OUTFILE");
              goto error;
            }

	  lex_match (lexer, T_EQUALS);

	  handle = fh_parse (lexer, FH_REF_FILE, NULL);
	  if (handle == NULL)
	    goto error;
	}
      else if (lex_match_id (lexer, "NAMES"))
        {
          /* Not yet implemented. */
        }
      else if (lex_match_id (lexer, "PERMISSIONS"))
        {
          bool cw;

          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "READONLY"))
            cw = false;
          else if (lex_match_id (lexer, "WRITEABLE"))
            cw = true;
          else
            {
              lex_error_expecting (lexer, "READONLY", "WRITEABLE",
                                   NULL_SENTINEL);
              goto error;
            }
          sysfile_opts.create_writeable = porfile_opts.create_writeable = cw;
        }
      else if (command_type == PROC_CMD && lex_match_id (lexer, "UNSELECTED"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "RETAIN"))
            *retain_unselected = true;
          else if (lex_match_id (lexer, "DELETE"))
            *retain_unselected = false;
          else
            {
              lex_error_expecting (lexer, "RETAIN", "DELETE", NULL_SENTINEL);
              goto error;
            }
        }
      else if (writer_type == SYSFILE_WRITER
               && lex_match_id (lexer, "COMPRESSED"))
	sysfile_opts.compression = ANY_COMP_SIMPLE;
      else if (writer_type == SYSFILE_WRITER
               && lex_match_id (lexer, "UNCOMPRESSED"))
	sysfile_opts.compression = ANY_COMP_NONE;
      else if (writer_type == SYSFILE_WRITER
               && lex_match_id (lexer, "ZCOMPRESSED"))
	sysfile_opts.compression = ANY_COMP_ZLIB;
      else if (writer_type == SYSFILE_WRITER
               && lex_match_id (lexer, "VERSION"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (!lex_force_int (lexer))
            goto error;
          sysfile_opts.version = lex_integer (lexer);
          lex_get (lexer);
	}
      else if (writer_type == PORFILE_WRITER && lex_match_id (lexer, "TYPE"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "COMMUNICATIONS"))
            porfile_opts.type = PFM_COMM;
          else if (lex_match_id (lexer, "TAPE"))
            porfile_opts.type = PFM_TAPE;
          else
            {
              lex_error_expecting (lexer, "COMM", "TAPE", NULL_SENTINEL);
              goto error;
            }
        }
      else if (writer_type == PORFILE_WRITER && lex_match_id (lexer, "DIGITS"))
        {
          lex_match (lexer, T_EQUALS);
          if (!lex_force_int (lexer))
            goto error;
          porfile_opts.digits = lex_integer (lexer);
          lex_get (lexer);
        }
      else if (!parse_dict_trim (lexer, dict))
        goto error;

      if (!lex_match (lexer, T_SLASH))
	break;
    }
  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto error;

  if (handle == NULL)
    {
      lex_sbc_missing ("OUTFILE");
      goto error;
    }

  dict_delete_scratch_vars (dict);
  dict_compact_values (dict);

  if (fh_get_referent (handle) == FH_REF_FILE)
    {
      switch (writer_type)
        {
        case SYSFILE_WRITER:
          writer = sfm_open_writer (handle, dict, sysfile_opts);
          break;
        case PORFILE_WRITER:
          writer = pfm_open_writer (handle, dict, porfile_opts);
          break;
        }
    }
  else
    writer = any_writer_open (handle, dict);
  if (writer == NULL)
    goto error;

  map = case_map_stage_get_case_map (stage);
  case_map_stage_destroy (stage);
  if (map != NULL)
    writer = case_map_create_output_translator (map, writer);
  dict_destroy (dict);

  fh_unref (handle);
  return writer;

 error:
  case_map_stage_destroy (stage);
  fh_unref (handle);
  casewriter_destroy (writer);
  dict_destroy (dict);
  case_map_destroy (map);
  return NULL;
}

/* Writes case *C to the system file specified on XSAVE or XEXPORT. */
static int
output_trns_proc (void *trns_, struct ccase **c, casenumber case_num UNUSED)
{
  struct output_trns *t = trns_;
  casewriter_write (t->writer, case_ref (*c));
  return TRNS_CONTINUE;
}

/* Frees an XSAVE or XEXPORT transformation.
   Returns true if successful, false if an I/O error occurred. */
static bool
output_trns_free (void *trns_)
{
  struct output_trns *t = trns_;
  bool ok = casewriter_destroy (t->writer);
  free (t);
  return ok;
}
