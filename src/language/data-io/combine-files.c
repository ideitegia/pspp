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

#include "data/any-reader.h"
#include "data/case-matcher.h"
#include "data/case.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/subcase.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/data-io/file-handle.h"
#include "language/data-io/trim.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "language/stats/sort-criteria.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/string-array.h"
#include "libpspp/taint.h"
#include "math/sort.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

enum comb_command_type
  {
    COMB_ADD,
    COMB_MATCH,
    COMB_UPDATE
  };

/* File types. */
enum comb_file_type
  {
    COMB_FILE,			/* Specified on FILE= subcommand. */
    COMB_TABLE			/* Specified on TABLE= subcommand. */
  };

/* One FILE or TABLE subcommand. */
struct comb_file
  {
    /* Basics. */
    enum comb_file_type type;   /* COMB_FILE or COMB_TABLE. */

    /* Variables. */
    struct subcase by_vars;     /* BY variables in this input file. */
    struct subcase src, dst;    /* Data to copy to output; where to put it. */
    const struct missing_values **mv; /* Each variable's missing values. */

    /* Input files. */
    struct file_handle *handle; /* Input file handle. */
    struct dictionary *dict;	/* Input file dictionary. */
    struct casereader *reader;  /* Input data source. */
    struct ccase *data;         /* The current input case. */
    bool is_minimal;            /* Does 'data' have minimum BY values across
                                   all input files? */
    bool is_sorted;             /* Is file presorted on the BY variables? */

    /* IN subcommand. */
    char *in_name;
    struct variable *in_var;
  };

struct comb_proc
  {
    struct comb_file *files;    /* All the files being merged. */
    size_t n_files;             /* Number of files. */

    struct dictionary *dict;    /* Dictionary of output file. */
    struct subcase by_vars;     /* BY variables in the output. */
    struct casewriter *output;  /* Destination for output. */

    struct case_matcher *matcher;

    /* FIRST, LAST.
       Only if "first" or "last" is nonnull are the remaining
       members used. */
    struct variable *first;     /* Variable specified on FIRST (if any). */
    struct variable *last;      /* Variable specified on LAST (if any). */
    struct ccase *buffered_case; /* Case ready for output except that we don't
                                    know the value for the LAST var yet. */
    union value *prev_BY;       /* Values of BY vars in buffered_case. */
  };

static int combine_files (enum comb_command_type, struct lexer *,
                          struct dataset *);
static void free_comb_proc (struct comb_proc *);

static void close_all_comb_files (struct comb_proc *);
static bool merge_dictionary (struct dictionary *const, struct comb_file *);

static void execute_update (struct comb_proc *);
static void execute_match_files (struct comb_proc *);
static void execute_add_files (struct comb_proc *);

static bool create_flag_var (const char *subcommand_name, const char *var_name,
                             struct dictionary *, struct variable **);
static void output_case (struct comb_proc *, struct ccase *, union value *by);
static void output_buffered_case (struct comb_proc *);

int
cmd_add_files (struct lexer *lexer, struct dataset *ds)
{
  return combine_files (COMB_ADD, lexer, ds);
}

int
cmd_match_files (struct lexer *lexer, struct dataset *ds)
{
  return combine_files (COMB_MATCH, lexer, ds);
}

int
cmd_update (struct lexer *lexer, struct dataset *ds)
{
  return combine_files (COMB_UPDATE, lexer, ds);
}

static int
combine_files (enum comb_command_type command,
               struct lexer *lexer, struct dataset *ds)
{
  struct comb_proc proc;

  bool saw_by = false;
  bool saw_sort = false;
  struct casereader *active_file = NULL;

  char *first_name = NULL;
  char *last_name = NULL;

  struct taint *taint = NULL;

  size_t n_tables = 0;
  size_t allocated_files = 0;

  size_t i;

  proc.files = NULL;
  proc.n_files = 0;
  proc.dict = dict_create (get_default_encoding ());
  proc.output = NULL;
  proc.matcher = NULL;
  subcase_init_empty (&proc.by_vars);
  proc.first = NULL;
  proc.last = NULL;
  proc.buffered_case = NULL;
  proc.prev_BY = NULL;

  dict_set_case_limit (proc.dict, dict_get_case_limit (dataset_dict (ds)));

  lex_match (lexer, T_SLASH);
  for (;;)
    {
      struct comb_file *file;
      enum comb_file_type type;

      if (lex_match_id (lexer, "FILE"))
        type = COMB_FILE;
      else if (command == COMB_MATCH && lex_match_id (lexer, "TABLE"))
        {
          type = COMB_TABLE;
          n_tables++;
        }
      else
        break;
      lex_match (lexer, T_EQUALS);

      if (proc.n_files >= allocated_files)
        proc.files = x2nrealloc (proc.files, &allocated_files,
                                sizeof *proc.files);
      file = &proc.files[proc.n_files++];
      file->type = type;
      subcase_init_empty (&file->by_vars);
      subcase_init_empty (&file->src);
      subcase_init_empty (&file->dst);
      file->mv = NULL;
      file->handle = NULL;
      file->dict = NULL;
      file->reader = NULL;
      file->data = NULL;
      file->is_sorted = true;
      file->in_name = NULL;
      file->in_var = NULL;

      if (lex_match (lexer, T_ASTERISK))
        {
          if (!dataset_has_source (ds))
            {
              msg (SE, _("Cannot specify the active dataset since none "
                         "has been defined."));
              goto error;
            }

          if (proc_make_temporary_transformations_permanent (ds))
            msg (SE, _("This command may not be used after TEMPORARY when "
                       "the active dataset is an input source.  "
                       "Temporary transformations will be made permanent."));

          file->dict = dict_clone (dataset_dict (ds));
        }
      else
        {
          file->handle = fh_parse (lexer, FH_REF_FILE, dataset_session (ds));
          if (file->handle == NULL)
            goto error;

          file->reader = any_reader_open_and_decode (file->handle, NULL,
                                                     &file->dict, NULL);
          if (file->reader == NULL)
            goto error;
        }

      while (lex_match (lexer, T_SLASH))
        if (lex_match_id (lexer, "RENAME"))
          {
            if (!parse_dict_rename (lexer, file->dict))
              goto error;
          }
        else if (lex_match_id (lexer, "IN"))
          {
            lex_match (lexer, T_EQUALS);
            if (lex_token (lexer) != T_ID)
              {
                lex_error (lexer, NULL);
                goto error;
              }

            if (file->in_name)
              {
                msg (SE, _("Multiple IN subcommands for a single FILE or "
                           "TABLE."));
                goto error;
              }
            file->in_name = xstrdup (lex_tokcstr (lexer));
            lex_get (lexer);
          }
        else if (lex_match_id (lexer, "SORT"))
          {
            file->is_sorted = false;
            saw_sort = true;
          }

      if (!merge_dictionary (proc.dict, file))
        goto error;
    }

  while (lex_token (lexer) != T_ENDCMD)
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

	  lex_match (lexer, T_EQUALS);
          if (!parse_sort_criteria (lexer, proc.dict, &proc.by_vars,
                                    &by_vars, NULL))
	    goto error;

          ok = true;
          for (i = 0; i < proc.n_files; i++)
            {
              struct comb_file *file = &proc.files[i];
              size_t j;

              for (j = 0; j < subcase_get_n_fields (&proc.by_vars); j++)
                {
                  const char *name = var_get_name (by_vars[j]);
                  struct variable *var = dict_lookup_var (file->dict, name);
                  if (var != NULL)
                    subcase_add_var (&file->by_vars, var,
                                     subcase_get_direction (&proc.by_vars, j));
                  else
                    {
                      if (file->handle != NULL)
                        msg (SE, _("File %s lacks BY variable %s."),
                             fh_get_name (file->handle), name);
                      else
                        msg (SE, _("Active dataset lacks BY variable %s."),
                             name);
                      ok = false;
                    }
                }
              assert (!ok || subcase_conformable (&file->by_vars,
                                                  &proc.files[0].by_vars));
            }
          free (by_vars);

          if (!ok)
            goto error;
	}
      else if (command != COMB_UPDATE && lex_match_id (lexer, "FIRST"))
        {
          if (first_name != NULL)
            {
              lex_sbc_only_once ("FIRST");
              goto error;
            }

	  lex_match (lexer, T_EQUALS);
          if (!lex_force_id (lexer))
            goto error;
          first_name = xstrdup (lex_tokcstr (lexer));
          lex_get (lexer);
        }
      else if (command != COMB_UPDATE && lex_match_id (lexer, "LAST"))
        {
          if (last_name != NULL)
            {
              lex_sbc_only_once ("LAST");
              goto error;
            }

	  lex_match (lexer, T_EQUALS);
          if (!lex_force_id (lexer))
            goto error;
          last_name = xstrdup (lex_tokcstr (lexer));
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "MAP"))
	{
	  /* FIXME. */
	}
      else if (lex_match_id (lexer, "DROP"))
        {
          if (!parse_dict_drop (lexer, proc.dict))
            goto error;
        }
      else if (lex_match_id (lexer, "KEEP"))
        {
          if (!parse_dict_keep (lexer, proc.dict))
            goto error;
        }
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}

      if (!lex_match (lexer, T_SLASH) && lex_token (lexer) != T_ENDCMD)
        {
          lex_end_of_command (lexer);
          goto error;
        }
    }

  if (!saw_by)
    {
      if (command == COMB_UPDATE)
        {
          lex_sbc_missing ("BY");
          goto error;
        }
      if (n_tables)
        {
          msg (SE, _("BY is required when %s is specified."), "TABLE");
          goto error;
        }
      if (saw_sort)
        {
          msg (SE, _("BY is required when %s is specified."), "SORT");
          goto error;
        }
    }

  /* Add IN, FIRST, and LAST variables to master dictionary. */
  for (i = 0; i < proc.n_files; i++)
    {
      struct comb_file *file = &proc.files[i];
      if (!create_flag_var ("IN", file->in_name, proc.dict, &file->in_var))
        goto error;
    }
  if (!create_flag_var ("FIRST", first_name, proc.dict, &proc.first)
      || !create_flag_var ("LAST", last_name, proc.dict, &proc.last))
    goto error;

  dict_delete_scratch_vars (proc.dict);
  dict_compact_values (proc.dict);

  /* Set up mapping from each file's variables to master
     variables. */
  for (i = 0; i < proc.n_files; i++)
    {
      struct comb_file *file = &proc.files[i];
      size_t src_var_cnt = dict_get_var_cnt (file->dict);
      size_t j;

      file->mv = xnmalloc (src_var_cnt, sizeof *file->mv);
      for (j = 0; j < src_var_cnt; j++)
        {
          struct variable *src_var = dict_get_var (file->dict, j);
          struct variable *dst_var = dict_lookup_var (proc.dict,
                                                      var_get_name (src_var));
          if (dst_var != NULL)
            {
              size_t n = subcase_get_n_fields (&file->src);
              file->mv[n] = var_get_missing_values (src_var);
              subcase_add_var (&file->src, src_var, SC_ASCEND);
              subcase_add_var (&file->dst, dst_var, SC_ASCEND);
            }
        }
    }

  proc.output = autopaging_writer_create (dict_get_proto (proc.dict));
  taint = taint_clone (casewriter_get_taint (proc.output));

  /* Set up case matcher. */
  proc.matcher = case_matcher_create ();
  for (i = 0; i < proc.n_files; i++)
    {
      struct comb_file *file = &proc.files[i];
      if (file->reader == NULL)
        {
          if (active_file == NULL)
            {
              proc_discard_output (ds);
              file->reader = active_file = proc_open_filtering (ds, false);
            }
          else
            file->reader = casereader_clone (active_file);
        }
      if (!file->is_sorted)
        file->reader = sort_execute (file->reader, &file->by_vars);
      taint_propagate (casereader_get_taint (file->reader), taint);
      file->data = casereader_read (file->reader);
      if (file->type == COMB_FILE)
        case_matcher_add_input (proc.matcher, &file->by_vars,
                                &file->data, &file->is_minimal);
    }

  if (command == COMB_ADD)
    execute_add_files (&proc);
  else if (command == COMB_MATCH)
    execute_match_files (&proc);
  else if (command == COMB_UPDATE)
    execute_update (&proc);
  else
    NOT_REACHED ();

  case_matcher_destroy (proc.matcher);
  proc.matcher = NULL;
  close_all_comb_files (&proc);
  if (active_file != NULL)
    proc_commit (ds);

  dataset_set_dict (ds, proc.dict);
  dataset_set_source (ds, casewriter_make_reader (proc.output));
  proc.dict = NULL;
  proc.output = NULL;

  free_comb_proc (&proc);

  free (first_name);
  free (last_name);

  return taint_destroy (taint) ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

 error:
  if (active_file != NULL)
    proc_commit (ds);
  free_comb_proc (&proc);
  taint_destroy (taint);
  free (first_name);
  free (last_name);
  return CMD_CASCADING_FAILURE;
}

/* Merge the dictionary for file F into master dictionary M. */
static bool
merge_dictionary (struct dictionary *const m, struct comb_file *f)
{
  struct dictionary *d = f->dict;
  const struct string_array *d_docs, *m_docs;
  int i;

  if (dict_get_label (m) == NULL)
    dict_set_label (m, dict_get_label (d));

  d_docs = dict_get_documents (d);
  m_docs = dict_get_documents (m);


  /* FIXME: If the input files have different encodings, then
     the result is undefined.
     The correct thing to do would be to convert to an encoding
     which can cope with all the input files (eg UTF-8).
   */
  if ( 0 != strcmp (dict_get_encoding (f->dict), dict_get_encoding (m)))
    msg (MW, _("Combining files with incompatible encodings. String data may "
               "not be represented correctly."));

  if (d_docs != NULL)
    {
      if (m_docs == NULL)
        dict_set_documents (m, d_docs);
      else
        {
          struct string_array new_docs;
          size_t i;

          new_docs.n = m_docs->n + d_docs->n;
          new_docs.strings = xmalloc (new_docs.n * sizeof *new_docs.strings);
          for (i = 0; i < m_docs->n; i++)
            new_docs.strings[i] = m_docs->strings[i];
          for (i = 0; i < d_docs->n; i++)
            new_docs.strings[m_docs->n + i] = d_docs->strings[i];

          dict_set_documents (m, &new_docs);

          free (new_docs.strings);
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
              const char *var_name = var_get_name (dv);
              struct string s = DS_EMPTY_INITIALIZER;
              const char *file_name;

              file_name = f->handle ? fh_get_name (f->handle) : "*";
              ds_put_format (&s,
                             _("Variable %s in file %s has different "
                               "type or width from the same variable in "
                               "earlier file."),
                             var_name, file_name);
              ds_put_cstr (&s, "  ");
              if (var_is_numeric (dv))
                ds_put_format (&s, _("In file %s, %s is numeric."),
                               file_name, var_name);
              else
                ds_put_format (&s, _("In file %s, %s is a string variable "
                                     "with width %d."),
                               file_name, var_name, var_get_width (dv));
              ds_put_cstr (&s, "  ");
              if (var_is_numeric (mv))
                ds_put_format (&s, _("In an earlier file, %s was numeric."),
                               var_name);
              else
                ds_put_format (&s, _("In an earlier file, %s was a string "
                                     "variable with width %d."),
                               var_name, var_get_width (mv));
              msg (SE, "%s", ds_cstr (&s));
              ds_destroy (&s);
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
        mv = dict_clone_var_assert (m, dv);
    }

  return true;
}

/* If VAR_NAME is non-NULL, attempts to create a
   variable named VAR_NAME, with format F1.0, in DICT, and stores
   a pointer to the variable in *VAR.  Returns true if
   successful, false if the variable name is a duplicate (in
   which case a message saying that the variable specified on the
   given SUBCOMMAND is a duplicate is emitted).

   Does nothing and returns true if VAR_NAME is null. */
static bool
create_flag_var (const char *subcommand, const char *var_name,
                 struct dictionary *dict, struct variable **var)
{
  if (var_name != NULL)
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

/* Closes all the files in PROC and frees their associated data. */
static void
close_all_comb_files (struct comb_proc *proc)
{
  size_t i;

  for (i = 0; i < proc->n_files; i++)
    {
      struct comb_file *file = &proc->files[i];
      subcase_destroy (&file->by_vars);
      subcase_destroy (&file->src);
      subcase_destroy (&file->dst);
      free (file->mv);
      fh_unref (file->handle);
      dict_destroy (file->dict);
      casereader_destroy (file->reader);
      case_unref (file->data);
      free (file->in_name);
    }
  free (proc->files);
  proc->files = NULL;
  proc->n_files = 0;
}

/* Frees all the data for the procedure. */
static void
free_comb_proc (struct comb_proc *proc)
{
  close_all_comb_files (proc);
  dict_destroy (proc->dict);
  casewriter_destroy (proc->output);
  case_matcher_destroy (proc->matcher);
  if (proc->prev_BY)
    {
      caseproto_destroy_values (subcase_get_proto (&proc->by_vars),
                                proc->prev_BY);
      free (proc->prev_BY);
    }
  subcase_destroy (&proc->by_vars);
  case_unref (proc->buffered_case);
}

static bool scan_table (struct comb_file *, union value by[]);
static struct ccase *create_output_case (const struct comb_proc *);
static void apply_case (const struct comb_file *, struct ccase *);
static void apply_nonmissing_case (const struct comb_file *, struct ccase *);
static void advance_file (struct comb_file *, union value by[]);
static void output_case (struct comb_proc *, struct ccase *, union value by[]);
static void output_buffered_case (struct comb_proc *);

/* Executes the ADD FILES command. */
static void
execute_add_files (struct comb_proc *proc)
{
  union value *by;

  while (case_matcher_match (proc->matcher, &by))
    {
      size_t i;

      for (i = 0; i < proc->n_files; i++)
        {
          struct comb_file *file = &proc->files[i];
          while (file->is_minimal)
            {
              struct ccase *output = create_output_case (proc);
              apply_case (file, output);
              advance_file (file, by);
              output_case (proc, output, by);
            }
        }
    }
  output_buffered_case (proc);
}

/* Executes the MATCH FILES command. */
static void
execute_match_files (struct comb_proc *proc)
{
  union value *by;

  while (case_matcher_match (proc->matcher, &by))
    {
      struct ccase *output;
      size_t i;

      output = create_output_case (proc);
      for (i = proc->n_files; i-- > 0; )
        {
          struct comb_file *file = &proc->files[i];
          if (file->type == COMB_FILE)
            {
              if (file->is_minimal)
                {
                  apply_case (file, output);
                  advance_file (file, NULL);
                }
            }
          else
            {
              if (scan_table (file, by))
                apply_case (file, output);
            }
        }
      output_case (proc, output, by);
    }
  output_buffered_case (proc);
}

/* Executes the UPDATE command. */
static void
execute_update (struct comb_proc *proc)
{
  union value *by;
  size_t n_duplicates = 0;

  while (case_matcher_match (proc->matcher, &by))
    {
      struct comb_file *first, *file;
      struct ccase *output;

      /* Find first nonnull case in array and make an output case
         from it. */
      output = create_output_case (proc);
      for (first = &proc->files[0]; ; first++)
        if (first->is_minimal)
          break;
      apply_case (first, output);
      advance_file (first, by);

      /* Read additional cases and update the output case from
         them.  (Don't update the output case from any duplicate
         cases in the master file.) */
      for (file = first + (first == proc->files);
           file < &proc->files[proc->n_files]; file++)
        {
          while (file->is_minimal)
            {
              apply_nonmissing_case (file, output);
              advance_file (file, by);
            }
        }
      casewriter_write (proc->output, output);

      /* Write duplicate cases in the master file directly to the
         output.  */
      if (first == proc->files && first->is_minimal)
        {
          n_duplicates++;
          while (first->is_minimal)
            {
              output = create_output_case (proc);
              apply_case (first, output);
              advance_file (first, by);
              casewriter_write (proc->output, output);
            }
        }
    }

  if (n_duplicates)
    msg (SW, _("Encountered %zu sets of duplicate cases in the master file."),
         n_duplicates);
}

/* Reads FILE, which must be of type COMB_TABLE, until it
   encounters a case with BY or greater for its BY variables.
   Returns true if a case with exactly BY for its BY variables
   was found, otherwise false. */
static bool
scan_table (struct comb_file *file, union value by[])
{
  while (file->data != NULL)
    {
      int cmp = subcase_compare_3way_xc (&file->by_vars, by, file->data);
      if (cmp > 0)
        {
          case_unref (file->data);
          file->data = casereader_read (file->reader);
        }
      else
        return cmp == 0;
    }
  return false;
}

/* Creates and returns an output case for PROC, initializing each
   of its values to system-missing or blanks, except that the
   values of IN variables are set to 0. */
static struct ccase *
create_output_case (const struct comb_proc *proc)
{
  size_t n_vars = dict_get_var_cnt (proc->dict);
  struct ccase *output;
  size_t i;

  output = case_create (dict_get_proto (proc->dict));
  for (i = 0; i < n_vars; i++)
    {
      struct variable *v = dict_get_var (proc->dict, i);
      value_set_missing (case_data_rw (output, v), var_get_width (v));
    }
  for (i = 0; i < proc->n_files; i++)
    {
      struct comb_file *file = &proc->files[i];
      if (file->in_var != NULL)
        case_data_rw (output, file->in_var)->f = false;
    }
  return output;
}

static void
mark_file_used (const struct comb_file *file, struct ccase *output)
{
  if (file->in_var != NULL)
    case_data_rw (output, file->in_var)->f = true;
}

/* Copies the data from FILE's case into output case OUTPUT.
   If FILE has an IN variable, then it is set to 1 in OUTPUT. */
static void
apply_case (const struct comb_file *file, struct ccase *output)
{
  subcase_copy (&file->src, file->data, &file->dst, output);
  mark_file_used (file, output);
}

/* Copies the data from FILE's case into output case OUTPUT,
   skipping values that are missing or all spaces.

   If FILE has an IN variable, then it is set to 1 in OUTPUT. */
static void
apply_nonmissing_case (const struct comb_file *file, struct ccase *output)
{
  size_t i;

  for (i = 0; i < subcase_get_n_fields (&file->src); i++)
    {
      const struct subcase_field *src_field = &file->src.fields[i];
      const struct subcase_field *dst_field = &file->dst.fields[i];
      const union value *src_value
        = case_data_idx (file->data, src_field->case_index);
      int width = src_field->width;

      if (!mv_is_value_missing (file->mv[i], src_value, MV_ANY)
          && !(width > 0 && value_is_spaces (src_value, width)))
        value_copy (case_data_rw_idx (output, dst_field->case_index),
                    src_value, width);
    }
  mark_file_used (file, output);
}

/* Advances FILE to its next case.  If BY is nonnull, then FILE's is_minimal
   member is updated based on whether the new case's BY values still match
   those in BY. */
static void
advance_file (struct comb_file *file, union value by[])
{
  case_unref (file->data);
  file->data = casereader_read (file->reader);
  if (by)
    file->is_minimal = (file->data != NULL
                        && subcase_equal_cx (&file->by_vars, file->data, by));
}

/* Writes OUTPUT, whose BY values has been extracted into BY, to
   PROC's output file, first initializing any FIRST or LAST
   variables in OUTPUT to the correct values. */
static void
output_case (struct comb_proc *proc, struct ccase *output, union value by[])
{
  if (proc->first == NULL && proc->last == NULL)
    casewriter_write (proc->output, output);
  else
    {
      /* It's harder with LAST, because we can't know whether
         this case is the last in a group until we've prepared
         the *next* case also.  Thus, we buffer the previous
         output case until the next one is ready. */
      bool new_BY;
      if (proc->prev_BY != NULL)
        {
          new_BY = !subcase_equal_xx (&proc->by_vars, proc->prev_BY, by);
          if (proc->last != NULL)
            case_data_rw (proc->buffered_case, proc->last)->f = new_BY;
          casewriter_write (proc->output, proc->buffered_case);
        }
      else
        new_BY = true;

      proc->buffered_case = output;
      if (proc->first != NULL)
        case_data_rw (proc->buffered_case, proc->first)->f = new_BY;

      if (new_BY)
        {
          size_t n_values = subcase_get_n_fields (&proc->by_vars);
          const struct caseproto *proto = subcase_get_proto (&proc->by_vars);
          if (proc->prev_BY == NULL)
            {
              proc->prev_BY = xmalloc (n_values * sizeof *proc->prev_BY);
              caseproto_init_values (proto, proc->prev_BY);
            }
          caseproto_copy (subcase_get_proto (&proc->by_vars), 0, n_values,
                          proc->prev_BY, by);
        }
    }
}

/* Writes a trailing buffered case to the output, if FIRST or
   LAST is in use. */
static void
output_buffered_case (struct comb_proc *proc)
{
  if (proc->prev_BY != NULL)
    {
      if (proc->last != NULL)
        case_data_rw (proc->buffered_case, proc->last)->f = 1.0;
      casewriter_write (proc->output, proc->buffered_case);
      proc->buffered_case = NULL;
    }
}
