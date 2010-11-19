/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#include "language/lexer/command-name.h"

#include <assert.h>
#include <limits.h>

#include "data/identifier.h"

#include "gl/c-ctype.h"

/* Stores the first word in S into WORD and advances S past that word.  Returns
   true if successful, false if no word remained in S to be extracted.

   A word is a sequence of digits, a letter possibly followed by a sequence of
   letters or digits, or one character of another type.  Words may be delimited
   by spaces. */
static bool
find_word (struct substring *s, struct substring *word)
{
  size_t ofs;
  ucs4_t c;

  /* Skip whitespace. */
  for (;;)
    {
      c = ss_first_mb (*s);
      if (c == UINT32_MAX)
        {
          *word = ss_empty ();
          return false;
        }
      else if (lex_uc_is_space (c))
        ss_get_mb (s);
      else
        break;
    }

  ofs = ss_first_mblen (*s);
  if (lex_uc_is_id1 (c))
    {
      while (lex_uc_is_idn (ss_at_mb (*s, ofs)))
        ofs += ss_at_mblen (*s, ofs);
    }
  else if (c_isdigit (c))
    {
      while (c_isdigit (s->string[ofs]))
        ofs++;
    }
  ss_get_bytes (s, ofs, word);
  return true;
}

/* Returns the number of words in S, as extracted by find_word(). */
static int
count_words (struct substring s)
{
  struct substring word;
  int n;

  n = 0;
  while (find_word (&s, &word))
    n++;
  return n;
}

/* Compares STRING obtained from the user against the full name of a COMMAND,
   using this algorithm:

   1. Divide COMMAND into words C[0] through C[n - 1].

   2. Divide STRING into words S[0] through S[m - 1].

   3. Compare word C[i] against S[i] for 0 <= i < min(n, m), using the keyword
      matching algorithm implemented by lex_id_match().  If any of them fail to
      match, then STRING does not match COMMAND and the function returns false.

   4. Otherwise, STRING and COMMAND match.  Set *MISSING_WORDS to n - m.  Set
      *EXACT to false if any of the S[i] were found to be abbreviated in the
      comparisons done in step 3, or to true if they were all exactly equal
      (modulo case).  Return true. */
bool
command_match (struct substring command, struct substring string,
               bool *exact, int *missing_words)
{
  *exact = true;
  for (;;)
    {
      struct substring cw, sw;
      int match;

      if (!find_word (&command, &cw))
        {
          *missing_words = -count_words (string);
          return true;
        }
      else if (!find_word (&string, &sw))
        {
          *missing_words = 1 + count_words (command);
          return true;
        }

      match = lex_id_match (cw, sw);
      if (sw.length < cw.length)
        *exact = false;
      if (match == 0)
        return false;
    }
}

/* Initializes CM for matching STRING against a table of command names.

   STRING may be ASCII or UTF-8.

   For sample use, see command.c.  Here's a usage outline:

      // Try each possible command.
      command_matcher_init (&cm, string);
      for (cmd = commands; cmd < &commands[command_cnt]; cmd++)
        command_matcher_add (&cm, cmd->name, cmd);

      // Get the result.
      match = command_matcher_get_match (&cm);
      missing_words = command_matcher_get_missing_words (&cm);

      if (missing_words > 0)
        {
          // Incomplete command name.  Add another word to the string
          // and start over.  Or if there are no more words to be added,
          // add " ." to the string as a sentinel and start over.
        }
      else if (match == NULL)
        {
          // No valid command with this name.
        }
      else if (missing_words == 0)
        {
          // The full, correct command name is 'match'.
        }
      else if (missing_words < 0)
        {
          // The abs(missing_words) last words of 'string' are actually
          // part of the command's body, not part of its name; they
          // were only needed to resolve ambiguities.  'match' is the
          // correct command but those extra words should be put back
          // for later re-parsing.
        }
*/
void
command_matcher_init (struct command_matcher *cm, struct substring string)
{
  cm->string = string;
  cm->extensible = false;
  cm->exact_match = NULL;
  cm->n_matches = 0;
  cm->match = NULL;
  cm->match_missing_words = 0;
}

/* Destroys CM's state. */
void
command_matcher_destroy (struct command_matcher *cm UNUSED)
{
  /* Nothing to do. */
}

/* Considers COMMAND as a candidate for the command name being parsed by CM.
   If COMMAND is the correct command name, then command_matcher_get_match()
   will return AUX later.

   COMMAND must be an ASCII string. */
void
command_matcher_add (struct command_matcher *cm, struct substring command,
                     void *aux)
{
  int missing_words;
  bool exact;

  assert (aux != NULL);
  if (command_match (command, cm->string, &exact, &missing_words))
    {
      if (missing_words > 0)
        cm->extensible = true;
      else if (exact && missing_words == 0)
        cm->exact_match = aux;
      else
        {
          if (missing_words > cm->match_missing_words)
            cm->n_matches = 0;

          if (missing_words >= cm->match_missing_words || cm->n_matches == 0)
            {
              cm->n_matches++;
              cm->match = aux;
              cm->match_missing_words = missing_words;
            }
        }
    }
}

/* Returns the command name matched by CM. */
void *
command_matcher_get_match (const struct command_matcher *cm)
{
  return (cm->extensible ? NULL
          : cm->exact_match != NULL ? cm->exact_match
          : cm->n_matches == 1 ? cm->match
          : NULL);
}

/* Returns the difference between the number of words in the matched command
   name and the string provided to command_matcher_init(). */
int
command_matcher_get_missing_words (const struct command_matcher *cm)
{
  return (cm->extensible ? 1
          : cm->exact_match != NULL ? 0
          : cm->match_missing_words);
}
