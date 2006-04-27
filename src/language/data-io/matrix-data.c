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

#include <stdlib.h>
#include <ctype.h>
#include <float.h>

#include <data/case-source.h>
#include <data/case.h>
#include <data/data-in.h>
#include <data/dictionary.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/data-reader.h>
#include <language/data-io/file-handle.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/array.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>
#include <procedure.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* FIXME: /N subcommand not implemented.  It should be pretty simple,
   too. */

/* Different types of variables for MATRIX DATA procedure.  Order is
   important: these are used for sort keys. */
enum
  {
    MXD_SPLIT,			/* SPLIT FILE variables. */
    MXD_ROWTYPE,		/* ROWTYPE_. */
    MXD_FACTOR,			/* Factor variables. */
    MXD_VARNAME,		/* VARNAME_. */
    MXD_CONTINUOUS,		/* Continuous variables. */

    MXD_COUNT
  };

/* Format type enums. */
enum format_type
  {
    LIST,
    FREE
  };

/* Matrix section enums. */
enum matrix_section
  {
    LOWER,
    UPPER,
    FULL
  };

/* Diagonal inclusion enums. */
enum include_diagonal
  {
    DIAGONAL,
    NODIAGONAL
  };

/* CONTENTS types. */
enum content_type
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
static const int content_type[PROX + 1] = 
  {
    0, 2, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1,
  };

/* Name of each content type. */
static const char *content_names[PROX + 1] =
  {
    "N", "N", "N_MATRIX", "MEAN", "STDDEV", "COUNT", "MSE",
    "DFE", "MAT", "COV", "CORR", "PROX",
  };

/* A MATRIX DATA input program. */
struct matrix_data_pgm 
  {
    struct pool *container;     /* Arena used for all allocations. */
    struct dfm_reader *reader;  /* Data file to read. */

    /* Format. */
    enum format_type fmt;	/* LIST or FREE. */
    enum matrix_section section;/* LOWER or UPPER or FULL. */
    enum include_diagonal diag; /* DIAGONAL or NODIAGONAL. */

    int explicit_rowtype;       /* ROWTYPE_ specified explicitly in data? */
    struct variable *rowtype_, *varname_; /* ROWTYPE_, VARNAME_ variables. */
    
    struct variable *single_split; /* Single SPLIT FILE variable. */

    /* Factor variables.  */
    size_t n_factors;           /* Number of factor variables. */
    struct variable **factors;  /* Factor variables. */
    int is_per_factor[PROX + 1]; /* Is there per-factor data? */

    int cells;                  /* Number of cells, or -1 if none. */

    int pop_n;                  /* Population N specified by user. */

    /* CONTENTS subcommand. */
    int contents[EOC * 3 + 1];  /* Contents. */
    int n_contents;             /* Number of entries. */

    /* Continuous variables. */
    int n_continuous;           /* Number of continuous variables. */
    int first_continuous;       /* Index into default_dict.var of
                                   first continuous variable. */
  };

/* Auxiliary data attached to MATRIX DATA variables. */
struct mxd_var 
  {
    int var_type;		/* Variable type. */
    int sub_type;		/* Subtype. */
  };

static const struct case_source_class matrix_data_with_rowtype_source_class;
static const struct case_source_class matrix_data_without_rowtype_source_class;

static int compare_variables_by_mxd_var_type (const void *pa,
					     const void *pb);
static bool read_matrices_without_rowtype (struct matrix_data_pgm *);
static bool read_matrices_with_rowtype (struct matrix_data_pgm *);
static int string_to_content_type (char *, int *);
static void attach_mxd_aux (struct variable *, int var_type, int sub_type);

int
cmd_matrix_data (void)
{
  struct pool *pool;
  struct matrix_data_pgm *mx;
  struct file_handle *fh = fh_inline_file ();
  bool ok;
    
  unsigned seen = 0;
  
  discard_variables ();

  pool = pool_create ();
  mx = pool_alloc (pool, sizeof *mx);
  mx->container = pool;
  mx->reader = NULL;
  mx->fmt = LIST;
  mx->section = LOWER;
  mx->diag = DIAGONAL;
  mx->explicit_rowtype = 0;
  mx->rowtype_ = NULL;
  mx->varname_ = NULL;
  mx->single_split = NULL;
  mx->n_factors = 0;
  mx->factors = NULL;
  memset (mx->is_per_factor, 0, sizeof mx->is_per_factor);
  mx->cells = -1;
  mx->pop_n = -1;
  mx->n_contents = 0;
  mx->n_continuous = 0;
  mx->first_continuous = 0;
  while (token != '.')
    {
      lex_match ('/');

      if (lex_match_id ("VARIABLES"))
	{
	  char **v;
	  size_t nv;

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
	    size_t i;

	    for (i = 0; i < nv; i++)
	      if (!strcasecmp (v[i], "VARNAME_"))
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
	    size_t i;

	    for (i = 0; i < nv; i++)
	      {
		struct variable *new_var;
		
		if (strcasecmp (v[i], "ROWTYPE_"))
		  {
		    new_var = dict_create_var_assert (default_dict, v[i], 0);
                    attach_mxd_aux (new_var, MXD_CONTINUOUS, i);
                  }
		else
		  mx->explicit_rowtype = 1;
		free (v[i]);
	      }
	    free (v);
	  }
	  
          mx->rowtype_ = dict_create_var_assert (default_dict,
                                                 "ROWTYPE_", 8);
          attach_mxd_aux (mx->rowtype_, MXD_ROWTYPE, 0);
	}
      else if (lex_match_id ("FILE"))
	{
	  lex_match ('=');
	  fh = fh_parse (FH_REF_FILE | FH_REF_INLINE);
	  if (fh == NULL)
	    goto lossage;
	}
      else if (lex_match_id ("FORMAT"))
	{
	  lex_match ('=');

	  while (token == T_ID)
	    {
	      if (lex_match_id ("LIST"))
		mx->fmt = LIST;
	      else if (lex_match_id ("FREE"))
		mx->fmt = FREE;
	      else if (lex_match_id ("LOWER"))
		mx->section = LOWER;
	      else if (lex_match_id ("UPPER"))
		mx->section = UPPER;
	      else if (lex_match_id ("FULL"))
		mx->section = FULL;
	      else if (lex_match_id ("DIAGONAL"))
		mx->diag = DIAGONAL;
	      else if (lex_match_id ("NODIAGONAL"))
		mx->diag = NODIAGONAL;
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
	      if (!strcasecmp (tokid, "ROWTYPE_")
                  || !strcasecmp (tokid, "VARNAME_"))
		{
		  msg (SE, _("Split variable may not be named ROWTYPE_ "
			     "or VARNAME_."));
		  goto lossage;
		}

	      mx->single_split = dict_create_var_assert (default_dict,
                                                         tokid, 0);
              attach_mxd_aux (mx->single_split, MXD_CONTINUOUS, 0);
	      lex_get ();

              dict_set_split_vars (default_dict, &mx->single_split, 1);
	    }
	  else
	    {
	      struct variable **split;
	      size_t n;

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
                struct mxd_var *mv = split[i]->aux;
                assert (mv != NULL);
		if (mv->var_type != MXD_CONTINUOUS)
		  {
		    msg (SE, _("Split variable %s is already another type."),
			 tokid);
		    goto lossage;
		  }
                var_clear_aux (split[i]);
                attach_mxd_aux (split[i], MXD_SPLIT, i);
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

	  if (!parse_variables (default_dict, &mx->factors, &mx->n_factors,
                                PV_NONE))
	    goto lossage;
	  
	  {
	    size_t i;
	    
	    for (i = 0; i < mx->n_factors; i++)
	      {
                struct variable *v = mx->factors[i];
                struct mxd_var *mv = v->aux;
                assert (mv != NULL);
		if (mv->var_type != MXD_CONTINUOUS)
		  {
		    msg (SE, _("Factor variable %s is already another type."),
			 tokid);
		    goto lossage;
		  }
                var_clear_aux (v);
                attach_mxd_aux (v, MXD_FACTOR, i);
	      }
	  }
	}
      else if (lex_match_id ("CELLS"))
	{
	  lex_match ('=');
	  
	  if (mx->cells != -1)
	    {
	      msg (SE, _("CELLS subcommand multiply specified."));
	      goto lossage;
	    }

	  if (!lex_is_integer () || lex_integer () < 1)
	    {
	      lex_error (_("expecting positive integer"));
	      goto lossage;
	    }

	  mx->cells = lex_integer ();
	  lex_get ();
	}
      else if (lex_match_id ("N"))
	{
	  lex_match ('=');

	  if (mx->pop_n != -1)
	    {
	      msg (SE, _("N subcommand multiply specified."));
	      goto lossage;
	    }

	  if (!lex_is_integer () || lex_integer () < 1)
	    {
	      lex_error (_("expecting positive integer"));
	      goto lossage;
	    }

	  mx->pop_n = lex_integer ();
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
	      mx->is_per_factor[i] = 0;
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
		  if (mx->contents[mx->n_contents - 1] == LPAREN)
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
		  mx->is_per_factor[item] = inside_parens;
		}
	      mx->contents[mx->n_contents++] = item;

	      if (token == '/' || token == '.')
		break;
	    }

	  if (inside_parens)
	    {
	      msg (SE, _("Missing right parenthesis."));
	      goto lossage;
	    }
	  mx->contents[mx->n_contents] = EOC;
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
  
  if (!mx->n_contents && !mx->explicit_rowtype)
    {
      msg (SW, _("CONTENTS subcommand not specified: assuming file "
		 "contains only CORR matrix."));

      mx->contents[0] = CORR;
      mx->contents[1] = EOC;
      mx->n_contents = 0;
    }

  if (mx->n_factors && !mx->explicit_rowtype && mx->cells == -1)
    {
      msg (SE, _("Missing CELLS subcommand.  CELLS is required "
		 "when ROWTYPE_ is not given in the data and "
		 "factors are present."));
      goto lossage;
    }

  if (mx->explicit_rowtype && mx->single_split)
    {
      msg (SE, _("Split file values must be present in the data when "
		 "ROWTYPE_ is present."));
      goto lossage;
    }
      
  /* Create VARNAME_. */
  mx->varname_ = dict_create_var_assert (default_dict, "VARNAME_", 8);
  attach_mxd_aux (mx->varname_, MXD_VARNAME, 0);
  
  /* Sort the dictionary variables into the desired order for the
     system file output. */
  {
    struct variable **v;
    size_t nv;

    dict_get_vars (default_dict, &v, &nv, 0);
    qsort (v, nv, sizeof *v, compare_variables_by_mxd_var_type);
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

    mx->first_continuous = -1;
    for (i = 0; i < dict_get_var_cnt (default_dict); i++)
      {
	struct variable *v = dict_get_var (default_dict, i);
        struct mxd_var *mv = v->aux;
	int type = mv->var_type;
	
	assert (type >= 0 && type < MXD_COUNT);
	v->print = v->write = fmt_tab[type];

	if (type == MXD_CONTINUOUS)
	  mx->n_continuous++;
	if (mx->first_continuous == -1 && type == MXD_CONTINUOUS)
	  mx->first_continuous = i;
      }
  }

  if (mx->n_continuous == 0)
    {
      msg (SE, _("No continuous variables specified."));
      goto lossage;
    }

  mx->reader = dfm_open_reader (fh);
  if (mx->reader == NULL)
    goto lossage;

  if (mx->explicit_rowtype)
    ok = read_matrices_with_rowtype (mx);
  else
    ok = read_matrices_without_rowtype (mx);

  dfm_close_reader (mx->reader);

  pool_destroy (mx->container);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

lossage:
  discard_variables ();
  free (mx->factors);
  pool_destroy (mx->container);
  return CMD_CASCADING_FAILURE;
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
    if (!strcasecmp (s, tp->string))
      {
	if (collide)
	  *collide = tp->collide;
	
	return tp->value;
      }
  return -1;
}

/* Compare two variables using p.mxd.var_type and p.mxd.sub_type
   fields. */
static int
compare_variables_by_mxd_var_type (const void *a_, const void *b_)
{
  struct variable *const *pa = a_;
  struct variable *const *pb = b_;
  const struct mxd_var *a = (*pa)->aux;
  const struct mxd_var *b = (*pb)->aux;
  
  if (a->var_type != b->var_type)
    return a->var_type > b->var_type ? 1 : -1;
  else
    return a->sub_type < b->sub_type ? -1 : a->sub_type > b->sub_type;
}

/* Attaches a struct mxd_var with the specific member values to
   V. */
static void
attach_mxd_aux (struct variable *v, int var_type, int sub_type) 
{
  struct mxd_var *mv;
  
  assert (v->aux == NULL);
  mv = xmalloc (sizeof *mv);
  mv->var_type = var_type;
  mv->sub_type = sub_type;
  var_attach_aux (v, mv, var_dtor_free);
}

/* Matrix tokenizer. */

/* Matrix token types. */
enum matrix_token_type
  {
    MNUM,		/* Number. */
    MSTR		/* String. */
  };

/* A MATRIX DATA parsing token. */
struct matrix_token
  {
    enum matrix_token_type type; 
    double number;       /* MNUM: token value. */
    char *string;        /* MSTR: token string; not null-terminated. */
    int length;          /* MSTR: tokstr length. */
  };

static int mget_token (struct matrix_token *, struct dfm_reader *);

#if DEBUGGING
#define mget_token(TOKEN, READER) mget_token_dump(TOKEN, READER)

static void
mdump_token (const struct matrix_token *token)
{
  switch (token->type)
    {
    case MNUM:
      printf (" #%g", token->number);
      break;
    case MSTR:
      printf (" '%.*s'", token->length, token->string);
      break;
    default:
      assert (0);
    }
  fflush (stdout);
}

static int
mget_token_dump (struct matrix_token *token, struct dfm_reader *reader)
{
  int result = (mget_token) (token, reader);
  mdump_token (token);
  return result;
}
#endif

/* Return the current position in READER. */
static const char *
context (struct dfm_reader *reader)
{
  static char buf[32];

  if (dfm_eof (reader))
    strcpy (buf, "at end of file");
  else 
    {
      struct fixed_string line;
      const char *sp;
      
      dfm_get_record (reader, &line);
      sp = ls_c_str (&line);
      while (sp < ls_end (&line) && isspace ((unsigned char) *sp))
        sp++;
      if (sp >= ls_end (&line))
        strcpy (buf, "at end of line");
      else
        {
          char *dp;
          size_t copy_cnt = 0;

          dp = stpcpy (buf, "before `");
          while (sp < ls_end (&line) && !isspace ((unsigned char) *sp)
                 && copy_cnt < 10) 
            {
              *dp++ = *sp++;
              copy_cnt++; 
            }
          strcpy (dp, "'");
        }
    }
  
  return buf;
}

/* Is there at least one token left in the data file? */
static int
another_token (struct dfm_reader *reader)
{
  for (;;)
    {
      struct fixed_string line;
      const char *cp;
      
      if (dfm_eof (reader))
        return 0;
      dfm_get_record (reader, &line);

      cp = ls_c_str (&line);
      while (isspace ((unsigned char) *cp) && cp < ls_end (&line))
	cp++;

      if (cp < ls_end (&line)) 
        {
          dfm_forward_columns (reader, cp - ls_c_str (&line));
          return 1;
        }

      dfm_forward_record (reader);
    }
}

/* Parse a MATRIX DATA token from READER into TOKEN. */
static int
(mget_token) (struct matrix_token *token, struct dfm_reader *reader)
{
  struct fixed_string line;
  int first_column;
  char *cp;

  if (!another_token (reader))
    return 0;

  dfm_get_record (reader, &line);
  first_column = dfm_column_start (reader);

  /* Three types of fields: quoted with ', quoted with ", unquoted. */
  cp = ls_c_str (&line);
  if (*cp == '\'' || *cp == '"')
    {
      int quote = *cp;

      token->type = MSTR;
      token->string = ++cp;
      while (cp < ls_end (&line) && *cp != quote)
	cp++;
      token->length = cp - token->string;
      if (cp < ls_end (&line))
	cp++;
      else
	msg (SW, _("Scope of string exceeds line."));
    }
  else
    {
      int is_num = isdigit ((unsigned char) *cp) || *cp == '.';

      token->string = cp++;
      while (cp < ls_end (&line)
             && !isspace ((unsigned char) *cp) && *cp != ','
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
      
      token->length = cp - token->string;
      assert (token->length);

      if (is_num)
	{
	  struct data_in di;

	  di.s = token->string;
	  di.e = token->string + token->length;
	  di.v = (union value *) &token->number;
	  di.f1 = first_column;
	  di.format = make_output_format (FMT_F, token->length, 0);

	  if (!data_in (&di))
	    return 0;
	}
      else
	token->type = MSTR;
    }

  dfm_forward_columns (reader, cp - ls_c_str (&line));
    
  return 1;
}

/* Forcibly skip the end of a line for content type CONTENT in
   READER. */
static int
force_eol (struct dfm_reader *reader, const char *content)
{
  struct fixed_string line;
  const char *cp;

  if (dfm_eof (reader))
    return 0;
  dfm_get_record (reader, &line);

  cp = ls_c_str (&line);
  while (isspace ((unsigned char) *cp) && cp < ls_end (&line))
    cp++;
  
  if (cp < ls_end (&line))
    {
      msg (SE, _("End of line expected %s while reading %s."),
	   context (reader), content);
      return 0;
    }
  
  dfm_forward_record (reader);
  return 1;
}

/* Back end, omitting ROWTYPE_. */

struct nr_aux_data 
  {
    struct matrix_data_pgm *mx; /* MATRIX DATA program. */
    double ***data;             /* MATRIX DATA data. */
    double *factor_values;      /* Factor values. */
    int max_cell_idx;           /* Max-numbered cell that we have
                                   read so far, plus one. */
    double *split_values;       /* SPLIT FILE variable values. */
  };

static int nr_read_splits (struct nr_aux_data *, int compare);
static int nr_read_factors (struct nr_aux_data *, int cell);
static bool nr_output_data (struct nr_aux_data *, struct ccase *,
                            write_case_func *, write_case_data);
static bool matrix_data_read_without_rowtype (struct case_source *source,
                                              struct ccase *,
                                              write_case_func *,
                                              write_case_data);

/* Read from the data file and write it to the active file.
   Returns true if successful, false if an I/O error occurred. */
static bool
read_matrices_without_rowtype (struct matrix_data_pgm *mx)
{
  struct nr_aux_data nr;
  bool ok;
  
  if (mx->cells == -1)
    mx->cells = 1;

  nr.mx = mx;
  nr.data = NULL;
  nr.factor_values = xnmalloc (mx->n_factors * mx->cells,
                               sizeof *nr.factor_values);
  nr.max_cell_idx = 0;
  nr.split_values = xnmalloc (dict_get_split_cnt (default_dict),
                              sizeof *nr.split_values);

  vfm_source = create_case_source (&matrix_data_without_rowtype_source_class, &nr);
  
  ok = procedure (NULL, NULL);

  free (nr.split_values);
  free (nr.factor_values);

  return ok;
}

/* Mirror data across the diagonal of matrix CP which contains
   CONTENT type data. */
static void
fill_matrix (struct matrix_data_pgm *mx, int content, double *cp)
{
  int type = content_type[content];

  if (type == 1 && mx->section != FULL)
    {
      if (mx->diag == NODIAGONAL)
	{
	  const double fill = content == CORR ? 1.0 : SYSMIS;
	  int i;

	  for (i = 0; i < mx->n_continuous; i++)
	    cp[i * (1 + mx->n_continuous)] = fill;
	}
      
      {
	int c, r;
	
	if (mx->section == LOWER)
	  {
	    int n_lines = mx->n_continuous;
	    if (mx->section != FULL && mx->diag == NODIAGONAL)
	      n_lines--;
	    
	    for (r = 1; r < n_lines; r++)
	      for (c = 0; c < r; c++)
		cp[r + c * mx->n_continuous] = cp[c + r * mx->n_continuous];
	  }
	else 
	  {
	    assert (mx->section == UPPER);
	    for (r = 1; r < mx->n_continuous; r++)
	      for (c = 0; c < r; c++)
		cp[c + r * mx->n_continuous] = cp[r + c * mx->n_continuous];
	  }
      }
    }
  else if (type == 2)
    {
      int c;

      for (c = 1; c < mx->n_continuous; c++)
	cp[c] = cp[0];
    }
}

/* Read data lines for content type CONTENT from the data file.
   If PER_FACTOR is nonzero, then factor information is read from
   the data file.  Data is for cell number CELL. */
static int
nr_read_data_lines (struct nr_aux_data *nr,
                    int per_factor, int cell, int content, int compare)
{
  struct matrix_data_pgm *mx = nr->mx;
  const int type = content_type[content];               /* Content type. */
  int n_lines; /* Number of lines to parse from data file for this type. */
  double *cp;                   /* Current position in vector or matrix. */
  int i;

  if (type != 1)
    n_lines = 1;
  else
    {
      n_lines = mx->n_continuous;
      if (mx->section != FULL && mx->diag == NODIAGONAL)
	n_lines--;
    }

  cp = nr->data[content][cell];
  if (type == 1 && mx->section == LOWER && mx->diag == NODIAGONAL)
    cp += mx->n_continuous;

  for (i = 0; i < n_lines; i++)
    {
      int n_cols;
      
      if (!nr_read_splits (nr, 1))
	return 0;
      if (per_factor && !nr_read_factors (nr, cell))
	return 0;
      compare = 1;

      switch (type)
	{
	case 0:
	  n_cols = mx->n_continuous;
	  break;
	case 1:
	  switch (mx->section)
	    {
	    case LOWER:
	      n_cols = i + 1;
	      break;
	    case UPPER:
	      cp += i;
	      n_cols = mx->n_continuous - i;
	      if (mx->diag == NODIAGONAL)
		{
		  n_cols--;
		  cp++;
		}
	      break;
	    case FULL:
	      n_cols = mx->n_continuous;
	      break;
	    default:
	      assert (0);
              abort ();
	    }
	  break;
	case 2:
	  n_cols = 1;
	  break;
	default:
	  assert (0);
          abort ();
	}

      {
	int j;
	
	for (j = 0; j < n_cols; j++)
	  {
            struct matrix_token token;
	    if (!mget_token (&token, mx->reader))
	      return 0;
	    if (token.type != MNUM)
	      {
		msg (SE, _("expecting value for %s %s"),
		     dict_get_var (default_dict, j)->name,
                     context (mx->reader));
		return 0;
	      }

	    *cp++ = token.number;
	  }
	if (mx->fmt != FREE
            && !force_eol (mx->reader, content_names[content]))
	  return 0;
      }

      if (mx->section == LOWER)
	cp += mx->n_continuous - n_cols;
    }

  fill_matrix (mx, content, nr->data[content][cell]);

  return 1;
}

/* When ROWTYPE_ does not appear in the data, reads the matrices and
   writes them to the output file.
   Returns true if successful, false if an I/O error occurred. */
static bool
matrix_data_read_without_rowtype (struct case_source *source,
                                  struct ccase *c,
                                  write_case_func *write_case,
                                  write_case_data wc_data)
{
  struct nr_aux_data *nr = source->aux;
  struct matrix_data_pgm *mx = nr->mx;

  {
    int *cp;

    nr->data = pool_nalloc (mx->container, PROX + 1, sizeof *nr->data);
    
    {
      int i;

      for (i = 0; i <= PROX; i++)
	nr->data[i] = NULL;
    }
    
    for (cp = mx->contents; *cp != EOC; cp++)
      if (*cp != LPAREN && *cp != RPAREN)
	{
	  int per_factor = mx->is_per_factor[*cp];
	  int n_entries;
	  
	  n_entries = mx->n_continuous;
	  if (content_type[*cp] == 1)
	    n_entries *= mx->n_continuous;
	  
	  {
	    int n_vectors = per_factor ? mx->cells : 1;
	    int i;
	    
	    nr->data[*cp] = pool_nalloc (mx->container,
                                         n_vectors, sizeof **nr->data);
	    
	    for (i = 0; i < n_vectors; i++)
	      nr->data[*cp][i] = pool_nalloc (mx->container,
                                              n_entries, sizeof ***nr->data);
	  }
	}
  }
  
  for (;;)
    {
      int *bp, *ep, *np;
      
      if (!nr_read_splits (nr, 0))
	return true;
      
      for (bp = mx->contents; *bp != EOC; bp = np)
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
	      
	    for (i = 0; i < (per_factor ? mx->cells : 1); i++)
	      {
		int *cp;

		for (cp = bp; cp < ep; cp++) 
		  if (!nr_read_data_lines (nr, per_factor, i, *cp, cp != bp))
		    return true;
	      }
	  }
	}

      if (!nr_output_data (nr, c, write_case, wc_data))
        return false;

      if (dict_get_split_cnt (default_dict) == 0
          || !another_token (mx->reader))
	return true;
    }
}

/* Read the split file variables.  If COMPARE is 1, compares the
   values read to the last values read and returns 1 if they're equal,
   0 otherwise. */
static int
nr_read_splits (struct nr_aux_data *nr, int compare)
{
  struct matrix_data_pgm *mx = nr->mx;
  static int just_read = 0; /* FIXME: WTF? */
  size_t split_cnt;
  size_t i;

  if (compare && just_read)
    {
      just_read = 0;
      return 1;
    }
  
  if (dict_get_split_vars (default_dict) == NULL)
    return 1;

  if (mx->single_split)
    {
      if (!compare) 
        {
          struct mxd_var *mv = dict_get_split_vars (default_dict)[0]->aux;
          nr->split_values[0] = ++mv->sub_type; 
        }
      return 1;
    }

  if (!compare)
    just_read = 1;

  split_cnt = dict_get_split_cnt (default_dict);
  for (i = 0; i < split_cnt; i++) 
    {
      struct matrix_token token;
      if (!mget_token (&token, mx->reader))
        return 0;
      if (token.type != MNUM)
        {
          msg (SE, _("Syntax error expecting SPLIT FILE value %s."),
               context (mx->reader));
          return 0;
        }

      if (!compare)
        nr->split_values[i] = token.number;
      else if (nr->split_values[i] != token.number)
        {
          msg (SE, _("Expecting value %g for %s."),
               nr->split_values[i],
               dict_get_split_vars (default_dict)[i]->name);
          return 0;
        }
    }

  return 1;
}

/* Read the factors for cell CELL.  If COMPARE is 1, compares the
   values read to the last values read and returns 1 if they're equal,
   0 otherwise. */
static int
nr_read_factors (struct nr_aux_data *nr, int cell)
{
  struct matrix_data_pgm *mx = nr->mx;
  int compare;
  
  if (mx->n_factors == 0)
    return 1;

  assert (nr->max_cell_idx >= cell);
  if (cell != nr->max_cell_idx)
    compare = 1;
  else
    {
      compare = 0;
      nr->max_cell_idx++;
    }
      
  {
    size_t i;
    
    for (i = 0; i < mx->n_factors; i++)
      {
        struct matrix_token token;
	if (!mget_token (&token, mx->reader))
	  return 0;
	if (token.type != MNUM)
	  {
	    msg (SE, _("Syntax error expecting factor value %s."),
		 context (mx->reader));
	    return 0;
	  }
	
	if (!compare)
	  nr->factor_values[i + mx->n_factors * cell] = token.number;
	else if (nr->factor_values[i + mx->n_factors * cell] != token.number)
	  {
	    msg (SE, _("Syntax error expecting value %g for %s %s."),
		 nr->factor_values[i + mx->n_factors * cell],
		 mx->factors[i]->name, context (mx->reader));
	    return 0;
	  }
      }
  }

  return 1;
}

/* Write the contents of a cell having content type CONTENT and data
   CP to the active file.
   Returns true if successful, false if an I/O error occurred. */
static bool
dump_cell_content (struct matrix_data_pgm *mx, int content, double *cp,
                   struct ccase *c,
                   write_case_func *write_case, write_case_data wc_data)
{
  int type = content_type[content];

  {
    buf_copy_str_rpad (case_data_rw (c, mx->rowtype_->fv)->s, 8,
                       content_names[content]);
    
    if (type != 1)
      memset (case_data_rw (c, mx->varname_->fv)->s, ' ', 8);
  }

  {
    int n_lines = (type == 1) ? mx->n_continuous : 1;
    int i;
		
    for (i = 0; i < n_lines; i++)
      {
	int j;

	for (j = 0; j < mx->n_continuous; j++)
	  {
            int fv = dict_get_var (default_dict, mx->first_continuous + j)->fv;
            case_data_rw (c, fv)->f = *cp;
	    cp++;
	  }
	if (type == 1)
	  buf_copy_str_rpad (case_data_rw (c, mx->varname_->fv)->s, 8,
                             dict_get_var (default_dict,
                                           mx->first_continuous + i)->name);
	if (!write_case (wc_data))
          return false;
      }
  }
  return true;
}

/* Finally dump out everything from nr_data[] to the output file. */
static bool
nr_output_data (struct nr_aux_data *nr, struct ccase *c,
                write_case_func *write_case, write_case_data wc_data)
{
  struct matrix_data_pgm *mx = nr->mx;
  
  {
    struct variable *const *split;
    size_t split_cnt;
    size_t i;

    split_cnt = dict_get_split_cnt (default_dict);
    split = dict_get_split_vars (default_dict);
    for (i = 0; i < split_cnt; i++)
      case_data_rw (c, split[i]->fv)->f = nr->split_values[i];
  }

  if (mx->n_factors)
    {
      int cell;

      for (cell = 0; cell < mx->cells; cell++)
	{
	  {
	    size_t factor;

	    for (factor = 0; factor < mx->n_factors; factor++)
              case_data_rw (c, mx->factors[factor]->fv)->f
                = nr->factor_values[factor + cell * mx->n_factors];
	  }
	  
	  {
	    int content;
	    
	    for (content = 0; content <= PROX; content++)
	      if (mx->is_per_factor[content])
		{
		  assert (nr->data[content] != NULL
			  && nr->data[content][cell] != NULL);

		  if (!dump_cell_content (mx, content, nr->data[content][cell],
                                          c, write_case, wc_data))
                    return false;
		}
	  }
	}
    }

  {
    int content;
    
    {
      size_t factor;

      for (factor = 0; factor < mx->n_factors; factor++)
	case_data_rw (c, mx->factors[factor]->fv)->f = SYSMIS;
    }
    
    for (content = 0; content <= PROX; content++)
      if (!mx->is_per_factor[content] && nr->data[content] != NULL) 
        {
          if (!dump_cell_content (mx, content, nr->data[content][0],
                                  c, write_case, wc_data))
            return false; 
        }
  }

  return true;
}

/* Back end, with ROWTYPE_. */

/* All the data for one set of factor values. */
struct factor_data
  {
    double *factors;
    int n_rows[PROX + 1];
    double *data[PROX + 1];
    struct factor_data *next;
  };

/* With ROWTYPE_ auxiliary data. */
struct wr_aux_data 
  {
    struct matrix_data_pgm *mx;         /* MATRIX DATA program. */
    int content;                        /* Type of current row. */
    double *split_values;               /* SPLIT FILE variable values. */
    struct factor_data *data;           /* All the data. */
    struct factor_data *current;        /* Current factor. */
  };

static int wr_read_splits (struct wr_aux_data *, struct ccase *,
                           write_case_func *, write_case_data);
static bool wr_output_data (struct wr_aux_data *, struct ccase *,
                           write_case_func *, write_case_data);
static int wr_read_rowtype (struct wr_aux_data *, 
                            const struct matrix_token *, struct dfm_reader *);
static int wr_read_factors (struct wr_aux_data *);
static int wr_read_indeps (struct wr_aux_data *);
static bool matrix_data_read_with_rowtype (struct case_source *,
                                           struct ccase *,
                                           write_case_func *,
                                           write_case_data);

/* When ROWTYPE_ appears in the data, reads the matrices and writes
   them to the output file.
   Returns true if successful, false if an I/O error occurred. */
static bool
read_matrices_with_rowtype (struct matrix_data_pgm *mx)
{
  struct wr_aux_data wr;
  bool ok;

  wr.mx = mx;
  wr.content = -1;
  wr.split_values = NULL;
  wr.data = NULL;
  wr.current = NULL;
  mx->cells = 0;

  vfm_source = create_case_source (&matrix_data_with_rowtype_source_class,
                                   &wr);
  ok = procedure (NULL, NULL);

  free (wr.split_values);
  return ok;
}

/* Read from the data file and write it to the active file.
   Returns true if successful, false if an I/O error occurred. */
static bool
matrix_data_read_with_rowtype (struct case_source *source,
                               struct ccase *c,
                               write_case_func *write_case,
                               write_case_data wc_data)
{
  struct wr_aux_data *wr = source->aux;
  struct matrix_data_pgm *mx = wr->mx;

  do
    {
      if (!wr_read_splits (wr, c, write_case, wc_data))
	return true;

      if (!wr_read_factors (wr))
	return true;

      if (!wr_read_indeps (wr))
	return true;
    }
  while (another_token (mx->reader));

  return wr_output_data (wr, c, write_case, wc_data);
}

/* Read the split file variables.  If they differ from the previous
   set of split variables then output the data.  Returns success. */
static int 
wr_read_splits (struct wr_aux_data *wr,
                struct ccase *c,
                write_case_func *write_case, write_case_data wc_data)
{
  struct matrix_data_pgm *mx = wr->mx;
  int compare;
  size_t split_cnt;

  split_cnt = dict_get_split_cnt (default_dict);
  if (split_cnt == 0)
    return 1;

  if (wr->split_values)
    compare = 1;
  else
    {
      compare = 0;
      wr->split_values = xnmalloc (split_cnt, sizeof *wr->split_values);
    }
  
  {
    int different = 0;
    int i;

    for (i = 0; i < split_cnt; i++)
      {
        struct matrix_token token;
	if (!mget_token (&token, mx->reader))
	  return 0;
	if (token.type != MNUM)
	  {
	    msg (SE, _("Syntax error %s expecting SPLIT FILE value."),
		 context (mx->reader));
	    return 0;
	  }

	if (compare && wr->split_values[i] != token.number && !different)
	  {
	    if (!wr_output_data (wr, c, write_case, wc_data))
	      return 0;
	    different = 1;
	    mx->cells = 0;
	  }
	wr->split_values[i] = token.number;
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

/* Return strcmp()-type comparison of the MX->n_factors factors at _A and
   _B.  Sort missing values toward the end. */
static int
compare_factors (const void *a_, const void *b_, void *mx_)
{
  struct matrix_data_pgm *mx = mx_;
  struct factor_data *const *pa = a_;
  struct factor_data *const *pb = b_;
  const double *a = (*pa)->factors;
  const double *b = (*pb)->factors;

  return lexicographical_compare_3way (a, mx->n_factors,
                                       b, mx->n_factors,
                                       sizeof *a,
                                       compare_doubles, NULL);
}

/* Write out the data for the current split file to the active
   file.
   Returns true if successful, false if an I/O error occurred. */
static bool
wr_output_data (struct wr_aux_data *wr,
                struct ccase *c,
                write_case_func *write_case, write_case_data wc_data)
{
  struct matrix_data_pgm *mx = wr->mx;
  bool ok = true;

  {
    struct variable *const *split;
    size_t split_cnt;
    size_t i;

    split_cnt = dict_get_split_cnt (default_dict);
    split = dict_get_split_vars (default_dict);
    for (i = 0; i < split_cnt; i++)
      case_data_rw (c, split[i]->fv)->f = wr->split_values[i];
  }

  /* Sort the wr->data list. */
  {
    struct factor_data **factors;
    struct factor_data *iter;
    int i;

    factors = xnmalloc (mx->cells, sizeof *factors);

    for (i = 0, iter = wr->data; iter; iter = iter->next, i++)
      factors[i] = iter;

    sort (factors, mx->cells, sizeof *factors, compare_factors, mx);

    wr->data = factors[0];
    for (i = 0; i < mx->cells - 1; i++)
      factors[i]->next = factors[i + 1];
    factors[mx->cells - 1]->next = NULL;

    free (factors);
  }

  /* Write out records for every set of factor values. */
  {
    struct factor_data *iter;
    
    for (iter = wr->data; iter; iter = iter->next)
      {
	{
	  size_t factor;

	  for (factor = 0; factor < mx->n_factors; factor++)
            case_data_rw (c, mx->factors[factor]->fv)->f
              = iter->factors[factor];
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
			       ? (mx->n_continuous
				  - (mx->section != FULL && mx->diag == NODIAGONAL))
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

	      fill_matrix (mx, content, iter->data[content]);

	      ok = dump_cell_content (mx, content, iter->data[content],
                                      c, write_case, wc_data);
              if (!ok)
                break;
	    }
	}
      }
  }
  
  pool_destroy (mx->container);
  mx->container = pool_create ();
  
  wr->data = wr->current = NULL;
  
  return ok;
}

/* Sets ROWTYPE_ based on the given TOKEN read from READER.
   Return success. */
static int 
wr_read_rowtype (struct wr_aux_data *wr,
                 const struct matrix_token *token,
                 struct dfm_reader *reader)
{
  if (wr->content != -1)
    {
      msg (SE, _("Multiply specified ROWTYPE_ %s."), context (reader));
      return 0;
    }
  if (token->type != MSTR)
    {
      msg (SE, _("Syntax error %s expecting ROWTYPE_ string."),
           context (reader));
      return 0;
    }
  
  {
    char s[16];
    char *cp;
    
    memcpy (s, token->string, min (15, token->length));
    s[min (15, token->length)] = 0;

    for (cp = s; *cp; cp++)
      *cp = toupper ((unsigned char) *cp);

    wr->content = string_to_content_type (s, NULL);
  }

  if (wr->content == -1)
    {
      msg (SE, _("Syntax error %s."), context (reader));
      return 0;
    }

  return 1;
}

/* Read the factors for the current row.  Select a set of factors and
   point wr_current to it. */
static int 
wr_read_factors (struct wr_aux_data *wr)
{
  struct matrix_data_pgm *mx = wr->mx;
  double *factor_values = local_alloc (sizeof *factor_values * mx->n_factors);

  wr->content = -1;
  {
    size_t i;
  
    for (i = 0; i < mx->n_factors; i++)
      {
        struct matrix_token token;
	if (!mget_token (&token, mx->reader))
	  goto lossage;
	if (token.type == MSTR)
	  {
	    if (!wr_read_rowtype (wr, &token, mx->reader))
	      goto lossage;
	    if (!mget_token (&token, mx->reader))
	      goto lossage;
	  }
	if (token.type != MNUM)
	  {
	    msg (SE, _("Syntax error expecting factor value %s."),
		 context (mx->reader));
	    goto lossage;
	  }
	
	factor_values[i] = token.number;
      }
  }
  if (wr->content == -1)
    {
      struct matrix_token token;
      if (!mget_token (&token, mx->reader))
	goto lossage;
      if (!wr_read_rowtype (wr, &token, mx->reader))
	goto lossage;
    }
  
  /* Try the most recent factor first as a simple caching
     mechanism. */
  if (wr->current)
    {
      size_t i;
      
      for (i = 0; i < mx->n_factors; i++)
	if (factor_values[i] != wr->current->factors[i])
	  goto cache_miss;
      goto winnage;
    }

  /* Linear search through the list. */
cache_miss:
  {
    struct factor_data *iter;

    for (iter = wr->data; iter; iter = iter->next)
      {
	size_t i;

	for (i = 0; i < mx->n_factors; i++)
	  if (factor_values[i] != iter->factors[i])
	    goto next_item;
	
	wr->current = iter;
	goto winnage;
	
      next_item: ;
      }
  }

  /* Not found.  Make a new item. */
  {
    struct factor_data *new = pool_alloc (mx->container, sizeof *new);

    new->factors = pool_nalloc (mx->container,
                                mx->n_factors, sizeof *new->factors);
    
    {
      size_t i;

      for (i = 0; i < mx->n_factors; i++)
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

    new->next = wr->data;
    wr->data = wr->current = new;
    mx->cells++;
  }

winnage:
  local_free (factor_values);
  return 1;

lossage:
  local_free (factor_values);
  return 0;
}

/* Read the independent variables into wr->current. */
static int 
wr_read_indeps (struct wr_aux_data *wr)
{
  struct matrix_data_pgm *mx = wr->mx;
  struct factor_data *c = wr->current;
  const int type = content_type[wr->content];
  const int n_rows = c->n_rows[wr->content];
  double *cp;
  int n_cols;

  /* Allocate room for data if necessary. */
  if (c->data[wr->content] == NULL)
    {
      int n_items = mx->n_continuous;
      if (type == 1)
	n_items *= mx->n_continuous;
      
      c->data[wr->content] = pool_nalloc (mx->container,
                                          n_items, sizeof **c->data);
    }

  cp = &c->data[wr->content][n_rows * mx->n_continuous];

  /* Figure out how much to read from this line. */
  switch (type)
    {
    case 0:
    case 2:
      if (n_rows > 0)
	{
	  msg (SE, _("Duplicate specification for %s."),
	       content_names[wr->content]);
	  return 0;
	}
      if (type == 0)
	n_cols = mx->n_continuous;
      else
	n_cols = 1;
      break;
    case 1:
      if (n_rows >= mx->n_continuous - (mx->section != FULL && mx->diag == NODIAGONAL))
	{
	  msg (SE, _("Too many rows of matrix data for %s."),
	       content_names[wr->content]);
	  return 0;
	}
      
      switch (mx->section)
	{
	case LOWER:
	  n_cols = n_rows + 1;
	  if (mx->diag == NODIAGONAL)
	    cp += mx->n_continuous;
	  break;
	case UPPER:
	  cp += n_rows;
	  n_cols = mx->n_continuous - n_rows;
	  if (mx->diag == NODIAGONAL)
	    {
	      n_cols--;
	      cp++;
	    }
	  break;
	case FULL:
	  n_cols = mx->n_continuous;
	  break;
	default:
	  assert (0);
          abort ();
	}
      break;
    default:
      assert (0);
      abort ();
    }
  c->n_rows[wr->content]++;

  /* Read N_COLS items at CP. */
  {
    int j;
	
    for (j = 0; j < n_cols; j++)
      {
        struct matrix_token token;
	if (!mget_token (&token, mx->reader))
	  return 0;
	if (token.type != MNUM)
	  {
	    msg (SE, _("Syntax error expecting value for %s %s."),
                 dict_get_var (default_dict, mx->first_continuous + j)->name,
                 context (mx->reader));
	    return 0;
	  }

	*cp++ = token.number;
      }
    if (mx->fmt != FREE
        && !force_eol (mx->reader, content_names[wr->content]))
      return 0;
  }

  return 1;
}

/* Matrix source. */

static const struct case_source_class matrix_data_with_rowtype_source_class = 
  {
    "MATRIX DATA",
    NULL,
    matrix_data_read_with_rowtype,
    NULL,
  };

static const struct case_source_class 
matrix_data_without_rowtype_source_class =
  {
    "MATRIX DATA",
    NULL,
    matrix_data_read_without_rowtype,
    NULL,
  };

