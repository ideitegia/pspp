/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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
#include "variable.h"

#include <stdlib.h>

#include "category.h"
#include "data-out.h"
#include "format.h"
#include "dictionary.h"
#include "identifier.h"
#include "missing-values.h"
#include "value-labels.h"
#include "vardict.h"

#include <libpspp/misc.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A variable. */
struct variable
  {
    /* Dictionary information. */
    char name[LONG_NAME_LEN + 1]; /* Variable name.  Mixed case. */
    int width;			/* 0 for numeric, otherwise string width. */
    struct missing_values miss; /* Missing values. */
    struct fmt_spec print;	/* Default format for PRINT. */
    struct fmt_spec write;	/* Default format for WRITE. */
    struct val_labs *val_labs;  /* Value labels. */
    char *label;		/* Variable label. */

    /* GUI information. */
    enum measure measure;       /* Nominal, ordinal, or continuous. */
    int display_width;          /* Width of data editor column. */
    enum alignment alignment;   /* Alignment of data in GUI. */

    /* Case information. */
    bool leave;                 /* Leave value from case to case? */

    /* Data for use by containing dictionary. */
    struct vardict_info vardict;

    /* Used only for system and portable file input and output.
       See short-names.h. */
    char **short_names;
    size_t short_name_cnt;

    /* Each command may use these fields as needed. */
    void *aux;
    void (*aux_dtor) (struct variable *);

    /* Values of a categorical variable.  Procedures need
       vectors with binary entries, so any variable of type ALPHA will
       have its values stored here. */
    struct cat_vals *obs_vals;
  };

/* Returns true if VAR_TYPE is a valid variable type. */
bool
var_type_is_valid (enum var_type var_type)
{
  return var_type == VAR_NUMERIC || var_type == VAR_STRING;
}

/* Returns the variable type for the given width. */
enum var_type
var_type_from_width (int width)
{
  return width != 0 ? VAR_STRING : VAR_NUMERIC;
}

/* Creates and returns a new variable with the given NAME and
   WIDTH and other fields initialized to default values.  The
   variable is not added to a dictionary; for that, use
   dict_create_var instead. */
struct variable *
var_create (const char *name, int width)
{
  struct variable *v;

  assert (width >= 0 && width <= MAX_STRING);

  v = xmalloc (sizeof *v);
  v->vardict.dict_index = v->vardict.case_index = -1;
  var_set_name (v, name);
  v->width = width;
  mv_init (&v->miss, width);
  v->leave = var_must_leave (v);
  if (var_is_numeric (v))
    {
      v->print = fmt_for_output (FMT_F, 8, 2);
      v->alignment = ALIGN_RIGHT;
      v->measure = MEASURE_SCALE;
    }
  else
    {
      v->print = fmt_for_output (FMT_A, var_get_width (v), 0);
      v->alignment = ALIGN_LEFT;
      v->measure = MEASURE_NOMINAL;
    }
  v->display_width = var_default_display_width (width);
  v->write = v->print;
  v->val_labs = NULL;
  v->label = NULL;
  v->short_names = NULL;
  v->short_name_cnt = 0;
  v->aux = NULL;
  v->aux_dtor = NULL;
  v->obs_vals = NULL;

  return v;
}

/* Creates and returns a clone of OLD_VAR.  Most properties of
   the new variable are copied from OLD_VAR, except:

    - The variable's short name is not copied, because there is
      no reason to give a new variable with potentially a new
      name the same short name.

    - The new variable is not added to OLD_VAR's dictionary by
      default.  Use dict_clone_var, instead, to do that.

    - Auxiliary data and obs_vals are not copied. */
struct variable *
var_clone (const struct variable *old_var)
{
  struct variable *new_var = var_create (var_get_name (old_var),
                                         var_get_width (old_var));

  var_set_missing_values (new_var, var_get_missing_values (old_var));
  var_set_print_format (new_var, var_get_print_format (old_var));
  var_set_write_format (new_var, var_get_write_format (old_var));
  var_set_value_labels (new_var, var_get_value_labels (old_var));
  var_set_label (new_var, var_get_label (old_var));
  var_set_measure (new_var, var_get_measure (old_var));
  var_set_display_width (new_var, var_get_display_width (old_var));
  var_set_alignment (new_var, var_get_alignment (old_var));
  var_set_leave (new_var, var_get_leave (old_var));

  return new_var;
}

/* Destroys variable V.
   V must not belong to a dictionary.  If it does, use
   dict_delete_var instead. */
void
var_destroy (struct variable *v)
{
  if (v != NULL)
    {
      assert (!var_has_vardict (v));
      cat_stored_values_destroy (v->obs_vals);
      var_clear_short_names (v);
      var_clear_aux (v);
      val_labs_destroy (v->val_labs);
      var_clear_label (v);
      free (v);
    }
}

/* Variable names. */

/* Return variable V's name. */
const char *
var_get_name (const struct variable *v)
{
  return v->name;
}

/* Sets V's name to NAME.
   Do not use this function for a variable in a dictionary.  Use
   dict_rename_var instead. */
void
var_set_name (struct variable *v, const char *name)
{
  assert (v->vardict.dict_index == -1);
  assert (var_is_plausible_name (name, false));

  str_copy_trunc (v->name, sizeof v->name, name);
  dict_var_changed (v);
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
        msg (SE, _("Character `%c' (in %s) may not appear "
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

/* Returns true if NAME is an plausible name for a variable,
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
compare_vars_by_name (const void *a_, const void *b_, const void *aux UNUSED)
{
  const struct variable *a = a_;
  const struct variable *b = b_;

  return strcasecmp (a->name, b->name);
}

/* A hsh_hash_func that hashes variable V based on its name. */
unsigned
hash_var_by_name (const void *v_, const void *aux UNUSED)
{
  const struct variable *v = v_;

  return hsh_hash_case_string (v->name);
}

/* A hsh_compare_func that orders pointers to variables A and B
   by their names. */
int
compare_var_ptrs_by_name (const void *a_, const void *b_,
                          const void *aux UNUSED)
{
  struct variable *const *a = a_;
  struct variable *const *b = b_;

  return strcasecmp (var_get_name (*a), var_get_name (*b));
}

/* A hsh_hash_func that hashes pointer to variable V based on its
   name. */
unsigned
hash_var_ptr_by_name (const void *v_, const void *aux UNUSED)
{
  struct variable *const *v = v_;

  return hsh_hash_case_string (var_get_name (*v));
}

/* Returns the type of variable V. */
enum var_type
var_get_type (const struct variable *v)
{
  return var_type_from_width (v->width);
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
  const int old_width = v->width;

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

  fmt_resize (&v->print, new_width);
  fmt_resize (&v->write, new_width);

  v->width = new_width;

  {
    const int old_val_count = value_cnt_from_width (old_width);
    const int new_val_count = value_cnt_from_width (new_width);

    if ( old_val_count != new_val_count)
	 dict_var_resized (v, new_val_count - old_val_count);
  }

  dict_var_changed (v);
}

/* Returns true if variable V is numeric, false otherwise. */
bool
var_is_numeric (const struct variable *v)
{
  return var_get_type (v) == VAR_NUMERIC;
}

/* Returns true if variable V is a string variable, false
   otherwise. */
bool
var_is_alpha (const struct variable *v)
{
  return var_get_type (v) == VAR_STRING;
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

/* Returns the number of "union value"s need to store a value of
   variable V. */
size_t
var_get_value_cnt (const struct variable *v)
{
  return value_cnt_from_width (v->width);
}

/* Returns variable V's missing values. */
const struct missing_values *
var_get_missing_values (const struct variable *v)
{
  return &v->miss;
}

/* Sets variable V's missing values to MISS, which must be of V's
   width or at least resizable to V's width.
   If MISS is null, then V's missing values, if any, are
   cleared. */
void
var_set_missing_values (struct variable *v, const struct missing_values *miss)
{
  if (miss != NULL)
    {
      assert (mv_is_resizable (miss, v->width));
      mv_copy (&v->miss, miss);
      mv_resize (&v->miss, v->width);
    }
  else
    mv_init (&v->miss, v->width);

  dict_var_changed (v);
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

/* Returns true if VALUE is in the given CLASS of missing values
   in V, false otherwise. */
bool
var_is_value_missing (const struct variable *v, const union value *value,
                      enum mv_class class)
{
  return mv_is_value_missing (&v->miss, value, class);
}

/* Returns true if D is in the given CLASS of missing values in
   V, false otherwise.
   V must be a numeric variable. */
bool
var_is_num_missing (const struct variable *v, double d, enum mv_class class)
{
  return mv_is_num_missing (&v->miss, d, class);
}

/* Returns true if S[] is a missing value for V, false otherwise.
   S[] must contain exactly as many characters as V's width.
   V must be a string variable. */
bool
var_is_str_missing (const struct variable *v, const char s[],
                    enum mv_class class)
{
  return mv_is_str_missing (&v->miss, s, class);
}

/* Returns variable V's value labels,
   possibly a null pointer if it has none. */
const struct val_labs *
var_get_value_labels (const struct variable *v)
{
  return v->val_labs;
}

/* Returns true if variable V has at least one value label. */
bool
var_has_value_labels (const struct variable *v)
{
  return val_labs_count (v->val_labs) > 0;
}

/* Sets variable V's value labels to a copy of VLS,
   which must have a width equal to V's width or one that can be
   changed to V's width.
   If VLS is null, then V's value labels, if any, are removed. */
void
var_set_value_labels (struct variable *v, const struct val_labs *vls)
{
  val_labs_destroy (v->val_labs);
  v->val_labs = NULL;

  if (vls != NULL)
    {
      assert (val_labs_can_set_width (vls, v->width));
      v->val_labs = val_labs_copy (vls);
      val_labs_set_width (v->val_labs, v->width);
      dict_var_changed (v);
    }
}

/* Makes sure that V has a set of value labels,
   by assigning one to it if necessary. */
static void
alloc_value_labels (struct variable *v)
{
  assert (!var_is_long_string (v));
  if (v->val_labs == NULL)
    v->val_labs = val_labs_create (v->width);
}

/* Attempts to add a value label with the given VALUE and LABEL
   to V.  Returns true if successful, false if VALUE has an
   existing label.
   V must not be a long string variable. */
bool
var_add_value_label (struct variable *v,
                     const union value *value, const char *label)
{
  alloc_value_labels (v);
  return val_labs_add (v->val_labs, *value, label);
}

/* Adds or replaces a value label with the given VALUE and LABEL
   to V.
   V must not be a long string variable. */
void
var_replace_value_label (struct variable *v,
                         const union value *value, const char *label)
{
  alloc_value_labels (v);
  val_labs_replace (v->val_labs, *value, label);
}

/* Removes V's value labels, if any. */
void
var_clear_value_labels (struct variable *v)
{
  var_set_value_labels (v, NULL);
}

/* Returns the label associated with VALUE for variable V,
   or a null pointer if none. */
const char *
var_lookup_value_label (const struct variable *v, const union value *value)
{
  return val_labs_find (v->val_labs, *value);
}

/* Get a string representing VALUE for variable V.
   That is, if VALUE has a label, return that label,
   otherwise format VALUE and return the formatted string. */
const char *
var_get_value_name (const struct variable *v, const union value *value)
{
  const char *name = var_lookup_value_label (v, value);
  if (name == NULL)
    {
      static char buf[MAX_STRING + 1];
      data_out (value, &v->print, buf);
      buf[v->print.w] = '\0';
      name = buf;
    }
  return name;
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
  dict_var_changed (v);
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
  dict_var_changed (v);
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

/* Return a string representing this variable, in the form most
   appropriate from a human factors perspective, that is, its
   variable label if it has one, otherwise its name. */
const char *
var_to_string (const struct variable *v)
{
  return v->label != NULL ? v->label : v->name;
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
      dict_var_changed (v);
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

/* Returns true if M is a valid variable measurement level,
   false otherwise. */
bool
measure_is_valid (enum measure m)
{
  return m == MEASURE_NOMINAL || m == MEASURE_ORDINAL || m == MEASURE_SCALE;
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
  dict_var_changed (v);
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
  dict_var_changed (v);
}

/* Returns the default display width for a variable of the given
   WIDTH, as set by var_create.  The return value can be used to
   reset a variable's display width to the default. */
int
var_default_display_width (int width)
{
  return width == 0 ? 8 : MIN (width, 32);
}

/* Returns true if A is a valid alignment,
   false otherwise. */
bool
alignment_is_valid (enum alignment a)
{
  return a == ALIGN_LEFT || a == ALIGN_RIGHT || a == ALIGN_CENTRE;
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
  dict_var_changed (v);
}

/* Whether variables' values should be preserved from case to
   case. */

/* Returns true if variable V's value should be left from case to
   case, instead of being reset to 0, system-missing, or blanks. */
bool
var_get_leave (const struct variable *v)
{
  return v->leave;
}

/* Sets V's leave setting to LEAVE. */
void
var_set_leave (struct variable *v, bool leave)
{
  assert (leave || !var_must_leave (v));
  v->leave = leave;
  dict_var_changed (v);
}

/* Returns true if V must be left from case to case,
   false if it can be set either way. */
bool
var_must_leave (const struct variable *v)
{
  return dict_class_from_id (v->name) == DC_SCRATCH;
}

/* Returns the number of short names stored in VAR.

   Short names are used only for system and portable file input
   and output.  They are upper-case only, not necessarily unique,
   and limited to SHORT_NAME_LEN characters (plus a null
   terminator).  Ordinarily a variable has at most one short
   name, but very long string variables (longer than 255 bytes)
   may have more.  A variable might not have any short name at
   all if it hasn't been saved to or read from a system or
   portable file. */
size_t
var_get_short_name_cnt (const struct variable *var) 
{
  return var->short_name_cnt;
}

/* Returns VAR's short name with the given IDX, if it has one
   with that index, or a null pointer otherwise.  Short names may
   be sparse: even if IDX is less than the number of short names
   in VAR, this function may return a null pointer. */
const char *
var_get_short_name (const struct variable *var, size_t idx)
{
  return idx < var->short_name_cnt ? var->short_names[idx] : NULL;
}

/* Sets VAR's short name with the given IDX to SHORT_NAME,
   truncating it to SHORT_NAME_LEN characters and converting it
   to uppercase in the process.  Specifying a null pointer for
   SHORT_NAME clears the specified short name. */
void
var_set_short_name (struct variable *var, size_t idx, const char *short_name)
{
  assert (var != NULL);
  assert (short_name == NULL || var_is_plausible_name (short_name, false));

  /* Clear old short name numbered IDX, if any. */
  if (idx < var->short_name_cnt) 
    {
      free (var->short_names[idx]);
      var->short_names[idx] = NULL; 
    }

  /* Install new short name for IDX. */
  if (short_name != NULL) 
    {
      if (idx >= var->short_name_cnt)
        {
          size_t old_cnt = var->short_name_cnt;
          size_t i;
          var->short_name_cnt = MAX (idx * 2, 1);
          var->short_names = xnrealloc (var->short_names, var->short_name_cnt,
                                        sizeof *var->short_names);
          for (i = old_cnt; i < var->short_name_cnt; i++)
            var->short_names[i] = NULL;
        }
      var->short_names[idx] = xstrndup (short_name, MAX_SHORT_STRING);
      str_uppercase (var->short_names[idx]);
    }

  dict_var_changed (var);
}

/* Clears V's short names. */
void
var_clear_short_names (struct variable *v)
{
  size_t i;

  for (i = 0; i < v->short_name_cnt; i++)
    free (v->short_names[i]);
  free (v->short_names);
  v->short_names = NULL;
  v->short_name_cnt = 0;
}

/* Relationship with dictionary. */

/* Returns V's index within its dictionary, the value
   for which "dict_get_var (dict, index)" will return V.
   V must be in a dictionary. */
size_t
var_get_dict_index (const struct variable *v)
{
  assert (v->vardict.dict_index != -1);
  return v->vardict.dict_index;
}

/* Returns V's index within the case represented by its
   dictionary, that is, the value for which "case_data_idx (case,
   index)" will return the data for V in that case.
   V must be in a dictionary. */
size_t
var_get_case_index (const struct variable *v)
{
  assert (v->vardict.case_index != -1);
  return v->vardict.case_index;
}

/* Returns V's auxiliary data, or a null pointer if none has been
   attached. */
void *
var_get_aux (const struct variable *v)
{
  return v->aux;
}

/* Assign auxiliary data AUX to variable V, which must not
   already have auxiliary data.  Before V's auxiliary data is
   cleared, AUX_DTOR(V) will be called.  (var_dtor_free, below,
   may be appropriate for use as AUX_DTOR.) */
void *
var_attach_aux (const struct variable *v_,
                void *aux, void (*aux_dtor) (struct variable *))
{
  struct variable *v = (struct variable *) v_ ; /* cast away const  */
  assert (v->aux == NULL);
  assert (aux != NULL);
  v->aux = aux;
  v->aux_dtor = aux_dtor;
  return aux;
}

/* Remove auxiliary data, if any, from V, and return it, without
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

/* Observed categorical values. */

/* Returns V's observed categorical values,
   which V must have. */
struct cat_vals *
var_get_obs_vals (const struct variable *v)
{
  assert (v->obs_vals != NULL);
  return v->obs_vals;
}

/* Sets V's observed categorical values to CAT_VALS. */
void
var_set_obs_vals (const struct variable *v_, struct cat_vals *cat_vals)
{
  struct variable *v = (struct variable *) v_ ; /* cast away const */
  cat_stored_values_destroy (v->obs_vals);
  v->obs_vals = cat_vals;
}

/* Returns true if V has observed categorical values,
   false otherwise. */
bool
var_has_obs_vals (const struct variable *v)
{
  return v->obs_vals != NULL;
}

/* Returns the dictionary class corresponding to a variable named
   NAME. */
enum dict_class
dict_class_from_id (const char *name)
{
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

/* Returns V's vardict structure. */
const struct vardict_info *
var_get_vardict (const struct variable *v)
{
  assert (var_has_vardict (v));
  return &v->vardict;
}

/* Sets V's vardict data to VARDICT. */
void
var_set_vardict (struct variable *v, const struct vardict_info *vardict)
{
  assert (vardict->dict_index >= 0);
  assert (vardict->case_index >= 0);
  v->vardict = *vardict;
}

/* Returns true if V has vardict data. */
bool
var_has_vardict (const struct variable *v)
{
  return v->vardict.dict_index != -1;
}

/* Clears V's vardict data. */
void
var_clear_vardict (struct variable *v)
{
  v->vardict.dict_index = v->vardict.case_index = -1;
}
