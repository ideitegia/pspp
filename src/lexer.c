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
#include "error.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "getline.h"
#include "magic.h"
#include "settings.h"
#include "str.h"

/*
#define DUMP_TOKENS 1
*/


/* Global variables. */

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

/* Table of keywords. */
static const char *keywords[T_N_KEYWORDS + 1] = 
  {
    "AND", "OR", "NOT",
    "EQ", "GE", "GT", "LE", "LT", "NE",
    "ALL", "BY", "TO", "WITH",
    NULL,
  };

/* Pointer to next token in getl_buf. */
static char *prog;

/* Nonzero only if this line ends with a terminal dot. */
static int dot;

/* Nonzero only if the last token returned was T_STOP. */
static int eof;

/* If nonzero, next token returned by lex_get().
   Used only in exceptional circumstances. */
static int put_token;
static struct string put_tokstr;
static double put_tokval;

static void unexpected_eof (void);
static void convert_numeric_string_to_char_string (int type);
static int parse_string (int type);

#if DUMP_TOKENS
static void dump_token (void);
#endif

/* Initialization. */

/* Initializes the lexer. */
void
lex_init (void)
{
  ds_init (&put_tokstr, 64);
  if (!lex_get_line ())
    unexpected_eof ();
}

void
lex_done (void)
{
  ds_destroy(&put_tokstr);
}


/* Common functions. */

/* Copies put_token, put_tokstr, put_tokval into token, tokstr,
   tokval, respectively, and sets tokid appropriately. */
static void
restore_token (void) 
{
  assert (put_token != 0);
  token = put_token;
  ds_replace (&tokstr, ds_c_str (&put_tokstr));
  str_copy_trunc (tokid, sizeof tokid, ds_c_str (&tokstr));
  tokval = put_tokval;
  put_token = 0;
}

/* Copies token, tokstr, tokval into put_token, put_tokstr,
   put_tokval respectively. */
static void
save_token (void) 
{
  put_token = token;
  ds_replace (&put_tokstr, ds_c_str (&tokstr));
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
      char *cp;

      /* Skip whitespace. */
      if (eof)
	unexpected_eof ();

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
	      eof = 1;
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
      cp = prog;
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
	    if (*cp == '-')
	      {
		ds_putc (&tokstr, *prog++);
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
	      ds_putc (&tokstr, *prog++);
	    if (*prog == '.')
	      {
		ds_putc (&tokstr, *prog++);
		while (isdigit ((unsigned char) *prog))
		  ds_putc (&tokstr, *prog++);
	      }
	    if (*prog == 'e' || *prog == 'E')
	      {
		ds_putc (&tokstr, *prog++);
		if (*prog == '+' || *prog == '-')
		  ds_putc (&tokstr, *prog++);
		while (isdigit ((unsigned char) *prog))
		  ds_putc (&tokstr, *prog++);
	      }

	    /* Parse as floating point. */
	    tokval = strtod (ds_c_str (&tokstr), &tail);
	    if (*tail)
	      {
		msg (SE, _("%s does not form a valid number."),
		     ds_c_str (&tokstr));
		tokval = 0.0;

		ds_clear (&tokstr);
		ds_putc (&tokstr, '0');
	      }

	    break;
	  }

	case '\'': case '"':
	  token = parse_string (0);
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

	case 'a': case 'b': case 'c': case 'd': case 'e':
	case 'f': case 'g': case 'h': case 'i': case 'j':
	case 'k': case 'l': case 'm': case 'n': case 'o':
	case 'p': case 'q': case 'r': case 's': case 't':
	case 'u': case 'v': case 'w': case 'x': case 'y':
	case 'z':
	case 'A': case 'B': case 'C': case 'D': case 'E':
	case 'F': case 'G': case 'H': case 'I': case 'J':
	case 'K': case 'L': case 'M': case 'N': case 'O':
	case 'P': case 'Q': case 'R': case 'S': case 'T':
	case 'U': case 'V': case 'W': case 'X': case 'Y':
	case 'Z':
	case '#': case '$': case '@': 
	  /* Strings can be specified in binary, octal, or hex using
	       this special syntax. */
	  if (prog[1] == '\'' || prog[1] == '"')
	    {
	      static const char special[3] = "box";
	      const char *p;

	      p = strchr (special, tolower ((unsigned char) *prog));
	      if (p)
		{
		  prog++;
		  token = parse_string (p - special + 1);
		  break;
		}
	    }

	  /* Copy id to tokstr. */
	  ds_putc (&tokstr, *prog++);
	  while (CHAR_IS_IDN (*prog))
	    ds_putc (&tokstr, *prog++);

	  /* Copy tokstr to tokid, possibly truncating it.*/
	  str_copy_trunc (tokid, sizeof tokid, ds_c_str (&tokstr));

          /* Determine token type. */
	  token = lex_id_to_token (ds_c_str (&tokstr), ds_length (&tokstr));
	  break;

	default:
	  if (isgraph ((unsigned char) *prog))
	    msg (SE, _("Bad character in input: `%c'."), *prog++);
	  else
	    msg (SE, _("Bad character in input: `\\%o'."), *prog++);
	  continue;
	}

      break;
    }

#if DUMP_TOKENS
  dump_token ();
#endif
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
      return CMD_TRAILING_GARBAGE;
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

/* If TOK is the current token, skips it and returns nonzero.
   Otherwise, returns zero. */
int
lex_match (int t)
{
  if (token == t)
    {
      lex_get ();
      return 1;
    }
  else
    return 0;
}

/* If the current token is the identifier S, skips it and returns
   nonzero.  The identifier may be abbreviated to its first three
   letters.
   Otherwise, returns zero. */
int
lex_match_id (const char *s)
{
  if (token == T_ID && lex_id_match (s, tokid))
    {
      lex_get ();
      return 1;
    }
  else
    return 0;
}

/* If the current token is integer N, skips it and returns nonzero.
   Otherwise, returns zero. */
int
lex_match_int (int x)
{
  if (lex_is_integer () && lex_integer () == x)
    {
      lex_get ();
      return 1;
    }
  else
    return 0;
}

/* Forced matches. */

/* If this token is identifier S, fetches the next token and returns
   nonzero.
   Otherwise, reports an error and returns zero. */
int
lex_force_match_id (const char *s)
{
  if (token == T_ID && lex_id_match (s, tokid))
    {
      lex_get ();
      return 1;
    }
  else
    {
      lex_error (_("expecting `%s'"), s);
      return 0;
    }
}

/* If the current token is T, skips the token.  Otherwise, reports an
   error and returns from the current function with return value 0. */
int
lex_force_match (int t)
{
  if (token == t)
    {
      lex_get ();
      return 1;
    }
  else
    {
      lex_error (_("expecting `%s'"), lex_token_name (t));
      return 0;
    }
}

/* If this token is a string, does nothing and returns nonzero.
   Otherwise, reports an error and returns zero. */
int
lex_force_string (void)
{
  if (token == T_STRING)
    return 1;
  else
    {
      lex_error (_("expecting string"));
      return 0;
    }
}

/* If this token is an integer, does nothing and returns nonzero.
   Otherwise, reports an error and returns zero. */
int
lex_force_int (void)
{
  if (lex_is_integer ())
    return 1;
  else
    {
      lex_error (_("expecting integer"));
      return 0;
    }
}
	
/* If this token is a number, does nothing and returns nonzero.
   Otherwise, reports an error and returns zero. */
int
lex_force_num (void)
{
  if (lex_is_number ())
    return 1;
  else
    {
      lex_error (_("expecting number"));
      return 0;
    }
}
	
/* If this token is an identifier, does nothing and returns nonzero.
   Otherwise, reports an error and returns zero. */
int
lex_force_id (void)
{
  if (token == T_ID)
    return 1;
  else
    {
      lex_error (_("expecting identifier"));
      return 0;
    }
}

/* Comparing identifiers. */

/* Keywords match if one of the following is true: KW and TOK are
   identical (except for differences in case), or TOK is at least 3
   characters long and those characters are identical to KW.  KW_LEN
   is the length of KW, TOK_LEN is the length of TOK. */
int
lex_id_match_len (const char *kw, size_t kw_len,
		  const char *tok, size_t tok_len)
{
  size_t i = 0;

  assert (kw && tok);
  for (;;)
    {
      if (i == kw_len && i == tok_len)
	return 1;
      else if (i == tok_len)
	return i >= 3;
      else if (i == kw_len)
	return 0;
      else if (toupper ((unsigned char) kw[i])
	       != toupper ((unsigned char) tok[i]))
	return 0;

      i++;
    }
}

/* Same as lex_id_match_len() minus the need to pass in the lengths. */
int
lex_id_match (const char *kw, const char *tok)
{
  return lex_id_match_len (kw, strlen (kw), tok, strlen (tok));
}

/* Returns the proper token type, either T_ID or a reserved keyword
   enum, for ID[], which must contain LEN characters. */
int
lex_id_to_token (const char *id, size_t len)
{
  const char **kwp;

  if (len < 2 || len > 4)
    return T_ID;
  
  for (kwp = keywords; *kwp; kwp++)
    if (!strcasecmp (*kwp, id))
      return T_FIRST_KEYWORD + (kwp - keywords);

  return T_ID;
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
	unexpected_eof ();

      for (;;)
	{
	  while (isspace ((unsigned char) *prog))
	    prog++;
	  if (*prog)
	    break;

	  if (dot)
	    return '.';
	  else if (!lex_get_line ())
	    unexpected_eof ();

	  if (put_token) 
	    return put_token;
	}

      if ((toupper ((unsigned char) *prog) == 'X'
	   || toupper ((unsigned char) *prog) == 'B')
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
  ds_replace (&tokstr, id);
  str_copy_trunc (tokid, sizeof tokid, ds_c_str (&tokstr));
}

/* Weird line processing functions. */

/* Returns the entire contents of the current line. */
const char *
lex_entire_line (void)
{
  return ds_c_str (&getl_buf);
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
  dot = put_token = 0;
}

/* Sets the current position in the current line to P, which must be
   in getl_buf. */
void
lex_set_prog (char *p)
{
  prog = p;
}

/* Weird line reading functions. */

/* Read a line for use by the tokenizer. */
int
lex_get_line (void)
{
  if (!getl_read_line ())
    return 0;

  lex_preprocess_line ();
  return 1;
}

/* Preprocesses getl_buf by removing comments, stripping trailing
   whitespace and the terminal dot, and removing leading indentors. */
void
lex_preprocess_line (void)
{
  /* Strips comments. */
  {
    /* getl_buf iterator. */
    char *cp;

    /* Nonzero inside a comment. */
    int comment;

    /* Nonzero inside a quoted string. */
    int quote;

    /* Remove C-style comments begun by slash-star and terminated by
       star-slash or newline. */
    quote = comment = 0;
    for (cp = ds_c_str (&getl_buf); *cp; )
      {
	/* If we're not commented out, toggle quoting. */
	if (!comment)
	  {
	    if (*cp == quote)
	      quote = 0;
	    else if (*cp == '\'' || *cp == '"')
	      quote = *cp;
	  }
      
	/* If we're not quoting, toggle commenting. */
	if (!quote)
	  {
	    if (cp[0] == '/' && cp[1] == '*')
	      {
		comment = 1;
		*cp++ = ' ';
		*cp++ = ' ';
		continue;
	      }
	    else if (cp[0] == '*' && cp[1] == '/' && comment)
	      {
		comment = 0;
		*cp++ = ' ';
		*cp++ = ' ';
		continue;
	      }
	  }
      
	/* Check commenting. */
	if (!comment)
	  cp++;
	else
	  *cp++ = ' ';
      }
  }
  
  /* Strip trailing whitespace and terminal dot. */
  {
    size_t len = ds_length (&getl_buf);
    char *s = ds_c_str (&getl_buf);
    
    /* Strip trailing whitespace. */
    while (len > 0 && isspace ((unsigned char) s[len - 1]))
      len--;

    /* Check for and remove terminal dot. */
    if (len > 0 && s[len - 1] == get_endcmd() )
      {
	dot = 1;
	len--;
      }
    else if (len == 0 && get_nullline() )
      dot = 1;
    else
      dot = 0;

    /* Set length. */
    ds_truncate (&getl_buf, len);
  }
  
  /* In batch mode, strip leading indentors and insert a terminal dot
     as necessary. */
  if (getl_interactive != 2 && getl_mode == GETL_MODE_BATCH)
    {
      char *s = ds_c_str (&getl_buf);
      
      if (s[0] == '+' || s[0] == '-' || s[0] == '.')
	s[0] = ' ';
      else if (s[0] && !isspace ((unsigned char) s[0]))
	put_token = '.';
    }

  prog = ds_c_str (&getl_buf);
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

  return _("<ERROR>");
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
      return xstrdup (ds_c_str (&tokstr));
      break;

    case T_STRING:
      {
	int hexstring = 0;
	char *sp, *dp;

	for (sp = ds_c_str (&tokstr); sp < ds_end (&tokstr); sp++)
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
	  for (sp = ds_c_str (&tokstr); *sp; )
	    {
	      if (*sp == '\'')
		*dp++ = '\'';
	      *dp++ = (unsigned char) *sp++;
	    }
	else
	  for (sp = ds_c_str (&tokstr); sp < ds_end (&tokstr); sp++)
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
	
  assert (0);
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
      ds_replace (&tokstr, ds_c_str (&tokstr) + 1);
      save_token ();
      token = '-';
    }
}
   
/* We're not at eof any more. */
void
lex_reset_eof (void)
{
  eof = 0;
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
          eof = 1;
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

/* Unexpected end of file. */
static void
unexpected_eof (void)
{
  msg (FE, _("Unexpected end of file."));
}

/* When invoked, tokstr contains a string of binary, octal, or hex
   digits, for values of TYPE of 0, 1, or 2, respectively.  The string
   is converted to characters having the specified values. */
static void
convert_numeric_string_to_char_string (int type)
{
  static const char *base_names[] = {N_("binary"), N_("octal"), N_("hex")};
  static const int bases[] = {2, 8, 16};
  static const int chars_per_byte[] = {8, 3, 2};

  const char *const base_name = base_names[type];
  const int base = bases[type];
  const int cpb = chars_per_byte[type];
  const int nb = ds_length (&tokstr) / cpb;
  int i;
  char *p;

  assert (type >= 0 && type <= 2);

  if (ds_length (&tokstr) % cpb)
    msg (SE, _("String of %s digits has %d characters, which is not a "
	       "multiple of %d."),
	 gettext (base_name), ds_length (&tokstr), cpb);

  p = ds_c_str (&tokstr);
  for (i = 0; i < nb; i++)
    {
      int value;
      int j;
	  
      value = 0;
      for (j = 0; j < cpb; j++, p++)
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

      ds_c_str (&tokstr)[i] = (unsigned char) value;
    }

  ds_truncate (&tokstr, nb);
}

/* Parses a string from the input buffer into tokstr.  The input
   buffer pointer prog must point to the initial single or double
   quote.  TYPE is 0 if it is an ordinary string, or 1, 2, or 3 for a
   binary, octal, or hexstring, respectively.  Returns token type. */
static int 
parse_string (int type)
{
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
	  if (*prog == 0)
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

	  ds_putc (&tokstr, *prog++);
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
	    unexpected_eof ();
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
	    unexpected_eof ();
	}

      /* Ensure that a valid string follows. */
      if (*prog != '\'' && *prog != '"')
	{
	  msg (SE, "String expected following `+'.");
	  goto finish;
	}
    }

  /* We come here when we've finished concatenating all the string sections
     into one large string. */
finish:
  if (type != 0)
    convert_numeric_string_to_char_string (type - 1);

  if (ds_length (&tokstr) > 255)
    {
      msg (SE, _("String exceeds 255 characters in length (%d characters)."),
	   ds_length (&tokstr));
      ds_truncate (&tokstr, 255);
    }
      
  {
    /* FIXME. */
    size_t i;
    int warned = 0;

    for (i = 0; i < ds_length (&tokstr); i++)
      if (ds_c_str (&tokstr)[i] == 0)
	{
	  if (!warned)
	    {
	      msg (SE, _("Sorry, literal strings may not contain null "
			 "characters.  Replacing with spaces."));
	      warned = 1;
	    }
	  ds_c_str (&tokstr)[i] = ' ';
	}
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
      fprintf (stderr, "STRING\t\"%s\"\n", ds_c_str (&tokstr));
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
