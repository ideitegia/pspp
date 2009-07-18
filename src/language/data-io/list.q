/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009 Free Software Foundation, Inc.

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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "intprops.h"
#include "xmalloca.h"

#include <data/casegrouper.h>
#include <data/casereader.h>
#include <data/dictionary.h>
#include <data/data-out.h>
#include <data/format.h>
#include <data/procedure.h>
#include <data/short-names.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/dictionary/split-file.h>
#include <language/lexer/lexer.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <output/htmlP.h>
#include <output/manager.h>
#include <output/output.h>
#include <output/table.h>

#include "minmax.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* (headers) */

/* (specification)
   list (lst_):
     *variables=varlist("PV_NO_SCRATCH");
     cases=:from n:first,"%s>0"/by n:step,"%s>0"/ *to n:last,"%s>0";
     +format=numbering:numbered/!unnumbered,
             wrap:!wrap/single,
             weight:weight/!noweight.
*/
/* (declarations) */
/* (functions) */

/* Layout for one output driver. */
struct list_ext
  {
    int type;		/* 0=Values and labels fit across the page. */
    size_t n_vertical;	/* Number of labels to list vertically. */
    size_t header_rows;	/* Number of header rows. */
    char **header;	/* The header itself. */
  };

/* Parsed command. */
static struct cmd_list cmd;

/* Line buffer. */
static struct string line_buffer;

/* TTY-style output functions. */
static unsigned n_lines_remaining (struct outp_driver *d);
static unsigned n_chars_width (struct outp_driver *d);
static void write_line (struct outp_driver *d, const char *s);

/* Other functions. */
static void list_case (const struct ccase *, casenumber case_idx,
                       const struct dataset *);
static void determine_layout (void);
static void clean_up (void);
static void write_header (struct outp_driver *);
static void write_all_headers (struct casereader *, const struct dataset*);

/* Returns the number of text lines that can fit on the remainder of
   the page. */
static inline unsigned
n_lines_remaining (struct outp_driver *d)
{
  int diff;

  diff = d->length - d->cp_y;
  return (diff > 0) ? (diff / d->font_height) : 0;
}

/* Returns the number of fixed-width character that can fit across the
   page. */
static inline unsigned
n_chars_width (struct outp_driver *d)
{
  return d->width / d->fixed_width;
}

/* Writes the line S at the current position and advances to the next
   line.  */
static void
write_line (struct outp_driver *d, const char *s)
{
  struct outp_text text;

  assert (d->cp_y + d->font_height <= d->length);
  text.font = OUTP_FIXED;
  text.justification = OUTP_LEFT;
  text.string = ss_cstr (s);
  text.x = d->cp_x;
  text.y = d->cp_y;
  text.h = text.v = INT_MAX;
  d->class->text_draw (d, &text);
  d->cp_x = 0;
  d->cp_y += d->font_height;
}

/* Parses and executes the LIST procedure. */
int
cmd_list (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct variable *casenum_var = NULL;
  struct casegrouper *grouper;
  struct casereader *group;
  casenumber case_idx;
  bool ok;

  if (!parse_list (lexer, ds, &cmd, NULL))
    return CMD_FAILURE;

  /* Fill in defaults. */
  if (cmd.step == LONG_MIN)
    cmd.step = 1;
  if (cmd.first == LONG_MIN)
    cmd.first = 1;
  if (cmd.last == LONG_MIN)
    cmd.last = LONG_MAX;
  if (!cmd.sbc_variables)
    dict_get_vars (dict, &cmd.v_variables, &cmd.n_variables,
                   DC_SYSTEM | DC_SCRATCH);
  if (cmd.n_variables == 0)
    {
      msg (SE, _("No variables specified."));
      return CMD_FAILURE;
    }

  /* Verify arguments. */
  if (cmd.first > cmd.last)
    {
      int t;
      msg (SW, _("The first case (%ld) specified precedes the last case (%ld) "
	   "specified.  The values will be swapped."), cmd.first, cmd.last);
      t = cmd.first;
      cmd.first = cmd.last;
      cmd.last = t;
    }
  if (cmd.first < 1)
    {
      msg (SW, _("The first case (%ld) to list is less than 1.  The value is "
	   "being reset to 1."), cmd.first);
      cmd.first = 1;
    }
  if (cmd.last < 1)
    {
      msg (SW, _("The last case (%ld) to list is less than 1.  The value is "
	   "being reset to 1."), cmd.last);
      cmd.last = 1;
    }
  if (cmd.step < 1)
    {
      msg (SW, _("The step value %ld is less than 1.  The value is being "
	   "reset to 1."), cmd.step);
      cmd.step = 1;
    }

  /* Weighting variable. */
  if (cmd.weight == LST_WEIGHT)
    {
      if (dict_get_weight (dict) != NULL)
	{
	  size_t i;

	  for (i = 0; i < cmd.n_variables; i++)
	    if (cmd.v_variables[i] == dict_get_weight (dict))
	      break;
	  if (i >= cmd.n_variables)
	    {
	      /* Add the weight variable to the end of the variable list. */
	      cmd.n_variables++;
	      cmd.v_variables = xnrealloc (cmd.v_variables, cmd.n_variables,
                                           sizeof *cmd.v_variables);
	      cmd.v_variables[cmd.n_variables - 1]
                = dict_get_weight (dict);
	    }
	}
      else
	msg (SW, _("`/FORMAT WEIGHT' specified, but weighting is not on."));
    }

  /* Case number. */
  if (cmd.numbering == LST_NUMBERED)
    {
      /* Initialize the case-number variable. */
      int width = cmd.last == LONG_MAX ? 5 : intlog10 (cmd.last);
      struct fmt_spec format = fmt_for_output (FMT_F, width, 0);
      casenum_var = var_create ("Case#", 0);
      var_set_both_formats (casenum_var, &format);

      /* Add the weight variable at the beginning of the variable list. */
      cmd.n_variables++;
      cmd.v_variables = xnrealloc (cmd.v_variables,
                                   cmd.n_variables, sizeof *cmd.v_variables);
      memmove (&cmd.v_variables[1], &cmd.v_variables[0],
	       (cmd.n_variables - 1) * sizeof *cmd.v_variables);
      cmd.v_variables[0] = casenum_var;
    }

  determine_layout ();

  case_idx = 0;
  for (grouper = casegrouper_create_splits (proc_open (ds), dict);
       casegrouper_get_next_group (grouper, &group);
       casereader_destroy (group))
    {
      struct ccase *c;

      write_all_headers (group, ds);
      for (; (c = casereader_read (group)) != NULL; case_unref (c))
        {
          case_idx++;
          if (case_idx >= cmd.first && case_idx <= cmd.last
              && (case_idx - cmd.first) % cmd.step == 0)
            list_case (c, case_idx, ds);
        }
    }
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  ds_destroy(&line_buffer);

  clean_up ();

  var_destroy (casenum_var);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

/* Writes headers to all devices.  This is done at the beginning of
   each SPLIT FILE group. */
static void
write_all_headers (struct casereader *input, const struct dataset *ds)
{
  struct outp_driver *d;
  struct ccase *c;

  c = casereader_peek (input, 0);
  if (c == NULL)
    return;
  output_split_file_values (ds, c);
  case_unref (c);

  for (d = outp_drivers (NULL); d; d = outp_drivers (d))
    {
      if (!d->class->special)
	{
	  d->cp_y += d->font_height;		/* Blank line. */
	  write_header (d);
	}
      else if (d->class == &html_class)
	{
	  struct html_driver_ext *x = d->ext;

	  fputs ("<TABLE BORDER=1>\n  <TR>\n", x->file);

	  {
	    size_t i;

	    for (i = 0; i < cmd.n_variables; i++)
	      fprintf (x->file, "    <TH><EM>%s</EM></TH>\n",
		       var_get_name (cmd.v_variables[i]));
	  }

	  fputs ("  </TR>\n", x->file);
	}
      else
	NOT_REACHED ();
    }
}

/* Writes the headers.  Some of them might be vertical; most are
   probably horizontal. */
static void
write_header (struct outp_driver *d)
{
  struct list_ext *prc = d->prc;

  if (!prc->header_rows)
    return;

  if (n_lines_remaining (d) < prc->header_rows + 1)
    {
      outp_eject_page (d);
      assert (n_lines_remaining (d) >= prc->header_rows + 1);
    }

  /* Design the header. */
  if (!prc->header)
    {
      size_t i;
      size_t x;

      /* Allocate, initialize header. */
      prc->header = xnmalloc (prc->header_rows, sizeof *prc->header);
      {
	int w = n_chars_width (d);
	for (i = 0; i < prc->header_rows; i++)
	  {
	    prc->header[i] = xmalloc (w + 1);
	    memset (prc->header[i], ' ', w);
	  }
      }

      /* Put in vertical names. */
      for (i = x = 0; i < prc->n_vertical; i++)
	{
	  const struct variable *v = cmd.v_variables[i];
          const char *name = var_get_name (v);
          size_t name_len = strlen (name);
          const struct fmt_spec *print = var_get_print_format (v);
	  size_t j;

	  memset (&prc->header[prc->header_rows - 1][x], '-', print->w);
	  x += print->w - 1;
	  for (j = 0; j < name_len; j++)
	    prc->header[name_len - j - 1][x] = name[j];
	  x += 2;
	}

      /* Put in horizontal names. */
      for (; i < cmd.n_variables; i++)
	{
	  const struct variable *v = cmd.v_variables[i];
          const char *name = var_get_name (v);
          size_t name_len = strlen (name);
          const struct fmt_spec *print = var_get_print_format (v);

	  memset (&prc->header[prc->header_rows - 1][x], '-',
		  MAX (print->w, (int) name_len));
	  if ((int) name_len < print->w)
	    x += print->w - name_len;
 	  memcpy (&prc->header[0][x], name, name_len);
	  x += name_len + 1;
	}

      /* Add null bytes. */
      for (i = 0; i < prc->header_rows; i++)
	{
	  for (x = n_chars_width (d); x >= 1; x--)
	    if (prc->header[i][x - 1] != ' ')
	      {
		prc->header[i][x] = 0;
		break;
	      }
	  assert (x);
	}
    }

  /* Write out the header, in back-to-front order except for the last line. */
  if (prc->header_rows >= 2)
    {
      size_t i;

      for (i = prc->header_rows - 1; i-- != 0; )
        write_line (d, prc->header[i]);
    }
  write_line (d, prc->header[prc->header_rows - 1]);
}


/* Frees up all the memory we've allocated. */
static void
clean_up (void)
{
  struct outp_driver *d;

  for (d = outp_drivers (NULL); d; d = outp_drivers (d))
    if (d->class->special == 0)
      {
	struct list_ext *prc = d->prc;
	size_t i;

	if (prc->header)
	  {
	    for (i = 0; i < prc->header_rows; i++)
	      free (prc->header[i]);
	    free (prc->header);
	  }
	free (prc);
      }
    else if (d->class == &html_class)
      {
	if (d->page_open)
	  {
	    struct html_driver_ext *x = d->ext;

	    fputs ("</TABLE>\n", x->file);
	  }
      }
    else
      NOT_REACHED ();

  free (cmd.v_variables);
}

/* Writes string STRING at the current position.  If the text would
   fall off the side of the page, then advance to the next line,
   indenting by amount INDENT. */
static void
write_varname (struct outp_driver *d, char *string, int indent)
{
  struct outp_text text;
  int width;

  if (d->cp_x + outp_string_width (d, string, OUTP_FIXED) > d->width)
    {
      d->cp_y += d->font_height;
      if (d->cp_y + d->font_height > d->length)
	outp_eject_page (d);
      d->cp_x = indent;
    }

  text.font = OUTP_FIXED;
  text.justification = OUTP_LEFT;
  text.string = ss_cstr (string);
  text.x = d->cp_x;
  text.y = d->cp_y;
  text.h = text.v = INT_MAX;
  d->class->text_draw (d, &text);
  d->class->text_metrics (d, &text, &width, NULL);
  d->cp_x += width;
}

/* When we can't fit all the values across the page, we write out all
   the variable names just once.  This is where we do it. */
static void
write_fallback_headers (struct outp_driver *d)
{
  const int max_width = n_chars_width(d) - 10;

  int index = 0;
  int width = 0;
  int line_number = 0;

  const char *Line = _("Line");
  char *leader = xmalloca (strlen (Line)
                           + INT_STRLEN_BOUND (line_number) + 1 + 1);

  while (index < cmd.n_variables)
    {
      struct outp_text text;
      int leader_width;

      /* Ensure that there is enough room for a line of text. */
      if (d->cp_y + d->font_height > d->length)
	outp_eject_page (d);

      /* The leader is a string like `Line 1: '.  Write the leader. */
      sprintf (leader, "%s %d:", Line, ++line_number);
      text.font = OUTP_FIXED;
      text.justification = OUTP_LEFT;
      text.string = ss_cstr (leader);
      text.x = 0;
      text.y = d->cp_y;
      text.h = text.v = INT_MAX;
      d->class->text_draw (d, &text);
      d->class->text_metrics (d, &text, &leader_width, NULL);
      d->cp_x = leader_width;

      goto entry;
      do
	{
	  width++;

	entry:
	  {
	    int var_width = var_get_print_format (cmd.v_variables[index])->w;
	    if (width + var_width > max_width && width != 0)
	      {
		width = 0;
		d->cp_x = 0;
		d->cp_y += d->font_height;
		break;
	      }
	    width += var_width;
	  }

	  {
	    char varname[VAR_NAME_LEN + 2];
	    snprintf (varname, sizeof varname,
                      " %s", var_get_name (cmd.v_variables[index]));
	    write_varname (d, varname, leader_width);
	  }
	}
      while (++index < cmd.n_variables);

    }
  d->cp_x = 0;
  d->cp_y += d->font_height;

  freea (leader);
}

/* There are three possible layouts for the LIST procedure:

   1. If the values and their variables' name fit across the page,
   then they are listed across the page in that way.

   2. If the values can fit across the page, but not the variable
   names, then as many variable names as necessary are printed
   vertically to compensate.

   3. If not even the values can fit across the page, the variable
   names are listed just once, at the beginning, in a compact format,
   and the values are listed with a variable name label at the
   beginning of each line for easier reference.

   This is complicated by the fact that we have to do all this for
   every output driver, not just once.  */
static void
determine_layout (void)
{
  struct outp_driver *d;

  /* This is the largest page width of any driver, so we can tell what
     size buffer to allocate. */
  int largest_page_width = 0;

  for (d = outp_drivers (NULL); d; d = outp_drivers (d))
    {
      size_t column;	/* Current column. */
      int width;	/* Accumulated width. */
      int height;       /* Height of vertical names. */
      int max_width;	/* Page width. */

      struct list_ext *prc;

      if (d->class == &html_class)
	continue;

      assert (d->class->special == 0);

      outp_open_page (d);

      max_width = n_chars_width (d);
      largest_page_width = MAX (largest_page_width, max_width);

      prc = d->prc = xmalloc (sizeof *prc);
      prc->type = 0;
      prc->n_vertical = 0;
      prc->header = NULL;

      /* Try layout #1. */
      for (width = cmd.n_variables - 1, column = 0; column < cmd.n_variables; column++)
	{
	  const struct variable *v = cmd.v_variables[column];
          int fmt_width = var_get_print_format (v)->w;
          int name_len = strlen (var_get_name (v));
	  width += MAX (fmt_width, name_len);
	}
      if (width <= max_width)
	{
	  prc->header_rows = 2;
	  continue;
	}

      /* Try layout #2. */
      for (width = cmd.n_variables - 1, height = 0, column = 0;
	   column < cmd.n_variables && width <= max_width;
	   column++)
        {
          const struct variable *v = cmd.v_variables[column];
          int fmt_width = var_get_print_format (v)->w;
          size_t name_len = strlen (var_get_name (v));
          width += fmt_width;
          if (name_len > height)
            height = name_len;
        }

      /* If it fit then we need to determine how many labels can be
         written horizontally. */
      if (width <= max_width && height <= SHORT_NAME_LEN)
	{
#ifndef NDEBUG
	  prc->n_vertical = SIZE_MAX;
#endif
	  for (column = cmd.n_variables; column-- != 0; )
	    {
	      const struct variable *v = cmd.v_variables[column];
              int name_len = strlen (var_get_name (v));
              int fmt_width = var_get_print_format (v)->w;
	      int trial_width = width - fmt_width + MAX (fmt_width, name_len);
	      if (trial_width > max_width)
		{
		  prc->n_vertical = column + 1;
		  break;
		}
	      width = trial_width;
	    }
	  assert (prc->n_vertical != SIZE_MAX);

	  prc->n_vertical = cmd.n_variables;
	  /* Finally determine the length of the headers. */
	  for (prc->header_rows = 0, column = 0;
	       column < prc->n_vertical;
	       column++)
            {
              const struct variable *var = cmd.v_variables[column];
              size_t name_len = strlen (var_get_name (var));
              prc->header_rows = MAX (prc->header_rows, name_len);
            }
	  prc->header_rows++;
	  continue;
	}

      /* Otherwise use the ugly fallback listing format. */
      prc->type = 1;
      prc->header_rows = 0;

      d->cp_y += d->font_height;
      write_fallback_headers (d);
      d->cp_y += d->font_height;
    }

  ds_init_empty (&line_buffer);
}

/* Writes case C to output. */
static void
list_case (const struct ccase *c, casenumber case_idx,
           const struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct outp_driver *d;

  for (d = outp_drivers (NULL); d; d = outp_drivers (d))
    if (d->class->special == 0)
      {
	const struct list_ext *prc = d->prc;
	const int max_width = n_chars_width (d);
	int column;

	if (!prc->header_rows)
	  {
	    ds_put_format(&line_buffer, "%8s: ",
                          var_get_name (cmd.v_variables[0]));
	  }


	for (column = 0; column < cmd.n_variables; column++)
	  {
	    const struct variable *v = cmd.v_variables[column];
            const struct fmt_spec *print = var_get_print_format (v);
	    int width;

	    if (prc->type == 0 && column >= prc->n_vertical)
              {
                int name_len = strlen (var_get_name (v));
                width = MAX (name_len, print->w);
              }
	    else
	      width = print->w;

	    if (width + ds_length(&line_buffer) > max_width &&
		ds_length(&line_buffer) != 0)
	      {
		if (!n_lines_remaining (d))
		  {
		    outp_eject_page (d);
		    write_header (d);
		  }

		write_line (d, ds_cstr (&line_buffer));
		ds_clear(&line_buffer);

		if (!prc->header_rows)
                  ds_put_format (&line_buffer, "%8s: ", var_get_name (v));
	      }

	    if (width > print->w)
              ds_put_char_multiple(&line_buffer, ' ', width - print->w);

            if (fmt_is_string (print->type)
                || dict_contains_var (dict, v))
	      {
		char *s = data_out (case_data (c, v), dict_get_encoding (dict), print);
		ds_put_cstr (&line_buffer, s);
		free (s);
	      }
            else
              {
		char *s;
                union value case_idx_value;
                case_idx_value.f = case_idx;
                s = data_out (&case_idx_value, dict_get_encoding (dict), print);
		ds_put_cstr (&line_buffer, s);
		free (s);
              }

	    ds_put_char (&line_buffer, ' ');
	  }

	if (!n_lines_remaining (d))
	  {
	    outp_eject_page (d);
	    write_header (d);
	  }

	write_line (d, ds_cstr (&line_buffer));
	ds_clear(&line_buffer);
      }
    else if (d->class == &html_class)
      {
	struct html_driver_ext *x = d->ext;
	int column;

	fputs ("  <TR>\n", x->file);

	for (column = 0; column < cmd.n_variables; column++)
	  {
	    const struct variable *v = cmd.v_variables[column];
            const struct fmt_spec *print = var_get_print_format (v);
	    char *s = NULL;

            if (fmt_is_string (print->type)
                || dict_contains_var (dict, v))
	      s = data_out (case_data (c, v), dict_get_encoding (dict), print);
            else
              {
                union value case_idx_value;
                case_idx_value.f = case_idx;
                s = data_out (&case_idx_value, dict_get_encoding (dict), print);
              }

            fputs ("    <TD>", x->file);
            html_put_cell_contents (d, TAB_FIX, ss_buffer (s, print->w));
	    free (s);
            fputs ("</TD>\n", x->file);
	  }

	fputs ("  </TR>\n", x->file);
      }
    else
      NOT_REACHED ();
}

/*
   Local Variables:
   mode: c
   End:
*/
