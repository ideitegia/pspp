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
#include "avl.h"
#include "command.h"
#include "error.h"
#include "file-handle.h"
#include "lexer.h"
#include "misc.h"
#include "output.h"
#include "sfm.h"
#include "som.h"
#include "tab.h"
#include "var.h"
#include "vector.h"

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

int describe_variable (struct variable *v, struct tab_table *t, int r, int as);
     
/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
sysfile_info_dim (struct tab_table *t, struct outp_driver *d)
{
  static const int max[] = {20, 5, 35, 3, 0};
  const int *p;
  int i;

  for (p = max; *p; p++)
    t->w[p - max] = min (tab_natural_width (t, d, p - max),
			 *p * d->prop_em_width);
  for (i = 0; i < t->nr; i++)
    t->h[i] = tab_natural_height (t, d, i);
}

/* SYSFILE INFO utility. */
int
cmd_sysfile_info (void)
{
  struct file_handle *h;
  struct dictionary *d;
  struct tab_table *t;
  struct sfm_read_info inf;
  int r, nr;
  int i;

  lex_match_id ("SYSFILE");
  lex_match_id ("INFO");

  lex_match_id ("FILE");
  lex_match ('=');

  h = fh_parse_file_handle ();
  if (!h)
    return CMD_FAILURE;

  d = sfm_read_dictionary (h, &inf);
  fh_close_handle (h);
  if (!d)
    return CMD_FAILURE;

  t = tab_create (2, 9, 0);
  tab_vline (t, TAL_1 | TAL_SPACING, 1, 0, 8);
  tab_text (t, 0, 0, TAB_LEFT, _("File:"));
  tab_text (t, 1, 0, TAB_LEFT, fh_handle_filename (h));
  tab_text (t, 0, 1, TAB_LEFT, _("Label:"));
  tab_text (t, 1, 1, TAB_LEFT,
		d->label ? d->label : _("No label."));
  tab_text (t, 0, 2, TAB_LEFT, _("Created:"));
  tab_text (t, 1, 2, TAB_LEFT | TAT_PRINTF, "%s %s by %s",
		inf.creation_date, inf.creation_time, inf.product);
  tab_text (t, 0, 3, TAB_LEFT, _("Endian:"));
  tab_text (t, 1, 3, TAB_LEFT, inf.bigendian ? _("Big.") : _("Little."));
  tab_text (t, 0, 4, TAB_LEFT, _("Variables:"));
  tab_text (t, 1, 4, TAB_LEFT | TAT_PRINTF, "%d",
		d->nvar);
  tab_text (t, 0, 5, TAB_LEFT, _("Cases:"));
  tab_text (t, 1, 5, TAB_LEFT | TAT_PRINTF,
		inf.ncases == -1 ? _("Unknown") : "%d", inf.ncases);
  tab_text (t, 0, 6, TAB_LEFT, _("Type:"));
  tab_text (t, 1, 6, TAB_LEFT, _("System File."));
  tab_text (t, 0, 7, TAB_LEFT, _("Weight:"));
  tab_text (t, 1, 7, TAB_LEFT,
		d->weight_var[0] ? d->weight_var : _("Not weighted."));
  tab_text (t, 0, 8, TAB_LEFT, _("Mode:"));
  tab_text (t, 1, 8, TAB_LEFT | TAT_PRINTF,
		_("Compression %s."), inf.compressed ? _("on") : _("off"));
  tab_dim (t, tab_natural_dimensions);
  tab_submit (t);

  nr = 1 + 2 * d->nvar;
  t = tab_create (4, nr, 1);
  tab_dim (t, sysfile_info_dim);
  tab_headers (t, 0, 0, 1, 0);
  tab_text (t, 0, 0, TAB_LEFT | TAT_TITLE, _("Variable"));
  tab_joint_text (t, 1, 0, 2, 0, TAB_LEFT | TAT_TITLE, _("Description"));
  tab_text (t, 3, 0, TAB_LEFT | TAT_TITLE, _("Position"));
  tab_hline (t, TAL_2, 0, 3, 1);
  for (r = 1, i = 0; i < d->nvar; i++)
    {
      int nvl = d->var[i]->val_lab ? avl_count (d->var[i]->val_lab) : 0;
      
      if (r + 10 + nvl > nr)
	{
	  nr = max (nr * d->nvar / (i + 1), nr);
	  nr += 10 + nvl;
	  tab_realloc (t, 4, nr);
	}

      r = describe_variable (d->var[i], t, r, AS_DICTIONARY);
    }
  tab_box (t, TAL_1, TAL_1, -1, -1, 0, 0, 3, r);
  tab_vline (t, TAL_1, 0, 0, r);
  tab_vline (t, TAL_1, 1, 0, r);
  tab_vline (t, TAL_1, 3, 0, r);
  tab_resize (t, -1, r);
  tab_flags (t, SOMF_NO_TITLE);
  tab_submit (t);

  free_dictionary (d);
  
  return lex_end_of_command ();
}

/* DISPLAY utility. */

static void display_macros (void);
static void display_documents (void);
static void display_variables (struct variable **, int, int);
static void display_vectors (int sorted);

static int cmp_var_by_name (const void *, const void *);

int
cmd_display (void)
{
  /* Whether to sort the list of variables alphabetically. */
  int sorted;

  /* Variables to display. */
  int n;
  struct variable **vl;

  lex_match_id ("DISPLAY");

  if (lex_match_id ("MACROS"))
    display_macros ();
  else if (lex_match_id ("DOCUMENTS"))
    display_documents ();
  else if (lex_match_id ("FILE"))
    {
      som_blank_line ();
      if (!lex_force_match_id ("LABEL"))
	return CMD_FAILURE;
      if (default_dict.label == NULL)
	tab_output_text (TAB_LEFT,
			 _("The active file does not have a file label."));
      else
	{
	  tab_output_text (TAB_LEFT | TAT_TITLE, _("File label:"));
	  tab_output_text (TAB_LEFT | TAT_FIX, default_dict.label);
	}
    }
  else
    {
      static const char *sbc[] =
	{"NAMES", "INDEX", "VARIABLES", "LABELS",
	 "DICTIONARY", "SCRATCH", "VECTORS", NULL};
      const char **cp;
      int as;

      sorted = lex_match_id ("SORTED");

      for (cp = sbc; *cp; cp++)
	if (token == T_ID && lex_id_match (*cp, tokid))
	  {
	    lex_get ();
	    break;
	  }
      as = cp - sbc;

      if (*cp == NULL)
	as = AS_NAMES;

      if (as == AS_VECTOR)
	{
	  display_vectors (sorted);
	  return CMD_SUCCESS;
	}

      lex_match ('/');
      lex_match_id ("VARIABLES");
      lex_match ('=');

      if (token != '.')
	{
	  if (!parse_variables (NULL, &vl, &n, PV_NONE))
	    {
	      free (vl);
	      return CMD_FAILURE;
	    }
	  as = AS_DICTIONARY;
	}
      else
	fill_all_vars (&vl, &n, FV_NONE);

      if (as == AS_SCRATCH)
	{
	  int i, m;
	  for (i = 0, m = n; i < n; i++)
	    if (vl[i]->name[0] != '#')
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
	qsort (vl, n, sizeof *vl, cmp_var_by_name);

      display_variables (vl, n, as);

      free (vl);
    }

  return lex_end_of_command ();
}

static int
cmp_var_by_name (const void *a, const void *b)
{
  return strcmp ((*((struct variable **) a))->name, (*((struct variable **) b))->name);
}

static void
display_macros (void)
{
  som_blank_line ();
  tab_output_text (TAB_LEFT, _("Macros not supported."));
}

static void
display_documents (void)
{
  som_blank_line ();
  if (default_dict.n_documents == 0)
    tab_output_text (TAB_LEFT, _("The active file dictionary does not "
		     "contain any documents."));
  else
    {
      char buf[81];
      int i;

      tab_output_text (TAB_LEFT | TAT_TITLE,
		       _("Documents in the active file:"));
      som_blank_line ();
      buf[80] = 0;
      for (i = 0; i < default_dict.n_documents; i++)
	{
	  int len = 79;

	  memcpy (buf, &default_dict.documents[i * 80], 80);
	  while ((isspace ((unsigned char) buf[len]) || buf[len] == 0)
		 && len > 0)
	    len--;
	  buf[len + 1] = 0;
	  tab_output_text (TAB_LEFT | TAT_FIX | TAT_NOWRAP, buf);
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
      t->w[1] = max (tab_natural_width (t, d, 1), d->prop_em_width * 5);
      t->w[2] = max (tab_natural_width (t, d, 2), d->prop_em_width * 35);
      pc = 3;
    }
  else pc = 1;
  if (_as != AS_NAMES)
    t->w[pc] = tab_natural_width (t, d, pc);

  for (i = 0; i < t->nr; i++)
    t->h[i] = tab_natural_height (t, d, i);
}
  
static void
display_variables (struct variable **vl, int n, int as)
{
  struct variable **vp = vl;		/* Variable pointer. */
  struct tab_table *t;
  int nc;			/* Number of columns. */
  int nr;			/* Number of rows. */
  int pc;			/* `Position column' */
  int r;			/* Current row. */
  int i;

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
  if (as != AS_NAMES)
    {
      pc = (as == AS_INDEX ? 1 : 3);
      tab_text (t, pc, 0, TAB_LEFT | TAT_TITLE, _("Position"));
    }
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
	  int nvl = v->val_lab ? avl_count (v->val_lab) : 0;
      
	  if (r + 10 + nvl > nr)
	    {
	      nr = max (nr * n / (i + 1), nr);
	      nr += 10 + nvl;
	      tab_realloc (t, nc, nr);
	    }

	  r = describe_variable (v, t, r, as);
	} else {
	  tab_text (t, 0, r, TAB_LEFT, v->name);
	  if (as == AS_LABELS)
	    tab_joint_text (t, 1, r, 2, r, TAB_LEFT,
			    v->label == NULL ? "(no label)" : v->label);
	  if (as != AS_NAMES)
	    {
	      tab_text (t, pc, r, TAT_PRINTF, "%d", v->index + 1);
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
int 
describe_variable (struct variable *v, struct tab_table *t, int r, int as)
{
  /* Put the name, var label, and position into the first row. */
  tab_text (t, 0, r, TAB_LEFT, v->name);
  tab_text (t, 3, r, TAT_PRINTF, "%d", v->index + 1);

  if (as == AS_DICTIONARY && v->label)
    {
      tab_joint_text (t, 1, r, 2, r, TAB_LEFT, v->label);
      r++;
    }
  
  /* Print/write format, or print and write formats. */
  if (v->print.type == v->write.type
      && v->print.w == v->write.w
      && v->print.d == v->write.d)
    {
      tab_joint_text (t, 1, r, 2, r, TAB_LEFT | TAT_PRINTF, _("Format: %s"),
		      fmt_to_string (&v->print));
      r++;
    }
  else
    {
      tab_joint_text (t, 1, r, 2, r, TAB_LEFT | TAT_PRINTF,
		      _("Print Format: %s"), fmt_to_string (&v->print));
      r++;
      tab_joint_text (t, 1, r, 2, r, TAB_LEFT | TAT_PRINTF,
		      _("Write Format: %s"), fmt_to_string (&v->write));
      r++;
    }

  /* Missing values if any. */
  if (v->miss_type != MISSING_NONE)
    {
      char buf[80];
      char *cp = stpcpy (buf, _("Missing Values: "));

      if (v->type == NUMERIC)
	switch (v->miss_type)
	  {
	  case MISSING_1:
	    sprintf (cp, "%g", v->missing[0].f);
	    break;
	  case MISSING_2:
	    sprintf (cp, "%g; %g", v->missing[0].f, v->missing[1].f);
	    break;
	  case MISSING_3:
	    sprintf (cp, "%g; %g; %g", v->missing[0].f,
		     v->missing[1].f, v->missing[2].f);
	    break;
	  case MISSING_RANGE:
	    sprintf (cp, "%g THRU %g", v->missing[0].f, v->missing[1].f);
	    break;
	  case MISSING_LOW:
	    sprintf (cp, "LOWEST THRU %g", v->missing[0].f);
	    break;
	  case MISSING_HIGH:
	    sprintf (cp, "%g THRU HIGHEST", v->missing[0].f);
	    break;
	  case MISSING_RANGE_1:
	    sprintf (cp, "%g THRU %g; %g",
		     v->missing[0].f, v->missing[1].f, v->missing[2].f);
	    break;
	  case MISSING_LOW_1:
	    sprintf (cp, "LOWEST THRU %g; %g",
		     v->missing[0].f, v->missing[1].f);
	    break;
	  case MISSING_HIGH_1:
	    sprintf (cp, "%g THRU HIGHEST; %g",
		     v->missing[0].f, v->missing[1].f);
	    break;
	  default:
	    assert (0);
	  }
      else
	{
	  int i;

	  for (i = 0; i < v->miss_type; i++)
	    {
	      if (i != 0)
		cp = stpcpy (cp, "; ");
	      *cp++ = '"';
	      memcpy (cp, v->missing[i].s, v->width);
	      cp += v->width;
	      *cp++ = '"';
	    }
	  *cp = 0;
	}

      tab_joint_text (t, 1, r, 2, r, TAB_LEFT, buf);
      r++;
    }

  /* Value labels. */
  if (as == AS_DICTIONARY && v->val_lab)
    {
      avl_traverser trav;
      struct value_label *vl;
      int nvl = avl_count (v->val_lab);
      int orig_r = r;
      int i;

#if 0
      tab_text (t, 1, r, TAB_LEFT, _("Value"));
      tab_text (t, 2, r, TAB_LEFT, _("Label"));
      r++;
#endif

      tab_hline (t, TAL_1, 1, 2, r);
      avl_traverser_init (trav);
      for (i = 1, vl = avl_traverse (v->val_lab, &trav); vl;
	   i++, vl = avl_traverse (v->val_lab, &trav))
	{
	  char buf[128];

	  if (v->type == ALPHA)
	    {
	      memcpy (buf, vl->v.s, v->width);
	      buf[v->width] = 0;
	    }
	  else
	    sprintf (buf, "%g", vl->v.f);

	  tab_text (t, 1, r, TAB_NONE, buf);
	  tab_text (t, 2, r, TAB_LEFT, vl->s);
	  r++;

	  if (i == nvl) 
	    break;
	}

      for (;;)
	{
	  if (vl == NULL)
	    break;
	  vl = avl_traverse (v->val_lab, &trav);
	}

      tab_vline (t, TAL_1, 2, orig_r, r - 1);
    }

  /* Draw a line below the last row of information on this variable. */
  tab_hline (t, TAL_1, 0, 3, r);

  return r;
}

static int
compare_vectors_by_name (const void *a, const void *b)
{
  return strcmp ((*((struct vector **) a))->name, (*((struct vector **) b))->name);
}

/* Display a list of vectors.  If SORTED is nonzero then they are
   sorted alphabetically. */
static void
display_vectors (int sorted)
{
  struct vector **vl;
  int i;
  struct tab_table *t;

  if (nvec == 0)
    {
      msg (SW, _("No vectors defined."));
      return;
    }

  vl = xmalloc (sizeof *vl * nvec);
  for (i = 0; i < nvec; i++)
    vl[i] = &vec[i];
  if (sorted)
    qsort (vl, nvec, sizeof *vl, compare_vectors_by_name);

  t = tab_create (1, nvec + 1, 0);
  tab_headers (t, 0, 0, 1, 0);
  tab_columns (t, TAB_COL_DOWN, 1);
  tab_dim (t, tab_natural_dimensions);
  tab_hline (t, TAL_1, 0, 0, 1);
  tab_text (t, 0, 0, TAT_TITLE | TAB_LEFT, _("Vector"));
  tab_flags (t, SOMF_NO_TITLE);
  for (i = 0; i < nvec; i++)
    tab_text (t, 0, i + 1, TAB_LEFT, vl[i]->name);
  tab_submit (t);

  free (vl);
}
