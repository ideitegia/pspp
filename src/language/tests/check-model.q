/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009 Free Software Foundation, Inc.

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

#include <language/tests/check-model.h>

#include <errno.h>

#include <language/lexer/lexer.h>
#include <libpspp/model-checker.h>

#include "error.h"
#include "fwriteerror.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* (headers) */

/* (specification)
   "CHECK MODEL" (chm_):
    search=strategy:broad/deep/random,
           :mxd(n:max_depth),
           :hash(n:hash_bits);
    path=integer list;
    queue=:limit(n:queue_limit,"%s>0"),
          drop:newest/oldest/random;
    seed=integer;
    stop=:states(n:max_unique_states,"%s>0"),
         :errors(n:max_errors),
         :timeout(d:time_limit,"%s>0");
    progress=progress:none/dots/fancy/verbose;
    output=:verbosity(n:verbosity),
           :errverbosity(n:err_verbosity),
           :file(s:output_file).
*/
/* (declarations) */
/* (functions) */

static struct mc_options *parse_options (struct lexer *);

/* Parses a syntax description of model checker options from
   LEXER and passes them, along with AUX, to the CHECKER
   function, which must wrap a call to mc_run and return the
   mc_results that it returned.  This function then prints a
   description of the mc_results to the output file.  Returns
   true if the model checker run found no errors, false
   otherwise. */
bool
check_model (struct lexer *lexer,
             struct mc_results *(*checker) (struct mc_options *, void *aux),
             void *aux)
{
  struct mc_options *options;
  struct mc_results *results;
  FILE *output_file;
  bool ok;

  options = parse_options (lexer);
  if (options == NULL)
    return false;
  output_file = mc_options_get_output_file (options);

  results = checker (options, aux);

  mc_results_print (results, output_file);

  if (output_file != stdout && output_file != stderr)
    {
      if (fwriteerror (output_file) < 0)
        {
          /* We've already discarded the name of the output file.
             Oh well. */
          error (0, errno, "error closing output file");
        }
    }

  ok = mc_results_get_error_count (results) == 0;
  mc_results_destroy (results);

  return ok;
}

/* Parses options from LEXER and returns a corresponding
   mc_options, or a null pointer if parsing fails. */
static struct mc_options *
parse_options (struct lexer *lexer)
{
  struct cmd_check_model cmd;
  struct mc_options *options;

  if (!parse_check_model (lexer, NULL, &cmd, NULL))
    return NULL;

  options = mc_options_create ();
  if (cmd.strategy != -1)
    mc_options_set_strategy (options,
                             cmd.strategy == CHM_BROAD ? MC_BROAD
                             : cmd.strategy == CHM_DEEP ? MC_DEEP
                             : cmd.strategy == CHM_RANDOM ? MC_RANDOM
                             : -1);
  if (cmd.sbc_path > 0)
    {
      if (cmd.sbc_search > 0)
        msg (SW, _("PATH and SEARCH subcommands are mutually exclusive.  "
                   "Ignoring PATH."));
      else
        {
          struct subc_list_int *list = &cmd.il_path[0];
          int count = subc_list_int_count (list);
          if (count > 0)
            {
              struct mc_path path;
              int i;

              mc_path_init (&path);
              for (i = 0; i < count; i++)
                mc_path_push (&path, subc_list_int_at (list, i));
              mc_options_set_follow_path (options, &path);
              mc_path_destroy (&path);
            }
          else
            msg (SW, _("At least one value must be specified on PATH."));
        }
    }
  if (cmd.max_depth != LONG_MIN)
    mc_options_set_max_depth (options, cmd.max_depth);
  if (cmd.hash_bits != LONG_MIN)
    {
      int hash_bits;
      mc_options_set_hash_bits (options, cmd.hash_bits);
      hash_bits = mc_options_get_hash_bits (options);
      if (hash_bits != cmd.hash_bits)
        msg (SW, _("Hash bits adjusted to %d."), hash_bits);
    }
  if (cmd.queue_limit != LONG_MIN)
    mc_options_set_queue_limit (options, cmd.queue_limit);
  if (cmd.drop != -1)
    {
      enum mc_queue_limit_strategy drop
        = (cmd.drop == CHM_NEWEST ? MC_DROP_NEWEST
           : cmd.drop == CHM_OLDEST ? MC_DROP_OLDEST
           : cmd.drop == CHM_RANDOM ? MC_DROP_RANDOM
           : -1);
      mc_options_set_queue_limit_strategy (options, drop);
    }
  if (cmd.sbc_search > 0)
    mc_options_set_seed (options, cmd.n_seed[0]);
  if (cmd.max_unique_states != LONG_MIN)
    mc_options_set_max_unique_states (options, cmd.max_unique_states);
  if (cmd.max_errors != LONG_MIN)
    mc_options_set_max_errors (options, cmd.max_errors);
  if (cmd.time_limit != SYSMIS)
    mc_options_set_time_limit (options, cmd.time_limit);
  if (cmd.verbosity != LONG_MIN)
    mc_options_set_verbosity (options, cmd.verbosity);
  if (cmd.err_verbosity != LONG_MIN)
    mc_options_set_failure_verbosity (options, cmd.err_verbosity);
  if (cmd.progress != -1)
    {
      if (cmd.progress == CHM_NONE)
        mc_options_set_progress_usec (options, 0);
      else if (cmd.progress == CHM_DOTS)
        mc_options_set_progress_func (options, mc_progress_dots);
      else if (cmd.progress == CHM_FANCY)
        mc_options_set_progress_func (options, mc_progress_fancy);
      else if (cmd.progress == CHM_VERBOSE)
        mc_options_set_progress_func (options, mc_progress_verbose);
    }
  if (cmd.output_file != NULL)
    {
      FILE *output_file = fopen (cmd.output_file, "w");
      if (output_file == NULL)
        {
          error (0, errno, _("error opening \"%s\" for writing"),
                 cmd.output_file);
          free_check_model (&cmd);
          mc_options_destroy (options);
          return NULL;
        }
      mc_options_set_output_file (options, output_file);
    }


  free_check_model (&cmd);

  return options;
}

/*
  Local Variables:
  mode: c
  End:
*/
