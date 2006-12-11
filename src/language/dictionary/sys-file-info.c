/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
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

#include <ctype.h>
#include <stdlib.h>

#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/format.h>
#include <data/missing-values.h>
#include <data/procedure.h>
#include <data/sys-file-reader.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <data/vector.h>
#include <language/command.h>
#include <language/data-io/file-handle.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/array.h>
#include <libpspp/hash.h>
#include <libpspp/magic.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <output/manager.h>
#include <output/output.h>
#include <output/table.h>

#include "minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Constants for DISPLAY utility. */
enum
  {
    AS_NAMES = 0,
    AS_INDEX,
    AS_VARIABLES,
    AS_LABELS,
    AS_DICTIONARY,
    AS_SCRATCH,
    AS_VECTOR
  };

static int describe_variable (struct variable *v, struct tab_table *t, int r, int as);
     
/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
sysfile_info_dim (struct tab_table *t, struct outp_driver *d)
{
  static const int max[] = {20, 5, 35, 3, 0};
  const int *p;
  int i;

  for (p = max; *p; p++)
    t->w[p - max] = MIN (tab_natural_width (t, d, p - max),
			 *p * d->prop_em_width);
  for (i = 0; i < t->nr; i++)
    t->h[i] = tab_natural_height (t, d, i);
}

/* SYSFILE INFO utility. */
int
cmd_sysfile_info (struct lexer *lexer, struct dataset *ds UNUSED)
{
  struct file_handle *h;
  struct dictionary *d;
  struct tab_table *t;
  struct sfm_reader *reader;
  struct sfm_read_info info;
  int r, nr;
  int i;

  lex_match_id (lexer, "FILE");
  lex_match (lexer, '=');

  h = fh_parse (lexer, FH_REF_FILE);
  if (!h)
    return CMD_FAILURE;

  reader = sfm_open_reader (h, &d, &info);
  if (!reader)
    return CMD_FAILURE;
  sfm_close_reader (reader);

  t = tab_create (2, 10, 0);
  tab_vline (t, TAL_GAP, 1, 0, 8);
  tab_text (t, 0, 0, TAB_LEFT, _("File:"));
  tab_text (t, 1, 0, TAB_LEFT, fh_get_file_name (h));
  tab_text (t, 0, 1, TAB_LEFT, _("Label:"));
  {
    const char *label = dict_get_label (d);
    if (label == NULL)
      label = _("No label.");
    tab_text (t, 1, 1, TAB_LEFT, label);
  }
  tab_text (t, 0, 2, TAB_LEFT, _("Created:"));
  tab_text (t, 1, 2, TAB_LEFT | TAT_PRINTF, "%s %s by %s",
		info.creation_date, info.creation_time, info.product);
  tab_text (t, 0, 3, TAB_LEFT, _("Integer Format:"));
  tab_text (t, 1, 3, TAB_LEFT,
            info.integer_format == INTEGER_MSB_FIRST ? _("Big Endian.")
            : info.integer_format == INTEGER_LSB_FIRST ? _("Little Endian.")
            : _("Unknown."));
  tab_text (t, 0, 4, TAB_LEFT, _("Real Format:"));
  tab_text (t, 1, 4, TAB_LEFT,
            info.float_format == FLOAT_IEEE_DOUBLE_LE ? _("IEEE 754 LE.")
            : info.float_format == FLOAT_IEEE_DOUBLE_BE ? _("IEEE 754 BE.")
            : info.float_format == FLOAT_VAX_D ? _("VAX D.")
            : info.float_format == FLOAT_VAX_G ? _("VAX G.")
            : info.float_format == FLOAT_Z_LONG ? _("IBM 390 Hex Long.")
            : _("Unknown."));
  tab_text (t, 0, 5, TAB_LEFT, _("Variables:"));
  tab_text (t, 1, 5, TAB_LEFT | TAT_PRINTF, "%d",
		dict_get_var_cnt (d));
  tab_text (t, 0, 6, TAB_LEFT, _("Cases:"));
  tab_text (t, 1, 6, TAB_LEFT | TAT_PRINTF,
		info.case_cnt == -1 ? _("Unknown") : "%d", info.case_cnt);
  tab_text (t, 0, 7, TAB_LEFT, _("Type:"));
  tab_text (t, 1, 7, TAB_LEFT, _("System File."));
  tab_text (t, 0, 8, TAB_LEFT, _("Weight:"));
  {
    struct variable *weight_var = dict_get_weight (d);
    tab_text (t, 1, 8, TAB_LEFT,
              (weight_var != NULL
               ? var_get_name (weight_var) : _("Not weighted."))); 
  }
  tab_text (t, 0, 9, TAB_LEFT, _("Mode:"));
  tab_text (t, 1, 9, TAB_LEFT | TAT_PRINTF,
		_("Compression %s."), info.compressed ? _("on") : _("off"));
  tab_dim (t, tab_natural_dimensions);
  tab_submit (t);

  nr = 1 + 2 * dict_get_var_cnt (d);

  t = tab_create (4, nr, 1);
  tab_dim (t, sysfile_info_dim);
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Variable"));
  tab_joint_text (t, 1, 0, 2, 0, TAB_LEFT | TAT_TITLE, _("Description"));
  tab_text (t, 3, 0, TAB_LEFT | TAT_TITLE, _("Position"));
  tab_hline (t, TAL_2, 0, 3, 1);
  for (r = 1, i = 0; i < dict_get_var_cnt (d); i++)
    {
      struct variable *v = dict_get_var (d, i);
      const int nvl = val_labs_count (var_get_value_labels (v));
      
      if (r + 10 + nvl > nr)
	{
	  nr = MAX (nr * dict_get_var_cnt (d) / (i + 1), nr);
	  nr += 10 + nvl;
	  tab_realloc (t, 4, nr);
	}

      r = describe_variable (v, t, r, AS_DICTIONARY);
    }

  tab_box (t, TAL_1, TAL_1, -1, -1, 0, 0, 3, r);
  tab_vline (t, TAL_1, 1, 0, r);
  tab_vline (t, TAL_1, 3, 0, r);

  tab_resize (t, -1, r);
  tab_flags (t, SOMF_NO_TITLE);
  tab_submit (t);

  dict_destroy (d);
  
  return lex_end_of_command (lexer);
}

/* DISPLAY utility. */

static void display_macros (void);
static void display_documents (const struct dictionary *dict);
static void display_variables (struct variable **, size_t, int);
static void display_vectors (const struct dictionary *dict, int sorted);

int
cmd_display (struct lexer *lexer, struct dataset *ds)
{
  /* Whether to sort the list of variables alphabetically. */
  int sorted;

  /* Variables to display. */
  size_t n;
  struct variable **vl;

  if (lex_match_id (lexer, "MACROS"))
    display_macros ();
  else if (lex_match_id (lexer, "DOCUMENTS"))
    display_documents (dataset_dict (ds));
  else if (lex_match_id (lexer, "FILE"))
    {
      som_blank_line ();
      if (!lex_force_match_id (lexer, "LABEL"))
	return CMD_FAILURE;
      if (dict_get_label (dataset_dict (ds)) == NULL)
	tab_output_text (TAB_LEFT,
			 _("The active file does not have a file label."));
      else
	{
	  tab_output_text (TAB_LEFT | TAT_TITLE, _("File label:"));
	  tab_output_text (TAB_LEFT | TAB_FIX, dict_get_label (dataset_dict (ds)));
	}
    }
  else
    {
      static const char *sbc[] =
	{"NAMES", "INDEX", "VARIABLES", "LABELS",
	 "DICTIONARY", "SCRATCH", "VECTORS", NULL};
      const char **cp;
      int as;

      sorted = lex_match_id (lexer, "SORTED");

      for (cp = sbc; *cp; cp++)
	if (lex_token (lexer) == T_ID
            && lex_id_match (ss_cstr (*cp), ss_cstr (lex_tokid (lexer))))
	  {
	    lex_get (lexer);
	    break;
	  }
      as = cp - sbc;

      if (*cp == NULL)
	as = AS_NAMES;

      if (as == AS_VECTOR)
	{
	  display_vectors (dataset_dict(ds), sorted);
	  return CMD_SUCCESS;
	}

      lex_match (lexer, '/');
      lex_match_id (lexer, "VARIABLES");
      lex_match (lexer, '=');

      if (lex_token (lexer) != '.')
	{
	  if (!parse_variables (lexer, dataset_dict (ds), &vl, &n, PV_NONE))
	    {
	      free (vl);
	      return CMD_FAILURE;
	    }
	  as = AS_DICTIONARY;
	}
      else
	dict_get_vars (dataset_dict (ds), &vl, &n, 0);

      if (as == AS_SCRATCH)
	{
	  size_t i, m;
	  for (i = 0, m = n; i < n; i++)
	    if (dict_class_from_id (var_get_name (vl[i])) != DC_SCRATCH)
	      {
		vl[i] = NULL;
		m--;
	      }
	  as = AS_NAMES;
	  n = m;
	}

      if (n == 0)
	{
	  msg (SW, _("No variables to display."));
	  return CMD_FAILURE;
	}

      if (sorted)
	sort (vl, n, sizeof *vl, compare_var_ptrs_by_name, NULL);

      display_variables (vl, n, as);

      free (vl);
    }

  return lex_end_of_command (lexer);
}

static void
display_macros (void)
{
  som_blank_line ();
  tab_output_text (TAB_LEFT, _("Macros not supported."));
}

static void
display_documents (const struct dictionary *dict)
{
  const char *documents = dict_get_documents (dict);

  som_blank_line ();
  if (documents == NULL)
    tab_output_text (TAB_LEFT, _("The active file dictionary does not "
                                 "contain any documents."));
  else
    {
      size_t n_lines = strlen (documents) / 80;
      char buf[81];
      size_t i;

      tab_output_text (TAB_LEFT | TAT_TITLE,
		       _("Documents in the active file:"));
      som_blank_line ();
      buf[80] = 0;
      for (i = 0; i < n_lines; i++)
	{
	  int len = 79;

	  memcpy (buf, &documents[i * 80], 80);
	  while ((isspace ((unsigned char) buf[len]) || buf[len] == 0)
		 && len > 0)
	    len--;
	  buf[len + 1] = 0;
	  tab_output_text (TAB_LEFT | TAB_FIX | TAT_NOWRAP, buf);
	}
    }
}

static int _as;

/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
variables_dim (struct tab_table *t, struct outp_driver *d)
{
  int pc;
  int i;
  
  t->w[0] = tab_natural_width (t, d, 0);
  if (_as == AS_DICTIONARY || _as == AS_VARIABLES || _as == AS_LABELS)
    {
      t->w[1] = MAX (tab_natural_width (t, d, 1), d->prop_em_width * 5);
      t->w[2] = MAX (tab_natural_width (t, d, 2), d->prop_em_width * 35);
      pc = 3;
    }
  else pc = 1;
  if (_as != AS_NAMES)
    t->w[pc] = tab_natural_width (t, d, pc);

  for (i = 0; i < t->nr; i++)
    t->h[i] = tab_natural_height (t, d, i);
}
  
static void
display_variables (struct variable **vl, size_t n, int as)
{
  struct variable **vp = vl;		/* Variable pointer. */
  struct tab_table *t;
  int nc;			/* Number of columns. */
  int nr;			/* Number of rows. */
  int pc;			/* `Position column' */
  int r;			/* Current row. */
  size_t i;

  _as = as;
  switch (as)
    {
    case AS_INDEX:
      nc = 2;
      break;
    case AS_NAMES:
      nc = 1;
      break;
    default:
      nc = 4;
      break;
    }

  t = tab_create (nc, n + 5, 1);
  tab_headers (t, 0, 0, 1, 0);
  nr = n + 5;
  tab_hline (t, TAL_2, 0, nc - 1, 1);
  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Variable"));
  pc = (as == AS_INDEX ? 1 : 3);
  if (as != AS_NAMES)
    tab_text (t, pc, 0, TAB_LEFT | TAT_TITLE, _("Position"));
  if (as == AS_DICTIONARY || as == AS_VARIABLES)
    tab_joint_text (t, 1, 0, 2, 0, TAB_LEFT | TAT_TITLE, _("Description"));
  else if (as == AS_LABELS)
    tab_joint_text (t, 1, 0, 2, 0, TAB_LEFT | TAT_TITLE, _("Label"));
  tab_dim (t, variables_dim);
    
  for (i = r = 1; i <= n; i++)
    {
      struct variable *v;

      while (*vp == NULL)
	vp++;
      v = *vp++;

      if (as == AS_DICTIONARY || as == AS_VARIABLES)
	{
	  int nvl = val_labs_count (var_get_value_labels (v));
      
	  if (r + 10 + nvl > nr)
	    {
	      nr = MAX (nr * n / (i + 1), nr);
	      nr += 10 + nvl;
	      tab_realloc (t, nc, nr);
	    }

	  r = describe_variable (v, t, r, as);
	} else {
	  tab_text (t, 0, r, TAB_LEFT, var_get_name (v));
	  if (as == AS_LABELS) 
            {
              const char *label = var_get_label (v);
              tab_joint_text (t, 1, r, 2, r, TAB_LEFT,
                              label != NULL ? "(no label)" : label); 
            }
	  if (as != AS_NAMES)
	    {
	      tab_text (t, pc, r, TAT_PRINTF, "%d",
                        (int) var_get_dict_index (v) + 1);
	      tab_hline (t, TAL_1, 0, nc - 1, r);
	    }
	  r++;
	}
    }
  tab_hline (t, as == AS_NAMES ? TAL_1 : TAL_2, 0, nc - 1, 1);
  if (as != AS_NAMES)
    {
      tab_box (t, TAL_1, TAL_1, -1, -1, 0, 0, nc - 1, r - 1);
      tab_vline (t, TAL_1, 1, 0, r - 1);
    }
  else
    tab_flags (t, SOMF_NO_TITLE);
  if (as == AS_DICTIONARY || as == AS_VARIABLES || as == AS_LABELS)
    tab_vline (t, TAL_1, 3, 0, r - 1);
  tab_resize (t, -1, r);
  tab_columns (t, TAB_COL_DOWN, 1);
  tab_submit (t);
}

/* Puts a description of variable V into table T starting at row R.
   The variable will be described in the format AS.  Returns the next
   row available for use in the table. */
static int 
describe_variable (struct variable *v, struct tab_table *t, int r, int as)
{
  const struct fmt_spec *print = var_get_print_format (v);
  const struct fmt_spec *write = var_get_write_format (v);

  /* Put the name, var label, and position into the first row. */
  tab_text (t, 0, r, TAB_LEFT, var_get_name (v));
  tab_text (t, 3, r, TAT_PRINTF, "%d", (int) var_get_dict_index (v) + 1);

  if (as == AS_DICTIONARY && var_has_label (v)) 
    {
      tab_joint_text (t, 1, r, 2, r, TAB_LEFT, var_get_label (v));
      r++;
    }
  
  /* Print/write format, or print and write formats. */
  if (fmt_equal (print, write))
    {
      char str[FMT_STRING_LEN_MAX + 1];
      tab_joint_text (t, 1, r, 2, r, TAB_LEFT | TAT_PRINTF, _("Format: %s"),
		      fmt_to_string (print, str));
      r++;
    }
  else
    {
      char str[FMT_STRING_LEN_MAX + 1];
      tab_joint_text (t, 1, r, 2, r, TAB_LEFT | TAT_PRINTF,
		      _("Print Format: %s"), fmt_to_string (print, str));
      r++;
      tab_joint_text (t, 1, r, 2, r, TAB_LEFT | TAT_PRINTF,
		      _("Write Format: %s"), fmt_to_string (write, str));
      r++;
    }

  /* Missing values if any. */
  if (var_has_missing_values (v))
    {
      char buf[128];
      char *cp;
      struct missing_values mv;
      int cnt = 0;
      
      cp = stpcpy (buf, _("Missing Values: "));
      
      mv_copy (&mv, var_get_missing_values (v));
      if (mv_has_range (&mv)) 
        {
          double x, y;
          mv_pop_range (&mv, &x, &y);
          if (x == LOWEST)
            cp += sprintf (cp, "LOWEST THRU %g", y);
          else if (y == HIGHEST)
            cp += sprintf (cp, "%g THRU HIGHEST", x);
          else
            cp += sprintf (cp, "%g THRU %g", x, y);
          cnt++;
        }
      while (mv_has_value (&mv)) 
        {
          union value value;
          mv_pop_value (&mv, &value);
          if (cnt++ > 0)
            cp += sprintf (cp, "; ");
          if (var_is_numeric (v))
            cp += sprintf (cp, "%g", value.f);
          else 
            {
              *cp++ = '"';
	      memcpy (cp, value.s, var_get_width (v));
	      cp += var_get_width (v);
	      *cp++ = '"';
              *cp = '\0';
            }
        }

      tab_joint_text (t, 1, r, 2, r, TAB_LEFT, buf);
      r++;
    }

  /* Value labels. */
  if (as == AS_DICTIONARY && var_has_value_labels (v))
    {
      const struct val_labs *val_labs = var_get_value_labels (v);
      struct val_labs_iterator *i;
      struct val_lab *vl;
      int orig_r = r;

#if 0
      tab_text (t, 1, r, TAB_LEFT, _("Value"));
      tab_text (t, 2, r, TAB_LEFT, _("Label"));
      r++;
#endif

      tab_hline (t, TAL_1, 1, 2, r);
      for (vl = val_labs_first_sorted (val_labs, &i); vl != NULL;
           vl = val_labs_next (val_labs, &i))
        {
	  char buf[128];

	  if (var_is_alpha (v))
	    {
	      memcpy (buf, vl->value.s, var_get_width (v));
	      buf[var_get_width (v)] = 0;
	    }
	  else
	    sprintf (buf, "%g", vl->value.f);

	  tab_text (t, 1, r, TAB_NONE, buf);
	  tab_text (t, 2, r, TAB_LEFT, vl->label);
	  r++;
	}

      tab_vline (t, TAL_1, 2, orig_r, r - 1);
    }

  /* Draw a line below the last row of information on this variable. */
  tab_hline (t, TAL_1, 0, 3, r);

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
  
  nvec = dict_get_vector_cnt (dict);
  if (nvec == 0)
    {
      msg (SW, _("No vectors defined."));
      return;
    }

  vl = xnmalloc (nvec, sizeof *vl);
  for (i = 0; i < nvec; i++)
    vl[i] = dict_get_vector (dict, i);
  if (sorted)
    qsort (vl, nvec, sizeof *vl, compare_vector_ptrs_by_name);

  t = tab_create (1, nvec + 1, 0);
  tab_headers (t, 0, 0, 1, 0);
  tab_columns (t, TAB_COL_DOWN, 1);
  tab_dim (t, tab_natural_dimensions);
  tab_hline (t, TAL_1, 0, 0, 1);
  tab_text (t, 0, 0, TAT_TITLE | TAB_LEFT, _("Vector"));
  tab_flags (t, SOMF_NO_TITLE);
  for (i = 0; i < nvec; i++)
    tab_text (t, 0, i + 1, TAB_LEFT, vector_get_name (vl[i]));
  tab_submit (t);

  free (vl);
}











