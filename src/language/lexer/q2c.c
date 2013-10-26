/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2008, 2010, 2011 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

/* GNU C allows the programmer to declare that certain functions take
   printf-like arguments, never return, etc.  Conditionalize these
   declarations on whether gcc is in use. */
#if __GNUC__ > 1
#define ATTRIBUTE(X) __attribute__ (X)
#else
#define ATTRIBUTE(X)
#endif

/* Marks a function argument as possibly not used. */
#define UNUSED ATTRIBUTE ((unused))

/* Marks a function that will never return. */
#define NO_RETURN ATTRIBUTE ((noreturn))

/* Mark a function as taking a printf- or scanf-like format
   string as its FMT'th argument and that the FIRST'th argument
   is the first one to be checked against the format string. */
#define PRINTF_FORMAT(FMT, FIRST) ATTRIBUTE ((format (__printf__, FMT, FIRST)))

/* Max length of an input line. */
#define MAX_LINE_LEN 1024

/* Max token length. */
#define MAX_TOK_LEN 1024

/* argv[0]. */
static char *program_name;

/* Have the input and output files been opened yet? */
static bool is_open;

/* Input, output files. */
static FILE *in, *out;

/* Input, output file names. */
static char *ifn, *ofn;

/* Input, output file line number. */
static int ln, oln = 1;

/* Input line buffer, current position. */
static char *buf, *cp;

/* Token types. */
enum
  {
    T_STRING = 256,	/* String literal. */
    T_ID = 257		/* Identifier.  */
  };

/* Current token: either one of the above, or a single character. */
static int token;

/* Token string value. */
static char *tokstr;

/* Utility functions. */

/* Close all open files and delete the output file, on failure. */
static void
finish_up (void)
{
  if (!is_open)
    return;
  is_open = false;
  fclose (in);
  fclose (out);
  if (remove (ofn) == -1)
    fprintf (stderr, "%s: %s: remove: %s\n", program_name, ofn, strerror (errno));
}

void hcf (void) NO_RETURN;

/* Terminate unsuccessfully. */
void
hcf (void)
{
  finish_up ();
  exit (EXIT_FAILURE);
}

int fail (const char *, ...) PRINTF_FORMAT (1, 2) NO_RETURN;
int error (const char *, ...) PRINTF_FORMAT (1, 2) NO_RETURN;

/* Output an error message and terminate unsuccessfully. */
int
fail (const char *format, ...)
{
  va_list args;

  va_start (args, format);
  fprintf (stderr, "%s: ", program_name);
  vfprintf (stderr, format, args);
  fprintf (stderr, "\n");
  va_end (args);

  hcf ();
}

/* Output a context-dependent error message and terminate
   unsuccessfully. */
int
error (const char *format,...)
{
  va_list args;

  va_start (args, format);
  fprintf (stderr, "%s:%d: (column %d) ", ifn, ln, (int) (cp - buf));
  vfprintf (stderr, format, args);
  fprintf (stderr, "\n");
  va_end (args);

  hcf ();
}

#define VME "virtual memory exhausted"

/* Allocate a block of SIZE bytes and return a pointer to its
   beginning. */
static void *
xmalloc (size_t size)
{
  void *vp;

  if (size == 0)
    return NULL;

  vp = malloc (size);
  if (!vp)
    fail ("xmalloc(%lu): %s", (unsigned long) size, VME);

  return vp;
}

/* Make a dynamically allocated copy of string S and return a pointer
   to the first character. */
static char *
xstrdup (const char *s)
{
  size_t size;
  char *t;

  assert (s != NULL);
  size = strlen (s) + 1;

  t = malloc (size);
  if (!t)
    fail ("xstrdup(%lu): %s", (unsigned long) strlen (s), VME);

  memcpy (t, s, size);
  return t;
}

/* Returns a pointer to one of 8 static buffers.  The buffers are used
   in rotation. */
static char *
get_buffer (void)
{
  static char b[8][256];
  static int cb;

  if (++cb >= 8)
    cb = 0;

  return b[cb];
}

/* Copies a string to a static buffer, converting it to lowercase in
   the process, and returns a pointer to the static buffer. */
static char *
st_lower (const char *s)
{
  char *p, *cp;

  p = cp = get_buffer ();
  while (*s)
    *cp++ = tolower ((unsigned char) (*s++));
  *cp++ = '\0';

  return p;
}

/* Copies a string to a static buffer, converting it to uppercase in
   the process, and returns a pointer to the static buffer. */
static char *
st_upper (const char *s)
{
  char *p, *cp;

  p = cp = get_buffer ();
  while (*s)
    *cp++ = toupper ((unsigned char) (*s++));
  *cp++ = '\0';

  return p;
}

/* Returns the address of the first non-whitespace character in S, or
   the address of the null terminator if none. */
static char *
skip_ws (char *s)
{
  while (isspace ((unsigned char) *s))
    s++;
  return s;
}

/* Read one line from the input file into buf.  Lines having special
   formats are handled specially. */
static bool
get_line (void)
{
  ln++;
  if (0 == fgets (buf, MAX_LINE_LEN, in))
    {
      if (ferror (in))
	fail ("%s: fgets: %s", ifn, strerror (errno));
      return false;
    }

  cp = strchr (buf, '\n');
  if (cp != NULL)
    *cp = '\0';

  cp = buf;
  return true;
}

/* Symbol table manager. */

/* Symbol table entry. */
typedef struct symbol symbol;
struct symbol
  {
    symbol *next;		/* Next symbol in symbol table. */
    char *name;			/* Symbol name. */
    int unique;			/* 1=Name must be unique in this file. */
    int ln;			/* Line number of definition. */
    int value;			/* Symbol value. */
  };

/* Symbol table. */
symbol *symtab;

/* Add a symbol to the symbol table having name NAME, uniqueness
   UNIQUE, and value VALUE.  If a symbol having the same name is found
   in the symbol table, its sequence number is returned and the symbol
   table is not modified.  Otherwise, the symbol is added and the next
   available sequence number is returned. */
static int
add_symbol (const char *name, int unique, int value)
{
  symbol *iter, *sym;
  int x;

  sym = xmalloc (sizeof *sym);
  sym->name = xstrdup (name);
  sym->unique = unique;
  sym->value = value;
  sym->next = NULL;
  sym->ln = ln;
  if (!symtab)
    {
      symtab = sym;
      return 1;
    }
  iter = symtab;
  x = 1;
  for (;;)
    {
      if (!strcmp (iter->name, name))
	{
	  if (iter->unique)
	    {
	      fprintf (stderr, "%s:%d: `%s' is already defined above\n", ifn,
		       ln, name);
	      fprintf (stderr, "%s:%d: location of previous definition\n", ifn,
		       iter->ln);
	      hcf ();
	    }
	  free (sym->name);
	  free (sym);
	  return x;
	}
      if (!iter->next)
	break;
      iter = iter->next;
      x++;
    }
  iter->next = sym;
  return ++x;
}

/* Finds the symbol having given sequence number X within the symbol
   table, and returns the associated symbol structure. */
static symbol *
find_symbol (int x)
{
  symbol *iter;

  iter = symtab;
  while (x > 1 && iter)
    {
      iter = iter->next;
      x--;
    }
  assert (iter);
  return iter;
}

#if DUMP_TOKENS
/* Writes a printable representation of the current token to
   stdout. */
static void
dump_token (void)
{
  switch (token)
    {
    case T_STRING:
      printf ("STRING\t\"%s\"\n", tokstr);
      break;
    case T_ID:
      printf ("ID\t%s\n", tokstr);
      break;
    default:
      printf ("PUNCT\t%c\n", token);
    }
}
#endif /* DUMP_TOKENS */


const char hyphen_proxy = '_';

static void
id_cpy (char **cp)
{
  char *dest = tokstr;
  char *src = *cp;

  while (*src == '_' || *src == '-' || isalnum ((unsigned char) *src))
    {
      *dest++ = *src == '-' ? hyphen_proxy :toupper ((unsigned char) (*src));
      src++;
    }

  *cp = src;
  *dest++ = '\0';
}

static char *
unmunge (const char *s)
{
  char *dest = xmalloc (strlen (s) + 1);
  char *d = dest;

  while (*s)
    {
      if (*s == hyphen_proxy)
	*d = '-';
      else
	*d = *s;
      s++;
      d++;
    }
  *d = '\0';

  return dest;
}

/* Reads a token from the input file. */
static int
lex_get (void)
{
  /* Skip whitespace and check for end of file. */
  for (;;)
    {
      cp = skip_ws (cp);
      if (*cp != '\0')
	break;

      if (!get_line ())
	fail ("%s: Unexpected end of file.", ifn);
    }

  if (*cp == '"')
    {
      char *dest = tokstr;
      token = T_STRING;
      cp++;
      while (*cp != '"' && *cp)
	{
	  if (*cp == '\\')
	    {
	      cp++;
	      if (!*cp)
		error ("Unterminated string literal.");
	      *dest++ = *cp++;
	    }
	  else
	    *dest++ = *cp++;
	}
      *dest++ = 0;
      if (!*cp)
	error ("Unterminated string literal.");
      cp++;
    }
  else if (*cp == '_' || isalnum ((unsigned char) *cp))
    {
      char *dest = tokstr;
      token = T_ID;

      id_cpy (&cp);
    }
  else
    token = *cp++;

#if DUMP_TOKENS
  dump_token ();
#endif

  return token;
}

/* Force the current token to be an identifier token. */
static void
force_id (void)
{
  if (token != T_ID)
    error ("Identifier expected.");
}

/* Force the current token to be a string token. */
static void
force_string (void)
{
  if (token != T_STRING)
    error ("String expected.");
}

/* Checks whether the current token is the identifier S; if so, skips
   the token and returns true; otherwise, returns false. */
static bool
match_id (const char *s)
{
  if (token == T_ID && !strcmp (tokstr, s))
    {
      lex_get ();
      return true;
    }
  return false;
}

/* Checks whether the current token is T.  If so, skips the token and
   returns true; otherwise, returns false. */
static bool
match_token (int t)
{
  if (token == t)
    {
      lex_get ();
      return true;
    }
  return false;
}

/* Force the current token to be T, and skip it. */
static void
skip_token (int t)
{
  if (token != t)
    error ("`%c' expected.", t);
  lex_get ();
}

/* Structures. */

/* Some specifiers have associated values. */
enum
  {
    VAL_NONE,	/* No value. */
    VAL_INT,	/* Integer value. */
    VAL_DBL,	/* Floating point value. */
    VAL_STRING  /* String value. */
  };

/* For those specifiers with values, the syntax of those values. */
enum
  {
    VT_PLAIN,	/* Unadorned value. */
    VT_PAREN	/* Value must be enclosed in parentheses. */
  };

/* Forward definition. */
typedef struct specifier specifier;

/* A single setting. */
typedef struct setting setting;
struct setting
  {
    specifier *parent;	/* Owning specifier. */
    setting *next;	/* Next in the chain. */
    char *specname;	/* Name of the setting. */
    int con;		/* Sequence number. */

    /* Values. */
    int valtype;	/* One of VT_*. */
    int value;		/* One of VAL_*. */
    int optvalue;	/* 1=value is optional, 0=value is required. */
    char *valname;	/* Variable name for the value. */
    char *restriction;	/* !=NULL: expression specifying valid values. */
  };

/* A single specifier. */
struct specifier
  {
    specifier *next;	/* Next in the chain. */
    char *varname;	/* Variable name. */
    setting *s;		/* Associated settings. */

    setting *def;	/* Default setting. */
    setting *omit_kw;	/* Setting for which the keyword can be omitted. */

    int index;		/* Next array index. */
  };

/* Subcommand types. */
typedef enum
  {
    SBC_PLAIN,		/* The usual case. */
    SBC_VARLIST,	/* Variable list. */
    SBC_INT,		/* Integer value. */
    SBC_PINT,		/* Integer inside parentheses. */
    SBC_DBL,		/* Floating point value. */
    SBC_INT_LIST,	/* List of integers (?). */
    SBC_DBL_LIST,	/* List of floating points (?). */
    SBC_CUSTOM,		/* Custom. */
    SBC_ARRAY,		/* Array of boolean values. */
    SBC_STRING,		/* String value. */
    SBC_VAR		/* Single variable name. */
  }
subcommand_type;

typedef enum
  {
    ARITY_ONCE_EXACTLY,  /* must occur exactly once */
    ARITY_ONCE_ONLY,     /* zero or once */
    ARITY_MANY           /* 0, 1, ... , inf */
  }subcommand_arity;

/* A single subcommand. */
typedef struct subcommand subcommand;
struct subcommand
  {
    subcommand *next;		/* Next in the chain. */
    char *name;			/* Subcommand name. */
    subcommand_type type;	/* One of SBC_*. */
    subcommand_arity arity;	/* How many times should the subcommand occur*/
    int narray;			/* Index of next array element. */
    const char *prefix;		/* Prefix for variable and constant names. */
    specifier *spec;		/* Array of specifiers. */
    char *pv_options;           /* PV_* options for SBC_VARLIST. */
  };

/* Name of the command; i.e., DESCRIPTIVES. */
char *cmdname;

/* Short prefix for the command; i.e., `dsc_'. */
char *prefix;

/* List of subcommands. */
subcommand *subcommands;

/* Default subcommand if any, or NULL. */
subcommand *def;

/* Parsing. */

void parse_subcommands (void);

/* Parse an entire specification. */
static void
parse (void)
{
  /* Get the command name and prefix. */
  if (token != T_STRING && token != T_ID)
    error ("Command name expected.");
  cmdname = xstrdup (tokstr);
  lex_get ();
  skip_token ('(');
  force_id ();
  prefix = xstrdup (tokstr);
  lex_get ();
  skip_token (')');
  skip_token (':');

  /* Read all the subcommands. */
  subcommands = NULL;
  def = NULL;
  parse_subcommands ();
}

/* Parses a single setting into S, given subcommand information SBC
   and specifier information SPEC. */
static void
parse_setting (setting *s, specifier *spec)
{
  s->parent = spec;

  if (match_token ('*'))
    {
      if (spec->omit_kw)
	error ("Cannot have two settings with omittable keywords.");
      else
	spec->omit_kw = s;
    }

  if (match_token ('!'))
    {
      if (spec->def)
	error ("Cannot have two default settings.");
      else
	spec->def = s;
    }

  force_id ();
  s->specname = xstrdup (tokstr);
  s->con = add_symbol (s->specname, 0, 0);
  s->value = VAL_NONE;

  lex_get ();

  /* Parse setting value info if necessary. */
  if (token != '/' && token != ';' && token != '.' && token != ',')
    {
      if (token == '(')
	{
	  s->valtype = VT_PAREN;
	  lex_get ();
	}
      else
	s->valtype = VT_PLAIN;

      s->optvalue = match_token ('*');

      if (match_id ("N"))
	s->value = VAL_INT;
      else if (match_id ("D"))
	s->value = VAL_DBL;
      else if (match_id ("S"))
        s->value = VAL_STRING;
      else
	error ("`n', `d', or `s' expected.");

      skip_token (':');

      force_id ();
      s->valname = xstrdup (tokstr);
      lex_get ();

      if (token == ',')
	{
	  lex_get ();
	  force_string ();
	  s->restriction = xstrdup (tokstr);
	  lex_get ();
	}
      else
	s->restriction = NULL;

      if (s->valtype == VT_PAREN)
	skip_token (')');
    }
}

/* Parse a single specifier into SPEC, given subcommand information
   SBC. */
static void
parse_specifier (specifier *spec, subcommand *sbc)
{
  spec->index = 0;
  spec->s = NULL;
  spec->def = NULL;
  spec->omit_kw = NULL;
  spec->varname = NULL;

  if (token == T_ID)
    {
      spec->varname = xstrdup (st_lower (tokstr));
      lex_get ();
    }

  /* Handle array elements. */
  if (token != ':')
    {
      spec->index = sbc->narray;
      if (sbc->type == SBC_ARRAY)
	{
	  if (token == '|')
	    token = ',';
	  else
	    sbc->narray++;
	}
      spec->s = NULL;
      return;
    }
  skip_token (':');

  if ( sbc->type == SBC_ARRAY && token == T_ID )
    {
	spec->varname = xstrdup (st_lower (tokstr));
	spec->index = sbc->narray;
	sbc->narray++;
    }



  /* Parse all the settings. */
  {
    setting **s = &spec->s;

    for (;;)
      {
	*s = xmalloc (sizeof **s);
	parse_setting (*s, spec);
	if (token == ',' || token == ';' || token == '.')
	  break;
	skip_token ('/');
	s = &(*s)->next;
      }
    (*s)->next = NULL;
  }
}

/* Parse a list of specifiers for subcommand SBC. */
static void
parse_specifiers (subcommand *sbc)
{
  specifier **spec = &sbc->spec;

  if (token == ';' || token == '.')
    {
      *spec = NULL;
      return;
    }

  for (;;)
    {
      *spec = xmalloc (sizeof **spec);
      parse_specifier (*spec, sbc);
      if (token == ';' || token == '.')
	break;
      skip_token (',');
      spec = &(*spec)->next;
    }
  (*spec)->next = NULL;
}

/* Parse a subcommand into SBC. */
static void
parse_subcommand (subcommand *sbc)
{
  if (match_token ('*'))
    {
      if (def)
	error ("Multiple default subcommands.");
      def = sbc;
    }

  sbc->arity = ARITY_ONCE_ONLY;
  if ( match_token('+'))
    sbc->arity = ARITY_MANY;
  else if (match_token('^'))
    sbc->arity = ARITY_ONCE_EXACTLY ;


  force_id ();
  sbc->name = xstrdup (tokstr);
  lex_get ();

  sbc->narray = 0;
  sbc->type = SBC_PLAIN;
  sbc->spec = NULL;

  if (match_token ('['))
    {
      force_id ();
      sbc->prefix = xstrdup (st_lower (tokstr));
      lex_get ();

      skip_token (']');
      skip_token ('=');

      sbc->type = SBC_ARRAY;
      parse_specifiers (sbc);

    }
  else
    {
      if (match_token ('('))
	{
	  force_id ();
	  sbc->prefix = xstrdup (st_lower (tokstr));
	  lex_get ();

	  skip_token (')');
	}
      else
	sbc->prefix = "";

      skip_token ('=');

      if (match_id ("VAR"))
	sbc->type = SBC_VAR;
      if (match_id ("VARLIST"))
	{
	  if (match_token ('('))
	    {
	      force_string ();
	      sbc->pv_options = xstrdup (tokstr);
	      lex_get();

	      skip_token (')');
	    }
	  else
            sbc->pv_options = NULL;

	  sbc->type = SBC_VARLIST;
	}
      else if (match_id ("INTEGER"))
	sbc->type = match_id ("LIST") ? SBC_INT_LIST : SBC_INT;
      else if (match_id ("PINT"))
	sbc->type = SBC_PINT;
      else if (match_id ("DOUBLE"))
	{
	  if ( match_id ("LIST") )
	    sbc->type = SBC_DBL_LIST;
	  else
	    sbc->type = SBC_DBL;
	}
      else if (match_id ("STRING"))
        sbc->type = SBC_STRING;
      else if (match_id ("CUSTOM"))
	sbc->type = SBC_CUSTOM;
      else
	parse_specifiers (sbc);
    }
}

/* Parse all the subcommands. */
void
parse_subcommands (void)
{
  subcommand **sbc = &subcommands;

  for (;;)
    {
      *sbc = xmalloc (sizeof **sbc);
      (*sbc)->next = NULL;

      parse_subcommand (*sbc);

      if (token == '.')
	return;

      skip_token (';');
      sbc = &(*sbc)->next;
    }
}

/* Output. */

#define BASE_INDENT 2		/* Starting indent. */
#define INC_INDENT 2		/* Indent increment. */

/* Increment the indent. */
#define indent() indent += INC_INDENT
#define outdent() indent -= INC_INDENT

/* Size of the indent from the left margin. */
int indent;

void dump (int, const char *, ...) PRINTF_FORMAT (2, 3);

/* Write line FORMAT to the output file, formatted as with printf,
   indented `indent' characters from the left margin.  If INDENTION is
   greater than 0, indents BASE_INDENT * INDENTION characters after
   writing the line; if INDENTION is less than 0, dedents BASE_INDENT
   * INDENTION characters _before_ writing the line. */
void
dump (int indention, const char *format, ...)
{
  va_list args;
  int i;

  if (indention < 0)
    indent += BASE_INDENT * indention;

  oln++;
  va_start (args, format);
  for (i = 0; i < indent; i++)
    putc (' ', out);
  vfprintf (out, format, args);
  putc ('\n', out);
  va_end (args);

  if (indention > 0)
    indent += BASE_INDENT * indention;
}

/* Writes a blank line to the output file and adjusts 'indent' by BASE_INDENT
   * INDENTION characters.

   (This is only useful because GCC complains about using "" as a format
   string, for whatever reason.) */
static void
dump_blank_line (int indention)
{
  oln++;
  indent += BASE_INDENT * indention;
  putc ('\n', out);
}

/* Write the structure members for specifier SPEC to the output file.
   SBC is the including subcommand. */
static void
dump_specifier_vars (const specifier *spec, const subcommand *sbc)
{
  if (spec->varname)
    dump (0, "long %s%s;", sbc->prefix, spec->varname);

  {
    setting *s;

    for (s = spec->s; s; s = s->next)
      {
	if (s->value != VAL_NONE)
	  {
	    const char *typename;

	    assert (s->value == VAL_INT || s->value == VAL_DBL
                    || s->value == VAL_STRING);
	    typename = (s->value == VAL_INT ? "long"
                        : s->value == VAL_DBL ? "double"
                        : "char *");

	    dump (0, "%s %s%s;", typename, sbc->prefix, st_lower (s->valname));
	  }
      }
  }
}

/* Returns true if string T is a PSPP keyword, false otherwise. */
static bool
is_keyword (const char *t)
{
  static const char *kw[] =
    {
      "AND", "OR", "NOT", "EQ", "GE", "GT", "LE", "LT",
      "NE", "ALL", "BY", "TO", "WITH", 0,
    };
  const char **cp;

  for (cp = kw; *cp; cp++)
    if (!strcmp (t, *cp))
      return true;
  return false;
}

/* Transforms a string NAME into a valid C identifier: makes
   everything lowercase and maps nonalphabetic characters to
   underscores.  Returns a pointer to a static buffer. */
static char *
make_identifier (const char *name)
{
  char *p = get_buffer ();
  char *cp;

  for (cp = p; *name; name++)
    if (isalpha ((unsigned char) *name))
      *cp++ = tolower ((unsigned char) (*name));
    else
      *cp++ = '_';
  *cp = '\0';

  return p;
}

/* Writes the struct and enum declarations for the parser. */
static void
dump_declarations (void)
{
  indent = 0;

  dump (0, "struct dataset;");

  /* Write out enums for all the identifiers in the symbol table. */
  {
    int f, k;
    symbol *sym;
    char *buf = NULL;

    /* Note the squirmings necessary to make sure that the last enum
       is not followed by a comma, as mandated by ANSI C89. */
    for (sym = symtab, f = k = 0; sym; sym = sym->next)
      if (!sym->unique && !is_keyword (sym->name))
	{
	  if (!f)
	    {
	      dump (0, "/* Settings for subcommand specifiers. */");
	      dump (1, "enum");
	      dump (1, "{");
	      f = 1;
	    }

	  if (buf == NULL)
	    buf = xmalloc (1024);
	  else
	    dump (0, "%s", buf);

	  if (k)
	    sprintf (buf, "%s%s,", st_upper (prefix), sym->name);
	  else
	    {
	      k = 1;
	      sprintf (buf, "%s%s = 1000,", st_upper (prefix), sym->name);
	    }
	}
    if (buf)
      {
	buf[strlen (buf) - 1] = 0;
	dump (0, "%s", buf);
	free (buf);
      }
    if (f)
      {
	dump (-1, "};");
	dump_blank_line (-1);
      }
  }

  /* Write out some type definitions */
  {
    dump (0, "#define MAXLISTS 10");
  }


  /* For every array subcommand, write out the associated enumerated
     values. */
  {
    subcommand *sbc;

    for (sbc = subcommands; sbc; sbc = sbc->next)
      if (sbc->type == SBC_ARRAY && sbc->narray)
	{
	  dump (0, "/* Array indices for %s subcommand. */", sbc->name);

	  dump (1, "enum");
	  dump (1, "{");

	  {
	    specifier *spec;

	    for (spec = sbc->spec; spec; spec = spec->next)
		dump (0, "%s%s%s = %d,",
		      st_upper (prefix), st_upper (sbc->prefix),
		      st_upper (spec->varname), spec->index);

	    dump (0, "%s%scount", st_upper (prefix), st_upper (sbc->prefix));

	    dump (-1, "};");
	    dump_blank_line (-1);
	  }
	}
  }

  /* Write out structure declaration. */
  {
    subcommand *sbc;

    dump (0, "/* %s structure. */", cmdname);
    dump (1, "struct cmd_%s", make_identifier (cmdname));
    dump (1, "{");
    for (sbc = subcommands; sbc; sbc = sbc->next)
      {
	int f = 0;

	if (sbc != subcommands)
	  dump_blank_line (0);

	dump (0, "/* %s subcommand. */", sbc->name);
	dump (0, "int sbc_%s;", st_lower (sbc->name));

	switch (sbc->type)
	  {
	  case SBC_ARRAY:
	  case SBC_PLAIN:
	    {
	      specifier *spec;

	      for (spec = sbc->spec; spec; spec = spec->next)
		{
		  if (spec->s == 0)
		    {
		      if (sbc->type == SBC_PLAIN)
			dump (0, "long int %s%s;", st_lower (sbc->prefix),
			      spec->varname);
		      else if (f == 0)
			{
			  dump (0, "int a_%s[%s%scount];",
				st_lower (sbc->name),
				st_upper (prefix),
				st_upper (sbc->prefix)
				);

			  f = 1;
			}
		    }
		  else
		    dump_specifier_vars (spec, sbc);
		}
	    }
	    break;

	  case SBC_VARLIST:
	    dump (0, "size_t %sn_%s;", st_lower (sbc->prefix),
		  st_lower (sbc->name));
	    dump (0, "const struct variable **%sv_%s;", st_lower (sbc->prefix),
		  st_lower (sbc->name));
	    break;

	  case SBC_VAR:
	    dump (0, "const struct variable *%sv_%s;", st_lower (sbc->prefix),
		  st_lower (sbc->name));
	    break;

	  case SBC_STRING:
	    dump (0, "char *s_%s;", st_lower (sbc->name));
	    break;

	  case SBC_INT:
	  case SBC_PINT:
	    dump (0, "long n_%s[MAXLISTS];", st_lower (sbc->name));
	    break;

	  case SBC_DBL:
	    dump (0, "double n_%s[MAXLISTS];", st_lower (sbc->name));
	    break;

	  case SBC_DBL_LIST:
	    dump (0, "subc_list_double dl_%s[MAXLISTS];",
		  st_lower(sbc->name));
	    break;

	  case SBC_INT_LIST:
	    dump (0, "subc_list_int il_%s[MAXLISTS];",
		  st_lower(sbc->name));
	    break;


	  default:;
	    /* nothing */
	  }
      }

    dump (-1, "};");
    dump_blank_line (-1);
  }

  /* Write out prototypes for custom_*() functions as necessary. */
  {
    bool seen = false;
    subcommand *sbc;

    for (sbc = subcommands; sbc; sbc = sbc->next)
      if (sbc->type == SBC_CUSTOM)
	{
	  if (!seen)
	    {
	      seen = true;
	      dump (0, "/* Prototype for custom subcommands of %s. */",
		    cmdname);
	    }
	  dump (0, "static int %scustom_%s (struct lexer *, struct dataset *, struct cmd_%s *, void *);",
		st_lower (prefix), st_lower (sbc->name),
		make_identifier (cmdname));
	}

    if (seen)
      dump_blank_line (0);
  }

  /* Prototypes for parsing and freeing functions. */
  {
    dump (0, "/* Command parsing functions. */");
    dump (0, "static int parse_%s (struct lexer *, struct dataset *, struct cmd_%s *, void *);",
	  make_identifier (cmdname), make_identifier (cmdname));
    dump (0, "static void free_%s (struct cmd_%s *);",
	  make_identifier (cmdname), make_identifier (cmdname));
    dump_blank_line (0);
  }
}

/* Writes out code to initialize all the variables that need
   initialization for particular specifier SPEC inside subcommand SBC. */
static void
dump_specifier_init (const specifier *spec, const subcommand *sbc)
{
  if (spec->varname)
    {
      char s[256];

      if (spec->def)
	sprintf (s, "%s%s",
		 st_upper (prefix), find_symbol (spec->def->con)->name);
      else
	strcpy (s, "-1");
      dump (0, "p->%s%s = %s;", sbc->prefix, spec->varname, s);
    }

  {
    setting *s;

    for (s = spec->s; s; s = s->next)
      {
	if (s->value != VAL_NONE)
	  {
	    const char *init;

	    assert (s->value == VAL_INT || s->value == VAL_DBL
                    || s->value == VAL_STRING);
	    init = (s->value == VAL_INT ? "LONG_MIN"
                    : s->value == VAL_DBL ? "SYSMIS"
                    : "NULL");

	    dump (0, "p->%s%s = %s;", sbc->prefix, st_lower (s->valname), init);
	  }
      }
  }
}

/* Write code to initialize all variables. */
static void
dump_vars_init (int persistent)
{
  /* Loop through all the subcommands. */
  {
    subcommand *sbc;

    for (sbc = subcommands; sbc; sbc = sbc->next)
      {
	int f = 0;

	dump (0, "p->sbc_%s = 0;", st_lower (sbc->name));
	if ( ! persistent )
	  {
	    switch (sbc->type)
	      {
	      case SBC_INT_LIST:
	      case SBC_DBL_LIST:
		dump (1, "{");
		dump (0, "int i;");
		dump (1, "for (i = 0; i < MAXLISTS; ++i)");
		dump (0, "subc_list_%s_create(&p->%cl_%s[i]) ;",
                      sbc->type == SBC_INT_LIST ? "int" : "double",
                      sbc->type == SBC_INT_LIST ? 'i' : 'd',
		      st_lower (sbc->name)
		      );
		dump (-2, "}");
		break;

	      case SBC_DBL:
		dump (1, "{");
		dump (0, "int i;");
		dump (1, "for (i = 0; i < MAXLISTS; ++i)");
		dump (0, "p->n_%s[i] = SYSMIS;", st_lower (sbc->name));
		dump (-2, "}");
		break;

	      case SBC_CUSTOM:
		/* nothing */
		break;

	      case SBC_PLAIN:
	      case SBC_ARRAY:
		{
		  specifier *spec;

		  for (spec = sbc->spec; spec; spec = spec->next)
		    if (spec->s == NULL)
		      {
			if (sbc->type == SBC_PLAIN)
			  dump (0, "p->%s%s = 0;", sbc->prefix, spec->varname);
			else if (f == 0)
			  {
			    dump (0, "memset (p->a_%s, 0, sizeof p->a_%s);",
				  st_lower (sbc->name), st_lower (sbc->name));
			    f = 1;
			  }
		      }
		    else
		      dump_specifier_init (spec, sbc);
		}
		break;

	      case SBC_VARLIST:
		dump (0, "p->%sn_%s = 0;",
		      st_lower (sbc->prefix), st_lower (sbc->name));
		dump (0, "p->%sv_%s = NULL;",
		      st_lower (sbc->prefix), st_lower (sbc->name));
		break;

	      case SBC_VAR:
		dump (0, "p->%sv_%s = NULL;",
		      st_lower (sbc->prefix), st_lower (sbc->name));
		break;

	      case SBC_STRING:
		dump (0, "p->s_%s = NULL;", st_lower (sbc->name));
		break;

	      case SBC_INT:
	      case SBC_PINT:
		dump (1, "{");
		dump (0, "int i;");
		dump (1, "for (i = 0; i < MAXLISTS; ++i)");
		dump (0, "p->n_%s[i] = LONG_MIN;", st_lower (sbc->name));
		dump (-2, "}");
		break;

	      default:
		abort ();
	      }
	  }
      }
  }
}

/* Return a pointer to a static buffer containing an expression that
   will match token T. */
static char *
make_match (const char *t)
{
  char *s;

  s = get_buffer ();

  while (*t == '_')
    t++;

  if (is_keyword (t))
    sprintf (s, "lex_match (lexer, T_%s)", t);
  else if (!strcmp (t, "ON") || !strcmp (t, "YES"))
    strcpy (s, "(lex_match_id (lexer, \"ON\") || lex_match_id (lexer, \"YES\") "
	    "|| lex_match_id (lexer, \"TRUE\"))");
  else if (!strcmp (t, "OFF") || !strcmp (t, "NO"))
    strcpy (s, "(lex_match_id (lexer, \"OFF\") || lex_match_id (lexer, \"NO\") "
	    "|| lex_match_id (lexer, \"FALSE\"))");
  else if (isdigit ((unsigned char) t[0]))
    sprintf (s, "lex_match_int (lexer, %s)", t);
  else if (strchr (t, hyphen_proxy))
    {
      char *c = unmunge (t);
      sprintf (s, "lex_match_phrase (lexer, \"%s\")", c);
      free (c);
    }
  else
    sprintf (s, "lex_match_id (lexer, \"%s\")", t);

  return s;
}

/* Write out the parsing code for specifier SPEC within subcommand
   SBC. */
static void
dump_specifier_parse (const specifier *spec, const subcommand *sbc)
{
  setting *s;

  if (spec->omit_kw && spec->omit_kw->next)
    error ("Omittable setting is not last setting in `%s' specifier.",
	   spec->varname);
  if (spec->omit_kw && spec->omit_kw->parent->next)
    error ("Default specifier is not in last specifier in `%s' "
	   "subcommand.", sbc->name);

  for (s = spec->s; s; s = s->next)
    {
      int first = spec == sbc->spec && s == spec->s;

      /* Match the setting's keyword. */
      if (spec->omit_kw == s)
	{
	  if (!first)
	    {
	      dump (1, "else");
	      dump (1, "{");
	    }
	  dump (1, "%s;", make_match (s->specname));
	}
      else
	dump (1, "%sif (%s)", first ? "" : "else ",
	      make_match (s->specname));


      /* Handle values. */
      if (s->value == VAL_NONE)
	dump (0, "p->%s%s = %s%s;", sbc->prefix, spec->varname,
	      st_upper (prefix), find_symbol (s->con)->name);
      else
	{
	  if (spec->omit_kw != s)
	    dump (1, "{");

	  if (spec->varname)
	    {
	      dump (0, "p->%s%s = %s%s;", sbc->prefix, spec->varname,
		    st_upper (prefix), find_symbol (s->con)->name);

	      if ( sbc->type == SBC_ARRAY )
		dump (0, "p->a_%s[%s%s%s] = 1;",
		      st_lower (sbc->name),
		      st_upper (prefix), st_upper (sbc->prefix),
		      st_upper (spec->varname));
	    }


	  if (s->valtype == VT_PAREN)
	    {
	      if (s->optvalue)
		{
		  dump (1, "if (lex_match (lexer, T_LPAREN))");
		  dump (1, "{");
		}
	      else
		{
		  dump (1, "if (!lex_match (lexer, T_LPAREN))");
		  dump (1, "{");
		  dump (0, "lex_error_expecting (lexer, \"`('\", "
                        "NULL_SENTINEL);");
                  dump (0, "goto lossage;");
		  dump (-1, "}");
		  outdent ();
		}
	    }

	  if (s->value == VAL_INT)
	    {
	      dump (1, "if (!lex_force_int (lexer))");
	      dump (0, "goto lossage;");
	      dump (-1, "p->%s%s = lex_integer (lexer);",
		    sbc->prefix, st_lower (s->valname));
	    }
	  else if (s->value == VAL_DBL)
	    {
	      dump (1, "if (!lex_force_num (lexer))");
	      dump (0, "goto lossage;");
	      dump (-1, "p->%s%s = lex_tokval (lexer);", sbc->prefix,
		    st_lower (s->valname));
	    }
          else if (s->value == VAL_STRING)
            {
              dump (1, "if (!lex_force_string_or_id (lexer))");
	      dump (0, "goto lossage;");
              dump (-1, "free (p->%s%s);", sbc->prefix, st_lower (s->valname));
              dump (0, "p->%s%s = ss_xstrdup (ss_tokss (lexer));",
                    sbc->prefix, st_lower (s->valname));
            }
          else
            abort ();

	  if (s->restriction)
	    {
	      {
		char *str, *str2;
		str = xmalloc (MAX_TOK_LEN);
		str2 = xmalloc (MAX_TOK_LEN);
		sprintf (str2, "p->%s%s", sbc->prefix, st_lower (s->valname));
		sprintf (str, s->restriction, str2, str2, str2, str2,
			 str2, str2, str2, str2);
		dump (1, "if (!(%s))", str);
		free (str);
		free (str2);
	      }

	      dump (1, "{");
              dump (0, "lex_error (lexer, NULL);");
	      dump (0, "goto lossage;");
	      dump (-1, "}");
	      outdent ();
	    }

	  dump (0, "lex_get (lexer);");

	  if (s->valtype == VT_PAREN)
	    {
	      dump (1, "if (!lex_force_match (lexer, T_RPAREN))");
	      dump (0, "goto lossage;");
	      outdent ();
	      if (s->optvalue)
		{
		  dump (-1, "}");
		  outdent ();
		}
	    }

	  if (s != spec->omit_kw)
	    dump (-1, "}");
	}

      if (s == spec->omit_kw)
	{
	  dump (-1, "}");
	  outdent ();
	}
      outdent ();
    }
}

/* Write out the code to parse subcommand SBC. */
static void
dump_subcommand (const subcommand *sbc)
{
  if (sbc->type == SBC_PLAIN || sbc->type == SBC_ARRAY)
    {
      int count;

      dump (1, "while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)");
      dump (1, "{");

      {
	specifier *spec;

	for (count = 0, spec = sbc->spec; spec; spec = spec->next)
	  {
	    if (spec->s)
	      dump_specifier_parse (spec, sbc);
	    else
	      {
		count++;
		dump (1, "%sif (%s)", spec != sbc->spec ? "else " : "",
		      make_match (st_upper (spec->varname)));
		if (sbc->type == SBC_PLAIN)
		  dump (0, "p->%s%s = 1;", st_lower (sbc->prefix),
			spec->varname);
		else
		  dump (0, "p->a_%s[%s%s%s] = 1;",
			st_lower (sbc->name),
			st_upper (prefix), st_upper (sbc->prefix),
			st_upper (spec->varname));
		outdent ();
	      }
	  }
      }

      {
	specifier *spec;
	setting *s;

	/* This code first finds the last specifier in sbc.  Then it
	   finds the last setting within that last specifier.  Either
	   or both might be NULL. */
	spec = sbc->spec;
	s = NULL;
	if (spec)
	  {
	    while (spec->next)
	      spec = spec->next;
	    s = spec->s;
	    if (s)
	      while (s->next)
		s = s->next;
	  }

	if (spec && (!spec->s || !spec->omit_kw))
	  {
	    dump (1, "else");
	    dump (1, "{");
	    dump (0, "lex_error (lexer, NULL);");
	    dump (0, "goto lossage;");
	    dump (-1, "}");
	    outdent ();
	  }
      }

      dump (0, "lex_match (lexer, T_COMMA);");
      dump (-1, "}");
      outdent ();
    }
  else if (sbc->type == SBC_VARLIST)
    {
      dump (1, "if (!parse_variables_const (lexer, dataset_dict (ds), &p->%sv_%s, &p->%sn_%s, "
	    "PV_APPEND%s%s))",
	    st_lower (sbc->prefix), st_lower (sbc->name),
	    st_lower (sbc->prefix), st_lower (sbc->name),
	    sbc->pv_options ? " |" : "",
	    sbc->pv_options ? sbc->pv_options : "");
      dump (0, "goto lossage;");
      outdent ();
    }
  else if (sbc->type == SBC_VAR)
    {
      dump (0, "p->%sv_%s = parse_variable (lexer, dataset_dict (ds));",
	    st_lower (sbc->prefix), st_lower (sbc->name));
      dump (1, "if (!p->%sv_%s)",
	    st_lower (sbc->prefix), st_lower (sbc->name));
      dump (0, "goto lossage;");
      outdent ();
    }
  else if (sbc->type == SBC_STRING)
    {
      dump (1, "if (!lex_force_string (lexer))");
      dump (0, "return false;");
      outdent ();
      dump (0, "free(p->s_%s);", st_lower(sbc->name) );
      dump (0, "p->s_%s = ss_xstrdup (lex_tokss (lexer));",
	    st_lower (sbc->name));
      dump (0, "lex_get (lexer);");
    }
  else if (sbc->type == SBC_DBL)
    {
      dump (1, "if (!lex_force_num (lexer))");
      dump (0, "goto lossage;");
      dump (-1, "p->n_%s[p->sbc_%s - 1] = lex_number (lexer);",
	    st_lower (sbc->name), st_lower (sbc->name) );
      dump (0, "lex_get(lexer);");
    }
  else if (sbc->type == SBC_INT)
    {
      dump(1, "{");
      dump(0, "int x;");
      dump (1, "if (!lex_force_int (lexer))");
      dump (0, "goto lossage;");
      dump (-1, "x = lex_integer (lexer);");
      dump (0, "lex_get(lexer);");
      dump (0, "p->n_%s[p->sbc_%s - 1] = x;", st_lower (sbc->name), st_lower(sbc->name) );
      dump (-1,"}");
    }
  else if (sbc->type == SBC_PINT)
    {
      dump (0, "lex_match (lexer, T_LPAREN);");
      dump (1, "if (!lex_force_int (lexer))");
      dump (0, "goto lossage;");
      dump (-1, "p->n_%s = lex_integer (lexer);", st_lower (sbc->name));
      dump (0, "lex_match (lexer, T_RPAREN);");
    }
  else if (sbc->type == SBC_DBL_LIST || sbc->type == SBC_INT_LIST)
    {
      dump (0, "if ( p->sbc_%s > MAXLISTS)",st_lower(sbc->name));
      dump (1, "{");
      dump (0, "subc_list_error (lexer, \"%s\", MAXLISTS);",
            st_lower(sbc->name));
      dump (0, "goto lossage;");
      dump (-1,"}");

      dump (1, "while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)");
      dump (1, "{");
      dump (0, "lex_match (lexer, T_COMMA);");
      dump (0, "if (!lex_force_num (lexer))");
      dump (1, "{");
      dump (0, "goto lossage;");
      dump (-1,"}");

      dump (0, "subc_list_%s_push (&p->%cl_%s[p->sbc_%s-1], lex_number (lexer));",
            sbc->type == SBC_INT_LIST ? "int" : "double",
            sbc->type == SBC_INT_LIST ? 'i' : 'd',
            st_lower (sbc->name), st_lower (sbc->name));

      dump (0, "lex_get (lexer);");
      dump (-1,"}");

    }
  else if (sbc->type == SBC_CUSTOM)
    {
      dump (1, "switch (%scustom_%s (lexer, ds, p, aux))",
	    st_lower (prefix), st_lower (sbc->name));
      dump (0, "{");
      dump (1, "case 0:");
      dump (0, "goto lossage;");
      dump (-1, "case 1:");
      indent ();
      dump (0, "break;");
      dump (-1, "case 2:");
      indent ();
      dump (0, "lex_error (lexer, NULL);");
      dump (0, "goto lossage;");
      dump (-1, "default:");
      indent ();
      dump (0, "NOT_REACHED ();");
      dump (-1, "}");
      outdent ();
    }
}

/* Write out entire parser. */
static void
dump_parser (int persistent)
{
  int f;

  indent = 0;

  dump (0, "static int");
  dump (0, "parse_%s (struct lexer *lexer, struct dataset *ds%s, struct cmd_%s *p, void *aux UNUSED)",
        make_identifier (cmdname),
	(def && ( def->type == SBC_VARLIST || def->type == SBC_CUSTOM))?"":" UNUSED",
	make_identifier (cmdname));
  dump (1, "{");

  dump_vars_init (persistent);

  dump (1, "for (;;)");
  dump (1, "{");

  f = 0;
  if (def && (def->type == SBC_VARLIST))
    {
      if (def->type == SBC_VARLIST)
	dump (1, "if (lex_token (lexer) == T_ID "
              "&& dict_lookup_var (dataset_dict (ds), lex_tokcstr (lexer)) != NULL "
	      "&& lex_next_token (lexer, 1) != T_EQUALS)");
      else
	{
	  dump (0, "if ((lex_token (lexer) == T_ID "
                "&& dict_lookup_var (dataset_dict (ds), lex_tokcstr (lexer)) "
		"&& lex_next_token (lexer, 1) != T_EQUALS)");
	  dump (1, "     || token == T_ALL)");
	}
      dump (1, "{");
      dump (0, "p->sbc_%s++;", st_lower (def->name));
      dump (1, "if (!parse_variables_const (lexer, dataset_dict (ds), &p->%sv_%s, &p->%sn_%s, "
	    "PV_APPEND))",
	    st_lower (def->prefix), st_lower (def->name),
	    st_lower (def->prefix), st_lower (def->name));
      dump (0, "goto lossage;");
      dump (-2, "}");
      outdent ();
      f = 1;
    }
  else if (def && def->type == SBC_CUSTOM)
    {
      dump (1, "switch (%scustom_%s (lexer, ds, p, aux))",
	    st_lower (prefix), st_lower (def->name));
      dump (0, "{");
      dump (1, "case 0:");
      dump (0, "goto lossage;");
      dump (-1, "case 1:");
      indent ();
      dump (0, "p->sbc_%s++;", st_lower (def->name));
      dump (0, "continue;");
      dump (-1, "case 2:");
      indent ();
      dump (0, "break;");
      dump (-1, "default:");
      indent ();
      dump (0, "NOT_REACHED ();");
      dump (-1, "}");
      outdent ();
    }

  {
    subcommand *sbc;

    for (sbc = subcommands; sbc; sbc = sbc->next)
      {
	dump (1, "%sif (%s)", f ? "else " : "", make_match (sbc->name));
	f = 1;
	dump (1, "{");

	dump (0, "lex_match (lexer, T_EQUALS);");
	dump (0, "p->sbc_%s++;", st_lower (sbc->name));
	if (sbc->arity != ARITY_MANY)
	  {
	    dump (1, "if (p->sbc_%s > 1)", st_lower (sbc->name));
	    dump (1, "{");
            dump (0, "lex_sbc_only_once (\"%s\");", sbc->name);
	    dump (0, "goto lossage;");
	    dump (-1, "}");
	    outdent ();
	  }
	dump_subcommand (sbc);
	dump (-1, "}");
	outdent ();
      }
  }


  /* Now deal with the /ALGORITHM subcommand implicit to all commands */
  dump(1,"else if ( settings_get_syntax () != COMPATIBLE && lex_match_id(lexer, \"ALGORITHM\"))");
  dump(1,"{");

  dump (0, "lex_match (lexer, T_EQUALS);");

  dump(1,"if (lex_match_id(lexer, \"COMPATIBLE\"))");
  dump(0,"settings_set_cmd_algorithm (COMPATIBLE);");
  outdent();
  dump(1,"else if (lex_match_id(lexer, \"ENHANCED\"))");
  dump(0,"settings_set_cmd_algorithm (ENHANCED);");

  dump (-1, "}");
  outdent ();



  dump (1, "if (!lex_match (lexer, T_SLASH))");
  dump (0, "break;");
  dump (-2, "}");
  outdent ();
  dump_blank_line (0);
  dump (1, "if (lex_token (lexer) != T_ENDCMD)");
  dump (1, "{");
  dump (0, "lex_error (lexer, _(\"expecting end of command\"));");
  dump (0, "goto lossage;");
  dump (-1, "}");
  dump_blank_line (0);

  outdent ();

  {
    /*  Check that mandatory subcommands have been specified  */
    subcommand *sbc;

    for (sbc = subcommands; sbc; sbc = sbc->next)
      {

	if ( sbc->arity == ARITY_ONCE_EXACTLY )
	  {
	    dump (0, "if ( 0 == p->sbc_%s)", st_lower (sbc->name));
	    dump (1, "{");
	    dump (0, "lex_sbc_missing (\"%s\");", sbc->name);
	    dump (0, "goto lossage;");
	    dump (-1, "}");
	    dump_blank_line (0);
	  }
      }
  }

  dump (-1, "return true;");
  dump_blank_line (0);
  dump (-1, "lossage:");
  indent ();
  dump (0, "free_%s (p);", make_identifier (cmdname));
  dump (0, "return false;");
  dump (-1, "}");
  dump_blank_line (0);
}


/* Write the output file header. */
static void
dump_header (void)
{
  indent = 0;
  dump (0,   "/* %s\t\t-*- mode: c; buffer-read-only: t -*-", ofn);
  dump_blank_line (0);
  dump (0, "   Generated by q2c from %s.", ifn);
  dump (0, "   Do not modify!");
  dump (0, " */");
}

/* Write out commands to free variable state. */
static void
dump_free (int persistent)
{
  subcommand *sbc;
  int used;

  indent = 0;

  used = 0;
  if ( ! persistent )
    {
      for (sbc = subcommands; sbc; sbc = sbc->next)
        used = (sbc->type == SBC_STRING
                || sbc->type == SBC_DBL_LIST
                || sbc->type == SBC_INT_LIST);
    }

  dump (0, "static void");
  dump (0, "free_%s (struct cmd_%s *p%s)", make_identifier (cmdname),
	make_identifier (cmdname), used ? "" : " UNUSED");
  dump (1, "{");

  if ( ! persistent )
    {

      for (sbc = subcommands; sbc; sbc = sbc->next)
	{
	  switch (sbc->type)
	    {
            case SBC_VARLIST:
	      dump (0, "free (p->v_%s);", st_lower (sbc->name));
              break;
	    case SBC_STRING:
	      dump (0, "free (p->s_%s);", st_lower (sbc->name));
	      break;
	    case SBC_DBL_LIST:
	    case SBC_INT_LIST:
              dump (0, "{");
	      dump (1, "int i;");
	      dump (2, "for(i = 0; i < MAXLISTS ; ++i)");
	      dump (1, "subc_list_%s_destroy(&p->%cl_%s[i]);",
                    sbc->type == SBC_INT_LIST ? "int" : "double",
                    sbc->type == SBC_INT_LIST ? 'i' : 'd',
                    st_lower (sbc->name));
              dump (0, "}");
	      outdent();
	      break;
            case SBC_PLAIN:
              {
                specifier *spec;
                setting *s;

                for (spec = sbc->spec; spec; spec = spec->next)
                  for (s = spec->s; s; s = s->next)
                    if (s->value == VAL_STRING)
                      dump (0, "free (p->%s%s);",
                            sbc->prefix, st_lower (s->valname));
              }
	    default:
	      break;
	    }
	}
    }

  dump (-1, "}");

}



/* Returns the name of a directive found on the current input line, if
   any, or a null pointer if none found. */
static const char *
recognize_directive (void)
{
  static char directive[16];
  char *sp, *ep;

  sp = skip_ws (buf);
  if (strncmp (sp, "/*", 2))
    return NULL;
  sp = skip_ws (sp + 2);
  if (*sp != '(')
    return NULL;
  sp++;

  ep = strchr (sp, ')');
  if (ep == NULL)
    return NULL;

  if (ep - sp > 15)
    ep = sp + 15;
  memcpy (directive, sp, ep - sp);
  directive[ep - sp] = '\0';
  return directive;
}

int
main (int argc, char *argv[])
{
  program_name = argv[0];
  if (argc != 3)
    fail ("Syntax: q2c input.q output.c");

  ifn = argv[1];
  in = fopen (ifn, "r");
  if (!in)
    fail ("%s: open: %s.", ifn, strerror (errno));

  ofn = argv[2];
  out = fopen (ofn, "w");
  if (!out)
    fail ("%s: open: %s.", ofn, strerror (errno));

  is_open = true;
  buf = xmalloc (MAX_LINE_LEN);
  tokstr = xmalloc (MAX_TOK_LEN);

  dump_header ();


  indent = 0;
  dump (0, "#line %d \"%s\"", ln + 1, ifn);
  while (get_line ())
    {
      const char *directive = recognize_directive ();
      if (directive == NULL)
	{
	  dump (0, "%s", buf);
	  continue;
	}

      dump (0, "#line %d \"%s\"", oln + 1, ofn);
      if (!strcmp (directive, "specification"))
	{
	  /* Skip leading slash-star line. */
	  get_line ();
	  lex_get ();

	  parse ();

	  /* Skip trailing star-slash line. */
	  get_line ();
	}
      else if (!strcmp (directive, "headers"))
	{
	  indent = 0;

	  dump (0, "#include <stdlib.h>");
          dump_blank_line (0);

          dump (0, "#include \"data/settings.h\"");
	  dump (0, "#include \"data/variable.h\"");
	  dump (0, "#include \"language/lexer/lexer.h\"");
          dump (0, "#include \"language/lexer/subcommand-list.h\"");
	  dump (0, "#include \"language/lexer/variable-parser.h\"");
	  dump (0, "#include \"libpspp/assertion.h\"");
	  dump (0, "#include \"libpspp/cast.h\"");
	  dump (0, "#include \"libpspp/message.h\"");
	  dump (0, "#include \"libpspp/str.h\"");
	  dump_blank_line (0);

          dump (0, "#include \"gl/xalloc.h\"");
	  dump_blank_line (0);
	}
      else if (!strcmp (directive, "declarations"))
	dump_declarations ();
      else if (!strcmp (directive, "functions"))
	{
	  dump_parser (0);
	  dump_free (0);
	}
      else if (!strcmp (directive, "_functions"))
	{
	  dump_parser (1);
	  dump_free (1);
	}
      else
	error ("unknown directive `%s'", directive);
      indent = 0;
      dump (0, "#line %d \"%s\"", ln + 1, ifn);
    }

  return EXIT_SUCCESS;
}
