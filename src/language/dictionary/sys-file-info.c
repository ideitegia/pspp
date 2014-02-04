/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <float.h>
#include <stdlib.h>

#include "data/attributes.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "data/sys-file-reader.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "data/vector.h"
#include "language/command.h"
#include "language/data-io/file-handle.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/string-array.h"
#include "output/tab.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Information to include in displaying a dictionary. */
enum 
  {
    DF_DICT_INDEX       = 1 << 0,
    DF_FORMATS          = 1 << 1,
    DF_VALUE_LABELS     = 1 << 2,
    DF_VARIABLE_LABELS  = 1 << 3,
    DF_MISSING_VALUES   = 1 << 4,
    DF_AT_ATTRIBUTES    = 1 << 5, /* Attributes whose names begin with @. */
    DF_ATTRIBUTES       = 1 << 6, /* All other attributes. */
    DF_MISC             = 1 << 7,
    DF_ALL              = (1 << 8) - 1
  };

static int describe_variable (const struct variable *v, struct tab_table *t,
                              int r, int pc, int flags);

/* SYSFILE INFO utility. */
int
cmd_sysfile_info (struct lexer *lexer, struct dataset *ds UNUSED)
{
  struct file_handle *h;
  struct dictionary *d;
  struct tab_table *t;
  struct casereader *reader;
  struct sfm_read_info info;
  char *encoding;
  int r, i;

  h = NULL;
  encoding = NULL;
  for (;;)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "FILE") || lex_is_string (lexer))
	{
	  lex_match (lexer, T_EQUALS);

          fh_unref (h);
	  h = fh_parse (lexer, FH_REF_FILE, NULL);
	  if (h == NULL)
            goto error;
	}
      else if (lex_match_id (lexer, "ENCODING"))
        {
	  lex_match (lexer, T_EQUALS);

          if (!lex_force_string (lexer))
            goto error;

          free (encoding);
          encoding = ss_xstrdup (lex_tokss (lexer));

          lex_get (lexer);
        }
      else
        break;
    }

  if (h == NULL)
    {
      lex_sbc_missing ("FILE");
      goto error;
    }

  reader = sfm_open_reader (h, encoding, &d, &info);
  if (!reader)
    goto error;

  casereader_destroy (reader);

  t = tab_create (2, 11 + (info.product_ext != NULL));
  r = 0;
  tab_vline (t, TAL_GAP, 1, 0, 8);

  tab_text (t, 0, r, TAB_LEFT, _("File:"));
  tab_text (t, 1, r++, TAB_LEFT, fh_get_file_name (h));

  tab_text (t, 0, r, TAB_LEFT, _("Label:"));
  {
    const char *label = dict_get_label (d);
    if (label == NULL)
      label = _("No label.");
    tab_text (t, 1, r++, TAB_LEFT, label);
  }

  tab_text (t, 0, r, TAB_LEFT, _("Created:"));
  tab_text_format (t, 1, r++, TAB_LEFT, "%s %s by %s",
                   info.creation_date, info.creation_time, info.product);

  if (info.product_ext)
    {
      tab_text (t, 0, r, TAB_LEFT, _("Product:"));
      tab_text (t, 1, r++, TAB_LEFT, info.product_ext);
    }

  tab_text (t, 0, r, TAB_LEFT, _("Integer Format:"));
  tab_text (t, 1, r++, TAB_LEFT,
            info.integer_format == INTEGER_MSB_FIRST ? _("Big Endian")
            : info.integer_format == INTEGER_LSB_FIRST ? _("Little Endian")
            : _("Unknown"));

  tab_text (t, 0, r, TAB_LEFT, _("Real Format:"));
  tab_text (t, 1, r++, TAB_LEFT,
            info.float_format == FLOAT_IEEE_DOUBLE_LE ? _("IEEE 754 LE.")
            : info.float_format == FLOAT_IEEE_DOUBLE_BE ? _("IEEE 754 BE.")
            : info.float_format == FLOAT_VAX_D ? _("VAX D.")
            : info.float_format == FLOAT_VAX_G ? _("VAX G.")
            : info.float_format == FLOAT_Z_LONG ? _("IBM 390 Hex Long.")
            : _("Unknown"));

  tab_text (t, 0, r, TAB_LEFT, _("Variables:"));
  tab_text_format (t, 1, r++, TAB_LEFT, "%zu", dict_get_var_cnt (d));

  tab_text (t, 0, r, TAB_LEFT, _("Cases:"));
  if (info.case_cnt == -1)
    tab_text (t, 1, r, TAB_LEFT, _("Unknown"));
  else
    tab_text_format (t, 1, r, TAB_LEFT, "%ld", (long int) info.case_cnt);
  r++;

  tab_text (t, 0, r, TAB_LEFT, _("Type:"));
  tab_text (t, 1, r++, TAB_LEFT, _("System File"));

  tab_text (t, 0, r, TAB_LEFT, _("Weight:"));
  {
    struct variable *weight_var = dict_get_weight (d);
    tab_text (t, 1, r++, TAB_LEFT,
              (weight_var != NULL
               ? var_get_name (weight_var) : _("Not weighted.")));
  }

  tab_text (t, 0, r, TAB_LEFT, _("Compression:"));
  tab_text_format (t, 1, r++, TAB_LEFT,
                   info.compression == SFM_COMP_NONE ? _("None")
                   : info.compression == SFM_COMP_SIMPLE ? "SAV"
                   : "ZSAV");

  tab_text (t, 0, r, TAB_LEFT, _("Charset:"));
  tab_text (t, 1, r++, TAB_LEFT, dict_get_encoding (d));

  tab_submit (t);

  t = tab_create (4, 1 + 2 * dict_get_var_cnt (d));
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Variable"));
  tab_joint_text (t, 1, 0, 2, 0, TAB_LEFT | TAT_TITLE, _("Description"));
  tab_text (t, 3, 0, TAB_LEFT | TAT_TITLE, _("Position"));
  tab_hline (t, TAL_2, 0, 3, 1);
  for (r = 1, i = 0; i < dict_get_var_cnt (d); i++)
    r = describe_variable (dict_get_var (d, i), t, r, 3,
                           DF_ALL & ~DF_AT_ATTRIBUTES);

  tab_box (t, TAL_1, TAL_1, -1, -1, 0, 0, 3, r);
  tab_vline (t, TAL_1, 1, 0, r);
  tab_vline (t, TAL_1, 3, 0, r);

  tab_resize (t, -1, r);
  tab_submit (t);

  dict_destroy (d);

  fh_unref (h);
  sfm_read_info_destroy (&info);
  return CMD_SUCCESS;

error:
  fh_unref (h);
  free (encoding);
  return CMD_FAILURE;
}

/* DISPLAY utility. */

static void display_macros (void);
static void display_documents (const struct dictionary *dict);
static void display_variables (const struct variable **, size_t, int);
static void display_vectors (const struct dictionary *dict, int sorted);
static void display_data_file_attributes (struct attrset *, int flags);

int
cmd_display (struct lexer *lexer, struct dataset *ds)
{
  /* Whether to sort the list of variables alphabetically. */
  int sorted;

  /* Variables to display. */
  size_t n;
  const struct variable **vl;

  if (lex_match_id (lexer, "MACROS"))
    display_macros ();
  else if (lex_match_id (lexer, "DOCUMENTS"))
    display_documents (dataset_dict (ds));
  else if (lex_match_id (lexer, "FILE"))
    {
      if (!lex_force_match_id (lexer, "LABEL"))
	return CMD_FAILURE;
      if (dict_get_label (dataset_dict (ds)) == NULL)
	tab_output_text (TAB_LEFT,
			 _("The active dataset does not have a file label."));
      else
        tab_output_text_format (TAB_LEFT, _("File label: %s"),
                                dict_get_label (dataset_dict (ds)));
    }
  else
    {
      int flags;

      sorted = lex_match_id (lexer, "SORTED");

      if (lex_match_id (lexer, "VECTORS"))
	{
	  display_vectors (dataset_dict(ds), sorted);
	  return CMD_SUCCESS;
	}
      else if (lex_match_id (lexer, "SCRATCH")) 
        {
          dict_get_vars (dataset_dict (ds), &vl, &n, DC_ORDINARY);
          flags = 0;
        }
      else 
        {
          struct subcommand 
            {
              const char *name;
              int flags;
            };
          static const struct subcommand subcommands[] = 
            {
              {"@ATTRIBUTES", DF_ATTRIBUTES | DF_AT_ATTRIBUTES},
              {"ATTRIBUTES", DF_ATTRIBUTES},
              {"DICTIONARY", DF_ALL & ~DF_AT_ATTRIBUTES},
              {"INDEX", DF_DICT_INDEX},
              {"LABELS", DF_DICT_INDEX | DF_VARIABLE_LABELS},
              {"NAMES", 0},
              {"VARIABLES",
               DF_DICT_INDEX | DF_FORMATS | DF_MISSING_VALUES | DF_MISC},
              {NULL, 0},
            };
          const struct subcommand *sbc;

          flags = 0;
          for (sbc = subcommands; sbc->name != NULL; sbc++)
            if (lex_match_id (lexer, sbc->name))
              {
                flags = sbc->flags;
                break;
              }

          lex_match (lexer, T_SLASH);
          lex_match_id (lexer, "VARIABLES");
          lex_match (lexer, T_EQUALS);

          if (lex_token (lexer) != T_ENDCMD)
            {
              if (!parse_variables_const (lexer, dataset_dict (ds), &vl, &n,
                                          PV_NONE))
                {
                  free (vl);
                  return CMD_FAILURE;
                }
            }
          else
            dict_get_vars (dataset_dict (ds), &vl, &n, 0);
        }

      if (n > 0) 
        {
          sort (vl, n, sizeof *vl,
                (sorted
                 ? compare_var_ptrs_by_name
                 : compare_var_ptrs_by_dict_index), NULL);
          display_variables (vl, n, flags);
        }
      else
        msg (SW, _("No variables to display."));
      free (vl);

      if (flags & (DF_ATTRIBUTES | DF_AT_ATTRIBUTES))
        display_data_file_attributes (dict_get_attributes (dataset_dict (ds)),
                                      flags);
    }

  return CMD_SUCCESS;
}

static void
display_macros (void)
{
  tab_output_text (TAB_LEFT, _("Macros not supported."));
}

static void
display_documents (const struct dictionary *dict)
{
  const struct string_array *documents = dict_get_documents (dict);

  if (string_array_is_empty (documents))
    tab_output_text (TAB_LEFT, _("The active dataset dictionary does not "
                                 "contain any documents."));
  else
    {
      size_t i;

      tab_output_text (TAB_LEFT | TAT_TITLE,
		       _("Documents in the active dataset:"));
      for (i = 0; i < dict_get_document_line_cnt (dict); i++)
        tab_output_text (TAB_LEFT | TAB_FIX, dict_get_document_line (dict, i));
    }
}

static void
display_variables (const struct variable **vl, size_t n, int flags)
{
  struct tab_table *t;
  int nc;			/* Number of columns. */
  int pc;			/* `Position column' */
  int r;			/* Current row. */
  size_t i;

  /* One column for the name,
     two columns for general description,
     one column for dictionary index. */
  nc = 1;
  if (flags & ~DF_DICT_INDEX)
    nc += 2;
  pc = nc;
  if (flags & DF_DICT_INDEX)
    nc++;

  t = tab_create (nc, n + 5);
  tab_headers (t, 0, 0, 1, 0);
  tab_hline (t, TAL_2, 0, nc - 1, 1);
  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Variable"));
  if (flags & ~DF_DICT_INDEX) 
    tab_joint_text (t, 1, 0, 2, 0, TAB_LEFT | TAT_TITLE,
                    (flags & ~(DF_DICT_INDEX | DF_VARIABLE_LABELS)
                     ? _("Description") : _("Label")));
  if (flags & DF_DICT_INDEX)
    tab_text (t, pc, 0, TAB_LEFT | TAT_TITLE, _("Position"));

  r = 1;
  for (i = 0; i < n; i++)
    r = describe_variable (vl[i], t, r, pc, flags);
  tab_hline (t, flags & ~DF_DICT_INDEX ? TAL_2 : TAL_1, 0, nc - 1, 1);
  if (flags)
    {
      tab_box (t, TAL_1, TAL_1, -1, -1, 0, 0, nc - 1, r - 1);
      tab_vline (t, TAL_1, 1, 0, r - 1);
    }
  if (flags & ~DF_DICT_INDEX)
    tab_vline (t, TAL_1, nc - 1, 0, r - 1);
  tab_resize (t, -1, r);
  tab_submit (t);
}

static bool
is_at_name (const char *name) 
{
  return name[0] == '@' || (name[0] == '$' && name[1] == '@');
}

static size_t
count_attributes (const struct attrset *set, int flags) 
{
  struct attrset_iterator i;
  struct attribute *attr;
  size_t n_attrs;
  
  n_attrs = 0;
  for (attr = attrset_first (set, &i); attr != NULL;
       attr = attrset_next (set, &i)) 
    if (flags & DF_AT_ATTRIBUTES || !is_at_name (attribute_get_name (attr)))
      n_attrs += attribute_get_n_values (attr);
  return n_attrs;
}

static void
display_attributes (struct tab_table *t, const struct attrset *set, int flags,
                    int c, int r)
{
  struct attribute **attrs;
  size_t n_attrs;
  size_t i;

  n_attrs = attrset_count (set);
  attrs = attrset_sorted (set);
  for (i = 0; i < n_attrs; i++)
    {
      const struct attribute *attr = attrs[i];
      const char *name = attribute_get_name (attr);
      size_t n_values;
      size_t j;

      if (!(flags & DF_AT_ATTRIBUTES) && is_at_name (name))
        continue;

      n_values = attribute_get_n_values (attr);
      for (j = 0; j < n_values; j++)
        {
          if (n_values > 1)
            tab_text_format (t, c, r, TAB_LEFT, "%s[%zu]", name, j + 1);
          else
            tab_text (t, c, r, TAB_LEFT, name);
          tab_text (t, c + 1, r, TAB_LEFT, attribute_get_value (attr, j));
          r++;
        }
    }
  free (attrs);
}

static void
display_data_file_attributes (struct attrset *set, int flags) 
{
  struct tab_table *t;
  size_t n_attrs;

  n_attrs = count_attributes (set, flags);
  if (!n_attrs)
    return;

  t = tab_create (2, n_attrs + 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, -1, TAL_1, 0, 0, tab_nc (t) - 1, tab_nr (t) - 1);
  tab_hline (t, TAL_2, 0, 1, 1); 
  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Attribute"));
  tab_text (t, 1, 0, TAB_LEFT | TAT_TITLE, _("Value"));
  display_attributes (t, set, flags, 0, 1);
  tab_title (t, "Custom data file attributes.");
  tab_submit (t);
}

/* Puts a description of variable V into table T starting at row
   R.  The variable will be described in the format given by
   FLAGS.  Returns the next row available for use in the
   table. */
static int
describe_variable (const struct variable *v, struct tab_table *t, int r,
                   int pc, int flags)
{
  size_t n_attrs = 0;
  int need_rows;

  /* Make sure that enough rows are allocated. */
  need_rows = 1;
  if (flags & ~(DF_DICT_INDEX | DF_VARIABLE_LABELS))
    need_rows += 16;
  if (flags & DF_VALUE_LABELS)
    need_rows += val_labs_count (var_get_value_labels (v));
  if (flags & (DF_ATTRIBUTES | DF_AT_ATTRIBUTES))
    {
      n_attrs = count_attributes (var_get_attributes (v), flags);
      need_rows += n_attrs; 
    }
  if (r + need_rows > tab_nr (t))
    {
      int nr = MAX (r + need_rows, tab_nr (t) * 2);
      tab_realloc (t, -1, nr);
    }

  /* Put the name, var label, and position into the first row. */
  tab_text (t, 0, r, TAB_LEFT, var_get_name (v));
  if (flags & DF_DICT_INDEX)
    tab_text_format (t, pc, r, 0, "%zu", var_get_dict_index (v) + 1);

  if (flags & DF_VARIABLE_LABELS && var_has_label (v))
    {
      if (flags & ~(DF_DICT_INDEX | DF_VARIABLE_LABELS))
        tab_joint_text_format (t, 1, r, 2, r, TAB_LEFT,
                               _("Label: %s"), var_get_label (v));
      else
        tab_joint_text (t, 1, r, 2, r, TAB_LEFT, var_get_label (v));
      r++;
    }

  /* Print/write format, or print and write formats. */
  if (flags & DF_FORMATS) 
    {
      const struct fmt_spec *print = var_get_print_format (v);
      const struct fmt_spec *write = var_get_write_format (v);

      if (fmt_equal (print, write))
        {
          char str[FMT_STRING_LEN_MAX + 1];
          tab_joint_text_format (t, 1, r, 2, r, TAB_LEFT,
                                 _("Format: %s"), fmt_to_string (print, str));
          r++;
        }
      else
        {
          char str[FMT_STRING_LEN_MAX + 1];
          tab_joint_text_format (t, 1, r, 2, r, TAB_LEFT,
                                 _("Print Format: %s"),
                                 fmt_to_string (print, str));
          r++;
          tab_joint_text_format (t, 1, r, 2, r, TAB_LEFT,
                                 _("Write Format: %s"),
                                 fmt_to_string (write, str));
          r++;
        }
    }
  
  /* Measurement level, role, display width, alignment. */
  if (flags & DF_MISC) 
    {
      enum var_role role = var_get_role (v);

      tab_joint_text_format (t, 1, r, 2, r, TAB_LEFT,
                             _("Measure: %s"),
                             measure_to_string (var_get_measure (v)));
      r++;

      if (role != ROLE_INPUT)
        {
          tab_joint_text_format (t, 1, r, 2, r, TAB_LEFT,
                                 _("Role: %s"), var_role_to_string (role));
          r++;
        }

      tab_joint_text_format (t, 1, r, 2, r, TAB_LEFT,
                             _("Display Alignment: %s"),
                             alignment_to_string (var_get_alignment (v)));
      r++;

      tab_joint_text_format (t, 1, r, 2, r, TAB_LEFT,
                             _("Display Width: %d"),
                             var_get_display_width (v));
      r++;
    }
  
  /* Missing values if any. */
  if (flags & DF_MISSING_VALUES && var_has_missing_values (v))
    {
      const struct missing_values *mv = var_get_missing_values (v);
      char buf[128];
      char *cp;
      int cnt = 0;
      int i;

      cp = stpcpy (buf, _("Missing Values: "));

      if (mv_has_range (mv))
        {
          double x, y;
          mv_get_range (mv, &x, &y);
          if (x == LOWEST)
            cp += sprintf (cp, "LOWEST THRU %.*g", DBL_DIG + 1, y);
          else if (y == HIGHEST)
            cp += sprintf (cp, "%.*g THRU HIGHEST", DBL_DIG + 1, x);
          else
            cp += sprintf (cp, "%.*g THRU %.*g",
                           DBL_DIG + 1, x,
                           DBL_DIG + 1, y);
          cnt++;
        }
      for (i = 0; i < mv_n_values (mv); i++)
        {
          const union value *value = mv_get_value (mv, i);
          if (cnt++ > 0)
            cp += sprintf (cp, "; ");
          if (var_is_numeric (v))
            cp += sprintf (cp, "%.*g", DBL_DIG + 1, value->f);
          else
            {
              int width = var_get_width (v);
              int mv_width = MIN (width, MV_MAX_STRING);

              *cp++ = '"';
	      memcpy (cp, value_str (value, width), mv_width);
	      cp += mv_width;
	      *cp++ = '"';
              *cp = '\0';
            }
        }

      tab_joint_text (t, 1, r, 2, r, TAB_LEFT, buf);
      r++;
    }

  /* Value labels. */
  if (flags & DF_VALUE_LABELS && var_has_value_labels (v))
    {
      const struct val_labs *val_labs = var_get_value_labels (v);
      size_t n_labels = val_labs_count (val_labs);
      const struct val_lab **labels;
      int orig_r = r;
      size_t i;

#if 0
      tab_text (t, 1, r, TAB_LEFT, _("Value"));
      tab_text (t, 2, r, TAB_LEFT, _("Label"));
      r++;
#endif

      tab_hline (t, TAL_1, 1, 2, r);

      labels = val_labs_sorted (val_labs);
      for (i = 0; i < n_labels; i++)
        {
          const struct val_lab *vl = labels[i];

	  tab_value (t, 1, r, TAB_NONE, &vl->value, v, NULL);
	  tab_text (t, 2, r, TAB_LEFT, val_lab_get_escaped_label (vl));
	  r++;
	}
      free (labels);

      tab_vline (t, TAL_1, 2, orig_r, r - 1);
    }

  if (flags & (DF_ATTRIBUTES | DF_AT_ATTRIBUTES) && n_attrs)
    {
      tab_joint_text (t, 1, r, 2, r, TAB_LEFT, "Custom attributes:");
      r++;

      display_attributes (t, var_get_attributes (v), flags, 1, r);
      r += n_attrs;
    }

  /* Draw a line below the last row of information on this variable. */
  tab_hline (t, TAL_1, 0, tab_nc (t) - 1, r);

  return r;
}

/* Display a list of vectors.  If SORTED is nonzero then they are
   sorted alphabetically. */
static void
display_vectors (const struct dictionary *dict, int sorted)
{
  const struct vector **vl;
  int i;
  struct tab_table *t;
  size_t nvec;
  size_t nrow;
  size_t row;

  nvec = dict_get_vector_cnt (dict);
  if (nvec == 0)
    {
      msg (SW, _("No vectors defined."));
      return;
    }

  vl = xnmalloc (nvec, sizeof *vl);
  nrow = 0;
  for (i = 0; i < nvec; i++)
    {
      vl[i] = dict_get_vector (dict, i);
      nrow += vector_get_var_cnt (vl[i]);
    }
  if (sorted)
    qsort (vl, nvec, sizeof *vl, compare_vector_ptrs_by_name);

  t = tab_create (4, nrow + 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, -1, -1, 0, 0, 3, nrow);
  tab_box (t, -1, -1, -1, TAL_1, 0, 0, 3, nrow);
  tab_hline (t, TAL_2, 0, 3, 1);
  tab_text (t, 0, 0, TAT_TITLE | TAB_LEFT, _("Vector"));
  tab_text (t, 1, 0, TAT_TITLE | TAB_LEFT, _("Position"));
  tab_text (t, 2, 0, TAT_TITLE | TAB_LEFT, _("Variable"));
  tab_text (t, 3, 0, TAT_TITLE | TAB_LEFT, _("Print Format"));

  row = 1;
  for (i = 0; i < nvec; i++)
    {
      const struct vector *vec = vl[i];
      size_t j;

      tab_joint_text (t, 0, row, 0, row + vector_get_var_cnt (vec) - 1,
                      TAB_LEFT, vector_get_name (vl[i]));

      for (j = 0; j < vector_get_var_cnt (vec); j++)
        {
          struct variable *var = vector_get_var (vec, j);
          char fmt_string[FMT_STRING_LEN_MAX + 1];
          fmt_to_string (var_get_print_format (var), fmt_string);

          tab_text_format (t, 1, row, TAB_RIGHT, "%zu", j + 1);
          tab_text (t, 2, row, TAB_LEFT, var_get_name (var));
          tab_text (t, 3, row, TAB_LEFT, fmt_string);
          row++;
        }
      tab_hline (t, TAL_1, 0, 3, row);
    }

  tab_submit (t);

  free (vl);
}
