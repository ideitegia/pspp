/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010 Free Software Foundation, Inc.

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
#include "lexer.h"
#include <libpspp/message.h>
#include <c-ctype.h>
#include <c-strtod.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <libpspp/assertion.h>
#include <language/command.h>
#include <libpspp/message.h>
#include <data/settings.h>
#include <libpspp/getl.h>
#include <libpspp/str.h>
#include <output/journal.h>
#include <output/text-item.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct lexer
{
  struct string line_buffer;

  struct source_stream *ss;

  int token;      /* Current token. */
  double tokval;  /* T_POS_NUM, T_NEG_NUM: the token's value. */

  char tokid [VAR_NAME_LEN + 1];   /* T_ID: the identifier. */

  struct string tokstr;   /* T_ID, T_STRING: token string value.
  			    For T_ID, this is not truncated as is
  			    tokid. */

  char *prog; /* Pointer to next token in line_buffer. */
  bool dot;   /* True only if this line ends with a terminal dot. */

  int put_token ; /* If nonzero, next token returned by lex_get().
  		    Used only in exceptional circumstances. */

  struct string put_tokstr;
  double put_tokval;
};


static int parse_id (struct lexer *);

/* How a string represents its contents. */
enum string_type
  {
    CHARACTER_STRING,   /* Characters. */
    BINARY_STRING,      /* Binary digits. */
    OCTAL_STRING,       /* Octal digits. */
    HEX_STRING          /* Hexadecimal digits. */
  };

static int parse_string (struct lexer *, enum string_type);

/* Initialization. */

/* Initializes the lexer. */
struct lexer *
lex_create (struct source_stream *ss)
{
  struct lexer *lexer = xzalloc (sizeof (*lexer));

  ds_init_empty (&lexer->tokstr);
  ds_init_empty (&lexer->put_tokstr);
  ds_init_empty (&lexer->line_buffer);
  lexer->ss = ss;

  return lexer;
}

struct source_stream *
lex_get_source_stream (const struct lexer *lex)
{
  return lex->ss;
}

enum syntax_mode
lex_current_syntax_mode (const struct lexer *lex)
{
  return source_stream_current_syntax_mode (lex->ss);
}

enum error_mode
lex_current_error_mode (const struct lexer *lex)
{
  return source_stream_current_error_mode (lex->ss);
}


void
lex_destroy (struct lexer *lexer)
{
  if ( NULL != lexer )
    {
      ds_destroy (&lexer->put_tokstr);
      ds_destroy (&lexer->tokstr);
      ds_destroy (&lexer->line_buffer);

      free (lexer);
    }
}


/* Common functions. */

/* Copies put_token, lexer->put_tokstr, put_tokval into token, tokstr,
   tokval, respectively, and sets tokid appropriately. */
static void
restore_token (struct lexer *lexer)
{
  assert (lexer->put_token != 0);
  lexer->token = lexer->put_token;
  ds_assign_string (&lexer->tokstr, &lexer->put_tokstr);
  str_copy_trunc (lexer->tokid, sizeof lexer->tokid, ds_cstr (&lexer->tokstr));
  lexer->tokval = lexer->put_tokval;
  lexer->put_token = 0;
}

/* Copies token, tokstr, lexer->tokval into lexer->put_token, put_tokstr,
   put_lexer->tokval respectively. */
static void
save_token (struct lexer *lexer)
{
  lexer->put_token = lexer->token;
  ds_assign_string (&lexer->put_tokstr, &lexer->tokstr);
  lexer->put_tokval = lexer->tokval;
}

/* Parses a single token, setting appropriate global variables to
   indicate the token's attributes. */
void
lex_get (struct lexer *lexer)
{
  /* Find a token. */
  for (;;)
    {
      if (NULL == lexer->prog && ! lex_get_line (lexer) )
	{
	  lexer->token = T_STOP;
	  return;
	}

      /* If a token was pushed ahead, return it. */
      if (lexer->put_token)
        {
          restore_token (lexer);
          return;
        }

      for (;;)
        {
          /* Skip whitespace. */
	  while (c_isspace ((unsigned char) *lexer->prog))
	    lexer->prog++;

	  if (*lexer->prog)
	    break;

	  if (lexer->dot)
	    {
	      lexer->dot = 0;
	      lexer->token = '.';
	      return;
	    }
	  else if (!lex_get_line (lexer))
	    {
	      lexer->prog = NULL;
	      lexer->token = T_STOP;
	      return;
	    }

	  if (lexer->put_token)
	    {
              restore_token (lexer);
	      return;
	    }
	}


      /* Actually parse the token. */
      ds_clear (&lexer->tokstr);

      switch (*lexer->prog)
	{
	case '-': case '.':
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
	  {
	    char *tail;

	    /* `-' can introduce a negative number, or it can be a
	       token by itself.  If it is not followed by a digit or a
	       decimal point, it is definitely not a number.
	       Otherwise, it might be either, but most of the time we
	       want it as a number.  When the syntax calls for a `-'
	       token, lex_negative_to_dash() must be used to break
	       negative numbers into two tokens. */
	    if (*lexer->prog == '-')
	      {
		ds_put_char (&lexer->tokstr, *lexer->prog++);
		while (c_isspace ((unsigned char) *lexer->prog))
		  lexer->prog++;

		if (!c_isdigit ((unsigned char) *lexer->prog) && *lexer->prog != '.')
		  {
		    lexer->token = '-';
		    break;
		  }
                lexer->token = T_NEG_NUM;
	      }
            else
              lexer->token = T_POS_NUM;

	    /* Parse the number, copying it into tokstr. */
	    while (c_isdigit ((unsigned char) *lexer->prog))
	      ds_put_char (&lexer->tokstr, *lexer->prog++);
	    if (*lexer->prog == '.')
	      {
		ds_put_char (&lexer->tokstr, *lexer->prog++);
		while (c_isdigit ((unsigned char) *lexer->prog))
		  ds_put_char (&lexer->tokstr, *lexer->prog++);
	      }
	    if (*lexer->prog == 'e' || *lexer->prog == 'E')
	      {
		ds_put_char (&lexer->tokstr, *lexer->prog++);
		if (*lexer->prog == '+' || *lexer->prog == '-')
		  ds_put_char (&lexer->tokstr, *lexer->prog++);
		while (c_isdigit ((unsigned char) *lexer->prog))
		  ds_put_char (&lexer->tokstr, *lexer->prog++);
	      }

	    /* Parse as floating point. */
	    lexer->tokval = c_strtod (ds_cstr (&lexer->tokstr), &tail);
	    if (*tail)
	      {
		msg (SE, _("%s does not form a valid number."),
		     ds_cstr (&lexer->tokstr));
		lexer->tokval = 0.0;

		ds_clear (&lexer->tokstr);
		ds_put_char (&lexer->tokstr, '0');
	      }

	    break;
	  }

	case '\'': case '"':
	  lexer->token = parse_string (lexer, CHARACTER_STRING);
	  break;

	case '(': case ')': case ',': case '=': case '+': case '/':
        case '[': case ']':
	  lexer->token = *lexer->prog++;
	  break;

	case '*':
	  if (*++lexer->prog == '*')
	    {
	      lexer->prog++;
	      lexer->token = T_EXP;
	    }
	  else
	    lexer->token = '*';
	  break;

	case '<':
	  if (*++lexer->prog == '=')
	    {
	      lexer->prog++;
	      lexer->token = T_LE;
	    }
	  else if (*lexer->prog == '>')
	    {
	      lexer->prog++;
	      lexer->token = T_NE;
	    }
	  else
	    lexer->token = T_LT;
	  break;

	case '>':
	  if (*++lexer->prog == '=')
	    {
	      lexer->prog++;
	      lexer->token = T_GE;
	    }
	  else
	    lexer->token = T_GT;
	  break;

	case '~':
	  if (*++lexer->prog == '=')
	    {
	      lexer->prog++;
	      lexer->token = T_NE;
	    }
	  else
	    lexer->token = T_NOT;
	  break;

	case '&':
	  lexer->prog++;
	  lexer->token = T_AND;
	  break;

	case '|':
	  lexer->prog++;
	  lexer->token = T_OR;
	  break;

        case 'b': case 'B':
          if (lexer->prog[1] == '\'' || lexer->prog[1] == '"')
            lexer->token = parse_string (lexer, BINARY_STRING);
          else
            lexer->token = parse_id (lexer);
          break;

        case 'o': case 'O':
          if (lexer->prog[1] == '\'' || lexer->prog[1] == '"')
            lexer->token = parse_string (lexer, OCTAL_STRING);
          else
            lexer->token = parse_id (lexer);
          break;

        case 'x': case 'X':
          if (lexer->prog[1] == '\'' || lexer->prog[1] == '"')
            lexer->token = parse_string (lexer, HEX_STRING);
          else
            lexer->token = parse_id (lexer);
          break;

	default:
          if (lex_is_id1 (*lexer->prog))
            {
              lexer->token = parse_id (lexer);
              break;
            }
          else
            {
              unsigned char c = *lexer->prog++;
              char *c_name = xasprintf (c_isgraph (c) ? "%c" : "\\%o", c);
              msg (SE, _("Bad character in input: `%s'."), c_name);
              free (c_name);
              continue;
            }
        }
      break;
    }
}

/* Parses an identifier at the current position into tokid and
   tokstr.
   Returns the correct token type. */
static int
parse_id (struct lexer *lexer)
{
  struct substring rest_of_line
    = ss_substr (ds_ss (&lexer->line_buffer),
                 ds_pointer_to_position (&lexer->line_buffer, lexer->prog),
                 SIZE_MAX);
  struct substring id = ss_head (rest_of_line,
                                 lex_id_get_length (rest_of_line));
  lexer->prog += ss_length (id);

  ds_assign_substring (&lexer->tokstr, id);
  str_copy_trunc (lexer->tokid, sizeof lexer->tokid, ds_cstr (&lexer->tokstr));
  return lex_id_to_token (id);
}

/* Reports an error to the effect that subcommand SBC may only be
   specified once. */
void
lex_sbc_only_once (const char *sbc)
{
  msg (SE, _("Subcommand %s may only be specified once."), sbc);
}

/* Reports an error to the effect that subcommand SBC is
   missing. */
void
lex_sbc_missing (struct lexer *lexer, const char *sbc)
{
  lex_error (lexer, _("missing required subcommand %s"), sbc);
}

/* Prints a syntax error message containing the current token and
   given message MESSAGE (if non-null). */
void
lex_error (struct lexer *lexer, const char *message, ...)
{
  struct string s;

  ds_init_empty (&s);

  if (lexer->token == T_STOP)
    ds_put_cstr (&s, _("Syntax error at end of file"));
  else if (lexer->token == '.')
    ds_put_cstr (&s, _("Syntax error at end of command"));
  else
    {
      char *token_rep = lex_token_representation (lexer);
      ds_put_format (&s, _("Syntax error at `%s'"), token_rep);
      free (token_rep);
    }

  if (message)
    {
      va_list args;

      ds_put_cstr (&s, ": ");

      va_start (args, message);
      ds_put_vformat (&s, message, args);
      va_end (args);
    }

  msg (SE, "%s.", ds_cstr (&s));
  ds_destroy (&s);
}

/* Checks that we're at end of command.
   If so, returns a successful command completion code.
   If not, flags a syntax error and returns an error command
   completion code. */
int
lex_end_of_command (struct lexer *lexer)
{
  if (lexer->token != '.')
    {
      lex_error (lexer, _("expecting end of command"));
      return CMD_FAILURE;
    }
  else
    return CMD_SUCCESS;
}

/* Token testing functions. */

/* Returns true if the current token is a number. */
bool
lex_is_number (struct lexer *lexer)
{
  return lexer->token == T_POS_NUM || lexer->token == T_NEG_NUM;
}


/* Returns true if the current token is a string. */
bool
lex_is_string (struct lexer *lexer)
{
  return lexer->token == T_STRING;
}


/* Returns the value of the current token, which must be a
   floating point number. */
double
lex_number (struct lexer *lexer)
{
  assert (lex_is_number (lexer));
  return lexer->tokval;
}

/* Returns true iff the current token is an integer. */
bool
lex_is_integer (struct lexer *lexer)
{
  return (lex_is_number (lexer)
	  && lexer->tokval > LONG_MIN
	  && lexer->tokval <= LONG_MAX
	  && floor (lexer->tokval) == lexer->tokval);
}

/* Returns the value of the current token, which must be an
   integer. */
long
lex_integer (struct lexer *lexer)
{
  assert (lex_is_integer (lexer));
  return lexer->tokval;
}

/* Token matching functions. */

/* If TOK is the current token, skips it and returns true
   Otherwise, returns false. */
bool
lex_match (struct lexer *lexer, int t)
{
  if (lexer->token == t)
    {
      lex_get (lexer);
      return true;
    }
  else
    return false;
}

/* If the current token is the identifier S, skips it and returns
   true.  The identifier may be abbreviated to its first three
   letters.
   Otherwise, returns false. */
bool
lex_match_id (struct lexer *lexer, const char *s)
{
  return lex_match_id_n (lexer, s, 3);
}

/* If the current token is the identifier S, skips it and returns
   true.  The identifier may be abbreviated to its first N
   letters.
   Otherwise, returns false. */
bool
lex_match_id_n (struct lexer *lexer, const char *s, size_t n)
{
  if (lexer->token == T_ID
      && lex_id_match_n (ss_cstr (s), ss_cstr (lexer->tokid), n))
    {
      lex_get (lexer);
      return true;
    }
  else
    return false;
}

/* If the current token is integer N, skips it and returns true.
   Otherwise, returns false. */
bool
lex_match_int (struct lexer *lexer, int x)
{
  if (lex_is_integer (lexer) && lex_integer (lexer) == x)
    {
      lex_get (lexer);
      return true;
    }
  else
    return false;
}

/* Forced matches. */

/* If this token is identifier S, fetches the next token and returns
   nonzero.
   Otherwise, reports an error and returns zero. */
bool
lex_force_match_id (struct lexer *lexer, const char *s)
{
  if (lex_match_id (lexer, s))
    return true;
  else
    {
      lex_error (lexer, _("expecting `%s'"), s);
      return false;
    }
}

/* If the current token is T, skips the token.  Otherwise, reports an
   error and returns from the current function with return value false. */
bool
lex_force_match (struct lexer *lexer, int t)
{
  if (lexer->token == t)
    {
      lex_get (lexer);
      return true;
    }
  else
    {
      lex_error (lexer, _("expecting `%s'"), lex_token_name (t));
      return false;
    }
}

/* If this token is a string, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_string (struct lexer *lexer)
{
  if (lexer->token == T_STRING)
    return true;
  else
    {
      lex_error (lexer, _("expecting string"));
      return false;
    }
}

/* If this token is an integer, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_int (struct lexer *lexer)
{
  if (lex_is_integer (lexer))
    return true;
  else
    {
      lex_error (lexer, _("expecting integer"));
      return false;
    }
}

/* If this token is a number, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_num (struct lexer *lexer)
{
  if (lex_is_number (lexer))
    return true;

  lex_error (lexer, _("expecting number"));
  return false;
}

/* If this token is an identifier, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_id (struct lexer *lexer)
{
  if (lexer->token == T_ID)
    return true;

  lex_error (lexer, _("expecting identifier"));
  return false;
}

/* Weird token functions. */

/* Returns the first character of the next token, except that if the
   next token is not an identifier, the character returned will not be
   a character that can begin an identifier.  Specifically, the
   hexstring lead-in X' causes lookahead() to return '.  Note that an
   alphanumeric return value doesn't guarantee an ID token, it could
   also be a reserved-word token. */
int
lex_look_ahead (struct lexer *lexer)
{
  if (lexer->put_token)
    return lexer->put_token;

  for (;;)
    {
      if (NULL == lexer->prog && ! lex_get_line (lexer) )
        return 0;

      for (;;)
	{
	  while (c_isspace ((unsigned char) *lexer->prog))
	    lexer->prog++;
	  if (*lexer->prog)
	    break;

	  if (lexer->dot)
	    return '.';
	  else if (!lex_get_line (lexer))
            return 0;

	  if (lexer->put_token)
	    return lexer->put_token;
	}

      if ((toupper ((unsigned char) *lexer->prog) == 'X'
	   || toupper ((unsigned char) *lexer->prog) == 'B'
           || toupper ((unsigned char) *lexer->prog) == 'O')
	  && (lexer->prog[1] == '\'' || lexer->prog[1] == '"'))
	return '\'';

      return *lexer->prog;
    }
}

/* Makes the current token become the next token to be read; the
   current token is set to T. */
void
lex_put_back (struct lexer *lexer, int t)
{
  save_token (lexer);
  lexer->token = t;
}

/* Makes the current token become the next token to be read; the
   current token is set to the identifier ID. */
void
lex_put_back_id (struct lexer *lexer, const char *id)
{
  assert (lex_id_to_token (ss_cstr (id)) == T_ID);
  save_token (lexer);
  lexer->token = T_ID;
  ds_assign_cstr (&lexer->tokstr, id);
  str_copy_trunc (lexer->tokid, sizeof lexer->tokid, ds_cstr (&lexer->tokstr));
}

/* Weird line processing functions. */

/* Returns the entire contents of the current line. */
const char *
lex_entire_line (const struct lexer *lexer)
{
  return ds_cstr (&lexer->line_buffer);
}

const struct string *
lex_entire_line_ds (const struct lexer *lexer)
{
  return &lexer->line_buffer;
}

/* As lex_entire_line(), but only returns the part of the current line
   that hasn't already been tokenized. */
const char *
lex_rest_of_line (const struct lexer *lexer)
{
  return lexer->prog;
}

/* Returns true if the current line ends in a terminal dot,
   false otherwise. */
bool
lex_end_dot (const struct lexer *lexer)
{
  return lexer->dot;
}

/* Causes the rest of the current input line to be ignored for
   tokenization purposes. */
void
lex_discard_line (struct lexer *lexer)
{
  ds_cstr (&lexer->line_buffer);  /* Ensures ds_end points to something valid */
  lexer->prog = ds_end (&lexer->line_buffer);
  lexer->dot = false;
  lexer->put_token = 0;
}


/* Discards the rest of the current command.
   When we're reading commands from a file, we skip tokens until
   a terminal dot or EOF.
   When we're reading commands interactively from the user,
   that's just discarding the current line, because presumably
   the user doesn't want to finish typing a command that will be
   ignored anyway. */
void
lex_discard_rest_of_command (struct lexer *lexer)
{
  if (!getl_is_interactive (lexer->ss))
    {
      while (lexer->token != T_STOP && lexer->token != '.')
	lex_get (lexer);
    }
  else
    lex_discard_line (lexer);
}

/* Weird line reading functions. */

/* Remove C-style comments in STRING, begun by slash-star and
   terminated by star-slash or newline. */
static void
strip_comments (struct string *string)
{
  char *cp;
  int quote;
  bool in_comment;

  in_comment = false;
  quote = EOF;
  for (cp = ds_cstr (string); *cp; )
    {
      /* If we're not in a comment, check for quote marks. */
      if (!in_comment)
        {
          if (*cp == quote)
            quote = EOF;
          else if (*cp == '\'' || *cp == '"')
            quote = *cp;
        }

      /* If we're not inside a quotation, check for comment. */
      if (quote == EOF)
        {
          if (cp[0] == '/' && cp[1] == '*')
            {
              in_comment = true;
              *cp++ = ' ';
              *cp++ = ' ';
              continue;
            }
          else if (in_comment && cp[0] == '*' && cp[1] == '/')
            {
              in_comment = false;
              *cp++ = ' ';
              *cp++ = ' ';
              continue;
            }
        }

      /* Check commenting. */
      if (in_comment)
        *cp = ' ';
      cp++;
    }
}

/* Prepares LINE, which is subject to the given SYNTAX rules, for
   tokenization by stripping comments and determining whether it
   is the beginning or end of a command and storing into
   *LINE_STARTS_COMMAND and *LINE_ENDS_COMMAND appropriately. */
void
lex_preprocess_line (struct string *line,
                     enum syntax_mode syntax,
                     bool *line_starts_command,
                     bool *line_ends_command)
{
  strip_comments (line);
  ds_rtrim (line, ss_cstr (CC_SPACES));
  *line_ends_command = (ds_chomp (line, settings_get_endcmd ())
                        || (ds_is_empty (line) && settings_get_nulline ()));
  *line_starts_command = false;
  if (syntax == GETL_BATCH)
    {
      int first = ds_first (line);
      *line_starts_command = !c_isspace (first);
      if (first == '+' || first == '-')
        *ds_data (line) = ' ';
    }
}

/* Reads a line, without performing any preprocessing. */
bool
lex_get_line_raw (struct lexer *lexer)
{
  bool ok = getl_read_line (lexer->ss, &lexer->line_buffer);
  if (ok)
    {
      const char *line = ds_cstr (&lexer->line_buffer);
      text_item_submit (text_item_create (TEXT_ITEM_SYNTAX, line));
    }
  return ok;
}

/* Reads a line for use by the tokenizer, and preprocesses it by
   removing comments, stripping trailing whitespace and the
   terminal dot, and removing leading indentors. */
bool
lex_get_line (struct lexer *lexer)
{
  bool line_starts_command;

  if (!lex_get_line_raw (lexer))
    {
      lexer->prog = NULL;
      return false;
    }

  lex_preprocess_line (&lexer->line_buffer,
		       lex_current_syntax_mode (lexer),
                       &line_starts_command, &lexer->dot);

  if (line_starts_command)
    lexer->put_token = '.';

  lexer->prog = ds_cstr (&lexer->line_buffer);
  return true;
}

/* Token names. */

/* Returns the name of a token. */
const char *
lex_token_name (int token)
{
  if (lex_is_keyword (token))
    return lex_id_name (token);
  else if (token < 256)
    {
      static char t[256][2];
      char *s = t[token];
      s[0] = token;
      s[1] = '\0';
      return s;
    }
  else
    NOT_REACHED ();
}

/* Returns an ASCII representation of the current token as a
   malloc()'d string. */
char *
lex_token_representation (struct lexer *lexer)
{
  char *token_rep;

  switch (lexer->token)
    {
    case T_ID:
    case T_POS_NUM:
    case T_NEG_NUM:
      return ds_xstrdup (&lexer->tokstr);
      break;

    case T_STRING:
      {
	int hexstring = 0;
	char *sp, *dp;

	for (sp = ds_cstr (&lexer->tokstr); sp < ds_end (&lexer->tokstr); sp++)
	  if (!c_isprint ((unsigned char) *sp))
	    {
	      hexstring = 1;
	      break;
	    }

	token_rep = xmalloc (2 + ds_length (&lexer->tokstr) * 2 + 1 + 1);

	dp = token_rep;
	if (hexstring)
	  *dp++ = 'X';
	*dp++ = '\'';

	if (!hexstring)
	  for (sp = ds_cstr (&lexer->tokstr); *sp; )
	    {
	      if (*sp == '\'')
		*dp++ = '\'';
	      *dp++ = (unsigned char) *sp++;
	    }
	else
	  for (sp = ds_cstr (&lexer->tokstr); sp < ds_end (&lexer->tokstr); sp++)
	    {
	      *dp++ = (((unsigned char) *sp) >> 4)["0123456789ABCDEF"];
	      *dp++ = (((unsigned char) *sp) & 15)["0123456789ABCDEF"];
	    }
	*dp++ = '\'';
	*dp = '\0';

	return token_rep;
      }
    break;

    case T_STOP:
      token_rep = xmalloc (1);
      *token_rep = '\0';
      return token_rep;

    case T_EXP:
      return xstrdup ("**");

    default:
      return xstrdup (lex_token_name (lexer->token));
    }

  NOT_REACHED ();
}

/* Really weird functions. */

/* Most of the time, a `-' is a lead-in to a negative number.  But
   sometimes it's actually part of the syntax.  If a dash can be part
   of syntax then this function is called to rip it off of a
   number. */
void
lex_negative_to_dash (struct lexer *lexer)
{
  if (lexer->token == T_NEG_NUM)
    {
      lexer->token = T_POS_NUM;
      lexer->tokval = -lexer->tokval;
      ds_assign_substring (&lexer->tokstr, ds_substr (&lexer->tokstr, 1, SIZE_MAX));
      save_token (lexer);
      lexer->token = '-';
    }
}

/* Skip a COMMENT command. */
void
lex_skip_comment (struct lexer *lexer)
{
  for (;;)
    {
      if (!lex_get_line (lexer))
        {
          lexer->put_token = T_STOP;
	  lexer->prog = NULL;
          return;
        }

      if (lexer->put_token == '.')
	break;

      ds_cstr (&lexer->line_buffer); /* Ensures ds_end will point to a valid char */
      lexer->prog = ds_end (&lexer->line_buffer);
      if (lexer->dot)
	break;
    }
}

/* Private functions. */

/* When invoked, tokstr contains a string of binary, octal, or
   hex digits, according to TYPE.  The string is converted to
   characters having the specified values. */
static void
convert_numeric_string_to_char_string (struct lexer *lexer,
				       enum string_type type)
{
  const char *base_name;
  int base;
  int chars_per_byte;
  size_t byte_cnt;
  size_t i;
  char *p;

  switch (type)
    {
    case BINARY_STRING:
      base_name = _("binary");
      base = 2;
      chars_per_byte = 8;
      break;
    case OCTAL_STRING:
      base_name = _("octal");
      base = 8;
      chars_per_byte = 3;
      break;
    case HEX_STRING:
      base_name = _("hex");
      base = 16;
      chars_per_byte = 2;
      break;
    default:
      NOT_REACHED ();
    }

  byte_cnt = ds_length (&lexer->tokstr) / chars_per_byte;
  if (ds_length (&lexer->tokstr) % chars_per_byte)
    msg (SE, _("String of %s digits has %zu characters, which is not a "
	       "multiple of %d."),
	 base_name, ds_length (&lexer->tokstr), chars_per_byte);

  p = ds_cstr (&lexer->tokstr);
  for (i = 0; i < byte_cnt; i++)
    {
      int value;
      int j;

      value = 0;
      for (j = 0; j < chars_per_byte; j++, p++)
	{
	  int v;

	  if (*p >= '0' && *p <= '9')
	    v = *p - '0';
	  else
	    {
	      static const char alpha[] = "abcdef";
	      const char *q = strchr (alpha, tolower ((unsigned char) *p));

	      if (q)
		v = q - alpha + 10;
	      else
		v = base;
	    }

	  if (v >= base)
	    msg (SE, _("`%c' is not a valid %s digit."), *p, base_name);

	  value = value * base + v;
	}

      ds_cstr (&lexer->tokstr)[i] = (unsigned char) value;
    }

  ds_truncate (&lexer->tokstr, byte_cnt);
}

/* Parses a string from the input buffer into tokstr.  The input
   buffer pointer lexer->prog must point to the initial single or double
   quote.  TYPE indicates the type of string to be parsed.
   Returns token type. */
static int
parse_string (struct lexer *lexer, enum string_type type)
{
  if (type != CHARACTER_STRING)
    lexer->prog++;

  /* Accumulate the entire string, joining sections indicated by +
     signs. */
  for (;;)
    {
      /* Single or double quote. */
      int c = *lexer->prog++;

      /* Accumulate section. */
      for (;;)
	{
	  /* Check end of line. */
	  if (*lexer->prog == '\0')
	    {
	      msg (SE, _("Unterminated string constant."));
	      goto finish;
	    }

	  /* Double quote characters to embed them in strings. */
	  if (*lexer->prog == c)
	    {
	      if (lexer->prog[1] == c)
		lexer->prog++;
	      else
		break;
	    }

	  ds_put_char (&lexer->tokstr, *lexer->prog++);
	}
      lexer->prog++;

      /* Skip whitespace after final quote mark. */
      if (lexer->prog == NULL)
	break;
      for (;;)
	{
	  while (c_isspace ((unsigned char) *lexer->prog))
	    lexer->prog++;
	  if (*lexer->prog)
	    break;

	  if (lexer->dot)
	    goto finish;

	  if (!lex_get_line (lexer))
            goto finish;
	}

      /* Skip plus sign. */
      if (*lexer->prog != '+')
	break;
      lexer->prog++;

      /* Skip whitespace after plus sign. */
      if (lexer->prog == NULL)
	break;
      for (;;)
	{
	  while (c_isspace ((unsigned char) *lexer->prog))
	    lexer->prog++;
	  if (*lexer->prog)
	    break;

	  if (lexer->dot)
	    goto finish;

	  if (!lex_get_line (lexer))
            {
              msg (SE, _("Unexpected end of file in string concatenation."));
              goto finish;
            }
	}

      /* Ensure that a valid string follows. */
      if (*lexer->prog != '\'' && *lexer->prog != '"')
	{
	  msg (SE, _("String expected following `+'."));
	  goto finish;
	}
    }

  /* We come here when we've finished concatenating all the string sections
     into one large string. */
finish:
  if (type != CHARACTER_STRING)
    convert_numeric_string_to_char_string (lexer, type);

  return T_STRING;
}

/* Token Accessor Functions */

int
lex_token (const struct lexer *lexer)
{
  return lexer->token;
}

double
lex_tokval (const struct lexer *lexer)
{
  return lexer->tokval;
}

const char *
lex_tokid (const struct lexer *lexer)
{
  return lexer->tokid;
}

const struct string *
lex_tokstr (const struct lexer *lexer)
{
  return &lexer->tokstr;
}

/* If the lexer is positioned at the (pseudo)identifier S, which
   may contain a hyphen ('-'), skips it and returns true.  Each
   half of the identifier may be abbreviated to its first three
   letters.
   Otherwise, returns false. */
bool
lex_match_hyphenated_word (struct lexer *lexer, const char *s)
{
  const char *hyphen = strchr (s, '-');
  if (hyphen == NULL)
    return lex_match_id (lexer, s);
  else if (lexer->token != T_ID
	   || !lex_id_match (ss_buffer (s, hyphen - s), ss_cstr (lexer->tokid))
	   || lex_look_ahead (lexer) != '-')
    return false;
  else
    {
      lex_get (lexer);
      lex_force_match (lexer, '-');
      lex_force_match_id (lexer, hyphen + 1);
      return true;
    }
}

