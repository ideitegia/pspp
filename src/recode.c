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
#include <ctype.h>
#include <stdlib.h>
#include "alloc.h"
#include "approx.h"
#include "cases.h"
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "magic.h"
#include "str.h"
#include "var.h"

#include "debug-print.h"

/* Definitions. */

enum
  {
    RCD_END,			/* sentinel value */
    RCD_USER,			/* user-missing => one */
    RCD_SINGLE,			/* one => one */
    RCD_HIGH,			/* x > a => one */
    RCD_LOW,			/* x < b => one */
    RCD_RANGE,			/* b < x < a => one */
    RCD_ELSE,			/* any but SYSMIS => one */
    RCD_CONVERT			/* "123" => 123 */
  };

/* Describes how to recode a single value or range of values into a
   single value.  */
struct coding
  {
    int type;			/* RCD_* */
    union value f1, f2;		/* Describe value or range as src.  Long
				   strings are stored in `c'. */
    union value t;		/* Describes value as dest. Long strings in `c'. */
  };

/* Describes how to recode a single variable. */
struct rcd_var
  {
    struct rcd_var *next;

    unsigned flags;		/* RCD_SRC_* | RCD_DEST_* | RCD_MISC_* */

    struct variable *src;	/* Source variable. */
    struct variable *dest;	/* Destination variable. */
    char dest_name[9];		/* Name of dest variable if we're creating it. */

    int has_sysmis;		/* Do we recode for SYSMIS? */
    union value sysmis;		/* Coding for SYSMIS (if src is numeric). */

    struct coding *map;		/* Coding for other values. */
    int nmap, mmap;		/* Length of map, max capacity of map. */
  };

/* RECODE transformation. */
struct recode_trns
  {
    struct trns_header h;
    struct rcd_var *codings;
  };

/* What we're recoding from (`src'==`source'). */
#define RCD_SRC_ERROR		0000u	/* Bad value for src. */
#define RCD_SRC_NUMERIC		0001u	/* Src is numeric. */
#define RCD_SRC_STRING		0002u	/* Src is short string. */
#define RCD_SRC_MASK		0003u	/* AND mask to isolate src bits. */

/* What we're recoding to (`dest'==`destination'). */
#define RCD_DEST_ERROR		0000u	/* Bad value for dest. */
#define RCD_DEST_NUMERIC	0004u	/* Dest is numeric. */
#define RCD_DEST_STRING		0010u	/* Dest is short string. */
#define RCD_DEST_MASK		0014u	/* AND mask to isolate dest bits. */

/* Miscellaneous bits. */
#define RCD_MISC_CREATE		0020u	/* We create dest var (numeric only) */
#define RCD_MISC_DUPLICATE	0040u	/* This var_info has the same MAP
					   value as the previous var_info.
					   Prevents redundant free()ing. */
#define RCD_MISC_MISSING	0100u	/* Encountered MISSING or SYSMIS in
					   this input spec. */

static int parse_dest_spec (struct rcd_var * rcd, union value *v,
			    size_t *max_dst_width);
static int parse_src_spec (struct rcd_var * rcd, int type, size_t max_src_width);
static int recode_trns_proc (struct trns_header *, struct ccase *);
static void recode_trns_free (struct trns_header *);
static double convert_to_double (char *, int);

#if DEBUGGING
static void debug_print (struct rcd_var * head);
#endif

/* Parser. */

/* First transformation in the list.  rcd is in this list. */
static struct rcd_var *head;

/* Variables in the current part of the recoding. */
struct variable **v;
int nv;

/* Parses the RECODE transformation. */
int
cmd_recode (void)
{
  int i;

  /* Transformation that we're constructing. */
  struct rcd_var *rcd;

  /* Type of the src variables. */
  int type;

  /* Length of longest src string. */
  size_t max_src_width;

  /* Length of longest dest string. */
  size_t max_dst_width;

  /* For stepping through, constructing the linked list of
     recodings. */
  struct rcd_var *iter;

  /* The real transformation, just a wrapper for a list of
     rcd_var's. */
  struct recode_trns *trns;

  lex_match_id ("RECODE");

  /* Parses each specification between slashes. */
  head = rcd = xmalloc (sizeof *rcd);
  for (;;)
    {
      /* Whether we've already encountered a specification for SYSMIS. */
      int had_sysmis = 0;

      /* Initialize this rcd_var to ensure proper cleanup. */
      rcd->next = NULL;
      rcd->map = NULL;
      rcd->nmap = rcd->mmap = 0;
      rcd->has_sysmis = 0;
      rcd->sysmis.f = 0;

      /* Parse variable names. */
      if (!parse_variables (default_dict, &v, &nv, PV_SAME_TYPE))
	goto lossage;

      /* Ensure all variables are same type; find length of longest
         source variable. */
      type = v[0]->type;
      max_src_width = v[0]->width;

      if (type == ALPHA)
	for (i = 0; i < nv; i++)
	  if (v[i]->width > (int) max_src_width)
	    max_src_width = v[i]->width;

      /* Set up flags. */
      rcd->flags = 0;
      if (type == NUMERIC)
	rcd->flags |= RCD_SRC_NUMERIC;
      else
	rcd->flags |= RCD_SRC_STRING;

      /* Parse each coding in parentheses. */
      max_dst_width = 0;
      if (!lex_force_match ('('))
	goto lossage;
      for (;;) 
	{
	  /* Get the input value (before the `='). */
	  int mark = rcd->nmap;
	  int code = parse_src_spec (rcd, type, max_src_width);
	  if (!code)
	    goto lossage;

	  /* ELSE is the same as any other input spec except that it
	     precludes later sysmis specifications. */
	  if (code == 3)
	    {
	      had_sysmis = 1;
	      code = 1;
	    }

	  /* If keyword CONVERT was specified, there is no output
	     specification.  */
	  if (code == 1)
	    {
	      union value output;

	      /* Get the output value (after the `='). */
	      lex_get ();	/* Skip `='. */
	      if (!parse_dest_spec (rcd, &output, &max_dst_width))
		goto lossage;

	      /* Set the value for SYSMIS if requested and if we don't
	         already have one. */
	      if ((rcd->flags & RCD_MISC_MISSING) && !had_sysmis)
		{
		  rcd->has_sysmis = 1;
		  if ((rcd->flags & RCD_DEST_MASK) == RCD_DEST_NUMERIC)
		    rcd->sysmis.f = output.f;
		  else
		    rcd->sysmis.c = xstrdup (output.c);
		  had_sysmis = 1;

		  rcd->flags &= ~RCD_MISC_MISSING;
		}

	      /* Since there may be multiple input values for a single
	         output, the output value need to propagated among all
	         of them. */
	      if ((rcd->flags & RCD_DEST_MASK) == RCD_DEST_NUMERIC)
		for (i = mark; i < rcd->nmap; i++)
		  rcd->map[i].t.f = output.f;
	      else
		{
		  for (i = mark; i < rcd->nmap; i++)
		    rcd->map[i].t.c = xstrdup (output.c);
		  free (output.c);
		}
	    }
	  lex_get ();		/* Skip `)'. */
	  if (!lex_match ('('))
	    break;
	}

      /* Append sentinel value. */
      rcd->map[rcd->nmap++].type = RCD_END;

      /* Since multiple variables may use the same recodings, it is
         necessary to propogate the codings to all of them. */
      rcd->src = v[0];
      rcd->dest = v[0];
      rcd->dest_name[0] = 0;
      iter = rcd;
      for (i = 1; i < nv; i++)
	{
	  iter = iter->next = xmalloc (sizeof *iter);
	  iter->next = NULL;
	  iter->flags = rcd->flags | RCD_MISC_DUPLICATE;
	  iter->src = v[i];
	  iter->dest = v[i];
	  iter->dest_name[0] = 0;
	  iter->has_sysmis = rcd->has_sysmis;
	  iter->sysmis = rcd->sysmis;
	  iter->map = rcd->map;
	}

      if (lex_match_id ("INTO"))
	{
	  char **names;
	  int nnames;

	  int success = 0;

	  if (!parse_mixed_vars (&names, &nnames, PV_NONE))
	    goto lossage;

	  if (nnames != nv)
	    {
	      for (i = 0; i < nnames; i++)
		free (names[i]);
	      free (names);
	      msg (SE, _("%d variable(s) cannot be recoded into "
			 "%d variable(s).  Specify the same number "
			 "of variables as input and output variables."),
		   nv, nnames);
	      goto lossage;
	    }

	  if ((rcd->flags & RCD_DEST_MASK) == RCD_DEST_STRING)
	    for (i = 0, iter = rcd; i < nv; i++, iter = iter->next)
	      {
		struct variable *v = dict_lookup_var (default_dict, names[i]);

		if (!v)
		  {
		    msg (SE, _("There is no string variable named "
			 "%s.  (All string variables specified "
			 "on INTO must already exist.  Use the "
			 "STRING command to create a string "
			 "variable.)"), names[i]);
		    goto INTO_fail;
		  }
		if (v->type != ALPHA)
		  {
		    msg (SE, _("Type mismatch between input and output "
			 "variables.  Output variable %s is not "
			 "a string variable, but all the input "
			 "variables are string variables."), v->name);
		    goto INTO_fail;
		  }
		if (v->width > (int) max_dst_width)
		  max_dst_width = v->width;
		iter->dest = v;
	      }
	  else
	    for (i = 0, iter = rcd; i < nv; i++, iter = iter->next)
	      {
		struct variable *v = dict_lookup_var (default_dict, names[i]);

		if (v)
		  {
		    if (v->type != NUMERIC)
		      {
			msg (SE, _("Type mismatch after INTO: %s "
				   "is not a numeric variable."), v->name);
			goto INTO_fail;
		      }
		    else
		      iter->dest = v;
		  }
		else
		  strcpy (iter->dest_name, names[i]);
	      }
	  success = 1;

	  /* Note that regardless of whether we succeed or fail,
	     flow-of-control comes here.  `success' is the important
	     factor.  Ah, if C had garbage collection...  */
	INTO_fail:
	  for (i = 0; i < nnames; i++)
	    free (names[i]);
	  free (names);
	  if (!success)
	    goto lossage;
	}
      else
	{
	  if (max_src_width > max_dst_width)
	    max_dst_width = max_src_width;

	  if ((rcd->flags & RCD_SRC_MASK) == RCD_SRC_NUMERIC
	      && (rcd->flags & RCD_DEST_MASK) != RCD_DEST_NUMERIC)
	    {
	      msg (SE, _("INTO must be used when the input values are "
			 "numeric and output values are string."));
	      goto lossage;
	    }
	  
	  if ((rcd->flags & RCD_SRC_MASK) != RCD_SRC_NUMERIC
	      && (rcd->flags & RCD_DEST_MASK) == RCD_DEST_NUMERIC)
	    {
	      msg (SE, _("INTO must be used when the input values are "
			 "string and output values are numeric."));
	      goto lossage;
	    }
	}

      if ((rcd->flags & RCD_DEST_MASK) == RCD_DEST_STRING)
	{
	  struct coding *cp;

	  for (cp = rcd->map; cp->type != RCD_END; cp++)
	    if (cp->t.c)
	      {
		if (strlen (cp->t.c) < max_dst_width)
		  {
		    /* The NULL is only really necessary for the
		       debugging code. */
		    char *repl = xmalloc (max_dst_width + 1);
		    st_pad_copy (repl, cp->t.c, max_dst_width + 1);
		    free (cp->t.c);
		    cp->t.c = repl;
		  }
		else
		  /* The strings are guaranteed to be in order of
		     nondecreasing length. */
		  break;
	      }
	  
	}

      if (!lex_match ('/'))
	break;
      while (rcd->next)
	rcd = rcd->next;
      rcd = rcd->next = xmalloc (sizeof *rcd);

      free (v);
    }

  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      goto lossage;
    }

  for (rcd = head; rcd; rcd = rcd->next)
    if (rcd->dest_name[0])
      {
	rcd->dest = dict_create_var (default_dict, rcd->dest_name, 0);
	if (!rcd->dest)
	  {
	    /* This can occur if a destname is duplicated.  We could
	       give an error at parse time but I don't care enough. */
	    rcd->dest = dict_lookup_var (default_dict, rcd->dest_name);
	    assert (rcd->dest != NULL);
	  }
	else
	  envector (rcd->dest);
      }

  trns = xmalloc (sizeof *trns);
  trns->h.proc = recode_trns_proc;
  trns->h.free = recode_trns_free;
  trns->codings = head;
  add_transformation ((struct trns_header *) trns);

#if DEBUGGING
  debug_print (head);
#endif

  return CMD_SUCCESS;

 lossage:
  {
    struct recode_trns t;

    t.codings = head;
    recode_trns_free ((struct trns_header *) &t);
    return CMD_FAILURE;
  }
}

static int
parse_dest_spec (struct rcd_var * rcd, union value * v, size_t *max_dst_width)
{
  int flags;

  v->c = NULL;

  if (token == T_NUM)
    {
      v->f = tokval;
      lex_get ();
      flags = RCD_DEST_NUMERIC;
    }
  else if (lex_match_id ("SYSMIS"))
    {
      v->f = SYSMIS;
      flags = RCD_DEST_NUMERIC;
    }
  else if (token == T_STRING)
    {
      size_t max = *max_dst_width;
      size_t toklen = ds_length (&tokstr);
      if (toklen > max)
	max = toklen;
      v->c = xmalloc (max + 1);
      st_pad_copy (v->c, ds_value (&tokstr), max + 1);
      flags = RCD_DEST_STRING;
      *max_dst_width = max;
      lex_get ();
    }
  else if (lex_match_id ("COPY"))
    {
      if ((rcd->flags & RCD_SRC_MASK) == RCD_SRC_NUMERIC)
	{
	  flags = RCD_DEST_NUMERIC;
	  v->f = -SYSMIS;
	}
      else
	{
	  flags = RCD_DEST_STRING;
	  v->c = NULL;
	}
    }

  if ((rcd->flags & RCD_DEST_MASK) == RCD_DEST_ERROR)
    rcd->flags |= flags;
#if 0
  else if (((rcd->flags & RCD_DEST_MASK) == RCD_DEST_NUMERIC
	    && flags != RCD_DEST_NUMERIC)
	   || ((rcd->flags & RCD_DEST_MASK) == RCD_DEST_STRING
	       && flags != RCD_DEST_STRING))
#endif
    else if ((rcd->flags & RCD_DEST_MASK) ^ flags)
      {
	msg (SE, _("Inconsistent output types.  The output values "
		   "must be all numeric or all string."));
	return 0;
      }

  return 1;
}

/* Reads a set of source specifications and returns one of the
   following values: 0 on failure; 1 for normal success; 2 for success
   but with CONVERT as the keyword; 3 for success but with ELSE as the
   keyword. */
static int
parse_src_spec (struct rcd_var * rcd, int type, size_t max_src_width)
{
  struct coding *c;

  for (;;)
    {
      if (rcd->nmap >= rcd->mmap - 1)
	{
	  rcd->mmap += 16;
	  rcd->map = xrealloc (rcd->map, rcd->mmap * sizeof *rcd->map);
	}

      c = &rcd->map[rcd->nmap];
      c->f1.c = c->f2.c = NULL;
      if (lex_match_id ("ELSE"))
	{
	  c->type = RCD_ELSE;
	  rcd->nmap++;
	  return 3;
	}
      else if (type == NUMERIC)
	{
	  if (token == T_ID)
	    {
	      if (lex_match_id ("LO") || lex_match_id ("LOWEST"))
		{
		  if (!lex_force_match_id ("THRU"))
		    return 0;
		  if (lex_match_id ("HI") || lex_match_id ("HIGHEST"))
		    c->type = RCD_ELSE;
		  else if (token == T_NUM)
		    {
		      c->type = RCD_LOW;
		      c->f1.f = tokval;
		      lex_get ();
		    }
		  else
		    {
		      lex_error (_("following LO THRU"));
		      return 0;
		    }
		}
	      else if (lex_match_id ("MISSING"))
		{
		  c->type = RCD_USER;
		  rcd->flags |= RCD_MISC_MISSING;
		}
	      else if (lex_match_id ("SYSMIS"))
		{
		  c->type = RCD_END;
		  rcd->flags |= RCD_MISC_MISSING;
		}
	      else
		{
		  lex_error (_("in source value"));
		  return 0;
		}
	    }
	  else if (token == T_NUM)
	    {
	      c->f1.f = tokval;
	      lex_get ();
	      if (lex_match_id ("THRU"))
		{
		  if (lex_match_id ("HI") || lex_match_id ("HIGHEST"))
		    c->type = RCD_HIGH;
		  else if (token == T_NUM)
		    {
		      c->type = RCD_RANGE;
		      c->f2.f = tokval;
		      lex_get ();
		    }
		  else
		    {
		      lex_error (NULL);
		      return 0;
		    }
		}
	      else
		c->type = RCD_SINGLE;
	    }
	  else
	    {
	      lex_error (_("in source value"));
	      return 0;
	    }
	}
      else
	{
	  assert (type == ALPHA);
	  if (lex_match_id ("CONVERT"))
	    {
	      if ((rcd->flags & RCD_DEST_MASK) == RCD_DEST_ERROR)
		rcd->flags |= RCD_DEST_NUMERIC;
	      else if ((rcd->flags & RCD_DEST_MASK) != RCD_DEST_NUMERIC)
		{
		  msg (SE, _("Keyword CONVERT may only be used with "
			     "string input values and numeric output "
			     "values."));
		  return 0;
		}

	      c->type = RCD_CONVERT;
	      rcd->nmap++;
	      return 2;
	    }
	  else
	    {
	      /* Only the debugging code needs the NULLs at the ends
	         of the strings.  However, changing code behavior more
	         than necessary based on the DEBUGGING `#define' is just
	         *inviting* bugs. */
	      c->type = RCD_SINGLE;
	      if (!lex_force_string ())
		return 0;
	      c->f1.c = xmalloc (max_src_width + 1);
	      st_pad_copy (c->f1.c, ds_value (&tokstr), max_src_width + 1);
	      lex_get ();
	    }
	}

      if (c->type != RCD_END)
	rcd->nmap++;

      lex_match (',');
      if (token == '=')
	break;
    }
  return 1;
}

/* Data transformation. */

static void
recode_trns_free (struct trns_header * t)
{
  int i;
  struct rcd_var *head, *next;

  head = ((struct recode_trns *) t)->codings;
  while (head)
    {
      if (head->map && !(head->flags & RCD_MISC_DUPLICATE))
	{
	  if (head->flags & RCD_SRC_STRING)
	    for (i = 0; i < head->nmap; i++)
	      switch (head->map[i].type)
		{
		case RCD_RANGE:
		  free (head->map[i].f2.c);
		  /* fall through */
		case RCD_USER:
		case RCD_SINGLE:
		case RCD_HIGH:
		case RCD_LOW:
		  free (head->map[i].f1.c);
		  break;
		case RCD_END:
		case RCD_ELSE:
		case RCD_CONVERT:
		  break;
		default:
		  assert (0);
		}
	  if (head->flags & RCD_DEST_STRING)
	    for (i = 0; i < head->nmap; i++)
	      if (head->map[i].type != RCD_CONVERT && head->map[i].type != RCD_END)
		free (head->map[i].t.c);
	  free (head->map);
	}
      next = head->next;
      free (head);
      head = next;
    }
}

static inline struct coding *
find_src_numeric (struct rcd_var * v, struct ccase * c)
{
  double cmp = c->data[v->src->fv].f;
  struct coding *cp;

  if (cmp == SYSMIS)
    {
      if (v->sysmis.f != -SYSMIS)
	{
	  if ((v->flags & RCD_DEST_MASK) == RCD_DEST_NUMERIC)
	    c->data[v->dest->fv].f = v->sysmis.f;
	  else
	    memcpy (c->data[v->dest->fv].s, v->sysmis.c,
		    v->dest->width);
	}
      return NULL;
    }

  for (cp = v->map;; cp++)
    switch (cp->type)
      {
      case RCD_END:
	return NULL;
      case RCD_USER:
	if (is_num_user_missing (cmp, v->src))
	  return cp;
	break;
      case RCD_SINGLE:
	if (approx_eq (cmp, cp->f1.f))
	  return cp;
	break;
      case RCD_HIGH:
	if (approx_ge (cmp, cp->f1.f))
	  return cp;
	break;
      case RCD_LOW:
	if (approx_le (cmp, cp->f1.f))
	  return cp;
	break;
      case RCD_RANGE:
	if (approx_in_range (cmp, cp->f1.f, cp->f2.f))
	  return cp;
	break;
      case RCD_ELSE:
	return cp;
      default:
	assert (0);
      }
}

static inline struct coding *
find_src_string (struct rcd_var * v, struct ccase * c)
{
  char *cmp = c->data[v->src->fv].s;
  int w = v->src->width;
  struct coding *cp;

  for (cp = v->map;; cp++)
    switch (cp->type)
      {
      case RCD_END:
	return NULL;
      case RCD_SINGLE:
	if (!memcmp (cp->f1.c, cmp, w))
	  return cp;
	break;
      case RCD_ELSE:
	return cp;
      case RCD_CONVERT:
	{
	  double f = convert_to_double (cmp, w);
	  if (f != -SYSMIS)
	    {
	      c->data[v->dest->fv].f = f;
	      return NULL;
	    }
	  break;
	}
      default:
	assert (0);
      }
}

static int
recode_trns_proc (struct trns_header * t, struct ccase * c)
{
  struct rcd_var *v;
  struct coding *cp;

  for (v = ((struct recode_trns *) t)->codings; v; v = v->next)
    {
      switch (v->flags & RCD_SRC_MASK)
	{
	case RCD_SRC_NUMERIC:
	  cp = find_src_numeric (v, c);
	  break;
	case RCD_SRC_STRING:
	  cp = find_src_string (v, c);
	  break;
	}
      if (!cp)
	continue;

      /* A matching input value was found. */
      if ((v->flags & RCD_DEST_MASK) == RCD_DEST_NUMERIC)
	{
	  double val = cp->t.f;
	  if (val == -SYSMIS)
	    c->data[v->dest->fv].f = c->data[v->src->fv].f;
	  else
	    c->data[v->dest->fv].f = val;
	}
      else
	{
	  char *val = cp->t.c;
	  if (val == NULL)
	    st_bare_pad_len_copy (c->data[v->dest->fv].s,
				  c->data[v->src->fv].c,
				  v->dest->width, v->src->width);
	  else
	    memcpy (c->data[v->dest->fv].s, cp->t.c, v->dest->width);
	}
    }

  return -1;
}

/* Debug output. */

#if DEBUGGING
static void
dump_dest (struct rcd_var * v, union value * c)
{
  if ((v->flags & RCD_DEST_MASK) == RCD_DEST_NUMERIC)
    if (c->f == SYSMIS)
      printf ("=SYSMIS");
    else if (c->f == -SYSMIS)
      printf ("=COPY");
    else
      printf ("=%g", c->f);
  else if (c->c)
    printf ("=\"%s\"", c->c);
  else
    printf ("=COPY");
}

static void
debug_print (struct rcd_var * head)
{
  struct rcd_var *iter, *start;
  struct coding *c;

  printf ("RECODE\n");
  for (iter = head; iter; iter = iter->next)
    {
      start = iter;
      printf ("  %s%s", iter == head ? "" : "/", iter->src->name);
      while (iter->next && (iter->next->flags & RCD_MISC_DUPLICATE))
	{
	  iter = iter->next;
	  printf (" %s", iter->src->name);
	}
      if (iter->has_sysmis)
	{
	  printf ("(SYSMIS");
	  dump_dest (iter, &iter->sysmis);
	  printf (")");
	}
      for (c = iter->map; c->type != RCD_END; c++)
	{
	  printf ("(");
	  if ((iter->flags & RCD_SRC_MASK) == RCD_SRC_NUMERIC)
	    switch (c->type)
	      {
	      case RCD_END:
		printf (_("!!END!!"));
		break;
	      case RCD_USER:
		printf ("MISSING");
		break;
	      case RCD_SINGLE:
		printf ("%g", c->f1.f);
		break;
	      case RCD_HIGH:
		printf ("%g THRU HIGH", c->f1.f);
		break;
	      case RCD_LOW:
		printf ("LOW THRU %g", c->f1.f);
		break;
	      case RCD_RANGE:
		printf ("%g THRU %g", c->f1.f, c->f2.f);
		break;
	      case RCD_ELSE:
		printf ("ELSE");
		break;
	      default:
		printf (_("!!ERROR!!"));
		break;
	      }
	  else
	    switch (c->type)
	      {
	      case RCD_SINGLE:
		printf ("\"%s\"", c->f1.c);
		break;
	      case RCD_ELSE:
		printf ("ELSE");
		break;
	      case RCD_CONVERT:
		printf ("CONVERT");
		break;
	      default:
		printf (_("!!ERROR!!"));
		break;
	      }
	  if (c->type != RCD_CONVERT)
	    dump_dest (iter, &c->t);
	  printf (")");
	}
      printf ("\n    INTO");
      for (;;)
	{
	  printf (" %s",
		start->dest_name[0] ? start->dest_name : start->dest->name);
	  if (start == iter)
	    break;
	  start = start->next;
	}
      printf ("\n");
    }
}
#endif

/* Convert NPTR to a `long int' in base 10.  Returns the long int on
   success, NOT_LONG on failure.  On success stores a pointer to the
   first character after the number into *ENDPTR.  From the GNU C
   library. */
static long int
string_to_long (char *nptr, int width, char **endptr)
{
  int negative;
  register unsigned long int cutoff;
  register unsigned int cutlim;
  register unsigned long int i;
  register char *s;
  register unsigned char c;
  const char *save;

  s = nptr;

  /* Check for a sign.  */
  if (*s == '-')
    {
      negative = 1;
      ++s;
    }
  else if (*s == '+')
    {
      negative = 0;
      ++s;
    }
  else
    negative = 0;
  if (s >= nptr + width)
    return NOT_LONG;

  /* Save the pointer so we can check later if anything happened.  */
  save = s;

  cutoff = ULONG_MAX / 10ul;
  cutlim = ULONG_MAX % 10ul;

  i = 0;
  for (c = *s;;)
    {
      if (isdigit ((unsigned char) c))
	c -= '0';
      else
	break;
      /* Check for overflow.  */
      if (i > cutoff || (i == cutoff && c > cutlim))
	return NOT_LONG;
      else
	i = i * 10ul + c;

      s++;
      if (s >= nptr + width)
	break;
      c = *s;
    }

  /* Check if anything actually happened.  */
  if (s == save)
    return NOT_LONG;

  /* Check for a value that is within the range of `unsigned long
     int', but outside the range of `long int'.  We limit LONG_MIN and
     LONG_MAX by one point because we know that NOT_LONG is out there
     somewhere. */
  if (i > (negative
	   ? -((unsigned long int) LONG_MIN) - 1
	   : ((unsigned long int) LONG_MAX) - 1))
    return NOT_LONG;

  *endptr = s;

  /* Return the result of the appropriate sign.  */
  return (negative ? -i : i);
}

/* Converts S to a double according to format Fx.0.  Returns the value
   found, or -SYSMIS if there was no valid number in s.  WIDTH is the
   length of string S.  From the GNU C library. */
static double
convert_to_double (char *s, int width)
{
  register const char *end = &s[width];

  short int sign;

  /* The number so far.  */
  double num;

  int got_dot;			/* Found a decimal point.  */
  int got_digit;		/* Count of digits.  */

  /* The exponent of the number.  */
  long int exponent;

  /* Eat whitespace.  */
  while (s < end && isspace ((unsigned char) *s))
    ++s;
  if (s >= end)
    return SYSMIS;

  /* Get the sign.  */
  sign = *s == '-' ? -1 : 1;
  if (*s == '-' || *s == '+')
    {
      ++s;
      if (s >= end)
	return -SYSMIS;
    }

  num = 0.0;
  got_dot = 0;
  got_digit = 0;
  exponent = 0;
  for (; s < end; ++s)
    {
      if (isdigit ((unsigned char) *s))
	{
	  got_digit++;

	  /* Make sure that multiplication by 10 will not overflow.  */
	  if (num > DBL_MAX * 0.1)
	    /* The value of the digit doesn't matter, since we have already
	       gotten as many digits as can be represented in a `double'.
	       This doesn't necessarily mean the result will overflow.
	       The exponent may reduce it to within range.

	       We just need to record that there was another
	       digit so that we can multiply by 10 later.  */
	    ++exponent;
	  else
	    num = (num * 10.0) + (*s - '0');

	  /* Keep track of the number of digits after the decimal point.
	     If we just divided by 10 here, we would lose precision.  */
	  if (got_dot)
	    --exponent;
	}
      else if (!got_dot && *s == '.')
	/* Record that we have found the decimal point.  */
	got_dot = 1;
      else
	break;
    }

  if (!got_digit)
    return -SYSMIS;

  if (s < end && (tolower ((unsigned char) (*s)) == 'e'
		  || tolower ((unsigned char) (*s)) == 'd'))
    {
      /* Get the exponent specified after the `e' or `E'.  */
      long int exp;

      s++;
      if (s >= end)
	return -SYSMIS;

      exp = string_to_long (s, end - s, &s);
      if (exp == NOT_LONG || end == s)
	return -SYSMIS;
      exponent += exp;
    }

  while (s < end && isspace ((unsigned char) *s))
    s++;
  if (s < end)
    return -SYSMIS;

  if (num == 0.0)
    return 0.0;

  /* Multiply NUM by 10 to the EXPONENT power,
     checking for overflow and underflow.  */

  if (exponent < 0)
    {
      if (-exponent + got_digit > -(DBL_MIN_10_EXP) + 5
	  || num < DBL_MIN * pow (10.0, (double) -exponent))
	return -SYSMIS;
      num *= pow (10.0, (double) exponent);
    }
  else if (exponent > 0)
    {
      if (num > DBL_MAX * pow (10.0, (double) -exponent))
	return -SYSMIS;
      num *= pow (10.0, (double) exponent);
    }

  return sign > 0 ? num : -num;
}
