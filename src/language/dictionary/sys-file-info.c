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
#include <errno.h>
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
#include "libpspp/hash-functions.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/string-array.h"
#include "output/tab.h"
#include "output/text-item.h"
#include "output/table-item.h"

#include "gl/localcharset.h"
#include "gl/intprops.h"
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
    DF_MEASURE          = 1 << 7,
    DF_ROLE             = 1 << 8,
    DF_ALIGNMENT        = 1 << 9,
    DF_WIDTH            = 1 << 10,
    DF_ALL              = (1 << 11) - 1
  };

static unsigned int dict_display_mask (const struct dictionary *);

static struct table *describe_variable (const struct variable *v, int flags);

static void report_encodings (const struct file_handle *,
                              const struct sfm_reader *);

/* SYSFILE INFO utility. */
int
cmd_sysfile_info (struct lexer *lexer, struct dataset *ds UNUSED)
{
  struct sfm_reader *sfm_reader;
  struct file_handle *h;
  struct dictionary *d;
  struct tab_table *t;
  struct casereader *reader;
  struct sfm_read_info info;
  char *encoding;
  struct table *table;
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

  sfm_reader = sfm_open (h);
  if (sfm_reader == NULL)
    goto error;

  if (encoding && !strcasecmp (encoding, "detect"))
    {
      report_encodings (h, sfm_reader);
      fh_unref (h);
      return CMD_SUCCESS;
    }

  reader = sfm_decode (sfm_reader, encoding, &d, &info);
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

  tab_text (t, 0, r, TAB_LEFT, _("Encoding:"));
  tab_text (t, 1, r++, TAB_LEFT, dict_get_encoding (d));

  tab_submit (t);

  t = tab_create (3, 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Variable"));
  tab_text (t, 1, 0, TAB_LEFT | TAT_TITLE, _("Description"));
  tab_text (t, 2, 0, TAB_LEFT | TAT_TITLE, _("Position"));
  tab_hline (t, TAL_2, 0, 2, 1);

  table = &t->table;
  for (i = 0; i < dict_get_var_cnt (d); i++)
    table = table_vpaste (table,
                          describe_variable (dict_get_var (d, i),
                                             DF_ALL & ~DF_AT_ATTRIBUTES));

  table_item_submit (table_item_create (table, NULL /* XXX */, NULL));

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
               DF_DICT_INDEX | DF_FORMATS | DF_MISSING_VALUES
               | DF_MEASURE | DF_ROLE | DF_ALIGNMENT | DF_WIDTH},
              {NULL, 0},
            };
          const struct subcommand *sbc;
          struct dictionary *dict = dataset_dict (ds);

          flags = 0;
          for (sbc = subcommands; sbc->name != NULL; sbc++)
            if (lex_match_id (lexer, sbc->name))
              {
                flags = sbc->flags & dict_display_mask (dict);
                break;
              }

          lex_match (lexer, T_SLASH);
          lex_match_id (lexer, "VARIABLES");
          lex_match (lexer, T_EQUALS);

          if (lex_token (lexer) != T_ENDCMD)
            {
              if (!parse_variables_const (lexer, dict, &vl, &n, PV_NONE))
                {
                  free (vl);
                  return CMD_FAILURE;
                }
            }
          else
            dict_get_vars (dict, &vl, &n, 0);
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

static int
count_columns (int flags)
{
  int nc = 1;
  if (flags & ~DF_DICT_INDEX)
    nc++;
  if (flags & DF_DICT_INDEX)
    nc++;

  return nc;
}

static int
position_column (int flags)
{
  int pc = 1;
  if (flags & ~DF_DICT_INDEX)
    pc++;
  return pc;
}

static void
display_variables (const struct variable **vl, size_t n, int flags)
{
  struct tab_table *t;
  struct table *table;
  size_t i;
  int nc;

  nc = count_columns (flags);
  t = tab_create (nc, 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_hline (t, TAL_2, 0, nc - 1, 1);
  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Variable"));
  if (flags & ~DF_DICT_INDEX) 
    tab_text (t, 1, 0, TAB_LEFT | TAT_TITLE,
              (flags & ~(DF_DICT_INDEX | DF_VARIABLE_LABELS)
               ? _("Description") : _("Label")));
  if (flags & DF_DICT_INDEX)
    tab_text (t, position_column (flags), 0, TAB_LEFT | TAT_TITLE,
              _("Position"));

  table = &t->table;
  for (i = 0; i < n; i++)
    table = table_vpaste (table, describe_variable (vl[i], flags));

  table_item_submit (table_item_create (table, NULL /* XXX */, NULL));
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

static struct table *
describe_attributes (const struct attrset *set, int flags)
{
  struct attribute **attrs;
  struct tab_table *t;
  size_t n_attrs;
  size_t i;
  int r = 1;

  t = tab_create (2, 1 + count_attributes (set, flags));
  tab_headers (t, 0, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, -1, TAL_1, 0, 0, tab_nc (t) - 1, tab_nr (t) - 1);
  tab_hline (t, TAL_1, 0, 1, 1);
  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Attribute"));
  tab_text (t, 1, 0, TAB_LEFT | TAT_TITLE, _("Value"));

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
            tab_text_format (t, 0, r, TAB_LEFT, "%s[%zu]", name, j + 1);
          else
            tab_text (t, 0, r, TAB_LEFT, name);
          tab_text (t, 1, r, TAB_LEFT, attribute_get_value (attr, j));
          r++;
        }
    }
  free (attrs);

  return &t->table;
}

static void
display_data_file_attributes (struct attrset *set, int flags) 
{
  if (count_attributes (set, flags))
    table_item_submit (table_item_create (describe_attributes (set, flags),
                                          _("Custom data file attributes."),
                                          NULL));
}

static struct table *
describe_value_labels (const struct variable *var)
{
  const struct val_labs *val_labs = var_get_value_labels (var);
  size_t n_labels = val_labs_count (val_labs);
  const struct val_lab **labels;
  struct tab_table *t;
  size_t i;

  t = tab_create (2, n_labels + 1);
  tab_box (t, TAL_1, TAL_1, -1, TAL_1, 0, 0, tab_nc (t) - 1, tab_nr (t) - 1);

  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Value"));
  tab_text (t, 1, 0, TAB_LEFT | TAT_TITLE, _("Label"));

  tab_hline (t, TAL_1, 0, 1, 1);
  tab_vline (t, TAL_1, 1, 0, n_labels);

  labels = val_labs_sorted (val_labs);
  for (i = 0; i < n_labels; i++)
    {
      const struct val_lab *vl = labels[i];

      tab_value (t, 0, i + 1, TAB_NONE, &vl->value, var, NULL);
      tab_text (t, 1, i + 1, TAB_LEFT, val_lab_get_escaped_label (vl));
    }
  free (labels);

  return &t->table;
}

static struct table *
describe_variable_details (const struct variable *v, int flags)
{
  struct table *table;
  struct string s;

  ds_init_empty (&s);

  /* Variable label. */
  if (flags & DF_VARIABLE_LABELS && var_has_label (v))
    {
      if (flags & ~(DF_DICT_INDEX | DF_VARIABLE_LABELS))
        ds_put_format (&s, _("Label: %s\n"), var_get_label (v));
      else
        ds_put_format (&s, "%s\n", var_get_label (v));
    }

  /* Print/write format, or print and write formats. */
  if (flags & DF_FORMATS) 
    {
      const struct fmt_spec *print = var_get_print_format (v);
      const struct fmt_spec *write = var_get_write_format (v);
      char str[FMT_STRING_LEN_MAX + 1];

      if (fmt_equal (print, write))
        ds_put_format (&s, _("Format: %s\n"), fmt_to_string (print, str));
      else
        {
          ds_put_format (&s, _("Print Format: %s\n"),
                         fmt_to_string (print, str));
          ds_put_format (&s, _("Write Format: %s\n"),
                         fmt_to_string (write, str));
        }
    }

  /* Measurement level, role, display width, alignment. */
  if (flags & DF_MEASURE)
    ds_put_format (&s, _("Measure: %s\n"),
                   measure_to_string (var_get_measure (v)));

  if (flags & DF_ROLE)
    ds_put_format (&s, _("Role: %s\n"), var_role_to_string (var_get_role (v)));


  if (flags & DF_ALIGNMENT)
    ds_put_format (&s, _("Display Alignment: %s\n"),
                   alignment_to_string (var_get_alignment (v)));

  if (flags & DF_WIDTH)
    ds_put_format (&s, _("Display Width: %d\n"), var_get_display_width (v));

  /* Missing values if any. */
  if (flags & DF_MISSING_VALUES && var_has_missing_values (v))
    {
      const struct missing_values *mv = var_get_missing_values (v);
      int cnt = 0;
      int i;

      ds_put_cstr (&s, _("Missing Values: "));

      if (mv_has_range (mv))
        {
          double x, y;
          mv_get_range (mv, &x, &y);
          if (x == LOWEST)
            ds_put_format (&s, "LOWEST THRU %.*g", DBL_DIG + 1, y);
          else if (y == HIGHEST)
            ds_put_format (&s, "%.*g THRU HIGHEST", DBL_DIG + 1, x);
          else
            ds_put_format (&s, "%.*g THRU %.*g",
                           DBL_DIG + 1, x,
                           DBL_DIG + 1, y);
          cnt++;
        }
      for (i = 0; i < mv_n_values (mv); i++)
        {
          const union value *value = mv_get_value (mv, i);
          if (cnt++ > 0)
            ds_put_cstr (&s, "; ");
          if (var_is_numeric (v))
            ds_put_format (&s, "%.*g", DBL_DIG + 1, value->f);
          else
            {
              int width = var_get_width (v);
              int mv_width = MIN (width, MV_MAX_STRING);

              ds_put_byte (&s, '"');
              memcpy (ds_put_uninit (&s, mv_width),
                      value_str (value, width), mv_width);
              ds_put_byte (&s, '"');
            }
        }
      ds_put_byte (&s, '\n');
    }

  ds_chomp_byte (&s, '\n');

  table = (ds_is_empty (&s)
           ? NULL
           : table_from_string (TAB_LEFT, ds_cstr (&s)));
  ds_destroy (&s);

  /* Value labels. */
  if (flags & DF_VALUE_LABELS && var_has_value_labels (v))
    table = table_vpaste (table, table_create_nested (describe_value_labels (v)));

  if (flags & (DF_ATTRIBUTES | DF_AT_ATTRIBUTES))
    {
      struct attrset *attrs = var_get_attributes (v);

      if (count_attributes (attrs, flags))
        table = table_vpaste (
          table, table_create_nested (describe_attributes (attrs, flags)));
    }

  return table ? table : table_from_string (TAB_LEFT, "");
}

/* Puts a description of variable V into table T starting at row
   R.  The variable will be described in the format given by
   FLAGS.  Returns the next row available for use in the
   table. */
static struct table *
describe_variable (const struct variable *v, int flags)
{
  struct table *table;

  table = flags & ~DF_DICT_INDEX ? describe_variable_details (v, flags) : NULL;
  table = table_hpaste (table_from_string (0, var_get_name (v)),
                        table ? table_stomp (table) : NULL);
  if (flags & DF_DICT_INDEX)
    {
      char s[INT_BUFSIZE_BOUND (size_t)];

      sprintf (s, "%zu", var_get_dict_index (v) + 1);
      table = table_hpaste (table, table_from_string (0, s));
    }

  return table;
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

/* Encoding analysis. */

static const char *encoding_names[] = {
  /* These encodings are from http://encoding.spec.whatwg.org/, as retrieved
     February 2014.  Encodings not supported by glibc and encodings relevant
     only to HTML have been removed. */
  "utf-8",
  "windows-1252",
  "iso-8859-2",
  "iso-8859-3",
  "iso-8859-4",
  "iso-8859-5",
  "iso-8859-6",
  "iso-8859-7",
  "iso-8859-8",
  "iso-8859-10",
  "iso-8859-13",
  "iso-8859-14",
  "iso-8859-16",
  "macintosh",
  "windows-874",
  "windows-1250",
  "windows-1251",
  "windows-1253",
  "windows-1254",
  "windows-1255",
  "windows-1256",
  "windows-1257",
  "windows-1258",
  "koi8-r",
  "koi8-u",
  "ibm866",
  "gb18030",
  "big5",
  "euc-jp",
  "iso-2022-jp",
  "shift_jis",
  "euc-kr",

  /* Added by user request. */
  "ibm850",
  "din_66003",
};
#define N_ENCODING_NAMES (sizeof encoding_names / sizeof *encoding_names)

struct encoding
  {
    uint64_t encodings;
    char **utf8_strings;
    unsigned int hash;
  };

static char **
recode_strings (struct pool *pool,
                char **strings, bool *ids, size_t n,
                const char *encoding)
{
  char **utf8_strings;
  size_t i;

  utf8_strings = pool_alloc (pool, n * sizeof *utf8_strings);
  for (i = 0; i < n; i++)
    {
      struct substring utf8;
      int error;

      error = recode_pedantically ("UTF-8", encoding, ss_cstr (strings[i]),
                                   pool, &utf8);
      if (!error)
        {
          ss_rtrim (&utf8, ss_cstr (" "));
          utf8.string[utf8.length] = '\0';

          if (ids[i] && !id_is_plausible (utf8.string, false))
            error = EINVAL;
        }

      if (error)
        return NULL;

      utf8_strings[i] = utf8.string;
    }

  return utf8_strings;
}

static struct encoding *
find_duplicate_encoding (struct encoding *encodings, size_t n_encodings,
                         char **utf8_strings, size_t n_strings,
                         unsigned int hash)
{
  struct encoding *e;

  for (e = encodings; e < &encodings[n_encodings]; e++)
    {
      int i;

      if (e->hash != hash)
        goto next_encoding;

      for (i = 0; i < n_strings; i++)
        if (strcmp (utf8_strings[i], e->utf8_strings[i]))
          goto next_encoding;

      return e;
    next_encoding:;
    }

  return NULL;
}

static bool
all_equal (const struct encoding *encodings, size_t n_encodings,
           size_t string_idx)
{
  const char *s0;
  size_t i;

  s0 = encodings[0].utf8_strings[string_idx];
  for (i = 1; i < n_encodings; i++)
    if (strcmp (s0, encodings[i].utf8_strings[string_idx]))
      return false;

  return true;
}

static int
equal_prefix (const struct encoding *encodings, size_t n_encodings,
              size_t string_idx)
{
  const char *s0;
  size_t prefix;
  size_t i;

  s0 = encodings[0].utf8_strings[string_idx];
  prefix = strlen (s0);
  for (i = 1; i < n_encodings; i++)
    {
      const char *si = encodings[i].utf8_strings[string_idx];
      size_t j;

      for (j = 0; j < prefix; j++)
        if (s0[j] != si[j])
          {
            prefix = j;
            if (!prefix)
              return 0;
            break;
          }
    }

  while (prefix > 0 && s0[prefix - 1] != ' ')
    prefix--;
  return prefix;
}

static int
equal_suffix (const struct encoding *encodings, size_t n_encodings,
              size_t string_idx)
{
  const char *s0;
  size_t s0_len;
  size_t suffix;
  size_t i;

  s0 = encodings[0].utf8_strings[string_idx];
  s0_len = strlen (s0);
  suffix = s0_len;
  for (i = 1; i < n_encodings; i++)
    {
      const char *si = encodings[i].utf8_strings[string_idx];
      size_t si_len = strlen (si);
      size_t j;

      if (si_len < suffix)
        suffix = si_len;
      for (j = 0; j < suffix; j++)
        if (s0[s0_len - j - 1] != si[si_len - j - 1])
          {
            suffix = j;
            if (!suffix)
              return 0;
            break;
          }
    }

  while (suffix > 0 && s0[s0_len - suffix] != ' ')
    suffix--;
  return suffix;
}

static void
report_encodings (const struct file_handle *h, const struct sfm_reader *r)
{
  char **titles;
  char **strings;
  bool *ids;
  struct encoding encodings[N_ENCODING_NAMES];
  size_t n_encodings, n_strings, n_unique_strings;
  size_t i, j;
  struct tab_table *t;
  struct pool *pool;
  size_t row;

  pool = pool_create ();
  n_strings = sfm_get_strings (r, pool, &titles, &ids, &strings);

  n_encodings = 0;
  for (i = 0; i < N_ENCODING_NAMES; i++)
    {
      char **utf8_strings;
      struct encoding *e;
      unsigned int hash;

      utf8_strings = recode_strings (pool, strings, ids, n_strings,
                                     encoding_names[i]);
      if (!utf8_strings)
        continue;

      /* Hash utf8_strings. */
      hash = 0;
      for (j = 0; j < n_strings; j++)
        hash = hash_string (utf8_strings[j], hash);

      /* If there's a duplicate encoding, just mark it. */
      e = find_duplicate_encoding (encodings, n_encodings,
                                   utf8_strings, n_strings, hash);
      if (e)
        {
          e->encodings |= UINT64_C (1) << i;
          continue;
        }

      e = &encodings[n_encodings++];
      e->encodings = UINT64_C (1) << i;
      e->utf8_strings = utf8_strings;
      e->hash = hash;
    }
  if (!n_encodings)
    {
      msg (SW, _("No valid encodings found."));
      pool_destroy (pool);
      return;
    }

  t = tab_create (2, n_encodings + 1);
  tab_title (t, _("Usable encodings for %s."), fh_get_name (h));
  tab_caption (t, _("Encodings that can successfully read %s (by specifying "
                    "the encoding name on the GET command's ENCODING "
                    "subcommand).  Encodings that yield identical text are "
                    "listed together."), fh_get_name (h));
  tab_headers (t, 1, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, -1, -1, 0, 0, 1, n_encodings);
  tab_hline (t, TAL_1, 0, 1, 1);
  tab_text (t, 0, 0, TAB_RIGHT, "#");
  tab_text (t, 1, 0, TAB_LEFT, _("Encodings"));
  for (i = 0; i < n_encodings; i++)
    {
      struct string s;

      ds_init_empty (&s);
      for (j = 0; j < 64; j++)
        if (encodings[i].encodings & (UINT64_C (1) << j))
          ds_put_format (&s, "%s, ", encoding_names[j]);
      ds_chomp (&s, ss_cstr (", "));

      tab_text_format (t, 0, i + 1, TAB_RIGHT, "%zu", i + 1);
      tab_text (t, 1, i + 1, TAB_LEFT, ds_cstr (&s));
      ds_destroy (&s);
    }
  tab_submit (t);

  n_unique_strings = 0;
  for (i = 0; i < n_strings; i++)
    if (!all_equal (encodings, n_encodings, i))
      n_unique_strings++;
  if (!n_unique_strings)
    {
      pool_destroy (pool);
      return;
    }

  t = tab_create (3, (n_encodings * n_unique_strings) + 1);
  tab_title (t, _("%s encoded text strings."), fh_get_name (h));
  tab_caption (t, _("Text strings in the file dictionary that the previously "
                    "listed encodings interpret differently, along with the "
                    "interpretations."));
  tab_headers (t, 1, 0, 1, 0);
  tab_box (t, TAL_1, TAL_1, -1, -1, 0, 0, 2, n_encodings * n_unique_strings);
  tab_hline (t, TAL_1, 0, 2, 1);

  tab_text (t, 0, 0, TAB_LEFT, _("Purpose"));
  tab_text (t, 1, 0, TAB_RIGHT, "#");
  tab_text (t, 2, 0, TAB_LEFT, _("Text"));

  row = 1;
  for (i = 0; i < n_strings; i++)
    if (!all_equal (encodings, n_encodings, i))
      {
        int prefix = equal_prefix (encodings, n_encodings, i);
        int suffix = equal_suffix (encodings, n_encodings, i);

        tab_joint_text (t, 0, row, 0, row + n_encodings - 1,
                        TAB_LEFT, titles[i]);
        tab_hline (t, TAL_1, 0, 2, row);
        for (j = 0; j < n_encodings; j++)
          {
            const char *s = encodings[j].utf8_strings[i] + prefix;

            tab_text_format (t, 1, row, TAB_RIGHT, "%zu", j + 1);
            if (prefix || suffix)
              {
                size_t len = strlen (s) - suffix;
                struct string entry;

                ds_init_empty (&entry);
                if (prefix)
                  ds_put_cstr (&entry, "...");
                ds_put_substring (&entry, ss_buffer (s, len));
                if (suffix)
                  ds_put_cstr (&entry, "...");
                tab_text (t, 2, row, TAB_LEFT, ds_cstr (&entry));
              }
            else
              tab_text (t, 2, row, TAB_LEFT, s);
            row++;
          }
      }
  tab_submit (t);

  pool_destroy (pool);
}

static unsigned int
dict_display_mask (const struct dictionary *d)
{
  size_t n_vars = dict_get_var_cnt (d);
  unsigned int mask;
  size_t i;

  mask = DF_ALL & ~(DF_MEASURE | DF_ROLE | DF_ALIGNMENT | DF_WIDTH);
  for (i = 0; i < n_vars; i++)
    {
      const struct variable *v = dict_get_var (d, i);
      enum val_type val = var_get_type (v);
      int width = var_get_width (v);

      if (var_get_measure (v) != var_default_measure (val))
        mask |= DF_MEASURE;

      if (var_get_role (v) != ROLE_INPUT)
        mask |= DF_ROLE;

      if (var_get_alignment (v) != var_default_alignment (val))
        mask |= DF_ALIGNMENT;

      if (var_get_display_width (v) != var_default_display_width (width))
        mask |= DF_WIDTH;
    }

  return mask;
}
