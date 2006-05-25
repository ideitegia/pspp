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

/* FIXME: seems like a lot of code duplication with data-list.c. */

#include <config.h>

#include <stdlib.h>

#include <data/case.h>
#include <data/procedure.h>
#include <data/transformations.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/data-writer.h>
#include <language/data-io/file-handle.h>
#include <language/expressions/public.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <output/manager.h>
#include <output/table.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Describes what to do when an output field is encountered. */
enum
  {
    PRT_ERROR,			/* Invalid value. */
    PRT_NEWLINE,		/* Newline. */
    PRT_CONST,			/* Constant string. */
    PRT_VAR,			/* Variable. */
    PRT_SPACE			/* A single space. */
  };

/* Describes how to output one field. */
struct prt_out_spec
  {
    struct prt_out_spec *next;
    int type;			/* PRT_* constant. */
    int fc;			/* 0-based first column. */
    union
      {
	char *c;		/* PRT_CONST: Associated string. */
	struct
	  {
	    struct variable *v;	/* PRT_VAR: Associated variable. */
	    struct fmt_spec f;	/* PRT_VAR: Output spec. */
	  }
	v;
      }
    u;
  };

/* Enums for use with print_trns's `options' field. */
enum
  {
    PRT_CMD_MASK = 1,		/* Command type mask. */
    PRT_PRINT = 0,		/* PRINT transformation identifier. */
    PRT_WRITE = 1,		/* WRITE transformation identifier. */
    PRT_EJECT = 002,		/* Can be combined with CMD_PRINT only. */
    PRT_BINARY = 004            /* File is binary, omit newlines. */
  };

/* PRINT, PRINT EJECT, WRITE private data structure. */
struct print_trns
  {
    struct dfm_writer *writer;	/* Output file, NULL=listing file. */
    int options;		/* PRT_* bitmapped field. */
    struct prt_out_spec *spec;	/* Output specifications. */
    int max_width;		/* Maximum line width including null. */
    char *line;			/* Buffer for sticking lines in. */
  };

/* PRT_PRINT or PRT_WRITE. */
int which_cmd;

/* Holds information on parsing the data file. */
static struct print_trns prt;

/* Last prt_out_spec in the chain.  Used for building the linked-list. */
static struct prt_out_spec *next;

/* Number of records. */
static int nrec;

static int internal_cmd_print (int flags);
static trns_proc_func print_trns_proc;
static trns_free_func print_trns_free;
static int parse_specs (void);
static void dump_table (const struct file_handle *);
static void append_var_spec (struct prt_out_spec *);
static void alloc_line (void);

/* Basic parsing. */

/* Parses PRINT command. */
int
cmd_print (void)
{
  return internal_cmd_print (PRT_PRINT);
}

/* Parses PRINT EJECT command. */
int
cmd_print_eject (void)
{
  return internal_cmd_print (PRT_PRINT | PRT_EJECT);
}

/* Parses WRITE command. */
int
cmd_write (void)
{
  return internal_cmd_print (PRT_WRITE);
}

/* Parses the output commands.  F is PRT_PRINT, PRT_WRITE, or
   PRT_PRINT|PRT_EJECT. */
static int
internal_cmd_print (int f)
{
  int table = 0;                /* Print table? */
  struct print_trns *trns = NULL; /* malloc()'d transformation. */
  struct file_handle *fh = NULL;

  /* Fill in prt to facilitate error-handling. */
  prt.writer = NULL;
  prt.options = f;
  prt.spec = NULL;
  prt.line = NULL;
  next = NULL;
  nrec = 0;

  which_cmd = f & PRT_CMD_MASK;

  /* Parse the command options. */
  while (!lex_match ('/'))
    {
      if (lex_match_id ("OUTFILE"))
	{
	  lex_match ('=');

	  fh = fh_parse (FH_REF_FILE);
	  if (fh == NULL)
	    goto error;
	}
      else if (lex_match_id ("RECORDS"))
	{
	  lex_match ('=');
	  lex_match ('(');
	  if (!lex_force_int ())
	    goto error;
	  nrec = lex_integer ();
	  lex_get ();
	  lex_match (')');
	}
      else if (lex_match_id ("TABLE"))
	table = 1;
      else if (lex_match_id ("NOTABLE"))
	table = 0;
      else
	{
	  lex_error (_("expecting a valid subcommand"));
	  goto error;
	}
    }

  /* Parse variables and strings. */
  if (!parse_specs ())
    goto error;

  if (fh != NULL)
    {
      prt.writer = dfm_open_writer (fh);
      if (prt.writer == NULL)
        goto error;

      if (fh_get_mode (fh) == FH_MODE_BINARY)
        prt.options |= PRT_BINARY;
    }

  /* Output the variable table if requested. */
  if (table)
    dump_table (fh);

  /* Count the maximum line width.  Allocate linebuffer if
     applicable. */
  alloc_line ();

  /* Put the transformation in the queue. */
  trns = xmalloc (sizeof *trns);
  memcpy (trns, &prt, sizeof *trns);
  add_transformation (print_trns_proc, print_trns_free, trns);

  return CMD_SUCCESS;

 error:
  print_trns_free (&prt);
  return CMD_FAILURE;
}

/* Appends the field output specification SPEC to the list maintained
   in prt. */
static void
append_var_spec (struct prt_out_spec *spec)
{
  if (next == 0)
    prt.spec = next = xmalloc (sizeof *spec);
  else
    next = next->next = xmalloc (sizeof *spec);

  memcpy (next, spec, sizeof *spec);
  next->next = NULL;
}

/* Field parsing.  Mostly stolen from data-list.c. */

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
    struct variable **v;		/* variable list */
    size_t nv;			/* number of variables in list */
    size_t cv;			/* number of variables from list used up so far
				   by the FORTRAN-like format specifiers */

    int recno;			/* current 1-based record number */
    int sc;			/* 1-based starting column for next variable */

    struct prt_out_spec spec;		/* next format spec to append to list */
    int fc, lc;			/* first, last 1-based column number of current
				   var */

    int level;			/* recursion level for FORTRAN-like format
				   specifiers */
  }
fx;

static int fixed_parse_compatible (void);
static struct fmt_list *fixed_parse_fortran (void);

static int parse_string_argument (void);
static int parse_variable_argument (void);

/* Parses all the variable and string specifications on a single
   PRINT, PRINT EJECT, or WRITE command into the prt structure.
   Returns success. */
static int
parse_specs (void)
{
  /* Return code from called function. */
  int code;

  fx.recno = 1;
  fx.sc = 1;

  while (token != '.')
    {
      while (lex_match ('/'))
	{
	  int prev_recno = fx.recno;

	  fx.recno++;
	  if (lex_is_number ())
	    {
	      if (!lex_force_int ())
		return 0;
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

	  fx.spec.type = PRT_NEWLINE;
	  while (prev_recno++ < fx.recno)
	    append_var_spec (&fx.spec);

	  fx.sc = 1;
	}

      if (token == T_STRING)
	code = parse_string_argument ();
      else
	code = parse_variable_argument ();
      if (!code)
	return 0;
    }
  fx.spec.type = PRT_NEWLINE;
  append_var_spec (&fx.spec);

  if (!nrec)
    nrec = fx.recno;
  else if (fx.recno > nrec)
    {
      msg (SE, _("Variables are specified on records that "
		 "should not exist according to RECORDS subcommand."));
      return 0;
    }
      
  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      return 0;
    }
  
  return 1;
}

/* Parses a string argument to the PRINT commands.  Returns success. */
static int
parse_string_argument (void)
{
  fx.spec.type = PRT_CONST;
  fx.spec.fc = fx.sc - 1;
  fx.spec.u.c = xstrdup (ds_c_str (&tokstr));
  lex_get ();

  /* Parse the included column range. */
  if (lex_is_number ())
    {
      /* Width of column range in characters. */
      int c_len;

      /* Width of constant string in characters. */
      int s_len;

      /* 1-based index of last column in range. */
      int lc;

      if (!lex_is_integer () || lex_integer () <= 0)
	{
	  msg (SE, _("%g is not a valid column location."), tokval);
	  goto fail;
	}
      fx.spec.fc = lex_integer () - 1;

      lex_get ();
      lex_negative_to_dash ();
      if (lex_match ('-'))
	{
	  if (!lex_is_integer ())
	    {
	      msg (SE, _("Column location expected following `%d-'."),
		   fx.spec.fc + 1);
	      goto fail;
	    }
	  if (lex_integer () <= 0)
	    {
	      msg (SE, _("%g is not a valid column location."), tokval);
	      goto fail;
	    }
	  if (lex_integer () < fx.spec.fc + 1)
	    {
	      msg (SE, _("%d-%ld is not a valid column range.  The second "
		   "column must be greater than or equal to the first."),
		   fx.spec.fc + 1, lex_integer ());
	      goto fail;
	    }
	  lc = lex_integer () - 1;

	  lex_get ();
	}
      else
	/* If only a starting location is specified then the field is
	   the width of the provided string. */
	lc = fx.spec.fc + strlen (fx.spec.u.c) - 1;

      /* Apply the range. */
      c_len = lc - fx.spec.fc + 1;
      s_len = strlen (fx.spec.u.c);
      if (s_len > c_len)
	fx.spec.u.c[c_len] = 0;
      else if (s_len < c_len)
	{
	  fx.spec.u.c = xrealloc (fx.spec.u.c, c_len + 1);
	  memset (&fx.spec.u.c[s_len], ' ', c_len - s_len);
	  fx.spec.u.c[c_len] = 0;
	}

      fx.sc = lc + 1;
    }
  else
    /* If nothing is provided then the field is the width of the
       provided string. */
    fx.sc += strlen (fx.spec.u.c);

  append_var_spec (&fx.spec);
  return 1;

fail:
  free (fx.spec.u.c);
  return 0;
}

/* Parses a variable argument to the PRINT commands by passing it off
   to fixed_parse_compatible() or fixed_parse_fortran() as appropriate.
   Returns success. */
static int
parse_variable_argument (void)
{
  if (!parse_variables (default_dict, &fx.v, &fx.nv, PV_DUPLICATE))
    return 0;

  if (lex_is_number ())
    {
      if (!fixed_parse_compatible ())
	goto fail;
    }
  else if (token == '(')
    {
      fx.level = 0;
      fx.cv = 0;
      if (!fixed_parse_fortran ())
	goto fail;
    }
  else
    {
      /* User wants dictionary format specifiers. */
      size_t i;

      lex_match ('*');
      for (i = 0; i < fx.nv; i++)
	{
	  /* Variable. */
	  fx.spec.type = PRT_VAR;
	  fx.spec.fc = fx.sc - 1;
	  fx.spec.u.v.v = fx.v[i];
	  fx.spec.u.v.f = fx.v[i]->print;
	  append_var_spec (&fx.spec);
	  fx.sc += fx.v[i]->print.w;

	  /* Space. */
	  fx.spec.type = PRT_SPACE;
	  fx.spec.fc = fx.sc - 1;
	  append_var_spec (&fx.spec);
	  fx.sc++;
	}
    }

  free (fx.v);
  return 1;

fail:
  free (fx.v);
  return 0;
}

/* Verifies that FORMAT doesn't need a variable wider than WIDTH.
   Returns true iff that is the case. */
static bool
check_string_width (const struct fmt_spec *format, const struct variable *v) 
{
  if (get_format_var_width (format) > v->width)
    {
      msg (SE, _("Variable %s has width %d so it cannot be output "
                 "as format %s."),
           v->name, v->width, fmt_to_string (format));
      return false;
    }
  return true;
}

/* Parses a column specification for parse_specs(). */
static int
fixed_parse_compatible (void)
{
  int individual_var_width;
  int type;
  size_t i;

  type = fx.v[0]->type;
  for (i = 1; i < fx.nv; i++)
    if (type != fx.v[i]->type)
      {
	msg (SE, _("%s is not of the same type as %s.  To specify "
		   "variables of different types in the same variable "
		   "list, use a FORTRAN-like format specifier."),
	     fx.v[i]->name, fx.v[0]->name);
	return 0;
      }

  if (!lex_force_int ())
    return 0;
  fx.fc = lex_integer () - 1;
  if (fx.fc < 0)
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
      fx.lc = lex_integer () - 1;
      if (fx.lc < 0)
	{
	  msg (SE, _("Column positions for fields must be positive."));
	  return 0;
	}
      else if (fx.lc < fx.fc)
	{
	  msg (SE, _("The ending column for a field must not "
		     "be less than the starting column."));
	  return 0;
	}
      lex_get ();
    }
  else
    fx.lc = fx.fc;

  fx.spec.u.v.f.w = fx.lc - fx.fc + 1;
  if (lex_match ('('))
    {
      struct fmt_desc *fdp;

      if (token == T_ID)
	{
	  const char *cp;

	  fx.spec.u.v.f.type = parse_format_specifier_name (&cp, 0);
	  if (fx.spec.u.v.f.type == -1)
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
	fx.spec.u.v.f.type = FMT_F;

      if (lex_is_number ())
	{
	  if (!lex_force_int ())
	    return 0;
	  if (lex_integer () < 1)
	    {
	      msg (SE, _("The value for number of decimal places "
			 "must be at least 1."));
	      return 0;
	    }
	  fx.spec.u.v.f.d = lex_integer ();
	  lex_get ();
	}
      else
	fx.spec.u.v.f.d = 0;

      fdp = &formats[fx.spec.u.v.f.type];
      if (fdp->n_args < 2 && fx.spec.u.v.f.d)
	{
	  msg (SE, _("Input format %s doesn't accept decimal places."),
	       fdp->name);
	  return 0;
	}
      if (fx.spec.u.v.f.d > 16)
	fx.spec.u.v.f.d = 16;

      if (!lex_force_match (')'))
	return 0;
    }
  else
    {
      fx.spec.u.v.f.type = FMT_F;
      fx.spec.u.v.f.d = 0;
    }

  fx.sc = fx.lc + 1;

  if ((fx.lc - fx.fc + 1) % fx.nv)
    {
      msg (SE, _("The %d columns %d-%d can't be evenly divided into %u "
		 "fields."),
           fx.lc - fx.fc + 1, fx.fc + 1, fx.lc + 1, (unsigned) fx.nv);
      return 0;
    }

  individual_var_width = (fx.lc - fx.fc + 1) / fx.nv;
  fx.spec.u.v.f.w = individual_var_width;
  if (!check_output_specifier (&fx.spec.u.v.f, true)
      || !check_specifier_type (&fx.spec.u.v.f, type, true))
    return 0;
  if (type == ALPHA)
    {
      for (i = 0; i < fx.nv; i++)
        if (!check_string_width (&fx.spec.u.v.f, fx.v[i]))
          return false;
    }

  fx.spec.type = PRT_VAR;
  for (i = 0; i < fx.nv; i++)
    {
      fx.spec.fc = fx.fc + individual_var_width * i;
      fx.spec.u.v.v = fx.v[i];
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

/* Recursively puts the format list F (which represents a set of
   FORTRAN-like format specifications, like 4(F10,2X)) into the
   structure prt. */
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
	fx.spec.type = PRT_NEWLINE;
	for (i = 0; i < f->count; i++)
	  append_var_spec (&fx.spec);
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
	    struct variable *v;

	    if (fx.cv >= fx.nv)
	      {
		msg (SE, _("The number of format "
			   "specifications exceeds the number of variable "
			   "names given."));
		return 0;
	      }

	    v = fx.v[fx.cv++];
            if (!check_output_specifier (&f->f, true)
                || !check_specifier_type (&f->f, v->type, true)
                || !check_string_width (&f->f, v))
              return false;

	    fx.spec.type = PRT_VAR;
	    fx.spec.u.v.v = v;
	    fx.spec.u.v.f = f->f;
	    fx.spec.fc = fx.sc - 1;
	    append_var_spec (&fx.spec);

	    fx.sc += f->f.w;
	  }
  return 1;
}

/* Recursively parses a list of FORTRAN-like format specifiers.  Calls
   itself to parse nested levels of parentheses.  Returns to its
   original caller NULL, to indicate error, non-NULL, but nothing
   useful, to indicate success (it returns a free()'d block). */
static struct fmt_list *
fixed_parse_fortran (void)
{
  struct fmt_list *head = NULL;
  struct fmt_list *fl = NULL;

  lex_get ();			/* skip opening parenthesis */
  while (token != ')')
    {
      if (fl)
	fl = fl->next = xmalloc (sizeof *fl);
      else
	head = fl = xmalloc (sizeof *fl);

      if (lex_is_number ())
	{
	  if (!lex_is_integer ())
	    goto fail;
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
      else if (!parse_format_specifier (&fl->f, FMTP_ALLOW_XT)
	       || !check_output_specifier (&fl->f, 1))
	goto fail;

      lex_match (',');
    }
  fl->next = NULL;
  lex_get ();

  if (fx.level)
    return head;

  fl->next = NULL;
  dump_fmt_list (head);
  destroy_fmt_list (head, 1);
  if (fx.cv < fx.nv)
    {
      msg (SE, _("There aren't enough format specifications "
	   "to match the number of variable names given."));
      goto fail;
    }
  return head;

fail:
  fl->next = NULL;
  destroy_fmt_list (head, 0);

  return NULL;
}

/* Prints the table produced by the TABLE subcommand to the listing
   file. */
static void
dump_table (const struct file_handle *fh)
{
  struct prt_out_spec *spec;
  struct tab_table *t;
  int recno;
  int nspec;

  for (nspec = 0, spec = prt.spec; spec; spec = spec->next)
    if (spec->type == PRT_CONST || spec->type == PRT_VAR)
      nspec++;
  t = tab_create (4, nspec + 1, 0);
  tab_columns (t, TAB_COL_DOWN, 1);
  tab_box (t, TAL_1, TAL_1, TAL_0, TAL_1, 0, 0, 3, nspec);
  tab_hline (t, TAL_2, 0, 3, 1);
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_CENTER | TAT_TITLE, _("Variable"));
  tab_text (t, 1, 0, TAB_CENTER | TAT_TITLE, _("Record"));
  tab_text (t, 2, 0, TAB_CENTER | TAT_TITLE, _("Columns"));
  tab_text (t, 3, 0, TAB_CENTER | TAT_TITLE, _("Format"));
  tab_dim (t, tab_natural_dimensions);
  for (nspec = recno = 0, spec = prt.spec; spec; spec = spec->next)
    switch (spec->type)
      {
      case PRT_NEWLINE:
	recno++;
	break;
      case PRT_CONST:
	{
	  int len = strlen (spec->u.c);
	  nspec++;
	  tab_text (t, 0, nspec, TAB_LEFT | TAB_FIX | TAT_PRINTF,
			"\"%s\"", spec->u.c);
	  tab_text (t, 1, nspec, TAT_PRINTF, "%d", recno + 1);
	  tab_text (t, 2, nspec, TAT_PRINTF, "%3d-%3d",
			spec->fc + 1, spec->fc + len);
	  tab_text (t, 3, nspec, TAB_LEFT | TAB_FIX | TAT_PRINTF,
			"A%d", len);
	  break;
	}
      case PRT_VAR:
	{
	  nspec++;
	  tab_text (t, 0, nspec, TAB_LEFT, spec->u.v.v->name);
	  tab_text (t, 1, nspec, TAT_PRINTF, "%d", recno + 1);
	  tab_text (t, 2, nspec, TAT_PRINTF, "%3d-%3d",
			spec->fc + 1, spec->fc + spec->u.v.f.w);
	  tab_text (t, 3, nspec, TAB_LEFT | TAB_FIX,
			fmt_to_string (&spec->u.v.f));
	  break;
	}
      case PRT_SPACE:
	break;
      case PRT_ERROR:
	assert (0);
      }

  if (fh != NULL)
    tab_title (t, ngettext ("Writing %d record to %s.",
                            "Writing %d records to %s.", recno),
               recno, fh_get_name (fh));
  else
    tab_title (t, ngettext ("Writing %d record.",
                            "Writing %d records.", recno), recno);
  tab_submit (t);
}

/* Calculates the maximum possible line width and allocates a buffer
   big enough to contain it */
static void
alloc_line (void)
{
  /* Cumulative maximum line width (excluding null terminator) so far. */
  int w = 0;

  /* Width required by current this prt_out_spec. */
  int pot_w;			/* Potential w. */

  /* Iterator. */
  struct prt_out_spec *i;

  for (i = prt.spec; i; i = i->next)
    {
      switch (i->type)
	{
	case PRT_NEWLINE:
	  pot_w = 0;
	  break;
	case PRT_CONST:
	  pot_w = i->fc + strlen (i->u.c);
	  break;
	case PRT_VAR:
	  pot_w = i->fc + i->u.v.f.w;
	  break;
	case PRT_SPACE:
	  pot_w = i->fc + 1;
	  break;
	case PRT_ERROR:
        default:
	  assert (0);
          abort ();
	}
      if (pot_w > w)
	w = pot_w;
    }
  prt.max_width = w + 2;
  prt.line = xmalloc (prt.max_width);
}

/* Transformation. */

/* Performs the transformation inside print_trns T on case C. */
static int
print_trns_proc (void *trns_, struct ccase *c, int case_num UNUSED)
{
  /* Transformation. */
  struct print_trns *t = trns_;

  /* Iterator. */
  struct prt_out_spec *i;

  /* Line buffer. */
  char *buf = t->line;

  /* Length of the line in buf. */
  int len = 0;
  memset (buf, ' ', t->max_width);

  if (t->options & PRT_EJECT)
    som_eject_page ();

  /* Note that a field written to a place where a field has
     already been written truncates the record.  `PRINT /A B
     (T10,F8,T1,F8).' only outputs B.  */
  for (i = t->spec; i; i = i->next)
    switch (i->type)
      {
      case PRT_NEWLINE:
	if (t->writer == NULL)
	  {
	    buf[len] = 0;
	    tab_output_text (TAB_FIX | TAT_NOWRAP, buf);
	  }
	else
	  {
	    if ((t->options & PRT_CMD_MASK) == PRT_PRINT
                || !(t->options & PRT_BINARY))
              buf[len++] = '\n';

	    dfm_put_record (t->writer, buf, len);
	  }

	memset (buf, ' ', t->max_width);
	len = 0;
	break;

      case PRT_CONST:
	/* FIXME: Should be revised to keep track of the string's
	   length outside the loop, probably in i->u.c[0]. */
	memcpy (&buf[i->fc], i->u.c, strlen (i->u.c));
	len = i->fc + strlen (i->u.c);
	break;

      case PRT_VAR:
        data_out (&buf[i->fc], &i->u.v.f, case_data (c, i->u.v.v->fv));
	len = i->fc + i->u.v.f.w;
	break;

      case PRT_SPACE:
	/* PRT_SPACE always immediately follows PRT_VAR. */
	buf[len++] = ' ';
	break;

      case PRT_ERROR:
	assert (0);
	break;
      }

  if (t->writer != NULL && dfm_write_error (t->writer))
    return TRNS_ERROR;
  return TRNS_CONTINUE;
}

/* Frees all the data inside print_trns PRT.  Does not free PRT. */
static bool
print_trns_free (void *prt_)
{
  struct print_trns *prt = prt_;
  struct prt_out_spec *i, *n;
  bool ok = true;

  for (i = prt->spec; i; i = n)
    {
      switch (i->type)
	{
	case PRT_CONST:
	  free (i->u.c);
	  /* fall through */
	case PRT_NEWLINE:
	case PRT_VAR:
	case PRT_SPACE:
	  /* nothing to do */
	  break;
	case PRT_ERROR:
	  assert (0);
	  break;
	}
      n = i->next;
      free (i);
    }
  if (prt->writer != NULL)
    ok = dfm_close_writer (prt->writer);
  free (prt->line);
  return ok;
}

/* PRINT SPACE. */

/* PRINT SPACE transformation. */
struct print_space_trns
{
  struct dfm_writer *writer;    /* Output data file. */
  struct expression *e;		/* Number of lines; NULL=1. */
}
print_space_trns;

static trns_proc_func print_space_trns_proc;
static trns_free_func print_space_trns_free;

int
cmd_print_space (void)
{
  struct print_space_trns *t;
  struct file_handle *fh;
  struct expression *e;
  struct dfm_writer *writer;

  if (lex_match_id ("OUTFILE"))
    {
      lex_match ('=');

      fh = fh_parse (FH_REF_FILE);
      if (fh == NULL)
	return CMD_FAILURE;
      lex_get ();
    }
  else
    fh = NULL;

  if (token != '.')
    {
      e = expr_parse (default_dict, EXPR_NUMBER);
      if (token != '.')
	{
	  expr_free (e);
	  lex_error (_("expecting end of command"));
	  return CMD_FAILURE;
	}
    }
  else
    e = NULL;

  if (fh != NULL)
    {
      writer = dfm_open_writer (fh);
      if (writer == NULL) 
        {
          expr_free (e);
          return CMD_FAILURE;
        } 
    }
  else
    writer = NULL;
  
  t = xmalloc (sizeof *t);
  t->writer = writer;
  t->e = e;

  add_transformation (print_space_trns_proc, print_space_trns_free, t);
  return CMD_SUCCESS;
}

/* Executes a PRINT SPACE transformation. */
static int
print_space_trns_proc (void *t_, struct ccase *c,
                       int case_num UNUSED)
{
  struct print_space_trns *t = t_;
  int n;

  n = 1;
  if (t->e)
    {
      double f = expr_evaluate_num (t->e, c, case_num);
      if (f == SYSMIS) 
        msg (SW, _("The expression on PRINT SPACE evaluated to the "
                   "system-missing value."));
      else if (f < 0 || f > INT_MAX)
        msg (SW, _("The expression on PRINT SPACE evaluated to %g."), f);
      else
        n = f;
    }

  while (n--)
    if (t->writer == NULL)
      som_blank_line ();
    else
      dfm_put_record (t->writer, "\n", 1);

  if (t->writer != NULL && dfm_write_error (t->writer))
    return TRNS_ERROR;
  return TRNS_CONTINUE;
}

/* Frees a PRINT SPACE transformation.
   Returns true if successful, false if an I/O error occurred. */
static bool
print_space_trns_free (void *trns_)
{
  struct print_space_trns *trns = trns_;
  bool ok = dfm_close_writer (trns->writer);
  expr_free (trns->e);
  free (trns);
  return ok;
}
