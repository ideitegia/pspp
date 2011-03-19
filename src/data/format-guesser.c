/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2010, 2011 Free Software Foundation, Inc.

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

#include "data/format-guesser.h"

#include <stdlib.h>
#include <string.h>

#include "data/format.h"
#include "data/settings.h"
#include "libpspp/assertion.h"
#include "libpspp/str.h"

#include "gl/c-ctype.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

/* A token in which potential date or time fields are broken.

   The token type is actually a bit-map.  This allows a single
   token to represent multiple roles, as often happens in parsing
   adate or a time.  For example, the number "1" can be a quarter
   of the year, month, hour, day of the month, week of the year,
   or a count of days.  Such ambiguities are resolved on the
   higher-level bases of multiple tokens and multiple full
   dates. */
enum date_token
  {
    DT_DAY = 1 << 0,            /* dd: Day of the month. */
    DT_MONTH = 1 << 1,          /* mm: Month. */
    DT_ENGLISH_MONTH = 1 << 2,  /* mmm: Spelled-out month, e.g. "jan". */
    DT_YEAR = 1 << 3,           /* yy: Year. */

    DT_HOUR = 1 << 4,           /* HH: Hour. */
    DT_MINUTE = 1 << 5,         /* MM: Minute. */
    DT_SECOND = 1 << 6,         /* SS: Second. */

    DT_WEEKDAY = 1 << 7,        /* www: Day of the week. */

    DT_DAY_COUNT = 1 << 8,      /* D: Number of days. */
    DT_WEEK = 1 << 9,           /* ww: Week of the year. */
    DT_QUARTER = 1 << 10,       /* q: Quarter of the year. */

    DT_Q = 1 << 11,             /* Literal "Q". */
    DT_WK = 1 << 12,            /* Literal "WK". */

    DT_DELIM = 1 << 13,         /* One of -/., or white space. */
    DT_SPACE = 1 << 14,         /* Any white space. */
    DT_COLON = 1 << 15,         /* : */
  };

/* Syntax of a date format, in terms of the date tokens that
   compose it.*/
struct date_syntax
  {
    enum fmt_type format;       /* Format type. */
#define MAX_TOKENS 11
    size_t token_cnt;           /* Number of tokens. */
    enum date_token tokens[MAX_TOKENS]; /* Tokens. */
  };

/* Syntax of all the data formats that we can parse.

   The order in the array can make a difference in the final
   choice of formats: in the case of a tie between the number of
   times each format is seen, the syntax earlier in the array
   takes precedence.  The most important cases are the ordering
   of DATE before EDATE, so that spelled-out months in input
   yield DATE format (that produces spelled-out months in output,
   and the ordering of EDATE before ADATE, so that ambiguous
   dates such as "1/1/99" yield the more sensible European date
   format instead of American format.

   When a given date format has more than one syntax, they must
   be in adjacent array elements. */
static struct date_syntax syntax[] =
  {
    /* dd-mmm-yy */
    { FMT_DATE, 5, {DT_DAY, DT_DELIM, DT_ENGLISH_MONTH, DT_DELIM, DT_YEAR} },

    /* dd.mm.yy */
    { FMT_EDATE, 5, {DT_DAY, DT_DELIM, DT_MONTH, DT_DELIM, DT_YEAR} },

    /* mm/dd/yy */
    { FMT_ADATE, 5, {DT_MONTH, DT_DELIM, DT_DAY, DT_DELIM, DT_YEAR} },

    /* yy/mm/dd */
    { FMT_SDATE, 5, {DT_YEAR, DT_DELIM, DT_MONTH, DT_DELIM, DT_DAY} },

    /* mmm yy */
    { FMT_MOYR, 3, {DT_MONTH, DT_DELIM, DT_YEAR} },

    /* q Q yy */
    { FMT_QYR, 3, {DT_QUARTER, DT_Q, DT_YEAR} },

    /* ww WK yy */
    { FMT_WKYR, 3, {DT_WEEK, DT_WK, DT_YEAR} },

    /* dd-mmm-yyyy HH:MM */
    { FMT_DATETIME,
      9, {DT_DAY, DT_DELIM, DT_MONTH, DT_DELIM, DT_YEAR, DT_SPACE, DT_HOUR,
          DT_COLON, DT_MINUTE} },
    /* dd-mmm-yyyy HH:MM:SS */
    { FMT_DATETIME,
      11, {DT_DAY, DT_DELIM, DT_MONTH, DT_DELIM, DT_YEAR, DT_SPACE, DT_HOUR,
           DT_COLON, DT_MINUTE, DT_COLON, DT_SECOND} },

    /* HH:MM */
    { FMT_TIME, 3, {DT_HOUR, DT_COLON, DT_MINUTE} },
    /* HH:MM:SS */
    { FMT_TIME, 5, {DT_HOUR, DT_COLON, DT_MINUTE, DT_COLON, DT_SECOND} },

    /* D HH:MM */
    { FMT_DTIME, 5, {DT_DAY_COUNT, DT_SPACE, DT_HOUR, DT_COLON, DT_MINUTE} },
    /* D HH:MM:SS */
    { FMT_DTIME,
      7, {DT_DAY_COUNT, DT_SPACE, DT_HOUR, DT_COLON, DT_MINUTE, DT_COLON,
          DT_SECOND} },

    /* www */
    { FMT_WKDAY, 1, {DT_WEEKDAY} },

    /* mmm

       We require a spelled-out English month so that
       single-character Roman numerals like "i" and "x" don't get
       detected as months.  The latter is particularly common in
       the password field of /etc/passwd-like files. */
    { FMT_MONTH, 1, {DT_ENGLISH_MONTH} },
  };

/* Number of recognized date syntax formats. */
#define DATE_SYNTAX_CNT (sizeof syntax / sizeof *syntax)

/* A format guesser. */
struct fmt_guesser
  {
    /* Maximum observed input width. */
    unsigned int width;

    /* Sum of the digits after the decimal point in each input
       (divide by count to obtain average decimal positions). */
    unsigned int decimals;

    /* Number of non-empty, non-missing input values.

       count is the sum of any_numeric, any_date, and the number
       of inputs that were not in any recognized format (hence,
       treated as A format). */
    unsigned int count;

    /* Numeric input formats. */
    unsigned int any_numeric;   /* Sum of following counts. */
    unsigned int f;             /* Number of inputs in F format. */
    unsigned int comma;         /* Number of inputs in COMMA format. */
    unsigned int dot;           /* Number of inputs in DOT format. */
    unsigned int dollar;        /* Number of inputs in DOLLAR format. */
    unsigned int pct;           /* Number of inputs in PCT format. */
    unsigned int e;             /* Number of inputs in E format. */

    /* Date or time input formats.

       The sum of the values in the date array is at least
       any_date, often higher because many example dates match
       more than one date format. */
    unsigned int any_date;      /* Number of inputs in any date format. */
    unsigned int date[DATE_SYNTAX_CNT]; /* Number of inputs in each date
                                           format. */
  };

static bool add_numeric (struct fmt_guesser *, struct substring);
static void guess_numeric (struct fmt_guesser *, struct fmt_spec *);
static void add_date_time (struct fmt_guesser *, struct substring);
static bool match_date_syntax (const enum date_token a[], size_t a_len,
                               const enum date_token b[], size_t b_len);
static void guess_date_time (struct fmt_guesser *, struct fmt_spec *);
static enum date_token parse_date_token (struct substring *,
                                         enum date_token tokens_seen,
                                         int *decimals);
static enum date_token parse_date_number (struct substring *,
                                          enum date_token tokens_seen,
                                          int *decimals);
static enum date_token recognize_identifier_token (struct substring *);
static enum date_token recognize_id2 (int s0, int s1, bool more);
static enum date_token recognize_id3 (int s0, int s1, int s2, bool more);

/* Creates and returns a new format guesser. */
struct fmt_guesser *
fmt_guesser_create (void)
{
  struct fmt_guesser *g = xmalloc (sizeof *g);
  fmt_guesser_clear (g);
  return g;
}

/* Destroys format guesser G. */
void
fmt_guesser_destroy (struct fmt_guesser *g)
{
  free (g);
}

/* Clears the state of format guesser G, making it available for
   guessing the format of a new input stream.  */
void
fmt_guesser_clear (struct fmt_guesser *g)
{
  memset (g, 0, sizeof *g);
}

/* Appends S to the stream of data items whose format G is
   guessing. */
void
fmt_guesser_add (struct fmt_guesser *g, struct substring s)
{
  if (ss_length (s) > g->width)
    g->width = ss_length (s);
  ss_trim (&s, ss_cstr (CC_SPACES));
  if (ss_is_empty (s) || ss_equals (s, ss_cstr (".")))
    {
      /* Can't guess anything from an empty string or a missing value. */
      return;
    }

  g->count++;
  if (!add_numeric (g, s))
    add_date_time (g, s);
}

/* Guesses the format of the input previously added to G using
   fmt_guesser_add, storing the guess into *F.  The guessed
   format may not actually a valid input or output format, in
   that its width and number of decimal places may be outside the
   valid range for the guessed format type.  The caller must
   therefore adjust the format to make it valid, e.g. by calling
   fmt_fix. */
void
fmt_guesser_guess (struct fmt_guesser *g, struct fmt_spec *f)
{
  if (g->count > 0)
    {
      /* Set defaults.  The guesser functions typically override
         the width and type. */
      f->type = FMT_A;
      f->w = g->width;
      f->d = 0;

      if (g->any_numeric > g->count / 2)
        guess_numeric (g, f);
      else if (g->any_date > g->count / 2)
        guess_date_time (g, f);
    }
  else
    {
      /* No data at all.  Use fallback default. */
      *f = fmt_default_for_width (0);
    }
}

/* Numeric formats. */

/* Tries to parse S as a numeric (F, COMMA, DOT, DOLLAR, PCT, or
   E) format.  If successful, increments G's any_numeric counter
   and the counter for the specific format S that S matches and
   returns true.  On failure, returns false without modifying G.

   This function is intended to match exactly the same set of
   strings that the actual numeric value parsers used by the
   data_in function would match. */
static bool
add_numeric (struct fmt_guesser *g, struct substring s)
{
  bool has_dollar;              /* '$' appeared at start of S? */
  bool has_percent;             /* '%' appeared at end of S? */
  int digits;                   /* Number of digits in S (before exponent). */
  int dots;                     /* Number of '.' in S. */
  int commas;                   /* Number of ',' in S. */
  bool has_exp;                 /* [eEdD] appeared introducing exponent? */
  bool has_exp_sign;            /* '+' or '-' appeared in exponent? */
  int exp_digits;               /* Number of digits in exponent. */

  int prev_delim;       /* Initially 0, then ',' or '.' as delimiters seen. */
  int delim_digits;             /* Number of digits since last delimiter. */

  int decimal;                  /* Decimal point character: '.', ',',
                                   or 0 if unknown or no decimal point in S. */
  int precision;                /* Digits of precision after decimal point. */

  int c;

  /* Skip leading "$" and optional following white space. */
  has_dollar = ss_match_byte (&s, '$');
  if (has_dollar)
    ss_ltrim (&s, ss_cstr (CC_SPACES));

  /* Skip optional sign. */
  ss_match_byte_in (&s, ss_cstr ("+-"));

  /* Skip digits punctuated by commas and dots.  We don't know
     whether the decimal point is a comma or a dot, so for now we
     just count them.  */
  digits = dots = commas = 0;
  delim_digits = 0;
  prev_delim = 0;
  for (; (c = ss_first (s)) != -1; ss_advance (&s, 1))
    {
      if (c >= '0' && c <= '9')
        {
          digits++;
          if (dots || commas)
            delim_digits++;
        }
      else if (c == '.' )
        {
          dots++;
          prev_delim = c;
          delim_digits = 0;
        }
      else if (c == ',')
        {
          commas++;
          prev_delim = c;
          delim_digits = 0;
        }
      else
        break;
    }
  if (digits == 0 || (dots > 1 && commas > 1))
    {
      /* A valid number has at least one digit and can't have
         more than one decimal point. */
      return false;
    }

  /* Skip the optional exponent. */
  has_exp = ss_match_byte_in (&s, ss_cstr ("eEdD")) != EOF;
  has_exp_sign = ss_match_byte_in (&s, ss_cstr ("-+")) != EOF;
  if (has_exp_sign)
    ss_match_byte (&s, ' ');
  exp_digits = ss_ltrim (&s, ss_cstr (CC_DIGITS));
  if ((has_exp || has_exp_sign) && !exp_digits)
    {
      /* Can't have the E or sign that leads in the exponent
         without actually having an exponent. */
      return false;
    }

  /* Skip optional '%'. */
  has_percent = ss_match_byte (&s, '%');
  if (has_dollar && has_percent)
    {
      /* A valid number cannot have both '$' and '%'. */
      return false;
    }

  /* Make sure there's no trailing garbage. */
  if (!ss_is_empty (s))
    return false;

  /* Figure out the decimal point (and therefore grouping)
     character and the number of digits following the decimal
     point.  Sometimes the answer is ambiguous. */
  if (dots > 1 && prev_delim == '.')
    {
      /* Can't have multiple decimal points, so '.' must really
         be the grouping character, with a precision of 0. */
      decimal = ',';
      precision = 0;
    }
  else if (commas > 1 && prev_delim == ',')
    {
      /* Can't have multiple decimal points, so ',' must really
         be the grouping character, with a precision of 0. */
      decimal = '.';
      precision = 0;
    }
  else if (delim_digits == 3 && (!dots || !commas))
    {
      /* The input is something like "1.234" or "1,234" where we
         can't tell whether the ',' or '.' is a grouping or
         decimal character.  Assume that the decimal character
         from the settings is in use. */
      if (prev_delim == settings_get_decimal_char (FMT_F))
        {
          decimal = prev_delim;
          precision = delim_digits;
        }
      else
        {
          decimal = prev_delim == '.' ? ',' : '.';
          precision = 0;
        }
    }
  else
    {
      /* The final delimiter is a decimal point, and the digits
         following it are decimals. */
      decimal = prev_delim;
      precision = delim_digits;
    }

  /* Decide the most likely format. */
  g->any_numeric++;
  g->decimals += precision;
  if (has_dollar)
    g->dollar++;
  else if (has_percent)
    g->pct++;
  else if (commas && decimal == '.')
    g->comma++;
  else if (dots && decimal == ',')
    g->dot++;
  else if (has_exp || has_exp_sign)
    g->e++;
  else
    g->f++;

  return true;
}

/* Guess which numeric format is most likely represented by G,
   and store it in F's type and d members.  (f->w is already
   initialized.) */
static void
guess_numeric (struct fmt_guesser *g, struct fmt_spec *f)
{
  int decimal_char = settings_get_decimal_char (FMT_COMMA);

  f->d = g->decimals / g->count;
  if (g->pct)
    f->type = FMT_PCT;
  else if (g->dollar)
    f->type = FMT_DOLLAR;
  else if (g->comma > g->dot)
    f->type = decimal_char == '.' ? FMT_COMMA : FMT_DOT;
  else if (g->dot > g->comma)
    f->type = decimal_char == '.' ? FMT_DOT : FMT_COMMA;
  else if (g->e > g->any_numeric / 2)
    f->type = FMT_E;
  else
    f->type = FMT_F;
}

/* Tries to parse S as a date (DATE, ADATE, EDATE, SDATE, QYR,
   MOYR, WKYR, or DATETIME), time (TIME or DTIME), or date
   component (WKDAY or MONTH) format.  If successful, increments
   G's any_date counter and the counter or counters for the
   specific format(s) that S matches.  On failure, does not
   modify G.

   Does not attempt to recognize JDATE format: it looks just like
   F format and will thus be caught by the numeric parser.

   This function is intended to match a set of strings close to
   those that actual date and time parsers used by the data_in
   function would match, but somewhat pickier.  In particular,
   minutes and seconds are only recognized when they have exactly
   two digits: "1:02:03" is a valid time, but "1:2:3" is
   rejected.  */
static void
add_date_time (struct fmt_guesser *g, struct substring s)
{
  enum date_token token;
  enum date_token tokens[MAX_TOKENS];
  enum date_token tokens_seen;
  size_t token_cnt;
  int decimals;
  bool is_date;
  int i;

  /* Break S into tokens. */
  token_cnt = 0;
  tokens_seen = 0;
  decimals = 0;
  while (!ss_is_empty (s))
    {
      if (token_cnt >= MAX_TOKENS)
        return;

      token = parse_date_token (&s, tokens_seen, &decimals);
      if (token == 0)
        return;
      tokens[token_cnt++] = token;
      tokens_seen |= token;
    }
  if (token_cnt == 0)
    return;

  /* Find matching date formats, if any, and increment the
     counter for each one of them. */
  is_date = false;
  for (i = 0; i < DATE_SYNTAX_CNT; i++)
    {
      struct date_syntax *s = &syntax[i];
      if (match_date_syntax (tokens, token_cnt, s->tokens, s->token_cnt))
        {
          is_date = true;
          g->date[i]++;
        }
    }
  if (is_date)
    {
      g->any_date++;
      g->decimals += decimals;
    }
}

/* Returns true if the A_LEN tokens in A[] match the B_LEN tokens
   in B[], false otherwise. */
static bool
match_date_syntax (const enum date_token a[], size_t a_len,
                   const enum date_token b[], size_t b_len)
{
  size_t i;

  if (a_len != b_len)
    return false;

  for (i = 0; i < a_len; i++)
    if (!(a[i] & b[i]))
      return false;

  return true;
}

/* Guess which date or time format is most likely represented by
   G, and store it in F's type and d members.  (f->w is already
   initialized.) */
static void
guess_date_time (struct fmt_guesser *g, struct fmt_spec *f)
{
  unsigned int max = 0;
  int i, j;

  /* Choose the date format matched by the most inputs.  Break
     ties by choosing the earliest in the array. */
  for (i = 0; i < DATE_SYNTAX_CNT; i = j)
    {
      unsigned int sum = g->date[i];
      for (j = i + 1; j < DATE_SYNTAX_CNT; j++)
        {
          if (syntax[i].format != syntax[j].format)
            break;
          sum += g->date[j];
        }
      if (sum > max)
        {
          f->type = syntax[i].format;
          max = sum;
        }
    }

  /* Formats that include a time have an optional seconds field.
     If we saw a seconds field in any of the inputs, make sure
     that the field width is large enough to include for them.
     (We use the minimum input width, but an output width would
     be equally appropriate, since all the time formats have the
     same minimum widths for input and output.)  */
  if (f->type == FMT_DATETIME || f->type == FMT_TIME
      || f->type == FMT_DTIME)
    {
      for (i = 0; i < DATE_SYNTAX_CNT; i++)
        if (g->date[i]
            && syntax[i].tokens[syntax[i].token_cnt - 1] == DT_SECOND)
          {
            f->d = g->decimals / g->count;
            f->w = MAX (f->w, fmt_min_input_width (f->type) + 3);
          }
    }
}

/* Extracts the next date token from the string represented by S,
   which must not be an empty string, and advances *S past the
   end of the token.  Returns the parsed token, or 0 if no valid
   token was found.

   TOKENS_SEEN should be a bitmap representing all the tokens
   already seen in this input; this is used to resolve some
   otherwise ambiguous parsing situation.  If a count of seconds
   is parsed, *DECIMALS is set to the number of digits after the
   decimal point.  */
static enum date_token
parse_date_token (struct substring *s, enum date_token tokens_seen,
                  int *decimals)
{
  int c = ss_first (*s);

  switch (c)
    {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      return parse_date_number (s, tokens_seen, decimals);

    case '+':
    case '-':
      /* '+' or '-' at the start of a string, or following a
         space, could be the sign that optionally introduces a
         time, e.g. "-1:00" in TIME format, "-1 1:00" in DTIME
         format, or "1/1/1978 +1:00" in DATETIME format. */
      if ((!tokens_seen || s->string[-1] == ' ') && c_isdigit (ss_at (*s, 1)))
        {
          ss_advance (s, 1);
          ss_ltrim (s, ss_cstr (CC_DIGITS));
          return DT_DAY_COUNT | DT_HOUR;
        }
      else if (c == '+')
        return 0;
      /* Fall through. */
    case '/': case '.': case ',':
      ss_advance (s, 1);
      return DT_DELIM;

    case ':':
      ss_advance (s, 1);
      return DT_COLON;

    case ' ': case '\t': case '\v': case '\r': case '\n':
      {
        enum date_token token;
        ss_advance (s, 1);
        token = recognize_identifier_token (s);
        if (token)
          ss_match_byte_in (s, ss_cstr (CC_SPACES));
        else
          token = DT_DELIM | DT_SPACE;
        return token;
      }

    default:
      return recognize_identifier_token (s);

    case EOF:
      NOT_REACHED ();
    }
}

/* Parses a digit sequence found in a date token.  Advances *S
   past the end of the token.  Returns the parsed token, or 0 if
   no valid token was found.

   TOKENS_SEEN should be a bitmap representing all the tokens
   already seen in this input; this is used to resolve some
   otherwise ambiguous parsing situation.  If a count of seconds
   is parsed, *DECIMALS is set to the number of digits after the
   decimal point.*/
static enum date_token
parse_date_number (struct substring *s, enum date_token tokens_seen,
                   int *decimals)
{
  long int value;
  size_t digit_cnt = ss_get_long (s, &value);
  enum date_token token = 0;

  if (ss_match_byte (s, settings_get_decimal_char (FMT_F))
      && tokens_seen & DT_COLON
      && value <= 59)
    {
      /* Parse digits after the decimal point. */
      token = DT_SECOND;
      *decimals = ss_ltrim (s, ss_cstr (CC_DIGITS));
    }
  else
    {
      if (value <= 4)
        token = (DT_QUARTER | DT_MONTH | DT_HOUR | DT_DAY | DT_WEEK
                 | DT_DAY_COUNT);
      else if (value <= 12)
        token = DT_MONTH | DT_HOUR | DT_DAY | DT_WEEK | DT_DAY_COUNT;
      else if (value <= 23)
        token = DT_HOUR | DT_DAY | DT_WEEK | DT_DAY_COUNT;
      else if (value <= 31)
        token = DT_DAY | DT_WEEK | DT_DAY_COUNT;
      else if (value <= 52)
        token = DT_WEEK | DT_DAY_COUNT;
      else
        token = DT_DAY_COUNT;

      if (digit_cnt == 2)
        {
          token |= DT_YEAR;
          if (value <= 59)
            token |= DT_MINUTE | DT_SECOND;
        }
      else if (digit_cnt == 4)
        token |= DT_YEAR;
    }

  return token;
}

/* Attempts to parse an identifier found in a date at the
   beginning of S.  Advances *S past the end of the token.
   Returns the parsed token, or 0 if no valid token was
   found.  */
static enum date_token
recognize_identifier_token (struct substring *s)
{
  size_t length = ss_span (*s, ss_cstr (CC_LETTERS));
  enum date_token token = 0;
  switch (length)
    {
    case 0:
      break;

    case 1:
      switch (c_tolower (s->string[0]))
        {
        case 'i':
        case 'v':
        case 'x':
          token = DT_MONTH;
          break;

        case 'q':
          token = DT_Q;
          break;
        }
      break;

    case 2:
      {
        int s0 = c_tolower ((unsigned char) s->string[0]);
        int s1 = c_tolower ((unsigned char) s->string[1]);
        token = recognize_id2 (s0, s1, false);
        if (!token && s0 == 'w' && s1 == 'k')
          token = DT_WK;
      }
      break;

    default:
      {
        int s0 = c_tolower ((unsigned char) s->string[0]);
        int s1 = c_tolower ((unsigned char) s->string[1]);
        int s2 = c_tolower ((unsigned char) s->string[2]);
        token = recognize_id2 (s0, s1, true);
        if (!token)
          token = recognize_id3 (s0, s1, s2, length > 3);
        if (!token && length == 4)
          {
            int s3 = c_tolower ((unsigned char) s->string[3]);
            if (s0 == 'v' && s1 == 'i' && s2 == 'i' && s3 == 'i')
              token = DT_MONTH;
          }
      }
      break;
    }
  if (token)
    ss_advance (s, length);
  return token;
}

static enum date_token
recognize_id2 (int s0, int s1, bool more)
{
  bool weekday;
  switch (s0)
    {
    case 's': weekday = s1 == 'a' || s1 == 'u'; break;
    case 'm': weekday = s1 == 'o'; break;
    case 't': weekday = s1 == 'u' || s1 == 'h'; break;
    case 'w': weekday = s1 == 'e'; break;
    case 'f': weekday = s1 == 'r'; break;
    default: weekday = false; break;
    }
  if (weekday)
    return DT_WEEKDAY;

  if (!more)
    {
      bool month;
      switch (s0)
        {
        case 'i': month = s1 == 'i' || s1 == 'v' || s1 == 'x'; break;
        case 'v': month = s1 == 'i'; break;
        case 'x': month = s1 == 'i'; break;
        default: month = false; break;
        }
      if (month)
        return DT_MONTH;
    }

  return 0;
}

static enum date_token
recognize_id3 (int s0, int s1, int s2, bool more)
{
  bool month;
  switch (s0)
    {
    case 'j':
      month = ((s1 == 'a' && s2 == 'n')
               || (s1 == 'u' && (s2 == 'n' || s2 == 'l')));
      break;
    case 'f':
      month = s1 == 'e' && s2 == 'b';
      break;
    case 'm':
      month = (s1 == 'a' && (s2 == 'r' || s2 == 'y'));
      break;
    case 'a':
      month = (s1 == 'p' && s2 == 'r') || (s1 == 'u' && s2 == 'g');
      break;
    case 's':
      month = s1 == 'e' && s2 == 'p';
      break;
    case 'o':
      month = s1 == 'c' && s2 == 't';
      break;
    case 'n':
      month = s1 == 'o' && s2 == 'v';
      break;
    case 'd':
      month = s1 == 'e' && s2 == 'c';
      break;
    default:
      month = false;
    }
  if (month)
    return DT_MONTH | DT_ENGLISH_MONTH;

  if (!more)
    {
      bool roman_month = false;
      switch (s0)
        {
        case 'i':
        case 'x':
          roman_month = s1 == 'i' && s2 == 'i';
          break;
        case 'v':
          roman_month = s1 == 'i' && s2 == 'i';
          break;
        }
      if (roman_month)
        return DT_MONTH;
    }

  return 0;
}




