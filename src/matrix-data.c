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
#include <ctype.h>
#include <float.h>
#include "algorithm.h"
#include "alloc.h"
#include "command.h"
#include "data-in.h"
#include "dfm.h"
#include "error.h"
#include "file-handle.h"
#include "lexer.h"
#include "misc.h"
#include "pool.h"
#include "str.h"
#include "var.h"
#include "vfm.h"

#include "debug-print.h"

/* FIXME: /N subcommand not implemented.  It should be pretty simple,
   too. */

/* Format type enums. */
enum
  {
    LIST,
    FREE
  };

/* Matrix section enums. */
enum
  {
    LOWER,
    UPPER,
    FULL
  };

/* Diagonal inclusion enums. */
enum
  {
    DIAGONAL,
    NODIAGONAL
  };

/* CONTENTS types. */
enum
  {
    N_VECTOR,
    N_SCALAR,
    N_MATRIX,
    MEAN,
    STDDEV,
    COUNT,
    MSE,
    DFE,
    MAT,
    COV,
    CORR,
    PROX,
    
    LPAREN,
    RPAREN,
    EOC
  };

/* 0=vector, 1=matrix, 2=scalar. */
static int content_type[PROX + 1] = 
  {
    0, 2, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1,
  };

/* Name of each content type. */
static const char *content_names[PROX + 1] =
  {
    "N", "N", "N_MATRIX", "MEAN", "STDDEV", "COUNT", "MSE",
    "DFE", "MAT", "COV", "CORR", "PROX",
  };

/* The data file to be read. */
static struct file_handle *data_file;

/* Format type. */
static int fmt;			/* LIST or FREE. */
static int section;		/* LOWER or UPPER or FULL. */
static int diag;		/* DIAGONAL or NODIAGONAL. */

/* Arena used for all the MATRIX DATA allocations. */
static struct pool *container;

/* ROWTYPE_ specified explicitly in data? */
static int explicit_rowtype;

/* ROWTYPE_, VARNAME_ variables. */
static struct variable *rowtype_, *varname_;

/* Is is per-factor data? */
int is_per_factor[PROX + 1];

/* Single SPLIT FILE variable. */
static struct variable *single_split;

/* Factor variables.  */
static int n_factors;
static struct variable **factors;

/* Number of cells, or -1 if none. */
static int cells;

/* Population N specified by user. */
static int pop_n;

/* CONTENTS subcommand. */
static int contents[EOC * 3 + 1];
static int n_contents;

/* Number of continuous variables. */
static int n_continuous;

/* Index into default_dict.var of first continuous variables. */
static int first_continuous;

static int compare_variables_by_mxd_vartype (const void *pa,
					     const void *pb);
static void read_matrices_without_rowtype (void);
static void read_matrices_with_rowtype (void);
static int string_to_content_type (char *, int *);

#if DEBUGGING
static void debug_print (void);
#endif

int
cmd_matrix_data (void)
{
  unsigned seen = 0;
  
  lex_match_id ("MATRIX");
  lex_match_id ("DATA");

  container = pool_create ();

  discard_variables ();

  data_file = inline_file;
  fmt = LIST;
  section = LOWER;
  diag = DIAGONAL;
  single_split = NULL;
  n_factors = 0;
  factors = NULL;
  cells = -1;
  pop_n = -1;
  n_contents = 0;
  while (token != '.')
    {
      lex_match ('/');

      if (lex_match_id ("VARIABLES"))
	{
	  char **v;
	  int nv;

	  if (seen & 1)
	    {
	      msg (SE, _("VARIABLES subcommand multiply specified."));
	      goto lossage;
	    }
	  seen |= 1;
	  
	  lex_match ('=');
	  if (!parse_DATA_LIST_vars (&v, &nv, PV_NO_DUPLICATE))
	    goto lossage;
	  
	  {
	    int i;

	    for (i = 0; i < nv; i++)
	      if (!strcmp (v[i], "VARNAME_"))
		{
		  msg (SE, _("VARNAME_ cannot be explicitly specified on "
			     "VARIABLES."));
		  for (i = 0; i < nv; i++)
		    free (v[i]);
		  free (v);
		  goto lossage;
		}
	  }
	  
	  {
	    int i;

	    for (i = 0; i < nv; i++)
	      {
		struct variable *new_var;
		
		if (strcmp (v[i], "ROWTYPE_"))
		  {
		    new_var = dict_create_var (default_dict, v[i], 0);
                    assert (new_var != NULL);
		    new_var->p.mxd.vartype = MXD_CONTINUOUS;
		    new_var->p.mxd.subtype = i;
		  }
		else
		  explicit_rowtype = 1;
		free (v[i]);
	      }
	    free (v);
	  }
	  
	  {
	    rowtype_ = dict_create_var (default_dict, "ROWTYPE_", 8);
            assert (rowtype_ != NULL);
	    rowtype_->p.mxd.vartype = MXD_ROWTYPE;
	    rowtype_->p.mxd.subtype = 0;
	  }
	}
      else if (lex_match_id ("FILE"))
	{
	  lex_match ('=');
	  data_file = fh_parse_file_handle ();
	  if (!data_file)
	    goto lossage;
	}
      else if (lex_match_id ("FORMAT"))
	{
	  lex_match ('=');

	  while (token == T_ID)
	    {
	      if (lex_match_id ("LIST"))
		fmt = LIST;
	      else if (lex_match_id ("FREE"))
		fmt = FREE;
	      else if (lex_match_id ("LOWER"))
		section = LOWER;
	      else if (lex_match_id ("UPPER"))
		section = UPPER;
	      else if (lex_match_id ("FULL"))
		section = FULL;
	      else if (lex_match_id ("DIAGONAL"))
		diag = DIAGONAL;
	      else if (lex_match_id ("NODIAGONAL"))
		diag = NODIAGONAL;
	      else 
		{
		  lex_error (_("in FORMAT subcommand"));
		  goto lossage;
		}
	    }
	}
      else if (lex_match_id ("SPLIT"))
	{
	  lex_match ('=');

	  if (seen & 2)
	    {
	      msg (SE, _("SPLIT subcommand multiply specified."));
	      goto lossage;
	    }
	  seen |= 2;
	  
	  if (token != T_ID)
	    {
	      lex_error (_("in SPLIT subcommand"));
	      goto lossage;
	    }
	  
	  if (dict_lookup_var (default_dict, tokid) == NULL
	      && (lex_look_ahead () == '.' || lex_look_ahead () == '/'))
	    {
	      if (!strcmp (tokid, "ROWTYPE_") || !strcmp (tokid, "VARNAME_"))
		{
		  msg (SE, _("Split variable may not be named ROWTYPE_ "
			     "or VARNAME_."));
		  goto lossage;
		}

	      single_split = dict_create_var (default_dict, tokid, 0);
              assert (single_split != NULL);
	      lex_get ();

	      single_split->p.mxd.vartype = MXD_CONTINUOUS;

              dict_set_split_vars (default_dict, &single_split, 1);
	    }
	  else
	    {
	      struct variable **split;
	      int n;

	      if (!parse_variables (default_dict, &split, &n, PV_NO_DUPLICATE))
		goto lossage;

              dict_set_split_vars (default_dict, split, n);
	    }
	  
	  {
            struct variable *const *split = dict_get_split_vars (default_dict);
            size_t split_cnt = dict_get_split_cnt (default_dict);
            int i;

            for (i = 0; i < split_cnt; i++)
              {
		if (split[i]->p.mxd.vartype != MXD_CONTINUOUS)
		  {
		    msg (SE, _("Split variable %s is already another type."),
			 tokid);
		    goto lossage;
		  }
		split[i]->p.mxd.vartype = MXD_SPLIT;
		split[i]->p.mxd.subtype = i;
              }
	  }
	}
      else if (lex_match_id ("FACTORS"))
	{
	  lex_match ('=');
	  
	  if (seen & 4)
	    {
	      msg (SE, _("FACTORS subcommand multiply specified."));
	      goto lossage;
	    }
	  seen |= 4;

	  if (!parse_variables (default_dict, &factors, &n_factors, PV_NONE))
	    goto lossage;
	  
	  {
	    int i;
	    
	    for (i = 0; i < n_factors; i++)
	      {
		if (factors[i]->p.mxd.vartype != MXD_CONTINUOUS)
		  {
		    msg (SE, _("Factor variable %s is already another type."),
			 tokid);
		    goto lossage;
		  }
		factors[i]->p.mxd.vartype = MXD_FACTOR;
		factors[i]->p.mxd.subtype = i;
	      }
	  }
	}
      else if (lex_match_id ("CELLS"))
	{
	  lex_match ('=');
	  
	  if (cells != -1)
	    {
	      msg (SE, _("CELLS subcommand multiply specified."));
	      goto lossage;
	    }

	  if (!lex_integer_p () || lex_integer () < 1)
	    {
	      lex_error (_("expecting positive integer"));
	      goto lossage;
	    }

	  cells = lex_integer ();
	  lex_get ();
	}
      else if (lex_match_id ("N"))
	{
	  lex_match ('=');

	  if (pop_n != -1)
	    {
	      msg (SE, _("N subcommand multiply specified."));
	      goto lossage;
	    }

	  if (!lex_integer_p () || lex_integer () < 1)
	    {
	      lex_error (_("expecting positive integer"));
	      goto lossage;
	    }

	  pop_n = lex_integer ();
	  lex_get ();
	}
      else if (lex_match_id ("CONTENTS"))
	{
	  int inside_parens = 0;
	  unsigned collide = 0;
	  int item;
	  
	  if (seen & 8)
	    {
	      msg (SE, _("CONTENTS subcommand multiply specified."));
	      goto lossage;
	    }
	  seen |= 8;

	  lex_match ('=');
	  
	  {
	    int i;
	    
	    for (i = 0; i <= PROX; i++)
	      is_per_factor[i] = 0;
	  }

	  for (;;)
	    {
	      if (lex_match ('('))
		{
		  if (inside_parens)
		    {
		      msg (SE, _("Nested parentheses not allowed."));
		      goto lossage;
		    }
		  inside_parens = 1;
		  item = LPAREN;
		}
	      else if (lex_match (')'))
		{
		  if (!inside_parens)
		    {
		      msg (SE, _("Mismatched right parenthesis (`(')."));
		      goto lossage;
		    }
		  if (contents[n_contents - 1] == LPAREN)
		    {
		      msg (SE, _("Empty parentheses not allowed."));
		      goto lossage;
		    }
		  inside_parens = 0;
		  item = RPAREN;
		}
	      else 
		{
		  int content_type;
		  int collide_index;
		  
		  if (token != T_ID)
		    {
		      lex_error (_("in CONTENTS subcommand"));
		      goto lossage;
		    }

		  content_type = string_to_content_type (tokid,
							 &collide_index);
		  if (content_type == -1)
		    {
		      lex_error (_("in CONTENTS subcommand"));
		      goto lossage;
		    }
		  lex_get ();

		  if (collide & (1 << collide_index))
		    {
		      msg (SE, _("Content multiply specified for %s."),
			   content_names[content_type]);
		      goto lossage;
		    }
		  collide |= (1 << collide_index);
		  
		  item = content_type;
		  is_per_factor[item] = inside_parens;
		}
	      contents[n_contents++] = item;

	      if (token == '/' || token == '.')
		break;
	    }

	  if (inside_parens)
	    {
	      msg (SE, _("Missing right parenthesis."));
	      goto lossage;
	    }
	  contents[n_contents] = EOC;
	}
      else 
	{
	  lex_error (NULL);
	  goto lossage;
	}
    }
  
  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      goto lossage;
    }
  
  if (!(seen & 1))
    {
      msg (SE, _("Missing VARIABLES subcommand."));
      goto lossage;
    }
  
  if (!n_contents && !explicit_rowtype)
    {
      msg (SW, _("CONTENTS subcommand not specified: assuming file "
		 "contains only CORR matrix."));

      contents[0] = CORR;
      contents[1] = EOC;
      n_contents = 0;
    }

  if (n_factors && !explicit_rowtype && cells == -1)
    {
      msg (SE, _("Missing CELLS subcommand.  CELLS is required "
		 "when ROWTYPE_ is not given in the data and "
		 "factors are present."));
      goto lossage;
    }

  if (explicit_rowtype && single_split)
    {
      msg (SE, _("Split file values must be present in the data when "
		 "ROWTYPE_ is present."));
      goto lossage;
    }
      
  /* Create VARNAME_. */
  {
    varname_ = dict_create_var (default_dict, "VARNAME_", 8);
    assert (varname_ != NULL);
    varname_->p.mxd.vartype = MXD_VARNAME;
    varname_->p.mxd.subtype = 0;
  }
  
  /* Sort the dictionary variables into the desired order for the
     system file output. */
  {
    struct variable **v;
    size_t nv;

    dict_get_vars (default_dict, &v, &nv, 0);
    qsort (v, nv, sizeof *v, compare_variables_by_mxd_vartype);
    dict_reorder_vars (default_dict, v, nv);
    free (v);
  }

  /* Set formats. */
  {
    static const struct fmt_spec fmt_tab[MXD_COUNT] =
      {
	{FMT_F, 4, 0},
        {FMT_A, 8, 0},
        {FMT_F, 4, 0},
	{FMT_A, 8, 0},
	{FMT_F, 10, 4},
      };
    
    int i;

    first_continuous = -1;
    for (i = 0; i < dict_get_var_cnt (default_dict); i++)
      {
	struct variable *v = dict_get_var (default_dict, i);
	int type = v->p.mxd.vartype;
	
	assert (type >= 0 && type < MXD_COUNT);
	v->print = v->write = fmt_tab[type];

	if (type == MXD_CONTINUOUS)
	  n_continuous++;
	if (first_continuous == -1 && type == MXD_CONTINUOUS)
	  first_continuous = i;
      }
  }

  if (n_continuous == 0)
    {
      msg (SE, _("No continuous variables specified."));
      goto lossage;
    }

#if DEBUGGING
  debug_print ();
#endif

  if (explicit_rowtype)
    read_matrices_with_rowtype ();
  else
    read_matrices_without_rowtype ();

  pool_destroy (container);

  return CMD_SUCCESS;

lossage:
  discard_variables ();
  free (factors);
  pool_destroy (container);
  return CMD_FAILURE;
}

/* Look up string S as a content-type name and return the
   corresponding enumerated value, or -1 if there is no match.  If
   COLLIDE is non-NULL then *COLLIDE returns a value (suitable for use
   as a bit-index) which can be used for determining whether a related
   statistic has already been used. */
static int
string_to_content_type (char *s, int *collide)
{
  static const struct
    {
      int value;
      int collide;
      const char *string;
    }
  *tp,
  tab[] = 
    {
      {N_VECTOR, 0, "N_VECTOR"},
      {N_VECTOR, 0, "N"},
      {N_SCALAR, 0, "N_SCALAR"},
      {N_MATRIX, 1, "N_MATRIX"},
      {MEAN, 2, "MEAN"},
      {STDDEV, 3, "STDDEV"},
      {STDDEV, 3, "SD"},
      {COUNT, 4, "COUNT"},
      {MSE, 5, "MSE"},
      {DFE, 6, "DFE"},
      {MAT, 7, "MAT"},
      {COV, 8, "COV"},
      {CORR, 9, "CORR"},
      {PROX, 10, "PROX"},
      {-1, -1, NULL},
    };

  for (tp = tab; tp->value != -1; tp++)
    if (!strcmp (s, tp->string))
      {
	if (collide)
	  *collide = tp->collide;
	
	return tp->value;
      }
  return -1;
}

/* Compare two variables using p.mxd.vartype and p.mxd.subtype
   fields. */
static int
compare_variables_by_mxd_vartype (const void *a_, const void *b_)
{
  struct variable *const *pa = a_;
  struct variable *const *pb = b_;
  const struct matrix_data_proc *a = &(*pa)->p.mxd;
  const struct matrix_data_proc *b = &(*pb)->p.mxd;

  if (a->vartype != b->vartype)
    return a->vartype > b->vartype ? 1 : -1;
  else
    return a->subtype < b->subtype ? -1 : a->subtype > b->subtype;
}

#if DEBUGGING
/* Print out the command as input. */
static void
debug_print (void)
{
  printf ("MATRIX DATA\n\t/VARIABLES=");
  
  {
    int i;
    
    for (i = 0; i < default_dict.nvar; i++)
      printf ("%s ", default_dict.var[i]->name);
  }
  printf ("\n");

  printf ("\t/FORMAT=");
  if (fmt == LIST)
    printf ("LIST");
  else if (fmt == FREE)
    printf ("FREE");
  else
    assert (0);
  if (section == LOWER)
    printf (" LOWER");
  else if (section == UPPER)
    printf (" UPPER");
  else if (section == FULL)
    printf (" FULL");
  else
    assert (0);
  if (diag == DIAGONAL)
    printf (" DIAGONAL\n");
  else if (diag == NODIAGONAL)
    printf (" NODIAGONAL\n");
  else
    assert (0);

  if (dict_get_split_cnt (default_dict) != 0)
    {
      int i;

      printf ("\t/SPLIT=");
      for (i = 0; i < dict_get_split_cnt (default_dict); i++)
	printf ("%s ", dict_get_split_vars (default_dict)[i]->name);
      if (single_split)
	printf ("\t/* single split");
      printf ("\n");
    }
  
  if (n_factors)
    {
      int i;

      printf ("\t/FACTORS=");
      for (i = 0; i < n_factors; i++)
	printf ("%s ", factors[i]->name);
      printf ("\n");
    }

  if (cells != -1)
    printf ("\t/CELLS=%d\n", cells);

  if (pop_n != -1)
    printf ("\t/N=%d\n", pop_n);

  if (n_contents)
    {
      int i;
      int space = 0;
      
      printf ("\t/CONTENTS=");
      for (i = 0; i < n_contents; i++)
	{
	  if (contents[i] == LPAREN)
	    {
	      if (space)
		printf (" ");
	      printf ("(");
	      space = 0;
	    }
	  else if (contents[i] == RPAREN)
	    {
	      printf (")");
	      space = 1;
	    }
	  else 
	    {

	      assert (contents[i] >= 0 && contents[i] <= PROX);
	      if (space)
		printf (" ");
	      printf ("%s", content_names[contents[i]]);
	      space = 1;
	    }
	}
      printf ("\n");
    }
}
#endif /* DEBUGGING */

/* Matrix tokenizer. */

/* Matrix token types. */
enum
  {
    MNULL,		/* No token. */
    MNUM,		/* Number. */
    MSTR,		/* String. */
    MSTOP		/* End of file. */
  };

/* Current matrix token. */
static int mtoken;

/* Token string if applicable; not null-terminated. */
static char *mtokstr;

/* Length of mtokstr in characters. */
static int mtoklen;

/* Token value if applicable. */
static double mtokval;

static int mget_token (void);

#if DEBUGGING
#define mget_token() mget_token_dump()

static int
mget_token_dump (void)
{
  int result = (mget_token) ();
  mdump_token ();
  return result;
}

static void
mdump_token (void)
{
  switch (mtoken)
    {
    case MNULL:
      printf (" <NULLTOK>");
      break;
    case MNUM:
      printf (" #%g", mtokval);
      break;
    case MSTR:
      printf (" #'%.*s'", mtoklen, mtokstr);
      break;
    case MSTOP:
      printf (" <STOP>");
      break;
    default:
      assert (0);
    }
  fflush (stdout);
}
#endif

/* Return the current position in the data file. */
static const char *
context (void)
{
  static char buf[32];
  int len;
  char *p = dfm_get_record (data_file, &len);
  
  if (!p || !len)
    strcpy (buf, "at end of line");
  else
    {
      char *cp = buf;
      int n_copy = min (10, len);
      cp = stpcpy (buf, "before `");
      while (n_copy && isspace ((unsigned char) *p))
	p++, n_copy++;
      while (n_copy && !isspace ((unsigned char) *p))
	*cp++ = *p++, n_copy--;
      *cp++ = '\'';
      *cp = 0;
    }
  
  return buf;
}

/* Is there at least one token left in the data file? */
static int
another_token (void)
{
  char *cp, *ep;
  int len;

  if (mtoken == MSTOP)
    return 0;
  
  for (;;)
    {
      cp = dfm_get_record (data_file, &len);
      if (!cp)
	return 0;

      ep = cp + len;
      while (isspace ((unsigned char) *cp) && cp < ep)
	cp++;

      if (cp < ep)
	break;

      dfm_fwd_record (data_file);
    }
  
  dfm_set_record (data_file, cp);

  return 1;
}

/* Parse a MATRIX DATA token from data_file into mtok*. */
static int
(mget_token) (void)
{
  char *cp, *ep;
  int len;
  int first_column;
    
  for (;;)
    {
      cp = dfm_get_record (data_file, &len);
      if (!cp)
	{
	  if (mtoken == MSTOP)
	    return 0;
	  mtoken = MSTOP;
	  return 1;
	}

      ep = cp + len;
      while (isspace ((unsigned char) *cp) && cp < ep)
	cp++;

      if (cp < ep)
	break;

      dfm_fwd_record (data_file);
    }
  
  dfm_set_record (data_file, cp);
  first_column = dfm_get_cur_col (data_file) + 1;

  /* Three types of fields: quoted with ', quoted with ", unquoted. */
  if (*cp == '\'' || *cp == '"')
    {
      int quote = *cp;

      mtoken = MSTR;
      mtokstr = ++cp;
      while (cp < ep && *cp != quote)
	cp++;
      mtoklen = cp - mtokstr;
      if (cp < ep)
	cp++;
      else
	msg (SW, _("Scope of string exceeds line."));
    }
  else
    {
      int is_num = isdigit ((unsigned char) *cp) || *cp == '.';

      mtokstr = cp++;
      while (cp < ep && !isspace ((unsigned char) *cp) && *cp != ','
	     && *cp != '-' && *cp != '+')
	{
	  if (isdigit ((unsigned char) *cp))
	    is_num = 1;
	  
	  if ((tolower ((unsigned char) *cp) == 'd'
	       || tolower ((unsigned char) *cp) == 'e')
	      && (cp[1] == '+' || cp[1] == '-'))
	    cp += 2;
	  else
	    cp++;
	}
      
      mtoklen = cp - mtokstr;
      assert (mtoklen);

      if (is_num)
	{
	  struct data_in di;

	  di.s = mtokstr;
	  di.e = mtokstr + mtoklen;
	  di.v = (union value *) &mtokval;
	  di.f1 = first_column;
	  di.format.type = FMT_F;
	  di.format.w = mtoklen;
	  di.format.d = 0;

	  if (!data_in (&di))
	    return 0;
	}
      else
	mtoken = MSTR;
    }

  dfm_set_record (data_file, cp);
    
  return 1;
}

/* Forcibly skip the end of a line for content type CONTENT in
   data_file. */
static int
force_eol (const char *content)
{
  char *cp;
  int len;
  
  if (fmt == FREE)
    return 1;

  cp = dfm_get_record (data_file, &len);
  if (!cp)
    return 0;
  while (len && isspace (*cp))
    cp++, len--;
  
  if (len)
    {
      msg (SE, _("End of line expected %s while reading %s."),
	   context (), content);
      return 0;
    }
  
  dfm_fwd_record (data_file);
  
  return 1;
}

/* Back end, omitting ROWTYPE_. */

/* MATRIX DATA data. */
static double ***nr_data;

/* Factor values. */
static double *nr_factor_values;

/* Largest-numbered cell that we have read in thus far, plus one. */
static int max_cell_index;

/* SPLIT FILE variable values. */
static double *split_values;

static int nr_read_splits (int compare);
static int nr_read_factors (int cell);
static void nr_output_data (write_case_func *, write_case_data);
static void matrix_data_read_without_rowtype (write_case_func *,
                                              write_case_data);

/* Read from the data file and write it to the active file. */
static void
read_matrices_without_rowtype (void)
{
  if (cells == -1)
    cells = 1;
  
  mtoken = MNULL;
  split_values = xmalloc (sizeof *split_values
                          * dict_get_split_cnt (default_dict));
  nr_factor_values = xmalloc (sizeof *nr_factor_values * n_factors * cells);
  max_cell_index = 0;

  matrix_data_source.read = matrix_data_read_without_rowtype;
  vfm_source = &matrix_data_source;
  
  procedure (NULL, NULL, NULL, NULL);

  free (split_values);
  free (nr_factor_values);

  fh_close_handle (data_file);
}

/* Mirror data across the diagonal of matrix CP which contains
   CONTENT type data. */
static void
fill_matrix (int content, double *cp)
{
  int type = content_type[content];

  if (type == 1 && section != FULL)
    {
      if (diag == NODIAGONAL)
	{
	  const double fill = content == CORR ? 1.0 : SYSMIS;
	  int i;

	  for (i = 0; i < n_continuous; i++)
	    cp[i * (1 + n_continuous)] = fill;
	}
      
      {
	int c, r;
	
	if (section == LOWER)
	  {
	    int n_lines = n_continuous;
	    if (section != FULL && diag == NODIAGONAL)
	      n_lines--;
	    
	    for (r = 1; r < n_lines; r++)
	      for (c = 0; c < r; c++)
		cp[r + c * n_continuous] = cp[c + r * n_continuous];
	  }
	else 
	  {
	    assert (section == UPPER);
	    for (r = 1; r < n_continuous; r++)
	      for (c = 0; c < r; c++)
		cp[c + r * n_continuous] = cp[r + c * n_continuous];
	  }
      }
    }
  else if (type == 2)
    {
      int c;

      for (c = 1; c < n_continuous; c++)
	cp[c] = cp[0];
    }
}

/* Read data lines for content type CONTENT from the data file.  If
   PER_FACTOR is nonzero, then factor information is read from the
   data file.  Data is for cell number CELL. */
static int
nr_read_data_lines (int per_factor, int cell, int content, int compare)
{
  /* Content type. */
  const int type = content_type[content];
  
  /* Number of lines that must be parsed from the data file for this
     content type. */
  int n_lines;
  
  /* Current position in vector or matrix. */
  double *cp;

  /* Counter. */
  int i;

  if (type != 1)
    n_lines = 1;
  else
    {
      n_lines = n_continuous;
      if (section != FULL && diag == NODIAGONAL)
	n_lines--;
    }

  cp = nr_data[content][cell];
  if (type == 1 && section == LOWER && diag == NODIAGONAL)
    cp += n_continuous;

  for (i = 0; i < n_lines; i++)
    {
      int n_cols;
      
      if (!nr_read_splits (1))
	return 0;
      if (per_factor && !nr_read_factors (cell))
	return 0;
      compare = 1;

      switch (type)
	{
	case 0:
	  n_cols = n_continuous;
	  break;
	case 1:
	  switch (section)
	    {
	    case LOWER:
	      n_cols = i + 1;
	      break;
	    case UPPER:
	      cp += i;
	      n_cols = n_continuous - i;
	      if (diag == NODIAGONAL)
		{
		  n_cols--;
		  cp++;
		}
	      break;
	    case FULL:
	      n_cols = n_continuous;
	      break;
	    default:
	      assert (0);
	    }
	  break;
	case 2:
	  n_cols = 1;
	  break;
	default:
	  assert (0);
	}

      {
	int j;
	
	for (j = 0; j < n_cols; j++)
	  {
	    if (!mget_token ())
	      return 0;
	    if (mtoken != MNUM)
	      {
		msg (SE, _("expecting value for %s %s"),
		     dict_get_var (default_dict, j)->name, context ());
		return 0;
	      }

	    *cp++ = mtokval;
	  }
	if (!force_eol (content_names[content]))
	  return 0;
	debug_printf (("\n"));
      }

      if (section == LOWER)
	cp += n_continuous - n_cols;
    }

  fill_matrix (content, nr_data[content][cell]);

  return 1;
}

/* When ROWTYPE_ does not appear in the data, reads the matrices and
   writes them to the output file.  Returns success. */
static void
matrix_data_read_without_rowtype (write_case_func *write_case,
                                  write_case_data wc_data)
{
  {
    int *cp;

    nr_data = pool_alloc (container, (PROX + 1) * sizeof *nr_data);
    
    {
      int i;

      for (i = 0; i <= PROX; i++)
	nr_data[i] = NULL;
    }
    
    for (cp = contents; *cp != EOC; cp++)
      if (*cp != LPAREN && *cp != RPAREN)
	{
	  int per_factor = is_per_factor[*cp];
	  int n_entries;
	  
	  n_entries = n_continuous;
	  if (content_type[*cp] == 1)
	    n_entries *= n_continuous;
	  
	  {
	    int n_vectors = per_factor ? cells : 1;
	    int i;
	    
	    nr_data[*cp] = pool_alloc (container,
				       n_vectors * sizeof **nr_data);
	    
	    for (i = 0; i < n_vectors; i++)
	      nr_data[*cp][i] = pool_alloc (container,
					    n_entries * sizeof ***nr_data);
	  }
	}
  }
  
  for (;;)
    {
      int *bp, *ep, *np;
      
      if (!nr_read_splits (0))
	return;
      
      for (bp = contents; *bp != EOC; bp = np)
	{
	  int per_factor;

	  /* Trap the CONTENTS that we should parse in this pass
	     between bp and ep.  Set np to the starting bp for next
	     iteration. */
	  if (*bp == LPAREN)
	    {
	      ep = ++bp;
	      while (*ep != RPAREN)
		ep++;
	      np = &ep[1];
	      per_factor = 1;
	    }
	  else
	    {
	      ep = &bp[1];
	      while (*ep != EOC && *ep != LPAREN)
		ep++;
	      np = ep;
	      per_factor = 0;
	    }
	  
	  {
	    int i;
	      
	    for (i = 0; i < (per_factor ? cells : 1); i++)
	      {
		int *cp;

		for (cp = bp; cp < ep; cp++) 
		  if (!nr_read_data_lines (per_factor, i, *cp, cp != bp))
		    return;
	      }
	  }
	}

      nr_output_data (write_case, wc_data);

      if (dict_get_split_cnt (default_dict) == 0 || !another_token ())
	return;
    }
}

/* Read the split file variables.  If COMPARE is 1, compares the
   values read to the last values read and returns 1 if they're equal,
   0 otherwise. */
static int
nr_read_splits (int compare)
{
  static int just_read = 0;
  size_t split_cnt;
  size_t i;

  if (compare && just_read)
    {
      just_read = 0;
      return 1;
    }
  
  if (dict_get_split_vars (default_dict) == NULL)
    return 1;

  if (single_split)
    {
      if (!compare)
	split_values[0]
          = ++dict_get_split_vars (default_dict)[0]->p.mxd.subtype;
      return 1;
    }

  if (!compare)
    just_read = 1;

  split_cnt = dict_get_split_cnt (default_dict);
  for (i = 0; i < split_cnt; i++) 
    {
      if (!mget_token ())
        return 0;
      if (mtoken != MNUM)
        {
          msg (SE, _("Syntax error expecting SPLIT FILE value %s."),
               context ());
          return 0;
        }

      if (!compare)
        split_values[i] = mtokval;
      else if (split_values[i] != mtokval)
        {
          msg (SE, _("Expecting value %g for %s."),
               split_values[i], dict_get_split_vars (default_dict)[i]->name);
          return 0;
        }
    }

  return 1;
}

/* Read the factors for cell CELL.  If COMPARE is 1, compares the
   values read to the last values read and returns 1 if they're equal,
   0 otherwise. */
static int
nr_read_factors (int cell)
{
  int compare;
  
  if (n_factors == 0)
    return 1;

  assert (max_cell_index >= cell);
  if (cell != max_cell_index)
    compare = 1;
  else
    {
      compare = 0;
      max_cell_index++;
    }
      
  {
    int i;
    
    for (i = 0; i < n_factors; i++)
      {
	if (!mget_token ())
	  return 0;
	if (mtoken != MNUM)
	  {
	    msg (SE, _("Syntax error expecting factor value %s."),
		 context ());
	    return 0;
	  }
	
	if (!compare)
	  nr_factor_values[i + n_factors * cell] = mtokval;
	else if (nr_factor_values[i + n_factors * cell] != mtokval)
	  {
	    msg (SE, _("Syntax error expecting value %g for %s %s."),
		 nr_factor_values[i + n_factors * cell],
		 factors[i]->name, context ());
	    return 0;
	  }
      }
  }

  return 1;
}

/* Write the contents of a cell having content type CONTENT and data
   CP to the active file. */
static void
dump_cell_content (int content, double *cp,
                   write_case_func *write_case, write_case_data wc_data)
{
  int type = content_type[content];

  {
    st_bare_pad_copy (temp_case->data[rowtype_->fv].s,
		      content_names[content], 8);
    
    if (type != 1)
      memset (&temp_case->data[varname_->fv].s, ' ', 8);
  }

  {
    int n_lines = (type == 1) ? n_continuous : 1;
    int i;
		
    for (i = 0; i < n_lines; i++)
      {
	int j;

	for (j = 0; j < n_continuous; j++)
	  {
            int fv = dict_get_var (default_dict, first_continuous + j)->fv;
	    temp_case->data[fv].f = *cp;
	    cp++;
	  }
	if (type == 1)
	  st_bare_pad_copy (temp_case->data[varname_->fv].s,
                            dict_get_var (default_dict,
                                          first_continuous + i)->name,
			    8);
	write_case (wc_data);
      }
  }
}

/* Finally dump out everything from nr_data[] to the output file. */
static void
nr_output_data (write_case_func *write_case, write_case_data wc_data)
{
  {
    struct variable *const *split;
    size_t split_cnt;
    size_t i;

    split_cnt = dict_get_split_cnt (default_dict);
    for (i = 0; i < split_cnt; i++)
      temp_case->data[split[i]->fv].f = split_values[i];
  }

  if (n_factors)
    {
      int cell;

      for (cell = 0; cell < cells; cell++)
	{
	  {
	    int factor;

	    for (factor = 0; factor < n_factors; factor++)
	      {
		temp_case->data[factors[factor]->fv].f
		  = nr_factor_values[factor + cell * n_factors];
		debug_printf (("f:%s ", factors[factor]->name));
	      }
	  }
	  
	  {
	    int content;
	    
	    for (content = 0; content <= PROX; content++)
	      if (is_per_factor[content])
		{
		  assert (nr_data[content] != NULL
			  && nr_data[content][cell] != NULL);

		  dump_cell_content (content, nr_data[content][cell],
                                     write_case, wc_data);
		}
	  }
	}
    }

  {
    int content;
    
    {
      int factor;

      for (factor = 0; factor < n_factors; factor++)
	temp_case->data[factors[factor]->fv].f = SYSMIS;
    }
    
    for (content = 0; content <= PROX; content++)
      if (!is_per_factor[content] && nr_data[content] != NULL)
	dump_cell_content (content, nr_data[content][0],
                           write_case, wc_data);
  }
}

/* Back end, with ROWTYPE_. */

/* Type of current row. */
static int wr_content;

/* All the data for one set of factor values. */
struct factor_data
  {
    double *factors;
    int n_rows[PROX + 1];
    double *data[PROX + 1];
    struct factor_data *next;
  };

/* All the data, period. */
struct factor_data *wr_data;

/* Current factor. */
struct factor_data *wr_current;

static int wr_read_splits (write_case_func *, write_case_data);
static int wr_output_data (write_case_func *, write_case_data);
static int wr_read_rowtype (void);
static int wr_read_factors (void);
static int wr_read_indeps (void);
static void matrix_data_read_with_rowtype (write_case_func *,
                                           write_case_data);

/* When ROWTYPE_ appears in the data, reads the matrices and writes
   them to the output file. */
static void
read_matrices_with_rowtype (void)
{
  mtoken = MNULL;
  wr_data = wr_current = NULL;
  split_values = NULL;
  cells = 0;

  matrix_data_source.read = matrix_data_read_with_rowtype;
  vfm_source = &matrix_data_source;
  
  procedure (NULL, NULL, NULL, NULL);

  free (split_values);
  fh_close_handle (data_file);
}

/* Read from the data file and write it to the active file. */
static void
matrix_data_read_with_rowtype (write_case_func *write_case,
                               write_case_data wc_data)
{
  do
    {
      if (!wr_read_splits (write_case, wc_data))
	return;

      if (!wr_read_factors ())
	return;

      if (!wr_read_indeps ())
	return;
    }
  while (another_token ());

  wr_output_data (write_case, wc_data);
}

/* Read the split file variables.  If they differ from the previous
   set of split variables then output the data.  Returns success. */
static int 
wr_read_splits (write_case_func *write_case, write_case_data wc_data)
{
  int compare;
  size_t split_cnt;

  split_cnt = dict_get_split_cnt (default_dict);
  if (split_cnt == 0)
    return 1;

  if (split_values)
    compare = 1;
  else
    {
      compare = 0;
      split_values = xmalloc (split_cnt * sizeof *split_values);
    }
  
  {
    int different = 0;
    size_t split_cnt;
    int i;

    for (i = 0; i < split_cnt; i++)
      {
	if (!mget_token ())
	  return 0;
	if (mtoken != MNUM)
	  {
	    msg (SE, _("Syntax error %s expecting SPLIT FILE value."),
		 context ());
	    return 0;
	  }

	if (compare && split_values[i] != mtokval && !different)
	  {
	    if (!wr_output_data (write_case, wc_data))
	      return 0;
	    different = 1;
	    cells = 0;
	  }
	split_values[i] = mtokval;
      }
  }

  return 1;
}

/* Compares doubles A and B, treating SYSMIS as greatest. */
static int
compare_doubles (const void *a_, const void *b_, void *aux UNUSED)
{
  const double *a = a_;
  const double *b = b_;

  if (*a == *b)
    return 0;
  else if (*a == SYSMIS)
    return 1;
  else if (*b == SYSMIS)
    return -1;
  else if (*a > *b)
    return 1;
  else
    return -1;
}

/* Return strcmp()-type comparison of the n_factors factors at _A and
   _B.  Sort missing values toward the end. */
static int
compare_factors (const void *a_, const void *b_)
{
  struct factor_data *const *pa = a_;
  struct factor_data *const *pb = b_;
  const double *a = (*pa)->factors;
  const double *b = (*pb)->factors;

  return lexicographical_compare (a, n_factors,
                                  b, n_factors,
                                  sizeof *a,
                                  compare_doubles, NULL);
}

/* Write out the data for the current split file to the active
   file. */
static int 
wr_output_data (write_case_func *write_case, write_case_data wc_data)
{
  {
    struct variable *const *split;
    size_t split_cnt;
    size_t i;

    split_cnt = dict_get_split_cnt (default_dict);
    for (i = 0; i < split_cnt; i++)
      temp_case->data[split[i]->fv].f = split_values[i];
  }

  /* Sort the wr_data list. */
  {
    struct factor_data **factors;
    struct factor_data *iter;
    int i;

    factors = xmalloc (sizeof *factors * cells);

    for (i = 0, iter = wr_data; iter; iter = iter->next, i++)
      factors[i] = iter;

    qsort (factors, cells, sizeof *factors, compare_factors);

    wr_data = factors[0];
    for (i = 0; i < cells - 1; i++)
      factors[i]->next = factors[i + 1];
    factors[cells - 1]->next = NULL;

    free (factors);
  }

  /* Write out records for every set of factor values. */
  {
    struct factor_data *iter;
    
    for (iter = wr_data; iter; iter = iter->next)
      {
	{
	  int factor;

	  for (factor = 0; factor < n_factors; factor++)
	    {
	      temp_case->data[factors[factor]->fv].f
		= iter->factors[factor];
	      debug_printf (("f:%s ", factors[factor]->name));
	    }
	}
	
	{
	  int content;

	  for (content = 0; content <= PROX; content++)
	    {
	      if (!iter->n_rows[content])
		continue;
	      
	      {
		int type = content_type[content];
		int n_lines = (type == 1
			       ? (n_continuous
				  - (section != FULL && diag == NODIAGONAL))
			       : 1);
		
		if (n_lines != iter->n_rows[content])
		  {
		    msg (SE, _("Expected %d lines of data for %s content; "
			       "actually saw %d lines.  No data will be "
			       "output for this content."),
			 n_lines, content_names[content],
			 iter->n_rows[content]);
		    continue;
		  }
	      }

	      fill_matrix (content, iter->data[content]);

	      dump_cell_content (content, iter->data[content],
                                 write_case, wc_data);
	    }
	}
      }
  }
  
  pool_destroy (container);
  container = pool_create ();
  
  wr_data = wr_current = NULL;
  
  return 1;
}

/* Read ROWTYPE_ from the data file.  Return success. */
static int 
wr_read_rowtype (void)
{
  if (wr_content != -1)
    {
      msg (SE, _("Multiply specified ROWTYPE_ %s."), context ());
      return 0;
    }
  if (mtoken != MSTR)
    {
      msg (SE, _("Syntax error %s expecting ROWTYPE_ string."), context ());
      return 0;
    }
  
  {
    char s[16];
    char *cp;
    
    memcpy (s, mtokstr, min (15, mtoklen));
    s[min (15, mtoklen)] = 0;

    for (cp = s; *cp; cp++)
      *cp = toupper ((unsigned char) *cp);

    wr_content = string_to_content_type (s, NULL);
  }

  if (wr_content == -1)
    {
      msg (SE, _("Syntax error %s."), context ());
      return 0;
    }

  return 1;
}

/* Read the factors for the current row.  Select a set of factors and
   point wr_current to it. */
static int 
wr_read_factors (void)
{
  double *factor_values = local_alloc (sizeof *factor_values * n_factors);

  wr_content = -1;
  {
    int i;
  
    for (i = 0; i < n_factors; i++)
      {
	if (!mget_token ())
	  goto lossage;
	if (mtoken == MSTR)
	  {
	    if (!wr_read_rowtype ())
	      goto lossage;
	    if (!mget_token ())
	      goto lossage;
	  }
	if (mtoken != MNUM)
	  {
	    msg (SE, _("Syntax error expecting factor value %s."),
		 context ());
	    goto lossage;
	  }
	
	factor_values[i] = mtokval;
      }
  }
  if (wr_content == -1)
    {
      if (!mget_token ())
	goto lossage;
      if (!wr_read_rowtype ())
	goto lossage;
    }
  
  /* Try the most recent factor first as a simple caching
     mechanism. */
  if (wr_current)
    {
      int i;
      
      for (i = 0; i < n_factors; i++)
	if (factor_values[i] != wr_current->factors[i])
	  goto cache_miss;
      goto winnage;
    }

  /* Linear search through the list. */
cache_miss:
  {
    struct factor_data *iter;

    for (iter = wr_data; iter; iter = iter->next)
      {
	int i;

	for (i = 0; i < n_factors; i++)
	  if (factor_values[i] != iter->factors[i])
	    goto next_item;
	
	wr_current = iter;
	goto winnage;
	
      next_item: ;
      }
  }

  /* Not found.  Make a new item. */
  {
    struct factor_data *new = pool_alloc (container, sizeof *new);

    new->factors = pool_alloc (container, sizeof *new->factors * n_factors);
    
    {
      int i;

      for (i = 0; i < n_factors; i++)
	new->factors[i] = factor_values[i];
    }
    
    {
      int i;

      for (i = 0; i <= PROX; i++)
	{
	  new->n_rows[i] = 0;
	  new->data[i] = NULL;
	}
    }

    new->next = wr_data;
    wr_data = wr_current = new;
    cells++;
  }

winnage:
  local_free (factor_values);
  return 1;

lossage:
  local_free (factor_values);
  return 0;
}

/* Read the independent variables into wr_current. */
static int 
wr_read_indeps (void)
{
  struct factor_data *c = wr_current;
  const int type = content_type[wr_content];
  const int n_rows = c->n_rows[wr_content];
  double *cp;
  int n_cols;

  /* Allocate room for data if necessary. */
  if (c->data[wr_content] == NULL)
    {
      int n_items = n_continuous;
      if (type == 1)
	n_items *= n_continuous;
      
      c->data[wr_content] = pool_alloc (container,
					sizeof **c->data * n_items);
    }

  cp = &c->data[wr_content][n_rows * n_continuous];

  /* Figure out how much to read from this line. */
  switch (type)
    {
    case 0:
    case 2:
      if (n_rows > 0)
	{
	  msg (SE, _("Duplicate specification for %s."),
	       content_names[wr_content]);
	  return 0;
	}
      if (type == 0)
	n_cols = n_continuous;
      else
	n_cols = 1;
      break;
    case 1:
      if (n_rows >= n_continuous - (section != FULL && diag == NODIAGONAL))
	{
	  msg (SE, _("Too many rows of matrix data for %s."),
	       content_names[wr_content]);
	  return 0;
	}
      
      switch (section)
	{
	case LOWER:
	  n_cols = n_rows + 1;
	  if (diag == NODIAGONAL)
	    cp += n_continuous;
	  break;
	case UPPER:
	  cp += n_rows;
	  n_cols = n_continuous - n_rows;
	  if (diag == NODIAGONAL)
	    {
	      n_cols--;
	      cp++;
	    }
	  break;
	case FULL:
	  n_cols = n_continuous;
	  break;
	default:
	  assert (0);
	}
      break;
    default:
      assert (0);
    }
  c->n_rows[wr_content]++;

  debug_printf ((" (c=%p,r=%d,n=%d)", c, n_rows + 1, n_cols));

  /* Read N_COLS items at CP. */
  {
    int j;
	
    for (j = 0; j < n_cols; j++)
      {
	if (!mget_token ())
	  return 0;
	if (mtoken != MNUM)
	  {
	    msg (SE, _("Syntax error expecting value for %s %s."),
                 dict_get_var (default_dict, first_continuous + j)->name,
                 context ());
	    return 0;
	  }

	*cp++ = mtokval;
      }
    if (!force_eol (content_names[wr_content]))
      return 0;
    debug_printf (("\n"));
  }

  return 1;
}

/* Matrix source. */

struct case_stream matrix_data_source = 
  {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "MATRIX DATA",
  };

