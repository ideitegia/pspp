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
#include "lexer.h"
#include <libpspp/message.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <language/command.h>
#include <libpspp/message.h>
#include <language/line-buffer.h>
#include <libpspp/magic.h>
#include <data/settings.h>
#include <libpspp/str.h>

#include "size_max.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/*
#define DUMP_TOKENS 1
*/


/* Current token. */
int token;

/* T_POS_NUM, T_NEG_NUM: the token's value. */
double tokval;

/* T_ID: the identifier. */
char tokid[LONG_NAME_LEN + 1];

/* T_ID, T_STRING: token string value.
   For T_ID, this is not truncated as is tokid. */
struct string tokstr;

/* Static variables. */

/* Pointer to next token in getl_buf. */
static char *prog;

/* True only if this line ends with a terminal dot. */
static bool dot;

/* True only if the last token returned was T_STOP. */
static bool eof;

/* If nonzero, next token returned by lex_get().
   Used only in exceptional circumstances. */
static int put_token;
static struct string put_tokstr;
static double put_tokval;

static int parse_id (void);

/* How a string represents its contents. */
enum string_type 
  {
    CHARACTER_STRING,   /* Characters. */
    BINARY_STRING,      /* Binary digits. */
    OCTAL_STRING,       /* Octal digits. */
    HEX_STRING          /* Hexadecimal digits. */
  };

static int parse_string (enum string_type);

#if DUMP_TOKENS
static void dump_token (void);
#endif

/* Initialization. */

/* Initializes the lexer. */
void
lex_init (void)
{
  ds_init_empty (&tokstr);
  ds_init_empty (&put_tokstr);
  if (!lex_get_line ())
    eof = true;
}

void
lex_done (void)
{
  ds_destroy (&put_tokstr);
  ds_destroy (&tokstr);
}


/* Common functions. */

/* Copies put_token, put_tokstr, put_tokval into token, tokstr,
   tokval, respectively, and sets tokid appropriately. */
static void
restore_token (void) 
{
  assert (put_token != 0);
  token = put_token;
  ds_assign_string (&tokstr, &put_tokstr);
  str_copy_trunc (tokid, sizeof tokid, ds_cstr (&tokstr));
  tokval = put_tokval;
  put_token = 0;
}

/* Copies token, tokstr, tokval into put_token, put_tokstr,
   put_tokval respectively. */
static void
save_token (void) 
{
  put_token = token;
  ds_assign_string (&put_tokstr, &tokstr);
  put_tokval = tokval;
}

/* Parses a single token, setting appropriate global variables to
   indicate the token's attributes. */
void
lex_get (void)
{
  /* If a token was pushed ahead, return it. */
  if (put_token)
    {
      restore_token ();
#if DUMP_TOKENS
      dump_token ();
#endif
      return;
    }

  /* Find a token. */
  for (;;)
    {
      /* Skip whitespace. */
      if (eof) 
        {
          token = T_STOP;
          return;
        }

      for (;;)
	{
	  while (isspace ((unsigned char) *prog))
	    prog++;
	  if (*prog)
	    break;

	  if (dot)
	    {
	      dot = 0;
	      token = '.';
#if DUMP_TOKENS
	      dump_token ();
#endif
	      return;
	    }
	  else if (!lex_get_line ())
	    {
	      eof = true;
	      token = T_STOP;
#if DUMP_TOKENS
	      dump_token ();
#endif
	      return;
	    }

	  if (put_token)
	    {
              restore_token ();
#if DUMP_TOKENS
	      dump_token ();
#endif
	      return;
	    }
	}


      /* Actually parse the token. */
      ds_clear (&tokstr);
      
      switch (*prog)
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
	    if (*prog == '-')
	      {
		ds_put_char (&tokstr, *prog++);
		while (isspace ((unsigned char) *prog))
		  prog++;

		if (!isdigit ((unsigned char) *prog) && *prog != '.')
		  {
		    token = '-';
		    break;
		  }
                token = T_NEG_NUM;
	      }
            else 
              token = T_POS_NUM;
                
	    /* Parse the number, copying it into tokstr. */
	    while (isdigit ((unsigned char) *prog))
	      ds_put_char (&tokstr, *prog++);
	    if (*prog == '.')
	      {
		ds_put_char (&tokstr, *prog++);
		while (isdigit ((unsigned char) *prog))
		  ds_put_char (&tokstr, *prog++);
	      }
	    if (*prog == 'e' || *prog == 'E')
	      {
		ds_put_char (&tokstr, *prog++);
		if (*prog == '+' || *prog == '-')
		  ds_put_char (&tokstr, *prog++);
		while (isdigit ((unsigned char) *prog))
		  ds_put_char (&tokstr, *prog++);
	      }

	    /* Parse as floating point. */
	    tokval = strtod (ds_cstr (&tokstr), &tail);
	    if (*tail)
	      {
		msg (SE, _("%s does not form a valid number."),
		     ds_cstr (&tokstr));
		tokval = 0.0;

		ds_clear (&tokstr);
		ds_put_char (&tokstr, '0');
	      }

	    break;
	  }

	case '\'': case '"':
	  token = parse_string (CHARACTER_STRING);
	  break;

	case '(': case ')': case ',': case '=': case '+': case '/':
	  token = *prog++;
	  break;

	case '*':
	  if (*++prog == '*')
	    {
	      prog++;
	      token = T_EXP;
	    }
	  else
	    token = '*';
	  break;

	case '<':
	  if (*++prog == '=')
	    {
	      prog++;
	      token = T_LE;
	    }
	  else if (*prog == '>')
	    {
	      prog++;
	      token = T_NE;
	    }
	  else
	    token = T_LT;
	  break;

	case '>':
	  if (*++prog == '=')
	    {
	      prog++;
	      token = T_GE;
	    }
	  else
	    token = T_GT;
	  break;

	case '~':
	  if (*++prog == '=')
	    {
	      prog++;
	      token = T_NE;
	    }
	  else
	    token = T_NOT;
	  break;

	case '&':
	  prog++;
	  token = T_AND;
	  break;

	case '|':
	  prog++;
	  token = T_OR;
	  break;

        case 'b': case 'B':
          if (prog[1] == '\'' || prog[1] == '"')
            token = parse_string (BINARY_STRING);
          else
            token = parse_id ();
          break;
          
        case 'o': case 'O':
          if (prog[1] == '\'' || prog[1] == '"')
            token = parse_string (OCTAL_STRING);
          else
            token = parse_id ();
          break;
          
        case 'x': case 'X':
          if (prog[1] == '\'' || prog[1] == '"')
            token = parse_string (HEX_STRING);
          else
            token = parse_id ();
          break;
          
	default:
          if (lex_is_id1 (*prog)) 
            {
              token = parse_id ();
              break; 
            }
          else
            {
              if (isgraph ((unsigned char) *prog))
                msg (SE, _("Bad character in input: `%c'."), *prog++);
              else
                msg (SE, _("Bad character in input: `\\%o'."), *prog++);
              continue; 
            }
        }
      break;
    }

#if DUMP_TOKENS
  dump_token ();
#endif
}

/* Parses an identifier at the current position into tokid and
   tokstr.
   Returns the correct token type. */
static int
parse_id (void) 
{
  const char *start = prog;
  prog = lex_skip_identifier (start);

  ds_put_substring (&tokstr, ss_buffer (start, prog - start));
  str_copy_trunc (tokid, sizeof tokid, ds_cstr (&tokstr));
  return lex_id_to_token (ds_cstr (&tokstr), ds_length (&tokstr));
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
lex_sbc_missing (const char *sbc) 
{
  lex_error (_("missing required subcommand %s"), sbc);
}

/* Prints a syntax error message containing the current token and
   given message MESSAGE (if non-null). */
void
lex_error (const char *message, ...)
{
  char *token_rep;
  char where[128];

  token_rep = lex_token_representation ();
  if (token == T_STOP)
    strcpy (where, "end of file");
  else if (token == '.')
    strcpy (where, "end of command");
  else
    snprintf (where, sizeof where, "`%s'", token_rep);
  free (token_rep);

  if (message)
    {
      char buf[1024];
      va_list args;
      
      va_start (args, message);
      vsnprintf (buf, 1024, message, args);
      va_end (args);

      msg (SE, _("Syntax error %s at %s."), buf, where);
    }
  else
    msg (SE, _("Syntax error at %s."), where);
}

/* Checks that we're at end of command.
   If so, returns a successful command completion code.
   If not, flags a syntax error and returns an error command
   completion code. */
int
lex_end_of_command (void)
{
  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      return CMD_FAILURE;
    }
  else
    return CMD_SUCCESS;
}

/* Token testing functions. */

/* Returns true if the current token is a number. */
bool
lex_is_number (void) 
{
  return token == T_POS_NUM || token == T_NEG_NUM;
}

/* Returns the value of the current token, which must be a
   floating point number. */
double
lex_number (void)
{
  assert (lex_is_number ());
  return tokval;
}

/* Returns true iff the current token is an integer. */
bool
lex_is_integer (void)
{
  return (lex_is_number ()
	  && tokval != NOT_LONG
	  && tokval >= LONG_MIN
	  && tokval <= LONG_MAX
	  && floor (tokval) == tokval);
}

/* Returns the value of the current token, which must be an
   integer. */
long
lex_integer (void)
{
  assert (lex_is_integer ());
  return tokval;
}
  
/* Token matching functions. */

/* If TOK is the current token, skips it and returns true
   Otherwise, returns false. */
bool
lex_match (int t)
{
  if (token == t)
    {
      lex_get ();
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
lex_match_id (const char *s)
{
  if (token == T_ID && lex_id_match (s, tokid))
    {
      lex_get ();
      return true;
    }
  else
    return false;
}

/* If the current token is integer N, skips it and returns true.
   Otherwise, returns false. */
bool
lex_match_int (int x)
{
  if (lex_is_integer () && lex_integer () == x)
    {
      lex_get ();
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
lex_force_match_id (const char *s)
{
  if (token == T_ID && lex_id_match (s, tokid))
    {
      lex_get ();
      return true;
    }
  else
    {
      lex_error (_("expecting `%s'"), s);
      return false;
    }
}

/* If the current token is T, skips the token.  Otherwise, reports an
   error and returns from the current function with return value false. */
bool
lex_force_match (int t)
{
  if (token == t)
    {
      lex_get ();
      return true;
    }
  else
    {
      lex_error (_("expecting `%s'"), lex_token_name (t));
      return false;
    }
}

/* If this token is a string, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_string (void)
{
  if (token == T_STRING)
    return true;
  else
    {
      lex_error (_("expecting string"));
      return false;
    }
}

/* If this token is an integer, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_int (void)
{
  if (lex_is_integer ())
    return true;
  else
    {
      lex_error (_("expecting integer"));
      return false;
    }
}
	
/* If this token is a number, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_num (void)
{
  if (lex_is_number ())
    return true;
  else
    {
      lex_error (_("expecting number"));
      return false;
    }
}
	
/* If this token is an identifier, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_id (void)
{
  if (token == T_ID)
    return true;
  else
    {
      lex_error (_("expecting identifier"));
      return false;
    }
}
/* Weird token functions. */

/* Returns the first character of the next token, except that if the
   next token is not an identifier, the character returned will not be
   a character that can begin an identifier.  Specifically, the
   hexstring lead-in X' causes lookahead() to return '.  Note that an
   alphanumeric return value doesn't guarantee an ID token, it could
   also be a reserved-word token. */
int
lex_look_ahead (void)
{
  if (put_token)
    return put_token;

  for (;;)
    {
      if (eof)
        return 0;

      for (;;)
	{
	  while (isspace ((unsigned char) *prog))
	    prog++;
	  if (*prog)
	    break;

	  if (dot)
	    return '.';
	  else if (!lex_get_line ())
            return 0;

	  if (put_token) 
	    return put_token;
	}

      if ((toupper ((unsigned char) *prog) == 'X'
	   || toupper ((unsigned char) *prog) == 'B'
           || toupper ((unsigned char) *prog) == 'O')
	  && (prog[1] == '\'' || prog[1] == '"'))
	return '\'';

      return *prog;
    }
}

/* Makes the current token become the next token to be read; the
   current token is set to T. */
void
lex_put_back (int t)
{
  save_token ();
  token = t;
}

/* Makes the current token become the next token to be read; the
   current token is set to the identifier ID. */
void
lex_put_back_id (const char *id)
{
  assert (lex_id_to_token (id, strlen (id)) == T_ID);
  save_token ();
  token = T_ID;
  ds_assign_cstr (&tokstr, id);
  str_copy_trunc (tokid, sizeof tokid, ds_cstr (&tokstr));
}

/* Weird line processing functions. */

/* Returns the entire contents of the current line. */
const char *
lex_entire_line (void)
{
  return ds_cstr (&getl_buf);
}

/* As lex_entire_line(), but only returns the part of the current line
   that hasn't already been tokenized.
   If END_DOT is non-null, stores nonzero into *END_DOT if the line
   ends with a terminal dot, or zero if it doesn't. */
const char *
lex_rest_of_line (int *end_dot)
{
  if (end_dot)
    *end_dot = dot;
  return prog;
}

/* Causes the rest of the current input line to be ignored for
   tokenization purposes. */
void
lex_discard_line (void)
{
  prog = ds_end (&getl_buf);
  dot = false;
  put_token = 0;
}

/* Sets the current position in the current line to P, which must be
   in getl_buf. */
void
lex_set_prog (char *p)
{
  prog = p;
}

/* Discards the rest of the current command.
   When we're reading commands from a file, we skip tokens until
   a terminal dot or EOF.
   When we're reading commands interactively from the user,
   that's just discarding the current line, because presumably
   the user doesn't want to finish typing a command that will be
   ignored anyway. */
void
lex_discard_rest_of_command (void) 
{
  if (!getl_is_interactive ())
    {
      while (token != T_STOP && token != '.')
	lex_get ();
    }
  else 
    lex_discard_line (); 
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

/* Reads a line for use by the tokenizer, and preprocesses it by
   removing comments, stripping trailing whitespace and the
   terminal dot, and removing leading indentors. */
bool
lex_get_line (void)
{
  struct string *line = &getl_buf;
  bool interactive;

  if (!getl_read_line (&interactive))
    return false;

  strip_comments (line);
  ds_rtrim (line, ss_cstr (CC_SPACES));
  
  /* Check for and remove terminal dot. */
  dot = (ds_chomp (line, get_endcmd ())
         || (ds_is_empty (line) && get_nulline ()));
  
  /* Strip leading indentors or insert a terminal dot (unless the
     line was obtained interactively). */
  if (!interactive)
    {
      int first = ds_first (line);

      if (first == '+' || first == '-')
	*ds_data (line) = ' ';
      else if (first != EOF && !isspace (first))
	put_token = '.';
    }

  prog = ds_cstr (line);

  return true;
}

/* Token names. */

/* Returns the name of a token in a static buffer. */
const char *
lex_token_name (int token)
{
  if (token >= T_FIRST_KEYWORD && token <= T_LAST_KEYWORD)
    return keywords[token - T_FIRST_KEYWORD];

  if (token < 256)
    {
      static char t[2];
      t[0] = token;
      return t;
    }

  NOT_REACHED ();
}

/* Returns an ASCII representation of the current token as a
   malloc()'d string. */
char *
lex_token_representation (void)
{
  char *token_rep;
  
  switch (token)
    {
    case T_ID:
    case T_POS_NUM:
    case T_NEG_NUM:
      return ds_xstrdup (&tokstr);
      break;

    case T_STRING:
      {
	int hexstring = 0;
	char *sp, *dp;

	for (sp = ds_cstr (&tokstr); sp < ds_end (&tokstr); sp++)
	  if (!isprint ((unsigned char) *sp))
	    {
	      hexstring = 1;
	      break;
	    }
	      
	token_rep = xmalloc (2 + ds_length (&tokstr) * 2 + 1 + 1);

	dp = token_rep;
	if (hexstring)
	  *dp++ = 'X';
	*dp++ = '\'';

	if (!hexstring)
	  for (sp = ds_cstr (&tokstr); *sp; )
	    {
	      if (*sp == '\'')
		*dp++ = '\'';
	      *dp++ = (unsigned char) *sp++;
	    }
	else
	  for (sp = ds_cstr (&tokstr); sp < ds_end (&tokstr); sp++)
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
      if (token >= T_FIRST_KEYWORD && token <= T_LAST_KEYWORD)
	return xstrdup (keywords [token - T_FIRST_KEYWORD]);
      else
	{
	  token_rep = xmalloc (2);
	  token_rep[0] = token;
	  token_rep[1] = '\0';
	  return token_rep;
	}
    }
	
  NOT_REACHED ();
}

/* Really weird functions. */

/* Most of the time, a `-' is a lead-in to a negative number.  But
   sometimes it's actually part of the syntax.  If a dash can be part
   of syntax then this function is called to rip it off of a
   number. */
void
lex_negative_to_dash (void)
{
  if (token == T_NEG_NUM)
    {
      token = T_POS_NUM;
      tokval = -tokval;
      ds_assign_substring (&tokstr, ds_substr (&tokstr, 1, SIZE_MAX));
      save_token ();
      token = '-';
    }
}
   
/* We're not at eof any more. */
void
lex_reset_eof (void)
{
  eof = false;
}

/* Skip a COMMENT command. */
void
lex_skip_comment (void)
{
  for (;;)
    {
      if (!lex_get_line ()) 
        {
          put_token = T_STOP;
          eof = true;
          return;
        }
      
      if (put_token == '.')
	break;

      prog = ds_end (&getl_buf);
      if (dot)
	break;
    }
}

/* Private functions. */

/* When invoked, tokstr contains a string of binary, octal, or
   hex digits, according to TYPE.  The string is converted to
   characters having the specified values. */
static void
convert_numeric_string_to_char_string (enum string_type type)
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
  
  byte_cnt = ds_length (&tokstr) / chars_per_byte;
  if (ds_length (&tokstr) % chars_per_byte)
    msg (SE, _("String of %s digits has %d characters, which is not a "
	       "multiple of %d."),
	 base_name, ds_length (&tokstr), chars_per_byte);

  p = ds_cstr (&tokstr);
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

      ds_cstr (&tokstr)[i] = (unsigned char) value;
    }

  ds_truncate (&tokstr, byte_cnt);
}

/* Parses a string from the input buffer into tokstr.  The input
   buffer pointer prog must point to the initial single or double
   quote.  TYPE indicates the type of string to be parsed.
   Returns token type. */
static int 
parse_string (enum string_type type)
{
  if (type != CHARACTER_STRING)
    prog++;

  /* Accumulate the entire string, joining sections indicated by +
     signs. */
  for (;;)
    {
      /* Single or double quote. */
      int c = *prog++;
      
      /* Accumulate section. */
      for (;;)
	{
	  /* Check end of line. */
	  if (*prog == '\0')
	    {
	      msg (SE, _("Unterminated string constant."));
	      goto finish;
	    }
	  
	  /* Double quote characters to embed them in strings. */
	  if (*prog == c)
	    {
	      if (prog[1] == c)
		prog++;
	      else
		break;
	    }

	  ds_put_char (&tokstr, *prog++);
	}
      prog++;

      /* Skip whitespace after final quote mark. */
      if (eof)
	break;
      for (;;)
	{
	  while (isspace ((unsigned char) *prog))
	    prog++;
	  if (*prog)
	    break;

	  if (dot)
	    goto finish;

	  if (!lex_get_line ())
            goto finish;
	}

      /* Skip plus sign. */
      if (*prog != '+')
	break;
      prog++;

      /* Skip whitespace after plus sign. */
      if (eof)
	break;
      for (;;)
	{
	  while (isspace ((unsigned char) *prog))
	    prog++;
	  if (*prog)
	    break;

	  if (dot)
	    goto finish;

	  if (!lex_get_line ())
            {
              msg (SE, _("Unexpected end of file in string concatenation."));
              goto finish;
            }
	}

      /* Ensure that a valid string follows. */
      if (*prog != '\'' && *prog != '"')
	{
	  msg (SE, _("String expected following `+'."));
	  goto finish;
	}
    }

  /* We come here when we've finished concatenating all the string sections
     into one large string. */
finish:
  if (type != CHARACTER_STRING)
    convert_numeric_string_to_char_string (type);

  if (ds_length (&tokstr) > 255)
    {
      msg (SE, _("String exceeds 255 characters in length (%d characters)."),
	   ds_length (&tokstr));
      ds_truncate (&tokstr, 255);
    }
      
  return T_STRING;
}
	
#if DUMP_TOKENS
/* Reads one token from the lexer and writes a textual representation
   on stdout for debugging purposes. */
static void
dump_token (void)
{
  {
    const char *curfn;
    int curln;

    getl_location (&curfn, &curln);
    if (curfn)
      fprintf (stderr, "%s:%d\t", curfn, curln);
  }
  
  switch (token)
    {
    case T_ID:
      fprintf (stderr, "ID\t%s\n", tokid);
      break;

    case T_POS_NUM:
    case T_NEG_NUM:
      fprintf (stderr, "NUM\t%f\n", tokval);
      break;

    case T_STRING:
      fprintf (stderr, "STRING\t\"%s\"\n", ds_cstr (&tokstr));
      break;

    case T_STOP:
      fprintf (stderr, "STOP\n");
      break;

    case T_EXP:
      fprintf (stderr, "MISC\tEXP\"");
      break;

    case 0:
      fprintf (stderr, "MISC\tEOF\n");
      break;

    default:
      if (token >= T_FIRST_KEYWORD && token <= T_LAST_KEYWORD)
	fprintf (stderr, "KEYWORD\t%s\n", lex_token_name (token));
      else
	fprintf (stderr, "PUNCT\t%c\n", token);
      break;
    }
}
#endif /* DUMP_TOKENS */
