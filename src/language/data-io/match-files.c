/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2008 Free Software Foundation, Inc.

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

#include <data/any-reader.h>
#include <data/case-matcher.h>
#include <data/case.h>
#include <data/casereader.h>
#include <data/casewriter.h>
#include <data/dictionary.h>
#include <data/format.h>
#include <data/procedure.h>
#include <data/subcase.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/file-handle.h>
#include <language/data-io/trim.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <language/stats/sort-criteria.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/taint.h>
#include <math/sort.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

enum command_type
  {
    ADD_FILES,
    MATCH_FILES,
    UPDATE
  };

/* File types. */
enum mtf_type
  {
    MTF_FILE,			/* Specified on FILE= subcommand. */
    MTF_TABLE			/* Specified on TABLE= subcommand. */
  };

/* One FILE or TABLE subcommand. */
struct mtf_file
  {
    enum mtf_type type;
    struct casereader *reader;
    struct subcase by;
    int idx;
    struct mtf_variable *vars;  /* Variables to copy to output. */
    size_t var_cnt;             /* Number of other variables. */
    bool is_sorted;             /* Is presorted on the BY variables? */

    struct file_handle *handle; /* Input file handle. */
    struct dictionary *dict;	/* Input file dictionary. */

    /* Used by TABLE. */
    struct ccase c;

    char in_name[VAR_NAME_LEN + 1];
    struct variable *in_var;
  };

struct mtf_variable
  {
    struct variable *in_var;
    struct variable *out_var;
  };

struct mtf_proc
  {
    struct mtf_file **files;    /* All the files being merged. */
    size_t n_files;             /* Number of files. */

    struct dictionary *dict;    /* Dictionary of output file. */
    struct casewriter *output;  /* Destination for output. */

    struct case_matcher *matcher;
    struct subcase by;

    /* FIRST, LAST.
       Only if "first" or "last" is nonnull are the remaining
       members used. */
    struct variable *first;     /* Variable specified on FIRST (if any). */
    struct variable *last;      /* Variable specified on LAST (if any). */
    struct ccase buffered_case; /* Case ready for output except that we don't
                                   know the value for the LAST variable yet. */
    union value *prev_BY;       /* Values of BY vars in buffered_case. */
  };

static int combine_files (enum command_type, struct lexer *, struct dataset *);
static void mtf_free (struct mtf_proc *);

static bool mtf_close_all_files (struct mtf_proc *);
static bool mtf_merge_dictionary (struct dictionary *const, struct mtf_file *);

static void process_update (struct mtf_proc *);
static void process_match_files (struct mtf_proc *);
static void process_add_files (struct mtf_proc *);

static bool create_flag_var (const char *subcommand_name, const char *var_name,
                             struct dictionary *, struct variable **);
static char *var_type_description (struct variable *);
static void output_case (struct mtf_proc *, struct ccase *, union value *by);
static void output_buffered_case (struct mtf_proc *);

int
cmd_add_files (struct lexer *lexer, struct dataset *ds)
{
  return combine_files (ADD_FILES, lexer, ds);
}

int
cmd_match_files (struct lexer *lexer, struct dataset *ds)
{
  return combine_files (MATCH_FILES, lexer, ds);
}

int
cmd_update (struct lexer *lexer, struct dataset *ds)
{
  return combine_files (UPDATE, lexer, ds);
}

static int
combine_files (enum command_type command,
               struct lexer *lexer, struct dataset *ds)
{
  struct mtf_proc mtf;

  bool saw_by = false;
  bool saw_sort = false;
  struct casereader *active_file = NULL;

  char first_name[VAR_NAME_LEN + 1] = "";
  char last_name[VAR_NAME_LEN + 1] = "";

  struct taint *taint = NULL;

  size_t n_files = 0;
  size_t n_tables = 0;
  size_t allocated_files = 0;

  size_t i;

  mtf.files = NULL;
  mtf.n_files = 0;
  mtf.dict = dict_create ();
  mtf.output = NULL;
  mtf.matcher = NULL;
  subcase_init_empty (&mtf.by);
  mtf.first = NULL;
  mtf.last = NULL;
  case_nullify (&mtf.buffered_case);
  mtf.prev_BY = NULL;

  dict_set_case_limit (mtf.dict, dict_get_case_limit (dataset_dict (ds)));

  lex_match (lexer, '/');
  for (;;)
    {
      struct mtf_file *file;
      enum mtf_type type;

      if (lex_match_id (lexer, "FILE")) 
        type = MTF_FILE;
      else if (command == MATCH_FILES && lex_match_id (lexer, "TABLE")) 
        type = MTF_TABLE;
      else
        break;
      lex_match (lexer, '=');

      if (mtf.n_files >= allocated_files)
        mtf.files = x2nrealloc (mtf.files, &allocated_files,
                                sizeof *mtf.files);
      mtf.files[mtf.n_files++] = file = xmalloc (sizeof *file);
      file->type = type;
      file->reader = NULL;
      subcase_init_empty (&file->by);
      file->idx = type == MTF_FILE ? n_files++ : n_tables++;
      file->vars = NULL;
      file->var_cnt = 0;
      file->is_sorted = true;
      file->handle = NULL;
      file->dict = NULL;
      case_nullify (&file->c);
      file->in_name[0] = '\0';
      file->in_var = NULL;

      if (lex_match (lexer, '*'))
        {
          if (!proc_has_active_file (ds))
            {
              msg (SE, _("Cannot specify the active file since no active "
                         "file has been defined."));
              goto error;
            }

          if (proc_make_temporary_transformations_permanent (ds))
            msg (SE,
                 _("This command may not be used after TEMPORARY when "
                   "the active file is an input source.  "
                   "Temporary transformations will be made permanent."));

          file->dict = dict_clone (dataset_dict (ds));
        }
      else
        {
          file->handle = fh_parse (lexer, FH_REF_FILE | FH_REF_SCRATCH);
          if (file->handle == NULL)
            goto error;

          file->reader = any_reader_open (file->handle, &file->dict);
          if (file->reader == NULL)
            goto error;
        }

      while (lex_match (lexer, '/'))
        if (lex_match_id (lexer, "RENAME"))
          {
            if (!parse_dict_rename (lexer, file->dict))
              goto error;
          }
        else if (lex_match_id (lexer, "IN"))
          {
            lex_match (lexer, '=');
            if (lex_token (lexer) != T_ID)
              {
                lex_error (lexer, NULL);
                goto error;
              }

            if (file->in_name[0])
              {
                msg (SE, _("Multiple IN subcommands for a single FILE or "
                           "TABLE."));
                goto error;
              }
            strcpy (file->in_name, lex_tokid (lexer));
            lex_get (lexer);
          }
        else if (lex_match_id (lexer, "SORT"))
          {
            file->is_sorted = false;
            saw_sort = true;
          }

      mtf_merge_dictionary (mtf.dict, file);
    }

  while (lex_token (lexer) != '.')
    {
      if (lex_match (lexer, T_BY))
	{
          const struct variable **by_vars;
          size_t i;
          bool ok;

	  if (saw_by)
	    {
              lex_sbc_only_once ("BY");
	      goto error;
	    }
          saw_by = true;

	  lex_match (lexer, '=');
          if (!parse_sort_criteria (lexer, mtf.dict, &mtf.by, &by_vars, NULL))
	    goto error;

          ok = true;
          for (i = 0; i < mtf.n_files; i++)
            {
              struct mtf_file *file = mtf.files[i];
              size_t j;

              for (j = 0; j < subcase_get_n_values (&mtf.by); j++)
                {
                  const char *name = var_get_name (by_vars[j]);
                  struct variable *var = dict_lookup_var (file->dict, name);
                  if (var != NULL)
                    subcase_add_var (&file->by, var,
                                     subcase_get_direction (&mtf.by, j));
                  else
                    {
                      if (file->handle != NULL)
                        msg (SE, _("File %s lacks BY variable %s."),
                             fh_get_name (file->handle), name);
                      else
                        msg (SE, _("Active file lacks BY variable %s."), name);
                      ok = false;
                    }
                }
              assert (!ok || subcase_conformable (&file->by,
                                                  &mtf.files[0]->by));
            }
          free (by_vars);

          if (!ok)
            goto error;
	}
      else if (command != UPDATE && lex_match_id (lexer, "FIRST"))
        {
          if (first_name[0] != '\0')
            {
              lex_sbc_only_once ("FIRST");
              goto error;
            }

	  lex_match (lexer, '=');
          if (!lex_force_id (lexer))
            goto error;
          strcpy (first_name, lex_tokid (lexer));
          lex_get (lexer);
        }
      else if (command != UPDATE && lex_match_id (lexer, "LAST"))
        {
          if (last_name[0] != '\0')
            {
              lex_sbc_only_once ("LAST");
              goto error;
            }

	  lex_match (lexer, '=');
          if (!lex_force_id (lexer))
            goto error;
          strcpy (last_name, lex_tokid (lexer));
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "MAP"))
	{
	  /* FIXME. */
	}
      else if (lex_match_id (lexer, "DROP"))
        {
          if (!parse_dict_drop (lexer, mtf.dict))
            goto error;
        }
      else if (lex_match_id (lexer, "KEEP"))
        {
          if (!parse_dict_keep (lexer, mtf.dict))
            goto error;
        }
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}

      if (!lex_match (lexer, '/') && lex_token (lexer) != '.')
        {
          lex_end_of_command (lexer);
          goto error;
        }
    }

  if (!saw_by)
    {
      if (command == UPDATE) 
        {
          msg (SE, _("The BY subcommand is required."));
          goto error;
        }
      if (n_tables)
        {
          msg (SE, _("BY is required when TABLE is specified."));
          goto error;
        }
      if (saw_sort)
        {
          msg (SE, _("BY is required when SORT is specified."));
          goto error;
        }
    }

  /* Set up mapping from each file's variables to master
     variables. */
  for (i = 0; i < mtf.n_files; i++)
    {
      struct mtf_file *file = mtf.files[i];
      size_t in_var_cnt = dict_get_var_cnt (file->dict);
      size_t j;

      file->vars = xnmalloc (in_var_cnt, sizeof *file->vars);
      file->var_cnt = 0;
      for (j = 0; j < in_var_cnt; j++)
        {
          struct variable *in_var = dict_get_var (file->dict, j);
          struct variable *out_var = dict_lookup_var (mtf.dict,
                                                      var_get_name (in_var));

          if (out_var != NULL)
            {
              struct mtf_variable *mv = &file->vars[file->var_cnt++];
              mv->in_var = in_var;
              mv->out_var = out_var;
            }
        }
    }

  /* Add IN, FIRST, and LAST variables to master dictionary. */
  for (i = 0; i < mtf.n_files; i++)
    {
      struct mtf_file *file = mtf.files[i];
      if (!create_flag_var ("IN", file->in_name, mtf.dict, &file->in_var))
        goto error; 
    }
  if (!create_flag_var ("FIRST", first_name, mtf.dict, &mtf.first)
      || !create_flag_var ("LAST", last_name, mtf.dict, &mtf.last))
    goto error;

  dict_delete_scratch_vars (mtf.dict);
  dict_compact_values (mtf.dict);
  mtf.output = autopaging_writer_create (dict_get_next_value_idx (mtf.dict));
  taint = taint_clone (casewriter_get_taint (mtf.output));

  mtf.matcher = case_matcher_create ();
  taint_propagate (case_matcher_get_taint (mtf.matcher), taint);
  for (i = 0; i < mtf.n_files; i++)
    {
      struct mtf_file *file = mtf.files[i];
      if (file->reader == NULL)
        {
          if (active_file == NULL)
            {
              proc_discard_output (ds);
              file->reader = active_file = proc_open (ds);
            }
          else
            file->reader = casereader_clone (active_file);
        }
      if (!file->is_sorted) 
        file->reader = sort_execute (file->reader, &file->by);
      if (file->type == MTF_FILE)
        case_matcher_add_input (mtf.matcher, file->reader, &file->by);
      else 
        {
          casereader_read (file->reader, &file->c);
          taint_propagate (casereader_get_taint (file->reader), taint);
        }
    }

  if (command == ADD_FILES)
    process_add_files (&mtf);
  else if (command == MATCH_FILES)
    process_match_files (&mtf);
  else if (command == UPDATE)
    process_update (&mtf);
  else
    NOT_REACHED ();

  case_matcher_destroy (mtf.matcher);
  mtf_close_all_files (&mtf);
  if (active_file != NULL)
    proc_commit (ds);

  proc_set_active_file (ds, casewriter_make_reader (mtf.output), mtf.dict);
  mtf.dict = NULL;
  mtf.output = NULL;

  mtf_free (&mtf);

  return taint_destroy (taint) ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

 error:
  if (active_file != NULL)
    proc_commit (ds);
  mtf_free (&mtf);
  taint_destroy (taint);
  return CMD_CASCADING_FAILURE;
}

/* If VAR_NAME is a non-empty string, attempts to create a
   variable named VAR_NAME, with format F1.0, in DICT, and stores
   a pointer to the variable in *VAR.  Returns true if
   successful, false if the variable name is a duplicate (in
   which case a message saying that the variable specified on the
   given SUBCOMMAND is a duplicate is emitted).  Also returns
   true, without doing anything, if VAR_NAME is null or empty. */
static bool
create_flag_var (const char *subcommand, const char *var_name,
                 struct dictionary *dict, struct variable **var)
{
  if (var_name[0] != '\0')
    {
      struct fmt_spec format = fmt_for_output (FMT_F, 1, 0);
      *var = dict_create_var (dict, var_name, 0);
      if (*var == NULL)
        {
          msg (SE, _("Variable name %s specified on %s subcommand "
                     "duplicates an existing variable name."),
               subcommand, var_name);
          return false;
        }
      var_set_both_formats (*var, &format);
    }
  else
    *var = NULL;
  return true;
}

/* Return a string in an allocated buffer describing V's variable
   type and width. */
static char *
var_type_description (struct variable *v)
{
  if (var_is_numeric (v))
    return xstrdup ("numeric");
  else
    return xasprintf ("string with width %d", var_get_width (v));
}

/* Closes all the files in MTF and frees their associated data.
   Returns true if successful, false if an I/O error occurred on
   any of the files. */
static bool
mtf_close_all_files (struct mtf_proc *mtf)
{
  bool ok = true;
  size_t i;

  for (i = 0; i < mtf->n_files; i++)
    {
      struct mtf_file *file = mtf->files[i];
      fh_unref (file->handle);
      dict_destroy (file->dict);
      subcase_destroy (&file->by);
      if (file->type == MTF_TABLE)
        casereader_destroy (file->reader);
      free (file->vars);
      free (file);
    }
  free (mtf->files);
  mtf->files = NULL;
  mtf->n_files = 0;

  return ok;
}

/* Frees all the data for the procedure. */
static void
mtf_free (struct mtf_proc *mtf)
{
  mtf_close_all_files (mtf);
  dict_destroy (mtf->dict);
  subcase_destroy (&mtf->by);
  casewriter_destroy (mtf->output);
  case_destroy (&mtf->buffered_case);
  free (mtf->prev_BY);
}

static bool
scan_table (struct mtf_file *file, union value *by) 
{
  while (!case_is_null (&file->c))
    {
      int cmp = subcase_compare_3way_xc (&file->by, by, &file->c);
      if (cmp > 0)
        casereader_read (file->reader, &file->c);
      else
        return cmp == 0;
    }
  return false;
}

static void
create_output_case (const struct mtf_proc *mtf, struct ccase *c)
{
  size_t i;

  case_create (c, dict_get_next_value_idx (mtf->dict));
  for (i = 0; i < dict_get_var_cnt (mtf->dict); i++)
    {
      struct variable *v = dict_get_var (mtf->dict, i);
      value_set_missing (case_data_rw (c, v), var_get_width (v));
    }
  for (i = 0; i < mtf->n_files; i++)
    {
      struct mtf_file *file = mtf->files[i];
      if (file->in_var != NULL)
        case_data_rw (c, file->in_var)->f = false;
    }
}

static void
apply_case (const struct mtf_file *file, struct ccase *file_case,
            struct ccase *c) 
{
  /* XXX subcases */
  size_t j;
  for (j = 0; j < file->var_cnt; j++)
    {
      const struct mtf_variable *mv = &file->vars[j];
      const union value *in = case_data (file_case, mv->in_var);
      union value *out = case_data_rw (c, mv->out_var);
      value_copy (out, in, var_get_width (mv->in_var));
    }
  case_destroy (file_case);
  if (file->in_var != NULL)
    case_data_rw (c, file->in_var)->f = true; 
}

static size_t
find_first_match (struct ccase *cases) 
{
  size_t i;
  for (i = 0; ; i++)
    if (!case_is_null (&cases[i]))
      return i;
}

static void
process_update (struct mtf_proc *mtf)
{
  struct ccase *cases;
  union value *by;

  while (case_matcher_read (mtf->matcher, &cases, &by))
    {
      struct mtf_file *min;
      struct ccase c;
      size_t min_idx;
      size_t i;

      create_output_case (mtf, &c);
      min_idx = find_first_match (cases);
      min = mtf->files[min_idx];
      apply_case (min, &cases[min_idx], &c);
      case_matcher_advance (mtf->matcher, min_idx, &cases[min_idx]);
      for (i = MAX (1, min_idx); i < mtf->n_files; i++)
        while (!case_is_null (&cases[i]))
          {
            apply_case (mtf->files[i], &cases[i], &c);
            case_matcher_advance (mtf->matcher, i, &cases[i]);
          }
      casewriter_write (mtf->output, &c);
      
      if (min_idx == 0)
        {
          size_t n_dups;

          for (n_dups = 0; !case_is_null (&cases[0]); n_dups++)
            {
              create_output_case (mtf, &c);
              apply_case (mtf->files[0], &cases[0], &c);
              case_matcher_advance (mtf->matcher, 0, &cases[0]);
              casewriter_write (mtf->output, &c);
            }
#if 0
          if (n_dups > 0) 
            msg (SW, _("Encountered %zu duplicates."), n_dups);
#endif
          /* XXX warn.  That's the whole point; otherwise we
             don't need the 'if' statement at all. */
        }
    }
}

/* Executes MATCH FILES for key-based matches. */
static void
process_match_files (struct mtf_proc *mtf)
{
  union value *by;
  struct ccase *cases;

  while (case_matcher_read (mtf->matcher, &cases, &by))
    {
      struct ccase c;
      size_t i;

      create_output_case (mtf, &c);
      for (i = mtf->n_files; i-- > 0; )
        {
          struct mtf_file *file = mtf->files[i];
          struct ccase *file_case;
          bool include;
          if (file->type == MTF_FILE) 
            {
              file_case = &cases[file->idx];
              include = !case_is_null (file_case);
              if (include)
                case_matcher_advance (mtf->matcher, file->idx, NULL);
            }
          else
            {
              file_case = &file->c;
              include = scan_table (file, by);
              if (include)
                case_clone (file_case, file_case);
            }
          if (include) 
            apply_case (file, file_case, &c);
        }
      output_case (mtf, &c, by);
    }
  output_buffered_case (mtf);
}

/* Processes input files and write one case to the output file. */
static void
process_add_files (struct mtf_proc *mtf)
{
  union value *by;
  struct ccase *cases;

  while (case_matcher_read (mtf->matcher, &cases, &by))
    {
      struct ccase c;
      size_t i;

      for (i = 0; i < mtf->n_files; i++)
        {
          struct mtf_file *file = mtf->files[i];
          while (!case_is_null (&cases[i])) 
            {
              create_output_case (mtf, &c);
              apply_case (file, &cases[i], &c);
              case_matcher_advance (mtf->matcher, i, &cases[i]);
              output_case (mtf, &c, by);
            }
        }
    }
  output_buffered_case (mtf);
}

static void
output_case (struct mtf_proc *mtf, struct ccase *c, union value *by)
{
  if (mtf->first == NULL && mtf->last == NULL)
    casewriter_write (mtf->output, c);
  else
    {
      /* It's harder with LAST, because we can't know whether
         this case is the last in a group until we've prepared
         the *next* case also.  Thus, we buffer the previous
         output case until the next one is ready. */
      bool new_BY;
      if (mtf->prev_BY != NULL)
        {
          new_BY = !subcase_equal_xx (&mtf->by, mtf->prev_BY, by);
          if (mtf->last != NULL)
            case_data_rw (&mtf->buffered_case, mtf->last)->f = new_BY;
          casewriter_write (mtf->output, &mtf->buffered_case);
        }
      else
        new_BY = true;

      case_move (&mtf->buffered_case, c);
      if (mtf->first != NULL)
        case_data_rw (&mtf->buffered_case, mtf->first)->f = new_BY;

      if (new_BY)
        {
          size_t n = subcase_get_n_values (&mtf->by) * sizeof (union value);
          if (mtf->prev_BY == NULL)
            mtf->prev_BY = xmalloc (n);
          memcpy (mtf->prev_BY, by, n);
        }
    }
}

static void
output_buffered_case (struct mtf_proc *mtf) 
{
  if (mtf->prev_BY != NULL)
    {
      if (mtf->last != NULL)
        case_data_rw (&mtf->buffered_case, mtf->last)->f = 1.0;
      casewriter_write (mtf->output, &mtf->buffered_case);
      case_nullify (&mtf->buffered_case);
    }
}

/* Merge the dictionary for file F into master dictionary M. */
static bool
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
          char *new_docs = xasprintf ("%s%s", m_docs, d_docs);
          dict_set_documents (m, new_docs);
          free (new_docs);
        }
    }

  for (i = 0; i < dict_get_var_cnt (d); i++)
    {
      struct variable *dv = dict_get_var (d, i);
      struct variable *mv = dict_lookup_var (m, var_get_name (dv));

      if (dict_class_from_id (var_get_name (dv)) == DC_SCRATCH)
        continue;

      if (mv != NULL)
        {
          if (var_get_width (mv) != var_get_width (dv))
            {
              char *dv_description = var_type_description (dv);
              char *mv_description = var_type_description (mv);
              msg (SE, _("Variable %s in file %s (%s) has different "
                         "type or width from the same variable in "
                         "earlier file (%s)."),
                   var_get_name (dv), fh_get_name (f->handle),
                   dv_description, mv_description);
              free (dv_description);
              free (mv_description);
              return false;
            }

          if (var_has_value_labels (dv) && !var_has_value_labels (mv))
            var_set_value_labels (mv, var_get_value_labels (dv));
          if (var_has_missing_values (dv) && !var_has_missing_values (mv))
            var_set_missing_values (mv, var_get_missing_values (dv));
          if (var_get_label (dv) && !var_get_label (mv))
            var_set_label (mv, var_get_label (dv));
        }
      else
        mv = dict_clone_var_assert (m, dv, var_get_name (dv));
    }

  return true;
}
