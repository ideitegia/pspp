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

#include <string.h>

#include "libpspp/cmac-aes256.h"
#include "libpspp/cast.h"

#include "gl/rijndael-alg-fst.h"

static void
gen_subkey (const uint8_t in[16], uint8_t out[16])
{
  size_t i;

  for (i = 0; i < 15; i++)
    out[i] = (in[i] << 1) | (in[i + 1] >> 7);
  out[15] = in[15] << 1;

  if (in[0] & 0x80)
    out[15] ^= 0x87;
}

/* Computes CMAC-AES-256 of the SIZE bytes in DATA, using the 256-bit AES key
   KEY.  Stores the result in the 128-bit CMAC. */
void
cmac_aes256(const uint8_t key[32],
            const void *data_, size_t size,
            uint8_t cmac[16])
{
  const char zeros[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  uint32_t rk[4 * (RIJNDAEL_MAXNR + 1)];
  uint8_t k1[16], k2[16], L[16];
  const uint8_t *data = data_;
  uint8_t c[16], tmp[16];
  int Nr;
  int i;

  Nr = rijndaelKeySetupEnc (rk, CHAR_CAST (const char *, key), 256);

  rijndaelEncrypt (rk, Nr, zeros, CHAR_CAST (char *, L));
  gen_subkey (L, k1);
  gen_subkey (k1, k2);

  memset (c, 0, 16);
  while (size > 16)
    {
      for (i = 0; i < 16; i++)
        tmp[i] = c[i] ^ data[i];
      rijndaelEncrypt (rk, Nr, CHAR_CAST (const char *, tmp),
                       CHAR_CAST (char *, c));

      size -= 16;
      data += 16;
    }

  if (size == 16)
    {
      for (i = 0; i < 16; i++)
        tmp[i] = c[i] ^ data[i] ^ k1[i];
    }
  else
    {
      for (i = 0; i < 16; i++)
        tmp[i] = c[i] ^ k2[i];
      for (i = 0; i < size; i++)
        tmp[i] ^= data[i];
      tmp[size] ^= 0x80;
    }
  rijndaelEncrypt (rk, Nr, CHAR_CAST (const char *, tmp),
                   CHAR_CAST (char *, cmac));
}
