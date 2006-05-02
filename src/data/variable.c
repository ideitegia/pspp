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
#include "variable.h"
#include <libpspp/message.h>
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include "dictionary.h"
#include <libpspp/hash.h>
#include "identifier.h"
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include "value-labels.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Returns an adjective describing the given variable TYPE,
   suitable for use in phrases like "numeric variable". */
const char *
var_type_adj (enum var_type type) 
{
  return type == NUMERIC ? _("numeric") : _("string");
}

/* Returns a noun describing a value of the given variable TYPE,
   suitable for use in phrases like "a number". */
const char *
var_type_noun (enum var_type type) 
{
  return type == NUMERIC ? _("number") : _("string");
}

/* Assign auxiliary data AUX to variable V, which must not
   already have auxiliary data.  Before V's auxiliary data is
   cleared, AUX_DTOR(V) will be called. */
void *
var_attach_aux (struct variable *v,
                void *aux, void (*aux_dtor) (struct variable *)) 
{
  assert (v->aux == NULL);
  assert (aux != NULL);
  v->aux = aux;
  v->aux_dtor = aux_dtor;
  return aux;
}

/* Remove auxiliary data, if any, from V, and returns it, without
   calling any associated destructor. */
void *
var_detach_aux (struct variable *v) 
{
  void *aux = v->aux;
  assert (aux != NULL);
  v->aux = NULL;
  return aux;
}

/* Clears auxiliary data, if any, from V, and calls any
   associated destructor. */
void
var_clear_aux (struct variable *v) 
{
  assert (v != NULL);
  if (v->aux != NULL) 
    {
      if (v->aux_dtor != NULL)
        v->aux_dtor (v);
      v->aux = NULL;
    }
}

/* This function is appropriate for use an auxiliary data
   destructor (passed as AUX_DTOR to var_attach_aux()) for the
   case where the auxiliary data should be passed to free(). */
void
var_dtor_free (struct variable *v) 
{
  free (v->aux);
}

/* Compares A and B, which both have the given WIDTH, and returns
   a strcmp()-type result. */
int
compare_values (const union value *a, const union value *b, int width) 
{
  if (width == 0) 
    return a->f < b->f ? -1 : a->f > b->f;
  else
    return memcmp (a->s, b->s, min(MAX_SHORT_STRING, width));
}

/* Create a hash of v */
unsigned 
hash_value(const union value  *v, int width)
{
  unsigned id_hash;

  if ( 0 == width ) 
    id_hash = hsh_hash_double (v->f);
  else
    id_hash = hsh_hash_bytes (v->s, min(MAX_SHORT_STRING, width));

  return id_hash;
}




/* Returns true if NAME is an acceptable name for a variable,
   false otherwise.  If ISSUE_ERROR is true, issues an
   explanatory error message on failure. */
bool
var_is_valid_name (const char *name, bool issue_error) 
{
  bool plausible;
  size_t length, i;
  
  assert (name != NULL);

  /* Note that strlen returns number of BYTES, not the number of 
     CHARACTERS */
  length = strlen (name);

  plausible = var_is_plausible_name(name, issue_error);

  if ( ! plausible ) 
    return false;


  if (!lex_is_id1 (name[0]))
    {
      if (issue_error)
        msg (SE, _("Character `%c' (in %s), may not appear "
                   "as the first character in a variable name."),
             name[0], name);
      return false;
    }


  for (i = 0; i < length; i++)
    {
    if (!lex_is_idn (name[i])) 
      {
        if (issue_error)
          msg (SE, _("Character `%c' (in %s) may not appear in "
                     "a variable name."),
               name[i], name);
        return false;
      }
    }

  return true;
}

/* 
   Returns true if NAME is an plausible name for a variable,
   false otherwise.  If ISSUE_ERROR is true, issues an
   explanatory error message on failure. 
   This function makes no use of LC_CTYPE.
*/
bool
var_is_plausible_name (const char *name, bool issue_error) 
{
  size_t length;
  
  assert (name != NULL);

  /* Note that strlen returns number of BYTES, not the number of 
     CHARACTERS */
  length = strlen (name);
  if (length < 1) 
    {
      if (issue_error)
        msg (SE, _("Variable name cannot be empty string."));
      return false;
    }
  else if (length > LONG_NAME_LEN) 
    {
      if (issue_error)
        msg (SE, _("Variable name %s exceeds %d-character limit."),
             name, (int) LONG_NAME_LEN);
      return false;
    }

  if (lex_id_to_token (name, strlen (name)) != T_ID) 
    {
      if (issue_error)
        msg (SE, _("`%s' may not be used as a variable name because it "
                   "is a reserved word."), name);
      return false;
    }

  return true;
}

/* A hsh_compare_func that orders variables A and B by their
   names. */
int
compare_var_names (const void *a_, const void *b_, void *foo UNUSED) 
{
  const struct variable *a = a_;
  const struct variable *b = b_;

  return strcasecmp (a->name, b->name);
}

/* A hsh_hash_func that hashes variable V based on its name. */
unsigned
hash_var_name (const void *v_, void *foo UNUSED) 
{
  const struct variable *v = v_;

  return hsh_hash_case_string (v->name);
}

/* A hsh_compare_func that orders pointers to variables A and B
   by their names. */
int
compare_var_ptr_names (const void *a_, const void *b_, void *foo UNUSED) 
{
  struct variable *const *a = a_;
  struct variable *const *b = b_;

  return strcasecmp ((*a)->name, (*b)->name);
}

/* A hsh_hash_func that hashes pointer to variable V based on its
   name. */
unsigned
hash_var_ptr_name (const void *v_, void *foo UNUSED) 
{
  struct variable *const *v = v_;

  return hsh_hash_case_string ((*v)->name);
}

/* Sets V's short_name to SHORT_NAME, truncating it to
   SHORT_NAME_LEN characters and converting it to uppercase in
   the process. */
void
var_set_short_name (struct variable *v, const char *short_name) 
{
  assert (v != NULL);
  assert (short_name[0] == '\0' || var_is_plausible_name (short_name, false));
  
  str_copy_trunc (v->short_name, sizeof v->short_name, short_name);
  str_uppercase (v->short_name);
}

/* Clears V's short name. */
void
var_clear_short_name (struct variable *v) 
{
  assert (v != NULL);

  v->short_name[0] = '\0';
}

/* Sets V's short name to BASE, followed by a suffix of the form
   _A, _B, _C, ..., _AA, _AB, etc. according to the value of
   SUFFIX.  Truncates BASE as necessary to fit. */
void
var_set_short_name_suffix (struct variable *v, const char *base, int suffix)
{
  char string[SHORT_NAME_LEN + 1];
  char *start, *end;
  int len, ofs;

  assert (v != NULL);
  assert (suffix >= 0);
  assert (strlen (v->short_name) > 0);

  /* Set base name. */
  var_set_short_name (v, base);

  /* Compose suffix_string. */
  start = end = string + sizeof string - 1;
  *end = '\0';
  do 
    {
      *--start = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[suffix % 26];
      if (start <= string + 1)
        msg (SE, _("Variable suffix too large."));
      suffix /= 26;
    }
  while (suffix > 0);
  *--start = '_';

  /* Append suffix_string to V's short name. */
  len = end - start;
  if (len + strlen (v->short_name) > SHORT_NAME_LEN)
    ofs = SHORT_NAME_LEN - len;
  else
    ofs = strlen (v->short_name);
  strcpy (v->short_name + ofs, start);
}


/* Returns the dictionary class corresponding to a variable named
   NAME. */
enum dict_class
dict_class_from_id (const char *name) 
{
  assert (name != NULL);

  switch (name[0]) 
    {
    default:
      return DC_ORDINARY;
    case '$':
      return DC_SYSTEM;
    case '#':
      return DC_SCRATCH;
    }
}

/* Returns the name of dictionary class DICT_CLASS. */
const char *
dict_class_to_name (enum dict_class dict_class) 
{
  switch (dict_class) 
    {
    case DC_ORDINARY:
      return _("ordinary");
    case DC_SYSTEM:
      return _("system");
    case DC_SCRATCH:
      return _("scratch");
    default:
      assert (0);
      abort ();
    }
}


/* Copies buffer SRC, of SRC_SIZE bytes, to DST, of DST_SIZE bytes.
   Each 256th byte, which is expected to be a ' ', is deleted.
   DST is then truncated to DST_SIZE bytes or padded on the right with
   spaces as needed. */
void
copy_demangle (char *dst, size_t dst_size,
	    const char *src, size_t src_size)
{
  int src_bytes_left = src_size;
  int dst_bytes_left = dst_size;
  const char *s = src;
  char *d = dst;


  while( src_bytes_left > 0 ) 
    {
      const size_t s_chunk = min(MAX_LONG_STRING, src_bytes_left);
      const size_t d_chunk = min(MAX_LONG_STRING, dst_bytes_left);

      assert ( d < dst + dst_size);

      buf_copy_rpad (d, d_chunk,
		     s, s_chunk);

      d += d_chunk;
      s += s_chunk;
      src_bytes_left -= s_chunk;
      dst_bytes_left -= d_chunk;

      if ( src_bytes_left > 0 && ! (++s - src) % (MAX_LONG_STRING+1) )
	{
	  if ( *s != ' ') 
	    msg(MW, _("Expected a space in very long string"));
	  src_bytes_left--;
	}
    }
}

/* Copies buffer SRC, of SRC_SIZE bytes, to DST, of DST_SIZE bytes.
   DST is rounded up to the nearest 8 byte boundary.
   A space is inserted at each 256th byte.
   DST is then truncated to DST_SIZE bytes or padded on the right with
   spaces as needed. */
void
copy_mangle (char *dst, size_t dst_size,
	    const char *src, size_t src_size)
{
  int src_bytes_left = src_size;
  int dst_bytes_left = dst_size;
  const char *s = src;
  char *d = dst;

  memset(dst, ' ', dst_size);

  while( src_bytes_left > 0 ) 
    {
      const size_t s_chunk = min(MAX_LONG_STRING, src_bytes_left);
      const size_t d_chunk = min(MAX_LONG_STRING, dst_bytes_left);

      buf_copy_rpad (d, d_chunk, s, s_chunk);

      d += d_chunk;
      s += s_chunk;
      src_bytes_left -= s_chunk;
      dst_bytes_left -= d_chunk;

      if ( dst_bytes_left > 0 && 0 == ( d + 1 - dst ) % (MAX_LONG_STRING + 1) )
	{
	  memset(d, ' ', 1);
	  d++;
	  dst_bytes_left--;
	}
    }
}

/* Return the number of bytes used when writing case_data for a variable 
   of WIDTH */
int
width_to_bytes(int width)
{
  int bytes, mod8;

  assert (width >= 0);

  if ( width == 0 ) 
    return MAX_SHORT_STRING ;

  if ( width <= MAX_LONG_STRING) 
    return MAX_SHORT_STRING * DIV_RND_UP(width, MAX_SHORT_STRING);

  const int chunks = width / EFFECTIVE_LONG_STRING_LENGTH ;

  const int remainder = width - (chunks * EFFECTIVE_LONG_STRING_LENGTH) ;

  bytes =  remainder + (chunks * (MAX_LONG_STRING + 1) );

  /* Round up to the nearest 8 */
  mod8 = bytes % MAX_SHORT_STRING;

  if ( mod8 ) 
    bytes += MAX_SHORT_STRING - mod8;

  assert( bytes % MAX_SHORT_STRING == 0 );

  return bytes;
}

