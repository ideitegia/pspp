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

#include <config.h>
#include "error.h"
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "data-in.h"
#include "dfm.h"
#include "file-handle.h"
#include "format.h"
#include "lexer.h"
#include "str.h"
#include "var.h"
#include "vfm.h"

/* Defines the three types of complex files read by FILE TYPE. */
enum
  {
    FTY_MIXED,
    FTY_GROUPED,
    FTY_NESTED
  };

/* Limited variable column specifications. */
struct col_spec
  {
   char name[9];		/* Variable name. */
    int fc, nc;			/* First column (1-based), # of columns. */
    int fmt;			/* Format type. */
    struct variable *v;		/* Variable. */
  };

/* RCT_* record type constants. */
enum
  {
    RCT_OTHER = 001,		/* 1=OTHER. */
    RCT_SKIP = 002,		/* 1=SKIP. */
    RCT_DUPLICATE = 004,	/* DUPLICATE: 0=NOWARN, 1=WARN. */
    RCT_MISSING = 010,		/* MISSING: 0=NOWARN, 1=WARN. */
    RCT_SPREAD = 020		/* SPREAD: 0=NO, 1=YES. */
  };

/* Represents a RECORD TYPE command. */
struct record_type
  {
    struct record_type *next;
    unsigned flags;		/* RCT_* constants. */
    union value *v;		/* Vector of values for this record type. */
    int nv;			/* Length of vector V. */
    struct col_spec case_sbc;	/* CASE subcommand. */
    int ft, lt;			/* First, last transformation index. */
  };				/* record_type */

/* Represents a FILE TYPE input program.  Does not contain a
   trns_header because it's never submitted as a transformation. */
struct file_type_pgm
  {
    int type;			/* One of the FTY_* constants. */
    struct file_handle *handle;	/* File handle of input file. */
    struct col_spec record;	/* RECORD subcommand. */
    struct col_spec case_sbc;	/* CASE subcommand. */
    int wild;			/* 0=NOWARN, 1=WARN. */
    int duplicate;		/* 0=NOWARN, 1=WARN. */
    int missing;		/* 0=NOWARN, 1=WARN, 2=CASE. */
    int ordered;		/* 0=NO, 1=YES. */
    int had_rec_type;		/* 1=Had a RECORD TYPE command.
				   RECORD TYPE must precede the first
				   DATA LIST. */
    struct record_type *recs_head;	/* List of record types. */
    struct record_type *recs_tail;	/* Last in list of record types. */
    size_t case_size;           /* Case size in bytes. */
  };

static int parse_col_spec (struct col_spec *, const char *);
static void create_col_var (struct col_spec *c);

/* Parses FILE TYPE command. */
int
cmd_file_type (void)
{
  static struct file_type_pgm *fty;

  /* Initialize. */
  discard_variables ();

  fty = xmalloc (sizeof *fty);
  fty->handle = inline_file;
  fty->record.name[0] = 0;
  fty->case_sbc.name[0] = 0;
  fty->wild = fty->duplicate = fty->missing = fty->ordered = 0;
  fty->had_rec_type = 0;
  fty->recs_head = fty->recs_tail = NULL;

  if (lex_match_id ("MIXED"))
    fty->type = FTY_MIXED;
  else if (lex_match_id ("GROUPED"))
    {
      fty->type = FTY_GROUPED;
      fty->wild = 1;
      fty->duplicate = 1;
      fty->missing = 1;
      fty->ordered = 1;
    }
  else if (lex_match_id ("NESTED"))
    fty->type = FTY_NESTED;
  else
    {
      msg (SE, _("MIXED, GROUPED, or NESTED expected."));
      goto error;
    }

  while (token != '.')
    {
      if (lex_match_id ("FILE"))
	{
	  lex_match ('=');
	  fty->handle = fh_parse_file_handle ();
	  if (!fty->handle)
	    goto error;
	}
      else if (lex_match_id ("RECORD"))
	{
	  lex_match ('=');
	  if (!parse_col_spec (&fty->record, "####RECD"))
	    goto error;
	}
      else if (lex_match_id ("CASE"))
	{
	  if (fty->type == FTY_MIXED)
	    {
	      msg (SE, _("The CASE subcommand is not valid on FILE TYPE "
			 "MIXED."));
	      goto error;
	    }
	  
	  lex_match ('=');
	  if (!parse_col_spec (&fty->case_sbc, "####CASE"))
	    goto error;
	}
      else if (lex_match_id ("WILD"))
	{
	  lex_match ('=');
	  if (lex_match_id ("WARN"))
	    fty->wild = 1;
	  else if (lex_match_id ("NOWARN"))
	    fty->wild = 0;
	  else
	    {
	      msg (SE, _("WARN or NOWARN expected after WILD."));
	      goto error;
	    }
	}
      else if (lex_match_id ("DUPLICATE"))
	{
	  if (fty->type == FTY_MIXED)
	    {
	      msg (SE, _("The DUPLICATE subcommand is not valid on "
			 "FILE TYPE MIXED."));
	      goto error;
	    }

	  lex_match ('=');
	  if (lex_match_id ("WARN"))
	    fty->duplicate = 1;
	  else if (lex_match_id ("NOWARN"))
	    fty->duplicate = 0;
	  else if (lex_match_id ("CASE"))
	    {
	      if (fty->type != FTY_NESTED)
		{
		  msg (SE, _("DUPLICATE=CASE is only valid on "
			     "FILE TYPE NESTED."));
		  goto error;
		}
	      
	      fty->duplicate = 2;
	    }
	  else
	    {
	      msg (SE, _("WARN%s expected after DUPLICATE."),
		   (fty->type == FTY_NESTED ? _(", NOWARN, or CASE")
		    : _(" or NOWARN")));
	      goto error;
	    }
	}
      else if (lex_match_id ("MISSING"))
	{
	  if (fty->type == FTY_MIXED)
	    {
	      msg (SE, _("The MISSING subcommand is not valid on "
			 "FILE TYPE MIXED."));
	      goto error;
	    }
	  
	  lex_match ('=');
	  if (lex_match_id ("NOWARN"))
	    fty->missing = 0;
	  else if (lex_match_id ("WARN"))
	    fty->missing = 1;
	  else
	    {
	      msg (SE, _("WARN or NOWARN after MISSING."));
	      goto error;
	    }
	}
      else if (lex_match_id ("ORDERED"))
	{
	  if (fty->type != FTY_GROUPED)
	    {
	      msg (SE, _("ORDERED is only valid on FILE TYPE GROUPED."));
	      goto error;
	    }
	  
	  lex_match ('=');
	  if (lex_match_id ("YES"))
	    fty->ordered = 1;
	  else if (lex_match_id ("NO"))
	    fty->ordered = 0;
	  else
	    {
	      msg (SE, _("YES or NO expected after ORDERED."));
	      goto error;
	    }
	}
      else
	{
	  lex_error (_("while expecting a valid subcommand"));
	  goto error;
	}
    }

  if (fty->record.name[0] == 0)
    {
      msg (SE, _("The required RECORD subcommand was not present."));
      goto error;
    }

  if (fty->type == FTY_GROUPED)
    {
      if (fty->case_sbc.name[0] == 0)
	{
	  msg (SE, _("The required CASE subcommand was not present."));
	  goto error;
	}
      
      if (!strcmp (fty->case_sbc.name, fty->record.name))
	{
	  msg (SE, _("CASE and RECORD must specify different variable "
		     "names."));
	  goto error;
	}
    }

  if (!dfm_open_for_reading (fty->handle))
    goto error;
  default_handle = fty->handle;

  create_col_var (&fty->record);
  if (fty->case_sbc.name[0])
    create_col_var (&fty->case_sbc);
  vfm_source = create_case_source (&file_type_source_class, default_dict, fty);

  return CMD_SUCCESS;

 error:
  free (fty);
  return CMD_FAILURE;
}

/* Creates a variable with attributes specified by struct col_spec C, and
   stores it into C->V. */
static void
create_col_var (struct col_spec *c)
{
  int width;

  if (formats[c->fmt].cat & FCAT_STRING)
    width = c->nc;
  else
    width = 0;
  c->v = dict_create_var (default_dict, c->name, width);
}

/* Parses variable, column, type specifications for a variable. */
static int
parse_col_spec (struct col_spec *c, const char *def_name)
{
  struct fmt_spec spec;

  /* Name. */
  if (token == T_ID)
    {
      strcpy (c->name, tokid);
      lex_get ();
    }
  else
    strcpy (c->name, def_name);

  /* First column. */
  if (!lex_force_int ())
    return 0;
  c->fc = lex_integer ();
  if (c->fc < 1)
    {
      msg (SE, _("Column value must be positive."));
      return 0;
    }
  lex_get ();

  /* Last column. */
  lex_negative_to_dash ();
  if (lex_match ('-'))
    {
      if (!lex_force_int ())
	return 0;
      c->nc = lex_integer ();
      lex_get ();

      if (c->nc < c->fc)
	{
	  msg (SE, _("Ending column precedes beginning column."));
	  return 0;
	}
      
      c->nc -= c->fc - 1;
    }
  else
    c->nc = 1;

  /* Format specifier. */
  if (lex_match ('('))
    {
      const char *cp;
      if (!lex_force_id ())
	return 0;
      c->fmt = parse_format_specifier_name (&cp, 0);
      if (c->fmt == -1)
	return 0;
      if (*cp)
	{
	  msg (SE, _("Bad format specifier name."));
	  return 0;
	}
      lex_get ();
      if (!lex_force_match (')'))
	return 0;
    }
  else
    c->fmt = FMT_F;

  spec.type = c->fmt;
  spec.w = c->nc;
  spec.d = 0;
  return check_input_specifier (&spec);
}

/* RECORD TYPE. */

/* Parse the RECORD TYPE command. */
int
cmd_record_type (void)
{
  struct file_type_pgm *fty;
  struct record_type *rct;

  /* Make sure we're inside a FILE TYPE structure. */
  if (pgm_state != STATE_INPUT
      || !case_source_is_class (vfm_source, &file_type_source_class))
    {
      msg (SE, _("This command may only appear within a "
		 "FILE TYPE/END FILE TYPE structure."));
      return CMD_FAILURE;
    }

  fty = vfm_source->aux;

  /* Initialize the record_type structure. */
  rct = xmalloc (sizeof *rct);
  rct->next = NULL;
  rct->flags = 0;
  if (fty->duplicate)
    rct->flags |= RCT_DUPLICATE;
  if (fty->missing)
    rct->flags |= RCT_MISSING;
  rct->v = NULL;
  rct->nv = 0;
  rct->ft = n_trns;
  if (fty->case_sbc.name[0])
    rct->case_sbc = fty->case_sbc;

  if (fty->recs_tail && (fty->recs_tail->flags & RCT_OTHER))
    {
      msg (SE, _("OTHER may appear only on the last RECORD TYPE command."));
      goto error;
    }
      
  if (fty->recs_tail)
    {
      fty->recs_tail->lt = n_trns - 1;
      if (!(fty->recs_tail->flags & RCT_SKIP)
	  && fty->recs_tail->ft == fty->recs_tail->lt)
	{
	  msg (SE, _("No input commands (DATA LIST, REPEATING DATA) "
		     "for above RECORD TYPE."));
	  goto error;
	}
    }

  /* Parse record type values. */
  if (lex_match_id ("OTHER"))
    rct->flags |= RCT_OTHER;
  else
    {
      int mv = 0;

      while (token == T_NUM || token == T_STRING)
	{
	  if (rct->nv >= mv)
	    {
	      mv += 16;
	      rct->v = xrealloc (rct->v, mv * sizeof *rct->v);
	    }

	  if (formats[fty->record.fmt].cat & FCAT_STRING)
	    {
	      if (!lex_force_string ())
		goto error;
	      rct->v[rct->nv].c = xmalloc (fty->record.nc + 1);
	      st_bare_pad_copy (rct->v[rct->nv].c, ds_value (&tokstr),
				fty->record.nc + 1);
	    }
	  else
	    {
	      if (!lex_force_num ())
		goto error;
	      rct->v[rct->nv].f = tokval;
	    }
	  rct->nv++;
	  lex_get ();

	  lex_match (',');
	}
    }

  /* Parse the rest of the subcommands. */
  while (token != '.')
    {
      if (lex_match_id ("SKIP"))
	rct->flags |= RCT_SKIP;
      else if (lex_match_id ("CASE"))
	{
	  if (fty->type == FTY_MIXED)
	    {
	      msg (SE, _("The CASE subcommand is not allowed on "
			 "the RECORD TYPE command for FILE TYPE MIXED."));
	      goto error;
	    }

	  lex_match ('=');
	  if (!parse_col_spec (&rct->case_sbc, ""))
	    goto error;
	  if (rct->case_sbc.name[0])
	    {
	      msg (SE, _("No variable name may be specified for the "
			 "CASE subcommand on RECORD TYPE."));
	      goto error;
	    }
	  
	  if ((formats[rct->case_sbc.fmt].cat ^ formats[fty->case_sbc.fmt].cat)
	      & FCAT_STRING)
	    {
	      msg (SE, _("The CASE column specification on RECORD TYPE "
			 "must give a format specifier that is the "
			 "same type as that of the CASE column "
			 "specification given on FILE TYPE."));
	      goto error;
	    }
	}
      else if (lex_match_id ("DUPLICATE"))
	{
	  lex_match ('=');
	  if (lex_match_id ("WARN"))
	    rct->flags |= RCT_DUPLICATE;
	  else if (lex_match_id ("NOWARN"))
	    rct->flags &= ~RCT_DUPLICATE;
	  else
	    {
	      msg (SE, _("WARN or NOWARN expected on DUPLICATE "
			 "subcommand."));
	      goto error;
	    }
	}
      else if (lex_match_id ("MISSING"))
	{
	  lex_match ('=');
	  if (lex_match_id ("WARN"))
	    rct->flags |= RCT_MISSING;
	  else if (lex_match_id ("NOWARN"))
	    rct->flags &= ~RCT_MISSING;
	  else
	    {
	      msg (SE, _("WARN or NOWARN expected on MISSING subcommand."));
	      goto error;
	    }
	}
      else if (lex_match_id ("SPREAD"))
	{
	  lex_match ('=');
	  if (lex_match_id ("YES"))
	    rct->flags |= RCT_SPREAD;
	  else if (lex_match_id ("NO"))
	    rct->flags &= ~RCT_SPREAD;
	  else
	    {
	      msg (SE, _("YES or NO expected on SPREAD subcommand."));
	      goto error;
	    }
	}
      else
	{
	  lex_error (_("while expecting a valid subcommand"));
	  goto error;
	}
    }

  if (fty->recs_head)
    fty->recs_tail = fty->recs_tail->next = xmalloc (sizeof *fty->recs_tail);
  else
    fty->recs_head = fty->recs_tail = xmalloc (sizeof *fty->recs_tail);
  memcpy (fty->recs_tail, &rct, sizeof *fty->recs_tail);

  return CMD_SUCCESS;

 error:
  if (formats[fty->record.fmt].cat & FCAT_STRING) 
    {
      int i;
      
      for (i = 0; i < rct->nv; i++)
        free (rct->v[i].c); 
    }
  free (rct->v);
  free (rct);

  return CMD_FAILURE;
}

/* END FILE TYPE. */

int
cmd_end_file_type (void)
{
  struct file_type_pgm *fty;

  if (pgm_state != STATE_INPUT
      || case_source_is_class (vfm_source, &file_type_source_class))
    {
      msg (SE, _("This command may only appear within a "
		 "FILE TYPE/END FILE TYPE structure."));
      return CMD_FAILURE;
    }
  fty = vfm_source->aux;
  fty->case_size = dict_get_case_size (default_dict);

  if (fty->recs_tail)
    {
      fty->recs_tail->lt = n_trns - 1;
      if (!(fty->recs_tail->flags & RCT_SKIP)
	  && fty->recs_tail->ft == fty->recs_tail->lt)
	{
	  msg (SE, _("No input commands (DATA LIST, REPEATING DATA) "
		     "on above RECORD TYPE."));
	  goto fail;
	}
    }
  else
    {
      msg (SE, _("No commands between FILE TYPE and END FILE TYPE."));
      goto fail;
    }

  f_trns = n_trns;

  return lex_end_of_command ();

 fail:
  /* Come here on discovering catastrophic error. */
  err_cond_fail ();
  discard_variables ();
  return CMD_FAILURE;
}

/* FILE TYPE runtime. */

/*static void read_from_file_type_mixed(void);
   static void read_from_file_type_grouped(void);
   static void read_from_file_type_nested(void); */

/* Reads any number of cases into case C and calls write_case()
   for each one.  Compare data-list.c:read_from_data_list. */
static void
file_type_source_read (struct case_source *source,
                       struct ccase *c,
                       write_case_func *write_case UNUSED,
                       write_case_data wc_data UNUSED)
{
  struct file_type_pgm *fty = source->aux;
  char *line;
  int len;

  struct fmt_spec format;

  dfm_push (fty->handle);

  format.type = fty->record.fmt;
  format.w = fty->record.nc;
  format.d = 0;
  while (NULL != (line = dfm_get_record (fty->handle, &len)))
    {
      struct record_type *iter;
      union value v;
      int i;

      if (formats[fty->record.fmt].cat & FCAT_STRING)
	{
	  struct data_in di;
	  
	  v.c = c->data[fty->record.v->fv].s;

	  data_in_finite_line (&di, line, len,
			       fty->record.fc, fty->record.fc + fty->record.nc);
	  di.v = (union value *) v.c;
	  di.flags = 0;
	  di.f1 = fty->record.fc;
	  di.format = format;
	  data_in (&di);

	  for (iter = fty->recs_head; iter; iter = iter->next)
	    {
	      if (iter->flags & RCT_OTHER)
		goto found;
	      for (i = 0; i < iter->nv; i++)
		if (!memcmp (iter->v[i].c, v.c, fty->record.nc))
		  goto found;
	    }
	  if (fty->wild)
	    msg (SW, _("Unknown record type \"%.*s\"."), fty->record.nc, v.c);
	}
      else
	{
	  struct data_in di;

	  data_in_finite_line (&di, line, len,
			       fty->record.fc, fty->record.fc + fty->record.nc);
	  di.v = &v;
	  di.flags = 0;
	  di.f1 = fty->record.fc;
	  di.format = format;
	  data_in (&di);

	  memcpy (&c->data[fty->record.v->fv].f, &v.f, sizeof v.f);
	  for (iter = fty->recs_head; iter; iter = iter->next)
	    {
	      if (iter->flags & RCT_OTHER)
		goto found;
	      for (i = 0; i < iter->nv; i++)
		if (iter->v[i].f == v.f)
		  goto found;
	    }
	  if (fty->wild)
	    msg (SW, _("Unknown record type %g."), v.f);
	}
      dfm_fwd_record (fty->handle);
      continue;

    found:
      /* Arrive here if there is a matching record_type, which is in
         iter. */
      dfm_fwd_record (fty->handle);
    }

/*  switch(fty->type)
   {
   case FTY_MIXED: read_from_file_type_mixed(); break;
   case FTY_GROUPED: read_from_file_type_grouped(); break;
   case FTY_NESTED: read_from_file_type_nested(); break;
   default: assert(0);
   } */

  dfm_pop (fty->handle);
}

static void
file_type_source_destroy (struct case_source *source)
{
  struct file_type_pgm *fty = source->aux;
  struct record_type *iter, *next;

  cancel_transformations ();
  for (iter = fty->recs_head; iter; iter = next)
    {
      next = iter->next;
      free (iter);
    }
}

const struct case_source_class file_type_source_class =
  {
    "FILE TYPE",
    NULL,
    file_type_source_read,
    file_type_source_destroy,
  };
