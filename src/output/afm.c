/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
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
#include "afm.h"
#include "c-ctype.h"
#include "c-strtod.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include "error.h"
#include "minmax.h"
#include <libpspp/pool.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A kern pair entry. */
struct afm_kern_pair 
  {
    struct afm_character *successor; /* Second character. */
    int adjust;                 /* Adjustment. */
  };

/* A ligature. */
struct afm_ligature 
  {
    struct afm_character *successor; /* Second character. */
    struct afm_character *ligature;  /* Resulting ligature. */
  };

/* How to map between byte strings and character values. */
enum mapping_scheme 
  {
    MAP_UNKNOWN,                /* Not yet determined. */
    MAP_ONE_BYTE,               /* 8-bit coding. */
    MAP_TWO_BYTE,               /* 16-bit coding. */
    MAP_ESCAPE,                 /* 8-bit coding with escape to change fonts. */
    MAP_DOUBLE_ESCAPE,          /* 8-bit coding with multiple escapes. */
    MAP_SHIFT                   /* 8-bit coding with 2 fonts that toggle. */
  };

/* AFM file data.  */
struct afm
  {
    struct pool *pool;		/* Containing pool. */
    char *findfont_name;        /* Name for PostScript /findfont operator. */
    int ascent;                 /* Height above the baseline (non-negative). */
    int descent;                /* Depth below the baseline (non-negative). */

    /* Encoding characters into strings. */
    enum mapping_scheme mapping; /* Basic mapping scheme. */
    char escape_char;           /* MAP_ESCAPE only: escape character to use. */
    char shift_out;             /* MAP_SHIFT only: selects font 0. */
    char shift_in;              /* MAP_SHIFT only: selects font 1. */

    /* Characters. */
    struct afm_character *undefined_codes[256];
    struct afm_character **codes[256];
    struct afm_character **chars;
    size_t char_cnt;
  };

/* AFM file parser. */
struct parser 
  {
    struct pool *pool;          /* Containing pool. */
    struct afm *afm;            /* AFM being parsed. */
    FILE *file;                 /* File being parsed. */
    const char *file_name;      /* Name of file being parsed. */
    int line_number;            /* Current line number in file. */
    jmp_buf bail_out;           /* longjmp() target for error handling. */

    size_t char_allocated;
    int max_code;
  };

static struct afm *create_afm (void);
static struct afm_character *create_character (struct afm *);

static void afm_error (struct parser *, const char *, ...)
     PRINTF_FORMAT (2, 3)
     NO_RETURN;

static void parse_afm (struct parser *);
static void skip_section (struct parser *, const char *end_key);
static bool parse_set_specific (struct parser *, const char *end_key);
static void parse_direction (struct parser *);
static void parse_char_metrics (struct parser *);
static void parse_kern_pairs (struct parser *);
static void add_kern_pair (struct parser *p,
                           struct afm_character *, struct afm_character *,
                           int adjust);

static int skip_spaces (struct parser *);
static char *parse_key (struct parser *);
static void skip_line (struct parser *);
static void force_eol (struct parser *);
static bool get_integer (struct parser *, int *);
static int force_get_integer (struct parser *);
static bool get_number (struct parser *, int *);
static int force_get_number (struct parser *);
static bool get_hex_code (struct parser *, int *);
static int force_get_hex_code (struct parser *);
static bool get_word (struct parser *, char **);
static char *force_get_word (struct parser *);
static bool get_string (struct parser *, char **);
static char *force_get_string (struct parser *);

static struct afm_character *get_char_by_name (struct parser *, const char *);
static struct afm_character *get_char_by_code (const struct afm *, int code);

/* Reads FILE_NAME as an AFM file and returns the metrics data.
   Returns a null pointer if the file cannot be parsed. */
struct afm *
afm_open (const char *file_name) 
{
  struct afm *volatile afm;
  struct parser *parser;

  parser = pool_create_container (struct parser, pool);
  afm = parser->afm = create_afm ();
  parser->file = pool_fopen (parser->pool, file_name, "r");
  parser->file_name = file_name;
  parser->line_number = 0;
  if (parser->file == NULL) 
    {
      error (0, errno, _("opening font metrics file \"%s\""), file_name);
      goto error; 
    }

  if (setjmp (parser->bail_out))
    goto error;

  parse_afm (parser);
  pool_destroy (parser->pool);
  return afm;

 error:
  pool_destroy (parser->pool);
  pool_destroy (afm->pool);
  return create_afm ();
}

/* Creates and returns an empty set of metrics. */
static struct afm *
create_afm (void) 
{
  struct afm *afm;
  struct afm_character *def_char;
  size_t i;

  afm = pool_create_container (struct afm, pool);
  afm->findfont_name = NULL;
  afm->ascent = 0;
  afm->descent = 0;
  afm->mapping = MAP_UNKNOWN;
  afm->escape_char = 255;
  afm->shift_out = 14;
  afm->shift_in = 15;
  def_char = create_character (afm);
  for (i = 0; i < 256; i++) 
    afm->undefined_codes[i] = def_char;
  for (i = 0; i < 256; i++) 
    afm->codes[i] = afm->undefined_codes; 
  afm->chars = NULL;
  afm->char_cnt = 0;

  return afm;
}

/* Creates and returns an initialized character within AFM. */
static struct afm_character *
create_character (struct afm *afm) 
{
  struct afm_character *c = pool_alloc (afm->pool, sizeof *c);
  c->code = ' ';
  c->name = NULL;
  c->width = 12000;
  c->ascent = 0;
  c->descent = 0;
  c->kern_pairs = NULL;
  c->kern_pair_cnt = 0;
  c->ligatures = NULL;
  c->ligature_cnt = 0;
  return c;
}

/* Reports the given MESSAGE at the current line in parser P
   and bails out with longjmp(). */
static void
afm_error (struct parser *p, const char *message, ...) 
{
  va_list args;
  char *msg;

  va_start (args, message);
  msg = xasprintf (message, args);
  va_end (args);

  error_at_line (0, 0, p->file_name, p->line_number, "%s", msg);
  free (msg);

  longjmp (p->bail_out, 1);
}

/* Parses an AFM file with parser P. */
static void
parse_afm (struct parser *p) 
{
  char *key;

  p->char_allocated = 0;
  p->max_code = 0;

  key = force_get_word (p);
  if (strcmp (key, "StartFontMetrics"))
    afm_error (p, _("first line must be StartFontMetrics"));
  skip_line (p);

  do
    {
      key = parse_key (p);
      if (!strcmp (key, "FontName")) 
        p->afm->findfont_name = pool_strdup (p->afm->pool,
                                             force_get_string (p));
      else if (!strcmp (key, "Ascender"))
        p->afm->ascent = force_get_integer (p);
      else if (!strcmp (key, "Descender"))
        p->afm->descent = force_get_integer (p);
      else if (!strcmp (key, "MappingScheme")) 
        {
          int scheme = force_get_integer (p);
          if (scheme == 4)
            p->afm->mapping = MAP_ONE_BYTE;
          else if (scheme == 2 || scheme == 5 || scheme == 6)
            p->afm->mapping = MAP_TWO_BYTE;
          else if (scheme == 3)
            p->afm->mapping = MAP_ESCAPE;
          else if (scheme == 7)
            p->afm->mapping = MAP_DOUBLE_ESCAPE;
          else if (scheme == 8)
            p->afm->mapping = MAP_SHIFT;
          else
            afm_error (p, _("unsupported MappingScheme %d"), scheme);
        }
      else if (!strcmp (key, "EscChar"))
        p->afm->escape_char = force_get_integer (p);
      else if (!strcmp (key, "StartDirection"))
        parse_direction (p);
      else if (!strcmp (key, "StartCharMetrics"))
        parse_char_metrics (p);
      else if (!strcmp (key, "StartKernPairs")
               || !strcmp (key, "StartKernPairs0"))
        parse_kern_pairs (p);
      else if (!strcmp (key, "StartTrackKern")) 
        skip_section (p, "EndTrackKern");
      else if (!strcmp (key, "StartComposites")) 
        skip_section (p, "EndComposites");
      else 
        skip_line (p);
    }
  while (strcmp (key, "EndFontMetrics"));

  if (p->afm->findfont_name == NULL)
    afm_error (p, _("required FontName is missing"));
  if (p->afm->mapping == MAP_UNKNOWN) 
    {
      /* There seem to be a number of fonts out there that use a
         2-byte encoding but don't announce it with
         MappingScheme. */
      p->afm->mapping = p->max_code > 255 ? MAP_TWO_BYTE : MAP_ONE_BYTE;
    }
}

/* Reads lines from parser P until one starts with END_KEY. */
static void
skip_section (struct parser *p, const char *end_key)
{
  const char *key;
  skip_line (p);
  do 
    {
      key = parse_key (p);
      skip_line (p);
    }
  while (strcmp (key, end_key));
}

/* Attempts to read an integer from parser P.
   If one is found, and it is nonzero, skips lines until END_KEY
   is encountered and returns false.
   Otherwise, skips the rest of the line and returns true.
   (This is useful because AFM files can have multiple sets of
   metrics.  Set 0 is for normal text, other sets are for
   vertical text, etc.  We only care about set 0.) */
static bool
parse_set_specific (struct parser *p, const char *end_key) 
{
  int set;
  
  if (get_integer (p, &set) && set != 0) 
    {
      skip_section (p, end_key);
      return false; 
    }
  else 
    {
      force_eol (p);
      return true;
    }
}

/* Parses a StartDirection...EndDirection section in parser P. */
static void
parse_direction (struct parser *p) 
{
  const char *key;

  if (!parse_set_specific (p, "EndDirection"))
    return;

  do 
    {
      key = parse_key (p);
      if (!strcmp (key, "CharWidth")) 
        p->afm->codes[0][0]->width = force_get_integer (p);
      skip_line (p);
    }
  while (strcmp (key, "EndDirection"));
}

/* Parses a StartCharMetrics...EndCharMetrics section in parser
   P. */
static void
parse_char_metrics (struct parser *p) 
{
  struct parsing_ligature 
    {
      struct afm_character *first;
      char *successor;
      char *ligature;
    };

  struct parsing_ligature *ligatures = NULL;
  size_t ligature_cnt = 0;
  size_t ligature_allocated = 0;

  size_t i;

  skip_line (p);
  
  for (;;)
    {
      char *key;
      struct afm_character *c;

      key = parse_key (p);
      if (!strcmp (key, "EndCharMetrics"))
        break;
      
      if (p->afm->char_cnt == p->char_allocated)
        p->afm->chars = pool_2nrealloc (p->afm->pool, p->afm->chars,
                                        &p->char_allocated,
                                        sizeof *p->afm->chars);
      c = create_character (p->afm);

      if (!strcmp (key, "C")) 
        c->code = force_get_integer (p);
      else if (!strcmp (key, "CH"))
        c->code = force_get_hex_code (p);
      else
        afm_error (p, _("CharMetrics line must start with C or CH"));
      if (c->code < 0 || c->code > 65535)
        c->code = -1;

      if (c->code > p->max_code)
        p->max_code = c->code;        

      p->afm->chars[p->afm->char_cnt++] = c;
      if (c->code != -1)
        p->afm->codes[c->code >> 8][c->code & 0xff] = c;

      key = force_get_word (p);
      while (!strcmp (key, ";")) 
        {
          if (!get_word (p, &key))
            break;

          if (!strcmp (key, "N"))
            c->name = force_get_word (p);
          else if (!strcmp (key, "WX") || !strcmp (key, "W0X"))
            c->width = force_get_number (p);
          else if (!strcmp (key, "W") || !strcmp (key, "W0")) 
            {
              c->width = force_get_number (p);
              force_get_number (p);
            }
          else if (!strcmp (key, "B")) 
            {
              int llx, lly, urx, ury;
              llx = force_get_number (p);
              lly = force_get_number (p);
              urx = force_get_number (p);
              ury = force_get_number (p);
              c->ascent = MAX (0, ury);
              c->descent = MAX (0, -lly);
            }
          else if (!strcmp (key, "L")) 
            {
              struct parsing_ligature *ligature;
              if (ligature_cnt == ligature_allocated)
                ligatures = pool_2nrealloc (p->pool, ligatures,
                                            &ligature_allocated,
                                            sizeof *ligatures);
              ligature = &ligatures[ligature_cnt++];
              ligature->first = c;
              ligature->successor = force_get_word (p);
              ligature->ligature = force_get_word (p);
            }
          else 
            {
              while (strcmp (key, ";"))
                key = force_get_word (p);
              continue;
            }
          if (!get_word (p, &key))
            break;
        }
    }
  skip_line (p);

  for (i = 0; i < ligature_cnt; i++) 
    {
      struct parsing_ligature *src = &ligatures[i];
      struct afm_ligature *dst;
      src->first->ligatures = pool_nrealloc (p->afm->pool,
                                             src->first->ligatures,
                                             src->first->ligature_cnt + 1,
                                             sizeof *src->first->ligatures);
      dst = &src->first->ligatures[src->first->ligature_cnt++];
      dst->successor = get_char_by_name (p, src->successor);
      dst->ligature = get_char_by_name (p, src->ligature);
    }
}

/* Parses a StartKernPairs...EndKernPairs section in parser P. */
static void
parse_kern_pairs (struct parser *p) 
{
  char *key;
  
  skip_line (p);

  do
    {
      struct afm_character *c1, *c2;
      int adjust;

      key = parse_key (p);
      if (!strcmp (key, "KP") || !strcmp (key, "KPX")) 
        {
          c1 = get_char_by_name (p, force_get_word (p));
          c2 = get_char_by_name (p, force_get_word (p));
          adjust = force_get_number (p);
          if (!strcmp (key, "KP"))
            force_get_number (p);
          add_kern_pair (p, c1, c2, adjust);
        }
      else if (!strcmp (key, "KPH")) 
        {
          c1 = get_char_by_code (p->afm, force_get_hex_code (p));
          c2 = get_char_by_code (p->afm, force_get_hex_code (p));
          adjust = force_get_number (p);
          force_get_number (p);
          add_kern_pair (p, c1, c2, adjust);
        }
      else
        skip_line (p);
    }
  while (strcmp (key, "EndKernPairs"));
}

/* Adds a kern pair that adjusts (FIRST, SECOND) by ADJUST units
   to the metrics within parser P. */
static void
add_kern_pair (struct parser *p, struct afm_character *first,
               struct afm_character *second, int adjust) 
{
  struct afm_kern_pair *kp;
  
  first->kern_pairs = pool_nrealloc (p->afm->pool, first->kern_pairs,
                                     first->kern_pair_cnt + 1,
                                     sizeof *first->kern_pairs);
  kp = &first->kern_pairs[first->kern_pair_cnt++];
  kp->successor = second;
  kp->adjust = adjust;
}

/* Returns the character with the given NAME with the metrics for
   parser P.  Reports an error if no character has the given
   name. */
static struct afm_character *
get_char_by_name (struct parser *p, const char *name) 
{
  size_t i;

  for (i = 0; i < p->afm->char_cnt; i++) 
    {
      struct afm_character *c = p->afm->chars[i];
      if (c->name != NULL && !strcmp (c->name, name))
        return c;
    }
  afm_error (p, _("reference to unknown character \"%s\""), name);
}

/* Returns the character with the given CODE within AFM.
   Returns a default character if the font doesn't have a
   character with that code. */
static struct afm_character *
get_char_by_code (const struct afm *afm, int code_) 
{
  uint16_t code = code_;
  return afm->codes[code >> 8][code & 0xff];
}

/* Skips white space, except for new-lines, within parser P. */
static int
skip_spaces (struct parser *p) 
{
  int c;
  while (isspace (c = getc (p->file)) && c != '\n')
    continue;
  ungetc (c, p->file);
  return c;
}

/* Parses a word at the beginning of a line.
   Skips comments.
   Reports an error if not at the beginning of a line. */
static char *
parse_key (struct parser *p) 
{
  force_eol (p);
  for (;;) 
    {
      char *key;

      do 
        {
          p->line_number++;
          getc (p->file); 
        }
      while (skip_spaces (p) == '\n');

      key = force_get_word (p);
      if (strcmp (key, "Comment")) 
        return key;

      skip_line (p);
    }
}

/* Skips to the next line within parser P. */
static void
skip_line (struct parser *p) 
{
  for (;;) 
    {
      int c = getc (p->file);
      if (c == EOF)
        afm_error (p, _("expected end of file"));
      if (c == '\n')
        break;
    }
  ungetc ('\n', p->file);
}

/* Ensures that parser P is at the end of a line. */
static void
force_eol (struct parser *p) 
{
  if (skip_spaces (p) != '\n')
    afm_error (p, _("syntax error expecting end of line"));
}
  
/* Tries to read an integer into *INTEGER at the current position
   in parser P.
   Returns success. */
static bool
get_integer (struct parser *p, int *integer) 
{
  int c = skip_spaces (p);
  if (isdigit (c) || c == '-') 
    {
      char *tail;
      long tmp;

      errno = 0;
      tmp = strtol (force_get_word (p), &tail, 10);
      if (errno == ERANGE || tmp < INT_MIN || tmp > INT_MAX) 
        afm_error (p, _("number out of valid range"));
      if (*tail != '\0')
        afm_error (p, _("invalid numeric syntax"));
      *integer = tmp;

      return true;
    }
  else
    return false;
}

/* Returns an integer read from the current position in P.
   Reports an error if unsuccessful. */
static int
force_get_integer (struct parser *p) 
{
  int integer;
  if (!get_integer (p, &integer))
    afm_error (p, _("syntax error expecting integer"));
  return integer;
}

/* Tries to read a floating-point number at the current position
   in parser P.  Stores the number's integer part into *INTEGER.
   Returns success. */
static bool
get_number (struct parser *p, int *integer) 
{
  int c = skip_spaces (p);
  if (c == '-' || c == '.' || isdigit (c))
    {
      char *tail;
      double number;

      errno = 0;
      number = c_strtod (force_get_word (p), &tail);
      if (errno == ERANGE || number < INT_MIN || number > INT_MAX) 
        afm_error (p, _("number out of valid range"));
      if (*tail != '\0')
        afm_error (p, _("invalid numeric syntax"));
      *integer = number;

      return true;
    }
  else
    return false;
}

/* Returns the integer part of a floating-point number read from
   the current position in P.
   Reports an error if unsuccessful. */
static int
force_get_number (struct parser *p) 
{
  int integer;
  if (!get_number (p, &integer))
    afm_error (p, _("syntax error expecting number"));
  return integer;
}

/* Tries to read an integer expressed in hexadecimal into
   *INTEGER from P.
   Returns success. */
static bool
get_hex_code (struct parser *p, int *integer) 
{
  if (skip_spaces (p) == '<') 
    {
      if (fscanf (p->file, "<%x", integer) != 1 || getc (p->file) != '>')
        afm_error (p, _("syntax error in hex constant"));
      return true;
    }
  else
    return false;
}

/* Reads an integer expressed in hexadecimal and returns its
   value.
   Reports an error if unsuccessful. */
static int
force_get_hex_code (struct parser *p)
{
  int integer;
  if (!get_hex_code (p, &integer))
    afm_error (p, _("syntax error expecting hex constant"));
  return integer;
}

/* Tries to read a word from P into *WORD.
   The word is allocated in P's pool.
   Returns success. */
static bool
get_word (struct parser *p, char **word) 
{
  if (skip_spaces (p) != '\n') 
    {
      struct string s;
      int c;

      ds_init (&s, 0);
      while (!isspace (c = getc (p->file)) && c != EOF)
        ds_putc (&s, c);
      ungetc (c, p->file);
      *word = ds_c_str (&s);
      pool_register (p->pool, free, *word);
      return true;
    }
  else 
    {
      *word = NULL;
      return false;
    }
}

/* Reads a word from P and returns it.
   The word is allocated in P's pool.
   Reports an error if unsuccessful. */
static char *
force_get_word (struct parser *p) 
{
  char *word;
  if (!get_word (p, &word))
    afm_error (p, _("unexpected end of line"));
  return word;
}

/* Reads a string, consisting of the remainder of the current
   line, from P, and stores it in *STRING.
   Leading and trailing spaces are removed.
   The word is allocated in P's pool.
   Returns true if a non-empty string was successfully read,
   false otherwise. */
static bool
get_string (struct parser *p, char **string)
{
  struct string s;

  ds_init (&s, 0);
  skip_spaces (p);
  for (;;) 
    {
      int c = getc (p->file);
      if (c == EOF || c == '\n')
        break;
      ds_putc (&s, c);
    }
  ungetc ('\n', p->file);
  ds_rtrim_spaces (&s);

  if (!ds_is_empty (&s)) 
    {
      *string = ds_c_str (&s);
      pool_register (p->pool, free, *string);
      return true;
    }
  else 
    {
      *string = NULL;
      ds_destroy (&s);
      return false;
    }
}

/* Reads a string, consisting of the remainder of the current
   line, from P, and returns it.
   Leading and trailing spaces are removed.
   The word is allocated in P's pool.
   Reports an error if the string is empty. */
static char *
force_get_string (struct parser *p) 
{
  char *string;
  if (!get_string (p, &string))
    afm_error (p, _("unexpected end of line expecting string"));
  return string;
}

/* Closes AFM and frees its storage. */
void
afm_close (struct afm *afm) 
{
  if (afm != NULL)
    pool_destroy (afm->pool);
}

/* Returns the string that must be passed to the PostScript
   "findfont" operator to obtain AFM's font. */
const char *
afm_get_findfont_name (const struct afm *afm) 
{
  return afm->findfont_name;
}

/* Returns the ascent for AFM, that is, the font's height above
   the baseline, in units of 1/1000 of the nominal font size. */
int
afm_get_ascent (const struct afm *afm) 
{
  return afm->ascent;
}

/* Returns the descent for AFM, that is, the font's depth below
   the baseline, in units of 1/1000 of the nominal font size. */
int
afm_get_descent (const struct afm *afm) 
{
  return afm->descent;
}

/* Returns the character numbered CODE within AFM,
   or a default character if the font has none. */
const struct afm_character *
afm_get_character (const struct afm *afm, int code) 
{
  return get_char_by_code (afm, code);
}

/* Returns the ligature formed when FIRST is followed by SECOND,
   or a null pointer if there is no such ligature. */
const struct afm_character *
afm_get_ligature (const struct afm_character *first,
                  const struct afm_character *second) 
{
  size_t i;

  for (i = 0; i < first->ligature_cnt; i++)
    if (first->ligatures[i].successor == second)
      return first->ligatures[i].ligature;
  return NULL;
}

/* Returns the pair kerning x-adjustment when FIRST is followed
   by SECOND, or 0 if no pair kerning should be done for the
   given pair of characters. */
int
afm_get_kern_adjustment (const struct afm_character *first,
                         const struct afm_character *second)
{
  size_t i;

  for (i = 0; i < first->kern_pair_cnt; i++)
    if (first->kern_pairs[i].successor == second)
      return first->kern_pairs[i].adjust;
  return 0;
}

/* Encodes the N characters in S as a PostScript string in OUT,
   using a single-byte encoding.
   Returns the number of characters remaining after all those
   that could be successfully encoded were. */
static size_t
encode_one_byte (const struct afm_character **s, size_t n,
                 struct string *out)
{
  ds_putc (out, '(');
  for (; n > 0; s++, n--)
    {
      uint8_t code = (*s)->code;
      if (code != (*s)->code)
        break;
          
      if (code == '(' || code == ')' || code == '\\')
        ds_printf (out, "\\%c", code);
      else if (!c_isprint (code))
        ds_printf (out, "\\%03o", code);
      else
        ds_putc (out, code); 
    }
  ds_putc (out, ')');
  return n;
}

/* State of binary encoder for PostScript. */
struct binary_encoder
  {
    struct string *out;         /* Output string. */
    uint32_t b;                 /* Accumulated bytes for base-85 encoding. */
    size_t n;                   /* Number of bytes in b (0...3). */
  };

/* Initializes encoder E for output to OUT. */
static void
binary_init (struct binary_encoder *e, struct string *out) 
{
  e->out = out;
  e->b = e->n = 0;
}

/* Returns the character that represents VALUE in ASCII85
   encoding. */
static int
value_to_ascii85 (int value) 
{
  assert (value >= 0 && value < 85);
#if C_CTYPE_ASCII
  return value + 33;
#else
  return ("!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJK"
          "LMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstu")[value];
#endif
}

/* Appends the first N characters of the ASCII85 representation
   of B to string OUT. */
static void
append_ascii85_block (unsigned b, size_t n, struct string *out) 
{
  char c[5];
  int i;

  for (i = 4; i >= 0; i--) 
    {
      c[i] = value_to_ascii85 (b % 85);
      b /= 85; 
    }
  ds_concat (out, c, n);
}

/* Encodes BYTE with encoder E. */
static void
binary_put (struct binary_encoder *e, uint8_t byte) 
{
  e->b = (e->b << 8) | byte;
  e->n++;
  if (e->n % 4 == 0)
    {
      if (e->n == 4)
        ds_puts (e->out, "<~");

      if (e->b != 0)
        append_ascii85_block (e->b, 5, e->out);
      else 
        ds_putc (e->out, 'z');
    }
}

/* Finishes up encoding with E. */
static void
binary_finish (struct binary_encoder *e) 
{
  if (e->n >= 4) 
    {
      /* We output at least one complete ASCII85 block.
         Finish up. */
      size_t n = e->n % 4;
      if (n > 0)
        append_ascii85_block (e->b << 8 * (4 - n), n + 1, e->out); 
      ds_puts (e->out, "~>");
    }
  else if (e->n > 0)
    {
      /* It's cheaper (or at least the same cost) to encode this
         string in hexadecimal. */
      uint32_t b;
      size_t i;

      ds_puts (e->out, "<");
      b = e->b << 8 * (4 - e->n);
      for (i = 0; i < e->n; i++) 
        {
          ds_printf (e->out, "%02x", b >> 24);
          b <<= 8;
        }
      ds_puts (e->out, ">");
    }
  else 
    {
      /* Empty string. */
      ds_puts (e->out, "()"); 
    }
}

/* Encodes the N characters in S into encoder E,
   using a two-byte encoding.
   Returns the number of characters remaining after all those
   that could be successfully encoded were. */
static size_t
encode_two_byte (const struct afm_character **s, size_t n,
                 struct binary_encoder *e)
{
  for (; n > 0; s++, n--) 
    {
      uint16_t code = (*s)->code;
      if (code != (*s)->code)
        break;

      binary_put (e, code >> 8);
      binary_put (e, code);
    }
  return n;
}

/* Encodes the N characters in S into encoder E,
   using an escape-based encoding with ESCAPE_CHAR as escape.
   Returns the number of characters remaining after all those
   that could be successfully encoded were. */
static size_t
encode_escape (const struct afm_character **s, size_t n,
               unsigned char escape_char,
               struct binary_encoder *e)
{
  uint8_t cur_font = 0;

  for (; n > 0; s++, n--)
    {
      uint16_t code = (*s)->code;
      uint8_t font_num = code >> 8;
      uint8_t char_num = code & 0xff;
      if (code != (*s)->code)
        break;

      if (font_num != cur_font) 
        {
          if (font_num == escape_char)
            break;
          binary_put (e, escape_char);
          binary_put (e, font_num);
          cur_font = font_num;
        }
      binary_put (e, char_num);
    }
  return n;
}

/* Encodes the N characters in S into encoder E,
   using an double escape-based encoding with ESCAPE_CHAR as
   escape.
   Returns the number of characters remaining after all those
   that could be successfully encoded were. */
static size_t
encode_double_escape (const struct afm_character **s, size_t n,
                      unsigned char escape_char,
                      struct binary_encoder *e)
{
  unsigned cur_font = 0;

  for (; n > 0; s++, n--)
    {
      unsigned font_num = (*s)->code >> 8;
      uint8_t char_num = (*s)->code & 0xff;
      if ((*s)->code & ~0x1ffff)
        break;

      if (font_num != cur_font) 
        {
          if (font_num == (escape_char & 0xff))
            break;
          if (font_num >= 256)
            binary_put (e, escape_char);
          binary_put (e, escape_char);
          binary_put (e, font_num & 0xff);
          cur_font = font_num;
        }
      binary_put (e, char_num);
    }
  return n;
}

/* Encodes the N characters in S into encoder E,
   using a shift-based encoding with SHIFT_IN and SHIFT_OUT as
   shift characters.
   Returns the number of characters remaining after all those
   that could be successfully encoded were. */
static size_t
encode_shift (const struct afm_character **s, size_t n,
              unsigned char shift_in, unsigned char shift_out,
              struct binary_encoder *e)
{
  unsigned cur_font = 0;

  for (; n > 0; s++, n--)
    {
      int font_num = ((*s)->code & 0x100) != 0;
      uint8_t char_num = (*s)->code & 0xff;
      if ((*s)->code & ~0x1ff)
        break;

      if (font_num != cur_font) 
        {
          binary_put (e, font_num ? shift_out : shift_in);
          cur_font = font_num;
        }
      binary_put (e, char_num);
    }
  return n;
}

/* Encodes the N characters in S into a PostScript string in OUT,
   according to AFM's character encoding.
   Returns the number of characters successfully encoded,
   which may be less than N if an unencodable character was
   encountered. */
size_t
afm_encode_string (const struct afm *afm,
                   const struct afm_character **s, size_t n,
                   struct string *out) 
{
  size_t initial_length = ds_length (out);
  size_t chars_left;

  if (afm->mapping == MAP_ONE_BYTE)
    chars_left = encode_one_byte (s, n, out);
  else 
    {
      struct binary_encoder e;

      binary_init (&e, out);
      switch (afm->mapping) 
        {
        case MAP_TWO_BYTE:
          chars_left = encode_two_byte (s, n, &e);
          break;

        case MAP_ESCAPE:
          chars_left = encode_escape (s, n, afm->escape_char, &e);
          break;
      
        case MAP_DOUBLE_ESCAPE:
          chars_left = encode_double_escape (s, n, afm->escape_char, &e);
          break;

        case MAP_SHIFT:
          chars_left = encode_shift (s, n, afm->shift_in, afm->shift_out, &e);
          break;
      
        default:
          abort ();
        }
      binary_finish (&e);
    }

  if (chars_left == n)
    ds_truncate (out, initial_length);
  return n - chars_left;
}
