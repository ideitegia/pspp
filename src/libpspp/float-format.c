/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2011 Free Software Foundation, Inc.

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

#include "libpspp/float-format.h"

#include <byteswap.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/assertion.h"
#include "libpspp/integer-format.h"


/* Neutral intermediate representation for binary floating-point numbers. */
struct fp
  {
    enum
      {
        FINITE,         /* Finite number (normalized or denormalized). */
        INFINITE,       /* Positive or negative infinity. */
        NAN,            /* Not a number. */

        ZERO,           /* Positive or negative zero. */
        MISSING,        /* System missing. */
        LOWEST,         /* LOWEST on e.g. missing values. */
        HIGHEST,        /* HIGHEST on e.g. missing values. */
        RESERVED        /* Special Vax representation. */
      }
    class;

    enum
      {
        POSITIVE,
        NEGATIVE
      }
    sign;

    /* class == FINITE: The number has value "fraction *
       2**exponent", considering bit 63 in fraction to be just
       right of the decimal point.

       class == NAN: The fraction holds the significand, with its
       leftmost bit in bit 63, so that signaling and quiet NaN
       values can be preserved.

       Unused for other classes. */
    uint64_t fraction;
    int exponent;
  };

static void extract_number (enum float_format, const void *, struct fp *);
static void assemble_number (enum float_format, struct fp *, void *);

static inline uint16_t get_uint16 (const void *);
static inline uint32_t get_uint32 (const void *);
static inline uint64_t get_uint64 (const void *);

static inline void put_uint16 (uint16_t, void *);
static inline void put_uint32 (uint32_t, void *);
static inline void put_uint64 (uint64_t, void *);

/* Converts SRC from format FROM to format TO, storing the
   converted value into DST.
   SRC and DST are permitted to arbitrarily overlap. */
void
float_convert (enum float_format from, const void *src,
               enum float_format to, void *dst)
{
  if (from != to)
    {
      if ((from == FLOAT_IEEE_SINGLE_LE || from == FLOAT_IEEE_SINGLE_BE)
          && (to == FLOAT_IEEE_SINGLE_LE || to == FLOAT_IEEE_SINGLE_BE))
        put_uint32 (bswap_32 (get_uint32 (src)), dst);
      else if ((from == FLOAT_IEEE_DOUBLE_LE || from == FLOAT_IEEE_DOUBLE_BE)
               && (to == FLOAT_IEEE_DOUBLE_LE || to == FLOAT_IEEE_DOUBLE_BE))
        put_uint64 (bswap_64 (get_uint64 (src)), dst);
      else
        {
          struct fp fp;
          extract_number (from, src, &fp);
          assemble_number (to, &fp, dst);
        }
    }
  else
    {
      if (src != dst)
        memmove (dst, src, float_get_size (from));
    }
}

/* Converts SRC from format FROM to a native double and returns
   the double. */
double
float_get_double (enum float_format from, const void *src)
{
  double dst;
  float_convert (from, src, FLOAT_NATIVE_DOUBLE, &dst);
  return dst;
}

/* Returns the number of bytes in a number in the given
   FORMAT. */
size_t
float_get_size (enum float_format format)
{
  switch (format)
    {
    case FLOAT_IEEE_SINGLE_LE:
    case FLOAT_IEEE_SINGLE_BE:
    case FLOAT_VAX_F:
    case FLOAT_Z_SHORT:
      return 4;

    case FLOAT_IEEE_DOUBLE_LE:
    case FLOAT_IEEE_DOUBLE_BE:
    case FLOAT_VAX_D:
    case FLOAT_VAX_G:
    case FLOAT_Z_LONG:
      return 8;

    case FLOAT_FP:
      return sizeof (struct fp);

    case FLOAT_HEX:
      return 32;
    }

  NOT_REACHED ();
}

/* Attempts to identify the floating point format(s) in which the
   LENGTH bytes in NUMBER represent the given EXPECTED_VALUE.
   Returns the number of matches, which may be zero, one, or
   greater than one.  If a positive value is returned, then the
   most likely candidate format (based on how common the formats
   are in practice) is stored in *BEST_GUESS. */
int
float_identify (double expected_value, const void *number, size_t length,
                enum float_format *best_guess)
{
  /* Candidates for identification in order of decreasing
     preference. */
  enum float_format candidates[] =
    {
      FLOAT_IEEE_SINGLE_LE,
      FLOAT_IEEE_SINGLE_BE,
      FLOAT_IEEE_DOUBLE_LE,
      FLOAT_IEEE_DOUBLE_BE,
      FLOAT_VAX_F,
      FLOAT_VAX_D,
      FLOAT_VAX_G,
      FLOAT_Z_SHORT,
      FLOAT_Z_LONG,
    };
  const size_t candidate_cnt = sizeof candidates / sizeof *candidates;

  enum float_format *p;
  int match_cnt;

  match_cnt = 0;
  for (p = candidates; p < candidates + candidate_cnt; p++)
    if (float_get_size (*p) == length)
      {
        char tmp[8];
        assert (sizeof tmp >= float_get_size (*p));
        float_convert (FLOAT_NATIVE_DOUBLE, &expected_value, *p, tmp);
        if (!memcmp (tmp, number, length) && match_cnt++ == 0)
          *best_guess = *p;
      }
  return match_cnt;
}

/* Returns the double value that is just greater than -DBL_MAX,
   which in PSPP syntax files is called LOWEST and used as the
   low end of numeric ranges that are supposed to be unbounded on
   the low end, as in the missing value set created by,
   e.g. MISSING VALUES X(LOWEST THRU 5).  (-DBL_MAX is used for
   SYSMIS so it is not available for LOWEST.) */
double
float_get_lowest (void)
{
  struct fp fp;
  double x;

  fp.class = LOWEST;
  fp.sign = POSITIVE;
  assemble_number (FLOAT_NATIVE_DOUBLE, &fp, &x);
  return x;
}

/* Returns CNT bits in X starting from the given bit OFS. */
static inline uint64_t
get_bits (uint64_t x, int ofs, int cnt)
{
  assert (ofs >= 0 && ofs < 64);
  assert (cnt > 0 && cnt < 64);
  assert (ofs + cnt <= 64);
  return (x >> ofs) & ((UINT64_C(1) << cnt) - 1);
}

/* Returns the 16-bit unsigned integer at P,
   which need not be aligned. */
static inline uint16_t
get_uint16 (const void *p)
{
  uint16_t x;
  memcpy (&x, p, sizeof x);
  return x;
}

/* Returns the 32-bit unsigned integer at P,
   which need not be aligned. */
static inline uint32_t
get_uint32 (const void *p)
{
  uint32_t x;
  memcpy (&x, p, sizeof x);
  return x;
}

/* Returns the 64-bit unsigned integer at P,
   which need not be aligned. */
static inline uint64_t
get_uint64 (const void *p)
{
  uint64_t x;
  memcpy (&x, p, sizeof x);
  return x;
}

/* Stores 16-bit unsigned integer X at P,
   which need not be aligned. */
static inline void
put_uint16 (uint16_t x, void *p)
{
  memcpy (p, &x, sizeof x);
}

/* Stores 32-bit unsigned integer X at P,
   which need not be aligned. */
static inline void
put_uint32 (uint32_t x, void *p)
{
  memcpy (p, &x, sizeof x);
}

/* Stores 64-bit unsigned integer X at P,
   which need not be aligned. */
static inline void
put_uint64 (uint64_t x, void *p)
{
  memcpy (p, &x, sizeof x);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in little-endian byte order. */
static inline uint16_t
native_to_le16 (uint16_t native)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? native : bswap_16 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in big-endian byte order. */
static inline uint16_t
native_to_be16 (uint16_t native)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? native : bswap_16 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in VAX-endian byte order. */
static inline uint16_t
native_to_vax16 (uint16_t native)
{
  return native_to_le16 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in little-endian byte order. */
static inline uint32_t
native_to_le32 (uint32_t native)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? native : bswap_32 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in big-endian byte order. */
static inline uint32_t
native_to_be32 (uint32_t native)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? native : bswap_32 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in VAX-endian byte order. */
static inline uint32_t
native_to_vax32 (uint32_t native)
{
  return native_to_be32 (((native & 0xff00ff00) >> 8) |
                         ((native & 0x00ff00ff) << 8));
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in little-endian byte order. */
static inline uint64_t
native_to_le64 (uint64_t native)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? native : bswap_64 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in big-endian byte order. */
static inline uint64_t
native_to_be64 (uint64_t native)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? native : bswap_64 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in VAX-endian byte order. */
static inline uint64_t
native_to_vax64 (uint64_t native)
{
  return native_to_be64 (((native & UINT64_C(0xff00ff0000000000)) >> 40) |
                         ((native & UINT64_C(0x00ff00ff00000000)) >> 24) |
                         ((native & UINT64_C(0x00000000ff00ff00)) << 24) |
                         ((native & UINT64_C(0x0000000000ff00ff)) << 40));
}

/* Given LE, obtained from memory in little-endian format,
   returns its value. */
static inline uint16_t
le_to_native16 (uint16_t le)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? le : bswap_16 (le);
}

/* Given BE, obtained from memory in big-endian format, returns
   its value. */
static inline uint16_t
be_to_native16 (uint16_t be)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? be : bswap_16 (be);
}

/* Given VAX, obtained from memory in VAX-endian format, returns
   its value. */
static inline uint16_t
vax_to_native16 (uint16_t vax)
{
  return le_to_native16 (vax);
}

/* Given LE, obtained from memory in little-endian format,
   returns its value. */
static inline uint32_t
le_to_native32 (uint32_t le)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? le : bswap_32 (le);
}

/* Given BE, obtained from memory in big-endian format, returns
   its value. */
static inline uint32_t
be_to_native32 (uint32_t be)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? be : bswap_32 (be);
}

/* Given VAX, obtained from memory in VAX-endian format, returns
   its value. */
static inline uint32_t
vax_to_native32 (uint32_t vax)
{
  uint32_t be = be_to_native32 (vax);
  return ((be & 0xff00ff00) >> 8) | ((be & 0x00ff00ff) << 8);
}

/* Given LE, obtained from memory in little-endian format,
   returns its value. */
static inline uint64_t
le_to_native64 (uint64_t le)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? le : bswap_64 (le);
}

/* Given BE, obtained from memory in big-endian format, returns
   its value. */
static inline uint64_t
be_to_native64 (uint64_t be)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? be : bswap_64 (be);
}

/* Given VAX, obtained from memory in VAX-endian format, returns
   its value. */
static inline uint64_t
vax_to_native64 (uint64_t vax)
{
  uint64_t be = be_to_native64 (vax);
  return (((be & UINT64_C(0xff00ff0000000000)) >> 40) |
          ((be & UINT64_C(0x00ff00ff00000000)) >> 24) |
          ((be & UINT64_C(0x00000000ff00ff00)) << 24) |
          ((be & UINT64_C(0x0000000000ff00ff)) << 40));
}

static void extract_ieee (uint64_t, int exp_bits, int frac_bits, struct fp *);
static void extract_vax (uint64_t, int exp_bits, int frac_bits, struct fp *);
static void extract_z (uint64_t, int exp_bits, int frac_bits, struct fp *);
static void extract_hex (const char *, struct fp *);

/* Converts the number at BITS from format TYPE into neutral
   format at FP. */
static void
extract_number (enum float_format type, const void *bits, struct fp *fp)
{
  switch (type)
    {
    case FLOAT_IEEE_SINGLE_LE:
      extract_ieee (le_to_native32 (get_uint32 (bits)), 8, 23, fp);
      break;
    case FLOAT_IEEE_SINGLE_BE:
      extract_ieee (be_to_native32 (get_uint32 (bits)), 8, 23, fp);
      break;
    case FLOAT_IEEE_DOUBLE_LE:
      extract_ieee (le_to_native64 (get_uint64 (bits)), 11, 52, fp);
      break;
    case FLOAT_IEEE_DOUBLE_BE:
      extract_ieee (be_to_native64 (get_uint64 (bits)), 11, 52, fp);
      break;

    case FLOAT_VAX_F:
      extract_vax (vax_to_native32 (get_uint32 (bits)), 8, 23, fp);
      break;
    case FLOAT_VAX_D:
      extract_vax (vax_to_native64 (get_uint64 (bits)), 8, 55, fp);
      break;
    case FLOAT_VAX_G:
      extract_vax (vax_to_native64 (get_uint64 (bits)), 11, 52, fp);
      break;

    case FLOAT_Z_SHORT:
      extract_z (be_to_native32 (get_uint32 (bits)), 7, 24, fp);
      break;
    case FLOAT_Z_LONG:
      extract_z (be_to_native64 (get_uint64 (bits)), 7, 56, fp);
      break;

    case FLOAT_FP:
      memcpy (fp, bits, sizeof *fp);
      break;
    case FLOAT_HEX:
      extract_hex (bits, fp);
      break;
    }

  assert (!(fp->class == FINITE && fp->fraction == 0));
}

/* Converts the IEEE format number in BITS, which has EXP_BITS of
   exponent and FRAC_BITS of fraction, into neutral format at
   FP. */
static void
extract_ieee (uint64_t bits, int exp_bits, int frac_bits, struct fp *fp)
{
  const int bias = (1 << (exp_bits - 1)) - 1;
  const uint64_t max_raw_frac = (UINT64_C(1) << frac_bits) - 1;
  const int max_raw_exp = (1 << exp_bits) - 1;

  const uint64_t raw_frac = get_bits (bits, 0, frac_bits);
  const int raw_exp = get_bits (bits, frac_bits, exp_bits);
  const bool raw_sign = get_bits (bits, frac_bits + exp_bits, 1);

  if (raw_sign && raw_exp == max_raw_exp - 1 && raw_frac == max_raw_frac - 1)
    fp->class = LOWEST;
  else if (raw_exp == max_raw_exp - 1 && raw_frac == max_raw_frac)
    fp->class = raw_sign ? MISSING : HIGHEST;
  else if (raw_exp == max_raw_exp)
    {
      if (raw_frac == 0)
        fp->class = INFINITE;
      else
        {
          fp->class = NAN;
          fp->fraction = raw_frac << (64 - frac_bits);
        }
    }
  else if (raw_exp == 0)
    {
      if (raw_frac != 0)
        {
          fp->class = FINITE;
          fp->exponent = 1 - bias;
          fp->fraction = raw_frac << (64 - frac_bits);
        }
      else
        fp->class = ZERO;
    }
  else
    {
      fp->class = FINITE;
      fp->exponent = raw_exp - bias + 1;
      fp->fraction = (raw_frac << (64 - frac_bits - 1)) | (UINT64_C(1) << 63);
    }

  fp->sign = raw_sign ? NEGATIVE : POSITIVE;
}

/* Converts the VAX format number in BITS, which has EXP_BITS of
   exponent and FRAC_BITS of fraction, into neutral format at
   FP. */
static void
extract_vax (uint64_t bits, int exp_bits, int frac_bits, struct fp *fp)
{
  const int bias = 1 << (exp_bits - 1);
  const uint64_t max_raw_frac = (UINT64_C(1) << frac_bits) - 1;
  const int max_raw_exp = (1 << exp_bits) - 1;

  const uint64_t raw_frac = get_bits (bits, 0, frac_bits);
  const int raw_exp = get_bits (bits, frac_bits, exp_bits);
  const bool raw_sign = get_bits (bits, frac_bits + exp_bits, 1);

  if (raw_sign && raw_exp == max_raw_exp && raw_frac == max_raw_frac - 1)
    fp->class = LOWEST;
  else if (raw_exp == max_raw_exp && raw_frac == max_raw_frac)
    fp->class = raw_sign ? MISSING : HIGHEST;
  else if (raw_exp == 0)
    fp->class = raw_sign == 0 ? ZERO : RESERVED;
  else
    {
      fp->class = FINITE;
      fp->fraction = (raw_frac << (64 - frac_bits - 1)) | (UINT64_C(1) << 63);
      fp->exponent = raw_exp - bias;
    }

  fp->sign = raw_sign ? NEGATIVE : POSITIVE;
}

/* Converts the Z architecture format number in BITS, which has
   EXP_BITS of exponent and FRAC_BITS of fraction, into neutral
   format at FP. */
static void
extract_z (uint64_t bits, int exp_bits, int frac_bits, struct fp *fp)
{
  const int bias = 1 << (exp_bits - 1);
  const uint64_t max_raw_frac = (UINT64_C(1) << frac_bits) - 1;
  const int max_raw_exp = (1 << exp_bits) - 1;

  uint64_t raw_frac = get_bits (bits, 0, frac_bits);
  int raw_exp = get_bits (bits, frac_bits, exp_bits);
  int raw_sign = get_bits (bits, frac_bits + exp_bits, 1);

  fp->sign = raw_sign ? NEGATIVE : POSITIVE;
  if (raw_exp == max_raw_exp && raw_frac == max_raw_frac)
    fp->class = raw_sign ? MISSING : HIGHEST;
  else if (raw_sign && raw_exp == max_raw_exp && raw_frac == max_raw_frac - 1)
    fp->class = LOWEST;
  else if (raw_frac != 0)
    {
      fp->class = FINITE;
      fp->fraction = raw_frac << (64 - frac_bits);
      fp->exponent = (raw_exp - bias) * 4;
    }
  else
    fp->class = ZERO;
}

/* Returns the integer value of hex digit C. */
static int
hexit_value (int c)
{
  const char s[] = "0123456789abcdef";
  const char *cp = strchr (s, tolower ((unsigned char) c));

  assert (cp != NULL);
  return cp - s;
}

/* Parses a hexadecimal floating point number string at S (useful
   for testing) into neutral format at FP. */
static void
extract_hex (const char *s, struct fp *fp)
{
  if (*s == '-')
    {
      fp->sign = NEGATIVE;
      s++;
    }
  else
    fp->sign = POSITIVE;

  if (!strcmp (s, "Infinity"))
    fp->class = INFINITE;
  else if (!strcmp (s, "Missing"))
    fp->class = MISSING;
  else if (!strcmp (s, "Lowest"))
    fp->class = LOWEST;
  else if (!strcmp (s, "Highest"))
    fp->class = HIGHEST;
  else if (!strcmp (s, "Reserved"))
    fp->class = RESERVED;
  else
    {
      int offset;

      if (!memcmp (s, "NaN:", 4))
        {
          fp->class = NAN;
          s += 4;
        }
      else
        fp->class = FINITE;

      if (*s == '.')
        s++;

      fp->exponent = 0;
      fp->fraction = 0;
      offset = 60;
      for (; isxdigit ((unsigned char) *s); s++)
        if (offset >= 0)
          {
            uint64_t digit = hexit_value (*s);
            fp->fraction += digit << offset;
            offset -= 4;
          }

      if (fp->class == FINITE)
        {
          if (fp->fraction == 0)
            fp->class = ZERO;
          else if (*s == 'p')
            {
              char *tail;
              fp->exponent += strtol (s + 1, &tail, 10);
              s = tail;
            }
        }
    }
}

static uint64_t assemble_ieee (struct fp *, int exp_bits, int frac_bits);
static uint64_t assemble_vax (struct fp *, int exp_bits, int frac_bits);
static uint64_t assemble_z (struct fp *, int exp_bits, int frac_bits);
static void assemble_hex (struct fp *, void *);

/* Converts the neutral format floating point number in FP into
   format TYPE at NUMBER.  May modify FP as part of the
   process. */
static void
assemble_number (enum float_format type, struct fp *fp, void *number)
{
  switch (type)
    {
    case FLOAT_IEEE_SINGLE_LE:
      put_uint32 (native_to_le32 (assemble_ieee (fp, 8, 23)), number);
      break;
    case FLOAT_IEEE_SINGLE_BE:
      put_uint32 (native_to_be32 (assemble_ieee (fp, 8, 23)), number);
      break;
    case FLOAT_IEEE_DOUBLE_LE:
      put_uint64 (native_to_le64 (assemble_ieee (fp, 11, 52)), number);
      break;
    case FLOAT_IEEE_DOUBLE_BE:
      put_uint64 (native_to_be64 (assemble_ieee (fp, 11, 52)), number);
      break;

    case FLOAT_VAX_F:
      put_uint32 (native_to_vax32 (assemble_vax (fp, 8, 23)), number);
      break;
    case FLOAT_VAX_D:
      put_uint64 (native_to_vax64 (assemble_vax (fp, 8, 55)), number);
      break;
    case FLOAT_VAX_G:
      put_uint64 (native_to_vax64 (assemble_vax (fp, 11, 52)), number);
      break;

    case FLOAT_Z_SHORT:
      put_uint32 (native_to_be32 (assemble_z (fp, 7, 24)), number);
      break;
    case FLOAT_Z_LONG:
      put_uint64 (native_to_be64 (assemble_z (fp, 7, 56)), number);
      break;

    case FLOAT_FP:
      memcpy (number, fp, sizeof *fp);
      break;
    case FLOAT_HEX:
      assemble_hex (fp, number);
      break;
    }
}

/* Rounds off FP's fraction to FRAC_BITS bits of precision.
   Halfway values are rounded to even. */
static void
normalize_and_round_fp (struct fp *fp, int frac_bits)
{
  assert (fp->class == FINITE);
  assert (fp->fraction != 0);

  /* Make sure that the leading fraction bit is 1. */
  while (!(fp->fraction & (UINT64_C(1) << 63)))
    {
      fp->fraction <<= 1;
      fp->exponent--;
    }

  if (frac_bits < 64)
    {
      uint64_t last_frac_bit = UINT64_C(1) << (64 - frac_bits);
      uint64_t decision_bit = last_frac_bit >> 1;
      if (fp->fraction & decision_bit
          && (fp->fraction & (decision_bit - 1)
              || fp->fraction & last_frac_bit))
        {
          fp->fraction += last_frac_bit;
          if ((fp->fraction >> 63) == 0)
            {
              fp->fraction = UINT64_C(1) << 63;
              fp->exponent++;
            }
        }

      /* Mask off all but FRAC_BITS high-order bits.
         If we rounded up, then these bits no longer have
         meaningful values. */
      fp->fraction &= ~(last_frac_bit - 1);
    }
}

/* Converts the neutral format floating point number in FP into
   IEEE format with EXP_BITS exponent bits and FRAC_BITS fraction
   bits, and returns the value. */
static uint64_t
assemble_ieee (struct fp *fp, int exp_bits, int frac_bits)
{
  const uint64_t max_raw_frac = (UINT64_C(1) << frac_bits) - 1;

  const int bias = (1 << (exp_bits - 1)) - 1;
  const int max_raw_exp = (1 << exp_bits) - 1;
  const int min_norm_exp = 1 - bias;
  const int min_denorm_exp = min_norm_exp - frac_bits;
  const int max_norm_exp = max_raw_exp - 1 - bias;

  uint64_t raw_frac;
  int raw_exp;
  bool raw_sign;

  raw_sign = fp->sign != POSITIVE;

  switch (fp->class)
    {
    case FINITE:
      normalize_and_round_fp (fp, frac_bits + 1);
      if (fp->exponent - 1 > max_norm_exp)
        {
          /* Overflow to infinity. */
          raw_exp = max_raw_exp;
          raw_frac = 0;
        }
      else if (fp->exponent - 1 >= min_norm_exp)
        {
          /* Normal. */
          raw_frac = (fp->fraction << 1) >> (64 - frac_bits);
          raw_exp = (fp->exponent - 1) + bias;
        }
      else if (fp->exponent - 1 >= min_denorm_exp)
        {
          /* Denormal. */
          const int denorm_shift = min_norm_exp - fp->exponent;
          raw_frac = (fp->fraction >> (64 - frac_bits)) >> denorm_shift;
          raw_exp = 0;
        }
      else
        {
          /* Underflow to zero. */
          raw_frac = 0;
          raw_exp = 0;
        }
      break;

    case INFINITE:
      raw_frac = 0;
      raw_exp = max_raw_exp;
      break;

    case NAN:
      raw_frac = fp->fraction >> (64 - frac_bits);
      if (raw_frac == 0)
        raw_frac = 1;
      raw_exp = max_raw_exp;
      break;

    case ZERO:
      raw_frac = 0;
      raw_exp = 0;
      break;

    case MISSING:
      raw_sign = 1;
      raw_exp = max_raw_exp - 1;
      raw_frac = max_raw_frac;
      break;

    case LOWEST:
      raw_sign = 1;
      raw_exp = max_raw_exp - 1;
      raw_frac = max_raw_frac - 1;
      break;

    case HIGHEST:
      raw_sign = 0;
      raw_exp = max_raw_exp - 1;
      raw_frac = max_raw_frac;
      break;

    case RESERVED:
      /* Convert to what processors commonly treat as signaling NAN. */
      raw_frac = (UINT64_C(1) << frac_bits) - 1;
      raw_exp = max_raw_exp;
      break;

    default:
      NOT_REACHED ();
    }

  return (((uint64_t) raw_sign << (frac_bits + exp_bits))
          | ((uint64_t) raw_exp << frac_bits)
          | raw_frac);
}

/* Converts the neutral format floating point number in FP into
   VAX format with EXP_BITS exponent bits and FRAC_BITS fraction
   bits, and returns the value. */
static uint64_t
assemble_vax (struct fp *fp, int exp_bits, int frac_bits)
{
  const int max_raw_exp = (1 << exp_bits) - 1;
  const int bias = 1 << (exp_bits - 1);
  const int min_finite_exp = 1 - bias;
  const int max_finite_exp = max_raw_exp - bias;
  const uint64_t max_raw_frac = (UINT64_C(1) << frac_bits) - 1;

  uint64_t raw_frac;
  int raw_exp;
  bool raw_sign;

  raw_sign = fp->sign != POSITIVE;

  switch (fp->class)
    {
    case FINITE:
      normalize_and_round_fp (fp, frac_bits + 1);
      if (fp->exponent > max_finite_exp)
        {
          /* Overflow to reserved operand. */
          raw_sign = 1;
          raw_exp = 0;
          raw_frac = 0;
        }
      else if (fp->exponent >= min_finite_exp)
        {
          /* Finite. */
          raw_frac = (fp->fraction << 1) >> (64 - frac_bits);
          raw_exp = fp->exponent + bias;
        }
      else
        {
          /* Underflow to zero. */
          raw_sign = 0;
          raw_frac = 0;
          raw_exp = 0;
        }
      break;

    case INFINITE:
    case NAN:
    case RESERVED:
      raw_sign = 1;
      raw_exp = 0;
      raw_frac = 0;
      break;

    case ZERO:
      raw_sign = 0;
      raw_frac = 0;
      raw_exp = 0;
      break;

    case MISSING:
      raw_sign = 1;
      raw_exp = max_finite_exp + bias;
      raw_frac = max_raw_frac;
      break;

    case LOWEST:
      raw_sign = 1;
      raw_exp = max_finite_exp + bias;
      raw_frac = max_raw_frac - 1;
      break;

    case HIGHEST:
      raw_sign = 0;
      raw_exp = max_finite_exp + bias;
      raw_frac = max_raw_frac;
      break;

    default:
      NOT_REACHED ();
    }

  return (((uint64_t) raw_sign << (frac_bits + exp_bits))
          | ((uint64_t) raw_exp << frac_bits)
          | raw_frac);
}

/* Shift right until the exponent is a multiple of 4.
   Rounding is not needed, because none of the formats we support
   has more than 53 bits of precision.  That is, we never shift a
   1-bit off the right end of the fraction.  */
static void
normalize_hex_fp (struct fp *fp)
{
  while (fp->exponent % 4)
    {
      fp->fraction >>= 1;
      fp->exponent++;
    }
}

/* Converts the neutral format floating point number in FP into Z
   architecture format with EXP_BITS exponent bits and FRAC_BITS
   fraction bits, and returns the value. */
static uint64_t
assemble_z (struct fp *fp, int exp_bits, int frac_bits)
{
  const int max_raw_exp = (1 << exp_bits) - 1;
  const int bias = 1 << (exp_bits - 1);
  const int max_norm_exp = (max_raw_exp - bias) * 4;
  const int min_norm_exp = -bias * 4;
  const int min_denorm_exp = min_norm_exp - (frac_bits - 1);

  const uint64_t max_raw_frac = (UINT64_C(1) << frac_bits) - 1;

  uint64_t raw_frac;
  int raw_exp;
  int raw_sign;

  raw_sign = fp->sign != POSITIVE;

  switch (fp->class)
    {
    case FINITE:
      normalize_and_round_fp (fp, frac_bits);
      normalize_hex_fp (fp);
      if (fp->exponent > max_norm_exp)
        {
          /* Overflow to largest value. */
          raw_exp = max_raw_exp;
          raw_frac = max_raw_frac;
        }
      else if (fp->exponent >= min_norm_exp)
        {
          /* Normal. */
          raw_frac = fp->fraction >> (64 - frac_bits);
          raw_exp = (fp->exponent / 4) + bias;
        }
      else if (fp->exponent >= min_denorm_exp)
        {
          /* Denormal. */
          const int denorm_shift = min_norm_exp - fp->exponent;
          raw_frac = (fp->fraction >> (64 - frac_bits)) >> denorm_shift;
          raw_exp = 0;
        }
      else
        {
          /* Underflow to zero. */
          raw_frac = 0;
          raw_exp = 0;
        }
      break;

    case INFINITE:
      /* Overflow to largest value. */
      raw_exp = max_raw_exp;
      raw_frac = max_raw_frac;
      break;

    case NAN:
    case RESERVED:
    case ZERO:
      /* Treat all of these as zero, because Z architecture
         doesn't have any reserved values. */
      raw_exp = 0;
      raw_frac = 0;
      break;

    case MISSING:
      raw_sign = 1;
      raw_exp = max_raw_exp;
      raw_frac = max_raw_frac;
      break;

    case LOWEST:
      raw_sign = 1;
      raw_exp = max_raw_exp;
      raw_frac = max_raw_frac - 1;
      break;

    case HIGHEST:
      raw_sign = 0;
      raw_exp = max_raw_exp;
      raw_frac = max_raw_frac;
      break;

    default:
      NOT_REACHED ();
    }

  return (((uint64_t) raw_sign << (frac_bits + exp_bits))
          | ((uint64_t) raw_exp << frac_bits)
          | raw_frac);
}

/* Converts the neutral format floating point number in FP into a
   null-terminated human-readable hex string in OUTPUT. */
static void
assemble_hex (struct fp *fp, void *output)
{
  char buffer[32];
  char *s = buffer;

  if (fp->sign == NEGATIVE)
    *s++ = '-';

  switch (fp->class)
    {
    case FINITE:
      normalize_and_round_fp (fp, 64);
      normalize_hex_fp (fp);
      assert (fp->fraction != 0);

      *s++ = '.';
      while (fp->fraction != 0)
        {
          *s++ = (fp->fraction >> 60)["0123456789abcdef"];
          fp->fraction <<= 4;
        }
      if (fp->exponent != 0)
        sprintf (s, "p%d", fp->exponent);
      break;

    case INFINITE:
      strcpy (s, "Infinity");
      break;

    case NAN:
      sprintf (s, "NaN:%016"PRIx64, fp->fraction);
      break;

    case ZERO:
      strcpy (s, "0");
      break;

    case MISSING:
      strcpy (buffer, "Missing");
      break;

    case LOWEST:
      strcpy (buffer, "Lowest");
      break;

    case HIGHEST:
      strcpy (buffer, "Highest");
      break;

    case RESERVED:
      strcpy (s, "Reserved");
      break;
    }

  strncpy (output, buffer, float_get_size (FLOAT_HEX));
}
