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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <assert.h>
#include "command.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include "alloc.h"
#include "error.h"
#include "getline.h"
#include "lexer.h"
#include "main.h"
#include "settings.h"
#include "som.h"
#include "str.h"
#include "tab.h"
#include "var.h"
#include "vfm.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "debug-print.h"

/* Global variables. */

/* A STATE_* constant giving the current program state. */
int pgm_state;

/* The name of the procedure currently executing, if any. */
const char *cur_proc;

/* Static variables. */

/* A single command. */
struct command
  {
    /* Initialized statically. */
    char cmd[22];		/* Command name. */
    int transition[4];		/* Transitions to make from each state. */
    int (*func) (void);		/* Function to call. */

    /* Calculated at startup time. */
    char *word[3];		/* cmd[], divided into individual words. */
    struct command *next;	/* Next command with same word[0]. */
  };

/* Define the command array. */
#define DEFCMD(NAME, T1, T2, T3, T4, FUNC)		\
	{NAME, {T1, T2, T3, T4}, FUNC, {NULL, NULL, NULL}, NULL},
#define UNIMPL(NAME, T1, T2, T3, T4)			\
	{NAME, {T1, T2, T3, T4}, NULL, {NULL, NULL, NULL}, NULL},
static struct command cmd_table[] = 
  {
#include "command.def"
    {"", {ERRO, ERRO, ERRO, ERRO}, NULL, {NULL, NULL, NULL}, NULL},
  };
#undef DEFCMD
#undef UNIMPL

/* Command parser. */

static struct command *figure_out_command (void);

/* Breaks the `cmd' member of C into individual words and sets C's
   word[] member appropriately. */
static void
split_words (struct command *c)
{
  char *cmd, *save;
  int i;

  cmd = xstrdup (c->cmd);
  for (i = 0; i < 3; i++)
    cmd = c->word[i] = strtok_r (i == 0 ? cmd : NULL, " -", &save);
}

/* Initializes the command parser. */
void
cmd_init (void)
{
  struct command *c;

  /* Break up command names into words. */
  for (c = cmd_table; c->cmd[0]; c++)
    split_words (c);

  /* Make chains of commands having the same first word. */
  for (c = cmd_table; c->cmd[0]; c++)
    {
      struct command *first;
      for (first = c; c[1].word[0] && !strcmp (c[0].word[0], c[1].word[0]); c++)
	c->next = c + 1;

      c->next = NULL;
    }
}

/* Determines whether command C is appropriate to call in this
   part of a FILE TYPE structure. */
static int
FILE_TYPE_okay (struct command *c)
{
  int okay = 0;
  
  if (c->func != cmd_record_type
      && c->func != cmd_data_list
      && c->func != cmd_repeating_data
      && c->func != cmd_end_file_type)
    msg (SE, _("%s not allowed inside FILE TYPE/END FILE TYPE."), c->cmd);
#if 0
  /* FIXME */
  else if (c->func == cmd_repeating_data && fty.type == FTY_GROUPED)
    msg (SE, _("%s not allowed inside FILE TYPE GROUPED/END FILE TYPE."),
	 c->cmd);
  else if (!fty.had_rec_type && c->func != cmd_record_type)
    msg (SE, _("RECORD TYPE must be the first command inside a "
		      "FILE TYPE structure."));
#endif
  else
    okay = 1;

#if 0
  if (c->func == cmd_record_type)
    fty.had_rec_type = 1;
#endif

  return okay;
}

/* Parses an entire PSPP command.  This includes everything from the
   command name to the terminating dot.  Does most of its work by
   passing it off to the respective command dispatchers.  Only called
   by parse() in main.c. */
int
cmd_parse (void)
{
  struct command *cp;	/* Iterator used to find the proper command. */

#if C_ALLOCA
  /* The generic alloca package performs garbage collection when it is
     called with an argument of zero. */
  alloca (0);
#endif /* C_ALLOCA */

  /* Null commands can result from extra empty lines. */
  if (token == '.')
    return CMD_SUCCESS;

  /* Parse comments. */
  if ((token == T_ID && !strcmp (tokid, "COMMENT"))
      || token == T_EXP || token == '*' || token == '[')
    {
      lex_skip_comment ();
      return CMD_SUCCESS;
    }

  /* Otherwise the line must begin with a command name, which is
     always an ID token. */
  if (token != T_ID)
    {
      msg (SE, _("This line does not begin with a valid command name."));
      return CMD_FAILURE;
    }

  /* Parse the command name. */
  cp = figure_out_command ();
  if (cp == NULL)
    return CMD_FAILURE;
  if (cp->func == NULL)
    {
      msg (SE, _("%s is not yet implemented."), cp->cmd);
      while (token && token != '.')
	lex_get ();
      return CMD_SUCCESS;
    }

  /* If we're in a FILE TYPE structure, only certain commands can be
     allowed. */
  if (pgm_state == STATE_INPUT && vfm_source == &file_type_source
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

      msg (SE, gettext (state_name[pgm_state]), cp->cmd);
      return CMD_FAILURE;
    }

#if DEBUGGING
  if (cp->func != cmd_remark)
    printf (_("%s command beginning\n"), cp->cmd);
#endif

  /* The structured output manager numbers all its tables.  Increment
     the major table number for each separate procedure. */
  som_new_series ();
  
  {
    int result;
    
    /* Call the command dispatcher.  Save and restore the name of
       the current command around this call. */
    {
      const char *prev_proc;
      
      prev_proc = cur_proc;
      cur_proc = cp->cmd;
      result = cp->func ();
      cur_proc = prev_proc;
    }
    
    /* Perform the state transition if the command completed
       successfully (at least in part). */
    if (result != CMD_FAILURE)
      {
	pgm_state = cp->transition[pgm_state];

	if (pgm_state == STATE_ERROR)
	  {
	    discard_variables ();
	    pgm_state = STATE_INIT;
	  }
      }

#if DEBUGGING
    if (cp->func != cmd_remark)
      printf (_("%s command completed\n\n"), cp->cmd);
#endif

    /* Pass the command's success value up to the caller. */
    return result;
  }
}

/* Parse the command name and return a pointer to the corresponding
   struct command if successful.
   If not successful, return a null pointer. */
static struct command *
figure_out_command (void)
{
  static const char *unk =
    N_("The identifier(s) specified do not form a valid command name:");

  static const char *inc = 
    N_("The identifier(s) specified do not form a complete command name:");

  struct command *cp;

  /* Parse the INCLUDE short form.
     Note that `@' is a valid character in identifiers. */
  if (tokid[0] == '@')
    return &cmd_table[0];

  /* Find a command whose first word matches this identifier.
     If it is the only command that begins with this word, return
     it. */
  for (cp = cmd_table; cp->cmd[0]; cp++)
    if (lex_id_match (cp->word[0], tokid))
      break;

  if (cp->cmd[0] == '\0')
    {
      msg (SE, "%s %s.", gettext (unk), ds_value (&tokstr));
      return NULL;
    }

  if (cp->next == NULL)
    return cp;
  
  /* We know that there is more than one command starting with this
     word.  Read the next word in the command name. */
  {
    struct command *ocp = cp;
    
    /* Verify that the next token is an identifier, because we
       must disambiguate this command name. */
    lex_get ();
    if (token != T_ID)
      {
	/* If there's a command whose name is the first word only,
	   return it.  This happens with, i.e., PRINT vs. PRINT
	   SPACE. */
	if (ocp->word[1] == NULL)
	  return ocp;
	
	msg (SE, "%s %s.", gettext (inc), ds_value (&tokstr));
	return NULL;
      }

    for (; cp; cp = cp->next)
      if (cp->word[1] && lex_id_match (cp->word[1], tokid))
	break;

    if (cp == NULL)
      {
	/* No match.  If there's a command whose name is the first
	   word only, return it.  This happens with, i.e., PRINT
	   vs. PRINT SPACE. */
	if (ocp->word[1] == NULL)
	  return ocp;
	
	msg (SE, "%s %s %s.", gettext (unk), ocp->word[0], tokid);
	return NULL;
      }
  
    /* Check whether the next token is an identifier.
       If not, bail. */
    if (!isalpha ((unsigned char) (lex_look_ahead ())))
      {
	/* Check whether there is an unambiguous interpretation.
	   If not, give an error. */
	if (cp->word[2]
	    && cp->next
	    && !strcmp (cp->word[1], cp->next->word[1]))
	  {
	    msg (SE, "%s %s %s.", gettext (inc), ocp->word[0], ocp->word[1]);
	    return NULL;
	  }
	else
	  return cp;
      }
  }
  
  /* If this command can have a third word, disambiguate based on it. */
  if (cp->word[2]
      || (cp->next
	  && cp->next->word[2]
	  && !strcmp (cp->word[1], cp->next->word[1])))
    {
      struct command *ocp = cp;
      
      lex_get ();
      assert (token == T_ID);

      /* Try to find a command with this third word.
	 If found, bail. */
      for (; cp; cp = cp->next)
	if (cp->word[2]
	    && !strcmp (cp->word[1], ocp->word[1])
	    && lex_id_match (cp->word[2], tokid))
	  break;

      if (cp != NULL)
	return cp;

      /* If no command with this third word found, make sure that
	 there's a command with those first two words but without a
	 third word. */
      cp = ocp;
      if (cp->word[2])
	{
	  msg (SE, "%s %s %s %s.",
	       gettext (unk), ocp->word[0], ocp->word[1], ds_value (&tokstr));
	  return 0;
	}
    }

  return cp;
}

/* Simple commands. */

/* Parse and execute EXIT command. */
int
cmd_exit (void)
{
  if (getl_reading_script)
    {
      msg (SE, _("This command is not accepted in a syntax file.  "
	   "Instead, use FINISH to terminate a syntax file."));
      lex_get ();
    }
  else
    finished = 1;

  return CMD_SUCCESS;
}

/* Parse and execute FINISH command. */
int
cmd_finish (void)
{
  /* Do not check for `.'
     Do not fetch any extra tokens. */
  if (getl_interactive)
    {
      msg (SM, _("This command is not executed "
	   "in interactive mode.  Instead, PSPP drops "
	   "down to the command prompt.  Use EXIT if you really want "
	   "to quit."));
      getl_close_all ();
    }
  else
    finished = 1;

  return CMD_SUCCESS;
}

/* Extracts a null-terminated 8-or-fewer-character PREFIX from STRING.
   PREFIX is converted to lowercase.  Removes trailing spaces from
   STRING as a side effect.  */
static void
extract_prefix (char *string, char *prefix)
{
  /* Length of STRING. */
  int len;

  /* Points to the null terminator in STRING (`end pointer'). */
  char *ep;

  /* Strip spaces from end of STRING. */
  len = strlen (string);
  while (len && isspace ((unsigned char) string[len - 1]))
    string[--len] = 0;

  /* Find null terminator. */
  ep = memchr (string, '\0', 8);
  if (!ep)
    ep = &string[8];

  /* Copy prefix, converting to lowercase. */
  while (string < ep)
    *prefix++ = tolower ((unsigned char) (*string++));
  *prefix = 0;
}

/* Prints STRING on the console and to the listing file, replacing \n
   by newline. */
static void
output_line (char *string)
{
  /* Location of \n in line read in. */
  char *cp;

  cp = strstr (string, "\\n");
  while (cp)
    {
      *cp = 0;
      tab_output_text (TAB_LEFT | TAT_NOWRAP, string);
      string = &cp[2];
      cp = strstr (string, "\\n");
    }
  tab_output_text (TAB_LEFT | TAT_NOWRAP, string);
}

/* Parse and execute REMARK command. */
int
cmd_remark ()
{
  /* Points to the line read in. */
  char *s;

  /* Index into s. */
  char *cp;

  /* 8-character sentinel used to terminate remark. */
  char sentinel[9];

  /* Beginning of line used to compare with SENTINEL. */
  char prefix[9];

  som_blank_line ();
  
  s = lex_rest_of_line (NULL);
  if (*s == '-')
    {
      output_line (&s[1]);
      return CMD_SUCCESS;
    }

  /* Read in SENTINEL from end of current line. */
  cp = s;
  while (isspace ((unsigned char) *cp))
    cp++;
  extract_prefix (cp, sentinel);
  if (sentinel[0] == 0)
    {
      msg (SE, _("The sentinel may not be the empty string."));
      return CMD_FAILURE;
    }

  /* Read in other lines until we encounter the sentinel. */
  while (getl_read_line ())
    {
      extract_prefix (ds_value (&getl_buf), prefix);
      if (!strcmp (sentinel, prefix))
	break;

      /* Output the line. */
      output_line (ds_value (&getl_buf));
    }

  /* Calling lex_entire_line() forces the sentinel line to be
     discarded. */
  getl_prompt = GETL_PRPT_STANDARD;
  lex_entire_line ();

  return CMD_SUCCESS;
}

/* Parses the N command. */
int
cmd_n_of_cases (void)
{
  /* Value for N. */
  int x;

  lex_match_id ("N");
  lex_match_id ("OF");
  lex_match_id ("CASES");
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
  lex_match_id ("EXECUTE");
  procedure (NULL, NULL, NULL);
  return lex_end_of_command ();
}


#define assert_not_safer() \
  do { \
   if (set_safer) \
    { \
      msg (SE, _("This command not allowed when the SAFER option is set.")); \
      return CMD_FAILURE; \
    } \
} while(0) 



/* Parses, performs the ERASE command. */
int
cmd_erase (void)
{

  assert_not_safer();
  
  lex_match_id ("ERASE");
  if (!lex_force_match_id ("FILE"))
    return CMD_FAILURE;
  lex_match ('=');
  if (!lex_force_string ())
    return CMD_FAILURE;

  if (remove (ds_value (&tokstr)) == -1)
    {
      msg (SW, _("Error removing `%s': %s."),
	   ds_value (&tokstr), strerror (errno));
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

	err_hcf (1);
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
  char *cmd;
  int string;

  /* Handle either a string argument or a full-line argument. */
  {
    int c = lex_look_ahead ();

    if (c == '\'' || c == '"')
      {
	lex_get ();
	if (!lex_force_string ())
	  return CMD_FAILURE;
	cmd = ds_value (&tokstr);
	string = 1;
      }
    else
      {
	cmd = lex_rest_of_line (NULL);
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

  assert_not_safer();
  
  lex_match_id ("HOST");

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
    success = run_command ();
  else
    {
      msg (SE, _("No operating system support for this command."));
      success = CMD_FAILURE;
    }
#endif /* !unix */

  return code ? CMD_FAILURE : CMD_SUCCESS;
}

/* Parses, performs the NEW FILE command. */
int
cmd_new_file (void)
{
  lex_match_id ("NEW");
  lex_match_id ("FILE");
  
  discard_variables ();

  return lex_end_of_command ();
}

/* Parses, performs the CLEAR TRANSFORMATIONS command. */
int
cmd_clear_transformations (void)
{
  lex_match_id ("CLEAR");
  lex_match_id ("TRANSFORMATIONS");

  if (getl_reading_script)
    {
      msg (SW, _("This command is not valid in a syntax file."));
      return CMD_FAILURE;
    }

  cancel_transformations ();

  return CMD_SUCCESS;
}
