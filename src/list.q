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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

/* AIX requires this to be the first thing in the file.  */
#include <config.h>
#if __GNUC__
#define alloca __builtin_alloca
#else
#if HAVE_ALLOCA_H
#include <alloca.h>
#else
#ifdef _AIX
#pragma alloca
#else
#ifndef alloca			/* predefined by HP cc +Olibcalls */
char *alloca ();
#endif
#endif
#endif
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "lexer.h"
#include "error.h"
#include "magic.h"
#include "misc.h"
#include "htmlP.h"
#include "output.h"
#include "som.h"
#include "var.h"
#include "vfm.h"
#include "format.h"

#include "debug-print.h"

#if DEBUGGING
static void debug_print (void);
#endif

/* (specification)
   list (lst_):
     *variables=varlist("PV_NO_SCRATCH");
     cases=:from n:first,"%s>0"/by n:step,"%s>0"/ *to n:last,"%s>0";
     format=numbering:numbered/!unnumbered,
            wrap:!wrap/single,
            weight:weight/!noweight.
*/
/* (declarations) */
/* (functions) */

/* Layout for one output driver. */
struct list_ext
  {
    int type;		/* 0=Values and labels fit across the page. */
    int n_vertical;	/* Number of labels to list vertically. */
    int header_rows;	/* Number of header rows. */
    char **header;	/* The header itself. */
  };

/* Parsed command. */
static struct cmd_list cmd;

/* Current case number. */
static int case_num;

/* Line buffer. */
static char *line_buf;

/* TTY-style output functions. */
static int n_lines_remaining (struct outp_driver *d);
static int n_chars_width (struct outp_driver *d);
static void write_line (struct outp_driver *d, char *s);

/* Other functions. */
static int list_cases (struct ccase *);
static void determine_layout (void);
static void clean_up (void);
static void write_header (struct outp_driver *);
static void write_all_headers (void);

/* Returns the number of text lines that can fit on the remainder of
   the page. */
static inline int
n_lines_remaining (struct outp_driver *d)
{
  int diff;

  diff = d->length - d->cp_y;
  return (diff > 0) ? (diff / d->font_height) : 0;
}

/* Returns the number of fixed-width character that can fit across the
   page. */
static inline int
n_chars_width (struct outp_driver *d)
{
  return d->width / d->fixed_width;
}

/* Writes the line S at the current position and advances to the next
   line.  */
static void
write_line (struct outp_driver *d, char *s)
{
  struct outp_text text;
  
  assert (d->cp_y + d->font_height <= d->length);
  text.options = OUTP_T_JUST_LEFT;
  ls_init (&text.s, s, strlen (s));
  text.x = d->cp_x;
  text.y = d->cp_y;
  d->class->text_draw (d, &text);
  d->cp_x = 0;
  d->cp_y += d->font_height;
}
    
/* Parses and executes the LIST procedure. */
int
cmd_list (void)
{
  struct variable casenum_var;

  lex_match_id ("LIST");
  if (!parse_list (&cmd))
    return CMD_FAILURE;
  
  /* Fill in defaults. */
  if (cmd.step == NOT_LONG)
    cmd.step = 1;
  if (cmd.first == NOT_LONG)
    cmd.first = 1;
  if (cmd.last == NOT_LONG)
    cmd.last = LONG_MAX;
  if (!cmd.sbc_variables)
    dict_get_vars (default_dict, &cmd.v_variables, &cmd.n_variables,
		   (1u << DC_SYSTEM) | (1u << DC_SCRATCH));
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
      if (dict_get_weight (default_dict) != NULL)
	{
	  int i;

	  for (i = 0; i < cmd.n_variables; i++)
	    if (cmd.v_variables[i] == dict_get_weight (default_dict))
	      break;
	  if (i >= cmd.n_variables)
	    {
	      /* Add the weight variable to the end of the variable list. */
	      cmd.n_variables++;
	      cmd.v_variables = xrealloc (cmd.v_variables,
					  (cmd.n_variables
					   * sizeof *cmd.v_variables));
	      cmd.v_variables[cmd.n_variables - 1]
                = dict_get_weight (default_dict);
	    }
	}
      else
	msg (SW, _("`/FORMAT WEIGHT' specified, but weighting is not on."));
    }

  /* Case number. */
  if (cmd.numbering == LST_NUMBERED)
    {
      /* Initialize the case-number variable. */
      strcpy (casenum_var.name, "Case#");
      casenum_var.type = NUMERIC;
      casenum_var.fv = -1;
      casenum_var.print.type = FMT_F;
      casenum_var.print.w = (cmd.last == LONG_MAX ? 5 : intlog10 (cmd.last));
      casenum_var.print.d = 0;

      /* Add the weight variable at the beginning of the variable list. */
      cmd.n_variables++;
      cmd.v_variables = xrealloc (cmd.v_variables,
				  cmd.n_variables * sizeof *cmd.v_variables);
      memmove (&cmd.v_variables[1], &cmd.v_variables[0],
	       (cmd.n_variables - 1) * sizeof *cmd.v_variables);
      cmd.v_variables[0] = &casenum_var;
    }

#if DEBUGGING
  /* Print out command. */
  debug_print ();
#endif

  determine_layout ();

  case_num = 0;
  procedure (write_all_headers, list_cases, NULL);
  free (line_buf);

  clean_up ();

  return CMD_SUCCESS;
}

/* Writes headers to all devices.  This is done at the beginning of
   each SPLIT FILE group. */
static void
write_all_headers (void)
{
  struct outp_driver *d;

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
  
	  assert (d->driver_open && d->page_open);
	  if (x->sequence_no == 0 && !d->class->open_page (d))
	    {
	      msg (ME, _("Cannot open first page on HTML device %s."),
		   d->name);
	      return;
	    }

	  fputs ("<TABLE BORDER=1>\n  <TR>\n", x->file.file);
	  
	  {
	    int i;

	    for (i = 0; i < cmd.n_variables; i++)
	      fprintf (x->file.file, "    <TH><I><B>%s</B></I></TH>\n",
		       cmd.v_variables[i]->name);
	  }

	  fputs ("  <TR>\n", x->file.file);
	}
      else
	assert (0);
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
      int i, x;

      /* Allocate, initialize header. */
      prc->header = xmalloc (sizeof (char *) * prc->header_rows);
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
	  struct variable *v = cmd.v_variables[i];
	  int j;

	  memset (&prc->header[prc->header_rows - 1][x], '-', v->print.w);
	  x += v->print.w - 1;
	  for (j = 0; j < (int) strlen (v->name); j++)
	    prc->header[strlen (v->name) - j - 1][x] = v->name[j];
	  x += 2;
	}

      /* Put in horizontal names. */
      for (; i < cmd.n_variables; i++)
	{
	  struct variable *v = cmd.v_variables[i];
	  
	  memset (&prc->header[prc->header_rows - 1][x], '-',
		  max (v->print.w, (int) strlen (v->name)));
	  if ((int) strlen (v->name) < v->print.w)
	    x += v->print.w - strlen (v->name);
 	  memcpy (&prc->header[0][x], v->name, strlen (v->name));
	  x += strlen (v->name) + 1;
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
  {
    int i;
    
    for (i = prc->header_rows - 2; i >= 0; i--)
      write_line (d, prc->header[i]);
    write_line (d, prc->header[prc->header_rows - 1]);
  }
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
	int i;

	if (prc->header)
	  {
	    for (i = 0; i < prc->header_rows; i++)
	      free (prc->header[i]);
	    free (prc->header);
	  }
	free (prc);
      
	d->class->text_set_font_by_name (d, "PROP");
      }
    else if (d->class == &html_class)
      {
	if (d->driver_open && d->page_open)
	  {
	    struct html_driver_ext *x = d->ext;

	    fputs ("</TABLE>\n", x->file.file);
	  }
      }
    else
      assert (0);
  
  free (cmd.v_variables);
}

/* Writes string STRING at the current position.  If the text would
   fall off the side of the page, then advance to the next line,
   indenting by amount INDENT. */
static void
write_varname (struct outp_driver *d, char *string, int indent)
{
  struct outp_text text;

  text.options = OUTP_T_JUST_LEFT;
  ls_init (&text.s, string, strlen (string));
  d->class->text_metrics (d, &text);
  
  if (d->cp_x + text.h > d->width)
    {
      d->cp_y += d->font_height;
      if (d->cp_y + d->font_height > d->length)
	outp_eject_page (d);
      d->cp_x = indent;
    }

  text.x = d->cp_x;
  text.y = d->cp_y;
  d->class->text_draw (d, &text);
  d->cp_x += text.h;
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
  char *leader = local_alloc (strlen (Line) + INT_DIGITS + 1 + 1);
      
  while (index < cmd.n_variables)
    {
      struct outp_text text;

      /* Ensure that there is enough room for a line of text. */
      if (d->cp_y + d->font_height > d->length)
	outp_eject_page (d);
      
      /* The leader is a string like `Line 1: '.  Write the leader. */
      sprintf(leader, "%s %d:", Line, ++line_number);
      text.options = OUTP_T_JUST_LEFT;
      ls_init (&text.s, leader, strlen (leader));
      text.x = 0;
      text.y = d->cp_y;
      d->class->text_draw (d, &text);
      d->cp_x = text.h;

      goto entry;
      do
	{
	  width++;

	entry:
	  {
	    int var_width = cmd.v_variables[index]->print.w;
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
	    char varname[10];
	    sprintf (varname, " %s", cmd.v_variables[index]->name);
	    write_varname (d, varname, text.h);
	  }
	}
      while (++index < cmd.n_variables);

    }
  d->cp_x = 0;
  d->cp_y += d->font_height;
  
  local_free (leader);
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
      int column;	/* Current column. */
      int width;	/* Accumulated width. */
      int max_width;	/* Page width. */
      
      struct list_ext *prc;

      if (d->class == &html_class)
	continue;
      
      assert (d->class->special == 0);

      if (!d->page_open)
	d->class->open_page (d);
      
      max_width = n_chars_width (d);
      largest_page_width = max (largest_page_width, max_width);

      prc = d->prc = xmalloc (sizeof *prc);
      prc->type = 0;
      prc->n_vertical = 0;
      prc->header = NULL;

      /* Try layout #1. */
      for (width = cmd.n_variables - 1, column = 0; column < cmd.n_variables; column++)
	{
	  struct variable *v = cmd.v_variables[column];
	  width += max (v->print.w, (int) strlen (v->name));
	}
      if (width <= max_width)
	{
	  prc->header_rows = 2;
	  d->class->text_set_font_by_name (d, "FIXED");
	  continue;
	}

      /* Try layout #2. */
      for (width = cmd.n_variables - 1, column = 0;
	   column < cmd.n_variables && width <= max_width;
	   column++)
	  width += cmd.v_variables[column]->print.w;
      
      /* If it fit then we need to determine how many labels can be
         written horizontally. */
      if (width <= max_width)
	{
#ifndef NDEBUG
	  prc->n_vertical = -1;
#endif
	  for (column = cmd.n_variables - 1; column >= 0; column--)
	    {
	      struct variable *v = cmd.v_variables[column];
	      int trial_width = (width - v->print.w
				 + max (v->print.w, (int) strlen (v->name)));
	      
	      if (trial_width > max_width)
		{
		  prc->n_vertical = column + 1;
		  break;
		}
	      width = trial_width;
	    }
	  assert(prc->n_vertical != -1);

	  prc->n_vertical = cmd.n_variables;
	  /* Finally determine the length of the headers. */
	  for (prc->header_rows = 0, column = 0;
	       column < prc->n_vertical;
	       column++)
	    prc->header_rows = max (prc->header_rows,
				    (int) strlen (cmd.v_variables[column]->name));
	  prc->header_rows++;

	  d->class->text_set_font_by_name (d, "FIXED");
	  continue;
	}

      /* Otherwise use the ugly fallback listing format. */
      prc->type = 1;
      prc->header_rows = 0;

      d->cp_y += d->font_height;
      write_fallback_headers (d);
      d->cp_y += d->font_height;
      d->class->text_set_font_by_name (d, "FIXED");
    }

  line_buf = xmalloc (max (1022, largest_page_width) + 2);
}

static int
list_cases (struct ccase *c)
{
  struct outp_driver *d;
  
  case_num++;
  if (case_num < cmd.first || case_num > cmd.last
      || (cmd.step != 1 && (case_num - cmd.first) % cmd.step))
    return 1;

  for (d = outp_drivers (NULL); d; d = outp_drivers (d))
    if (d->class->special == 0)
      {
	const struct list_ext *prc = d->prc;
	const int max_width = n_chars_width (d);
	int column;
	int x = 0;

	if (!prc->header_rows)
	  x = nsprintf (line_buf, "%8s: ", cmd.v_variables[0]->name);
      
	for (column = 0; column < cmd.n_variables; column++)
	  {
	    struct variable *v = cmd.v_variables[column];
	    int width;

	    if (prc->type == 0 && column >= prc->n_vertical)
	      width = max ((int) strlen (v->name), v->print.w);
	    else
	      width = v->print.w;

	    if (width + x > max_width && x != 0)
	      {
		if (!n_lines_remaining (d))
		  {
		    outp_eject_page (d);
		    write_header (d);
		  }
	      
		line_buf[x] = 0;
		write_line (d, line_buf);

		x = 0;
		if (!prc->header_rows)
		  x = nsprintf (line_buf, "%8s: ", v->name);
	      }

	    if (width > v->print.w)
	      {
		memset(&line_buf[x], ' ', width - v->print.w);
		x += width - v->print.w;
	      }
	  
	    {
	      union value value;
	    
	      if (formats[v->print.type].cat & FCAT_STRING)
		value.c = c->data[v->fv].s;
	      else if (v->fv == -1)
		value.f = case_num;
	      else
		value.f = c->data[v->fv].f;
		
	      data_out (&line_buf[x], &v->print, &value);
	    }
	    x += v->print.w;
	  
	    line_buf[x++] = ' ';
	  }
      
	if (!n_lines_remaining (d))
	  {
	    outp_eject_page (d);
	    write_header (d);
	  }
	      
	line_buf[x] = 0;
	write_line (d, line_buf);
      }
    else if (d->class == &html_class)
      {
	struct html_driver_ext *x = d->ext;
	int column;

	fputs ("  <TR>\n", x->file.file);
	
	for (column = 0; column < cmd.n_variables; column++)
	  {
	    struct variable *v = cmd.v_variables[column];
	    union value value;
	    char buf[41];
	    
	    if (formats[v->print.type].cat & FCAT_STRING)
	      value.c = c->data[v->fv].s;
	    else if (v->fv == -1)
	      value.f = case_num;
	    else
	      value.f = c->data[v->fv].f;
		
	    data_out (buf, &v->print, &value);
	    buf[v->print.w] = 0;

	    fprintf (x->file.file, "    <TD ALIGN=RIGHT>%s</TD>\n",
		     &buf[strspn (buf, " ")]);
	  }
	  
	fputs ("  </TR>\n", x->file.file);
      }
    else
      assert (0);

  return 1;
}

/* Debugging output. */

#if DEBUGGING
/* Prints out the command as parsed by cmd_list(). */
static void
debug_print (void)
{
  int i;

  puts ("LIST");
  printf ("  VARIABLES=");
  for (i = 0; i < cmd.n_variables; i++)
    {
      if (i)
	putc (' ', stdout);
      fputs (cmd.v_variables[i]->name, stdout);
    }

  printf ("\n  /CASES=FROM %ld TO %ld BY %ld\n", cmd.first, cmd.last, cmd.step);

  fputs ("  /FORMAT=", stdout);
  if (cmd.numbering == LST_NUMBERED)
    fputs ("NUMBERED", stdout);
  else
    fputs ("UNNUMBERED", stdout);
  putc (' ', stdout);
  if (cmd.wrap == LST_WRAP)
    fputs ("WRAP", stdout);
  else
    fputs ("SINGLE", stdout);
  putc (' ', stdout);
  if (cmd.weight == LST_WEIGHT)
    fputs ("WEIGHT", stdout);
  else
    fputs ("NOWEIGHT", stdout);
  puts (".");
}
#endif /* DEBUGGING */

/* 
   Local Variables:
   mode: c
   End:
*/
