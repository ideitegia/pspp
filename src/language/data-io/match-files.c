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
#include <data/case.h>
#include <data/casereader.h>
#include <data/casewriter.h>
#include <data/format.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/file-handle.h>
#include <language/data-io/trim.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/taint.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* File types. */
enum mtf_type
  {
    MTF_FILE,			/* Specified on FILE= subcommand. */
    MTF_TABLE			/* Specified on TABLE= subcommand. */
  };

/* One of the FILEs or TABLEs on MATCH FILES. */
struct mtf_file
  {
    struct ll ll;               /* In list of all files and tables. */

    enum mtf_type type;
    int sequence;

    const struct variable **by; /* List of BY variables for this file. */
    struct mtf_variable *vars;  /* Variables to copy to output. */
    size_t var_cnt;             /* Number of other variables. */

    struct file_handle *handle; /* Input file handle. */
    struct dictionary *dict;	/* Input file dictionary. */
    struct casereader *reader;  /* Input reader. */
    struct ccase input;         /* Input record (null at end of file). */

    /* IN subcommand. */
    char *in_name;              /* Variable name. */
    struct variable *in_var;    /* Variable (in master dictionary). */
  };

struct mtf_variable
  {
    struct variable *in_var;
    struct variable *out_var;
  };

/* MATCH FILES procedure. */
struct mtf_proc
  {
    struct ll_list files;       /* List of "struct mtf_file"s. */
    int nonempty_files;         /* FILEs that are not at end-of-file. */

    bool ok;                    /* False if I/O error occurs. */

    struct dictionary *dict;    /* Dictionary of output file. */
    struct casewriter *output;  /* MATCH FILES output. */

    size_t by_cnt;              /* Number of variables on BY subcommand. */

    /* FIRST, LAST.
       Only if "first" or "last" is nonnull are the remaining
       members used. */
    struct variable *first;     /* Variable specified on FIRST (if any). */
    struct variable *last;      /* Variable specified on LAST (if any). */
    struct ccase buffered_case; /* Case ready for output except that we don't
                                   know the value for the LAST variable yet. */
    struct ccase prev_BY_case;  /* Case with values of last set of BY vars. */
    const struct variable **prev_BY;  /* Last set of BY variables. */
  };

static void mtf_free (struct mtf_proc *);

static bool mtf_close_all_files (struct mtf_proc *);
static bool mtf_merge_dictionary (struct dictionary *const, struct mtf_file *);
static bool mtf_read_record (struct mtf_proc *mtf, struct mtf_file *);

static void mtf_process_case (struct mtf_proc *);

static bool create_flag_var (const char *subcommand_name, const char *var_name,
                             struct dictionary *, struct variable **);
static char *var_type_description (struct variable *);

/* Parse and execute the MATCH FILES command. */
int
cmd_match_files (struct lexer *lexer, struct dataset *ds)
{
  struct mtf_proc mtf;
  struct ll *first_table;
  struct mtf_file *file, *next;

  bool saw_in = false;
  struct casereader *active_file = NULL;

  char first_name[VAR_NAME_LEN + 1] = "";
  char last_name[VAR_NAME_LEN + 1] = "";

  struct taint *taint = NULL;

  size_t i;

  ll_init (&mtf.files);
  mtf.nonempty_files = 0;
  first_table = ll_null (&mtf.files);
  mtf.dict = dict_create ();
  mtf.output = NULL;
  mtf.by_cnt = 0;
  mtf.first = mtf.last = NULL;
  case_nullify (&mtf.buffered_case);
  case_nullify (&mtf.prev_BY_case);
  mtf.prev_BY = NULL;

  dict_set_case_limit (mtf.dict, dict_get_case_limit (dataset_dict (ds)));

  lex_match (lexer, '/');
  while (lex_token (lexer) == T_ID
         && (lex_id_match (ss_cstr ("FILE"), ss_cstr (lex_tokid (lexer)))
             || lex_id_match (ss_cstr ("TABLE"), ss_cstr (lex_tokid (lexer)))))
    {
      struct mtf_file *file = xmalloc (sizeof *file);
      file->by = NULL;
      file->handle = NULL;
      file->reader = NULL;
      file->dict = NULL;
      file->in_name = NULL;
      file->in_var = NULL;
      file->var_cnt = 0;
      file->vars = NULL;
      case_nullify (&file->input);

      if (lex_match_id (lexer, "FILE"))
        {
          file->type = MTF_FILE;
          ll_insert (first_table, &file->ll);
          mtf.nonempty_files++;
        }
      else if (lex_match_id (lexer, "TABLE"))
        {
          file->type = MTF_TABLE;
          ll_push_tail (&mtf.files, &file->ll);
          if (first_table == ll_null (&mtf.files))
            first_table = &file->ll;
        }
      else
        NOT_REACHED ();
      lex_match (lexer, '=');

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
                 _("MATCH FILES may not be used after TEMPORARY when "
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

            if (file->in_name != NULL)
              {
                msg (SE, _("Multiple IN subcommands for a single FILE or "
                           "TABLE."));
                goto error;
              }
            file->in_name = xstrdup (lex_tokid (lexer));
            lex_get (lexer);
            saw_in = true;
          }

      mtf_merge_dictionary (mtf.dict, file);
    }

  while (lex_token (lexer) != '.')
    {
      if (lex_match (lexer, T_BY))
	{
          struct mtf_file *file;
          struct variable **by;
          bool ok;

	  if (mtf.by_cnt)
	    {
              lex_sbc_only_once ("BY");
	      goto error;
	    }

	  lex_match (lexer, '=');
	  if (!parse_variables (lexer, mtf.dict, &by, &mtf.by_cnt,
				PV_NO_DUPLICATE | PV_NO_SCRATCH))
	    goto error;

          ok = true;
          ll_for_each (file, struct mtf_file, ll, &mtf.files)
            {
              size_t i;

              file->by = xnmalloc (mtf.by_cnt, sizeof *file->by);
              for (i = 0; i < mtf.by_cnt; i++)
                {
                  const char *var_name = var_get_name (by[i]);
                  file->by[i] = dict_lookup_var (file->dict, var_name);
                  if (file->by[i] == NULL)
                    {
                      if (file->handle != NULL)
                        msg (SE, _("File %s lacks BY variable %s."),
                             fh_get_name (file->handle), var_name);
                      else
                        msg (SE, _("Active file lacks BY variable %s."),
                             var_name);
                      ok = false;
                    }
                }
            }
          free (by);

          if (!ok)
            goto error;
	}
      else if (lex_match_id (lexer, "FIRST"))
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
      else if (lex_match_id (lexer, "LAST"))
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

  if (mtf.by_cnt == 0)
    {
      if (first_table != ll_null (&mtf.files))
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
  ll_for_each (file, struct mtf_file, ll, &mtf.files)
    {
      size_t in_var_cnt = dict_get_var_cnt (file->dict);

      file->vars = xnmalloc (in_var_cnt, sizeof *file->vars);
      file->var_cnt = 0;
      for (i = 0; i < in_var_cnt; i++)
        {
          struct variable *in_var = dict_get_var (file->dict, i);
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
  ll_for_each (file, struct mtf_file, ll, &mtf.files)
    if (!create_flag_var ("IN", file->in_name, mtf.dict, &file->in_var))
      goto error;
  if (!create_flag_var ("FIRST", first_name, mtf.dict, &mtf.first)
      || !create_flag_var ("LAST", last_name, mtf.dict, &mtf.last))
    goto error;

  dict_delete_scratch_vars (mtf.dict);
  dict_compact_values (mtf.dict);
  mtf.output = autopaging_writer_create (dict_get_next_value_idx (mtf.dict));
  taint = taint_clone (casewriter_get_taint (mtf.output));

  ll_for_each (file, struct mtf_file, ll, &mtf.files)
    {
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
      taint_propagate (casereader_get_taint (file->reader), taint);
    }

  ll_for_each_safe (file, next, struct mtf_file, ll, &mtf.files)
    mtf_read_record (&mtf, file);
  while (mtf.nonempty_files > 0)
    mtf_process_case (&mtf);
  if ((mtf.first != NULL || mtf.last != NULL) && mtf.prev_BY != NULL)
    {
      if (mtf.last != NULL)
        case_data_rw (&mtf.buffered_case, mtf.last)->f = 1.0;
      casewriter_write (mtf.output, &mtf.buffered_case);
      case_nullify (&mtf.buffered_case);
    }
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

/* If VAR_NAME is a nonnull pointer to a non-empty string,
   attempts to create a variable named VAR_NAME, with format
   F1.0, in DICT, and stores a pointer to the variable in *VAR.
   Returns true if successful, false if the variable name is a
   duplicate (in which case a message saying that the variable
   specified on the given SUBCOMMAND is a duplicate is emitted).
   Also returns true, without doing anything, if VAR_NAME is null
   or empty. */
static bool
create_flag_var (const char *subcommand, const char *var_name,
                 struct dictionary *dict, struct variable **var)
{
  if (var_name != NULL && var_name[0] != '\0')
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
  struct mtf_file *file;
  bool ok = true;

  ll_for_each_preremove (file, struct mtf_file, ll, &mtf->files)
    {
      fh_unref (file->handle);
      casereader_destroy (file->reader);
      free (file->by);
      dict_destroy (file->dict);
      free (file->in_name);
      case_destroy (&file->input);
      free (file->vars);
      free (file);
    }

  return ok;
}

/* Frees all the data for the MATCH FILES procedure. */
static void
mtf_free (struct mtf_proc *mtf)
{
  mtf_close_all_files (mtf);
  dict_destroy (mtf->dict);
  casewriter_destroy (mtf->output);
  case_destroy (&mtf->buffered_case);
  case_destroy (&mtf->prev_BY_case);
}

/* Reads the next record into FILE, if possible, and update MTF's
   nonempty_files count if not. */
static bool
mtf_read_record (struct mtf_proc *mtf, struct mtf_file *file)
{
  case_destroy (&file->input);
  if (!casereader_read (file->reader, &file->input))
    {
      mtf->nonempty_files--;
      return false;
    }
  else
    return true;
}

/* Compare the BY variables for files A and B; return -1 if A <
   B, 0 if A == B, 1 if A > B.  (If there are no BY variables,
   then all records are equal.) */
static inline int
mtf_compare_BY_values (struct mtf_proc *mtf,
                       struct mtf_file *a, struct mtf_file *b)
{
  return case_compare_2dict (&a->input, &b->input, a->by, b->by, mtf->by_cnt);
}

/* Processes input files and write one case to the output file. */
static void
mtf_process_case (struct mtf_proc *mtf)
{
  struct ccase c;
  struct mtf_file *min;
  struct mtf_file *file;
  int min_sequence;
  size_t i;

  /* Find the set of one or more FILEs whose BY values are
     minimal, as well as the set of zero or more TABLEs whose BY
     values equal those of the minimum FILEs.

     After each iteration of the loop, this invariant holds: the
     FILEs with minimum BY values thus far have "sequence"
     members equal to min_sequence, and "min" points to one of
     the mtf_files whose case has those minimum BY values, and
     similarly for TABLEs. */
  min_sequence = 0;
  min = NULL;
  ll_for_each (file, struct mtf_file, ll, &mtf->files)
    if (case_is_null (&file->input))
      file->sequence = -1;
    else if (file->type == MTF_FILE)
      {
        int cmp = min != NULL ? mtf_compare_BY_values (mtf, min, file) : 1;
        if (cmp <= 0)
          file->sequence = cmp < 0 ? -1 : min_sequence;
        else
          {
            file->sequence = ++min_sequence;
            min = file;
          }
      }
    else
      {
        int cmp;
        assert (min != NULL);
        do
          {
            cmp = mtf_compare_BY_values (mtf, min, file);
          }
        while (cmp > 0 && mtf_read_record (mtf, file));
        file->sequence = cmp == 0 ? min_sequence : -1;
      }

  /* Form the output case from the input cases. */
  case_create (&c, dict_get_next_value_idx (mtf->dict));
  for (i = 0; i < dict_get_var_cnt (mtf->dict); i++)
    {
      struct variable *v = dict_get_var (mtf->dict, i);
      value_set_missing (case_data_rw (&c, v), var_get_width (v));
    }
  ll_for_each_reverse (file, struct mtf_file, ll, &mtf->files)
    {
      bool include_file = file->sequence == min_sequence;
      if (include_file)
        for (i = 0; i < file->var_cnt; i++)
          {
            const struct mtf_variable *mv = &file->vars[i];
            const union value *in = case_data (&file->input, mv->in_var);
            union value *out = case_data_rw (&c, mv->out_var);
            value_copy (out, in, var_get_width (mv->in_var));
          }
      if (file->in_var != NULL)
        case_data_rw (&c, file->in_var)->f = include_file;
    }

  /* Write the output case. */
  if (mtf->first == NULL && mtf->last == NULL)
    {
      /* With no FIRST or LAST variables, it's trivial. */
      casewriter_write (mtf->output, &c);
    }
  else
    {
      /* It's harder with LAST, because we can't know whether
         this case is the last in a group until we've prepared
         the *next* case also.  Thus, we buffer the previous
         output case until the next one is ready.

         We also have to save a copy of one of the previous input
         cases, so that we can compare the BY variables.  We
         can't compare the BY variables between the current
         output case and the saved one because the BY variables
         might not be in the output (the user is allowed to drop
         them). */
      bool new_BY;
      if (mtf->prev_BY != NULL)
        {
          new_BY = case_compare_2dict (&min->input, &mtf->prev_BY_case,
                                       min->by, mtf->prev_BY,
                                       mtf->by_cnt);
          if (mtf->last != NULL)
            case_data_rw (&mtf->buffered_case, mtf->last)->f = new_BY;
          casewriter_write (mtf->output, &mtf->buffered_case);
        }
      else
        new_BY = true;

      case_move (&mtf->buffered_case, &c);
      if (mtf->first != NULL)
        case_data_rw (&mtf->buffered_case, mtf->first)->f = new_BY;

      if (new_BY)
        {
          mtf->prev_BY = min->by;
          case_destroy (&mtf->prev_BY_case);
          case_clone (&mtf->prev_BY_case, &min->input);
        }
    }

  /* Read another record from each input file FILE with minimum
     values. */
  ll_for_each (file, struct mtf_file, ll, &mtf->files)
    if (file->type == MTF_FILE)
      {
        if (file->sequence == min_sequence)
          mtf_read_record (mtf, file);
      }
    else
      break;
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

          if (var_get_width (dv) == var_get_width (mv))
            {
              if (var_has_value_labels (dv) && !var_has_value_labels (mv))
                var_set_value_labels (mv, var_get_value_labels (dv));
              if (var_has_missing_values (dv) && !var_has_missing_values (mv))
                var_set_missing_values (mv, var_get_missing_values (dv));
            }

          if (var_get_label (dv) && !var_get_label (mv))
            var_set_label (mv, var_get_label (dv));
        }
      else
        mv = dict_clone_var_assert (m, dv, var_get_name (dv));
    }

  return true;
}
