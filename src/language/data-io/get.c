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

#include <stdlib.h>

#include <data/any-reader.h>
#include <data/any-writer.h>
#include <data/case-sink.h>
#include <data/case-source.h>
#include <data/case.h>
#include <data/casefile.h>
#include <data/fastfile.h>
#include <data/dictionary.h>
#include <data/por-file-writer.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <data/storage-stream.h>
#include <data/sys-file-writer.h>
#include <data/transformations.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/file-handle.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Rearranging and reducing a dictionary. */
static void start_case_map (struct dictionary *);
static struct case_map *finish_case_map (struct dictionary *);
static void map_case (const struct case_map *,
                      const struct ccase *, struct ccase *);
static void destroy_case_map (struct case_map *);

static bool parse_dict_trim (struct dictionary *);

/* Reading system and portable files. */

/* Type of command. */
enum reader_command 
  {
    GET_CMD,
    IMPORT_CMD
  };

/* Case reader input program. */
struct case_reader_pgm 
  {
    struct any_reader *reader;  /* File reader. */
    struct case_map *map;       /* Map from file dict to active file dict. */
    struct ccase bounce;        /* Bounce buffer. */
  };

static const struct case_source_class case_reader_source_class;

static void case_reader_pgm_free (struct case_reader_pgm *);

/* Parses a GET or IMPORT command. */
static int
parse_read_command (enum reader_command type)
{
  struct case_reader_pgm *pgm = NULL;
  struct file_handle *fh = NULL;
  struct dictionary *dict = NULL;

  for (;;)
    {
      lex_match ('/');

      if (lex_match_id ("FILE") || token == T_STRING)
	{
	  lex_match ('=');

	  fh = fh_parse (FH_REF_FILE | FH_REF_SCRATCH);
	  if (fh == NULL)
            goto error;
	}
      else if (type == IMPORT_CMD && lex_match_id ("TYPE"))
	{
	  lex_match ('=');

	  if (lex_match_id ("COMM"))
	    type = PFM_COMM;
	  else if (lex_match_id ("TAPE"))
	    type = PFM_TAPE;
	  else
	    {
	      lex_error (_("expecting COMM or TAPE"));
              goto error;
	    }
	}
      else
        break; 
    }
  
  if (fh == NULL) 
    {
      lex_sbc_missing ("FILE");
      goto error;
    }
              
  discard_variables ();

  pgm = xmalloc (sizeof *pgm);
  pgm->reader = any_reader_open (fh, &dict);
  pgm->map = NULL;
  case_nullify (&pgm->bounce);
  if (pgm->reader == NULL)
    goto error;

  case_create (&pgm->bounce, dict_get_next_value_idx (dict));
  
  start_case_map (dict);

  while (token != '.')
    {
      lex_match ('/');
      if (!parse_dict_trim (dict))
        goto error;
    }

  pgm->map = finish_case_map (dict);
  
  dict_destroy (default_dict);
  default_dict = dict;

  proc_set_source (create_case_source (&case_reader_source_class, pgm));

  return CMD_SUCCESS;

 error:
  case_reader_pgm_free (pgm);
  if (dict != NULL)
    dict_destroy (dict);
  return CMD_CASCADING_FAILURE;
}

/* Frees a struct case_reader_pgm. */
static void
case_reader_pgm_free (struct case_reader_pgm *pgm) 
{
  if (pgm != NULL) 
    {
      any_reader_close (pgm->reader);
      destroy_case_map (pgm->map);
      case_destroy (&pgm->bounce);
      free (pgm);
    }
}

/* Clears internal state related to case reader input procedure. */
static void
case_reader_source_destroy (struct case_source *source)
{
  struct case_reader_pgm *pgm = source->aux;
  case_reader_pgm_free (pgm);
}

/* Reads all the cases from the data file into C and passes them
   to WRITE_CASE one by one, passing WC_DATA.
   Returns true if successful, false if an I/O error occurred. */
static bool
case_reader_source_read (struct case_source *source,
                         struct ccase *c,
                         write_case_func *write_case, write_case_data wc_data)
{
  struct case_reader_pgm *pgm = source->aux;
  bool ok = true;

  do
    {
      bool got_case;
      if (pgm->map == NULL)
        got_case = any_reader_read (pgm->reader, c);
      else
        {
          got_case = any_reader_read (pgm->reader, &pgm->bounce);
          if (got_case)
            map_case (pgm->map, &pgm->bounce, c);
        }
      if (!got_case)
        break;

      ok = write_case (wc_data);
    }
  while (ok);

  return ok && !any_reader_error (pgm->reader);
}

static const struct case_source_class case_reader_source_class =
  {
    "case reader",
    NULL,
    case_reader_source_read,
    case_reader_source_destroy,
  };

/* GET. */
int
cmd_get (void) 
{
  return parse_read_command (GET_CMD);
}

/* IMPORT. */
int
cmd_import (void) 
{
  return parse_read_command (IMPORT_CMD);
}

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

/* File writer plus a case map. */
struct case_writer
  {
    struct any_writer *writer;  /* File writer. */
    struct case_map *map;       /* Map to output file dictionary
                                   (null pointer for identity mapping). */
    struct ccase bounce;        /* Bounce buffer for mapping (if needed). */
  };

/* Destroys AW. */
static bool
case_writer_destroy (struct case_writer *aw)
{
  bool ok = true;
  if (aw != NULL) 
    {
      ok = any_writer_close (aw->writer);
      destroy_case_map (aw->map);
      case_destroy (&aw->bounce);
      free (aw);
    }
  return ok;
}

/* Parses SAVE or XSAVE or EXPORT or XEXPORT command.
   WRITER_TYPE identifies the type of file to write,
   and COMMAND_TYPE identifies the type of command.

   On success, returns a writer.
   For procedures only, sets *RETAIN_UNSELECTED to true if cases
   that would otherwise be excluded by FILTER or USE should be
   included.

   On failure, returns a null pointer. */
static struct case_writer *
parse_write_command (enum writer_type writer_type,
                     enum command_type command_type,
                     bool *retain_unselected)
{
  /* Common data. */
  struct file_handle *handle; /* Output file. */
  struct dictionary *dict;    /* Dictionary for output file. */
  struct case_writer *aw;      /* Writer. */  

  /* Common options. */
  bool print_map;             /* Print map?  TODO. */
  bool print_short_names;     /* Print long-to-short name map.  TODO. */
  struct sfm_write_options sysfile_opts;
  struct pfm_write_options porfile_opts;

  assert (writer_type == SYSFILE_WRITER || writer_type == PORFILE_WRITER);
  assert (command_type == XFORM_CMD || command_type == PROC_CMD);
  assert ((retain_unselected != NULL) == (command_type == PROC_CMD));

  if (command_type == PROC_CMD)
    *retain_unselected = true;

  handle = NULL;
  dict = dict_clone (default_dict);
  aw = xmalloc (sizeof *aw);
  aw->writer = NULL;
  aw->map = NULL;
  case_nullify (&aw->bounce);
  print_map = false;
  print_short_names = false;
  sysfile_opts = sfm_writer_default_options ();
  porfile_opts = pfm_writer_default_options ();

  start_case_map (dict);
  dict_delete_scratch_vars (dict);

  lex_match ('/');
  for (;;)
    {
      if (lex_match_id ("OUTFILE"))
	{
          if (handle != NULL) 
            {
              lex_sbc_only_once ("OUTFILE");
              goto error; 
            }
          
	  lex_match ('=');
      
	  handle = fh_parse (FH_REF_FILE | FH_REF_SCRATCH);
	  if (handle == NULL)
	    goto error;
	}
      else if (lex_match_id ("NAMES"))
        print_short_names = true;
      else if (lex_match_id ("PERMISSIONS")) 
        {
          bool cw;
          
          lex_match ('=');
          if (lex_match_id ("READONLY"))
            cw = false;
          else if (lex_match_id ("WRITEABLE"))
            cw = true;
          else
            {
              lex_error (_("expecting %s or %s"), "READONLY", "WRITEABLE");
              goto error;
            }
          sysfile_opts.create_writeable = porfile_opts.create_writeable = cw;
        }
      else if (command_type == PROC_CMD && lex_match_id ("UNSELECTED")) 
        {
          lex_match ('=');
          if (lex_match_id ("RETAIN"))
            *retain_unselected = true;
          else if (lex_match_id ("DELETE"))
            *retain_unselected = false;
          else
            {
              lex_error (_("expecting %s or %s"), "RETAIN", "DELETE");
              goto error;
            }
        }
      else if (writer_type == SYSFILE_WRITER && lex_match_id ("COMPRESSED"))
	sysfile_opts.compress = true;
      else if (writer_type == SYSFILE_WRITER && lex_match_id ("UNCOMPRESSED"))
	sysfile_opts.compress = false;
      else if (writer_type == SYSFILE_WRITER && lex_match_id ("VERSION"))
	{
	  lex_match ('=');
	  if (!lex_force_int ())
            goto error;
          sysfile_opts.version = lex_integer ();
          lex_get ();
	}
      else if (writer_type == PORFILE_WRITER && lex_match_id ("TYPE")) 
        {
          lex_match ('=');
          if (lex_match_id ("COMMUNICATIONS"))
            porfile_opts.type = PFM_COMM;
          else if (lex_match_id ("TAPE"))
            porfile_opts.type = PFM_TAPE;
          else
            {
              lex_error (_("expecting %s or %s"), "COMM", "TAPE");
              goto error;
            }
        }
      else if (writer_type == PORFILE_WRITER && lex_match_id ("DIGITS")) 
        {
          lex_match ('=');
          if (!lex_force_int ())
            goto error;
          porfile_opts.digits = lex_integer ();
          lex_get ();
        }
      else if (!parse_dict_trim (dict))
        goto error;
      
      if (!lex_match ('/'))
	break;
    }
  if (lex_end_of_command () != CMD_SUCCESS)
    goto error;

  if (handle == NULL) 
    {
      lex_sbc_missing ("OUTFILE");
      goto error;
    }

  dict_compact_values (dict);
  aw->map = finish_case_map (dict);
  if (aw->map != NULL)
    case_create (&aw->bounce, dict_get_next_value_idx (dict));

  if (fh_get_referent (handle) == FH_REF_FILE) 
    {
      switch (writer_type) 
        {
        case SYSFILE_WRITER:
          aw->writer = any_writer_from_sfm_writer (
            sfm_open_writer (handle, dict, sysfile_opts));
          break;
        case PORFILE_WRITER:
          aw->writer = any_writer_from_pfm_writer (
            pfm_open_writer (handle, dict, porfile_opts));
          break;
        }
    }
  else
    aw->writer = any_writer_open (handle, dict);
  if (aw->writer == NULL)
    goto error;
  dict_destroy (dict);
  
  return aw;

 error:
  case_writer_destroy (aw);
  dict_destroy (dict);
  return NULL;
}

/* Writes case C to writer AW. */
static bool
case_writer_write_case (struct case_writer *aw, const struct ccase *c) 
{
  if (aw->map != NULL) 
    {
      map_case (aw->map, c, &aw->bounce);
      c = &aw->bounce; 
    }
  return any_writer_write (aw->writer, c);
}

/* SAVE and EXPORT. */

static bool output_proc (const struct ccase *, void *);

/* Parses and performs the SAVE or EXPORT procedure. */
static int
parse_output_proc (enum writer_type writer_type)
{
  bool retain_unselected;
  struct variable *saved_filter_variable;
  struct case_writer *aw;
  bool ok;

  aw = parse_write_command (writer_type, PROC_CMD, &retain_unselected);
  if (aw == NULL) 
    return CMD_CASCADING_FAILURE;

  saved_filter_variable = dict_get_filter (default_dict);
  if (retain_unselected) 
    dict_set_filter (default_dict, NULL);
  ok = procedure (output_proc, aw);
  dict_set_filter (default_dict, saved_filter_variable);

  case_writer_destroy (aw);
  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

/* Writes case C to file. */
static bool
output_proc (const struct ccase *c, void *aw_) 
{
  struct case_writer *aw = aw_;
  return case_writer_write_case (aw, c);
}

int
cmd_save (void) 
{
  return parse_output_proc (SYSFILE_WRITER);
}

int
cmd_export (void) 
{
  return parse_output_proc (PORFILE_WRITER);
}

/* XSAVE and XEXPORT. */

/* Transformation. */
struct output_trns 
  {
    struct case_writer *aw;      /* Writer. */
  };

static trns_proc_func output_trns_proc;
static trns_free_func output_trns_free;

/* Parses the XSAVE or XEXPORT transformation command. */
static int
parse_output_trns (enum writer_type writer_type) 
{
  struct output_trns *t = xmalloc (sizeof *t);
  t->aw = parse_write_command (writer_type, XFORM_CMD, NULL);
  if (t->aw == NULL) 
    {
      free (t);
      return CMD_CASCADING_FAILURE;
    }

  add_transformation (output_trns_proc, output_trns_free, t);
  return CMD_SUCCESS;
}

/* Writes case C to the system file specified on XSAVE or XEXPORT. */
static int
output_trns_proc (void *trns_, struct ccase *c, int case_num UNUSED)
{
  struct output_trns *t = trns_;
  case_writer_write_case (t->aw, c);
  return TRNS_CONTINUE;
}

/* Frees an XSAVE or XEXPORT transformation.
   Returns true if successful, false if an I/O error occurred. */
static bool
output_trns_free (void *trns_)
{
  struct output_trns *t = trns_;
  bool ok = true;

  if (t != NULL)
    {
      ok = case_writer_destroy (t->aw);
      free (t);
    }
  return ok;
}

/* XSAVE command. */
int
cmd_xsave (void) 
{
  return parse_output_trns (SYSFILE_WRITER);
}

/* XEXPORT command. */
int
cmd_xexport (void) 
{
  return parse_output_trns (PORFILE_WRITER);
}

static bool rename_variables (struct dictionary *dict);
static bool drop_variables (struct dictionary *dict);
static bool keep_variables (struct dictionary *dict);

/* Commands that read and write system files share a great deal
   of common syntactic structure for rearranging and dropping
   variables.  This function parses this syntax and modifies DICT
   appropriately.  Returns true on success, false on failure. */
static bool
parse_dict_trim (struct dictionary *dict)
{
  if (lex_match_id ("MAP")) 
    {
      /* FIXME. */
      return true;
    }
  else if (lex_match_id ("DROP"))
    return drop_variables (dict);
  else if (lex_match_id ("KEEP"))
    return keep_variables (dict);
  else if (lex_match_id ("RENAME"))
    return rename_variables (dict);
  else
    {
      lex_error (_("expecting a valid subcommand"));
      return false;
    }
}

/* Parses and performs the RENAME subcommand of GET and SAVE. */
static bool
rename_variables (struct dictionary *dict)
{
  size_t i;

  int success = 0;

  struct variable **v;
  char **new_names;
  size_t nv, nn;
  char *err_name;

  int group;

  lex_match ('=');
  if (token != '(')
    {
      struct variable *v;

      v = parse_dict_variable (dict);
      if (v == NULL)
	return 0;
      if (!lex_force_match ('=')
	  || !lex_force_id ())
	return 0;
      if (dict_lookup_var (dict, tokid) != NULL)
	{
	  msg (SE, _("Cannot rename %s as %s because there already exists "
		     "a variable named %s.  To rename variables with "
		     "overlapping names, use a single RENAME subcommand "
		     "such as \"/RENAME (A=B)(B=C)(C=A)\", or equivalently, "
		     "\"/RENAME (A B C=B C A)\"."), v->name, tokid, tokid);
	  return 0;
	}
      
      dict_rename_var (dict, v, tokid);
      lex_get ();
      return 1;
    }

  nv = nn = 0;
  v = NULL;
  new_names = 0;
  group = 1;
  while (lex_match ('('))
    {
      size_t old_nv = nv;

      if (!parse_variables (dict, &v, &nv, PV_NO_DUPLICATE | PV_APPEND))
	goto done;
      if (!lex_match ('='))
	{
	  msg (SE, _("`=' expected after variable list."));
	  goto done;
	}
      if (!parse_DATA_LIST_vars (&new_names, &nn, PV_APPEND | PV_NO_SCRATCH))
	goto done;
      if (nn != nv)
	{
	  msg (SE, _("Number of variables on left side of `=' (%d) does not "
                     "match number of variables on right side (%d), in "
                     "parenthesized group %d of RENAME subcommand."),
	       (unsigned) (nv - old_nv), (unsigned) (nn - old_nv), group);
	  goto done;
	}
      if (!lex_force_match (')'))
	goto done;
      group++;
    }

  if (!dict_rename_vars (dict, v, new_names, nv, &err_name)) 
    {
      msg (SE, _("Requested renaming duplicates variable name %s."), err_name);
      goto done;
    }
  success = 1;

 done:
  for (i = 0; i < nn; i++)
    free (new_names[i]);
  free (new_names);
  free (v);

  return success;
}

/* Parses and performs the DROP subcommand of GET and SAVE.
   Returns true if successful, false on failure.*/
static bool
drop_variables (struct dictionary *dict)
{
  struct variable **v;
  size_t nv;

  lex_match ('=');
  if (!parse_variables (dict, &v, &nv, PV_NONE))
    return false;
  dict_delete_vars (dict, v, nv);
  free (v);

  if (dict_get_var_cnt (dict) == 0)
    {
      msg (SE, _("Cannot DROP all variables from dictionary."));
      return false;
    }
  return true;
}

/* Parses and performs the KEEP subcommand of GET and SAVE.
   Returns true if successful, false on failure.*/
static bool
keep_variables (struct dictionary *dict)
{
  struct variable **v;
  size_t nv;
  size_t i;

  lex_match ('=');
  if (!parse_variables (dict, &v, &nv, PV_NONE))
    return false;

  /* Move the specified variables to the beginning. */
  dict_reorder_vars (dict, v, nv);
          
  /* Delete the remaining variables. */
  v = xnrealloc (v, dict_get_var_cnt (dict) - nv, sizeof *v);
  for (i = nv; i < dict_get_var_cnt (dict); i++)
    v[i - nv] = dict_get_var (dict, i);
  dict_delete_vars (dict, v, dict_get_var_cnt (dict) - nv);
  free (v);

  return true;
}

/* MATCH FILES. */

/* File types. */
enum
  {
    MTF_FILE,			/* Specified on FILE= subcommand. */
    MTF_TABLE			/* Specified on TABLE= subcommand. */
  };

/* One of the files on MATCH FILES. */
struct mtf_file
  {
    struct mtf_file *next, *prev; /* Next, previous in the list of files. */
    struct mtf_file *next_min;	/* Next in the chain of minimums. */
    
    int type;			/* One of MTF_*. */
    struct variable **by;	/* List of BY variables for this file. */
    struct file_handle *handle; /* File handle. */
    struct any_reader *reader;  /* File reader. */
    struct dictionary *dict;	/* Dictionary from system file. */

    /* IN subcommand. */
    char *in_name;              /* Variable name. */
    struct variable *in_var;    /* Variable (in master dictionary). */

    struct ccase input;         /* Input record. */
  };

/* MATCH FILES procedure. */
struct mtf_proc 
  {
    struct mtf_file *head;      /* First file mentioned on FILE or TABLE. */
    struct mtf_file *tail;      /* Last file mentioned on FILE or TABLE. */

    bool ok;                    /* False if I/O error occurs. */

    size_t by_cnt;              /* Number of variables on BY subcommand. */

    /* Names of FIRST, LAST variables. */
    char first[LONG_NAME_LEN + 1], last[LONG_NAME_LEN + 1];
    
    struct dictionary *dict;    /* Dictionary of output file. */
    struct casefile *output;    /* MATCH FILES output. */
    struct ccase mtf_case;      /* Case used for output. */

    unsigned seq_num;           /* Have we initialized this variable? */
    unsigned *seq_nums;         /* Sequence numbers for each var in dict. */
  };

static bool mtf_free (struct mtf_proc *);
static bool mtf_close_file (struct mtf_file *);
static int mtf_merge_dictionary (struct dictionary *const, struct mtf_file *);
static bool mtf_delete_file_in_place (struct mtf_proc *, struct mtf_file **);

static bool mtf_read_nonactive_records (void *);
static bool mtf_processing_finish (void *);
static bool mtf_processing (const struct ccase *, void *);

static char *var_type_description (struct variable *);

static void set_master (struct variable *, struct variable *master);
static struct variable *get_master (struct variable *);

/* Parse and execute the MATCH FILES command. */
int
cmd_match_files (void)
{
  struct mtf_proc mtf;
  struct mtf_file *first_table = NULL;
  struct mtf_file *iter;
  
  bool used_active_file = false;
  bool saw_table = false;
  bool saw_in = false;

  bool ok;
  
  mtf.head = mtf.tail = NULL;
  mtf.by_cnt = 0;
  mtf.first[0] = '\0';
  mtf.last[0] = '\0';
  mtf.dict = dict_create ();
  mtf.output = NULL;
  case_nullify (&mtf.mtf_case);
  mtf.seq_num = 0;
  mtf.seq_nums = NULL;
  dict_set_case_limit (mtf.dict, dict_get_case_limit (default_dict));

  lex_match ('/');
  while (token == T_ID
         && (lex_id_match ("FILE", tokid) || lex_id_match ("TABLE", tokid)))
    {
      struct mtf_file *file = xmalloc (sizeof *file);

      if (lex_match_id ("FILE"))
        file->type = MTF_FILE;
      else if (lex_match_id ("TABLE"))
        {
          file->type = MTF_TABLE;
          saw_table = true;
        }
      else
        NOT_REACHED ();
      lex_match ('=');

      file->by = NULL;
      file->handle = NULL;
      file->reader = NULL;
      file->dict = NULL;
      file->in_name = NULL;
      file->in_var = NULL;
      case_nullify (&file->input);

      /* FILEs go first, then TABLEs. */
      if (file->type == MTF_TABLE || first_table == NULL)
        {
          file->next = NULL;
          file->prev = mtf.tail;
          if (mtf.tail)
            mtf.tail->next = file;
          mtf.tail = file;
          if (mtf.head == NULL)
            mtf.head = file;
          if (file->type == MTF_TABLE && first_table == NULL)
            first_table = file;
        }
      else 
        {
          assert (file->type == MTF_FILE);
          file->next = first_table;
          file->prev = first_table->prev;
          if (first_table->prev)
            first_table->prev->next = file;
          else
            mtf.head = file;
          first_table->prev = file;
        }

      if (lex_match ('*'))
        {
          file->handle = NULL;
          file->reader = NULL;
              
          if (used_active_file)
            {
              msg (SE, _("The active file may not be specified more "
                         "than once."));
              goto error;
            }
          used_active_file = true;

          if (!proc_has_source ())
            {
              msg (SE, _("Cannot specify the active file since no active "
                         "file has been defined."));
              goto error;
            }

          if (proc_make_temporary_transformations_permanent ())
            msg (SE,
                 _("MATCH FILES may not be used after TEMPORARY when "
                   "the active file is an input source.  "
                   "Temporary transformations will be made permanent."));

          file->dict = default_dict;
        }
      else
        {
          file->handle = fh_parse (FH_REF_FILE | FH_REF_SCRATCH);
          if (file->handle == NULL)
            goto error;

          file->reader = any_reader_open (file->handle, &file->dict);
          if (file->reader == NULL)
            goto error;

          case_create (&file->input, dict_get_next_value_idx (file->dict));
        }

      while (lex_match ('/'))
        if (lex_match_id ("RENAME")) 
          {
            if (!rename_variables (file->dict))
              goto error; 
          }
        else if (lex_match_id ("IN"))
          {
            lex_match ('=');
            if (token != T_ID)
              {
                lex_error (NULL);
                goto error;
              }

            if (file->in_name != NULL)
              {
                msg (SE, _("Multiple IN subcommands for a single FILE or "
                           "TABLE."));
                goto error;
              }
            file->in_name = xstrdup (tokid);
            lex_get ();
            saw_in = true;
          }

      mtf_merge_dictionary (mtf.dict, file);
    }
  
  while (token != '.')
    {
      if (lex_match (T_BY))
	{
          struct variable **by;
          
	  if (mtf.by_cnt)
	    {
	      msg (SE, _("BY may appear at most once."));
	      goto error;
	    }
	      
	  lex_match ('=');
	  if (!parse_variables (mtf.dict, &by, &mtf.by_cnt,
				PV_NO_DUPLICATE | PV_NO_SCRATCH))
	    goto error;

          for (iter = mtf.head; iter != NULL; iter = iter->next)
            {
              size_t i;
	  
              iter->by = xnmalloc (mtf.by_cnt, sizeof *iter->by);

              for (i = 0; i < mtf.by_cnt; i++)
                {
                  iter->by[i] = dict_lookup_var (iter->dict, by[i]->name);
                  if (iter->by[i] == NULL)
                    {
                      msg (SE, _("File %s lacks BY variable %s."),
                           iter->handle ? fh_get_name (iter->handle) : "*",
                           by[i]->name);
                      free (by);
                      goto error;
                    }
                }
            }
          free (by);
	}
      else if (lex_match_id ("FIRST")) 
        {
          if (mtf.first[0] != '\0')
            {
              msg (SE, _("FIRST may appear at most once."));
              goto error;
            }
	      
	  lex_match ('=');
          if (!lex_force_id ())
            goto error;
          strcpy (mtf.first, tokid);
          lex_get ();
        }
      else if (lex_match_id ("LAST")) 
        {
          if (mtf.last[0] != '\0')
            {
              msg (SE, _("LAST may appear at most once."));
              goto error;
            }
	      
	  lex_match ('=');
          if (!lex_force_id ())
            goto error;
          strcpy (mtf.last, tokid);
          lex_get ();
        }
      else if (lex_match_id ("MAP"))
	{
	  /* FIXME. */
	}
      else if (lex_match_id ("DROP")) 
        {
          if (!drop_variables (mtf.dict))
            goto error;
        }
      else if (lex_match_id ("KEEP")) 
        {
          if (!keep_variables (mtf.dict))
            goto error;
        }
      else
	{
	  lex_error (NULL);
	  goto error;
	}

      if (!lex_match ('/') && token != '.') 
        {
          lex_end_of_command ();
          goto error;
        }
    }

  if (mtf.by_cnt == 0)
    {
      if (saw_table)
        {
          msg (SE, _("BY is required when TABLE is specified."));
          goto error;
        }
      if (saw_in)
        {
          msg (SE, _("BY is required when IN is specified."));
          goto error;
        }
    }

  /* Set up mapping from each file's variables to master
     variables. */
  for (iter = mtf.head; iter != NULL; iter = iter->next)
    {
      struct dictionary *d = iter->dict;
      int i;

      for (i = 0; i < dict_get_var_cnt (d); i++)
        {
          struct variable *v = dict_get_var (d, i);
          struct variable *mv = dict_lookup_var (mtf.dict, v->name);
          if (mv != NULL)
            set_master (v, mv);
        }
    }

  /* Add IN variables to master dictionary. */
  for (iter = mtf.head; iter != NULL; iter = iter->next) 
    if (iter->in_name != NULL)
      {
        iter->in_var = dict_create_var (mtf.dict, iter->in_name, 0);
        if (iter->in_var == NULL)
          {
            msg (SE, _("IN variable name %s duplicates an "
                       "existing variable name."),
                 iter->in_var->name);
            goto error;
          }
        iter->in_var->print = iter->in_var->write
          = make_output_format (FMT_F, 1, 0);
      }
    
  /* MATCH FILES performs an n-way merge on all its input files.
     Abstract algorithm:

     1. Read one input record from every input FILE.

     2. If no FILEs are left, stop.  Otherwise, proceed to step 3.

     3. Find the FILE input record(s) that have minimum BY
     values.  Store all the values from these input records into
     the output record.

     4. For every TABLE, read another record as long as the BY values
     on the TABLE's input record are less than the FILEs' BY values.
     If an exact match is found, store all the values from the TABLE
     input record into the output record.

     5. Write the output record.

     6. Read another record from each input file FILE and TABLE that
     we stored values from above.  If we come to the end of one of the
     input files, remove it from the list of input files.

     7. Repeat from step 2.

     Unfortunately, this algorithm can't be implemented in a
     straightforward way because there's no function to read a
     record from the active file.  Instead, it has to be written
     as a state machine.

     FIXME: For merging large numbers of files (more than 10?) a
     better algorithm would use a heap for finding minimum
     values. */

  if (!used_active_file)
    discard_variables ();

  dict_compact_values (mtf.dict);
  mtf.output = fastfile_create (dict_get_next_value_idx (mtf.dict));
  mtf.seq_nums = xcalloc (dict_get_var_cnt (mtf.dict), sizeof *mtf.seq_nums);
  case_create (&mtf.mtf_case, dict_get_next_value_idx (mtf.dict));

  if (!mtf_read_nonactive_records (&mtf))
    goto error;

  if (used_active_file) 
    {
      proc_set_sink (create_case_sink (&null_sink_class, default_dict, NULL));
      ok = procedure (mtf_processing, &mtf) && mtf_processing_finish (&mtf); 
    }
  else
    ok = mtf_processing_finish (&mtf);

  discard_variables ();

  dict_destroy (default_dict);
  default_dict = mtf.dict;
  mtf.dict = NULL;
  proc_set_source (storage_source_create (mtf.output));
  mtf.output = NULL;
  
  if (!mtf_free (&mtf))
    ok = false;
  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
  
 error:
  mtf_free (&mtf);
  return CMD_CASCADING_FAILURE;
}

/* Repeats 2...7 an arbitrary number of times. */
static bool
mtf_processing_finish (void *mtf_)
{
  struct mtf_proc *mtf = mtf_;
  struct mtf_file *iter;

  /* Find the active file and delete it. */
  for (iter = mtf->head; iter; iter = iter->next)
    if (iter->handle == NULL)
      {
        if (!mtf_delete_file_in_place (mtf, &iter))
          NOT_REACHED ();
        break;
      }
  
  while (mtf->head && mtf->head->type == MTF_FILE)
    if (!mtf_processing (NULL, mtf))
      return false;

  return true;
}

/* Return a string in a static buffer describing V's variable type and
   width. */
static char *
var_type_description (struct variable *v)
{
  static char buf[2][32];
  static int x = 0;
  char *s;

  x ^= 1;
  s = buf[x];

  if (v->type == NUMERIC)
    strcpy (s, "numeric");
  else
    {
      assert (v->type == ALPHA);
      sprintf (s, "string with width %d", v->width);
    }
  return s;
}

/* Closes FILE and frees its associated data.
   Returns true if successful, false if an I/O error
   occurred on FILE. */
static bool
mtf_close_file (struct mtf_file *file)
{
  bool ok = file->reader == NULL || !any_reader_error (file->reader);
  free (file->by);
  any_reader_close (file->reader);
  if (file->handle != NULL)
    dict_destroy (file->dict);
  case_destroy (&file->input);
  free (file->in_name);
  free (file);
  return ok;
}

/* Free all the data for the MATCH FILES procedure.
   Returns true if successful, false if an I/O error
   occurred. */
static bool
mtf_free (struct mtf_proc *mtf)
{
  struct mtf_file *iter, *next;
  bool ok = true;

  for (iter = mtf->head; iter; iter = next)
    {
      next = iter->next;
      assert (iter->dict != mtf->dict);
      if (!mtf_close_file (iter))
        ok = false;
    }
  
  if (mtf->dict)
    dict_destroy (mtf->dict);
  case_destroy (&mtf->mtf_case);
  free (mtf->seq_nums);

  return ok;
}

/* Remove *FILE from the mtf_file chain.  Make *FILE point to the next
   file in the chain, or to NULL if was the last in the chain.
   Returns true if successful, false if an I/O error occurred. */
static bool
mtf_delete_file_in_place (struct mtf_proc *mtf, struct mtf_file **file)
{
  struct mtf_file *f = *file;
  int i;

  if (f->prev)
    f->prev->next = f->next;
  if (f->next)
    f->next->prev = f->prev;
  if (f == mtf->head)
    mtf->head = f->next;
  if (f == mtf->tail)
    mtf->tail = f->prev;
  *file = f->next;

  if (f->in_var != NULL)
    case_data_rw (&mtf->mtf_case, f->in_var->fv)->f = 0.;
  for (i = 0; i < dict_get_var_cnt (f->dict); i++)
    {
      struct variable *v = dict_get_var (f->dict, i);
      struct variable *mv = get_master (v);
      if (mv != NULL) 
        {
          union value *out = case_data_rw (&mtf->mtf_case, mv->fv);
	  
          if (v->type == NUMERIC)
            out->f = SYSMIS;
          else
            memset (out->s, ' ', v->width);
        } 
    }

  return mtf_close_file (f);
}

/* Read a record from every input file except the active file.
   Returns true if successful, false if an I/O error occurred. */
static bool
mtf_read_nonactive_records (void *mtf_)
{
  struct mtf_proc *mtf = mtf_;
  struct mtf_file *iter, *next;
  bool ok = true;

  for (iter = mtf->head; ok && iter != NULL; iter = next)
    {
      next = iter->next;
      if (iter->handle && !any_reader_read (iter->reader, &iter->input)) 
        if (!mtf_delete_file_in_place (mtf, &iter))
          ok = false;
    }
  return ok;
}

/* Compare the BY variables for files A and B; return -1 if A < B, 0
   if A == B, 1 if A > B. */
static inline int
mtf_compare_BY_values (struct mtf_proc *mtf,
                       struct mtf_file *a, struct mtf_file *b,
                       const struct ccase *c)
{
  const struct ccase *ca = case_is_null (&a->input) ? c : &a->input;
  const struct ccase *cb = case_is_null (&b->input) ? c : &b->input;
  assert ((a == NULL) + (b == NULL) + (c == NULL) <= 1);
  return case_compare_2dict (ca, cb, a->by, b->by, mtf->by_cnt);
}

/* Perform one iteration of steps 3...7 above.
   Returns true if successful, false if an I/O error occurred. */
static bool
mtf_processing (const struct ccase *c, void *mtf_)
{
  struct mtf_proc *mtf = mtf_;

  /* Do we need another record from the active file? */
  bool read_active_file;

  assert (mtf->head != NULL);
  if (mtf->head->type == MTF_TABLE)
    return true;
  
  do
    {
      struct mtf_file *min_head, *min_tail; /* Files with minimum BY values. */
      struct mtf_file *max_head, *max_tail; /* Files with non-minimum BYs. */
      struct mtf_file *iter, *next;

      read_active_file = false;
      
      /* 3. Find the FILE input record(s) that have minimum BY
         values.  Store all the values from these input records into
         the output record. */
      min_head = min_tail = mtf->head;
      max_head = max_tail = NULL;
      for (iter = mtf->head->next; iter && iter->type == MTF_FILE;
	   iter = iter->next) 
        {
          int cmp = mtf_compare_BY_values (mtf, min_head, iter, c);
          if (cmp < 0) 
            {
              if (max_head)
                max_tail = max_tail->next_min = iter;
              else
                max_head = max_tail = iter;
            }
          else if (cmp == 0) 
	    min_tail = min_tail->next_min = iter;
          else /* cmp > 0 */
            {
              if (max_head)
                {
                  max_tail->next_min = min_head;
                  max_tail = min_tail;
                }
              else
                {
                  max_head = min_head;
                  max_tail = min_tail;
                }
              min_head = min_tail = iter;
            }
        }
      
      /* 4. For every TABLE, read another record as long as the BY
	 values on the TABLE's input record are less than the FILEs'
	 BY values.  If an exact match is found, store all the values
	 from the TABLE input record into the output record. */
      for (; iter != NULL; iter = next)
	{
	  assert (iter->type == MTF_TABLE);
      
	  next = iter->next;
          for (;;) 
            {
              int cmp = mtf_compare_BY_values (mtf, min_head, iter, c);
              if (cmp < 0) 
                {
                  if (max_head)
                    max_tail = max_tail->next_min = iter;
                  else
                    max_head = max_tail = iter;
                }
              else if (cmp == 0)
                min_tail = min_tail->next_min = iter;
              else /* cmp > 0 */
                {
                  if (iter->handle == NULL)
                    return true;
                  if (any_reader_read (iter->reader, &iter->input))
                    continue;
                  if (!mtf_delete_file_in_place (mtf, &iter))
                    return false;
                }
              break;
            }
	}

      /* Next sequence number. */
      mtf->seq_num++;

      /* Store data to all the records we are using. */
      if (min_tail)
	min_tail->next_min = NULL;
      for (iter = min_head; iter; iter = iter->next_min)
	{
	  int i;

	  for (i = 0; i < dict_get_var_cnt (iter->dict); i++)
	    {
	      struct variable *v = dict_get_var (iter->dict, i);
              struct variable *mv = get_master (v);
	  
	      if (mv != NULL && mtf->seq_nums[mv->index] != mtf->seq_num) 
                {
                  const struct ccase *record
                    = case_is_null (&iter->input) ? c : &iter->input;
                  union value *out = case_data_rw (&mtf->mtf_case, mv->fv);

                  mtf->seq_nums[mv->index] = mtf->seq_num;
                  if (v->type == NUMERIC)
                    out->f = case_num (record, v->fv);
                  else
                    memcpy (out->s, case_str (record, v->fv), v->width);
                } 
            }
          if (iter->in_var != NULL)
            case_data_rw (&mtf->mtf_case, iter->in_var->fv)->f = 1.;

          if (iter->type == MTF_FILE && iter->handle == NULL)
            read_active_file = true;
	}

      /* Store missing values to all the records we're not
         using. */
      if (max_tail)
	max_tail->next_min = NULL;
      for (iter = max_head; iter; iter = iter->next_min)
	{
	  int i;

	  for (i = 0; i < dict_get_var_cnt (iter->dict); i++)
	    {
	      struct variable *v = dict_get_var (iter->dict, i);
              struct variable *mv = get_master (v);

	      if (mv != NULL && mtf->seq_nums[mv->index] != mtf->seq_num) 
                {
                  union value *out = case_data_rw (&mtf->mtf_case, mv->fv);
                  mtf->seq_nums[mv->index] = mtf->seq_num;

                  if (v->type == NUMERIC)
                    out->f = SYSMIS;
                  else
                    memset (out->s, ' ', v->width);
                }
            }
          if (iter->in_var != NULL)
            case_data_rw (&mtf->mtf_case, iter->in_var->fv)->f = 0.;
	}

      /* 5. Write the output record. */
      casefile_append (mtf->output, &mtf->mtf_case);

      /* 6. Read another record from each input file FILE and TABLE
	 that we stored values from above.  If we come to the end of
	 one of the input files, remove it from the list of input
	 files. */
      for (iter = min_head; iter && iter->type == MTF_FILE; iter = next)
	{
	  next = iter->next_min;
	  if (iter->reader != NULL
              && !any_reader_read (iter->reader, &iter->input))
            if (!mtf_delete_file_in_place (mtf, &iter))
              return false;
	}
    }
  while (!read_active_file
         && mtf->head != NULL && mtf->head->type == MTF_FILE);

  return true;
}

/* Merge the dictionary for file F into master dictionary M. */
static int
mtf_merge_dictionary (struct dictionary *const m, struct mtf_file *f)
{
  struct dictionary *d = f->dict;
  const char *d_docs, *m_docs;
  int i;

  if (dict_get_label (m) == NULL)
    dict_set_label (m, dict_get_label (d));

  d_docs = dict_get_documents (d);
  m_docs = dict_get_documents (m);
  if (d_docs != NULL) 
    {
      if (m_docs == NULL)
        dict_set_documents (m, d_docs);
      else
        {
          char *new_docs;
          size_t new_len;

          new_len = strlen (m_docs) + strlen (d_docs);
          new_docs = xmalloc (new_len + 1);
          strcpy (new_docs, m_docs);
          strcat (new_docs, d_docs);
          dict_set_documents (m, new_docs);
          free (new_docs);
        }
    }
  
  for (i = 0; i < dict_get_var_cnt (d); i++)
    {
      struct variable *dv = dict_get_var (d, i);
      struct variable *mv = dict_lookup_var (m, dv->name);

      if (dict_class_from_id (dv->name) == DC_SCRATCH)
        continue;

      if (mv != NULL)
        {
          if (mv->width != dv->width) 
            {
              msg (SE, _("Variable %s in file %s (%s) has different "
                         "type or width from the same variable in "
                         "earlier file (%s)."),
                   dv->name, fh_get_name (f->handle),
                   var_type_description (dv), var_type_description (mv));
              return 0;
            }
        
          if (dv->width == mv->width)
            {
              if (val_labs_count (dv->val_labs)
                  && !val_labs_count (mv->val_labs)) 
                {
                  val_labs_destroy (mv->val_labs);
                  mv->val_labs = val_labs_copy (dv->val_labs); 
                }
              if (!mv_is_empty (&dv->miss) && mv_is_empty (&mv->miss))
                mv_copy (&mv->miss, &dv->miss);
            }

          if (dv->label && !mv->label)
            mv->label = xstrdup (dv->label);
        }
      else
        mv = dict_clone_var_assert (m, dv, dv->name);
    }

  return 1;
}

/* Marks V's master variable as MASTER. */
static void
set_master (struct variable *v, struct variable *master) 
{
  var_attach_aux (v, master, NULL);
}

/* Returns the master variable corresponding to V,
   as set with set_master(). */
static struct variable *
get_master (struct variable *v) 
{
  return v->aux;
}



/* Case map.

   A case map copies data from a case that corresponds for one
   dictionary to a case that corresponds to a second dictionary
   derived from the first by, optionally, deleting, reordering,
   or renaming variables.  (No new variables may be created.)
   */

/* A case map. */
struct case_map
  {
    size_t value_cnt;   /* Number of values in map. */
    int *map;           /* For each destination index, the
                           corresponding source index. */
  };

/* Prepares dictionary D for producing a case map.  Afterward,
   the caller may delete, reorder, or rename variables within D
   at will before using finish_case_map() to produce the case
   map.

   Uses D's aux members, which must otherwise not be in use. */
static void
start_case_map (struct dictionary *d) 
{
  size_t var_cnt = dict_get_var_cnt (d);
  size_t i;
  
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      int *src_fv = xmalloc (sizeof *src_fv);
      *src_fv = v->fv;
      var_attach_aux (v, src_fv, var_dtor_free);
    }
}

/* Produces a case map from dictionary D, which must have been
   previously prepared with start_case_map().

   Does not retain any reference to D, and clears the aux members
   set up by start_case_map().

   Returns the new case map, or a null pointer if no mapping is
   required (that is, no data has changed position). */
static struct case_map *
finish_case_map (struct dictionary *d) 
{
  struct case_map *map;
  size_t var_cnt = dict_get_var_cnt (d);
  size_t i;
  int identity_map;

  map = xmalloc (sizeof *map);
  map->value_cnt = dict_get_next_value_idx (d);
  map->map = xnmalloc (map->value_cnt, sizeof *map->map);
  for (i = 0; i < map->value_cnt; i++)
    map->map[i] = -1;

  identity_map = 1;
  for (i = 0; i < var_cnt; i++) 
    {
      struct variable *v = dict_get_var (d, i);
      int *src_fv = (int *) var_detach_aux (v);
      size_t idx;

      if (v->fv != *src_fv)
        identity_map = 0;
      
      for (idx = 0; idx < v->nv; idx++)
        {
          int src_idx = *src_fv + idx;
          int dst_idx = v->fv + idx;
          
          assert (map->map[dst_idx] == -1);
          map->map[dst_idx] = src_idx;
        }
      free (src_fv);
    }

  if (identity_map) 
    {
      destroy_case_map (map);
      return NULL;
    }

  while (map->value_cnt > 0 && map->map[map->value_cnt - 1] == -1)
    map->value_cnt--;

  return map;
}

/* Maps from SRC to DST, applying case map MAP. */
static void
map_case (const struct case_map *map,
          const struct ccase *src, struct ccase *dst) 
{
  size_t dst_idx;

  assert (map != NULL);
  assert (src != NULL);
  assert (dst != NULL);
  assert (src != dst);

  for (dst_idx = 0; dst_idx < map->value_cnt; dst_idx++)
    {
      int src_idx = map->map[dst_idx];
      if (src_idx != -1)
        *case_data_rw (dst, dst_idx) = *case_data (src, src_idx);
    }
}

/* Destroys case map MAP. */
static void
destroy_case_map (struct case_map *map) 
{
  if (map != NULL) 
    {
      free (map->map);
      free (map);
    }
}
