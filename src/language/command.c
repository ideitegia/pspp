/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#include <language/command.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include <data/casereader.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <data/variable.h>
#include <language/lexer/lexer.h>
#include <language/prompt.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/str.h>
#include <output/manager.h>
#include <output/table.h>

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#if HAVE_READLINE
#include <readline/readline.h>
#endif

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Returns true if RESULT is a valid "enum cmd_result",
   false otherwise. */
static inline bool
cmd_result_is_valid (enum cmd_result result)
{
  return (result == CMD_SUCCESS || result == CMD_EOF || result == CMD_FINISH
          || (result >= CMD_PRIVATE_FIRST && result <= CMD_PRIVATE_LAST)
          || result == CMD_FAILURE || result == CMD_NOT_IMPLEMENTED
          || result == CMD_CASCADING_FAILURE);
}

/* Returns true if RESULT indicates success,
   false otherwise. */
bool
cmd_result_is_success (enum cmd_result result)
{
  assert (cmd_result_is_valid (result));
  return result > 0;
}

/* Returns true if RESULT indicates failure,
   false otherwise. */
bool
cmd_result_is_failure (enum cmd_result result)
{
  assert (cmd_result_is_valid (result));
  return result < 0;
}

/* Command processing states. */
enum states
  {
    S_INITIAL = 0x01,         /* Allowed before active file defined. */
    S_DATA = 0x02,            /* Allowed after active file defined. */
    S_INPUT_PROGRAM = 0x04,   /* Allowed in INPUT PROGRAM. */
    S_FILE_TYPE = 0x08,       /* Allowed in FILE TYPE. */
    S_ANY = 0x0f              /* Allowed anywhere. */
  };

/* Other command requirements. */
enum flags
  {
    F_ENHANCED = 0x10,        /* Allowed only in enhanced syntax mode. */
    F_TESTING = 0x20,         /* Allowed only in testing mode. */
    F_KEEP_FINAL_TOKEN = 0x40,/* Don't skip final token in command name. */
    F_ABBREV = 0x80           /* Not a candidate for name completion. */
  };

/* A single command. */
struct command
  {
    enum states states;         /* States in which command is allowed. */
    enum flags flags;           /* Other command requirements. */
    const char *name;		/* Command name. */
    int (*function) (struct lexer *, struct dataset *);	/* Function to call. */
  };

/* Define the command array. */
#define DEF_CMD(STATES, FLAGS, NAME, FUNCTION) {STATES, FLAGS, NAME, FUNCTION},
#define UNIMPL_CMD(NAME, DESCRIPTION) {S_ANY, 0, NAME, NULL},
static const struct command commands[] =
  {
#include "command.def"
  };
#undef DEF_CMD
#undef UNIMPL_CMD

static const size_t command_cnt = sizeof commands / sizeof *commands;

static bool in_correct_state (const struct command *, enum cmd_state);
static bool report_state_mismatch (const struct command *, enum cmd_state);
static const struct command *find_command (const char *name);
static void set_completion_state (enum cmd_state);

/* Command parser. */

static const struct command *parse_command_name (struct lexer *lexer);
static enum cmd_result do_parse_command (struct lexer *, struct dataset *, enum cmd_state);

/* Parses an entire command, from command name to terminating
   dot.  On failure, skips to the terminating dot.
   Returns the command's success or failure result. */
enum cmd_result
cmd_parse_in_state (struct lexer *lexer, struct dataset *ds,
		    enum cmd_state state)
{
  int result;

  som_new_series ();

  result = do_parse_command (lexer, ds, state);
  if (cmd_result_is_failure (result))
    lex_discard_rest_of_command (lexer);

  assert (!proc_is_open (ds));
  unset_cmd_algorithm ();
  dict_clear_aux (dataset_dict (ds));
  if (!dataset_end_of_command (ds))
    result = CMD_CASCADING_FAILURE;

  return result;
}

enum cmd_result
cmd_parse (struct lexer *lexer, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);
  return cmd_parse_in_state (lexer, ds,
			     proc_has_active_file (ds) &&
			     dict_get_var_cnt (dict) > 0 ?
			     CMD_STATE_DATA : CMD_STATE_INITIAL);
}


/* Parses an entire command, from command name to terminating
   dot. */
static enum cmd_result
do_parse_command (struct lexer *lexer, struct dataset *ds, enum cmd_state state)
{
  const struct command *command;
  enum cmd_result result;

  /* Read the command's first token. */
  prompt_set_style (PROMPT_FIRST);
  set_completion_state (state);
  lex_get (lexer);
  if (lex_token (lexer) == T_STOP)
    return CMD_EOF;
  else if (lex_token (lexer) == '.')
    {
      /* Null commands can result from extra empty lines. */
      return CMD_SUCCESS;
    }
  prompt_set_style (PROMPT_LATER);

  /* Parse the command name. */
  command = parse_command_name (lexer);
  if (command == NULL)
    return CMD_FAILURE;
  else if (command->function == NULL)
    {
      msg (SE, _("%s is unimplemented."), command->name);
      return CMD_NOT_IMPLEMENTED;
    }
  else if ((command->flags & F_TESTING) && !get_testing_mode ())
    {
      msg (SE, _("%s may be used only in testing mode."), command->name);
      return CMD_FAILURE;
    }
  else if ((command->flags & F_ENHANCED) && get_syntax () != ENHANCED)
    {
      msg (SE, _("%s may be used only in enhanced syntax mode."),
           command->name);
      return CMD_FAILURE;
    }
  else if (!in_correct_state (command, state))
    {
      report_state_mismatch (command, state);
      return CMD_FAILURE;
    }

  /* Execute command. */
  msg_set_command_name (command->name);
  tab_set_command_name (command->name);
  result = command->function (lexer, ds);
  tab_set_command_name (NULL);
  msg_set_command_name (NULL);

  assert (cmd_result_is_valid (result));
  return result;
}

static size_t
match_strings (const char *a, size_t a_len,
               const char *b, size_t b_len)
{
  size_t match_len = 0;

  while (a_len > 0 && b_len > 0)
    {
      /* Mismatch always returns zero. */
      if (toupper ((unsigned char) *a++) != toupper ((unsigned char) *b++))
        return 0;

      /* Advance. */
      a_len--;
      b_len--;
      match_len++;
    }

  return match_len;
}

/* Returns the first character in the first word in STRING,
   storing the word's length in *WORD_LEN.  If no words remain,
   returns a null pointer and stores 0 in *WORD_LEN.  Words are
   sequences of alphanumeric characters or single
   non-alphanumeric characters.  Words are delimited by
   spaces. */
static const char *
find_word (const char *string, size_t *word_len)
{
  /* Skip whitespace and asterisks. */
  while (isspace ((unsigned char) *string))
    string++;

  /* End of string? */
  if (*string == '\0')
    {
      *word_len = 0;
      return NULL;
    }

  /* Special one-character word? */
  if (!isalnum ((unsigned char) *string))
    {
      *word_len = 1;
      return string;
    }

  /* Alphanumeric word. */
  *word_len = 1;
  while (isalnum ((unsigned char) string[*word_len]))
    (*word_len)++;

  return string;
}

/* Returns true if strings A and B can be confused based on
   their first three letters. */
static bool
conflicting_3char_prefixes (const char *a, const char *b)
{
  size_t aw_len, bw_len;
  const char *aw, *bw;

  aw = find_word (a, &aw_len);
  bw = find_word (b, &bw_len);
  assert (aw != NULL && bw != NULL);

  /* Words that are the same don't conflict. */
  if (aw_len == bw_len && !buf_compare_case (aw, bw, aw_len))
    return false;

  /* Words that are otherwise the same in the first three letters
     do conflict. */
  return ((aw_len > 3 && bw_len > 3)
          || (aw_len == 3 && bw_len > 3)
          || (bw_len == 3 && aw_len > 3)) && !buf_compare_case (aw, bw, 3);
}

/* Returns true if CMD can be confused with another command
   based on the first three letters of its first word. */
static bool
conflicting_3char_prefix_command (const struct command *cmd)
{
  assert (cmd >= commands && cmd < commands + command_cnt);

  return ((cmd > commands
           && conflicting_3char_prefixes (cmd[-1].name, cmd[0].name))
          || (cmd < commands + command_cnt
              && conflicting_3char_prefixes (cmd[0].name, cmd[1].name)));
}

/* Ways that a set of words can match a command name. */
enum command_match
  {
    MISMATCH,           /* Not a match. */
    PARTIAL_MATCH,      /* The words begin the command name. */
    COMPLETE_MATCH      /* The words are the command name. */
  };

/* Figures out how well the WORD_CNT words in WORDS match CMD,
   and returns the appropriate enum value.  If WORDS are a
   partial match for CMD and the next word in CMD is a dash, then
   *DASH_POSSIBLE is set to 1 if DASH_POSSIBLE is non-null;
   otherwise, *DASH_POSSIBLE is unchanged. */
static enum command_match
cmd_match_words (const struct command *cmd,
                 char *const words[], size_t word_cnt,
                 int *dash_possible)
{
  const char *word;
  size_t word_len;
  size_t word_idx;

  for (word = find_word (cmd->name, &word_len), word_idx = 0;
       word != NULL && word_idx < word_cnt;
       word = find_word (word + word_len, &word_len), word_idx++)
    if (word_len != strlen (words[word_idx])
        || buf_compare_case (word, words[word_idx], word_len))
      {
        size_t match_chars = match_strings (word, word_len,
                                            words[word_idx],
                                            strlen (words[word_idx]));
        if (match_chars == 0)
          {
            /* Mismatch. */
            return MISMATCH;
          }
        else if (match_chars == 1 || match_chars == 2)
          {
            /* One- and two-character abbreviations are not
               acceptable. */
            return MISMATCH;
          }
        else if (match_chars == 3)
          {
            /* Three-character abbreviations are acceptable
               in the first word of a command if there are
               no name conflicts.  They are always
               acceptable after the first word. */
            if (word_idx == 0 && conflicting_3char_prefix_command (cmd))
              return MISMATCH;
          }
        else /* match_chars > 3 */
          {
            /* Four-character and longer abbreviations are
               always acceptable.  */
          }
      }

  if (word == NULL && word_idx == word_cnt)
    {
      /* cmd->name = "FOO BAR", words[] = {"FOO", "BAR"}. */
      return COMPLETE_MATCH;
    }
  else if (word == NULL)
    {
      /* cmd->name = "FOO BAR", words[] = {"FOO", "BAR", "BAZ"}. */
      return MISMATCH;
    }
  else
    {
      /* cmd->name = "FOO BAR BAZ", words[] = {"FOO", "BAR"}. */
      if (word[0] == '-' && dash_possible != NULL)
        *dash_possible = 1;
      return PARTIAL_MATCH;
    }
}

/* Returns the number of commands for which the WORD_CNT words in
   WORDS are a partial or complete match.  If some partial match
   has a dash as the next word, then *DASH_POSSIBLE is set to 1,
   otherwise it is set to 0. */
static int
count_matching_commands (char *const words[], size_t word_cnt,
                         int *dash_possible)
{
  const struct command *cmd;
  int cmd_match_count;

  cmd_match_count = 0;
  *dash_possible = 0;
  for (cmd = commands; cmd < commands + command_cnt; cmd++)
    if (cmd_match_words (cmd, words, word_cnt, dash_possible) != MISMATCH)
      cmd_match_count++;

  return cmd_match_count;
}

/* Returns the command for which the WORD_CNT words in WORDS are
   a complete match.  Returns a null pointer if no such command
   exists. */
static const struct command *
get_complete_match (char *const words[], size_t word_cnt)
{
  const struct command *cmd;

  for (cmd = commands; cmd < commands + command_cnt; cmd++)
    if (cmd_match_words (cmd, words, word_cnt, NULL) == COMPLETE_MATCH)
      return cmd;

  return NULL;
}

/* Returns the command with the given exact NAME.
   Aborts if no such command exists. */
static const struct command *
find_command (const char *name)
{
  const struct command *cmd;

  for (cmd = commands; cmd < commands + command_cnt; cmd++)
    if (!strcmp (cmd->name, name))
      return cmd;
  NOT_REACHED ();
}

/* Frees the WORD_CNT words in WORDS. */
static void
free_words (char *words[], size_t word_cnt)
{
  size_t idx;

  for (idx = 0; idx < word_cnt; idx++)
    free (words[idx]);
}

/* Flags an error that the command whose name is given by the
   WORD_CNT words in WORDS is unknown. */
static void
unknown_command_error (struct lexer *lexer, char *const words[], size_t word_cnt)
{
  if (word_cnt == 0)
    lex_error (lexer, _("expecting command name"));
  else
    {
      struct string s;
      size_t i;

      ds_init_empty (&s);
      for (i = 0; i < word_cnt; i++)
        {
          if (i != 0)
            ds_put_char (&s, ' ');
          ds_put_cstr (&s, words[i]);
        }

      msg (SE, _("Unknown command %s."), ds_cstr (&s));

      ds_destroy (&s);
    }
}

/* Parse the command name and return a pointer to the corresponding
   struct command if successful.
   If not successful, return a null pointer. */
static const struct command *
parse_command_name (struct lexer *lexer)
{
  char *words[16];
  int word_cnt;
  int complete_word_cnt;
  int dash_possible;

  if (lex_token (lexer) == T_EXP ||
		  lex_token (lexer) == '*' || lex_token (lexer) == '[')
    return find_command ("COMMENT");

  dash_possible = 0;
  word_cnt = complete_word_cnt = 0;
  while (lex_token (lexer) == T_ID || (dash_possible && lex_token (lexer) == '-'))
    {
      int cmd_match_cnt;

      assert (word_cnt < sizeof words / sizeof *words);
      if (lex_token (lexer) == T_ID)
        {
          words[word_cnt] = ds_xstrdup (lex_tokstr (lexer));
          str_uppercase (words[word_cnt]);
        }
      else if (lex_token (lexer) == '-')
        words[word_cnt] = xstrdup ("-");
      word_cnt++;

      cmd_match_cnt = count_matching_commands (words, word_cnt,
                                               &dash_possible);
      if (cmd_match_cnt == 0)
        break;
      else if (cmd_match_cnt == 1)
        {
          const struct command *command = get_complete_match (words, word_cnt);
          if (command != NULL)
            {
              if (!(command->flags & F_KEEP_FINAL_TOKEN))
                lex_get (lexer);
              free_words (words, word_cnt);
              return command;
            }
        }
      else /* cmd_match_cnt > 1 */
        {
          /* Do we have a complete command name so far? */
          if (get_complete_match (words, word_cnt) != NULL)
            complete_word_cnt = word_cnt;
        }
      lex_get (lexer);
    }

  /* If we saw a complete command name earlier, drop back to
     it. */
  if (complete_word_cnt)
    {
      int pushback_word_cnt;
      const struct command *command;

      /* Get the command. */
      command = get_complete_match (words, complete_word_cnt);
      assert (command != NULL);

      /* Figure out how many words we want to keep.
         We normally want to swallow the entire command. */
      pushback_word_cnt = complete_word_cnt + 1;
      if (command->flags & F_KEEP_FINAL_TOKEN)
        pushback_word_cnt--;

      /* FIXME: We only support one-token pushback. */
      assert (pushback_word_cnt + 1 >= word_cnt);

      while (word_cnt > pushback_word_cnt)
        {
          word_cnt--;
          if (strcmp (words[word_cnt], "-"))
            lex_put_back_id (lexer, words[word_cnt]);
          else
            lex_put_back (lexer, '-');
          free (words[word_cnt]);
        }

      free_words (words, word_cnt);
      return command;
    }

  /* We didn't get a valid command name. */
  unknown_command_error (lexer, words, word_cnt);
  free_words (words, word_cnt);
  return NULL;
}

/* Returns true if COMMAND is allowed in STATE,
   false otherwise. */
static bool
in_correct_state (const struct command *command, enum cmd_state state)
{
  return ((state == CMD_STATE_INITIAL && command->states & S_INITIAL)
          || (state == CMD_STATE_DATA && command->states & S_DATA)
          || (state == CMD_STATE_INPUT_PROGRAM
              && command->states & S_INPUT_PROGRAM)
          || (state == CMD_STATE_FILE_TYPE && command->states & S_FILE_TYPE));
}

/* Emits an appropriate error message for trying to invoke
   COMMAND in STATE. */
static bool
report_state_mismatch (const struct command *command, enum cmd_state state)
{
  assert (!in_correct_state (command, state));
  if (state == CMD_STATE_INITIAL || state == CMD_STATE_DATA)
    {
      const char *allowed[3];
      int allowed_cnt;
      char *s;

      allowed_cnt = 0;
      if (command->states & S_INITIAL)
        allowed[allowed_cnt++] = _("before the active file has been defined");
      else if (command->states & S_DATA)
        allowed[allowed_cnt++] = _("after the active file has been defined");
      if (command->states & S_INPUT_PROGRAM)
        allowed[allowed_cnt++] = _("inside INPUT PROGRAM");
      if (command->states & S_FILE_TYPE)
        allowed[allowed_cnt++] = _("inside FILE TYPE");

      if (allowed_cnt == 1)
        s = xstrdup (allowed[0]);
      else if (allowed_cnt == 2)
        s = xasprintf (_("%s or %s"), allowed[0], allowed[1]);
      else if (allowed_cnt == 3)
        s = xasprintf (_("%s, %s, or %s"), allowed[0], allowed[1], allowed[2]);
      else
        NOT_REACHED ();

      msg (SE, _("%s is allowed only %s."), command->name, s);

      free (s);
    }
  else if (state == CMD_STATE_INPUT_PROGRAM)
    msg (SE, _("%s is not allowed inside INPUT PROGRAM."), command->name);
  else if (state == CMD_STATE_FILE_TYPE)
    msg (SE, _("%s is not allowed inside FILE TYPE."), command->name);

  return false;
}

/* Command name completion. */

static enum cmd_state completion_state = CMD_STATE_INITIAL;

static void
set_completion_state (enum cmd_state state)
{
  completion_state = state;
}

/* Returns the next possible completion of a command name that
   begins with PREFIX, in the current command state, or a null
   pointer if no completions remain.
   Before calling the first time, set *CMD to a null pointer. */
const char *
cmd_complete (const char *prefix, const struct command **cmd)
{
  if (*cmd == NULL)
    *cmd = commands;

  for (; *cmd < commands + command_cnt; (*cmd)++)
    if (!memcasecmp ((*cmd)->name, prefix, strlen (prefix))
        && (!((*cmd)->flags & F_TESTING) || get_testing_mode ())
        && (!((*cmd)->flags & F_ENHANCED) || get_syntax () == ENHANCED)
        && !((*cmd)->flags & F_ABBREV)
        && ((*cmd)->function != NULL)
        && in_correct_state (*cmd, completion_state))
      return (*cmd)++->name;

  return NULL;
}

/* Simple commands. */

/* Parse and execute FINISH command. */
int
cmd_finish (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  return CMD_FINISH;
}

/* Parses the N command. */
int
cmd_n_of_cases (struct lexer *lexer, struct dataset *ds)
{
  /* Value for N. */
  int x;

  if (!lex_force_int (lexer))
    return CMD_FAILURE;
  x = lex_integer (lexer);
  lex_get (lexer);
  if (!lex_match_id (lexer, "ESTIMATED"))
    dict_set_case_limit (dataset_dict (ds), x);

  return lex_end_of_command (lexer);
}

/* Parses, performs the EXECUTE procedure. */
int
cmd_execute (struct lexer *lexer, struct dataset *ds)
{
  bool ok = casereader_destroy (proc_open (ds));
  if (!proc_commit (ds) || !ok)
    return CMD_CASCADING_FAILURE;
  return lex_end_of_command (lexer);
}

/* Parses, performs the ERASE command. */
int
cmd_erase (struct lexer *lexer, struct dataset *ds UNUSED)
{
  if (get_safer_mode ())
    {
      msg (SE, _("This command not allowed when the SAFER option is set."));
      return CMD_FAILURE;
    }

  if (!lex_force_match_id (lexer, "FILE"))
    return CMD_FAILURE;
  lex_match (lexer, '=');
  if (!lex_force_string (lexer))
    return CMD_FAILURE;

  if (remove (ds_cstr (lex_tokstr (lexer))) == -1)
    {
      msg (SW, _("Error removing `%s': %s."),
	   ds_cstr (lex_tokstr (lexer)), strerror (errno));
      return CMD_FAILURE;
    }

  return CMD_SUCCESS;
}

#if HAVE_FORK && HAVE_EXECL
/* Spawn an interactive shell process. */
static bool
shell (void)
{
  int pid;

  pid = fork ();
  switch (pid)
    {
    case 0:
      {
	const char *shell_fn;
	char *shell_process;

	{
	  int i;

	  for (i = 3; i < 20; i++)
	    close (i);
	}

	shell_fn = getenv ("SHELL");
	if (shell_fn == NULL)
	  shell_fn = "/bin/sh";

	{
	  const char *cp = strrchr (shell_fn, '/');
	  cp = cp ? &cp[1] : shell_fn;
	  shell_process = local_alloc (strlen (cp) + 8);
	  strcpy (shell_process, "-");
	  strcat (shell_process, cp);
	  if (strcmp (cp, "sh"))
	    shell_process[0] = '+';
	}

	execl (shell_fn, shell_process, NULL);

	_exit (1);
      }

    case -1:
      msg (SE, _("Couldn't fork: %s."), strerror (errno));
      return false;

    default:
      assert (pid > 0);
      while (wait (NULL) != pid)
	;
      return true;
    }
}
#else /* !(HAVE_FORK && HAVE_EXECL) */
/* Don't know how to spawn an interactive shell. */
static bool
shell (void)
{
  msg (SE, _("Interactive shell not supported on this platform."));
  return false;
}
#endif

/* Executes the specified COMMAND in a subshell.  Returns true if
   successful, false otherwise. */
static bool
run_command (const char *command)
{
  if (system (NULL) == 0)
    {
      msg (SE, _("Command shell not supported on this platform."));
      return false;
    }

  /* Execute the command. */
  if (system (command) == -1)
    msg (SE, _("Error executing command: %s."), strerror (errno));

  return true;
}

/* Parses, performs the HOST command. */
int
cmd_host (struct lexer *lexer, struct dataset *ds UNUSED)
{
  int look_ahead;

  if (get_safer_mode ())
    {
      msg (SE, _("This command not allowed when the SAFER option is set."));
      return CMD_FAILURE;
    }

  look_ahead = lex_look_ahead (lexer);
  if (look_ahead == '.')
    {
      lex_get (lexer);
      return shell () ? CMD_SUCCESS : CMD_FAILURE;
    }
  else if (look_ahead == '\'' || look_ahead == '"')
    {
      bool ok;

      lex_get (lexer);
      if (!lex_force_string (lexer))
        NOT_REACHED ();
      ok = run_command (ds_cstr (lex_tokstr (lexer)));

      lex_get (lexer);
      return ok ? lex_end_of_command (lexer) : CMD_FAILURE;
    }
  else
    {
      bool ok = run_command (lex_rest_of_line (lexer));
      lex_discard_line (lexer);
      return ok ? CMD_SUCCESS : CMD_FAILURE;
    }
}

/* Parses, performs the NEW FILE command. */
int
cmd_new_file (struct lexer *lexer, struct dataset *ds)
{
  proc_discard_active_file (ds);

  return lex_end_of_command (lexer);
}

/* Parses a comment. */
int
cmd_comment (struct lexer *lexer, struct dataset *ds UNUSED)
{
  lex_skip_comment (lexer);
  return CMD_SUCCESS;
}
