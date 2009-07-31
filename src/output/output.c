/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <errno.h>
#if HAVE_LC_PAPER
#include <langinfo.h>
#endif
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include <data/file-name.h>
#include <data/settings.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include <output/htmlP.h>
#include <output/output.h>

#include "error.h"
#include "intprops.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

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
static struct ll_list outp_driver_list = LL_INITIALIZER (outp_driver_list);

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
      sprintf (buf, "%d", settings_get_viewwidth ());
      return buf;
    }
  else if (!strcmp (key, "viewlength"))
    {
      sprintf (buf, "%d", settings_get_viewlength ());
      return buf;
    }
  else
    return getenv (key);
}

/* Initializes global variables. */
void
outp_init (void)
{
  char def[] = "default";

  add_class (&html_class);
  add_class (&postscript_class);
  add_class (&ascii_class);
#ifdef HAVE_CAIRO
  add_class (&cairo_class);
#endif
  add_class (&odt_class);

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
      if (ll_is_empty (&outp_driver_list))
        error (0, 0, _("no active output drivers"));
    }
  else
    error (0, 0, _("error reading device definition file"));

  if (!result || ll_is_empty (&outp_driver_list))
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

/* Closes all the output drivers. */
void
outp_done (void)
{
  struct outp_driver_class_list *n = outp_class_list ;
  outp_configure_clear ();
  while (!ll_is_empty (&outp_driver_list))
    {
      struct outp_driver *d = ll_data (ll_head (&outp_driver_list),
                                       struct outp_driver, node);
      destroy_driver (d);
    }

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
  int width = settings_get_viewwidth ();
  struct outp_driver_class_list *c;

  printf (_("Driver classes:\n\t"));
  width -= 8;
  for (c = outp_class_list; c; c = c->next)
    {
      if ((int) strlen (c->class->name) + 1 > width)
	{
	  printf ("\n\t");
	  width = settings_get_viewwidth () - 8;
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
outp_parse_options (const char *driver_name, struct substring options,
                    bool (*callback) (void *aux, const char *key,
                                      const struct string *value), void *aux)
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

      if (!get_option_token (&left, driver_name, &key))
        break;

      ss_ltrim (&left, ss_cstr (CC_SPACES));
      if (!ss_match_char (&left, '='))
	{
	  error (0, 0, _("syntax error expecting `=' "
                         "parsing options for driver \"%s\""),
                 driver_name);
	  break;
	}

      ss_ltrim (&left, ss_cstr (CC_SPACES));
      if (!get_option_token (&left, driver_name, &value))
        break;

      ok = callback (aux, ds_cstr (&key), &value);
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
  ll_for_each (d, struct outp_driver, node, &outp_driver_list)
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
  struct outp_driver_class_list *c;
  struct substring token;
  size_t save_idx = 0;
  char *name;
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

  /* Open driver. */
  name = ss_xstrdup (driver_name);
  if (!c->class->open_driver (name, device, options))
    error (0, 0, _("cannot initialize output driver `%s' of class `%s'"),
           name, c->class->name);
  free (name);
}

/* Allocates and returns a new outp_driver for a device with the
   given NAME and CLASS and the OUTP_DEV_* type(s) in TYPES

   This function is intended to be used by output drivers, not
   by their clients. */
struct outp_driver *
outp_allocate_driver (const struct outp_class *class,
                      const char *name, int types)
{
  struct outp_driver *d = xmalloc (sizeof *d);
  d->class = class;
  d->name = xstrdup (name);
  d->page_open = false;
  d->device = types;
  d->cp_x = d->cp_y = 0;
  d->ext = NULL;
  return d;
}

/* Frees driver D and the data that it owns directly.  The
   driver's class must already have unregistered D (if it was
   registered) and freed data private to its class.

   This function is intended to be used by output drivers, not
   by their clients. */
void
outp_free_driver (struct outp_driver *d)
{
  free (d->name);
  free (d);
}

/* Adds D to the list of drivers that will be used for output. */
void
outp_register_driver (struct outp_driver *d)
{
  struct outp_driver *victim;

  /* Find like-named driver and delete. */
  victim = find_driver (d->name);
  if (victim != NULL)
    destroy_driver (victim);

  /* Add D to list. */
  ll_push_tail (&outp_driver_list, &d->node);
}

/* Remove driver D from the list of drivers that will be used for
   output. */
void
outp_unregister_driver (struct outp_driver *d)
{
  ll_remove (&d->node);
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
  if (d->class && d->class->close_driver)
    d->class->close_driver (d);
  outp_unregister_driver (d);
  outp_free_driver (d);
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

/* Parses UNIT as a dimensional unit.  Returns the multiplicative
   factor needed to change a quantity measured in that unit into
   1/72000" units.  If UNIT is empty, it is treated as
   millimeters.  If the unit is unrecognized, returns 0. */
static double
parse_unit (const char *unit)
{
  struct unit
    {
      char name[3];
      double factor;
    };

  static const struct unit units[] =
    {
      {"pt", 72000 / 72},
      {"pc", 72000 / 72 * 12.0},
      {"in", 72000},
      {"cm", 72000 / 2.54},
      {"mm", 72000 / 25.4},
      {"", 72000 / 25.4},
    };

  const struct unit *p;

  unit += strspn (unit, CC_SPACES);
  for (p = units; p < units + sizeof units / sizeof *units; p++)
    if (!strcasecmp (unit, p->name))
      return p->factor;
  return 0.0;
}

/* Determines the size of a dimensional measurement and returns
   the size in units of 1/72000".  Units are assumed to be
   millimeters unless otherwise specified.  Returns 0 on
   error. */
int
outp_evaluate_dimension (const char *dimen)
{
  double raw, factor;
  char *tail;

  /* Number. */
  raw = strtod (dimen, &tail);
  if (raw <= 0.0)
    goto syntax_error;

  /* Unit. */
  factor = parse_unit (tail);
  if (factor == 0.0)
    goto syntax_error;

  return raw * factor;

syntax_error:
  error (0, 0, _("`%s' is not a valid length."), dimen);
  return 0;
}

/* Stores the dimensions in 1/72000" units of paper identified by
   SIZE, which is of form `HORZ x VERT [UNIT]' where HORZ and
   VERT are numbers and UNIT is an optional unit of measurement,
   into *H and *V.  Return true on success. */
static bool
parse_paper_size (const char *size, int *h, int *v)
{
  double raw_h, raw_v, factor;
  char *tail;

  /* Width. */
  raw_h = strtod (size, &tail);
  if (raw_h <= 0.0)
    return false;

  /* Delimiter. */
  tail += strspn (tail, CC_SPACES "x,");

  /* Length. */
  raw_v = strtod (tail, &tail);
  if (raw_v <= 0.0)
    return false;

  /* Unit. */
  factor = parse_unit (tail);
  if (factor == 0.0)
    return false;

  *h = raw_h * factor + .5;
  *v = raw_v * factor + .5;
  return true;
}

static bool
get_standard_paper_size (struct substring name, int *h, int *v)
{
  static const char *sizes[][2] =
    {
      {"a0", "841 x 1189 mm"},
      {"a1", "594 x 841 mm"},
      {"a2", "420 x 594 mm"},
      {"a3", "297 x 420 mm"},
      {"a4", "210 x 297 mm"},
      {"a5", "148 x 210 mm"},
      {"b5", "176 x 250 mm"},
      {"a6", "105 x 148 mm"},
      {"a7", "74 x 105 mm"},
      {"a8", "52 x 74 mm"},
      {"a9", "37 x 52 mm"},
      {"a10", "26 x 37 mm"},
      {"b0", "1000 x 1414 mm"},
      {"b1", "707 x 1000 mm"},
      {"b2", "500 x 707 mm"},
      {"b3", "353 x 500 mm"},
      {"b4", "250 x 353 mm"},
      {"letter", "612 x 792 pt"},
      {"legal", "612 x 1008 pt"},
      {"executive", "522 x 756 pt"},
      {"note", "612 x 792 pt"},
      {"11x17", "792 x 1224 pt"},
      {"tabloid", "792 x 1224 pt"},
      {"statement", "396 x 612 pt"},
      {"halfletter", "396 x 612 pt"},
      {"halfexecutive", "378 x 522 pt"},
      {"folio", "612 x 936 pt"},
      {"quarto", "610 x 780 pt"},
      {"ledger", "1224 x 792 pt"},
      {"archA", "648 x 864 pt"},
      {"archB", "864 x 1296 pt"},
      {"archC", "1296 x 1728 pt"},
      {"archD", "1728 x 2592 pt"},
      {"archE", "2592 x 3456 pt"},
      {"flsa", "612 x 936 pt"},
      {"flse", "612 x 936 pt"},
      {"csheet", "1224 x 1584 pt"},
      {"dsheet", "1584 x 2448 pt"},
      {"esheet", "2448 x 3168 pt"},
    };

  size_t i;

  for (i = 0; i < sizeof sizes / sizeof *sizes; i++)
    if (ss_equals_case (ss_cstr (sizes[i][0]), name))
      {
        bool ok = parse_paper_size (sizes[i][1], h, v);
        assert (ok);
        return ok;
      }
  error (0, 0, _("unknown paper type `%.*s'"),
         (int) ss_length (name), ss_data (name));
  return false;
}

/* Reads file FILE_NAME to find a paper size.  Stores the
   dimensions, in 1/72000" units, into *H and *V.  Returns true
   on success, false on failure. */
static bool
read_paper_conf (const char *file_name, int *h, int *v)
{
  struct string line = DS_EMPTY_INITIALIZER;
  int line_number = 0;
  FILE *file;

  file = fopen (file_name, "r");
  if (file == NULL)
    {
      error (0, errno, _("error opening \"%s\""), file_name);
      return false;
    }

  for (;;)
    {
      struct substring name;

      if (!ds_read_config_line (&line, &line_number, file))
	{
	  if (ferror (file))
	    error (0, errno, _("error reading \"%s\""), file_name);
	  break;
	}

      name = ds_ss (&line);
      ss_trim (&name, ss_cstr (CC_SPACES));
      if (!ss_is_empty (name))
        {
          bool ok = get_standard_paper_size (name, h, v);
          fclose (file);
          ds_destroy (&line);
          return ok;
        }
    }

  fclose (file);
  ds_destroy (&line);
  error (0, 0, _("paper size file \"%s\" does not state a paper size"),
         file_name);
  return false;
}

/* The user didn't specify a paper size, so let's choose a
   default based on his environment.  Stores the
   dimensions, in 1/72000" units, into *H and *V.  Returns true
   on success, false on failure. */
static bool
get_default_paper_size (int *h, int *v)
{
  /* libpaper in Debian (and other distributions?) allows the
     paper size to be specified in $PAPERSIZE or in a file
     specified in $PAPERCONF. */
  if (getenv ("PAPERSIZE") != NULL)
    return get_standard_paper_size (ss_cstr (getenv ("PAPERSIZE")), h, v);
  if (getenv ("PAPERCONF") != NULL)
    return read_paper_conf (getenv ("PAPERCONF"), h, v);

#if HAVE_LC_PAPER
  /* LC_PAPER is a non-standard glibc extension. */
  *h = (int) nl_langinfo(_NL_PAPER_WIDTH) * (72000 / 25.4);
  *v = (int) nl_langinfo(_NL_PAPER_HEIGHT) * (72000 / 25.4);
  if (*h > 0 && *v > 0)
     return true;
#endif

  /* libpaper defaults to /etc/papersize. */
  if (fn_exists ("/etc/papersize"))
    return read_paper_conf ("/etc/papersize", h, v);

  /* Can't find a default. */
  return false;
}

/* Stores the dimensions, in 1/72000" units, of paper identified
   by SIZE into *H and *V.  SIZE can be the name of a kind of
   paper ("a4", "letter", ...) or a pair of dimensions
   ("210x297", "8.5x11in", ...).  Returns true on success, false
   on failure.  On failure, *H and *V are set for A4 paper. */
bool
outp_get_paper_size (const char *size, int *h, int *v)
{
  struct substring s;
  bool ok;

  s = ss_cstr (size);
  ss_trim (&s, ss_cstr (CC_SPACES));

  if (ss_is_empty (s))
    {
      /* Treat empty string as default paper size. */
      ok = get_default_paper_size (h, v);
    }
  else if (isdigit (ss_first (s)))
    {
      /* Treat string that starts with digit as explicit size. */
      ok = parse_paper_size (size, h, v);
      if (!ok)
        error (0, 0, _("syntax error in paper size `%s'"), size);
    }
  else
    {
      /* Check against standard paper sizes. */
      ok = get_standard_paper_size (s, h, v);
    }

  /* Default to A4 on error. */
  if (!ok)
    {
      *h = 210 * (72000 / 25.4);
      *v = 297 * (72000 / 25.4);
    }
  return ok;
}

/* If D is NULL, returns the first enabled driver if any, NULL if
   none.  Otherwise D must be the last driver returned by this
   function, in which case the next enabled driver is returned or NULL
   if that was the last. */
struct outp_driver *
outp_drivers (struct outp_driver *d)
{
  do
    {
      struct ll *next;

      next = d == NULL ? ll_head (&outp_driver_list) : ll_next (&d->node);
      if (next == ll_null (&outp_driver_list))
        return NULL;

      d = ll_data (next, struct outp_driver, node);
    }
  while (d->device != 0 && (d->device & disabled_devices) == d->device);

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
