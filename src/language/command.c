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
#include "message.h"
#include "command.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include "alloc.h"
#include "dictionary.h"
#include "message.h"
#include "lexer.h"
#include "settings.h"
#include "manager.h"
#include "str.h"
#include "table.h"
#include "variable.h"
#include "procedure.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Global variables. */

/* A STATE_* constant giving the current program state. */
int pgm_state;

/* Static variables. */

/* A single command. */
struct command
  {
    const char *name;		/* Command name. */
    int transition[4];		/* Transitions to make from each state. */
    int (*func) (void);		/* Function to call. */
    int skip_entire_name;       /* If zero, we don't skip the
                                   final token in the command name. */
    short debug;                /* Set if this cmd available only in test mode*/
  };

/* Define the command array. */
#define DEFCMD(NAME, T1, T2, T3, T4, FUNC)		\
	{NAME, {T1, T2, T3, T4}, FUNC, 1, 0},
#define DBGCMD(NAME, T1, T2, T3, T4, FUNC)		\
	{NAME, {T1, T2, T3, T4}, FUNC, 1, 1},
#define SPCCMD(NAME, T1, T2, T3, T4, FUNC)		\
	{NAME, {T1, T2, T3, T4}, FUNC, 0, 0},
#define UNIMPL(NAME, T1, T2, T3, T4, DESC)		\
	{NAME, {T1, T2, T3, T4}, NULL, 1, 0},
static const struct command commands[] = 
  {
#include "command.def"
  };
#undef DEFCMD
#undef DBGCMD
#undef UNIMPL


/* Complete the line using the name of a command, 
 * based upon the current prg_state
 */
char * 
pspp_completion_function (const char *text,   int state)
{
  static int skip=0;
  const struct command *cmd = 0;
  
  for(;;)
    {
      if ( state + skip >= sizeof(commands)/ sizeof(struct command))
	{
	  skip = 0;
	  return 0;
	}

      cmd = &commands[state + skip];
  
      if ( cmd->transition[pgm_state] == STATE_ERROR || ( cmd->debug  &&  ! get_testing_mode () ) ) 
	{
	  skip++; 
	  continue;
	}
      
      if ( text == 0 || 0 == strncasecmp (cmd->name, text, strlen(text)))
	{
	  break;
	}

      skip++;
    }
  

  return xstrdup(cmd->name);
}



#define COMMAND_CNT (sizeof commands / sizeof *commands)

/* Command parser. */

static const struct command *parse_command_name (void);

/* Determines whether command C is appropriate to call in this
   part of a FILE TYPE structure. */
static int
FILE_TYPE_okay (const struct command *c UNUSED)
#if 0
{
  int okay = 0;
  
  if (c->func != cmd_record_type
      && c->func != cmd_data_list
      && c->func != cmd_repeating_data
      && c->func != cmd_end_file_type)
    msg (SE, _("%s not allowed inside FILE TYPE/END FILE TYPE."), c->name);
  /* FIXME */
  else if (c->func == cmd_repeating_data && fty.type == FTY_GROUPED)
    msg (SE, _("%s not allowed inside FILE TYPE GROUPED/END FILE TYPE."),
	 c->name);
  else if (!fty.had_rec_type && c->func != cmd_record_type)
    msg (SE, _("RECORD TYPE must be the first command inside a "
		      "FILE TYPE structure."));
  else
    okay = 1;

  if (c->func == cmd_record_type)
    fty.had_rec_type = 1;

  return okay;
}
#else
{
  return 1;
}
#endif

/* Parses an entire PSPP command.  This includes everything from the
   command name to the terminating dot.  Does most of its work by
   passing it off to the respective command dispatchers.  Only called
   by parse() in main.c. */
int
cmd_parse (void)
{
  const struct command *cp;	/* Iterator used to find the proper command. */

#if C_ALLOCA
  /* The generic alloca package performs garbage collection when it is
     called with an argument of zero. */
  alloca (0);
#endif /* C_ALLOCA */

  /* Null commands can result from extra empty lines. */
  if (token == '.')
    return CMD_SUCCESS;

  /* Parse comments. */
  if ((token == T_ID && !strcasecmp (tokid, "COMMENT"))
      || token == T_EXP || token == '*' || token == '[')
    {
      lex_skip_comment ();
      return CMD_SUCCESS;
    }

  /* Otherwise the line must begin with a command name, which is
     always an ID token. */
  if (token != T_ID)
    {
      lex_error (_("expecting command name"));
      return CMD_FAILURE;
    }

  /* Parse the command name. */
  cp = parse_command_name ();
  if (cp == NULL)
    return CMD_FAILURE;
  if (cp->func == NULL)
    {
      msg (SE, _("%s is not yet implemented."), cp->name);
      while (token && token != '.')
	lex_get ();
      return CMD_SUCCESS;
    }

  /* If we're in a FILE TYPE structure, only certain commands can be
     allowed. */
  if (pgm_state == STATE_INPUT
      && case_source_is_class (vfm_source, &file_type_source_class)
      && !FILE_TYPE_okay (cp))
    return CMD_FAILURE;

  /* Certain state transitions are not allowed.  Check for these. */
  assert (pgm_state >= 0 && pgm_state < STATE_ERROR);
  if (cp->transition[pgm_state] == STATE_ERROR)
    {
      static const char *state_name[4] =
      {
	N_("%s is not allowed (1) before a command to specify the "
	   "input program, such as DATA LIST, (2) between FILE TYPE "
	   "and END FILE TYPE, (3) between INPUT PROGRAM and END "
	   "INPUT PROGRAM."),
	N_("%s is not allowed within an input program."),
	N_("%s is only allowed within an input program."),
	N_("%s is only allowed within an input program."),
      };

      msg (SE, gettext (state_name[pgm_state]), cp->name);
      return CMD_FAILURE;
    }

  /* The structured output manager numbers all its tables.  Increment
     the major table number for each separate procedure. */
  som_new_series ();
  
  {
    int result;
    
    /* Call the command dispatcher. */
    err_set_command_name (cp->name);
    tab_set_command_name (cp->name);
    result = cp->func ();
    err_set_command_name (NULL);
    tab_set_command_name (NULL);
    
    /* Perform the state transition if the command completed
       successfully (at least in part). */
    if (result != CMD_FAILURE && result != CMD_CASCADING_FAILURE)
      {
	pgm_state = cp->transition[pgm_state];

	if (pgm_state == STATE_ERROR)
	  {
	    discard_variables ();
	    pgm_state = STATE_INIT;
	  }
      }

    /* Pass the command's success value up to the caller. */
    return result;
  }
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

/* Returns nonzero if strings A and B can be confused based on
   their first three letters. */
static int
conflicting_3char_prefixes (const char *a, const char *b) 
{
  size_t aw_len, bw_len;
  const char *aw, *bw;

  aw = find_word (a, &aw_len);
  bw = find_word (b, &bw_len);
  assert (aw != NULL && bw != NULL);

  /* Words that are the same don't conflict. */
  if (aw_len == bw_len && !buf_compare_case (aw, bw, aw_len))
    return 0;
  
  /* Words that are otherwise the same in the first three letters
     do conflict. */
  return ((aw_len > 3 && bw_len > 3)
          || (aw_len == 3 && bw_len > 3)
          || (bw_len == 3 && aw_len > 3)) && !buf_compare_case (aw, bw, 3);
}

/* Returns nonzero if CMD can be confused with another command
   based on the first three letters of its first word. */
static int
conflicting_3char_prefix_command (const struct command *cmd) 
{
  assert (cmd >= commands && cmd < commands + COMMAND_CNT);

  return ((cmd > commands
           && conflicting_3char_prefixes (cmd[-1].name, cmd[0].name))
          || (cmd < commands + COMMAND_CNT
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
  for (cmd = commands; cmd < commands + COMMAND_CNT; cmd++) 
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
  
  for (cmd = commands; cmd < commands + COMMAND_CNT; cmd++) 
    if (cmd_match_words (cmd, words, word_cnt, NULL) == COMPLETE_MATCH) 
      return cmd; 
  
  return NULL;
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
unknown_command_error (char *const words[], size_t word_cnt) 
{
  size_t idx;
  size_t words_len;
  char *name, *cp;

  words_len = 0;
  for (idx = 0; idx < word_cnt; idx++)
    words_len += strlen (words[idx]);

  cp = name = xmalloc (words_len + word_cnt + 16);
  for (idx = 0; idx < word_cnt; idx++) 
    {
      if (idx != 0)
        *cp++ = ' ';
      cp = stpcpy (cp, words[idx]);
    }
  *cp = '\0';

  msg (SE, _("Unknown command %s."), name);

  free (name);
}


/* Parse the command name and return a pointer to the corresponding
   struct command if successful.
   If not successful, return a null pointer. */
static const struct command *
parse_command_name (void)
{
  char *words[16];
  int word_cnt;
  int complete_word_cnt;
  int dash_possible;

  dash_possible = 0;
  word_cnt = complete_word_cnt = 0;
  while (token == T_ID || (dash_possible && token == '-')) 
    {
      int cmd_match_cnt;
      
      assert (word_cnt < sizeof words / sizeof *words);
      if (token == T_ID)
        words[word_cnt] = xstrdup (ds_c_str (&tokstr));
      else
        words[word_cnt] = xstrdup ("-");
      str_uppercase (words[word_cnt]);
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
              if (command->skip_entire_name)
                lex_get ();
	      if ( command->debug & !get_testing_mode () ) 
		goto error;
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
      lex_get ();
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
      if (!command->skip_entire_name)
        pushback_word_cnt--;
      
      /* FIXME: We only support one-token pushback. */
      assert (pushback_word_cnt + 1 >= word_cnt);

      while (word_cnt > pushback_word_cnt) 
        {
          word_cnt--;
          if (strcmp (words[word_cnt], "-")) 
            lex_put_back_id (words[word_cnt]);
          else
            lex_put_back ('-');
          free (words[word_cnt]);
        }

      if ( command->debug && !get_testing_mode () ) 
	goto error;

      free_words (words, word_cnt);
      return command;
    }

error:
  unknown_command_error (words, word_cnt);
  free_words (words, word_cnt);
  return NULL;
}

/* Simple commands. */

/* Parse and execute FINISH command. */
int
cmd_finish (void)
{
  return CMD_EOF;
}

/* Parses the N command. */
int
cmd_n_of_cases (void)
{
  /* Value for N. */
  int x;

  if (!lex_force_int ())
    return CMD_FAILURE;
  x = lex_integer ();
  lex_get ();
  if (!lex_match_id ("ESTIMATED"))
    dict_set_case_limit (default_dict, x);

  return lex_end_of_command ();
}

/* Parses, performs the EXECUTE procedure. */
int
cmd_execute (void)
{
  if (!procedure (NULL, NULL))
    return CMD_CASCADING_FAILURE;
  return lex_end_of_command ();
}

/* Parses, performs the ERASE command. */
int
cmd_erase (void)
{
  if (get_safer_mode ()) 
    { 
      msg (SE, _("This command not allowed when the SAFER option is set.")); 
      return CMD_FAILURE; 
    } 
  
  if (!lex_force_match_id ("FILE"))
    return CMD_FAILURE;
  lex_match ('=');
  if (!lex_force_string ())
    return CMD_FAILURE;

  if (remove (ds_c_str (&tokstr)) == -1)
    {
      msg (SW, _("Error removing `%s': %s."),
	   ds_c_str (&tokstr), strerror (errno));
      return CMD_FAILURE;
    }

  return CMD_SUCCESS;
}

#ifdef unix
/* Spawn a shell process. */
static int
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
      return 0;

    default:
      assert (pid > 0);
      while (wait (NULL) != pid)
	;
      return 1;
    }
}
#endif /* unix */

/* Parses the HOST command argument and executes the specified
   command.  Returns a suitable command return code. */
static int
run_command (void)
{
  const char *cmd;
  int string;

  /* Handle either a string argument or a full-line argument. */
  {
    int c = lex_look_ahead ();

    if (c == '\'' || c == '"')
      {
	lex_get ();
	if (!lex_force_string ())
	  return CMD_FAILURE;
	cmd = ds_c_str (&tokstr);
	string = 1;
      }
    else
      {
	cmd = lex_rest_of_line (NULL);
        lex_discard_line ();
	string = 0;
      }
  }

  /* Execute the command. */
  if (system (cmd) == -1)
    msg (SE, _("Error executing command: %s."), strerror (errno));

  /* Finish parsing. */
  if (string)
    {
      lex_get ();

      if (token != '.')
	{
	  lex_error (_("expecting end of command"));
	  return CMD_TRAILING_GARBAGE;
	}
    }
  else
    token = '.';

  return CMD_SUCCESS;
}

/* Parses, performs the HOST command. */
int
cmd_host (void)
{
  int code;

  if (get_safer_mode ()) 
    { 
      msg (SE, _("This command not allowed when the SAFER option is set.")); 
      return CMD_FAILURE; 
    } 

#ifdef unix
  /* Figure out whether to invoke an interactive shell or to execute a
     single shell command. */
  if (lex_look_ahead () == '.')
    {
      lex_get ();
      code = shell () ? CMD_PART_SUCCESS_MAYBE : CMD_SUCCESS;
    }
  else
    code = run_command ();
#else /* !unix */
  /* Make sure that the system has a command interpreter, then run a
     command. */
  if (system (NULL) != 0)
    code = run_command ();
  else
    {
      msg (SE, _("No operating system support for this command."));
      code = CMD_FAILURE;
    }
#endif /* !unix */

  return code;
}

/* Parses, performs the NEW FILE command. */
int
cmd_new_file (void)
{
  discard_variables ();

  return lex_end_of_command ();
}

/* Parses, performs the CLEAR TRANSFORMATIONS command. */
int
cmd_clear_transformations (void)
{
  cancel_transformations ();
  /* FIXME: what about variables created by transformations?
     They need to be properly initialized. */

  return CMD_SUCCESS;
}
