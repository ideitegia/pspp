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
#include <ctype.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "data-in.h"
#include "debug-print.h"
#include "dfm.h"
#include "error.h"
#include "file-handle.h"
#include "format.h"
#include "lexer.h"
#include "misc.h"
#include "settings.h"
#include "str.h"
#include "tab.h"
#include "var.h"
#include "vfm.h"

/* Utility function. */

/* FIXME: Either REPEATING DATA must be the last transformation, or we
   must multiplex the transformations that follow (i.e., perform them
   for every case that we produce from a repetition instance).
   Currently we do neither.  We should do one or the other. */
   
/* Describes how to parse one variable. */
struct dls_var_spec
  {
    struct dls_var_spec *next;
    struct variable *v;		/* Associated variable.  Used only in
				   parsing.  Not safe later. */
    char name[9];		/* Free-format: Name of variable. */
    int rec;			/* Fixed-format: Record number (1-based). */
    int fc, lc;			/* Fixed-format: Column numbers in record. */
    struct fmt_spec input;	/* Input format of this field. */
    int fv;			/* First value in case. */
    int type;			/* 0=numeric, >0=width of alpha field. */
  };

/* Constants for DATA LIST type. */
/* Must match table in cmd_data_list(). */
enum
  {
    DLS_FIXED,
    DLS_FREE,
    DLS_LIST
  };

/* DATA LIST private data structure. */
struct data_list_pgm
  {
    struct trns_header h;
    struct dls_var_spec *spec;	/* Variable parsing specifications. */
    struct file_handle *handle;	/* Input file, never NULL. */
    /* Do not reorder preceding fields. */

    int type;			/* A DLS_* constant. */
    struct variable *end;	/* Variable specified on END subcommand. */
    int eof;			/* End of file encountered. */
    int nrec;			/* Number of records. */
  };

/* Holds information on parsing the data file. */
static struct data_list_pgm dls;

/* Pointer to a pointer to where the first dls_var_spec should go. */
static struct dls_var_spec **first;

/* Last dls_var_spec in the chain.  Used for building the linked-list. */
static struct dls_var_spec *next;

static int parse_fixed (void);
static int parse_free (void);
static void dump_fixed_table (void);
static void dump_free_table (void);
static void destroy_dls (struct trns_header *);
static int read_one_case (struct trns_header *, struct ccase *);

/* Message title for REPEATING DATA. */
#define RPD_ERR "REPEATING DATA: "

int
cmd_data_list (void)
{
  /* 0=print no table, 1=print table.  (TABLE subcommand.)  */
  int table = -1;

  lex_match_id ("DATA");
  lex_match_id ("LIST");

  if (vfm_source != &input_program_source
      && vfm_source != &file_type_source)
    discard_variables ();

  dls.handle = default_handle;
  dls.type = -1;
  dls.end = NULL;
  dls.eof = 0;
  dls.nrec = 0;
  dls.spec = NULL;
  next = NULL;
  first = &dls.spec;

  while (token != '/')
    {
      if (lex_match_id ("FILE"))
	{
	  lex_match ('=');
	  dls.handle = fh_parse_file_handle ();
	  if (!dls.handle)
	    return CMD_FAILURE;
	  if (vfm_source == &file_type_source && dls.handle != default_handle)
	    {
	      msg (SE, _("DATA LIST may not use a different file from "
			 "that specified on its surrounding FILE TYPE."));
	      return CMD_FAILURE;
	    }
	}
      else if (lex_match_id ("RECORDS"))
	{
	  lex_match ('=');
	  lex_match ('(');
	  if (!lex_force_int ())
	    return CMD_FAILURE;
	  dls.nrec = lex_integer ();
	  lex_get ();
	  lex_match (')');
	}
      else if (lex_match_id ("END"))
	{
	  if (dls.end)
	    {
	      msg (SE, _("The END subcommand may only be specified once."));
	      return CMD_FAILURE;
	    }
	  
	  lex_match ('=');
	  if (!lex_force_id ())
	    return CMD_FAILURE;
	  dls.end = find_variable (tokid);
	  if (!dls.end)
	    dls.end = force_create_variable (&default_dict, tokid, NUMERIC, 0);
	  lex_get ();
	}
      else if (token == T_ID)
	{
	  /* Must match DLS_* constants. */
	  static const char *id[] = {"FIXED", "FREE", "LIST", "NOTABLE",
				     "TABLE", NULL};
	  const char **p;
	  int index;

	  for (p = id; *p; p++)
	    if (lex_id_match (*p, tokid))
	      break;
	  if (*p == NULL)
	    {
	      lex_error (NULL);
	      return CMD_FAILURE;
	    }
	  
	  lex_get ();

	  index = p - id;
	  if (index < 3)
	    {
	      if (dls.type != -1)
		{
		  msg (SE, _("Only one of FIXED, FREE, or LIST may "
			    "be specified."));
		  return CMD_FAILURE;
		}
	      
	      dls.type = index;
	    }
	  else
	    table = index - 3;
	}
      else
	{
	  lex_error (NULL);
	  return CMD_FAILURE;
	}
    }

  default_handle = dls.handle;

  if (dls.type == -1)
    dls.type = DLS_FIXED;

  if (table == -1)
    {
      if (dls.type == DLS_FREE)
	table = 0;
      else
	table = 1;
    }

  if (dls.type == DLS_FIXED)
    {
      if (!parse_fixed ())
	return CMD_FAILURE;
      if (table)
	dump_fixed_table ();
    }
  else
    {
      if (!parse_free ())
	return CMD_FAILURE;
      if (table)
	dump_free_table ();
    }

  if (vfm_source != NULL)
    {
      struct data_list_pgm *new_pgm;

      dls.h.proc = read_one_case;
      dls.h.free = destroy_dls;

      new_pgm = xmalloc (sizeof *new_pgm);
      memcpy (new_pgm, &dls, sizeof *new_pgm);
      add_transformation ((struct trns_header *) new_pgm);
    }
  else
    vfm_source = &data_list_source;

  return CMD_SUCCESS;
}

static void
append_var_spec (struct dls_var_spec *spec)
{
  if (next == 0)
    *first = next = xmalloc (sizeof *spec);
  else
    next = next->next = xmalloc (sizeof *spec);

  memcpy (next, spec, sizeof *spec);
  next->next = NULL;
}

/* Fixed-format parsing. */

/* Used for chaining together fortran-like format specifiers. */
struct fmt_list
  {
    struct fmt_list *next;
    int count;
    struct fmt_spec f;
    struct fmt_list *down;
  };

/* Used as "local" variables among the fixed-format parsing funcs.  If
   it were guaranteed that PSPP were going to be compiled by gcc,
   I'd make all these functions a single set of nested functions. */
static struct
  {
    char **name;		/* Variable names. */
    int nname;			/* Number of names. */
    int cname;			/* dump_fmt_list: index of next name to use. */

    int recno;			/* Index of current record. */
    int sc;			/* 1-based column number of starting column for
				   next field to output. */

    struct dls_var_spec spec;	/* Next specification to output. */
    int fc, lc;			/* First, last column in set of fields specified
				   together. */

    int level;			/* Nesting level in fixed_parse_fortran(). */
  }
fx;

static int fixed_parse_compatible (void);
static struct fmt_list *fixed_parse_fortran (void);

static int
parse_fixed (void)
{
  int i;

  fx.recno = 0;
  fx.sc = 1;

  while (token != '.')
    {
      while (lex_match ('/'))
	{
	  fx.recno++;
	  if (lex_integer_p ())
	    {
	      if (lex_integer () < fx.recno)
		{
		  msg (SE, _("The record number specified, %ld, is "
			     "before the previous record, %d.  Data "
			     "fields must be listed in order of "
			     "increasing record number."),
		       lex_integer (), fx.recno - 1);
		  return 0;
		}
	      
	      fx.recno = lex_integer ();
	      lex_get ();
	    }
	  fx.sc = 1;
	}
      fx.spec.rec = fx.recno;

      if (!parse_DATA_LIST_vars (&fx.name, &fx.nname, PV_NONE))
	return 0;

      if (token == T_NUM)
	{
	  if (!fixed_parse_compatible ())
	    goto fail;
	}
      else if (token == '(')
	{
	  fx.level = 0;
	  fx.cname = 0;
	  if (!fixed_parse_fortran ())
	    goto fail;
	}
      else
	{
	  msg (SE, _("SPSS-like or FORTRAN-like format "
	       "specification expected after variable names."));
	  goto fail;
	}

      for (i = 0; i < fx.nname; i++)
	free (fx.name[i]);
      free (fx.name);
    }
  if (dls.nrec && next->rec > dls.nrec)
    {
      msg (SE, _("Variables are specified on records that "
		 "should not exist according to RECORDS subcommand."));
      return 0;
    }
  else if (!dls.nrec)
    dls.nrec = next->rec;
  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      return 0;
    }
  return 1;

fail:
  for (i = 0; i < fx.nname; i++)
    free (fx.name[i]);
  free (fx.name);
  return 0;
}

static int
fixed_parse_compatible (void)
{
  int dividend;
  int i;

  if (!lex_force_int ())
    return 0;
  
  fx.fc = lex_integer ();
  if (fx.fc < 1)
    {
      msg (SE, _("Column positions for fields must be positive."));
      return 0;
    }
  lex_get ();

  lex_negative_to_dash ();
  if (lex_match ('-'))
    {
      if (!lex_force_int ())
	return 0;
      fx.lc = lex_integer ();
      if (fx.lc < 1)
	{
	  msg (SE, _("Column positions for fields must be positive."));
	  return 0;
	}
      else if (fx.lc < fx.fc)
	{
	  msg (SE, _("The ending column for a field must be "
		     "greater than the starting column."));
	  return 0;
	}
      
      lex_get ();
    }
  else
    fx.lc = fx.fc;

  fx.spec.input.w = fx.lc - fx.fc + 1;
  if (lex_match ('('))
    {
      struct fmt_desc *fdp;

      if (token == T_ID)
	{
	  const char *cp;

	  fx.spec.input.type = parse_format_specifier_name (&cp, 0);
	  if (fx.spec.input.type == -1)
	    return 0;
	  if (*cp)
	    {
	      msg (SE, _("A format specifier on this line "
			 "has extra characters on the end."));
	      return 0;
	    }
	  
	  lex_get ();
	  lex_match (',');
	}
      else
	fx.spec.input.type = FMT_F;

      if (lex_integer_p ())
	{
	  if (lex_integer () < 1)
	    {
	      msg (SE, _("The value for number of decimal places "
			 "must be at least 1."));
	      return 0;
	    }
	  
	  fx.spec.input.d = lex_integer ();
	  lex_get ();
	}
      else
	fx.spec.input.d = 0;

      fdp = &formats[fx.spec.input.type];
      if (fdp->n_args < 2 && fx.spec.input.d)
	{
	  msg (SE, _("Input format %s doesn't accept decimal places."),
	       fdp->name);
	  return 0;
	}
      
      if (fx.spec.input.d > 16)
	fx.spec.input.d = 16;

      if (!lex_force_match (')'))
	return 0;
    }
  else
    {
      fx.spec.input.type = FMT_F;
      fx.spec.input.d = 0;
    }

  fx.sc = fx.lc + 1;

  if ((fx.lc - fx.fc + 1) % fx.nname)
    {
      msg (SE, _("The %d columns %d-%d "
		 "can't be evenly divided into %d fields."),
	   fx.lc - fx.fc + 1, fx.fc, fx.lc, fx.nname);
      return 0;
    }

  dividend = (fx.lc - fx.fc + 1) / fx.nname;
  fx.spec.input.w = dividend;
  if (!check_input_specifier (&fx.spec.input))
    return 0;

  for (i = 0; i < fx.nname; i++)
    {
      int type;
      struct variable *v;

      if (fx.spec.input.type == FMT_A || fx.spec.input.type == FMT_AHEX)
	type = ALPHA;
      else
	type = NUMERIC;

      v = create_variable (&default_dict, fx.name[i], type, dividend);
      if (v)
	{
	  convert_fmt_ItoO (&fx.spec.input, &v->print);
	  v->write = v->print;
	}
      else
	{
	  v = find_variable (fx.name[i]);
	  assert (v);
	  if (!vfm_source)
	    {
	      msg (SE, _("%s is a duplicate variable name."), fx.name[i]);
	      return 0;
	    }
	  if (type != v->type)
	    {
	      msg (SE, _("There is already a variable %s of a "
			 "different type."),
		   fx.name[i]);
	      return 0;
	    }
	  if (type == ALPHA && dividend != v->width)
	    {
	      msg (SE, _("There is already a string variable %s of a "
			 "different width."), fx.name[i]);
	      return 0;
	    }
	}

      fx.spec.v = v;
      fx.spec.fc = fx.fc + dividend * i;
      fx.spec.lc = fx.spec.fc + dividend - 1;
      fx.spec.fv = v->fv;
      fx.spec.type = v->type == NUMERIC ? 0 : v->width;
      append_var_spec (&fx.spec);
    }
  return 1;
}

/* Destroy a format list and, optionally, all its sublists. */
static void
destroy_fmt_list (struct fmt_list *f, int recurse)
{
  struct fmt_list *next;

  for (; f; f = next)
    {
      next = f->next;
      if (recurse && f->f.type == FMT_DESCEND)
	destroy_fmt_list (f->down, 1);
      free (f);
    }
}

/* Takes a hierarchically structured fmt_list F as constructed by
   fixed_parse_fortran(), and flattens it into a linear list of
   dls_var_spec's. */
static int
dump_fmt_list (struct fmt_list *f)
{
  int i;

  for (; f; f = f->next)
    if (f->f.type == FMT_X)
      fx.sc += f->count;
    else if (f->f.type == FMT_T)
      fx.sc = f->f.w;
    else if (f->f.type == FMT_NEWREC)
      {
	fx.recno += f->count;
	fx.sc = 1;
      }
    else
      for (i = 0; i < f->count; i++)
	if (f->f.type == FMT_DESCEND)
	  {
	    if (!dump_fmt_list (f->down))
	      return 0;
	  }
	else
	  {
	    int type;
	    struct variable *v;

	    type = (formats[f->f.type].cat & FCAT_STRING) ? ALPHA : NUMERIC;
	    if (fx.cname >= fx.nname)
	      {
		msg (SE, _("The number of format "
			   "specifications exceeds the number of "
			   "variable names given."));
		return 0;
	      }
	    
	    fx.spec.v = v = create_variable (&default_dict,
					     fx.name[fx.cname++],
					     type, f->f.w);
	    if (!v)
	      {
		msg (SE, _("%s is a duplicate variable name."), fx.name[i]);
		return 0;
	      }
	    
	    fx.spec.input = f->f;
	    convert_fmt_ItoO (&fx.spec.input, &v->print);
	    v->write = v->print;

	    fx.spec.rec = fx.recno;
	    fx.spec.fc = fx.sc;
	    fx.spec.lc = fx.sc + f->f.w - 1;
	    fx.spec.fv = v->fv;
	    fx.spec.type = v->type == NUMERIC ? 0 : v->width;
	    append_var_spec (&fx.spec);

	    fx.sc += f->f.w;
	  }
  return 1;
}

/* Calls itself recursively to parse nested levels of parentheses.
   Returns to its original caller: NULL, to indicate error; non-NULL,
   but nothing useful, to indicate success (it returns a free()'d
   block). */
static struct fmt_list *
fixed_parse_fortran (void)
{
  struct fmt_list *head;
  struct fmt_list *fl = NULL;

  lex_get ();			/* Skip opening parenthesis. */
  while (token != ')')
    {
      if (fl)
	fl = fl->next = xmalloc (sizeof *fl);
      else
	head = fl = xmalloc (sizeof *fl);

      if (lex_integer_p ())
	{
	  fl->count = lex_integer ();
	  lex_get ();
	}
      else
	fl->count = 1;

      if (token == '(')
	{
	  fl->f.type = FMT_DESCEND;
	  fx.level++;
	  fl->down = fixed_parse_fortran ();
	  fx.level--;
	  if (!fl->down)
	    goto fail;
	}
      else if (lex_match ('/'))
	fl->f.type = FMT_NEWREC;
      else if (!parse_format_specifier (&fl->f, 1)
	       || !check_input_specifier (&fl->f))
	goto fail;

      lex_match (',');
    }
  fl->next = NULL;
  lex_get ();

  if (fx.level)
    return head;

  fl->next = NULL;
  dump_fmt_list (head);
  if (fx.cname < fx.nname)
    {
      msg (SE, _("There aren't enough format specifications "
	   "to match the number of variable names given."));
      goto fail;
    }
  destroy_fmt_list (head, 1);
  return head;

fail:
  fl->next = NULL;
  destroy_fmt_list (head, 0);

  return NULL;
}

/* Displays a table giving information on fixed-format variable
   parsing on DATA LIST. */
/* FIXME: The `Columns' column should be divided into three columns,
   one for the starting column, one for the dash, one for the ending
   column; then right-justify the starting column and left-justify the
   ending column. */
static void
dump_fixed_table (void)
{
  struct dls_var_spec *spec;
  struct tab_table *t;
  char *buf;
  const char *filename;
  int i;

  for (i = 0, spec = *first; spec; spec = spec->next)
    i++;
  t = tab_create (4, i + 1, 0);
  tab_columns (t, TAB_COL_DOWN, 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Variable"));
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Record"));
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Columns"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Format"));
  tab_box (t, TAL_1, TAL_1, TAL_0, TAL_1, 0, 0, 3, i);
  tab_hline (t, TAL_2, 0, 3, 1);
  tab_dim (t, tab_natural_dimensions);

  for (i = 1, spec = *first; spec; spec = spec->next, i++)
    {
      tab_text (t, 0, i, TAB_LEFT, spec->v->name);
      tab_text (t, 1, i, TAT_PRINTF, "%d", spec->rec);
      tab_text (t, 2, i, TAT_PRINTF, "%3d-%3d",
		    spec->fc, spec->lc);
      tab_text (t, 3, i, TAB_LEFT | TAT_FIX,
		    fmt_to_string (&spec->input));
    }

  if (*first == dls.spec)
    {
      filename = fh_handle_name (dls.handle);
      if (filename == NULL)
	filename = "";
      buf = local_alloc (strlen (filename) + INT_DIGITS + 80);
      sprintf (buf, (dls.handle != inline_file
		     ? 
		     ngettext("Reading %d record from file %s.",
			      "Reading %d records from file %s.",dls.nrec)
		     : 
		     ngettext("Reading %d record from the command file.",
			      "Reading %d records from the command file.",
			      dls.nrec)),
	       dls.nrec, filename);
    }
  else
    {
      buf = local_alloc (strlen (_("Occurrence data specifications.")) + 1);
      strcpy (buf, _("Occurrence data specifications."));
    }
  
  tab_title (t, 0, buf);
  tab_submit (t);
  fh_handle_name (NULL);
  local_free (buf);
}

/* Free-format parsing. */

static int
parse_free (void)
{
  struct dls_var_spec spec;
  struct fmt_spec in, out;
  char **name;
  int nname;
  int i;
  int type;

  lex_get ();
  while (token != '.')
    {
      if (!parse_DATA_LIST_vars (&name, &nname, PV_NONE))
	return 0;
      if (lex_match ('('))
	{
	  if (!parse_format_specifier (&in, 0) || !check_input_specifier (&in))
	    goto fail;
	  if (!lex_force_match (')'))
	    goto fail;
	  convert_fmt_ItoO (&in, &out);
	}
      else
	{
	  lex_match ('*');
	  in.type = FMT_F;
	  in.w = 8;
	  in.d = 0;
	  out = set_format;
	}

      spec.input = in;
      if (in.type == FMT_A || in.type == FMT_AHEX)
	type = ALPHA;
      else
	type = NUMERIC;
      for (i = 0; i < nname; i++)
	{
	  struct variable *v;

	  spec.v = v = create_variable (&default_dict, name[i], type, in.w);
	  if (!v)
	    {
	      msg (SE, _("%s is a duplicate variable name."), name[i]);
	      return 0;
	    }
	  
	  v->print = v->write = out;

	  strcpy (spec.name, name[i]);
	  spec.fv = v->fv;
	  spec.type = type == NUMERIC ? 0 : v->width;
	  append_var_spec (&spec);
	}
      for (i = 0; i < nname; i++)
	free (name[i]);
      free (name);
    }

  if (token != '.')
    lex_error (_("expecting end of command"));
  return 1;

fail:
  for (i = 0; i < nname; i++)
    free (name[i]);
  free (name);
  return 0;
}

/* Displays a table giving information on free-format variable parsing
   on DATA LIST. */
static void
dump_free_table (void)
{
  struct tab_table *t;
  int i;
  
  {
    struct dls_var_spec *spec;
    for (i = 0, spec = dls.spec; spec; spec = spec->next)
      i++;
  }
  
  t = tab_create (2, i + 1, 0);
  tab_columns (t, TAB_COL_DOWN, 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Variable"));
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Format"));
  tab_box (t, TAL_1, TAL_1, TAL_0, TAL_1, 0, 0, 1, i);
  tab_hline (t, TAL_2, 0, 1, 1);
  tab_dim (t, tab_natural_dimensions);
  
  {
    struct dls_var_spec *spec;
    
    for (i = 1, spec = dls.spec; spec; spec = spec->next, i++)
      {
	tab_text (t, 0, i, TAB_LEFT, spec->v->name);
	tab_text (t, 1, i, TAB_LEFT | TAT_FIX, fmt_to_string (&spec->input));
      }
  }
  
  {
    const char *filename;

    filename = fh_handle_name (dls.handle);
    if (filename == NULL)
      filename = "";
    tab_title (t, 1,
	       (dls.handle != inline_file
		? _("Reading free-form data from file %s.")
		: _("Reading free-form data from the command file.")),
	       filename);
  }
  
  tab_submit (t);
  fh_handle_name (NULL);
}

/* Input procedure. */ 

/* Pointer to relevant parsing data.  Static just to avoid passing it
   around so much. */
static struct data_list_pgm *dlsp;

/* Extracts a field from the current position in the current record.
   Fields can be unquoted or quoted with single- or double-quote
   characters.  *RET_LEN is set to the field length, *RET_CP is set to
   the field itself.  After parsing the field, sets the current
   position in the record to just past the field.  Returns 0 on
   failure or a 1-based column number indicating the beginning of the
   field on success. */
static int
cut_field (char **ret_cp, int *ret_len)
{
  char *cp, *ep;
  int len;

  cp = dfm_get_record (dlsp->handle, &len);
  if (!cp)
    return 0;

  ep = cp + len;

  /* Skip leading whitespace and commas. */
  while ((isspace ((unsigned char) *cp) || *cp == ',') && cp < ep)
    cp++;
  if (cp >= ep)
    return 0;

  /* Three types of fields: quoted with ', quoted with ", unquoted. */
  if (*cp == '\'' || *cp == '"')
    {
      int quote = *cp;

      *ret_cp = ++cp;
      while (cp < ep && *cp != quote)
	cp++;
      *ret_len = cp - *ret_cp;
      if (cp < ep)
	cp++;
      else
	msg (SW, _("Scope of string exceeds line."));
    }
  else
    {
      *ret_cp = cp;
      while (cp < ep && !isspace ((unsigned char) *cp) && *cp != ',')
	cp++;
      *ret_len = cp - *ret_cp;
    }

  {
    int beginning_column;
    
    dfm_set_record (dlsp->handle, *ret_cp);
    beginning_column = dfm_get_cur_col (dlsp->handle) + 1;
    
    dfm_set_record (dlsp->handle, cp);
    
    return beginning_column;
  }
}

static int read_from_data_list_fixed (void);
static int read_from_data_list_free (void);
static int read_from_data_list_list (void);
static int do_reading (int flag);

/* FLAG==0: reads any number of cases into temp_case and calls
   write_case() for each one, returns garbage.  FLAG!=0: reads one
   case into temp_case and returns -2 on eof, -1 otherwise.
   Uses dlsp as the relevant parsing description. */
static int
do_reading (int flag)
{
  int (*func) (void);

  int code;

  dfm_push (dlsp->handle);

  switch (dlsp->type)
    {
    case DLS_FIXED:
      func = read_from_data_list_fixed;
      break;
    case DLS_FREE:
      func = read_from_data_list_free;
      break;
    case DLS_LIST:
      func = read_from_data_list_list;
      break;
    default:
      assert (0);
    }
  if (flag)
    {
      code = func ();
      if (code == -2)
	{
	  if (dlsp->eof == 1)
	    {
	      msg (SE, _("Attempt to read past end of file."));
	      err_failure ();
	      return -2;
	    }
	  dlsp->eof = 1;
	}
      else
	dlsp->eof = 0;

      if (dlsp->end != NULL)
	{
	  if (code == -2)
	    {
	      printf ("end of file, setting %s to 1\n", dlsp->end->name);
	      temp_case->data[dlsp->end->fv].f = 1.0;
	      code = -1;
	    }
	  else
	    {
	      printf ("not end of file, setting %s to 0\n", dlsp->end->name);
	      temp_case->data[dlsp->end->fv].f = 0.0;
	    }
	}
    }
  else
    {
      while (func () != -2)
	if (!write_case ())
	  {
	    debug_printf ((_("abort in write_case()\n")));
	    break;
	  }
      fh_close_handle (dlsp->handle);
    }
  dfm_pop (dlsp->handle);

  return code;
}

/* Reads a case from the data file and parses it according to
   fixed-format syntax rules. */
static int
read_from_data_list_fixed (void)
{
  struct dls_var_spec *var_spec = dlsp->spec;
  int i;

  if (!dfm_get_record (dlsp->handle, NULL))
    return -2;
  for (i = 1; i <= dlsp->nrec; i++)
    {
      int len;
      char *line = dfm_get_record (dlsp->handle, &len);
      
      if (!line)
	{
	  /* Note that this can't occur on the first record. */
	  msg (SW, _("Partial case of %d of %d records discarded."),
	       i - 1, dlsp->nrec);
	  return -2;
	}

      for (; var_spec && i == var_spec->rec; var_spec = var_spec->next)
	{
	  struct data_in di;

	  data_in_finite_line (&di, line, len, var_spec->fc, var_spec->lc);
	  di.v = &temp_case->data[var_spec->fv];
	  di.flags = 0;
	  di.f1 = var_spec->fc;
	  di.format = var_spec->input;

	  data_in (&di);
	}

      dfm_fwd_record (dlsp->handle);
    }

  return -1;
}

/* Reads a case from the data file and parses it according to
   free-format syntax rules. */
static int
read_from_data_list_free (void)
{
  struct dls_var_spec *var_spec;
  char *field;
  int len;

  for (var_spec = dlsp->spec; var_spec; var_spec = var_spec->next)
    {
      int column;
      
      /* Cut out a field and read in a new record if necessary. */
      for (;;)
	{
	  column = cut_field (&field, &len);
	  if (column != 0)
	    break;

	  if (dfm_get_record (dlsp->handle, NULL))
	    dfm_fwd_record (dlsp->handle);
	  if (!dfm_get_record (dlsp->handle, NULL))
	    {
	      if (var_spec != dlsp->spec)
		msg (SW, _("Partial case discarded.  The first variable "
		     "missing was %s."), var_spec->name);
	      return -2;
	    }
	}
      
      {
	struct data_in di;

	di.s = field;
	di.e = field + len;
	di.v = &temp_case->data[var_spec->fv];
	di.flags = 0;
	di.f1 = column;
	di.format = var_spec->input;
	data_in (&di);
      }
    }
  return -1;
}

/* Reads a case from the data file and parses it according to
   list-format syntax rules. */
static int
read_from_data_list_list (void)
{
  struct dls_var_spec *var_spec;
  char *field;
  int len;

  if (!dfm_get_record (dlsp->handle, NULL))
    return -2;

  for (var_spec = dlsp->spec; var_spec; var_spec = var_spec->next)
    {
      /* Cut out a field and check for end-of-line. */
      int column = cut_field (&field, &len);
      
      if (column == 0)
	{
	  if (set_undefined)
	    msg (SW, _("Missing value(s) for all variables from %s onward.  "
		 "These will be filled with the system-missing value "
		 "or blanks, as appropriate."),
		 var_spec->name);
	  for (; var_spec; var_spec = var_spec->next)
	    if (!var_spec->type)
	      temp_case->data[var_spec->fv].f = SYSMIS;
	    else
	      memset (temp_case->data[var_spec->fv].s, ' ', var_spec->type);
	  break;
	}
      
      {
	struct data_in di;

	di.s = field;
	di.e = field + len;
	di.v = &temp_case->data[var_spec->fv];
	di.flags = 0;
	di.f1 = column;
	di.format = var_spec->input;
	data_in (&di);
      }
    }

  dfm_fwd_record (dlsp->handle);
  return -1;
}

/* Destroys DATA LIST transformation or input program PGM. */
static void
destroy_dls (struct trns_header *pgm)
{
  struct data_list_pgm *dls = (struct data_list_pgm *) pgm;
  struct dls_var_spec *iter, *next;

  iter = dls->spec;
  while (iter)
    {
      next = iter->next;
      free (iter);
      iter = next;
    }
  fh_close_handle (dls->handle);
}

/* Note that since this is exclusively an input program, C is
   guaranteed to be temp_case. */
static int
read_one_case (struct trns_header *t, struct ccase *c unused)
{
  dlsp = (struct data_list_pgm *) t;
  return do_reading (1);
}

/* Reads all the records from the data file and passes them to
   write_case(). */
static void
data_list_source_read (void)
{
  dlsp = &dls;
  do_reading (0);
}

/* Destroys the source's internal data. */
static void
data_list_source_destroy_source (void)
{
  destroy_dls ((struct trns_header *) & dls);
}

struct case_stream data_list_source = 
  {
    NULL,
    data_list_source_read,
    NULL,
    NULL,
    data_list_source_destroy_source,
    NULL,
    "DATA LIST",
  };

/* REPEATING DATA. */

/* Represents a number or a variable. */
struct rpd_num_or_var
  {
    int num;			/* Value, or 0. */
    struct variable *var;	/* Variable, if number==0. */
  };
    
/* REPEATING DATA private data structure. */
struct repeating_data_trns
  {
    struct trns_header h;
    struct dls_var_spec *spec;	/* Variable parsing specifications. */
    struct file_handle *handle;	/* Input file, never NULL. */
    /* Do not reorder preceding fields. */

    struct rpd_num_or_var starts_beg;	/* STARTS=, before the dash. */
    struct rpd_num_or_var starts_end;	/* STARTS=, after the dash. */
    struct rpd_num_or_var occurs;	/* OCCURS= subcommand. */
    struct rpd_num_or_var length;	/* LENGTH= subcommand. */
    struct rpd_num_or_var cont_beg;	/* CONTINUED=, before the dash. */
    struct rpd_num_or_var cont_end;	/* CONTINUED=, after the dash. */
    int id_beg, id_end;			/* ID subcommand, beginning & end columns. */
    struct variable *id_var;		/* ID subcommand, DATA LIST variable. */
    struct fmt_spec id_spec;		/* ID subcommand, input format spec. */
  };

/* Information about the transformation being parsed. */
static struct repeating_data_trns rpd;

static int read_one_set_of_repetitions (struct trns_header *, struct ccase *);
static int parse_num_or_var (struct rpd_num_or_var *, const char *);
static int parse_repeating_data (void);
static void find_variable_input_spec (struct variable *v,
				      struct fmt_spec *spec);

/* Parses the REPEATING DATA command. */
int
cmd_repeating_data (void)
{
  /* 0=print no table, 1=print table.  (TABLE subcommand.)  */
  int table = 1;

  /* Bits are set when a particular subcommand has been seen. */
  unsigned seen = 0;
  
  lex_match_id ("REPEATING");
  lex_match_id ("DATA");

  assert (vfm_source == &input_program_source
	  || vfm_source == &file_type_source);
  
  rpd.handle = default_handle;
  rpd.starts_beg.num = 0;
  rpd.starts_beg.var = NULL;
  rpd.starts_end = rpd.occurs = rpd.length = rpd.cont_beg
    = rpd.cont_end = rpd.starts_beg;
  rpd.id_beg = rpd.id_end = 0;
  rpd.id_var = NULL;
  rpd.spec = NULL;
  first = &rpd.spec;
  next = NULL;

  lex_match ('/');
  
  for (;;)
    {
      if (lex_match_id ("FILE"))
	{
	  lex_match ('=');
	  rpd.handle = fh_parse_file_handle ();
	  if (!rpd.handle)
	    return CMD_FAILURE;
	  if (rpd.handle != default_handle)
	    {
	      msg (SE, _("REPEATING DATA must use the same file as its "
			 "corresponding DATA LIST or FILE TYPE."));
	      return CMD_FAILURE;
	    }
	}
      else if (lex_match_id ("STARTS"))
	{
	  lex_match ('=');
	  if (seen & 1)
	    {
	      msg (SE, _("STARTS subcommand given multiple times."));
	      return CMD_FAILURE;
	    }
	  seen |= 1;

	  if (!parse_num_or_var (&rpd.starts_beg, "STARTS beginning column"))
	    return CMD_FAILURE;

	  lex_negative_to_dash ();
	  if (lex_match ('-'))
	    {
	      if (!parse_num_or_var (&rpd.starts_end, "STARTS ending column"))
		return CMD_FAILURE;
	    } else {
	      /* Otherwise, rpd.starts_end is left uninitialized.
		 This is okay.  We will initialize it later from the
		 record length of the file.  We can't do this now
		 because we can't be sure that the user has specified
		 the file handle yet. */
	    }

	  if (rpd.starts_beg.num != 0 && rpd.starts_end.num != 0
	      && rpd.starts_beg.num > rpd.starts_end.num)
	    {
	      msg (SE, _("STARTS beginning column (%d) exceeds "
			 "STARTS ending column (%d)."),
		   rpd.starts_beg.num, rpd.starts_end.num);
	      return CMD_FAILURE;
	    }
	}
      else if (lex_match_id ("OCCURS"))
	{
	  lex_match ('=');
	  if (seen & 2)
	    {
	      msg (SE, _("OCCURS subcommand given multiple times."));
	      return CMD_FAILURE;
	    }
	  seen |= 2;

	  if (!parse_num_or_var (&rpd.occurs, "OCCURS"))
	    return CMD_FAILURE;
	}
      else if (lex_match_id ("LENGTH"))
	{
	  lex_match ('=');
	  if (seen & 4)
	    {
	      msg (SE, _("LENGTH subcommand given multiple times."));
	      return CMD_FAILURE;
	    }
	  seen |= 4;

	  if (!parse_num_or_var (&rpd.length, "LENGTH"))
	    return CMD_FAILURE;
	}
      else if (lex_match_id ("CONTINUED"))
	{
	  lex_match ('=');
	  if (seen & 8)
	    {
	      msg (SE, _("CONTINUED subcommand given multiple times."));
	      return CMD_FAILURE;
	    }
	  seen |= 8;

	  if (!lex_match ('/'))
	    {
	      if (!parse_num_or_var (&rpd.cont_beg, "CONTINUED beginning column"))
		return CMD_FAILURE;

	      lex_negative_to_dash ();
	      if (lex_match ('-')
		  && !parse_num_or_var (&rpd.cont_end,
					"CONTINUED ending column"))
		return CMD_FAILURE;
	  
	      if (rpd.cont_beg.num != 0 && rpd.cont_end.num != 0
		  && rpd.cont_beg.num > rpd.cont_end.num)
		{
		  msg (SE, _("CONTINUED beginning column (%d) exceeds "
			     "CONTINUED ending column (%d)."),
		       rpd.cont_beg.num, rpd.cont_end.num);
		  return CMD_FAILURE;
		}
	    }
	  else
	    rpd.cont_beg.num = 1;
	}
      else if (lex_match_id ("ID"))
	{
	  lex_match ('=');
	  if (seen & 16)
	    {
	      msg (SE, _("ID subcommand given multiple times."));
	      return CMD_FAILURE;
	    }
	  seen |= 16;
	  
	  if (!lex_force_int ())
	    return CMD_FAILURE;
	  if (lex_integer () < 1)
	    {
	      msg (SE, _("ID beginning column (%ld) must be positive."),
		   lex_integer ());
	      return CMD_FAILURE;
	    }
	  rpd.id_beg = lex_integer ();
	  
	  lex_get ();
	  lex_negative_to_dash ();
	  
	  if (lex_match ('-'))
	    {
	      if (!lex_force_int ())
		return CMD_FAILURE;
	      if (lex_integer () < 1)
		{
		  msg (SE, _("ID ending column (%ld) must be positive."),
		       lex_integer ());
		  return CMD_FAILURE;
		}
	      if (lex_integer () < rpd.id_end)
		{
		  msg (SE, _("ID ending column (%ld) cannot be less than "
			     "ID beginning column (%d)."),
		       lex_integer (), rpd.id_beg);
		  return CMD_FAILURE;
		}
	      
	      rpd.id_end = lex_integer ();
	      lex_get ();
	    }
	  else rpd.id_end = rpd.id_beg;

	  if (!lex_force_match ('='))
	    return CMD_FAILURE;
	  rpd.id_var = parse_variable ();
	  if (rpd.id_var == NULL)
	    return CMD_FAILURE;

	  find_variable_input_spec (rpd.id_var, &rpd.id_spec);
	}
      else if (lex_match_id ("TABLE"))
	table = 1;
      else if (lex_match_id ("NOTABLE"))
	table = 0;
      else if (lex_match_id ("DATA"))
	break;
      else
	{
	  lex_error (NULL);
	  return CMD_FAILURE;
	}

      if (!lex_force_match ('/'))
	return CMD_FAILURE;
    }

  /* Comes here when DATA specification encountered. */
  if ((seen & (1 | 2)) != (1 | 2))
    {
      if ((seen & 1) == 0)
	msg (SE, _("Missing required specification STARTS."));
      if ((seen & 2) == 0)
	msg (SE, _("Missing required specification OCCURS."));
      return CMD_FAILURE;
    }

  /* Enforce ID restriction. */
  if ((seen & 16) && !(seen & 8))
    {
      msg (SE, _("ID specified without CONTINUED."));
      return CMD_FAILURE;
    }

  /* Calculate starts_end, cont_end if necessary. */
  if (rpd.starts_end.num == 0 && rpd.starts_end.var == NULL)
    rpd.starts_end.num = fh_record_width (rpd.handle);
  if (rpd.cont_end.num == 0 && rpd.starts_end.var == NULL)
    rpd.cont_end.num = fh_record_width (rpd.handle);
      
  /* Calculate length if possible. */
  if ((seen & 4) == 0)
    {
      struct dls_var_spec *iter;
      
      for (iter = rpd.spec; iter; iter = iter->next)
	{
	  if (iter->lc > rpd.length.num)
	    rpd.length.num = iter->lc;
	}
      assert (rpd.length.num != 0);
    }
  
  lex_match ('=');
  if (!parse_repeating_data ())
    return CMD_FAILURE;

  if (table)
    dump_fixed_table ();

  {
    struct repeating_data_trns *new_trns;

    rpd.h.proc = read_one_set_of_repetitions;
    rpd.h.free = destroy_dls;

    new_trns = xmalloc (sizeof *new_trns);
    memcpy (new_trns, &rpd, sizeof *new_trns);
    add_transformation ((struct trns_header *) new_trns);
  }

  return lex_end_of_command ();
}

/* Because of the way that DATA LIST is structured, it's not trivial
   to determine what input format is associated with a given variable.
   This function finds the input format specification for variable V
   and puts it in SPEC. */
static void 
find_variable_input_spec (struct variable *v, struct fmt_spec *spec)
{
  int i;
  
  for (i = 0; i < n_trns; i++)
    {
      struct data_list_pgm *pgm = (struct data_list_pgm *) t_trns[i];
      
      if (pgm->h.proc == read_one_case)
	{
	  struct dls_var_spec *iter;

	  for (iter = pgm->spec; iter; iter = iter->next)
	    if (iter->v == v)
	      {
		*spec = iter->input;
		return;
	      }
	}
    }
  
  assert (0);
}

/* Parses a number or a variable name from the syntax file and puts
   the results in VALUE.  Ensures that the number is at least 1; else
   emits an error based on MESSAGE.  Returns nonzero only if
   successful. */
static int
parse_num_or_var (struct rpd_num_or_var *value, const char *message)
{
  if (token == T_ID)
    {
      value->num = 0;
      value->var = parse_variable ();
      if (value->var == NULL)
	return 0;
      if (value->var->type == ALPHA)
	{
	  msg (SE, _("String variable not allowed here."));
	  return 0;
	}
    }
  else if (lex_integer_p ())
    {
      value->num = lex_integer ();
      
      if (value->num < 1)
	{
	  msg (SE, _("%s (%d) must be at least 1."), message, value->num);
	  return 0;
	}
      
      lex_get ();
    } else {
      msg (SE, _("Variable or integer expected for %s."), message);
      return 0;
    }
  return 1;
}

/* Parses data specifications for repeating data groups.  Taken from
   parse_fixed().  Returns nonzero only if successful.  */
static int
parse_repeating_data (void)
{
  int i;

  fx.recno = 0;
  fx.sc = 1;

  while (token != '.')
    {
      fx.spec.rec = fx.recno;

      if (!parse_DATA_LIST_vars (&fx.name, &fx.nname, PV_NONE))
	return 0;

      if (token == T_NUM)
	{
	  if (!fixed_parse_compatible ())
	    goto fail;
	}
      else if (token == '(')
	{
	  fx.level = 0;
	  fx.cname = 0;
	  if (!fixed_parse_fortran ())
	    goto fail;
	}
      else
	{
	  msg (SE, _("SPSS-like or FORTRAN-like format "
	       "specification expected after variable names."));
	  goto fail;
	}

      for (i = 0; i < fx.nname; i++)
	free (fx.name[i]);
      free (fx.name);
    }
  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      return 0;
    }
  
  return 1;

fail:
  for (i = 0; i < fx.nname; i++)
    free (fx.name[i]);
  free (fx.name);
  return 0;
}

/* Obtains the real value for rpd_num_or_var N in case C and returns
   it.  The valid range is nonnegative numbers, but numbers outside
   this range can be returned and should be handled by the caller as
   invalid. */
static int
realize_value (struct rpd_num_or_var *n, struct ccase *c)
{
  if (n->num > 0)
    return n->num;
  
  assert (n->num == 0);
  if (n->var != NULL)
    {
      double v = c->data[n->var->fv].f;

      if (v == SYSMIS || v <= INT_MIN || v >= INT_MAX)
	return -1;
      else
	return v;
    }
  else
    return 0;
}

/* Parses one record of repeated data and outputs corresponding cases.
   Repeating data is present in line LINE having length LEN.
   Repeating data begins in column BEG and continues through column
   END inclusive (1-based columns); occurrences are offset OFS columns
   from each other.  C is the case that will be filled in; T is the
   REPEATING DATA transformation.  The record ID will be verified if
   COMPARE_ID is nonzero; if it is zero, then the record ID is
   initialized to the ID present in the case (assuming that ID
   location was specified by the user).  Returns number of occurrences
   parsed up to the specified maximum of MAX_OCCURS. */
static int
rpd_parse_record (int beg, int end, int ofs, struct ccase *c,
		  struct repeating_data_trns *t,
		  char *line, int len, int compare_id, int max_occurs)
{
  int occurrences;
  int cur = beg;

  /* Handle record ID values. */
  if (t->id_beg != 0)
    {
      static union value comparator;
      union value v;
      
      {
	struct data_in di;

	data_in_finite_line (&di, line, len, t->id_beg, t->id_end);
	di.v = &v;
	di.flags = 0;
	di.f1 = t->id_beg;
	di.format = t->id_spec;

	if (!data_in (&di))
	  return 0;
      }

      if (compare_id == 0)
	comparator = v;
      else if ((t->id_var->type == NUMERIC && comparator.f != v.f)
	       || (t->id_var->type == ALPHA
		   && strncmp (comparator.s, v.s, t->id_var->width)))
	{
	  char comp_str [64];
	  char v_str [64];

	  if (!data_out (comp_str, &t->id_var->print, &comparator))
	    comp_str[0] = 0;
	  if (!data_out (v_str, &t->id_var->print, &v))
	    v_str[0] = 0;
	  
	  comp_str[t->id_var->print.w] = v_str[t->id_var->print.w] = 0;
	    
	  tmsg (SE, RPD_ERR, 
		_("Mismatched case ID (%s).  Expected value was %s."),
		v_str, comp_str);

	  return 0;
	}
    }

  /* Iterate over the set of expected occurrences and record each of
     them as a separate case.  FIXME: We need to execute any
     transformations that follow the current one. */
  {
    int warned = 0;

    for (occurrences = 0; occurrences < max_occurs; )
      {
	if (cur + ofs > end + 1)
	  break;
	occurrences++;

	{
	  struct dls_var_spec *var_spec = t->spec;
	
	  for (; var_spec; var_spec = var_spec->next)
	    {
	      int fc = var_spec->fc - 1 + cur;
	      int lc = var_spec->lc - 1 + cur;

	      if (fc > len && !warned && var_spec->input.type != FMT_A)
		{
		  warned = 1;

		  tmsg (SW, RPD_ERR,
			_("Variable %s startging in column %d extends "
			  "beyond physical record length of %d."),
			var_spec->v->name, fc, len);
		}
	      
	      {
		struct data_in di;

		data_in_finite_line (&di, line, len, fc, lc);
		di.v = &c->data[var_spec->fv];
		di.flags = 0;
		di.f1 = fc + 1;
		di.format = var_spec->input;

		if (!data_in (&di))
		  return 0;
	      }
	    }
	}

	cur += ofs;

	if (!write_case ())
	  return 0;
      }
  }

  return occurrences;
}

/* Analogous to read_one_case; reads one set of repetitions of the
   elements in the REPEATING DATA structure.  Returns -1 on success,
   -2 on end of file or on failure. */
static int
read_one_set_of_repetitions (struct trns_header *trns, struct ccase *c)
{
  dfm_push (dlsp->handle);
  
  {
    struct repeating_data_trns *t = (struct repeating_data_trns *) trns;
    
    char *line;		/* Current record. */
    int len;		/* Length of current record. */

    int starts_beg;	/* Starting column. */
    int starts_end;	/* Ending column. */
    int occurs;		/* Number of repetitions. */
    int length;		/* Length of each occurrence. */
    int cont_beg;	/* Starting column for continuation lines. */
    int cont_end;	/* Ending column for continuation lines. */

    int occurs_left;	/* Number of occurrences remaining. */

    int code;		/* Return value from rpd_parse_record(). */
    
    int skip_first_record = 0;
    
    /* Read the current record. */
    dfm_bkwd_record (dlsp->handle, 1);
    line = dfm_get_record (dlsp->handle, &len);
    if (line == NULL)
      return -2;
    dfm_fwd_record (dlsp->handle);

    /* Calculate occurs, length. */
    occurs_left = occurs = realize_value (&t->occurs, c);
    if (occurs <= 0)
      {
	tmsg (SE, RPD_ERR, _("Invalid value %d for OCCURS."), occurs);
	return -3;
      }
    starts_beg = realize_value (&t->starts_beg, c);
    if (starts_beg <= 0)
      {
	tmsg (SE, RPD_ERR, _("Beginning column for STARTS (%d) must be "
			     "at least 1."),
	      starts_beg);
	return -3;
      }
    starts_end = realize_value (&t->starts_end, c);
    if (starts_end < starts_beg)
      {
	tmsg (SE, RPD_ERR, _("Ending column for STARTS (%d) is less than "
			     "beginning column (%d)."),
	      starts_end, starts_beg);
	skip_first_record = 1;
      }
    length = realize_value (&t->length, c);
    if (length < 0)
      {
	tmsg (SE, RPD_ERR, _("Invalid value %d for LENGTH."), length);
	length = 1;
	occurs = occurs_left = 1;
      }
    cont_beg = realize_value (&t->cont_beg, c);
    if (cont_beg < 0)
      {
	tmsg (SE, RPD_ERR, _("Beginning column for CONTINUED (%d) must be "
			     "at least 1."),
	      cont_beg);
	return -2;
      }
    cont_end = realize_value (&t->cont_end, c);
    if (cont_end < cont_beg)
      {
	tmsg (SE, RPD_ERR, _("Ending column for CONTINUED (%d) is less than "
			     "beginning column (%d)."),
	      cont_end, cont_beg);
	return -2;
      }

    /* Parse the first record. */
    if (!skip_first_record)
      {
	code = rpd_parse_record (starts_beg, starts_end, length, c, t, line,
				 len, 0, occurs_left);
	if (!code)
	  return -2;
      }
    else if (cont_beg == 0)
      return -3;

    /* Make sure, if some occurrences are left, that we have
       continuation records. */
    occurs_left -= code;
    if (occurs_left != 0 && cont_beg == 0)
      {
	tmsg (SE, RPD_ERR,
	      _("Number of repetitions specified on OCCURS (%d) "
		"exceed number of repetitions available in "
		"space on STARTS (%d), and CONTINUED not specified."),
	      occurs, code);
	return -2;
      }

    /* Go on to additional records. */
    while (occurs_left != 0)
      {
	assert (occurs_left >= 0);

	/* Read in another record. */
	line = dfm_get_record (dlsp->handle, &len);
	if (line == NULL)
	  {
	    tmsg (SE, RPD_ERR,
		  _("Unexpected end of file with %d repetitions "
		    "remaining out of %d."),
		  occurs_left, occurs);
	    return -2;
	  }
	dfm_fwd_record (dlsp->handle);

	/* Parse this record. */
	code = rpd_parse_record (cont_beg, cont_end, length, c, t, line,
				 len, 1, occurs_left);
	if (!code)
	  return -2;
	occurs_left -= code;
      }
  }
    
  dfm_pop (dlsp->handle);

  /* FIXME: This is a kluge until we've implemented multiplexing of
     transformations. */
  return -3;
}
