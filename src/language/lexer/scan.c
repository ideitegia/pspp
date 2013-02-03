/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2013 Free Software Foundation, Inc.

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

#include "language/lexer/scan.h"

#include <limits.h>
#include <unistr.h>

#include "data/identifier.h"
#include "language/lexer/token.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"

#include "gl/c-ctype.h"
#include "gl/c-strtod.h"
#include "gl/xmemdup0.h"

enum
  {
    S_START,
    S_DASH,
    S_STRING
  };

#define SS_NL_BEFORE_PLUS (1u << 0)
#define SS_PLUS           (1u << 1)
#define SS_NL_AFTER_PLUS  (1u << 2)

/* Returns the integer value of (hex) digit C. */
static int
digit_value (int c)
{
  switch (c)
    {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    default: return INT_MAX;
    }
}

static bool
scan_quoted_string__ (struct substring s, struct token *token)
{
  int quote;

  /* Trim ' or " from front and back. */
  quote = s.string[s.length - 1];
  s.string++;
  s.length -= 2;

  ss_realloc (&token->string, token->string.length + s.length + 1);

  for (;;)
    {
      size_t pos = ss_find_byte (s, quote);
      if (pos == SIZE_MAX)
        break;

      memcpy (ss_end (token->string), s.string, pos + 1);
      token->string.length += pos + 1;
      ss_advance (&s, pos + 2);
    }

  memcpy (ss_end (token->string), s.string, ss_length (s));
  token->string.length += ss_length (s);

  return true;
}

static bool
scan_hex_string__ (struct substring s, struct token *token)
{
  uint8_t *dst;
  size_t i;

  /* Trim X' from front and ' from back. */
  s.string += 2;
  s.length -= 3;

  if (s.length % 2 != 0)
    {
      token->type = SCAN_BAD_HEX_LENGTH;
      token->number = s.length;
      return false;
    }

  ss_realloc (&token->string, token->string.length + s.length / 2 + 1);
  dst = CHAR_CAST (uint8_t *, ss_end (token->string));
  token->string.length += s.length / 2;
  for (i = 0; i < s.length; i += 2)
    {
      int hi = digit_value (s.string[i]);
      int lo = digit_value (s.string[i + 1]);

      if (hi >= 16 || lo >= 16)
        {
          token->type = SCAN_BAD_HEX_DIGIT;
          token->number = s.string[hi >= 16 ? i : i + 1];
          return false;
        }

      *dst++ = hi * 16 + lo;
    }

  return true;
}

static bool
scan_unicode_string__ (struct substring s, struct token *token)
{
  uint8_t *dst;
  ucs4_t uc;
  size_t i;

  /* Trim U' from front and ' from back. */
  s.string += 2;
  s.length -= 3;

  if (s.length < 1 || s.length > 8)
    {
      token->type = SCAN_BAD_UNICODE_LENGTH;
      token->number = s.length;
      return 0;
    }

  ss_realloc (&token->string, token->string.length + 4 + 1);

  uc = 0;
  for (i = 0; i < s.length; i++)
    {
      int digit = digit_value (s.string[i]);
      if (digit >= 16)
        {
          token->type = SCAN_BAD_UNICODE_DIGIT;
          token->number = s.string[i];
          return 0;
        }
      uc = uc * 16 + digit;
    }

  if ((uc >= 0xd800 && uc < 0xe000) || uc > 0x10ffff)
    {
      token->type = SCAN_BAD_UNICODE_CODE_POINT;
      token->number = uc;
      return 0;
    }

  dst = CHAR_CAST (uint8_t *, ss_end (token->string));
  token->string.length += u8_uctomb (dst, uc, 4);

  return true;
}

static enum scan_result
scan_string_segment__ (struct scanner *scanner, enum segment_type type,
                       struct substring s, struct token *token)
{
  bool ok;

  switch (type)
    {
    case SEG_QUOTED_STRING:
      ok = scan_quoted_string__ (s, token);
      break;

    case SEG_HEX_STRING:
      ok = scan_hex_string__ (s, token);
      break;

    case SEG_UNICODE_STRING:
      ok = scan_unicode_string__ (s, token);
      break;

    default:
      NOT_REACHED ();
    }

  if (ok)
    {
      token->type = T_STRING;
      token->string.string[token->string.length] = '\0';
      scanner->state = S_STRING;
      scanner->substate = 0;
      return SCAN_SAVE;
    }
  else
    {
      /* The function we called above should have filled in token->type and
         token->number properly to describe the error. */
      ss_dealloc (&token->string);
      token->string = ss_empty ();
      return SCAN_DONE;
    }

}

static enum scan_result
add_bit (struct scanner *scanner, unsigned int bit)
{
  if (!(scanner->substate & bit))
    {
      scanner->substate |= bit;
      return SCAN_MORE;
    }
  else
    return SCAN_BACK;
}

static enum scan_result
scan_string__ (struct scanner *scanner, enum segment_type type,
               struct substring s, struct token *token)
{
  switch (type)
    {
    case SEG_SPACES:
    case SEG_COMMENT:
      return SCAN_MORE;

    case SEG_NEWLINE:
      if (scanner->substate & SS_PLUS)
        return add_bit (scanner, SS_NL_AFTER_PLUS);
      else
        return add_bit (scanner, SS_NL_BEFORE_PLUS);

    case SEG_PUNCT:
      return (s.length == 1 && s.string[0] == '+'
              ? add_bit (scanner, SS_PLUS)
              : SCAN_BACK);

    case SEG_QUOTED_STRING:
    case SEG_HEX_STRING:
    case SEG_UNICODE_STRING:
      return (scanner->substate & SS_PLUS
              ? scan_string_segment__ (scanner, type, s, token)
              : SCAN_BACK);

    default:
      return SCAN_BACK;
    }
}

static enum token_type
scan_reserved_word__ (struct substring word)
{
  switch (c_toupper (word.string[0]))
    {
    case 'B':
      return T_BY;

    case 'E':
      return T_EQ;

    case 'G':
      return c_toupper (word.string[1]) == 'E' ? T_GE : T_GT;

    case 'L':
      return c_toupper (word.string[1]) == 'E' ? T_LE : T_LT;

    case 'N':
      return word.length == 2 ? T_NE : T_NOT;

    case 'O':
      return T_OR;

    case 'T':
      return T_TO;

    case 'A':
      return c_toupper (word.string[1]) == 'L' ? T_ALL : T_AND;

    case 'W':
      return T_WITH;
    }

  NOT_REACHED ();
}

static enum token_type
scan_punct1__ (char c0)
{
  switch (c0)
    {
    case '(': return T_LPAREN;
    case ')': return T_RPAREN;
    case ',': return T_COMMA;
    case '=': return T_EQUALS;
    case '-': return T_DASH;
    case '[': return T_LBRACK;
    case ']': return T_RBRACK;
    case '&': return T_AND;
    case '|': return T_OR;
    case '+': return T_PLUS;
    case '/': return T_SLASH;
    case '*': return T_ASTERISK;
    case '<': return T_LT;
    case '>': return T_GT;
    case '~': return T_NOT;
    }

  NOT_REACHED ();
}

static enum token_type
scan_punct2__ (char c0, char c1)
{
  switch (c0)
    {
    case '*':
      return T_EXP;

    case '<':
      return c1 == '=' ? T_LE : T_NE;

    case '>':
      return T_GE;

    case '~':
      return T_NE;

    case '&':
      return T_AND;

    case '|':
      return T_OR;
    }

  NOT_REACHED ();
}

static enum token_type
scan_punct__ (struct substring s)
{
  return (s.length == 1
          ? scan_punct1__ (s.string[0])
          : scan_punct2__ (s.string[0], s.string[1]));
}

static double
scan_number__ (struct substring s)
{
  char buf[128];
  double number;
  char *p;

  if (s.length < sizeof buf)
    {
      p = buf;
      memcpy (buf, s.string, s.length);
      buf[s.length] = '\0';
    }
  else
    p = xmemdup0 (s.string, s.length);

  number = c_strtod (p, NULL);

  if (p != buf)
    free (p);

  return number;
}

static enum scan_result
scan_unexpected_char (const struct substring *s, struct token *token)
{
  ucs4_t uc;

  token->type = SCAN_UNEXPECTED_CHAR;
  u8_mbtouc (&uc, CHAR_CAST (const uint8_t *, s->string), s->length);
  token->number = uc;

  return SCAN_DONE;
}

const char *
scan_type_to_string (enum scan_type type)
{
  switch (type)
    {
#define SCAN_TYPE(NAME) case SCAN_##NAME: return #NAME;
      SCAN_TYPES
#undef SCAN_TYPE

    default:
      return token_type_to_name (type);
    }
}

bool
is_scan_type (enum scan_type type)
{
  return type > SCAN_FIRST && type < SCAN_LAST;
}

static enum scan_result
scan_start__ (struct scanner *scanner, enum segment_type type,
              struct substring s, struct token *token)
{
  switch (type)
    {
    case SEG_NUMBER:
      token->type = T_POS_NUM;
      token->number = scan_number__ (s);
      return SCAN_DONE;

    case SEG_QUOTED_STRING:
    case SEG_HEX_STRING:
    case SEG_UNICODE_STRING:
      return scan_string_segment__ (scanner, type, s, token);

    case SEG_UNQUOTED_STRING:
    case SEG_DO_REPEAT_COMMAND:
    case SEG_INLINE_DATA:
    case SEG_DOCUMENT:
      token->type = T_STRING;
      ss_alloc_substring (&token->string, s);
      return SCAN_DONE;

    case SEG_RESERVED_WORD:
      token->type = scan_reserved_word__ (s);
      return SCAN_DONE;

    case SEG_IDENTIFIER:
      token->type = T_ID;
      ss_alloc_substring (&token->string, s);
      return SCAN_DONE;

    case SEG_PUNCT:
      if (s.length == 1 && s.string[0] == '-')
        {
          scanner->state = S_DASH;
          return SCAN_SAVE;
        }
      else
        {
          token->type = scan_punct__ (s);
          return SCAN_DONE;
        }

    case SEG_SHBANG:
    case SEG_SPACES:
    case SEG_COMMENT:
    case SEG_NEWLINE:
    case SEG_COMMENT_COMMAND:
      token->type = SCAN_SKIP;
      return SCAN_DONE;

    case SEG_START_DOCUMENT:
      token->type = T_ID;
      ss_alloc_substring (&token->string, ss_cstr ("DOCUMENT"));
      return SCAN_DONE;

    case SEG_START_COMMAND:
    case SEG_SEPARATE_COMMANDS:
    case SEG_END_COMMAND:
      token->type = T_ENDCMD;
      return SCAN_DONE;

    case SEG_END:
      token->type = T_STOP;
      return SCAN_DONE;

    case SEG_EXPECTED_QUOTE:
      token->type = SCAN_EXPECTED_QUOTE;
      return SCAN_DONE;

    case SEG_EXPECTED_EXPONENT:
      token->type = SCAN_EXPECTED_EXPONENT;
      ss_alloc_substring (&token->string, s);
      return SCAN_DONE;

    case SEG_UNEXPECTED_DOT:
      token->type = SCAN_UNEXPECTED_DOT;
      return SCAN_DONE;

    case SEG_UNEXPECTED_CHAR:
      return scan_unexpected_char (&s, token);
    }

  NOT_REACHED ();
}

static enum scan_result
scan_dash__ (enum segment_type type, struct substring s, struct token *token)
{
  switch (type)
    {
    case SEG_SPACES:
    case SEG_COMMENT:
      return SCAN_MORE;

    case SEG_NUMBER:
      token->type = T_NEG_NUM;
      token->number = -scan_number__ (s);
      return SCAN_DONE;

    default:
      token->type = T_DASH;
      return SCAN_BACK;
    }
}

/* Initializes SCANNER for scanning a token from a sequence of segments.
   Initializes TOKEN as the output token.  (The client retains ownership of
   TOKEN, but it must be preserved across subsequent calls to scanner_push()
   for SCANNER.)

   A scanner only produces a single token.  To obtain the next token,
   re-initialize it by calling this function again.

   A scanner does not contain any external references, so nothing needs to be
   done to destroy one.  For the same reason, scanners may be copied with plain
   struct assignment (or memcpy). */
void
scanner_init (struct scanner *scanner, struct token *token)
{
  scanner->state = S_START;
  token_init (token);
}

/* Adds the segment with type TYPE and UTF-8 text S to SCANNER.  TOKEN must be
   the same token passed to scanner_init() for SCANNER, or a copy of it.
   scanner_push() may modify TOKEN.  The client retains ownership of TOKEN,

   The possible return values are:

     - SCAN_DONE: All of the segments that have been passed to scanner_push()
       form the token now stored in TOKEN.  SCANNER is now "used up" and must
       be reinitialized with scanner_init() if it is to be used again.

       Most tokens only consist of a single segment, so this is the most common
       return value.

     - SCAN_MORE: The segments passed to scanner_push() don't yet determine a
       token.  The caller should call scanner_push() again with the next token.
       (This won't happen if TYPE is SEG_END indicating the end of input.)

     - SCAN_SAVE: This is similar to SCAN_MORE, with one difference: the caller
       needs to "save its place" in the stream of segments for a possible
       future SCAN_BACK return.  This value can be returned more than once in a
       sequence of scanner_push() calls for SCANNER, but the caller only needs
       to keep track of the most recent position.

     - SCAN_BACK: This is similar to SCAN_DONE, but the token consists of only
       the segments up to and including the segment for which SCAN_SAVE was
       most recently returned.  Segments following that one should be passed to
       the next scanner to be initialized.
*/
enum scan_result
scanner_push (struct scanner *scanner, enum segment_type type,
              struct substring s, struct token *token)
{
  switch (scanner->state)
    {
    case S_START:
      return scan_start__ (scanner, type, s, token);

    case S_DASH:
      return scan_dash__ (type, s, token);

    case S_STRING:
      return scan_string__ (scanner, type, s, token);
    }

  NOT_REACHED ();
}

/* Initializes SLEX for parsing INPUT in the specified MODE.

   SLEX has no internal state to free, but it retains a reference to INPUT, so
   INPUT must not be modified or freed while SLEX is still in use. */
void
string_lexer_init (struct string_lexer *slex, const char *input,
                   enum segmenter_mode mode)
{
  slex->input = input;
  slex->length = strlen (input) + 1;
  slex->offset = 0;
  segmenter_init (&slex->segmenter, mode);
}

/*  */
bool
string_lexer_next (struct string_lexer *slex, struct token *token)
{
  struct segmenter saved_segmenter;
  size_t saved_offset = 0;

  struct scanner scanner;

  scanner_init (&scanner, token);
  for (;;)
    {
      const char *s = slex->input + slex->offset;
      size_t left = slex->length - slex->offset;
      enum segment_type type;
      int n;

      n = segmenter_push (&slex->segmenter, s, left, &type);
      assert (n >= 0);

      slex->offset += n;
      switch (scanner_push (&scanner, type, ss_buffer (s, n), token))
        {
        case SCAN_BACK:
          slex->segmenter = saved_segmenter;
          slex->offset = saved_offset;
          /* Fall through. */
        case SCAN_DONE:
          return token->type != T_STOP;

        case SCAN_MORE:
          break;

        case SCAN_SAVE:
          saved_segmenter = slex->segmenter;
          saved_offset = slex->offset;
          break;
        }
    }
}
