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
#include <assert.h>
#include <stdlib.h>
#include "alloc.h"
#include "approx.h"
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
  };

/* Current FILE TYPE input program. */
static struct file_type_pgm fty;

static int parse_col_spec (struct col_spec *, const char *);
static void create_col_var (struct col_spec *c);

/* Parses FILE TYPE command. */
int
cmd_file_type (void)
{
  /* Initialize. */
  discard_variables ();
  fty.handle = inline_file;
  fty.record.name[0] = 0;
  fty.case_sbc.name[0] = 0;
  fty.wild = fty.duplicate = fty.missing = fty.ordered = 0;
  fty.had_rec_type = 0;
  fty.recs_head = fty.recs_tail = NULL;

  lex_match_id ("TYPE");
  if (lex_match_id ("MIXED"))
    fty.type = FTY_MIXED;
  else if (lex_match_id ("GROUPED"))
    {
      fty.type = FTY_GROUPED;
      fty.wild = 1;
      fty.duplicate = 1;
      fty.missing = 1;
      fty.ordered = 1;
    }
  else if (lex_match_id ("NESTED"))
    fty.type = FTY_NESTED;
  else
    {
      msg (SE, _("MIXED, GROUPED, or NESTED expected."));
      return CMD_FAILURE;
    }

  while (token != '.')
    {
      if (lex_match_id ("FILE"))
	{
	  lex_match ('=');
	  fty.handle = fh_parse_file_handle ();
	  if (!fty.handle)
	    return CMD_FAILURE;
	}
      else if (lex_match_id ("RECORD"))
	{
	  lex_match ('=');
	  if (!parse_col_spec (&fty.record, "####RECD"))
	    return CMD_FAILURE;
	}
      else if (lex_match_id ("CASE"))
	{
	  if (fty.type == FTY_MIXED)
	    {
	      msg (SE, _("The CASE subcommand is not valid on FILE TYPE "
			 "MIXED."));
	      return CMD_FAILURE;
	    }
	  
	  lex_match ('=');
	  if (!parse_col_spec (&fty.case_sbc, "####CASE"))
	    return CMD_FAILURE;
	}
      else if (lex_match_id ("WILD"))
	{
	  lex_match ('=');
	  if (lex_match_id ("WARN"))
	    fty.wild = 1;
	  else if (lex_match_id ("NOWARN"))
	    fty.wild = 0;
	  else
	    {
	      msg (SE, _("WARN or NOWARN expected after WILD."));
	      return CMD_FAILURE;
	    }
	}
      else if (lex_match_id ("DUPLICATE"))
	{
	  if (fty.type == FTY_MIXED)
	    {
	      msg (SE, _("The DUPLICATE subcommand is not valid on "
			 "FILE TYPE MIXED."));
	      return CMD_FAILURE;
	    }

	  lex_match ('=');
	  if (lex_match_id ("WARN"))
	    fty.duplicate = 1;
	  else if (lex_match_id ("NOWARN"))
	    fty.duplicate = 0;
	  else if (lex_match_id ("CASE"))
	    {
	      if (fty.type != FTY_NESTED)
		{
		  msg (SE, _("DUPLICATE=CASE is only valid on "
			     "FILE TYPE NESTED."));
		  return CMD_FAILURE;
		}
	      
	      fty.duplicate = 2;
	    }
	  else
	    {
	      msg (SE, _("WARN%s expected after DUPLICATE."),
		   (fty.type == FTY_NESTED ? _(", NOWARN, or CASE")
		    : _(" or NOWARN")));
	      return CMD_FAILURE;
	    }
	}
      else if (lex_match_id ("MISSING"))
	{
	  if (fty.type == FTY_MIXED)
	    {
	      msg (SE, _("The MISSING subcommand is not valid on "
			 "FILE TYPE MIXED."));
	      return CMD_FAILURE;
	    }
	  
	  lex_match ('=');
	  if (lex_match_id ("NOWARN"))
	    fty.missing = 0;
	  else if (lex_match_id ("WARN"))
	    fty.missing = 1;
	  else
	    {
	      msg (SE, _("WARN or NOWARN after MISSING."));
	      return CMD_FAILURE;
	    }
	}
      else if (lex_match_id ("ORDERED"))
	{
	  if (fty.type != FTY_GROUPED)
	    {
	      msg (SE, _("ORDERED is only valid on FILE TYPE GROUPED."));
	      return CMD_FAILURE;
	    }
	  
	  lex_match ('=');
	  if (lex_match_id ("YES"))
	    fty.ordered = 1;
	  else if (lex_match_id ("NO"))
	    fty.ordered = 0;
	  else
	    {
	      msg (SE, _("YES or NO expected after ORDERED."));
	      return CMD_FAILURE;
	    }
	}
      else
	{
	  lex_error (_("while expecting a valid subcommand"));
	  return CMD_FAILURE;
	}
    }

  if (fty.record.name[0] == 0)
    {
      msg (SE, _("The required RECORD subcommand was not present."));
      return CMD_FAILURE;
    }

  if (fty.type == FTY_GROUPED)
    {
      if (fty.case_sbc.name[0] == 0)
	{
	  msg (SE, _("The required CASE subcommand was not present."));
	  return CMD_FAILURE;
	}
      
      if (!strcmp (fty.case_sbc.name, fty.record.name))
	{
	  msg (SE, _("CASE and RECORD must specify different variable "
		     "names."));
	  return CMD_FAILURE;
	}
    }

  default_handle = fty.handle;

  vfm_source = &file_type_source;
  create_col_var (&fty.record);
  if (fty.case_sbc.name[0])
    create_col_var (&fty.case_sbc);

  return CMD_SUCCESS;
}

/* Creates a variable with attributes specified by struct col_spec C, and
   stores it into C->V. */
static void
create_col_var (struct col_spec *c)
{
  int type;
  int width;

  type = (formats[c->fmt].cat & FCAT_STRING) ? ALPHA : NUMERIC;
  if (type == ALPHA)
    width = c->nc;
  else
    width = 0;
  c->v = force_create_variable (&default_dict, c->name, type, width);
}

/* Parses variable, column, type specifications for a variable. */
static int
parse_col_spec (struct col_spec *c, const char *def_name)
{
  struct fmt_spec spec;

  if (token == T_ID)
    {
      strcpy (c->name, tokid);
      lex_get ();
    }
  else
    strcpy (c->name, def_name);

  if (!lex_force_int ())
    return 0;
  c->fc = lex_integer ();
  if (c->fc < 1)
    {
      msg (SE, _("Column value must be positive."));
      return 0;
    }
  lex_get ();

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

/* Structure being filled in by internal_cmd_record_type. */
static struct record_type rct;

static int internal_cmd_record_type (void);

/* Parse the RECORD TYPE command. */
int
cmd_record_type (void)
{
  int result = internal_cmd_record_type ();

  if (result == CMD_FAILURE)
    {
      int i;

      if (formats[fty.record.fmt].cat & FCAT_STRING)
	for (i = 0; i < rct.nv; i++)
	  free (rct.v[i].c);
      free (rct.v);
    }

  return result;
}

static int
internal_cmd_record_type (void)
{
  /* Initialize the record_type structure. */
  rct.next = NULL;
  rct.flags = 0;
  if (fty.duplicate)
    rct.flags |= RCT_DUPLICATE;
  if (fty.missing)
    rct.flags |= RCT_MISSING;
  rct.v = NULL;
  rct.nv = 0;
  rct.ft = n_trns;
  if (fty.case_sbc.name[0])
    rct.case_sbc = fty.case_sbc;
#if __CHECKER__
  else
    memset (&rct.case_sbc, 0, sizeof rct.case_sbc);
  rct.lt = -1;
#endif

  /* Make sure we're inside a FILE TYPE structure. */
  if (pgm_state != STATE_INPUT || vfm_source != &file_type_source)
    {
      msg (SE, _("This command may only appear within a "
		 "FILE TYPE/END FILE TYPE structure."));
      return CMD_FAILURE;
    }

  if (fty.recs_tail && (fty.recs_tail->flags & RCT_OTHER))
    {
      msg (SE, _("OTHER may appear only on the last RECORD TYPE command."));
      return CMD_FAILURE;
    }
      
  if (fty.recs_tail)
    {
      fty.recs_tail->lt = n_trns - 1;
      if (!(fty.recs_tail->flags & RCT_SKIP)
	  && fty.recs_tail->ft == fty.recs_tail->lt)
	{
	  msg (SE, _("No input commands (DATA LIST, REPEATING DATA) "
		     "for above RECORD TYPE."));
	  return CMD_FAILURE;
	}
    }

  lex_match_id ("RECORD");
  lex_match_id ("TYPE");

  /* Parse record type values. */
  if (lex_match_id ("OTHER"))
    rct.flags |= RCT_OTHER;
  else
    {
      int mv = 0;

      while (token == T_NUM || token == T_STRING)
	{
	  if (rct.nv >= mv)
	    {
	      mv += 16;
	      rct.v = xrealloc (rct.v, mv * sizeof *rct.v);
	    }

	  if (formats[fty.record.fmt].cat & FCAT_STRING)
	    {
	      if (!lex_force_string ())
		return CMD_FAILURE;
	      rct.v[rct.nv].c = xmalloc (fty.record.nc + 1);
	      st_bare_pad_copy (rct.v[rct.nv].c, ds_value (&tokstr),
				fty.record.nc + 1);
	    }
	  else
	    {
	      if (!lex_force_num ())
		return CMD_FAILURE;
	      rct.v[rct.nv].f = tokval;
	    }
	  rct.nv++;
	  lex_get ();

	  lex_match (',');
	}
    }

  /* Parse the rest of the subcommands. */
  while (token != '.')
    {
      if (lex_match_id ("SKIP"))
	rct.flags |= RCT_SKIP;
      else if (lex_match_id ("CASE"))
	{
	  if (fty.type == FTY_MIXED)
	    {
	      msg (SE, _("The CASE subcommand is not allowed on "
			 "the RECORD TYPE command for FILE TYPE MIXED."));
	      return CMD_FAILURE;
	    }

	  lex_match ('=');
	  if (!parse_col_spec (&rct.case_sbc, ""))
	    return CMD_FAILURE;
	  if (rct.case_sbc.name[0])
	    {
	      msg (SE, _("No variable name may be specified for the "
			 "CASE subcommand on RECORD TYPE."));
	      return CMD_FAILURE;
	    }
	  
	  if ((formats[rct.case_sbc.fmt].cat ^ formats[fty.case_sbc.fmt].cat)
	      & FCAT_STRING)
	    {
	      msg (SE, _("The CASE column specification on RECORD TYPE "
			 "must give a format specifier that is the "
			 "same type as that of the CASE column "
			 "specification given on FILE TYPE."));
	      return CMD_FAILURE;
	    }
	}
      else if (lex_match_id ("DUPLICATE"))
	{
	  lex_match ('=');
	  if (lex_match_id ("WARN"))
	    rct.flags |= RCT_DUPLICATE;
	  else if (lex_match_id ("NOWARN"))
	    rct.flags &= ~RCT_DUPLICATE;
	  else
	    {
	      msg (SE, _("WARN or NOWARN expected on DUPLICATE "
			 "subcommand."));
	      return CMD_FAILURE;
	    }
	}
      else if (lex_match_id ("MISSING"))
	{
	  lex_match ('=');
	  if (lex_match_id ("WARN"))
	    rct.flags |= RCT_MISSING;
	  else if (lex_match_id ("NOWARN"))
	    rct.flags &= ~RCT_MISSING;
	  else
	    {
	      msg (SE, _("WARN or NOWARN expected on MISSING subcommand."));
	      return CMD_FAILURE;
	    }
	}
      else if (lex_match_id ("SPREAD"))
	{
	  lex_match ('=');
	  if (lex_match_id ("YES"))
	    rct.flags |= RCT_SPREAD;
	  else if (lex_match_id ("NO"))
	    rct.flags &= ~RCT_SPREAD;
	  else
	    {
	      msg (SE, _("YES or NO expected on SPREAD subcommand."));
	      return CMD_FAILURE;
	    }
	}
      else
	{
	  lex_error (_("while expecting a valid subcommand"));
	  return CMD_FAILURE;
	}
    }

  if (fty.recs_head)
    fty.recs_tail = fty.recs_tail->next = xmalloc (sizeof *fty.recs_tail);
  else
    fty.recs_head = fty.recs_tail = xmalloc (sizeof *fty.recs_tail);
  memcpy (fty.recs_tail, &rct, sizeof *fty.recs_tail);

  return CMD_SUCCESS;
}

/* END FILE TYPE. */

int
cmd_end_file_type (void)
{
  if (pgm_state != STATE_INPUT || vfm_source != &file_type_source)
    {
      msg (SE, _("This command may only appear within a "
		 "FILE TYPE/END FILE TYPE structure."));
      return CMD_FAILURE;
    }

  lex_match_id ("TYPE");

  if (fty.recs_tail)
    {
      fty.recs_tail->lt = n_trns - 1;
      if (!(fty.recs_tail->flags & RCT_SKIP)
	  && fty.recs_tail->ft == fty.recs_tail->lt)
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

/* Reads any number of cases into temp_case and calls write_case() for
   each one.  Compare data-list.c:read_from_data_list. */
static void
file_type_source_read (void)
{
  char *line;
  int len;

  struct fmt_spec format;

  dfm_push (fty.handle);

  format.type = fty.record.fmt;
  format.w = fty.record.nc;
  format.d = 0;
  while (NULL != (line = dfm_get_record (fty.handle, &len)))
    {
      struct record_type *iter;
      union value v;
      int i;

      if (formats[fty.record.fmt].cat & FCAT_STRING)
	{
	  struct data_in di;
	  
	  v.c = temp_case->data[fty.record.v->fv].s;

	  data_in_finite_line (&di, line, len,
			       fty.record.fc, fty.record.fc + fty.record.nc);
	  di.v = (union value *) v.c;
	  di.flags = 0;
	  di.f1 = fty.record.fc;
	  di.format = format;
	  data_in (&di);

	  for (iter = fty.recs_head; iter; iter = iter->next)
	    {
	      if (iter->flags & RCT_OTHER)
		goto found;
	      for (i = 0; i < iter->nv; i++)
		if (!memcmp (iter->v[i].c, v.c, fty.record.nc))
		  goto found;
	    }
	  if (fty.wild)
	    msg (SW, _("Unknown record type \"%.*s\"."), fty.record.nc, v.c);
	}
      else
	{
	  struct data_in di;

	  data_in_finite_line (&di, line, len,
			       fty.record.fc, fty.record.fc + fty.record.nc);
	  di.v = &v;
	  di.flags = 0;
	  di.f1 = fty.record.fc;
	  di.format = format;
	  data_in (&di);

	  memcpy (&temp_case->data[fty.record.v->fv].f, &v.f, sizeof v.f);
	  for (iter = fty.recs_head; iter; iter = iter->next)
	    {
	      if (iter->flags & RCT_OTHER)
		goto found;
	      for (i = 0; i < iter->nv; i++)
		if (approx_eq (iter->v[i].f, v.f))
		  goto found;
	    }
	  if (fty.wild)
	    msg (SW, _("Unknown record type %g."), v.f);
	}
      dfm_fwd_record (fty.handle);
      continue;

    found:
      /* Arrive here if there is a matching record_type, which is in
         iter. */
      dfm_fwd_record (fty.handle);
    }

/*  switch(fty.type)
   {
   case FTY_MIXED: read_from_file_type_mixed(); break;
   case FTY_GROUPED: read_from_file_type_grouped(); break;
   case FTY_NESTED: read_from_file_type_nested(); break;
   default: assert(0);
   } */

  dfm_pop (fty.handle);
}

static void
file_type_source_destroy_source (void)
{
  struct record_type *iter, *next;

  cancel_transformations ();
  for (iter = fty.recs_head; iter; iter = next)
    {
      next = iter->next;
      free (iter);
    }
}

struct case_stream file_type_source =
  {
    NULL,
    file_type_source_read,
    NULL,
    NULL,
    file_type_source_destroy_source,
    NULL,
    "FILE TYPE",
  };
