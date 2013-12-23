/* PSPP - a program for statistical analysis.
   Copyright (C) 2013 Free Software Foundation, Inc.

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

#include "data/sys-file-encryption.h"

#include <errno.h>
#include <stdlib.h>

#include "data/file-name.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/cmac-aes256.h"
#include "libpspp/message.h"

#include "gl/minmax.h"
#include "gl/rijndael-alg-fst.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct encrypted_sys_file
  {
    FILE *file;
    int error;

    uint8_t ciphertext[16];
    uint8_t plaintext[16];
    unsigned int n;

    uint32_t rk[4 * (RIJNDAEL_MAXNR + 1)];
    int Nr;
  };

static bool try_password(struct encrypted_sys_file *, const char *password);
static bool decode_password (const char *input, char output[11]);
static bool fill_buffer (struct encrypted_sys_file *);

/* If FILENAME names an encrypted system file, returns 1 and initializes *FP
   for further use by the caller.

   If FILENAME can be opened and read, but is not an encrypted system file,
   returns 0.

   If FILENAME cannot be open or read, returns a negative errno value. */
int
encrypted_sys_file_open (struct encrypted_sys_file **fp, const char *filename)
{
  struct encrypted_sys_file *f;
  char header[36 + 16];
  int retval;
  int n;

  f = xmalloc (sizeof *f);
  f->error = 0;
  f->file = fn_open (filename, "rb");
  if (f->file == NULL)
    {
      msg (ME, _("An error occurred while opening `%s': %s."),
           filename, strerror (errno));
      retval = -errno;
      goto error;
    }

  n = fread (header, 1, sizeof header, f->file);
  if (n != sizeof header)
    {
      int error = feof (f->file) ? 0 : errno;
      if (error)
        msg (ME, _("An error occurred while reading `%s': %s."),
             filename, strerror (error));
      retval = -error;
      goto error;
    }

  if (memcmp (header + 8, "ENCRYPTEDSAV", 12))
    {
      retval = 0;
      goto error;
    }

  memcpy (f->ciphertext, header + 36, 16);
  f->n = 16;
  *fp = f;
  return 1;

error:
  if (f->file)
    fn_close (filename, f->file);
  free (f);
  *fp = NULL;

  return retval;
}

/* Attempts to use PASSWORD, which may be a plaintext or "encrypted" password,
   to unlock F.  Returns true if successful, otherwise false. */
bool
encrypted_sys_file_unlock (struct encrypted_sys_file *f, const char *password)
{
  char decoded_password[11];

  return (try_password (f, password)
          || (decode_password (password, decoded_password)
              && try_password (f, decoded_password)));
}

/* Attempts to read N bytes of plaintext from F into BUF.  Returns the number
   of bytes successfully read.  A return value less than N may indicate end of
   file or an error; use encrypted_sys_file_close() to distinguish.

   This function can only be used after encrypted_sys_file_unlock() returns
   true. */
size_t
encrypted_sys_file_read (struct encrypted_sys_file *f, void *buf_, size_t n)
{
  uint8_t *buf = buf_;
  size_t ofs = 0;

  if (f->error)
    return 0;

  while (ofs < n)
    {
      unsigned int chunk = MIN (n - ofs, f->n);
      if (chunk > 0)
        {
          memcpy (buf + ofs, &f->plaintext[16 - f->n], chunk);
          ofs += chunk;
          f->n -= chunk;
        }
      else
        {
          if (!fill_buffer (f))
            return ofs;
        }
    }

  return ofs;
}

/* Closes F.  Returns 0 if no read errors occurred, otherwise a positive errno
   value. */
int
encrypted_sys_file_close (struct encrypted_sys_file *f)
{
  int error = f->error;
  if (fclose (f->file) == EOF && !error)
    error = errno;
  free (f);

  return error;
}

#define b(x) (1 << (x))

static const uint16_t m0[4][2] = {
  { b(2),                         b(2) | b(3) | b(6) | b(7) },
  { b(3),                         b(0) | b(1) | b(4) | b(5) },
  { b(4) | b(7),                  b(8) | b(9) | b(12) | b(14) },
  { b(5) | b(6),                  b(10) | b(11) | b(14) | b(15) },
};

static const uint16_t m1[4][2] = {
  { b(0) | b(3) | b(12) | b(15),  b(0) | b(1) | b(4) | b(5) },
  { b(1) | b(2) | b(13) | b(14),  b(2) | b(3) | b(6) | b(7) },
  { b(4) | b(7) | b(8) | b(11),   b(8) | b(9) | b(12) | b(13) },
  { b(5) | b(6) | b(9) | b(10),   b(10) | b(11) | b(14) | b(15) },
};

static const uint16_t m2[4][2] = {
  { b(2),                         b(1) | b(3) | b(9) | b(11) },
  { b(3),                         b(0) | b(2) | b(8) | b(10) },
  { b(4) | b(7),                  b(4) | b(6) | b(12) | b(14) },
  { b(5) | b(6),                  b(5) | b(7) | b(13) | b(15) },
};

static const uint16_t m3[4][2] = {
  { b(0) | b(3) | b(12) | b(15),  b(0) | b(2) | b(8) | b(10) },
  { b(1) | b(2) | b(13) | b(14),  b(1) | b(3) | b(9) | b(11) },
  { b(4) | b(7) | b(8) | b(11),   b(4) | b(6) | b(12) | b(14) },
  { b(5) | b(6) | b(9) | b(10),   b(5) | b(7) | b(13) | b(15) },
};

static int
decode_nibble (const uint16_t table[4][2], int nibble)
{
  int i;

  for (i = 0; i < 4; i++)
    if (table[i][0] & (1 << nibble))
      return table[i][1];

  return 0;
}

/* Returns true if X has exactly one 1-bit, false otherwise. */
static bool
is_pow2 (int x)
{
  return x && (x & (x - 1)) == 0;
}

/* If X has exactly one 1-bit, returns its index, where bit 0 is the LSB.
   Otherwise, returns 0. */
static int
find_1bit (uint16_t x)
{
  int i;

  if (!is_pow2 (x))
    return -1;

  for (i = 0; i < 16; i++)
    if (x & (1u << i))
      return i;

  abort ();
}

/* Attempts to decode a pair of encoded password characters A and B into a
   single byte of the plaintext password.  Returns 0 if A and B are not a valid
   encoded password pair, otherwise a byte of the plaintext password. */
static int
decode_password_2bytes (uint8_t a, uint8_t b)
{
  int x = find_1bit (decode_nibble (m0, a >> 4) & decode_nibble (m2, b >> 4));
  int y = find_1bit (decode_nibble (m1, a & 15) & decode_nibble (m3, b & 15));
  return x < 0 || y < 0 ? 0 : (x << 4) | y;
}

/* Decodes an SPSS so-called "encrypted" password INPUT into OUTPUT.

   An encoded password is always an even number of bytes long and no longer
   than 20 bytes.  A decoded password is never longer than 10 bytes plus a null
   terminator.

   Returns true if successful, otherwise false. */
static bool
decode_password (const char *input, char output[11])
{
  size_t len;

  len = strlen (input);
  if (len > 20 || len % 2)
    return false;

  for (; *input; input += 2)
    {
      int c = decode_password_2bytes (input[0], input[1]);
      if (!c)
        return false;
      *output++ = c;
    }
  *output = '\0';

  return true;
}

/* If CIPHERTEXT is the first ciphertext block in an encrypted .sav file for
   PASSWORD, initializes rk[] and returns an nonzero Nr value.

   Otherwise, returns zero. */
static bool
try_password(struct encrypted_sys_file *f, const char *password)
{
  /* NIST SP 800-108 fixed data. */
  static const uint8_t fixed[] = {
    /* i */
    0x00, 0x00, 0x00, 0x01,

    /* label */
    0x35, 0x27, 0x13, 0xcc, 0x53, 0xa7, 0x78, 0x89,
    0x87, 0x53, 0x22, 0x11, 0xd6, 0x5b, 0x31, 0x58,
    0xdc, 0xfe, 0x2e, 0x7e, 0x94, 0xda, 0x2f, 0x00,
    0xcc, 0x15, 0x71, 0x80, 0x0a, 0x6c, 0x63, 0x53,

    /* delimiter */
    0x00,

    /* context */
    0x38, 0xc3, 0x38, 0xac, 0x22, 0xf3, 0x63, 0x62,
    0x0e, 0xce, 0x85, 0x3f, 0xb8, 0x07, 0x4c, 0x4e,
    0x2b, 0x77, 0xc7, 0x21, 0xf5, 0x1a, 0x80, 0x1d,
    0x67, 0xfb, 0xe1, 0xe1, 0x83, 0x07, 0xd8, 0x0d,

    /* L */
    0x00, 0x00, 0x01, 0x00,
  };

  char padded_password[32];
  size_t password_len;
  uint8_t cmac[16];
  uint8_t key[32];

  /* Truncate password to at most 10 bytes. */
  password_len = strlen (password);
  if (password_len > 10)
    password_len = 10;

  /* padded_password = password padded with zeros to 32 bytes. */
  memset (padded_password, 0, sizeof padded_password);
  memcpy (padded_password, password, password_len);

  /* cmac = CMAC(padded_password, fixed). */
  cmac_aes256 (CHAR_CAST (const uint8_t *, padded_password),
               fixed, sizeof fixed, cmac);

  /* The key is the cmac repeated twice. */
  memcpy(key, cmac, 16);
  memcpy(key + 16, cmac, 16);

  /* Use key to initialize AES. */
  assert (sizeof key == 32);
  f->Nr = rijndaelKeySetupDec (f->rk, CHAR_CAST (const char *, key), 256);

  /* Check for magic number "$FL" always present in SPSS .sav file. */
  rijndaelDecrypt (f->rk, f->Nr,
                   CHAR_CAST (const char *, f->ciphertext),
                   CHAR_CAST (char *, f->plaintext));
  return !memcmp (f->plaintext, "$FL", 3);
}

static bool
fill_buffer (struct encrypted_sys_file *f)
{
  f->n = fread (f->ciphertext, 1, sizeof f->ciphertext, f->file);
  if (f->n == sizeof f->ciphertext)
    {
      rijndaelDecrypt (f->rk, f->Nr,
                       CHAR_CAST (const char *, f->ciphertext),
                       CHAR_CAST (char *, f->plaintext));
      return true;
    }
  else
    {
      if (ferror (f->file))
        f->error = errno;
      return false;
    }
}
