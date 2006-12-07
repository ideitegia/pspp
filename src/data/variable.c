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
#include <libpspp/assertion.h>
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

#include "minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Returns true if VAR_TYPE is a valid variable type. */
bool
var_type_is_valid (enum var_type var_type) 
{
  return var_type == NUMERIC || var_type == ALPHA;
}

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

/* Returns true if M is a valid variable measurement level,
   false otherwise. */
bool
measure_is_valid (enum measure m)
{
  return m == MEASURE_NOMINAL || m == MEASURE_ORDINAL || m == MEASURE_SCALE;
}

/* Returns true if A is a valid alignment,
   false otherwise. */
bool
alignment_is_valid (enum alignment a)
{
  return a == ALIGN_LEFT || a == ALIGN_RIGHT || a == ALIGN_CENTRE;
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

/* Duplicate a value.
   The caller is responsible for freeing the returned value
*/
union value *
value_dup (const union value *val, int width)
{
  size_t bytes = MAX(width, sizeof *val);

  union value *v = xmalloc (bytes);
  memcpy (v, val, bytes);
  return v;
}



/* Compares A and B, which both have the given WIDTH, and returns
   a strcmp()-type result. */
int
compare_values (const union value *a, const union value *b, int width) 
{
  if (width == 0) 
    return a->f < b->f ? -1 : a->f > b->f;
  else
    return memcmp (a->s, b->s, MIN(MAX_SHORT_STRING, width));
}

/* Create a hash of v */
unsigned 
hash_value(const union value  *v, int width)
{
  unsigned id_hash;

  if ( 0 == width ) 
    id_hash = hsh_hash_double (v->f);
  else
    id_hash = hsh_hash_bytes (v->s, MIN(MAX_SHORT_STRING, width));

  return id_hash;
}

/* Return variable V's name. */
const char *
var_get_name (const struct variable *v) 
{
  return v->name;
}

/* Sets V's name to NAME. */
void
var_set_name (struct variable *v, const char *name) 
{
  assert (name[0] != '\0');
  assert (lex_id_to_token (ss_cstr (name)) == T_ID);

  str_copy_trunc (v->name, sizeof v->name, name);
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

  if (lex_id_to_token (ss_cstr (name)) != T_ID) 
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
compare_var_names (const void *a_, const void *b_, const void *aux UNUSED) 
{
  const struct variable *a = a_;
  const struct variable *b = b_;

  return strcasecmp (var_get_name (a), var_get_name (b));
}

/* A hsh_hash_func that hashes variable V based on its name. */
unsigned
hash_var_name (const void *v_, const void *aux UNUSED) 
{
  const struct variable *v = v_;

  return hsh_hash_case_string (var_get_name (v));
}

/* A hsh_compare_func that orders pointers to variables A and B
   by their names. */
int
compare_var_ptr_names (const void *a_, const void *b_, const void *aux UNUSED) 
{
  struct variable *const *a = a_;
  struct variable *const *b = b_;

  return strcasecmp (var_get_name (*a), var_get_name (*b));
}

/* A hsh_hash_func that hashes pointer to variable V based on its
   name. */
unsigned
hash_var_ptr_name (const void *v_, const void *aux UNUSED) 
{
  struct variable *const *v = v_;

  return hsh_hash_case_string (var_get_name (*v));
}

/* Returns the type of a variable with the given WIDTH. */
static enum var_type
width_to_type (int width) 
{
  return width == 0 ? NUMERIC : ALPHA;
}

/* Returns the type of variable V. */
enum var_type
var_get_type (const struct variable *v) 
{
  return width_to_type (v->width);
}

/* Returns the width of variable V. */
int
var_get_width (const struct variable *v) 
{
  return v->width;
}

/* Sets the width of V to WIDTH. */
void
var_set_width (struct variable *v, int new_width) 
{
  enum var_type new_type = width_to_type (new_width);
  
  if (mv_is_resizable (&v->miss, new_width))
    mv_resize (&v->miss, new_width);
  else
    mv_init (&v->miss, new_width);

  if (v->val_labs != NULL) 
    {
      if (val_labs_can_set_width (v->val_labs, new_width))
        val_labs_set_width (v->val_labs, new_width);
      else 
        {
          val_labs_destroy (v->val_labs);
          v->val_labs = NULL;
        }
    }
  
  if (var_get_type (v) != new_type) 
    {
      v->print = (new_type == NUMERIC
                  ? fmt_for_output (FMT_F, 8, 2)
                  : fmt_for_output (FMT_A, new_width, 0));
      v->write = v->print;
    }
  else if (new_type == ALPHA) 
    {
      v->print.w = v->print.type == FMT_AHEX ? new_width * 2 : new_width;
      v->write.w = v->write.type == FMT_AHEX ? new_width * 2 : new_width;
    }

  v->width = new_width;
}

/* Returns true if variable V is numeric, false otherwise. */
bool
var_is_numeric (const struct variable *v) 
{
  return var_get_type (v) == NUMERIC;
}

/* Returns true if variable V is a string variable, false
   otherwise. */
bool
var_is_alpha (const struct variable *v) 
{
  return var_get_type (v) == ALPHA;
}

/* Returns true if variable V is a short string variable, false
   otherwise. */
bool
var_is_short_string (const struct variable *v) 
{
  return v->width > 0 && v->width <= MAX_SHORT_STRING;
}

/* Returns true if variable V is a long string variable, false
   otherwise. */
bool
var_is_long_string (const struct variable *v) 
{
  return v->width > MAX_SHORT_STRING;
}

/* Returns true if variable V is a very long string variable,
   false otherwise. */
bool
var_is_very_long_string (const struct variable *v) 
{
  return v->width > MAX_LONG_STRING;
}

/* Returns variable V's missing values. */
const struct missing_values *
var_get_missing_values (const struct variable *v) 
{
  return &v->miss;
}

/* Sets variable V's missing values to MISS, which must be of the
   correct width. */
void
var_set_missing_values (struct variable *v, const struct missing_values *miss)
{
  if (miss != NULL) 
    {
      assert (v->width == mv_get_width (miss));
      mv_copy (&v->miss, miss);
    }
  else
    mv_init (&v->miss, v->width);
}

/* Sets variable V to have no user-missing values. */
void
var_clear_missing_values (struct variable *v) 
{
  var_set_missing_values (v, NULL);
}

/* Returns true if V has any user-missing values,
   false otherwise. */
bool
var_has_missing_values (const struct variable *v) 
{
  return !mv_is_empty (&v->miss);
}

/* Returns true if VALUE is system missing or user-missing value
   for V, false otherwise. */
bool
var_is_value_missing (const struct variable *v, const union value *value) 
{
  return mv_is_value_missing (&v->miss, value);
}

/* Returns true if D is system missing or a missing value in V,
   false otherwise.
   V must be a numeric variable. */
bool
var_is_num_missing (const struct variable *v, double d) 
{
  return mv_is_num_missing (&v->miss, d);
}

/* Returns true if S[] is a missing value for V, false otherwise.
   S[] must contain exactly as many characters as V's width.
   V must be a string variable. */
bool
var_is_str_missing (const struct variable *v, const char s[]) 
{
  return mv_is_str_missing (&v->miss, s);
}

/* Returns true if VALUE is a missing value for V, false
   otherwise. */
bool
var_is_value_user_missing (const struct variable *v, const union value *value) 
{
  return mv_is_value_user_missing (&v->miss, value);
}

/* Returns true if D is a user-missing value for V, false
   otherwise.  V must be a numeric variable. */
bool
var_is_num_user_missing (const struct variable *v, double d) 
{
  return mv_is_num_user_missing (&v->miss, d);
}

/* Returns true if S[] is a missing value for V, false otherwise.
   V must be a string variable. 
   S[] must contain exactly as many characters as V's width. */
bool
var_is_str_user_missing (const struct variable *v, const char s[]) 
{
  return mv_is_str_user_missing (&v->miss, s);
}

/* Returns true if V is a numeric variable and VALUE is the
   system missing value. */
bool
var_is_value_system_missing (const struct variable *v,
                             const union value *value) 
{
  return mv_is_value_system_missing (&v->miss, value);
}

/* Print and write formats. */

/* Returns V's print format specification. */
const struct fmt_spec *
var_get_print_format (const struct variable *v) 
{
  return &v->print;
}

/* Sets V's print format specification to PRINT, which must be a
   valid format specification for outputting a variable of V's
   width. */
void
var_set_print_format (struct variable *v, const struct fmt_spec *print) 
{
  assert (fmt_check_width_compat (print, v->width));
  v->print = *print;
}

/* Returns V's write format specification. */
const struct fmt_spec *
var_get_write_format (const struct variable *v) 
{
  return &v->write;
}

/* Sets V's write format specification to WRITE, which must be a
   valid format specification for outputting a variable of V's
   width. */
void
var_set_write_format (struct variable *v, const struct fmt_spec *write) 
{
  assert (fmt_check_width_compat (write, v->width));
  v->write = *write;
}

/* Sets V's print and write format specifications to FORMAT,
   which must be a valid format specification for outputting a
   variable of V's width. */
void
var_set_both_formats (struct variable *v, const struct fmt_spec *format) 
{
  var_set_print_format (v, format);
  var_set_write_format (v, format);
}

/* Returns V's variable label, or a null pointer if it has none. */
const char *
var_get_label (const struct variable *v) 
{
  return v->label;
}

/* Sets V's variable label to LABEL, stripping off leading and
   trailing white space and truncating to 255 characters.
   If LABEL is a null pointer or if LABEL is an empty string
   (after stripping white space), then V's variable label (if
   any) is removed. */
void
var_set_label (struct variable *v, const char *label) 
{
  free (v->label);
  v->label = NULL;

  if (label != NULL) 
    {
      struct substring s = ss_cstr (label);
      ss_trim (&s, ss_cstr (CC_SPACES));
      ss_truncate (&s, 255);
      if (!ss_is_empty (s)) 
        v->label = ss_xstrdup (s);
    }
}

/* Removes any variable label from V. */
void
var_clear_label (struct variable *v) 
{
  var_set_label (v, NULL);
}

/* Returns true if V has a variable V,
   false otherwise. */
bool
var_has_label (const struct variable *v) 
{
  return v->label != NULL;
}

/* Returns V's measurement level. */
enum measure
var_get_measure (const struct variable *v) 
{
  return v->measure;
}

/* Sets V's measurement level to MEASURE. */
void
var_set_measure (struct variable *v, enum measure measure) 
{
  assert (measure_is_valid (measure));
  v->measure = measure;
}

/* Returns V's display width, which applies only to GUIs. */
int
var_get_display_width (const struct variable *v) 
{
  return v->display_width;
}

/* Sets V's display width to DISPLAY_WIDTH. */
void
var_set_display_width (struct variable *v, int display_width) 
{
  v->display_width = display_width;
}

/* Returns V's display alignment, which applies only to GUIs. */
enum alignment
var_get_alignment (const struct variable *v) 
{
  return v->alignment;
}

/* Sets V's display alignment to ALIGNMENT. */
void
var_set_alignment (struct variable *v, enum alignment alignment) 
{
  assert (alignment_is_valid (alignment));
  v->alignment = alignment;
}

/* Returns the number of "union value"s need to store a value of
   variable V. */
size_t
var_get_value_cnt (const struct variable *v) 
{
  return v->width == 0 ? 1 : DIV_RND_UP (v->width, MAX_SHORT_STRING);
}

/* Return whether variable V's values should be preserved from
   case to case. */
bool
var_get_leave (const struct variable *v) 
{
  return v->leave;
}

/* Returns V's short name, if it has one, or a null pointer
   otherwise.

   Short names are used only for system and portable file input
   and output.  They are upper-case only, not necessarily unique,
   and limited to SHORT_NAME_LEN characters (plus a null
   terminator).  Any variable may have no short name, indicated
   by returning a null pointer. */
const char *
var_get_short_name (const struct variable *v) 
{
  return v->short_name[0] != '\0' ? v->short_name : NULL;
}

/* Sets V's short_name to SHORT_NAME, truncating it to
   SHORT_NAME_LEN characters and converting it to uppercase in
   the process.  Specifying a null pointer for SHORT_NAME clears
   the variable's short name. */
void
var_set_short_name (struct variable *v, const char *short_name) 
{
  assert (v != NULL);
  assert (short_name == NULL || var_is_plausible_name (short_name, false));

  if (short_name != NULL) 
    {
      str_copy_trunc (v->short_name, sizeof v->short_name, short_name);
      str_uppercase (v->short_name); 
    }
  else
    v->short_name[0] = '\0';
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
   SUFFIX_NUMBER.  Truncates BASE as necessary to fit. */
void
var_set_short_name_suffix (struct variable *v, const char *base,
                           int suffix_number)
{
  char suffix[SHORT_NAME_LEN + 1];
  char short_name[SHORT_NAME_LEN + 1];
  char *start, *end;
  int len, ofs;

  assert (v != NULL);
  assert (suffix_number >= 0);

  /* Set base name. */
  var_set_short_name (v, base);

  /* Compose suffix. */
  start = end = suffix + sizeof suffix - 1;
  *end = '\0';
  do 
    {
      *--start = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[suffix_number % 26];
      if (start <= suffix + 1)
        msg (SE, _("Variable suffix too large."));
      suffix_number /= 26;
    }
  while (suffix_number > 0);
  *--start = '_';

  /* Append suffix to V's short name. */
  str_copy_trunc (short_name, sizeof short_name, base);
  len = end - start;
  if (len + strlen (short_name) > SHORT_NAME_LEN)
    ofs = SHORT_NAME_LEN - len;
  else
    ofs = strlen (short_name);
  strcpy (short_name + ofs, start);

  /* Set name. */
  var_set_short_name (v, short_name);
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
      NOT_REACHED ();
    }
}

/* Return the number of bytes used when writing case_data for a variable 
   of WIDTH */
int
width_to_bytes(int width)
{
  assert (width >= 0);

  if ( width == 0 ) 
    return MAX_SHORT_STRING ;
  else if (width <= MAX_LONG_STRING) 
    return ROUND_UP (width, MAX_SHORT_STRING);
  else 
    {
      int chunks = width / EFFECTIVE_LONG_STRING_LENGTH ;
      int remainder = width % EFFECTIVE_LONG_STRING_LENGTH ;
      int bytes = remainder + (chunks * (MAX_LONG_STRING + 1) );
      return ROUND_UP (bytes, MAX_SHORT_STRING); 
    }
}


