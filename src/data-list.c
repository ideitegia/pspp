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
#include "data-list.h"
#include "error.h"
#include <ctype.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include "alloc.h"
#include "case.h"
#include "command.h"
#include "data-in.h"
#include "debug-print.h"
#include "dfm-read.h"
#include "dictionary.h"
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
    struct dls_var_spec *next;  /* Next specification in list. */

    /* Both free and fixed formats. */
    struct fmt_spec input;	/* Input format of this field. */
    struct variable *v;		/* Associated variable.  Used only in
				   parsing.  Not safe later. */
    int fv;			/* First value in case. */

    /* Fixed format only. */
    int rec;			/* Record number (1-based). */
    int fc, lc;			/* Column numbers in record. */

    /* Free format only. */
    char name[LONG_NAME_LEN + 1]; /* Name of variable. */
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

    struct dls_var_spec *first, *last;	/* Variable parsing specifications. */
    struct dfm_reader *reader;  /* Data file reader. */

    int type;			/* A DLS_* constant. */
    struct variable *end;	/* Variable specified on END subcommand. */
    int eof;			/* End of file encountered. */
    int rec_cnt;                /* Number of records. */
    size_t case_size;           /* Case size in bytes. */
    char *delims;               /* Delimiters if any; not null-terminated. */
    size_t delim_cnt;           /* Number of delimiter, or 0 for spaces. */
  };

static int parse_fixed (struct data_list_pgm *);
static int parse_free (struct dls_var_spec **, struct dls_var_spec **);
static void dump_fixed_table (const struct dls_var_spec *,
                              const struct file_handle *, int rec_cnt);
static void dump_free_table (const struct data_list_pgm *,
                             const struct file_handle *);
static void destroy_dls_var_spec (struct dls_var_spec *);
static trns_free_func data_list_trns_free;
static trns_proc_func data_list_trns_proc;

/* Message title for REPEATING DATA. */
#define RPD_ERR "REPEATING DATA: "

int
cmd_data_list (void)
{
  struct data_list_pgm *dls;     /* DATA LIST program under construction. */
  int table = -1;                /* Print table if nonzero, -1=undecided. */
  struct file_handle *fh = NULL; /* File handle of source, NULL=inline file. */

  if (!case_source_is_complex (vfm_source))
    discard_variables ();

  dls = xmalloc (sizeof *dls);
  dls->reader = NULL;
  dls->type = -1;
  dls->end = NULL;
  dls->eof = 0;
  dls->rec_cnt = 0;
  dls->delims = NULL;
  dls->delim_cnt = 0;
  dls->first = dls->last = NULL;

  while (token != '/')
    {
      if (lex_match_id ("FILE"))
	{
	  lex_match ('=');
	  fh = fh_parse ();
	  if (fh == NULL)
	    goto error;
	  if (case_source_is_class (vfm_source, &file_type_source_class)
              && fh != default_handle)
	    {
	      msg (SE, _("DATA LIST may not use a different file from "
			 "that specified on its surrounding FILE TYPE."));
	      goto error;
	    }
	}
      else if (lex_match_id ("RECORDS"))
	{
	  lex_match ('=');
	  lex_match ('(');
	  if (!lex_force_int ())
	    goto error;
	  dls->rec_cnt = lex_integer ();
	  lex_get ();
	  lex_match (')');
	}
      else if (lex_match_id ("END"))
	{
	  if (dls->end)
	    {
	      msg (SE, _("The END subcommand may only be specified once."));
	      goto error;
	    }
	  
	  lex_match ('=');
	  if (!lex_force_id ())
	    goto error;
	  dls->end = dict_lookup_var (default_dict, tokid);
	  if (!dls->end) 
            dls->end = dict_create_var_assert (default_dict, tokid, 0);
	  lex_get ();
	}
      else if (token == T_ID)
	{
          if (lex_match_id ("NOTABLE"))
            table = 0;
          else if (lex_match_id ("TABLE"))
            table = 1;
          else 
            {
              int type;
              if (lex_match_id ("FIXED"))
                type = DLS_FIXED;
              else if (lex_match_id ("FREE"))
                type = DLS_FREE;
              else if (lex_match_id ("LIST"))
                type = DLS_LIST;
              else 
                {
                  lex_error (NULL);
                  goto error;
                }

	      if (dls->type != -1)
		{
		  msg (SE, _("Only one of FIXED, FREE, or LIST may "
                             "be specified."));
		  goto error;
		}
	      dls->type = type;

              if ((dls->type == DLS_FREE || dls->type == DLS_LIST)
                  && lex_match ('(')) 
                {
                  while (!lex_match (')'))
                    {
                      int delim;

                      if (lex_match_id ("TAB"))
                        delim = '\t';
                      else if (token == T_STRING && tokstr.length == 1)
			{
			  delim = tokstr.string[0];
			  lex_get();
			}
                      else 
                        {
                          lex_error (NULL);
                          goto error;
                        }

                      dls->delims = xrealloc (dls->delims, dls->delim_cnt + 1);
                      dls->delims[dls->delim_cnt++] = delim;

                      lex_match (',');
                    }
                }
            }
        }
      else
	{
	  lex_error (NULL);
	  goto error;
	}
    }

  dls->case_size = dict_get_case_size (default_dict);
  default_handle = fh;

  if (dls->type == -1)
    dls->type = DLS_FIXED;

  if (table == -1)
    {
      if (dls->type == DLS_FREE)
	table = 0;
      else
	table = 1;
    }

  if (dls->type == DLS_FIXED)
    {
      if (!parse_fixed (dls))
	goto error;
      if (table)
	dump_fixed_table (dls->first, fh, dls->rec_cnt);
    }
  else
    {
      if (!parse_free (&dls->first, &dls->last))
	goto error;
      if (table)
	dump_free_table (dls, fh);
    }

  dls->reader = dfm_open_reader (fh);
  if (dls->reader == NULL)
    goto error;

  if (vfm_source != NULL)
    {
      dls->h.proc = data_list_trns_proc;
      dls->h.free = data_list_trns_free;
      add_transformation (&dls->h);
    }
  else 
    vfm_source = create_case_source (&data_list_source_class, dls);

  return CMD_SUCCESS;

 error:
  destroy_dls_var_spec (dls->first);
  free (dls->delims);
  free (dls);
  return CMD_FAILURE;
}

/* Adds SPEC to the linked list with head at FIRST and tail at
   LAST. */
static void
append_var_spec (struct dls_var_spec **first, struct dls_var_spec **last,
                 struct dls_var_spec *spec)
{
  spec->next = NULL;

  if (*first == NULL)
    *first = spec;
  else 
    (*last)->next = spec;
  *last = spec;
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

/* State of parsing DATA LIST. */
struct fixed_parsing_state
  {
    char **name;		/* Variable names. */
    int name_cnt;		/* Number of names. */

    int recno;			/* Index of current record. */
    int sc;			/* 1-based column number of starting column for
				   next field to output. */
  };

static int fixed_parse_compatible (struct fixed_parsing_state *,
                                   struct dls_var_spec **,
                                   struct dls_var_spec **);
static int fixed_parse_fortran (struct fixed_parsing_state *,
                                struct dls_var_spec **,
                                struct dls_var_spec **);

/* Parses all the variable specifications for DATA LIST FIXED,
   storing them into DLS.  Returns nonzero if successful. */
static int
parse_fixed (struct data_list_pgm *dls)
{
  struct fixed_parsing_state fx;
  int i;

  fx.recno = 0;
  fx.sc = 1;

  while (token != '.')
    {
      while (lex_match ('/'))
	{
	  fx.recno++;
	  if (lex_is_integer ())
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

      if (!parse_DATA_LIST_vars (&fx.name, &fx.name_cnt, PV_NONE))
	return 0;

      if (lex_is_number ())
	{
	  if (!fixed_parse_compatible (&fx, &dls->first, &dls->last))
	    goto fail;
	}
      else if (token == '(')
	{
	  if (!fixed_parse_fortran (&fx, &dls->first, &dls->last))
	    goto fail;
	}
      else
	{
	  msg (SE, _("SPSS-like or FORTRAN-like format "
                     "specification expected after variable names."));
	  goto fail;
	}

      for (i = 0; i < fx.name_cnt; i++)
	free (fx.name[i]);
      free (fx.name);
    }
  if (dls->first == NULL) 
    {
      msg (SE, _("At least one variable must be specified."));
      return 0;
    }
  if (dls->rec_cnt && dls->last->rec > dls->rec_cnt)
    {
      msg (SE, _("Variables are specified on records that "
		 "should not exist according to RECORDS subcommand."));
      return 0;
    }
  else if (!dls->rec_cnt)
    dls->rec_cnt = dls->last->rec;
  return lex_end_of_command () == CMD_SUCCESS;

fail:
  for (i = 0; i < fx.name_cnt; i++)
    free (fx.name[i]);
  free (fx.name);
  return 0;
}

/* Parses a variable specification in the form 1-10 (A) based on
   FX and adds specifications to the linked list with head at
   FIRST and tail at LAST. */
static int
fixed_parse_compatible (struct fixed_parsing_state *fx,
                        struct dls_var_spec **first, struct dls_var_spec **last)
{
  struct fmt_spec input;
  int fc, lc;
  int width;
  int i;

  /* First column. */
  if (!lex_force_int ())
    return 0;
  fc = lex_integer ();
  if (fc < 1)
    {
      msg (SE, _("Column positions for fields must be positive."));
      return 0;
    }
  lex_get ();

  /* Last column. */
  lex_negative_to_dash ();
  if (lex_match ('-'))
    {
      if (!lex_force_int ())
	return 0;
      lc = lex_integer ();
      if (lc < 1)
	{
	  msg (SE, _("Column positions for fields must be positive."));
	  return 0;
	}
      else if (lc < fc)
	{
	  msg (SE, _("The ending column for a field must be "
		     "greater than the starting column."));
	  return 0;
	}
      
      lex_get ();
    }
  else
    lc = fc;

  /* Divide columns evenly. */
  input.w = (lc - fc + 1) / fx->name_cnt;
  if ((lc - fc + 1) % fx->name_cnt)
    {
      msg (SE, _("The %d columns %d-%d "
		 "can't be evenly divided into %d fields."),
	   lc - fc + 1, fc, lc, fx->name_cnt);
      return 0;
    }

  /* Format specifier. */
  if (lex_match ('('))
    {
      struct fmt_desc *fdp;

      if (token == T_ID)
	{
	  const char *cp;

	  input.type = parse_format_specifier_name (&cp, 0);
	  if (input.type == -1)
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
	input.type = FMT_F;

      if (lex_is_integer ())
	{
	  if (lex_integer () < 1)
	    {
	      msg (SE, _("The value for number of decimal places "
			 "must be at least 1."));
	      return 0;
	    }
	  
	  input.d = lex_integer ();
	  lex_get ();
	}
      else
	input.d = 0;

      fdp = &formats[input.type];
      if (fdp->n_args < 2 && input.d)
	{
	  msg (SE, _("Input format %s doesn't accept decimal places."),
	       fdp->name);
	  return 0;
	}
      
      if (input.d > 16)
	input.d = 16;

      if (!lex_force_match (')'))
	return 0;
    }
  else
    {
      input.type = FMT_F;
      input.d = 0;
    }
  if (!check_input_specifier (&input, 1))
    return 0;

  /* Start column for next specification. */
  fx->sc = lc + 1;

  /* Width of variables to create. */
  if (input.type == FMT_A || input.type == FMT_AHEX) 
    width = input.w;
  else
    width = 0;

  /* Create variables and var specs. */
  for (i = 0; i < fx->name_cnt; i++)
    {
      struct dls_var_spec *spec;
      struct variable *v;

      v = dict_create_var (default_dict, fx->name[i], width);
      if (v != NULL)
	{
	  convert_fmt_ItoO (&input, &v->print);
	  v->write = v->print;
          if (!case_source_is_complex (vfm_source))
            v->init = 0;
	}
      else
	{
	  v = dict_lookup_var_assert (default_dict, fx->name[i]);
	  if (vfm_source == NULL)
	    {
	      msg (SE, _("%s is a duplicate variable name."), fx->name[i]);
	      return 0;
	    }
	  if ((width != 0) != (v->width != 0))
	    {
	      msg (SE, _("There is already a variable %s of a "
			 "different type."),
		   fx->name[i]);
	      return 0;
	    }
	  if (width != 0 && width != v->width)
	    {
	      msg (SE, _("There is already a string variable %s of a "
			 "different width."), fx->name[i]);
	      return 0;
	    }
	}

      spec = xmalloc (sizeof *spec);
      spec->input = input;
      spec->v = v;
      spec->fv = v->fv;
      spec->rec = fx->recno;
      spec->fc = fc + input.w * i;
      spec->lc = spec->fc + input.w - 1;
      append_var_spec (first, last, spec);
    }
  return 1;
}

/* Destroy format list F and, if RECURSE is nonzero, all its
   sublists. */
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
   fixed_parse_fortran(), and flattens it, adding the variable
   specifications to the linked list with head FIRST and tail
   LAST.  NAME_IDX is used to take values from the list of names
   in FX; it should initially point to a value of 0. */
static int
dump_fmt_list (struct fixed_parsing_state *fx, struct fmt_list *f,
               struct dls_var_spec **first, struct dls_var_spec **last,
               int *name_idx)
{
  int i;

  for (; f; f = f->next)
    if (f->f.type == FMT_X)
      fx->sc += f->count;
    else if (f->f.type == FMT_T)
      fx->sc = f->f.w;
    else if (f->f.type == FMT_NEWREC)
      {
	fx->recno += f->count;
	fx->sc = 1;
      }
    else
      for (i = 0; i < f->count; i++)
	if (f->f.type == FMT_DESCEND)
	  {
	    if (!dump_fmt_list (fx, f->down, first, last, name_idx))
	      return 0;
	  }
	else
	  {
            struct dls_var_spec *spec;
            int width;
	    struct variable *v;

            if (formats[f->f.type].cat & FCAT_STRING) 
              width = f->f.w;
            else
              width = 0;
	    if (*name_idx >= fx->name_cnt)
	      {
		msg (SE, _("The number of format "
			   "specifications exceeds the given number of "
			   "variable names."));
		return 0;
	      }
	    
	    v = dict_create_var (default_dict, fx->name[(*name_idx)++], width);
	    if (!v)
	      {
		msg (SE, _("%s is a duplicate variable name."), fx->name[i]);
		return 0;
	      }
	    
            if (!case_source_is_complex (vfm_source))
              v->init = 0;

            spec = xmalloc (sizeof *spec);
            spec->v = v;
	    spec->input = f->f;
	    spec->fv = v->fv;
	    spec->rec = fx->recno;
	    spec->fc = fx->sc;
	    spec->lc = fx->sc + f->f.w - 1;
	    append_var_spec (first, last, spec);

	    convert_fmt_ItoO (&spec->input, &v->print);
	    v->write = v->print;

	    fx->sc += f->f.w;
	  }
  return 1;
}

/* Recursively parses a FORTRAN-like format specification into
   the linked list with head FIRST and tail TAIL.  LEVEL is the
   level of recursion, starting from 0.  Returns the parsed
   specification if successful, or a null pointer on failure.  */
static struct fmt_list *
fixed_parse_fortran_internal (struct fixed_parsing_state *fx,
                              struct dls_var_spec **first,
                              struct dls_var_spec **last)
{
  struct fmt_list *head = NULL;
  struct fmt_list *tail = NULL;

  lex_force_match ('(');
  while (token != ')')
    {
      /* New fmt_list. */
      struct fmt_list *new = xmalloc (sizeof *new);
      new->next = NULL;

      /* Append new to list. */
      if (head != NULL)
	tail->next = new;
      else
	head = new;
      tail = new;

      /* Parse count. */
      if (lex_is_integer ())
	{
	  new->count = lex_integer ();
	  lex_get ();
	}
      else
	new->count = 1;

      /* Parse format specifier. */
      if (token == '(')
	{
	  new->f.type = FMT_DESCEND;
	  new->down = fixed_parse_fortran_internal (fx, first, last);
	  if (new->down == NULL)
	    goto fail;
	}
      else if (lex_match ('/'))
	new->f.type = FMT_NEWREC;
      else if (!parse_format_specifier (&new->f, FMTP_ALLOW_XT)
	       || !check_input_specifier (&new->f, 1))
	goto fail;

      lex_match (',');
    }
  lex_force_match (')');

  return head;

fail:
  destroy_fmt_list (head, 0);

  return NULL;
}

/* Parses a FORTRAN-like format specification into the linked
   list with head FIRST and tail LAST.  Returns nonzero if
   successful. */
static int
fixed_parse_fortran (struct fixed_parsing_state *fx,
                     struct dls_var_spec **first, struct dls_var_spec **last)
{
  struct fmt_list *list;
  int name_idx;

  list = fixed_parse_fortran_internal (fx, first, last);
  if (list == NULL)
    return 0;
  
  name_idx = 0;
  dump_fmt_list (fx, list, first, last, &name_idx);
  destroy_fmt_list (list, 1);
  if (name_idx < fx->name_cnt)
    {
      msg (SE, _("There aren't enough format specifications "
                 "to match the number of variable names given."));
      return 0; 
    }

  return 1;
}

/* Displays a table giving information on fixed-format variable
   parsing on DATA LIST. */
/* FIXME: The `Columns' column should be divided into three columns,
   one for the starting column, one for the dash, one for the ending
   column; then right-justify the starting column and left-justify the
   ending column. */
static void
dump_fixed_table (const struct dls_var_spec *specs,
                  const struct file_handle *fh, int rec_cnt)
{
  const struct dls_var_spec *spec;
  struct tab_table *t;
  int i;

  for (i = 0, spec = specs; spec; spec = spec->next)
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

  for (i = 1, spec = specs; spec; spec = spec->next, i++)
    {
      tab_text (t, 0, i, TAB_LEFT, spec->v->name);
      tab_text (t, 1, i, TAT_PRINTF, "%d", spec->rec);
      tab_text (t, 2, i, TAT_PRINTF, "%3d-%3d",
		    spec->fc, spec->lc);
      tab_text (t, 3, i, TAB_LEFT | TAT_FIX,
		    fmt_to_string (&spec->input));
    }

  if (fh != NULL)
    tab_title (t, 1, ngettext ("Reading %d record from file %s.",
                               "Reading %d records from file %s.", rec_cnt),
               rec_cnt, handle_get_filename (fh));
  else
    tab_title (t, 1, ngettext ("Reading %d record from the command file.",
                               "Reading %d records from the command file.",
                               rec_cnt),
               rec_cnt);
  tab_submit (t);
}

/* Free-format parsing. */

/* Parses variable specifications for DATA LIST FREE and adds
   them to the linked list with head FIRST and tail LAST.
   Returns nonzero only if successful. */
static int
parse_free (struct dls_var_spec **first, struct dls_var_spec **last)
{
  lex_get ();
  while (token != '.')
    {
      struct fmt_spec input, output;
      char **name;
      int name_cnt;
      int width;
      int i;

      if (!parse_DATA_LIST_vars (&name, &name_cnt, PV_NONE))
	return 0;

      if (lex_match ('('))
	{
	  if (!parse_format_specifier (&input, 0)
              || !check_input_specifier (&input, 1)
              || !lex_force_match (')')) 
            {
              for (i = 0; i < name_cnt; i++)
                free (name[i]);
              free (name);
              return 0; 
            }
	  convert_fmt_ItoO (&input, &output);
	}
      else
	{
	  lex_match ('*');
          input = make_input_format (FMT_F, 8, 0);
	  output = get_format ();
	}

      if (input.type == FMT_A || input.type == FMT_AHEX)
	width = input.w;
      else
	width = 0;
      for (i = 0; i < name_cnt; i++)
	{
          struct dls_var_spec *spec;
	  struct variable *v;

	  v = dict_create_var (default_dict, name[i], width);
	  
	  if (!v)
	    {
	      msg (SE, _("%s is a duplicate variable name."), name[i]);
	      return 0;
	    }
	  v->print = v->write = output;

          if (!case_source_is_complex (vfm_source))
            v->init = 0;

          spec = xmalloc (sizeof *spec);
          spec->input = input;
          spec->v = v;
	  spec->fv = v->fv;
	  str_copy_trunc (spec->name, sizeof spec->name, v->name);
	  append_var_spec (first, last, spec);
	}
      for (i = 0; i < name_cnt; i++)
	free (name[i]);
      free (name);
    }

  return lex_end_of_command () == CMD_SUCCESS;
}

/* Displays a table giving information on free-format variable parsing
   on DATA LIST. */
static void
dump_free_table (const struct data_list_pgm *dls,
                 const struct file_handle *fh)
{
  struct tab_table *t;
  int i;
  
  {
    struct dls_var_spec *spec;
    for (i = 0, spec = dls->first; spec; spec = spec->next)
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
    
    for (i = 1, spec = dls->first; spec; spec = spec->next, i++)
      {
	tab_text (t, 0, i, TAB_LEFT, spec->v->name);
	tab_text (t, 1, i, TAB_LEFT | TAT_FIX, fmt_to_string (&spec->input));
      }
  }

  if (fh != NULL)
    tab_title (t, 1, _("Reading free-form data from file %s."),
               handle_get_filename (fh));
  else
    tab_title (t, 1, _("Reading free-form data from the command file."));
  
  tab_submit (t);
}

/* Input procedure. */ 

/* Extracts a field from the current position in the current
   record.  Fields can be unquoted or quoted with single- or
   double-quote characters.  *FIELD is set to the field content.
   After parsing the field, sets the current position in the
   record to just past the field and any trailing delimiter.
   END_BLANK is used internally; it should be initialized by the
   caller to 0 and left alone afterward.  Returns 0 on failure or
   a 1-based column number indicating the beginning of the field
   on success. */
static int
cut_field (const struct data_list_pgm *dls, struct fixed_string *field,
           int *end_blank)
{
  struct fixed_string line;
  char *cp;
  size_t column_start;

  if (dfm_eof (dls->reader))
    return 0;
  if (dls->delim_cnt == 0)
    dfm_expand_tabs (dls->reader);
  dfm_get_record (dls->reader, &line);

  cp = ls_c_str (&line);
  if (dls->delim_cnt == 0) 
    {
      /* Skip leading whitespace. */
      while (cp < ls_end (&line) && isspace ((unsigned char) *cp))
        cp++;
      if (cp >= ls_end (&line))
        return 0;
      
      /* Handle actual data, whether quoted or unquoted. */
      if (*cp == '\'' || *cp == '"')
        {
          int quote = *cp;

          field->string = ++cp;
          while (cp < ls_end (&line) && *cp != quote)
            cp++;
          field->length = cp - field->string;
          if (cp < ls_end (&line))
            cp++;
          else
            msg (SW, _("Quoted string missing terminating `%c'."), quote);
        }
      else
        {
          field->string = cp;
          while (cp < ls_end (&line)
                 && !isspace ((unsigned char) *cp) && *cp != ',')
            cp++;
          field->length = cp - field->string;
        }

      /* Skip trailing whitespace and a single comma if present. */
      while (cp < ls_end (&line) && isspace ((unsigned char) *cp))
        cp++;
      if (cp < ls_end (&line) && *cp == ',')
        cp++;
    }
  else 
    {
      if (cp >= ls_end (&line)) 
        {
          int column = dfm_column_start (dls->reader);
               /* A blank line or a line that ends in \t has a
             trailing blank field. */
          if (column == 1 || (column > 1 && cp[-1] == '\t'))
            {
              if (*end_blank == 0)
                {
                  *end_blank = 1;
                  field->string = ls_end (&line);
                  field->length = 0;
                  dfm_forward_record (dls->reader);
                  return column;
                }
              else 
                {
                  *end_blank = 0;
                  return 0;
                }
            }
          else 
            return 0;
        }
      else 
        {
          field->string = cp;
          while (cp < ls_end (&line)
                 && memchr (dls->delims, *cp, dls->delim_cnt) == NULL)
            cp++; 
          field->length = cp - field->string;
          if (cp < ls_end (&line)) 
            cp++;
        }
    }
  
  dfm_forward_columns (dls->reader, field->string - line.string);
  column_start = dfm_column_start (dls->reader);
    
  dfm_forward_columns (dls->reader, cp - field->string);
    
  return column_start;
}

typedef int data_list_read_func (const struct data_list_pgm *, struct ccase *);
static data_list_read_func read_from_data_list_fixed;
static data_list_read_func read_from_data_list_free;
static data_list_read_func read_from_data_list_list;

/* Returns the proper function to read the kind of DATA LIST
   data specified by DLS. */
static data_list_read_func *
get_data_list_read_func (const struct data_list_pgm *dls) 
{
  switch (dls->type)
    {
    case DLS_FIXED:
      return read_from_data_list_fixed;

    case DLS_FREE:
      return read_from_data_list_free;

    case DLS_LIST:
      return read_from_data_list_list;

    default:
      assert (0);
      abort ();
    }
}

/* Reads a case from the data file into C, parsing it according
   to fixed-format syntax rules in DLS.  Returns -1 on success,
   -2 at end of file. */
static int
read_from_data_list_fixed (const struct data_list_pgm *dls,
                           struct ccase *c)
{
  struct dls_var_spec *var_spec = dls->first;
  int i;

  if (dfm_eof (dls->reader))
    return -2;
  for (i = 1; i <= dls->rec_cnt; i++)
    {
      struct fixed_string line;
      
      if (dfm_eof (dls->reader))
	{
	  /* Note that this can't occur on the first record. */
	  msg (SW, _("Partial case of %d of %d records discarded."),
	       i - 1, dls->rec_cnt);
	  return -2;
	}
      dfm_expand_tabs (dls->reader);
      dfm_get_record (dls->reader, &line);

      for (; var_spec && i == var_spec->rec; var_spec = var_spec->next)
	{
	  struct data_in di;

	  data_in_finite_line (&di, ls_c_str (&line), ls_length (&line),
                               var_spec->fc, var_spec->lc);
	  di.v = case_data_rw (c, var_spec->fv);
	  di.flags = DI_IMPLIED_DECIMALS;
	  di.f1 = var_spec->fc;
	  di.format = var_spec->input;

	  data_in (&di);
	}

      dfm_forward_record (dls->reader);
    }

  return -1;
}

/* Reads a case from the data file into C, parsing it according
   to free-format syntax rules in DLS.  Returns -1 on success,
   -2 at end of file. */
static int
read_from_data_list_free (const struct data_list_pgm *dls,
                          struct ccase *c)
{
  struct dls_var_spec *var_spec;
  int end_blank = 0;

  for (var_spec = dls->first; var_spec; var_spec = var_spec->next)
    {
      struct fixed_string field;
      int column;
      
      /* Cut out a field and read in a new record if necessary. */
      for (;;)
	{
	  column = cut_field (dls, &field, &end_blank);
	  if (column != 0)
	    break;

	  if (!dfm_eof (dls->reader)) 
            dfm_forward_record (dls->reader);
	  if (dfm_eof (dls->reader))
	    {
	      if (var_spec != dls->first)
		msg (SW, _("Partial case discarded.  The first variable "
                           "missing was %s."), var_spec->name);
	      return -2;
	    }
	}
      
      {
	struct data_in di;

	di.s = ls_c_str (&field);
	di.e = ls_end (&field);
	di.v = case_data_rw (c, var_spec->fv);
	di.flags = 0;
	di.f1 = column;
	di.format = var_spec->input;
	data_in (&di);
      }
    }
  return -1;
}

/* Reads a case from the data file and parses it according to
   list-format syntax rules.  Returns -1 on success, -2 at end of
   file. */
static int
read_from_data_list_list (const struct data_list_pgm *dls,
                          struct ccase *c)
{
  struct dls_var_spec *var_spec;
  int end_blank = 0;

  if (dfm_eof (dls->reader))
    return -2;

  for (var_spec = dls->first; var_spec; var_spec = var_spec->next)
    {
      struct fixed_string field;
      int column;

      /* Cut out a field and check for end-of-line. */
      column = cut_field (dls, &field, &end_blank);
      if (column == 0)
	{
	  if (get_undefined ())
	    msg (SW, _("Missing value(s) for all variables from %s onward.  "
                       "These will be filled with the system-missing value "
                       "or blanks, as appropriate."),
		 var_spec->name);
	  for (; var_spec; var_spec = var_spec->next)
            {
              int width = get_format_var_width (&var_spec->input);
              if (width == 0)
                case_data_rw (c, var_spec->fv)->f = SYSMIS;
              else
                memset (case_data_rw (c, var_spec->fv)->s, ' ', width); 
            }
	  break;
	}
      
      {
	struct data_in di;

	di.s = ls_c_str (&field);
	di.e = ls_end (&field);
	di.v = case_data_rw (c, var_spec->fv);
	di.flags = 0;
	di.f1 = column;
	di.format = var_spec->input;
	data_in (&di);
      }
    }

  dfm_forward_record (dls->reader);
  return -1;
}

/* Destroys SPEC. */
static void
destroy_dls_var_spec (struct dls_var_spec *spec) 
{
  struct dls_var_spec *next;

  while (spec != NULL)
    {
      next = spec->next;
      free (spec);
      spec = next;
    }
}

/* Destroys DATA LIST transformation PGM. */
static void
data_list_trns_free (struct trns_header *pgm)
{
  struct data_list_pgm *dls = (struct data_list_pgm *) pgm;
  free (dls->delims);
  destroy_dls_var_spec (dls->first);
  dfm_close_reader (dls->reader);
}

/* Handle DATA LIST transformation T, parsing data into C. */
static int
data_list_trns_proc (struct trns_header *t, struct ccase *c,
                     int case_num UNUSED)
{
  struct data_list_pgm *dls = (struct data_list_pgm *) t;
  data_list_read_func *read_func;
  int retval;

  dfm_push (dls->reader);

  read_func = get_data_list_read_func (dls);
  retval = read_func (dls, c);

  /* Handle end of file. */
  if (retval == -2)
    {
      /* If we already encountered end of file then this is an
         error. */
      if (dls->eof == 1)
        {
          msg (SE, _("Attempt to read past end of file."));
          err_failure ();
          dfm_pop (dls->reader);
          return -2;
        }

      /* Otherwise simply note it. */
      dls->eof = 1;
    }
  else
    dls->eof = 0;

  /* If there was an END subcommand handle it. */
  if (dls->end != NULL) 
    {
      if (retval == -2)
        {
          case_data_rw (c, dls->end->fv)->f = 1.0;
          retval = -1;
        }
      else
        case_data_rw (c, dls->end->fv)->f = 0.0;
    }
  
  dfm_pop (dls->reader);

  return retval;
}

/* Reads all the records from the data file and passes them to
   write_case(). */
static void
data_list_source_read (struct case_source *source,
                       struct ccase *c,
                       write_case_func *write_case, write_case_data wc_data)
{
  struct data_list_pgm *dls = source->aux;
  data_list_read_func *read_func = get_data_list_read_func (dls);

  dfm_push (dls->reader);
  while (read_func (dls, c) != -2)
    if (!write_case (wc_data))
      break;
  dfm_pop (dls->reader);
}

/* Destroys the source's internal data. */
static void
data_list_source_destroy (struct case_source *source)
{
  data_list_trns_free (source->aux);
  free (source->aux);
}

const struct case_source_class data_list_source_class = 
  {
    "DATA LIST",
    NULL,
    data_list_source_read,
    data_list_source_destroy,
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
    struct dls_var_spec *first, *last;	/* Variable parsing specifications. */
    struct dfm_reader *reader;  	/* Input file, never NULL. */

    struct rpd_num_or_var starts_beg;	/* STARTS=, before the dash. */
    struct rpd_num_or_var starts_end;	/* STARTS=, after the dash. */
    struct rpd_num_or_var occurs;	/* OCCURS= subcommand. */
    struct rpd_num_or_var length;	/* LENGTH= subcommand. */
    struct rpd_num_or_var cont_beg;	/* CONTINUED=, before the dash. */
    struct rpd_num_or_var cont_end;	/* CONTINUED=, after the dash. */

    /* ID subcommand. */
    int id_beg, id_end;			/* Beginning & end columns. */
    struct variable *id_var;		/* DATA LIST variable. */
    struct fmt_spec id_spec;		/* Input format spec. */
    union value *id_value;              /* ID value. */

    write_case_func *write_case;
    write_case_data wc_data;
  };

static trns_free_func repeating_data_trns_free;
static int parse_num_or_var (struct rpd_num_or_var *, const char *);
static int parse_repeating_data (struct dls_var_spec **,
                                 struct dls_var_spec **);
static void find_variable_input_spec (struct variable *v,
				      struct fmt_spec *spec);

/* Parses the REPEATING DATA command. */
int
cmd_repeating_data (void)
{
  struct repeating_data_trns *rpd;
  int table = 1;                /* Print table? */
  bool saw_starts = false;      /* Saw STARTS subcommand? */
  bool saw_occurs = false;      /* Saw OCCURS subcommand? */
  bool saw_length = false;      /* Saw LENGTH subcommand? */
  bool saw_continued = false;   /* Saw CONTINUED subcommand? */
  bool saw_id = false;          /* Saw ID subcommand? */
  struct file_handle *const fh = default_handle;
  
  assert (case_source_is_complex (vfm_source));

  rpd = xmalloc (sizeof *rpd);
  rpd->reader = dfm_open_reader (default_handle);
  rpd->first = rpd->last = NULL;
  rpd->starts_beg.num = 0;
  rpd->starts_beg.var = NULL;
  rpd->starts_end = rpd->occurs = rpd->length = rpd->cont_beg
    = rpd->cont_end = rpd->starts_beg;
  rpd->id_beg = rpd->id_end = 0;
  rpd->id_var = NULL;
  rpd->id_value = NULL;

  lex_match ('/');
  
  for (;;)
    {
      if (lex_match_id ("FILE"))
	{
          struct file_handle *file;
	  lex_match ('=');
	  file = fh_parse ();
	  if (file == NULL)
	    goto error;
	  if (file != fh)
	    {
	      msg (SE, _("REPEATING DATA must use the same file as its "
			 "corresponding DATA LIST or FILE TYPE."));
              goto error;
	    }
	}
      else if (lex_match_id ("STARTS"))
	{
	  lex_match ('=');
	  if (saw_starts)
	    {
	      msg (SE, _("%s subcommand given multiple times."),"STARTS");
	      goto error;
	    }
          saw_starts = true;
          
	  if (!parse_num_or_var (&rpd->starts_beg, "STARTS beginning column"))
	    goto error;

	  lex_negative_to_dash ();
	  if (lex_match ('-'))
	    {
	      if (!parse_num_or_var (&rpd->starts_end, "STARTS ending column"))
		goto error;
	    } else {
	      /* Otherwise, rpd->starts_end is uninitialized.  We
		 will initialize it later from the record length
		 of the file.  We can't do so now because the
		 file handle may not be specified yet. */
	    }

	  if (rpd->starts_beg.num != 0 && rpd->starts_end.num != 0
	      && rpd->starts_beg.num > rpd->starts_end.num)
	    {
	      msg (SE, _("STARTS beginning column (%d) exceeds "
			 "STARTS ending column (%d)."),
		   rpd->starts_beg.num, rpd->starts_end.num);
	      goto error;
	    }
	}
      else if (lex_match_id ("OCCURS"))
	{
	  lex_match ('=');
	  if (saw_occurs)
	    {
	      msg (SE, _("%s subcommand given multiple times."),"OCCURS");
	      goto error;
	    }
	  saw_occurs |= 2;

	  if (!parse_num_or_var (&rpd->occurs, "OCCURS"))
	    goto error;
	}
      else if (lex_match_id ("LENGTH"))
	{
	  lex_match ('=');
	  if (saw_length & 4)
	    {
	      msg (SE, _("%s subcommand given multiple times."),"LENGTH");
	      goto error;
	    }
	  saw_length |= 4;

	  if (!parse_num_or_var (&rpd->length, "LENGTH"))
	    goto error;
	}
      else if (lex_match_id ("CONTINUED"))
	{
	  lex_match ('=');
	  if (saw_continued & 8)
	    {
	      msg (SE, _("%s subcommand given multiple times."),"CONTINUED");
	      goto error;
	    }
	  saw_continued |= 8;

	  if (!lex_match ('/'))
	    {
	      if (!parse_num_or_var (&rpd->cont_beg,
                                     "CONTINUED beginning column"))
		goto error;

	      lex_negative_to_dash ();
	      if (lex_match ('-')
		  && !parse_num_or_var (&rpd->cont_end,
					"CONTINUED ending column"))
		goto error;
	  
	      if (rpd->cont_beg.num != 0 && rpd->cont_end.num != 0
		  && rpd->cont_beg.num > rpd->cont_end.num)
		{
		  msg (SE, _("CONTINUED beginning column (%d) exceeds "
			     "CONTINUED ending column (%d)."),
		       rpd->cont_beg.num, rpd->cont_end.num);
		  goto error;
		}
	    }
	  else
	    rpd->cont_beg.num = 1;
	}
      else if (lex_match_id ("ID"))
	{
	  lex_match ('=');
	  if (saw_id & 16)
	    {
	      msg (SE, _("%s subcommand given multiple times."),"ID");
	      goto error;
	    }
	  saw_id |= 16;
	  
	  if (!lex_force_int ())
	    goto error;
	  if (lex_integer () < 1)
	    {
	      msg (SE, _("ID beginning column (%ld) must be positive."),
		   lex_integer ());
	      goto error;
	    }
	  rpd->id_beg = lex_integer ();
	  
	  lex_get ();
	  lex_negative_to_dash ();
	  
	  if (lex_match ('-'))
	    {
	      if (!lex_force_int ())
		goto error;
	      if (lex_integer () < 1)
		{
		  msg (SE, _("ID ending column (%ld) must be positive."),
		       lex_integer ());
		  goto error;
		}
	      if (lex_integer () < rpd->id_end)
		{
		  msg (SE, _("ID ending column (%ld) cannot be less than "
			     "ID beginning column (%d)."),
		       lex_integer (), rpd->id_beg);
		  goto error;
		}
	      
	      rpd->id_end = lex_integer ();
	      lex_get ();
	    }
	  else rpd->id_end = rpd->id_beg;

	  if (!lex_force_match ('='))
	    goto error;
	  rpd->id_var = parse_variable ();
	  if (rpd->id_var == NULL)
	    goto error;

	  find_variable_input_spec (rpd->id_var, &rpd->id_spec);
          rpd->id_value = xmalloc (sizeof *rpd->id_value * rpd->id_var->nv);
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
	  goto error;
	}

      if (!lex_force_match ('/'))
	goto error;
    }

  /* Comes here when DATA specification encountered. */
  if (!saw_starts || !saw_occurs)
    {
      if (!saw_starts)
	msg (SE, _("Missing required specification STARTS."));
      if (!saw_occurs)
	msg (SE, _("Missing required specification OCCURS."));
      goto error;
    }

  /* Enforce ID restriction. */
  if (saw_id && !saw_continued)
    {
      msg (SE, _("ID specified without CONTINUED."));
      goto error;
    }

  /* Calculate and check starts_end, cont_end if necessary. */
  if (rpd->starts_end.num == 0 && rpd->starts_end.var == NULL) 
    {
      rpd->starts_end.num = fh != NULL ? handle_get_record_width (fh) : 80;
      if (rpd->starts_beg.num != 0 
          && rpd->starts_beg.num > rpd->starts_end.num)
        {
          msg (SE, _("STARTS beginning column (%d) exceeds "
                     "default STARTS ending column taken from file's "
                     "record width (%d)."),
               rpd->starts_beg.num, rpd->starts_end.num);
          goto error;
        } 
    }
  if (rpd->cont_end.num == 0 && rpd->cont_end.var == NULL) 
    {
      rpd->cont_end.num = fh != NULL ? handle_get_record_width (fh) : 80;
      if (rpd->cont_beg.num != 0
          && rpd->cont_beg.num > rpd->cont_end.num)
        {
          msg (SE, _("CONTINUED beginning column (%d) exceeds "
                     "default CONTINUED ending column taken from file's "
                     "record width (%d)."),
               rpd->cont_beg.num, rpd->cont_end.num);
          goto error;
        } 
    }
  
  lex_match ('=');
  if (!parse_repeating_data (&rpd->first, &rpd->last))
    goto error;

  /* Calculate length if necessary. */
  if (!saw_length)
    {
      struct dls_var_spec *iter;
      
      for (iter = rpd->first; iter; iter = iter->next)
        if (iter->lc > rpd->length.num)
          rpd->length.num = iter->lc;
      assert (rpd->length.num != 0);
    }
  
  if (table)
    dump_fixed_table (rpd->first, fh, rpd->last->rec);

  rpd->h.proc = repeating_data_trns_proc;
  rpd->h.free = repeating_data_trns_free;
  add_transformation (&rpd->h);

  return lex_end_of_command ();

 error:
  destroy_dls_var_spec (rpd->first);
  free (rpd->id_value);
  return CMD_FAILURE;
}

/* Finds the input format specification for variable V and puts
   it in SPEC.  Because of the way that DATA LIST is structured,
   this is nontrivial. */
static void 
find_variable_input_spec (struct variable *v, struct fmt_spec *spec)
{
  int i;
  
  for (i = 0; i < n_trns; i++)
    {
      struct data_list_pgm *pgm = (struct data_list_pgm *) t_trns[i];
      
      if (pgm->h.proc == data_list_trns_proc)
	{
	  struct dls_var_spec *iter;

	  for (iter = pgm->first; iter; iter = iter->next)
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
  else if (lex_is_integer ())
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

/* Parses data specifications for repeating data groups, adding
   them to the linked list with head FIRST and tail LAST.
   Returns nonzero only if successful.  */
static int
parse_repeating_data (struct dls_var_spec **first, struct dls_var_spec **last)
{
  struct fixed_parsing_state fx;
  int i;

  fx.recno = 0;
  fx.sc = 1;

  while (token != '.')
    {
      if (!parse_DATA_LIST_vars (&fx.name, &fx.name_cnt, PV_NONE))
	return 0;

      if (lex_is_number ())
	{
	  if (!fixed_parse_compatible (&fx, first, last))
	    goto fail;
	}
      else if (token == '(')
	{
	  if (!fixed_parse_fortran (&fx, first, last))
	    goto fail;
	}
      else
	{
	  msg (SE, _("SPSS-like or FORTRAN-like format "
                     "specification expected after variable names."));
	  goto fail;
	}

      for (i = 0; i < fx.name_cnt; i++)
	free (fx.name[i]);
      free (fx.name);
    }
  
  return 1;

 fail:
  for (i = 0; i < fx.name_cnt; i++)
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
  if (n->var != NULL)
    {
      double v = case_num (c, n->var->fv);
      return v != SYSMIS && v >= INT_MIN && v <= INT_MAX ? v : -1;
    }
  else
    return n->num;
}

/* Parameter record passed to rpd_parse_record(). */
struct rpd_parse_info 
  {
    struct repeating_data_trns *trns;  /* REPEATING DATA transformation. */
    const char *line;   /* Line being parsed. */
    size_t len;         /* Line length. */
    int beg, end;       /* First and last column of first occurrence. */
    int ofs;            /* Column offset between repeated occurrences. */
    struct ccase *c;    /* Case to fill in. */
    int verify_id;      /* Zero to initialize ID, nonzero to verify it. */
    int max_occurs;     /* Max number of occurrences to parse. */
  };

/* Parses one record of repeated data and outputs corresponding
   cases.  Returns number of occurrences parsed up to the
   maximum specified in INFO. */
static int
rpd_parse_record (const struct rpd_parse_info *info)
{
  struct repeating_data_trns *t = info->trns;
  int cur = info->beg;
  int occurrences;

  /* Handle record ID values. */
  if (t->id_beg != 0)
    {
      union value id_temp[MAX_ELEMS_PER_VALUE];
      
      /* Parse record ID into V. */
      {
	struct data_in di;

	data_in_finite_line (&di, info->line, info->len, t->id_beg, t->id_end);
	di.v = info->verify_id ? id_temp : t->id_value;
	di.flags = 0;
	di.f1 = t->id_beg;
	di.format = t->id_spec;

	if (!data_in (&di))
	  return 0;
      }

      if (info->verify_id
          && compare_values (id_temp, t->id_value, t->id_var->width) != 0)
	{
	  char expected_str [MAX_FORMATTED_LEN + 1];
	  char actual_str [MAX_FORMATTED_LEN + 1];

	  data_out (expected_str, &t->id_var->print, t->id_value);
          expected_str[t->id_var->print.w] = '\0';

	  data_out (actual_str, &t->id_var->print, id_temp);
          actual_str[t->id_var->print.w] = '\0';
	    
	  tmsg (SE, RPD_ERR, 
		_("Encountered mismatched record ID \"%s\" expecting \"%s\"."),
		actual_str, expected_str);

	  return 0;
	}
    }

  /* Iterate over the set of expected occurrences and record each of
     them as a separate case.  FIXME: We need to execute any
     transformations that follow the current one. */
  {
    int warned = 0;

    for (occurrences = 0; occurrences < info->max_occurs; )
      {
	if (cur + info->ofs > info->end + 1)
	  break;
	occurrences++;

	{
	  struct dls_var_spec *var_spec = t->first;
	
	  for (; var_spec; var_spec = var_spec->next)
	    {
	      int fc = var_spec->fc - 1 + cur;
	      int lc = var_spec->lc - 1 + cur;

	      if (fc > info->len && !warned && var_spec->input.type != FMT_A)
		{
		  warned = 1;

		  tmsg (SW, RPD_ERR,
			_("Variable %s starting in column %d extends "
			  "beyond physical record length of %d."),
			var_spec->v->name, fc, info->len);
		}
	      
	      {
		struct data_in di;

		data_in_finite_line (&di, info->line, info->len, fc, lc);
		di.v = case_data_rw (info->c, var_spec->fv);
		di.flags = 0;
		di.f1 = fc + 1;
		di.format = var_spec->input;

		if (!data_in (&di))
		  return 0;
	      }
	    }
	}

	cur += info->ofs;

	if (!t->write_case (t->wc_data))
	  return 0;
      }
  }

  return occurrences;
}

/* Reads one set of repetitions of the elements in the REPEATING
   DATA structure.  Returns -1 on success, -2 on end of file or
   on failure. */
int
repeating_data_trns_proc (struct trns_header *trns, struct ccase *c,
                          int case_num UNUSED)
{
  struct repeating_data_trns *t = (struct repeating_data_trns *) trns;
    
  struct fixed_string line;       /* Current record. */

  int starts_beg;	/* Starting column. */
  int starts_end;	/* Ending column. */
  int occurs;		/* Number of repetitions. */
  int length;		/* Length of each occurrence. */
  int cont_beg;         /* Starting column for continuation lines. */
  int cont_end;         /* Ending column for continuation lines. */

  int occurs_left;	/* Number of occurrences remaining. */

  int code;		/* Return value from rpd_parse_record(). */
    
  int skip_first_record = 0;
    
  dfm_push (t->reader);
  
  /* Read the current record. */
  dfm_reread_record (t->reader, 1);
  dfm_expand_tabs (t->reader);
  if (dfm_eof (t->reader))
    return -2;
  dfm_get_record (t->reader, &line);
  dfm_forward_record (t->reader);

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
      struct rpd_parse_info info;
      info.trns = t;
      info.line = ls_c_str (&line);
      info.len = ls_length (&line);
      info.beg = starts_beg;
      info.end = starts_end;
      info.ofs = length;
      info.c = c;
      info.verify_id = 0;
      info.max_occurs = occurs_left;
      code = rpd_parse_record (&info);
      if (!code)
        return -2;
      occurs_left -= code;
    }
  else if (cont_beg == 0)
    return -3;

  /* Make sure, if some occurrences are left, that we have
     continuation records. */
  if (occurs_left > 0 && cont_beg == 0)
    {
      tmsg (SE, RPD_ERR,
            _("Number of repetitions specified on OCCURS (%d) "
              "exceed number of repetitions available in "
              "space on STARTS (%d), and CONTINUED not specified."),
            occurs, (starts_end - starts_beg + 1) / length);
      return -2;
    }

  /* Go on to additional records. */
  while (occurs_left != 0)
    {
      struct rpd_parse_info info;

      assert (occurs_left >= 0);

      /* Read in another record. */
      if (dfm_eof (t->reader))
        {
          tmsg (SE, RPD_ERR,
                _("Unexpected end of file with %d repetitions "
                  "remaining out of %d."),
                occurs_left, occurs);
          return -2;
        }
      dfm_expand_tabs (t->reader);
      dfm_get_record (t->reader, &line);
      dfm_forward_record (t->reader);

      /* Parse this record. */
      info.trns = t;
      info.line = ls_c_str (&line);
      info.len = ls_length (&line);
      info.beg = cont_beg;
      info.end = cont_end;
      info.ofs = length;
      info.c = c;
      info.verify_id = 1;
      info.max_occurs = occurs_left;
      code = rpd_parse_record (&info);;
      if (!code)
        return -2;
      occurs_left -= code;
    }
    
  dfm_pop (t->reader);

  /* FIXME: This is a kluge until we've implemented multiplexing of
     transformations. */
  return -3;
}

/* Frees a REPEATING DATA transformation. */
void
repeating_data_trns_free (struct trns_header *rpd_) 
{
  struct repeating_data_trns *rpd = (struct repeating_data_trns *) rpd_;

  destroy_dls_var_spec (rpd->first);
  dfm_close_reader (rpd->reader);
  free (rpd->id_value);
}

/* Lets repeating_data_trns_proc() know how to write the cases
   that it composes.  Not elegant. */
void
repeating_data_set_write_case (struct trns_header *trns,
                               write_case_func *write_case,
                               write_case_data wc_data) 
{
  struct repeating_data_trns *t = (struct repeating_data_trns *) trns;

  assert (trns->proc == repeating_data_trns_proc);
  t->write_case = write_case;
  t->wc_data = wc_data;
}
