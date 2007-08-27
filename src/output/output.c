/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007 Free Software Foundation, Inc.

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

#include <config.h>
#include "output.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <libpspp/alloc.h>
#include <data/file-name.h>
#include "htmlP.h"
#include "intprops.h"
#include <libpspp/misc.h>
#include <data/settings.h>
#include <libpspp/str.h>
#include "error.h"

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
    struct string value;
    struct outp_defn *next, *prev;
  };

static struct outp_defn *outp_macros;
static struct outp_names *outp_configure_vec;

/* A list of driver classes. */
struct outp_driver_class_list
  {
    const struct outp_class *class;
    struct outp_driver_class_list *next;
  };

static struct outp_driver_class_list *outp_class_list;
static struct outp_driver *outp_driver_list;

char *outp_title;
char *outp_subtitle;

/* A set of OUTP_DEV_* bits indicating the devices that are
   disabled. */
static int disabled_devices;

static void destroy_driver (struct outp_driver *);
static void configure_driver (const struct substring, const struct substring,
                              const struct substring, const struct substring);

/* Add a class to the class list. */
static void
add_class (const struct outp_class *class)
{
  struct outp_driver_class_list *new_list = xmalloc (sizeof *new_list);

  new_list->class = class;

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

/* Checks that outp_configure_vec is empty, complains and clears
   it if it isn't. */
static void
check_configure_vec (void)
{
  struct outp_names *n;

  for (n = outp_configure_vec; n; n = n->next)
    if (n->source == OUTP_S_COMMAND_LINE)
      error (0, 0, _("unknown output driver `%s'"), n->name);
    else
      error (0, 0, _("output driver `%s' referenced but never defined"),
             n->name);
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
  static char buf[INT_STRLEN_BOUND (int) + 1];
  struct outp_defn *d;

  for (d = outp_macros; d; d = d->next)
    if (!strcmp (key, d->key))
      return ds_cstr (&d->value);
  if (!strcmp (key, "viewwidth"))
    {
      sprintf (buf, "%d", get_viewwidth ());
      return buf;
    }
  else if (!strcmp (key, "viewlength"))
    {
      sprintf (buf, "%d", get_viewlength ());
      return buf;
    }
  else
    return getenv (key);
}

/* Initializes global variables. */
void
outp_init (void)
{
  extern struct outp_class ascii_class;
  extern struct outp_class postscript_class;

  char def[] = "default";

  add_class (&html_class);
  add_class (&postscript_class);
  add_class (&ascii_class);

  add_name (def, &def[strlen (def)], OUTP_S_INIT_FILE);
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
      ds_destroy (&d->value);
      free (d);
    }
}

static void
init_default_drivers (void)
{
  error (0, 0, _("using default output driver configuration"));
  configure_driver (ss_cstr ("list"),
                    ss_cstr ("ascii"),
                    ss_cstr ("listing"),
                    ss_cstr ("length=66 width=79 output-file=\"pspp.list\""));
}

/* Reads the initialization file; initializes
   outp_driver_list. */
void
outp_read_devices (void)
{
  int result = 0;

  char *init_fn;

  FILE *f = NULL;
  struct string line;
  int line_number;

  init_fn = fn_search_path (fn_getenv_default ("STAT_OUTPUT_INIT_FILE",
					       "devices"),
			    fn_getenv_default ("STAT_OUTPUT_INIT_PATH",
					       config_path));

  ds_init_empty (&line);

  if (init_fn == NULL)
    {
      error (0, 0, _("cannot find output initialization file "
                     "(use `-vv' to view search path)"));
      goto exit;
    }

  f = fopen (init_fn, "r");
  if (f == NULL)
    {
      error (0, errno, _("cannot open \"%s\""), init_fn);
      goto exit;
    }

  line_number = 0;
  for (;;)
    {
      char *cp;

      if (!ds_read_config_line (&line, &line_number, f))
	{
	  if (ferror (f))
	    error (0, errno, _("reading \"%s\""), init_fn);
	  break;
	}
      for (cp = ds_cstr (&line); isspace ((unsigned char) *cp); cp++);
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
		  outp_configure_driver_line (ds_ss (&line));
		  delete_name (n);
		}
	    }
	  else
	    error_at_line (0, 0, init_fn, line_number, _("syntax error"));
	}
    }
  result = 1;

  check_configure_vec ();

exit:
  if (f && -1 == fclose (f))
    error (0, errno, _("error closing \"%s\""), init_fn);
  free (init_fn);
  ds_destroy (&line);
  delete_macros ();

  if (result)
    {
      if (outp_driver_list == NULL)
        error (0, 0, _("no active output drivers"));
    }
  else
    error (0, 0, _("error reading device definition file"));

  if (!result || outp_driver_list == NULL)
    init_default_drivers ();
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

  ds_init_cstr (&d->value, ep);
  fn_interp_vars (ds_ss (&d->value), find_defn_value, &d->value);
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
void
outp_done (void)
{
  struct outp_driver_class_list *n = outp_class_list ;
  outp_configure_clear ();
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

/* Obtains a token from S and advances its position.  Errors are
   reported against the given DRIVER_NAME.
   The token is stored in TOKEN.  Returns true if successful,
   false on syntax error.

   Caller is responsible for skipping leading spaces. */
static bool
get_option_token (struct substring *s, const char *driver_name,
                  struct string *token)
{
  int c;

  ds_clear (token);
  c = ss_get_char (s);
  if (c == EOF)
    {
      error (0, 0, _("syntax error parsing options for \"%s\" driver"),
             driver_name);
      return false;
    }
  else if (c == '\'' || c == '"')
    {
      int quote = c;

      for (;;)
        {
          c = ss_get_char (s);
          if (c == quote)
            break;
          else if (c == EOF)
            {
              error (0, 0,
                     _("reached end of options inside quoted string "
                       "parsing options for \"%s\" driver"),
                     driver_name);
              return false;
            }
          else if (c != '\\')
            ds_put_char (token, c);
          else
            {
              int out;

              c = ss_get_char (s);
              switch (c)
                {
                case '\'':
                  out = '\'';
                  break;
                case '"':
                  out = '"';
                  break;
                case '\\':
                  out = '\\';
                  break;
                case 'a':
                  out = '\a';
                  break;
                case 'b':
                  out = '\b';
                  break;
                case 'f':
                  out = '\f';
                  break;
                case 'n':
                  out = '\n';
                  break;
                case 'r':
                  out = '\r';
                  break;
                case 't':
                  out = '\t';
                  break;
                case 'v':
                  out = '\v';
                  break;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                  out = c - '0';
                  while (ss_first (*s) >= '0' && ss_first (*s) <= '7')
                    out = out * 8 + (ss_get_char (s) - '0');
                  break;
                case 'x':
                case 'X':
                  out = 0;
                  while (isxdigit (ss_first (*s)))
                    {
                      c = ss_get_char (s);
                      out *= 16;
                      if (isdigit (c))
                        out += c - '0';
                      else
                        out += tolower (c) - 'a' + 10;
                    }
                  break;
                default:
                  error (0, 0, _("syntax error in string constant "
                                 "parsing options for \"%s\" driver"),
                         driver_name);
                  return false;
                }
              ds_put_char (token, out);
            }
        }
    }
  else
    {
      for (;;)
        {
          ds_put_char (token, c);

          c = ss_first (*s);
          if (c == EOF || c == '=' || isspace (c))
            break;
          ss_advance (s, 1);
        }
    }

  return 1;
}

bool
outp_parse_options (struct substring options,
                    bool (*callback) (struct outp_driver *, const char *key,
                                      const struct string *value),
                    struct outp_driver *driver)
{
  struct string key = DS_EMPTY_INITIALIZER;
  struct string value = DS_EMPTY_INITIALIZER;
  struct substring left = options;
  bool ok = true;

  do
    {
      ss_ltrim (&left, ss_cstr (CC_SPACES));
      if (ss_is_empty (left))
        break;

      if (!get_option_token (&left, driver->name, &key))
        break;

      ss_ltrim (&left, ss_cstr (CC_SPACES));
      if (!ss_match_char (&left, '='))
	{
	  error (0, 0, _("syntax error expecting `=' "
                         "parsing options for driver \"%s\""),
                 driver->name);
	  break;
	}

      ss_ltrim (&left, ss_cstr (CC_SPACES));
      if (!get_option_token (&left, driver->name, &value))
        break;

      ok = callback (driver, ds_cstr (&key), &value);
    }
  while (ok);

  ds_destroy (&key);
  ds_destroy (&value);

  return ok;
}

/* Find the driver in outp_driver_list with name NAME. */
static struct outp_driver *
find_driver (char *name)
{
  struct outp_driver *d;

  for (d = outp_driver_list; d; d = d->next)
    if (!strcmp (d->name, name))
      return d;
  return NULL;
}

/* Adds a driver to outp_driver_list pursuant to the
   specification provided.  */
static void
configure_driver (struct substring driver_name, struct substring class_name,
                  struct substring device_type, struct substring options)
{
  struct outp_driver *d, *iter;
  struct outp_driver_class_list *c;

  struct substring token;
  size_t save_idx = 0;
  int device;

  /* Find class. */
  for (c = outp_class_list; c; c = c->next)
    if (!ss_compare (ss_cstr (c->class->name), class_name))
      break;
  if (c == NULL)
    {
      error (0, 0, _("unknown output driver class `%.*s'"),
             (int) ss_length (class_name), ss_data (class_name));
      return;
    }

  /* Parse device type. */
  device = 0;
  while (ss_tokenize (device_type, ss_cstr (CC_SPACES), &save_idx, &token))
    if (!ss_compare (token, ss_cstr ("listing")))
      device |= OUTP_DEV_LISTING;
    else if (!ss_compare (token, ss_cstr ("screen")))
      device |= OUTP_DEV_SCREEN;
    else if (!ss_compare (token, ss_cstr ("printer")))
      device |= OUTP_DEV_PRINTER;
    else
      error (0, 0, _("unknown device type `%.*s'"),
             (int) ss_length (token), ss_data (token));

  /* Open the device. */
  d = xmalloc (sizeof *d);
  d->next = d->prev = NULL;
  d->class = c->class;
  d->name = ss_xstrdup (driver_name);
  d->page_open = false;
  d->device = device;
  d->cp_x = d->cp_y = 0;
  d->ext = NULL;
  d->prc = NULL;

  /* Open driver. */
  if (!d->class->open_driver (d, options))
    {
      error (0, 0, _("cannot initialize output driver `%s' of class `%s'"),
             d->name, d->class->name);
      free (d->name);
      free (d);
      return;
    }

  /* Find like-named driver and delete. */
  iter = find_driver (d->name);
  if (iter != NULL)
    destroy_driver (iter);

  /* Add to list. */
  d->next = outp_driver_list;
  d->prev = NULL;
  if (outp_driver_list != NULL)
    outp_driver_list->prev = d;
  outp_driver_list = d;
}

/* String LINE is in format:
   DRIVERNAME:CLASSNAME:DEVICETYPE:OPTIONS
   Adds a driver to outp_driver_list pursuant to the specification
   provided.  */
void
outp_configure_driver_line (struct substring line_)
{
  struct string line = DS_EMPTY_INITIALIZER;
  struct substring tokens[4];
  size_t save_idx;
  size_t i;

  fn_interp_vars (line_, find_defn_value, &line);

  save_idx = 0;
  for (i = 0; i < 4; i++)
    {
      struct substring *token = &tokens[i];
      ds_separate (&line, ss_cstr (i < 3 ? ":" : ""), &save_idx, token);
      ss_trim (token, ss_cstr (CC_SPACES));
    }

  if (!ss_is_empty (tokens[0]) && !ss_is_empty (tokens[1]))
    configure_driver (tokens[0], tokens[1], tokens[2], tokens[3]);
  else
    error (0, 0,
           _("driver definition line missing driver name or class name"));

  ds_destroy (&line);
}

/* Destroys output driver D. */
static void
destroy_driver (struct outp_driver *d)
{
  outp_close_page (d);
  if (d->class)
    {
      struct outp_driver_class_list *c;

      d->class->close_driver (d);

      for (c = outp_class_list; c; c = c->next)
	if (c->class == d->class)
	  break;
      assert (c != NULL);
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

/* Tries to match S as one of the keywords in TAB, with
   corresponding information structure INFO.  Returns category
   code and stores subcategory in *SUBCAT on success.  Returns -1
   on failure. */
int
outp_match_keyword (const char *s, const struct outp_option *tab, int *subcat)
{
  for (; tab->keyword != NULL; tab++)
    if (!strcmp (s, tab->keyword))
      {
        *subcat = tab->subcat;
        return tab->cat;
      }
  return -1;
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
	    error (0, 0,
                   _("unit \"%s\" is unknown in dimension \"%s\""), s, dimen);
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
  error (0, 0, _("bad dimension \"%s\""), dimen);
  return 0;
}

/* Stores the dimensions in 1/72000" units of paper identified by
   SIZE, which is of form `HORZ x VERT' or `HORZ by VERT' where each
   of HORZ and VERT are dimensions, into *H and *V.  Return true on
   success. */
static bool
internal_get_paper_size (char *size, int *h, int *v)
{
  char *tail;

  while (isspace ((unsigned char) *size))
    size++;
  *h = outp_evaluate_dimension (size, &tail);
  if (tail == NULL)
    return false;
  while (isspace ((unsigned char) *tail))
    tail++;
  if (*tail == 'x')
    tail++;
  else if (*tail == 'b' && tail[1] == 'y')
    tail += 2;
  else
    {
      error (0, 0, _("`x' expected in paper size `%s'"), size);
      return false;
    }
  *v = outp_evaluate_dimension (tail, &tail);
  if (tail == NULL)
    return 0;
  while (isspace ((unsigned char) *tail))
    tail++;
  if (*tail)
    {
      error (0, 0, _("trailing garbage `%s' on paper size `%s'"), tail, size);
      return false;
    }

  return true;
}

/* Stores the dimensions, in 1/72000" units, of paper identified by
   SIZE into *H and *V.  SIZE may be a pair of dimensions of form `H x
   V', or it may be a case-insensitive paper identifier, which is
   looked up in the `papersize' configuration file.  Returns true
   on success.  May modify SIZE. */
/* Don't read further unless you've got a strong stomach. */
bool
outp_get_paper_size (char *size, int *h, int *v)
{
  struct paper_size
    {
      char *name;
      int use;
      int h, v;
    };

  FILE *f;
  char *pprsz_fn;

  struct string line;
  int line_number = 0;

  bool free_it = false;
  bool result = false;
  char *ep;

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
      error (0, 0, _("paper size name cannot be empty"));
      return 0;
    }

  ep++;
  if (*ep)
    *ep = 0;

  pprsz_fn = fn_search_path (fn_getenv_default ("STAT_OUTPUT_PAPERSIZE_FILE",
						"papersize"),
			     fn_getenv_default ("STAT_OUTPUT_INIT_PATH",
						config_path));

  ds_init_empty (&line);

  if (pprsz_fn == NULL)
    {
      error (0, 0, _("cannot find `papersize' configuration file"));
      goto exit;
    }

  f = fopen (pprsz_fn, "r");
  if (!f)
    {
      error (0, errno, _("error opening \"%s\""), pprsz_fn);
      goto exit;
    }

  for (;;)
    {
      struct substring p, name;

      if (!ds_read_config_line (&line, &line_number, f))
	{
	  if (ferror (f))
	    error (0, errno, _("error reading \"%s\""), pprsz_fn);
	  break;
	}

      p = ds_ss (&line);
      ss_ltrim (&p, ss_cstr (CC_SPACES));
      if (!ss_match_char (&p, '"') || !ss_get_until (&p, '"', &name))
	goto lex_error;
      if (ss_compare (name, ss_cstr (size)))
	continue;

      ss_ltrim (&p, ss_cstr (CC_SPACES));
      if (ss_match_char (&p, '='))
	{
          if (free_it)
            free (size);
          ss_trim (&p, ss_cstr (CC_SPACES));
          size = ss_xstrdup (p);
	  free_it = true;
	  continue;
	}
      size = ss_data (p);
      break;

    lex_error:
      error_at_line (0, 0, pprsz_fn, line_number,
                     _("syntax error in paper size definition"));
    }

  /* We found the one we want! */
  result = internal_get_paper_size (size, h, v);

exit:
  ds_destroy (&line);
  if (free_it)
    free (size);

  if (!result)
    error (0, 0, _("error reading paper size definition file"));

  return result;
}

/* If D is NULL, returns the first enabled driver if any, NULL if
   none.  Otherwise D must be the last driver returned by this
   function, in which case the next enabled driver is returned or NULL
   if that was the last. */
struct outp_driver *
outp_drivers (struct outp_driver *d)
{
  for (;;)
    {
      if (d == NULL)
	d = outp_driver_list;
      else
	d = d->next;

      if (d == NULL
	  || (d->device == 0 || (d->device & disabled_devices) != d->device))
	break;
    }

  return d;
}

/* Enables (if ENABLE is true) or disables (if ENABLE is false) the
   device(s) given in mask DEVICE. */
void
outp_enable_device (bool enable, int device)
{
  if (enable)
    disabled_devices &= ~device;
  else
    disabled_devices |= device;
}

/* Opens a page on driver D (if one is not open). */
void
outp_open_page (struct outp_driver *d)
{
  if (!d->page_open)
    {
      d->cp_x = d->cp_y = 0;

      d->page_open = true;
      if (d->class->open_page != NULL)
        d->class->open_page (d);
    }
}

/* Closes the page on driver D (if one is open). */
void
outp_close_page (struct outp_driver *d)
{
  if (d->page_open)
    {
      if (d->class->close_page != NULL)
        d->class->close_page (d);
      d->page_open = false;
    }
}

/* Ejects the page on device D, if a page is open and non-blank,
   and opens a new page.  */
void
outp_eject_page (struct outp_driver *d)
{
  if (d->page_open && d->cp_y != 0)
    outp_close_page (d);
  outp_open_page (d);
}

/* Flushes output to screen devices, so that the user can see
   output that doesn't fill up an entire page. */
void
outp_flush (struct outp_driver *d)
{
  if (d->device & OUTP_DEV_SCREEN && d->class->flush != NULL)
    {
      outp_close_page (d);
      d->class->flush (d);
    }
}

/* Returns the width of string S, in device units, when output on
   device D. */
int
outp_string_width (struct outp_driver *d, const char *s, enum outp_font font)
{
  struct outp_text text;
  int width;

  text.font = font;
  text.justification = OUTP_LEFT;
  text.string = ss_cstr (s);
  text.h = text.v = INT_MAX;
  d->class->text_metrics (d, &text, &width, NULL);

  return width;
}
