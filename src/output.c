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
#include "output.h"
#include "error.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include "alloc.h"
#include "devind.h"
#include "error.h"
#include "filename.h"
#include "htmlP.h"
#include "lexer.h"
#include "misc.h"
#include "settings.h"
#include "str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* FIXME? Should the output configuration format be changed to
   drivername:classname:devicetype:options, where devicetype is zero
   or more of screen, printer, listing? */

/* FIXME: Have the reentrancy problems been solved? */

/* Where the output driver name came from. */
enum
  {
    OUTP_S_COMMAND_LINE,	/* Specified by the user. */
    OUTP_S_INIT_FILE		/* `default' or the init file. */
  };

/* Names the output drivers to be used. */
struct outp_names
  {
    char *name;			/* Name of the output driver. */
    int source;			/* OUTP_S_* */
    struct outp_names *next, *prev;
  };

/* Defines an init file macro. */
struct outp_defn
  {
    char *key;
    char *value;
    struct outp_defn *next, *prev;
  };

static struct outp_defn *outp_macros;
static struct outp_names *outp_configure_vec;

struct outp_driver_class_list *outp_class_list;
struct outp_driver *outp_driver_list;

char *outp_title;
char *outp_subtitle;

/* A set of OUTP_DEV_* bits indicating the devices that are
   disabled. */
static int disabled_devices;

static void destroy_driver (struct outp_driver *);
static void configure_driver (char *);

#if GLOBAL_DEBUGGING
/* This mechanism attempts to catch reentrant use of outp_driver_list. */
static int iterating_driver_list;

#define reentrancy() msg (FE, _("Attempt to iterate driver list reentrantly."))
#endif

/* Add a class to the class list. */
static void
add_class (struct outp_class *class)
{
  struct outp_driver_class_list *new_list = xmalloc (sizeof *new_list);

  new_list->class = class;
  new_list->ref_count = 0;

  if (!outp_class_list)
    {
      outp_class_list = new_list;
      new_list->next = NULL;
    }
  else
    {
      new_list->next = outp_class_list;
      outp_class_list = new_list;
    }
}

/* Finds the outp_names in outp_configure_vec with name between BP and
   EP exclusive. */
static struct outp_names *
search_names (char *bp, char *ep)
{
  struct outp_names *n;

  for (n = outp_configure_vec; n; n = n->next)
    if ((int) strlen (n->name) == ep - bp && !memcmp (n->name, bp, ep - bp))
      return n;
  return NULL;
}

/* Deletes outp_names NAME from outp_configure_vec. */
static void
delete_name (struct outp_names * n)
{
  free (n->name);
  if (n->prev)
    n->prev->next = n->next;
  if (n->next)
    n->next->prev = n->prev;
  if (n == outp_configure_vec)
    outp_configure_vec = n->next;
  free (n);
}

/* Adds the name between BP and EP exclusive to list
   outp_configure_vec with source SOURCE. */
static void
add_name (char *bp, char *ep, int source)
{
  struct outp_names *n = xmalloc (sizeof *n);
  n->name = xmalloc (ep - bp + 1);
  memcpy (n->name, bp, ep - bp);
  n->name[ep - bp] = 0;
  n->source = source;
  n->next = outp_configure_vec;
  n->prev = NULL;
  if (outp_configure_vec)
    outp_configure_vec->prev = n;
  outp_configure_vec = n;
}

/* Checks that outp_configure_vec is empty, bitches & clears it if it
   isn't. */
static void
check_configure_vec (void)
{
  struct outp_names *n;

  for (n = outp_configure_vec; n; n = n->next)
    if (n->source == OUTP_S_COMMAND_LINE)
      msg (ME, _("Unknown output driver `%s'."), n->name);
    else
      msg (IE, _("Output driver `%s' referenced but never defined."), n->name);
  outp_configure_clear ();
}

/* Searches outp_configure_vec for the name between BP and EP
   exclusive.  If found, it is deleted, then replaced by the names
   given in EP+1, if any. */
static void
expand_name (char *bp, char *ep)
{
  struct outp_names *n = search_names (bp, ep);
  if (!n)
    return;
  delete_name (n);

  bp = ep + 1;
  for (;;)
    {
      while (isspace ((unsigned char) *bp))
	bp++;
      ep = bp;
      while (*ep && !isspace ((unsigned char) *ep))
	ep++;
      if (bp == ep)
	return;
      if (!search_names (bp, ep))
	add_name (bp, ep, OUTP_S_INIT_FILE);
      bp = ep;
    }
}

/* Looks for a macro with key KEY, and returns the corresponding value
   if found, or NULL if not. */
static const char *
find_defn_value (const char *key)
{
  static char buf[INT_DIGITS + 1];
  struct outp_defn *d;

  for (d = outp_macros; d; d = d->next)
    if (!strcmp (key, d->key))
      return d->value;
  if (!strcmp (key, "viewwidth"))
    {
      sprintf (buf, "%d", get_viewwidth());
      return buf;
    }
  else if (!strcmp (key, "viewlength"))
    {
      sprintf (buf, "%d", get_viewlength());
      return buf;
    }
  else
    return getenv (key);
}

/* Initializes global variables. */
int
outp_init (void)
{
  extern struct outp_class ascii_class;
#if !NO_POSTSCRIPT
  extern struct outp_class postscript_class;
  extern struct outp_class epsf_class;
#endif
  extern struct outp_class html_class;
  extern struct outp_class devind_class;

  char def[] = "default";

#if !NO_HTML
  add_class (&html_class);
#endif
#if !NO_POSTSCRIPT
  add_class (&epsf_class);
  add_class (&postscript_class);
#endif
  add_class (&devind_class);
  add_class (&ascii_class);

  add_name (def, &def[strlen (def)], OUTP_S_INIT_FILE);

  return 1;
}

/* Deletes all the output macros. */
static void
delete_macros (void)
{
  struct outp_defn *d, *next;

  for (d = outp_macros; d; d = next)
    {
      next = d->next;
      free (d->key);
      free (d->value);
      free (d);
    }
}

/* Reads the initialization file; initializes outp_driver_list. */
int
outp_read_devices (void)
{
  int result = 0;

  char *init_fn;

  FILE *f = NULL;
  struct string line;
  struct file_locator where;

#if GLOBAL_DEBUGGING
  if (iterating_driver_list)
    reentrancy ();
#endif

  init_fn = fn_search_path (fn_getenv_default ("STAT_OUTPUT_INIT_FILE",
					       "devices"),
			    fn_getenv_default ("STAT_OUTPUT_INIT_PATH",
					       config_path),
			    NULL);
  where.filename = init_fn;
  where.line_number = 0;
  err_push_file_locator (&where);

  ds_init (&line, 128);

  if (init_fn == NULL)
    {
      msg (IE, _("Cannot find output initialization file.  "
                 "Use `-vvvvv' to view search path."));
      goto exit;
    }

  msg (VM (1), _("%s: Opening device description file..."), init_fn);
  f = fopen (init_fn, "r");
  if (f == NULL)
    {
      msg (IE, _("Opening %s: %s."), init_fn, strerror (errno));
      goto exit;
    }

  for (;;)
    {
      char *cp;

      if (!ds_get_config_line (f, &line, &where))
	{
	  if (ferror (f))
	    msg (ME, _("Reading %s: %s."), init_fn, strerror (errno));
	  break;
	}
      for (cp = ds_c_str (&line); isspace ((unsigned char) *cp); cp++);
      if (!strncmp ("define", cp, 6) && isspace ((unsigned char) cp[6]))
	outp_configure_macro (&cp[7]);
      else if (*cp)
	{
	  char *ep;
	  for (ep = cp; *ep && *ep != ':' && *ep != '='; ep++);
	  if (*ep == '=')
	    expand_name (cp, ep);
	  else if (*ep == ':')
	    {
	      struct outp_names *n = search_names (cp, ep);
	      if (n)
		{
		  configure_driver (cp);
		  delete_name (n);
		}
	    }
	  else
	    msg (IS, _("Syntax error."));
	}
    }
  result = 1;

  check_configure_vec ();

exit:
  err_pop_file_locator (&where);
  if (f && -1 == fclose (f))
    msg (MW, _("Closing %s: %s."), init_fn, strerror (errno));
  free (init_fn);
  ds_destroy (&line);
  delete_macros ();
  if (outp_driver_list == NULL)
    msg (MW, _("No output drivers are active."));

  if (result)
    msg (VM (2), _("Device definition file read successfully."));
  else
    msg (VM (1), _("Error reading device definition file."));
  return result;
}

/* Clear the list of drivers to configure. */
void
outp_configure_clear (void)
{
  struct outp_names *n, *next;

  for (n = outp_configure_vec; n; n = next)
    {
      next = n->next;
      free (n->name);
      free (n);
    }
  outp_configure_vec = NULL;
}

/* Adds the name BP to the list of drivers to configure into
   outp_driver_list. */
void
outp_configure_add (char *bp)
{
  char *ep = &bp[strlen (bp)];
  if (!search_names (bp, ep))
    add_name (bp, ep, OUTP_S_COMMAND_LINE);
}

/* Defines one configuration macro based on the text in BP, which
   should be of the form `KEY=VALUE'. */
void
outp_configure_macro (char *bp)
{
  struct outp_defn *d;
  char *ep;

  while (isspace ((unsigned char) *bp))
    bp++;
  ep = bp;
  while (*ep && !isspace ((unsigned char) *ep) && *ep != '=')
    ep++;

  d = xmalloc (sizeof *d);
  d->key = xmalloc (ep - bp + 1);
  memcpy (d->key, bp, ep - bp);
  d->key[ep - bp] = 0;

  /* Earlier definitions for a particular KEY override later ones. */
  if (find_defn_value (d->key))
    {
      free (d->key);
      free (d);
      return;
    }
  
  if (*ep == '=')
    ep++;
  while (isspace ((unsigned char) *ep))
    ep++;
  d->value = fn_interp_vars (ep, find_defn_value);
  d->next = outp_macros;
  d->prev = NULL;
  if (outp_macros)
    outp_macros->prev = d;
  outp_macros = d;
}

/* Destroys all the drivers in driver list *DL and sets *DL to
   NULL. */
static void
destroy_list (struct outp_driver ** dl)
{
  struct outp_driver *d, *next;

  for (d = *dl; d; d = next)
    {
      destroy_driver (d);
      next = d->next;
      free (d);
    }
  *dl = NULL;
}

/* Closes all the output drivers. */
int
outp_done (void)
{
  struct outp_driver_class_list *n = outp_class_list ; 
#if GLOBAL_DEBUGGING
  if (iterating_driver_list)
    reentrancy ();
#endif
  destroy_list (&outp_driver_list);

  while (n) 
    {
      struct outp_driver_class_list *next = n->next;
      free(n);
      n = next;
    }
  outp_class_list = NULL;

  free (outp_title);
  outp_title = NULL;
  
  free (outp_subtitle);
  outp_subtitle = NULL;

  return 1;
}

/* Display on stdout a list of all registered driver classes. */
void
outp_list_classes (void)
{
  int width = get_viewwidth();
  struct outp_driver_class_list *c;

  printf (_("Driver classes:\n\t"));
  width -= 8;
  for (c = outp_class_list; c; c = c->next)
    {
      if ((int) strlen (c->class->name) + 1 > width)
	{
	  printf ("\n\t");
	  width = get_viewwidth() - 8;
	}
      else
	putc (' ', stdout);
      fputs (c->class->name, stdout);
    }
  putc('\n', stdout);
}

static int op_token;		/* `=', 'a', 0. */
static struct string op_tokstr;
static char *prog;

/* Parses a token from prog into op_token, op_tokstr.  Sets op_token
   to '=' on an equals sign, to 'a' on a string or identifier token,
   or to 0 at end of line.  Returns the new op_token. */
static int
tokener (void)
{
  if (op_token == 0)
    {
      msg (IS, _("Syntax error."));
      return 0;
    }

  while (isspace ((unsigned char) *prog))
    prog++;
  if (!*prog)
    {
      op_token = 0;
      return 0;
    }

  if (*prog == '=')
    op_token = *prog++;
  else
    {
      ds_clear (&op_tokstr);

      if (*prog == '\'' || *prog == '"')
	{
	  int quote = *prog++;

	  while (*prog && *prog != quote)
	    {
	      if (*prog != '\\')
		ds_putc (&op_tokstr, *prog++);
	      else
		{
		  int c;
		  
		  prog++;
		  assert ((int) *prog);	/* How could a line end in `\'? */
		  switch (*prog++)
		    {
		    case '\'':
		      c = '\'';
		      break;
		    case '"':
		      c = '"';
		      break;
		    case '?':
		      c = '?';
		      break;
		    case '\\':
		      c = '\\';
		      break;
		    case '}':
		      c = '}';
		      break;
		    case 'a':
		      c = '\a';
		      break;
		    case 'b':
		      c = '\b';
		      break;
		    case 'f':
		      c = '\f';
		      break;
		    case 'n':
		      c = '\n';
		      break;
		    case 'r':
		      c = '\r';
		      break;
		    case 't':
		      c = '\t';
		      break;
		    case 'v':
		      c = '\v';
		      break;
		    case '0':
		    case '1':
		    case '2':
		    case '3':
		    case '4':
		    case '5':
		    case '6':
		    case '7':
		      {
			c = prog[-1] - '0';
			while (*prog >= '0' && *prog <= '7')
			  c = c * 8 + *prog++ - '0';
		      }
		      break;
		    case 'x':
		    case 'X':
		      {
			c = 0;
			while (isxdigit ((unsigned char) *prog))
			  {
			    c *= 16;
			    if (isdigit ((unsigned char) *prog))
			      c += *prog - '0';
			    else
			      c += (tolower ((unsigned char) (*prog))
				    - 'a' + 10);
			    prog++;
			  }
		      }
		      break;
		    default:
		      msg (IS, _("Syntax error in string constant."));
                      continue;
		    }
		  ds_putc (&op_tokstr, (unsigned char) c);
		}
	    }
	  prog++;
	}
      else
	while (*prog && !isspace ((unsigned char) *prog) && *prog != '=')
	  ds_putc (&op_tokstr, *prog++);
      op_token = 'a';
    }

  return 1;
}

/* Applies the user-specified options in string S to output driver D
   (at configuration time). */
static void
parse_options (char *s, struct outp_driver * d)
{
  prog = s;
  op_token = -1;

  ds_init (&op_tokstr, 64);
  while (tokener ())
    {
      char key[65];

      if (op_token != 'a')
	{
	  msg (IS, _("Syntax error in options."));
	  break;
	}

      ds_truncate (&op_tokstr, 64);
      strcpy (key, ds_c_str (&op_tokstr));

      tokener ();
      if (op_token != '=')
	{
	  msg (IS, _("Syntax error in options (`=' expected)."));
	  break;
	}

      tokener ();
      if (op_token != 'a')
	{
	  msg (IS, _("Syntax error in options (value expected after `=')."));
	  break;
	}
      d->class->option (d, key, &op_tokstr);
    }
  ds_destroy (&op_tokstr);
}

/* Find the driver in outp_driver_list with name NAME. */
static struct outp_driver *
find_driver (char *name)
{
  struct outp_driver *d;

#if GLOBAL_DEBUGGING
  if (iterating_driver_list)
    reentrancy ();
#endif
  for (d = outp_driver_list; d; d = d->next)
    if (!strcmp (d->name, name))
      return d;
  return NULL;
}

/* Tokenize string S into colon-separated fields, removing leading and
   trailing whitespace on tokens.  Returns a pointer to the
   null-terminated token, which is formed by setting a NUL character
   into the string.  After the first call, subsequent calls should set
   S to NULL.  CP should be consistent across calls.  Returns NULL
   after all fields have been used up.

   FIXME: Should ignore colons inside double quotes. */
static char *
colon_tokenize (char *s, char **cp)
{
  char *token;
  
  if (!s)
    {
      s = *cp;
      if (*s == 0)
	return NULL;
    }
  token = s += strspn (s, " \t\v\r");
  *cp = strchr (s, ':');
  if (*cp == NULL)
    s = *cp = strchr (s, 0);
  else
    s = (*cp)++;
  while (s > token && strchr (" \t\v\r", s[-1]))
    s--;
  *s = 0;
  return token;
}

/* String S is in format:
   DRIVERNAME:CLASSNAME:DEVICETYPE:OPTIONS
   Adds a driver to outp_driver_list pursuant to the specification
   provided.  */
static void
configure_driver (char *s)
{
  char *token, *cp;
  struct outp_driver *d = NULL, *iter;
  struct outp_driver_class_list *c = NULL;

  s = fn_interp_vars (s, find_defn_value);

  /* Driver name. */
  token = colon_tokenize (s, &cp);
  if (!token)
    {
      msg (IS, _("Driver name expected."));
      goto error;
    }

  d = xmalloc (sizeof *d);

  d->class = NULL;
  d->name = xstrdup (token);
  d->driver_open = 0;
  d->page_open = 0;

  d->next = d->prev = NULL;

  d->device = OUTP_DEV_NONE;
  
  d->ext = NULL;

  /* Class name. */
  token = colon_tokenize (NULL, &cp);
  if (!token)
    {
      msg (IS, _("Class name expected."));
      goto error;
    }

  for (c = outp_class_list; c; c = c->next)
    if (!strcmp (c->class->name, token))
      break;
  if (!c)
    {
      msg (IS, _("Unknown output driver class `%s'."), token);
      goto error;
    }
  
  d->class = c->class;
  if (!c->ref_count && !d->class->open_global (d->class))
    {
      msg (IS, _("Can't initialize output driver class `%s'."),
	   d->class->name);
      goto error;
    }
  c->ref_count++;
  if (!d->class->preopen_driver (d))
    {
      msg (IS, _("Can't initialize output driver `%s' of class `%s'."),
	   d->name, d->class->name);
      goto error;
    }

  /* Device types. */
  token = colon_tokenize (NULL, &cp);
  if (token)
    {
      char *sp, *type;

      for (type = strtok_r (token, " \t\r\v", &sp); type;
	   type = strtok_r (NULL, " \t\r\v", &sp))
	{
	  if (!strcmp (type, "listing"))
	    d->device |= OUTP_DEV_LISTING;
	  else if (!strcmp (type, "screen"))
	    d->device |= OUTP_DEV_SCREEN;
	  else if (!strcmp (type, "printer"))
	    d->device |= OUTP_DEV_PRINTER;
	  else
	    {
	      msg (IS, _("Unknown device type `%s'."), type);
	      goto error;
	    }
	}
    }
  
  /* Options. */
  token = colon_tokenize (NULL, &cp);
  if (token)
    parse_options (token, d);
  if (!d->class->postopen_driver (d))
    {
      msg (IS, _("Can't complete initialization of output driver `%s' of "
	   "class `%s'."), d->name, d->class->name);
      goto error;
    }

  /* Find like-named driver and delete. */
  iter = find_driver (d->name);
  if (iter)
    destroy_driver (iter);

  /* Add to list. */
  d->next = outp_driver_list;
  d->prev = NULL;
  if (outp_driver_list)
    outp_driver_list->prev = d;
  outp_driver_list = d;
  goto exit;

error:
  if (d)
    destroy_driver (d);
exit:
  free (s);
}

/* Destroys output driver D. */
static void
destroy_driver (struct outp_driver *d)
{
  if (d->page_open)
    d->class->close_page (d);
  if (d->class)
    {
      struct outp_driver_class_list *c;

      if (d->driver_open)
	d->class->close_driver (d);

      for (c = outp_class_list; c; c = c->next)
	if (c->class == d->class)
	  break;
      assert (c != NULL);
      
      c->ref_count--;
      if (c->ref_count == 0)
	{
	  if (!d->class->close_global (d->class))
	    msg (IS, _("Can't deinitialize output driver class `%s'."),
		 d->class->name);
	}
    }
  free (d->name);

  /* Remove this driver from the global driver list. */
  if (d->prev)
    d->prev->next = d->next;
  if (d->next)
    d->next->prev = d->prev;
  if (d == outp_driver_list)
    outp_driver_list = d->next;
}

static int
option_cmp (const void *a, const void *b)
{
  const struct outp_option *o1 = a;
  const struct outp_option *o2 = b;
  return strcmp (o1->keyword, o2->keyword);
}

/* Tries to match S as one of the keywords in TAB, with corresponding
   information structure INFO.  Returns category code or 0 on failure;
   if category code is negative then stores subcategory in *SUBCAT. */
int
outp_match_keyword (const char *s, struct outp_option *tab,
		    struct outp_option_info *info, int *subcat)
{
  char *cp;
  struct outp_option *oip;

  /* Form hash table. */
  if (NULL == info->initial)
    {
      /* Count items. */
      int count, i;
      char s[256], *cp;
      struct outp_option *ptr[255], **oip;

      for (count = 0; tab[count].keyword[0]; count++)
	;

      /* Sort items. */
      qsort (tab, count, sizeof *tab, option_cmp);

      cp = s;
      oip = ptr;
      *cp = tab[0].keyword[0];
      *oip++ = &tab[0];
      for (i = 0; i < count; i++)
	if (tab[i].keyword[0] != *cp)
	  {
	    *++cp = tab[i].keyword[0];
	    *oip++ = &tab[i];
	  }
      *++cp = 0;

      info->initial = xstrdup (s);
      info->options = xnmalloc (cp - s, sizeof *info->options);
      memcpy (info->options, ptr, sizeof *info->options * (cp - s));
    }

  cp = info->initial;
  oip = *info->options;

  if (s[0] == 0)
    return 0;
  cp = strchr (info->initial, s[0]);
  if (!cp)
    return 0;
#if 0
  printf (_("Trying to find keyword `%s'...\n"), s);
#endif
  oip = info->options[cp - info->initial];
  while (oip->keyword[0] == s[0])
    {
#if 0
      printf ("- %s\n", oip->keyword);
#endif
      if (!strcmp (s, oip->keyword))
	{
	  if (oip->cat < 0)
	    *subcat = oip->subcat;
	  return oip->cat;
	}
      oip++;
    }

  return 0;
}

/* Encapsulate two characters in a single int. */
#define TWO_CHARS(A, B)				\
	((A) + ((B)<<8))

/* Determines the size of a dimensional measurement and returns the
   size in units of 1/72000".  Units if not specified explicitly are
   inches for values under 50, millimeters otherwise.  Returns 0,
   stores NULL to *TAIL on error; otherwise returns dimension, stores
   address of next */
int
outp_evaluate_dimension (char *dimen, char **tail)
{
  char *s = dimen;
  char *ptail;
  double value;

  value = strtod (s, &ptail);
  if (ptail == s)
    goto lossage;
  if (*ptail == '-')
    {
      double b, c;
      s = &ptail[1];
      b = strtod (s, &ptail);
      if (b <= 0.0 || ptail == s)
	goto lossage;
      if (*ptail != '/')
	goto lossage;
      s = &ptail[1];
      c = strtod (s, &ptail);
      if (c <= 0.0 || ptail == s)
	goto lossage;
      s = ptail;
      if (c == 0.0)
	goto lossage;
      if (value > 0)
	value += b / c;
      else
	value -= b / c;
    }
  else if (*ptail == '/')
    {
      double b;
      s = &ptail[1];
      b = strtod (s, &ptail);
      if (b <= 0.0 || ptail == s)
	goto lossage;
      s = ptail;
      value /= b;
    }
  else
    s = ptail;
  if (*s == 0 || isspace ((unsigned char) *s))
    {
      if (value < 50.0)
	value *= 72000;
      else
	value *= 72000 / 25.4;
    }
  else
    {
      double factor;

      /* Standard TeX units are supported. */
      if (*s == '"')
	factor = 72000, s++;
      else
	switch (TWO_CHARS (s[0], s[1]))
	  {
	  case TWO_CHARS ('p', 't'):
	    factor = 72000 / 72.27;
	    break;
	  case TWO_CHARS ('p', 'c'):
	    factor = 72000 / 72.27 * 12.0;
	    break;
	  case TWO_CHARS ('i', 'n'):
	    factor = 72000;
	    break;
	  case TWO_CHARS ('b', 'p'):
	    factor = 72000 / 72.0;
	    break;
	  case TWO_CHARS ('c', 'm'):
	    factor = 72000 / 2.54;
	    break;
	  case TWO_CHARS ('m', 'm'):
	    factor = 72000 / 25.4;
	    break;
	  case TWO_CHARS ('d', 'd'):
	    factor = 72000 / 72.27 * 1.0700086;
	    break;
	  case TWO_CHARS ('c', 'c'):
	    factor = 72000 / 72.27 * 12.840104;
	    break;
	  case TWO_CHARS ('s', 'p'):
	    factor = 72000 / 72.27 / 65536.0;
	    break;
	  default:
	    msg (SE, _("Unit \"%s\" is unknown in dimension \"%s\"."), s, dimen);
	    *tail = NULL;
	    return 0;
	  }
      ptail += 2;
      value *= factor;
    }
  if (value <= 0.0)
    goto lossage;
  if (tail)
    *tail = ptail;
  return value + 0.5;

lossage:
  *tail = NULL;
  msg (SE, _("Bad dimension \"%s\"."), dimen);
  return 0;
}

/* Stores the dimensions in 1/72000" units of paper identified by
   SIZE, which is of form `HORZ x VERT' or `HORZ by VERT' where each
   of HORZ and VERT are dimensions, into *H and *V.  Return nonzero on
   success. */
static int
internal_get_paper_size (char *size, int *h, int *v)
{
  char *tail;

  while (isspace ((unsigned char) *size))
    size++;
  *h = outp_evaluate_dimension (size, &tail);
  if (tail == NULL)
    return 0;
  while (isspace ((unsigned char) *tail))
    tail++;
  if (*tail == 'x')
    tail++;
  else if (*tail == 'b' && tail[1] == 'y')
    tail += 2;
  else
    {
      msg (SE, _("`x' expected in paper size `%s'."), size);
      return 0;
    }
  *v = outp_evaluate_dimension (tail, &tail);
  if (tail == NULL)
    return 0;
  while (isspace ((unsigned char) *tail))
    tail++;
  if (*tail)
    {
      msg (SE, _("Trailing garbage `%s' on paper size `%s'."), tail, size);
      return 0;
    }
  
  return 1;
}

/* Stores the dimensions, in 1/72000" units, of paper identified by
   SIZE into *H and *V.  SIZE may be a pair of dimensions of form `H x
   V', or it may be a case-insensitive paper identifier, which is
   looked up in the `papersize' configuration file.  Returns nonzero
   on success.  May modify SIZE. */
/* Don't read further unless you've got a strong stomach. */
int
outp_get_paper_size (char *size, int *h, int *v)
{
  struct paper_size
    {
      char *name;
      int use;
      int h, v;
    };

  static struct paper_size cache[4];
  static int use;

  FILE *f;
  char *pprsz_fn;

  struct string line;
  struct file_locator where;

  int free_it = 0;
  int result = 0;
  int min_value, min_index;
  char *ep;
  int i;

  while (isspace ((unsigned char) *size))
    size++;
  if (isdigit ((unsigned char) *size))
    return internal_get_paper_size (size, h, v);
  ep = size;
  while (*ep)
    ep++;
  while (isspace ((unsigned char) *ep) && ep >= size)
    ep--;
  if (ep == size)
    {
      msg (SE, _("Paper size name must not be empty."));
      return 0;
    }
  
  ep++;
  if (*ep)
    *ep = 0;

  use++;
  for (i = 0; i < 4; i++)
    if (cache[i].name != NULL && !strcasecmp (cache[i].name, size))
      {
	*h = cache[i].h;
	*v = cache[i].v;
	cache[i].use = use;
	return 1;
      }

  pprsz_fn = fn_search_path (fn_getenv_default ("STAT_OUTPUT_PAPERSIZE_FILE",
						"papersize"),
			     fn_getenv_default ("STAT_OUTPUT_INIT_PATH",
						config_path),
			     NULL);

  where.filename = pprsz_fn;
  where.line_number = 0;
  err_push_file_locator (&where);
  ds_init (&line, 128);

  if (pprsz_fn == NULL)
    {
      msg (IE, _("Cannot find `papersize' configuration file."));
      goto exit;
    }

  msg (VM (1), _("%s: Opening paper size definition file..."), pprsz_fn);
  f = fopen (pprsz_fn, "r");
  if (!f)
    {
      msg (IE, _("Opening %s: %s."), pprsz_fn, strerror (errno));
      goto exit;
    }

  for (;;)
    {
      char *cp, *bp, *ep;

      if (!ds_get_config_line (f, &line, &where))
	{
	  if (ferror (f))
	    msg (ME, _("Reading %s: %s."), pprsz_fn, strerror (errno));
	  break;
	}
      for (cp = ds_c_str (&line); isspace ((unsigned char) *cp); cp++);
      if (*cp == 0)
	continue;
      if (*cp != '"')
	goto lex_error;
      for (bp = ep = cp + 1; *ep && *ep != '"'; ep++);
      if (!*ep)
	goto lex_error;
      *ep = 0;
      if (0 != strcasecmp (bp, size))
	continue;

      for (cp = ep + 1; isspace ((unsigned char) *cp); cp++);
      if (*cp == '=')
	{
	  size = xmalloc (ep - bp + 1);
	  strcpy (size, bp);
	  free_it = 1;
	  continue;
	}
      size = &ep[1];
      break;

    lex_error:
      msg (IE, _("Syntax error in paper size definition."));
    }

  /* We found the one we want! */
  result = internal_get_paper_size (size, h, v);
  if (result)
    {
      min_value = cache[0].use;
      min_index = 0;
      for (i = 1; i < 4; i++)
	if (cache[0].use < min_value)
	  {
	    min_value = cache[i].use;
	    min_index = i;
	  }
      free (cache[min_index].name);
      cache[min_index].name = xstrdup (size);
      cache[min_index].use = use;
      cache[min_index].h = *h;
      cache[min_index].v = *v;
    }

exit:
  err_pop_file_locator (&where);
  ds_destroy (&line);
  if (free_it)
    free (size);

  if (result)
    msg (VM (2), _("Paper size definition file read successfully."));
  else
    msg (VM (1), _("Error reading paper size definition file."));
  
  return result;
}

/* If D is NULL, returns the first enabled driver if any, NULL if
   none.  Otherwise D must be the last driver returned by this
   function, in which case the next enabled driver is returned or NULL
   if that was the last. */
struct outp_driver *
outp_drivers (struct outp_driver *d)
{
#if GLOBAL_DEBUGGING
  struct outp_driver *orig_d = d;
#endif

  for (;;)
    {
      if (d == NULL)
	d = outp_driver_list;
      else
	d = d->next;

      if (d == NULL
	  || (d->driver_open
	      && (d->device == 0
		  || (d->device & disabled_devices) != d->device)))
	break;
    }

#if GLOBAL_DEBUGGING
  if (d && !orig_d)
    {
      if (iterating_driver_list++)
	reentrancy ();
    }
  else if (orig_d && !d)
    {
      assert (iterating_driver_list == 1);
      iterating_driver_list = 0;
    }
#endif

  return d;
}

/* Enables (if ENABLE is nonzero) or disables (if ENABLE is zero) the
   device(s) given in mask DEVICE. */
void
outp_enable_device (int enable, int device)
{
  if (enable)
    disabled_devices &= ~device;
  else
    disabled_devices |= device;
}

/* Ejects the paper on device D, if the page is not blank. */
int
outp_eject_page (struct outp_driver *d)
{
  if (d->page_open == 0)
    return 1;
  
  if (d->cp_y != 0)
    {
      d->cp_x = d->cp_y = 0;

      if (d->class->close_page (d) == 0)
	msg (ME, _("Error closing page on %s device of %s class."),
	     d->name, d->class->name);
      if (d->class->open_page (d) == 0)
	{
	  msg (ME, _("Error opening page on %s device of %s class."),
	       d->name, d->class->name);
	  return 0;
	}
    }
  return 1;
}

/* Returns the width of string S, in device units, when output on
   device D. */
int
outp_string_width (struct outp_driver *d, const char *s)
{
  struct outp_text text;

  text.options = OUTP_T_JUST_LEFT;
  ls_init (&text.s, (char *) s, strlen (s));
  d->class->text_metrics (d, &text);

  return text.h;
}
