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
#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include <data/case-source.h>
#include <data/case.h>
#include <data/data-in.h>
#include <data/dictionary.h>
#include <data/format.h>
#include <data/settings.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/data-reader.h>
#include <language/data-io/file-handle.h>
#include <language/data-io/inpt-pgm.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include <output/table.h>
#include <procedure.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Utility function. */

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
    struct dls_var_spec *first, *last;	/* Variable parsing specifications. */
    struct dfm_reader *reader;  /* Data file reader. */

    int type;			/* A DLS_* constant. */
    struct variable *end;	/* Variable specified on END subcommand. */
    int rec_cnt;                /* Number of records. */
    size_t case_size;           /* Case size in bytes. */
    char *delims;               /* Delimiters if any; not null-terminated. */
    size_t delim_cnt;           /* Number of delimiter, or 0 for spaces. */
  };

static const struct case_source_class data_list_source_class;

static int parse_fixed (struct data_list_pgm *);
static int parse_free (struct dls_var_spec **, struct dls_var_spec **);
static void dump_fixed_table (const struct dls_var_spec *,
                              const struct file_handle *, int rec_cnt);
static void dump_free_table (const struct data_list_pgm *,
                             const struct file_handle *);
static void destroy_dls_var_spec (struct dls_var_spec *);
static trns_free_func data_list_trns_free;
static trns_proc_func data_list_trns_proc;

int
cmd_data_list (void)
{
  struct data_list_pgm *dls;
  int table = -1;                /* Print table if nonzero, -1=undecided. */
  struct file_handle *fh = fh_inline_file ();

  if (!in_input_program ())
    discard_variables ();

  dls = xmalloc (sizeof *dls);
  dls->reader = NULL;
  dls->type = -1;
  dls->end = NULL;
  dls->rec_cnt = 0;
  dls->delims = NULL;
  dls->delim_cnt = 0;
  dls->first = dls->last = NULL;

  while (token != '/')
    {
      if (lex_match_id ("FILE"))
	{
	  lex_match ('=');
	  fh = fh_parse (FH_REF_FILE | FH_REF_INLINE);
	  if (fh == NULL)
	    goto error;
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
  fh_set_default_handle (fh);

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
    add_transformation (data_list_trns_proc, data_list_trns_free, dls);
  else 
    vfm_source = create_case_source (&data_list_source_class, dls);

  return CMD_SUCCESS;

 error:
  data_list_trns_free (dls);
  return CMD_CASCADING_FAILURE;
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
    size_t name_cnt;		/* Number of names. */

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
  size_t i;

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
      tab_text (t, 3, i, TAB_LEFT | TAB_FIX,
		    fmt_to_string (&spec->input));
    }

  tab_title (t, ngettext ("Reading %d record from %s.",
                          "Reading %d records from %s.", rec_cnt),
             rec_cnt, fh_get_name (fh));
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
      size_t name_cnt;
      int width;
      size_t i;

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
	  output = *get_format ();
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
	tab_text (t, 1, i, TAB_LEFT | TAB_FIX, fmt_to_string (&spec->input));
      }
  }

  tab_title (t, _("Reading free-form data from %s."), fh_get_name (fh));
  
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

static bool read_from_data_list_fixed (const struct data_list_pgm *,
                                       struct ccase *);
static bool read_from_data_list_free (const struct data_list_pgm *,
                                      struct ccase *);
static bool read_from_data_list_list (const struct data_list_pgm *,
                                      struct ccase *);

/* Reads a case from DLS into C.
   Returns true if successful, false at end of file or on I/O error. */
static bool
read_from_data_list (const struct data_list_pgm *dls, struct ccase *c) 
{
  bool retval;

  dfm_push (dls->reader);
  switch (dls->type)
    {
    case DLS_FIXED:
      retval = read_from_data_list_fixed (dls, c);
      break;
    case DLS_FREE:
      retval = read_from_data_list_free (dls, c);
      break;
    case DLS_LIST:
      retval = read_from_data_list_list (dls, c);
      break;
    default:
      abort ();
    }
  dfm_pop (dls->reader);

  return retval;
}

/* Reads a case from the data file into C, parsing it according
   to fixed-format syntax rules in DLS.  
   Returns true if successful, false at end of file or on I/O error. */
static bool
read_from_data_list_fixed (const struct data_list_pgm *dls, struct ccase *c)
{
  struct dls_var_spec *var_spec = dls->first;
  int i;

  if (dfm_eof (dls->reader))
    return false;
  for (i = 1; i <= dls->rec_cnt; i++)
    {
      struct fixed_string line;
      
      if (dfm_eof (dls->reader))
	{
	  /* Note that this can't occur on the first record. */
	  msg (SW, _("Partial case of %d of %d records discarded."),
	       i - 1, dls->rec_cnt);
	  return false;
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

  return true;
}

/* Reads a case from the data file into C, parsing it according
   to free-format syntax rules in DLS.  
   Returns true if successful, false at end of file or on I/O error. */
static bool
read_from_data_list_free (const struct data_list_pgm *dls, struct ccase *c)
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
	      return false;
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
  return true;
}

/* Reads a case from the data file and parses it according to
   list-format syntax rules.  
   Returns true if successful, false at end of file or on I/O error. */
static bool
read_from_data_list_list (const struct data_list_pgm *dls, struct ccase *c)
{
  struct dls_var_spec *var_spec;
  int end_blank = 0;

  if (dfm_eof (dls->reader))
    return false;

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
  return true;
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

/* Destroys DATA LIST transformation DLS.
   Returns true if successful, false if an I/O error occurred. */
static bool
data_list_trns_free (void *dls_)
{
  struct data_list_pgm *dls = dls_;
  free (dls->delims);
  destroy_dls_var_spec (dls->first);
  dfm_close_reader (dls->reader);
  free (dls);
  return true;
}

/* Handle DATA LIST transformation DLS, parsing data into C. */
static int
data_list_trns_proc (void *dls_, struct ccase *c, int case_num UNUSED)
{
  struct data_list_pgm *dls = dls_;
  int retval;

  if (read_from_data_list (dls, c))
    retval = TRNS_CONTINUE;
  else if (dfm_reader_error (dls->reader) || dfm_eof (dls->reader) > 1) 
    {
      /* An I/O error, or encountering end of file for a second
         time, should be escalated into a more serious error. */
      retval = TRNS_ERROR;
    }
  else
    retval = TRNS_DROP_CASE;
  
  /* If there was an END subcommand handle it. */
  if (dls->end != NULL) 
    {
      double *end = &case_data_rw (c, dls->end->fv)->f;
      if (retval == TRNS_DROP_CASE)
        {
          *end = 1.0;
          retval = TRNS_CONTINUE;
        }
      else
        *end = 0.0;
    }

  return retval;
}

/* Reads all the records from the data file and passes them to
   write_case().
   Returns true if successful, false if an I/O error occurred. */
static bool
data_list_source_read (struct case_source *source,
                       struct ccase *c,
                       write_case_func *write_case, write_case_data wc_data)
{
  struct data_list_pgm *dls = source->aux;

  for (;;) 
    {
      bool ok;

      if (!read_from_data_list (dls, c)) 
        return !dfm_reader_error (dls->reader);

      dfm_push (dls->reader);
      ok = write_case (wc_data);
      dfm_pop (dls->reader);
      if (!ok)
        return false;
    }
}

/* Destroys the source's internal data. */
static void
data_list_source_destroy (struct case_source *source)
{
  data_list_trns_free (source->aux);
}

static const struct case_source_class data_list_source_class = 
  {
    "DATA LIST",
    NULL,
    data_list_source_read,
    data_list_source_destroy,
  };
