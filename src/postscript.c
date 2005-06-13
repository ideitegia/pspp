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

/*this #if encloses the remainder of the file. */
#if !NO_POSTSCRIPT

#include <ctype.h>
#include "error.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#include "alloc.h"
#include "bitvector.h"
#include "error.h"
#include "filename.h"
#include "font.h"
#include "getline.h"
#include "hash.h"
#include "main.h"
#include "misc.h"
#include "misc.h"
#include "output.h"
#include "som.h"
#include "version.h"

/* FIXMEs:

   optimize-text-size not implemented.
   
   Line buffering is the only possibility; page buffering should also
   be possible.

   max-fonts-simult
   
   Should add a field to give a file that has a list of fonts
   typically used.
   
   Should add an option that tells the driver it can emit %%Include:'s.
   
   Should have auto-encode=true stream-edit or whatever to allow
   addition to list of encodings.
   
   Should align fonts of different sizes along their baselines (see
   text()).  */

/* PostScript driver options: (defaults listed first)

   output-file="pspp.ps"
   color=yes|no
   data=clean7bit|clean8bit|binary
   line-ends=lf|crlf

   paper-size=letter (see "papersize" file)
   orientation=portrait|landscape
   headers=on|off
   
   left-margin=0.5in
   right-margin=0.5in
   top-margin=0.5in
   bottom-margin=0.5in

   font-dir=devps
   prologue-file=ps-prologue
   device-file=DESC
   encoding-file=ps-encodings
   auto-encode=true|false

   prop-font-family=T
   fixed-font-family=C
   font-size=10000

   line-style=thick|double
   line-gutter=0.5pt
   line-spacing=0.5pt
   line-width=0.5pt
   line-width-thick=1pt

   optimize-text-size=1|0|2
   optimize-line-size=1|0
   max-fonts-simult=0     Max # of fonts in printer memory at once (0=infinite)
 */

/* The number of `psus' (PostScript driver UnitS) per inch.  Although
   this is a #define, the value is expected never to change.  If it
   does, review all uses.  */
#define PSUS 72000

/* Magic numbers for PostScript and EPSF drivers. */
enum
  {
    MAGIC_PS,
    MAGIC_EPSF
  };

/* Orientations. */
enum
  {
    OTN_PORTRAIT,		/* Portrait. */
    OTN_LANDSCAPE		/* Landscape. */
  };

/* Output options. */
enum
  {
    OPO_MIRROR_HORZ = 001,	/* 1=Mirror across a horizontal axis. */
    OPO_MIRROR_VERT = 002,	/* 1=Mirror across a vertical axis. */
    OPO_ROTATE_180 = 004,	/* 1=Rotate the page 180 degrees. */
    OPO_COLOR = 010,		/* 1=Enable color. */
    OPO_HEADERS = 020,		/* 1=Draw headers at top of page. */
    OPO_AUTO_ENCODE = 040,	/* 1=Add encodings semi-intelligently. */
    OPO_DOUBLE_LINE = 0100	/* 1=Double lines instead of thick lines. */
  };

/* Data allowed in output. */
enum
  {
    ODA_CLEAN7BIT,		/* 0x09, 0x0a, 0x0d, 0x1b...0x7e */
    ODA_CLEAN8BIT,		/* 0x09, 0x0a, 0x0d, 0x1b...0xff */
    ODA_BINARY,			/* 0x00...0xff */
    ODA_COUNT
  };

/* Types of lines for purpose of caching. */
enum
  {
    horz,			/* Single horizontal. */
    dbl_horz,			/* Double horizontal. */
    spl_horz,			/* Special horizontal. */
    vert,			/* Single vertical. */
    dbl_vert,			/* Double vertical. */
    spl_vert,			/* Special vertical. */
    n_line_types
  };

/* Cached line. */
struct line_form
  {
    int ind;			/* Independent var.  Don't reorder. */
    int mdep;			/* Maximum number of dependent var pairs. */
    int ndep;			/* Current number of dependent var pairs. */
    int dep[1][2];		/* Dependent var pairs. */
  };

/* Contents of ps_driver_ext.loaded. */
struct font_entry
  {
    char *dit;			/* Font Groff name. */
    struct font_desc *font;	/* Font descriptor. */
  };

/* Combines a font with a font size for benefit of generated code. */
struct ps_font_combo
  {
    struct font_entry *font;	/* Font. */
    int size;			/* Font size. */
    int index;			/* PostScript index. */
  };

/* A font encoding. */
struct ps_encoding
  {
    char *filename;		/* Normalized filename of this encoding. */
    int index;			/* Index value. */
  };

/* PostScript output driver extension record. */
struct ps_driver_ext
  {
    /* User parameters. */
    int orientation;		/* OTN_PORTRAIT or OTN_LANDSCAPE. */
    int output_options;		/* OPO_*. */
    int data;			/* ODA_*. */

    int left_margin;		/* Left margin in psus. */
    int right_margin;		/* Right margin in psus. */
    int top_margin;		/* Top margin in psus. */
    int bottom_margin;		/* Bottom margin in psus. */

    char eol[3];		/* End of line--CR, LF, or CRLF. */
    
    char *font_dir;		/* Font directory relative to font path. */
    char *prologue_fn;		/* Prologue's filename relative to font dir. */
    char *desc_fn;		/* DESC filename relative to font dir. */
    char *encoding_fn;		/* Encoding's filename relative to font dir. */

    char *prop_family;		/* Default proportional font family. */
    char *fixed_family;		/* Default fixed-pitch font family. */
    int font_size;		/* Default font size (psus). */

    int line_gutter;		/* Space around lines. */
    int line_space;		/* Space between lines. */
    int line_width;		/* Width of lines. */
    int line_width_thick;	/* Width of thick lines. */

    int text_opt;		/* Text optimization level. */
    int line_opt;		/* Line optimization level. */
    int max_fonts;		/* Max # of simultaneous fonts (0=infinite). */

    /* Internal state. */
    struct file_ext file;	/* Output file. */
    int page_number;		/* Current page number. */
    int file_page_number;	/* Page number in this file. */
    int w, l;			/* Paper size. */
    struct hsh_table *lines[n_line_types];	/* Line buffers. */
    
    struct font_entry *prop;	/* Default Roman proportional font. */
    struct font_entry *fixed;	/* Default Roman fixed-pitch font. */
    struct hsh_table *loaded;	/* Fonts in memory. */

    struct hsh_table *combos;	/* Combinations of fonts with font sizes. */
    struct ps_font_combo *last_font;	/* PostScript selected font. */
    int next_combo;		/* Next font combo position index. */

    struct hsh_table *encodings;/* Set of encodings. */
    int next_encoding;		/* Next font encoding index. */

    /* Currently selected font. */
    struct font_entry *current;	/* Current font. */
    char *family;		/* Font family. */
    int size;			/* Size in psus. */
  }
ps_driver_ext;

/* Transform logical y-ordinate Y into a page ordinate. */
#define YT(Y) (this->length - (Y))

/* Prototypes. */
static int postopen (struct file_ext *);
static int preclose (struct file_ext *);
static void draw_headers (struct outp_driver *this);

static int compare_font_entry (const void *, const void *, void *param);
static unsigned hash_font_entry (const void *, void *param);
static void free_font_entry (void *, void *foo);
static struct font_entry *load_font (struct outp_driver *, const char *dit);
static void init_fonts (void);
static void done_fonts (void);

static void dump_lines (struct outp_driver *this);

static void read_ps_encodings (struct outp_driver *this);
static int compare_ps_encoding (const void *pa, const void *pb, void *foo);
static unsigned hash_ps_encoding (const void *pa, void *foo);
static void free_ps_encoding (void *a, void *foo);
static void add_encoding (struct outp_driver *this, char *filename);
static struct ps_encoding *default_encoding (struct outp_driver *this);

static int compare_ps_combo (const void *pa, const void *pb, void *foo);
static unsigned hash_ps_combo (const void *pa, void *foo);
static void free_ps_combo (void *a, void *foo);

static char *quote_ps_name (char *dest, const char *string);
static char *quote_ps_string (char *dest, const char *string);

/* Driver initialization. */

static int
ps_open_global (struct outp_class *this UNUSED)
{
  init_fonts ();
  groff_init ();
  return 1;
}

static int
ps_close_global (struct outp_class *this UNUSED)
{
  groff_done ();
  done_fonts ();
  return 1;
}

static int *
ps_font_sizes (struct outp_class *this UNUSED, int *n_valid_sizes)
{
  /* Allow fonts up to 1" in height. */
  static int valid_sizes[] =
  {1, PSUS, 0, 0};

  assert (n_valid_sizes != NULL);
  *n_valid_sizes = 1;
  return valid_sizes;
}

static int
ps_preopen_driver (struct outp_driver *this)
{
  struct ps_driver_ext *x;
  
  int i;

  assert (this->driver_open == 0);
  msg (VM (1), _("PostScript driver initializing as `%s'..."), this->name);
	
  this->ext = x = xmalloc (sizeof (struct ps_driver_ext));
  this->res = PSUS;
  this->horiz = this->vert = 1;
  this->width = this->length = 0;

  x->orientation = OTN_PORTRAIT;
  x->output_options = OPO_COLOR | OPO_HEADERS | OPO_AUTO_ENCODE;
  x->data = ODA_CLEAN7BIT;
	
  x->left_margin = x->right_margin =
    x->top_margin = x->bottom_margin = PSUS / 2;
	
  strcpy (x->eol, "\n");

  x->font_dir = NULL;
  x->prologue_fn = NULL;
  x->desc_fn = NULL;
  x->encoding_fn = NULL;

  x->prop_family = NULL;
  x->fixed_family = NULL;
  x->font_size = PSUS * 10 / 72;

  x->line_gutter = PSUS / 144;
  x->line_space = PSUS / 144;
  x->line_width = PSUS / 144;
  x->line_width_thick = PSUS / 48;

  x->text_opt = -1;
  x->line_opt = -1;
  x->max_fonts = 0;

  x->file.filename = NULL;
  x->file.mode = "wb";
  x->file.file = NULL;
  x->file.sequence_no = &x->page_number;
  x->file.param = this;
  x->file.postopen = postopen;
  x->file.preclose = preclose;
  x->page_number = 0;
  x->w = x->l = 0;

  x->file_page_number = 0;
  for (i = 0; i < n_line_types; i++)
    x->lines[i] = NULL;
  x->last_font = NULL;

  x->prop = NULL;
  x->fixed = NULL;
  x->loaded = NULL;

  x->next_combo = 0;
  x->combos = NULL;

  x->encodings = hsh_create (31, compare_ps_encoding, hash_ps_encoding,
			     free_ps_encoding, NULL);
  x->next_encoding = 0;

  x->current = NULL;
  x->family = NULL;
  x->size = 0;

  return 1;
}

static int
ps_postopen_driver (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;
  
  assert (this->driver_open == 0);

  if (this->width == 0)
    {
      this->width = PSUS * 17 / 2;	/* Defaults to 8.5"x11". */
      this->length = PSUS * 11;
    }

  if (x->text_opt == -1)
    x->text_opt = (this->device & OUTP_DEV_SCREEN) ? 0 : 1;
  if (x->line_opt == -1)
    x->line_opt = (this->device & OUTP_DEV_SCREEN) ? 0 : 1;

  x->w = this->width;
  x->l = this->length;
  if (x->orientation == OTN_LANDSCAPE)
    {
      int temp = this->width;
      this->width = this->length;
      this->length = temp;
    }
  this->width -= x->left_margin + x->right_margin;
  this->length -= x->top_margin + x->bottom_margin;
  if (x->output_options & OPO_HEADERS)
    {
      this->length -= 3 * x->font_size;
      x->top_margin += 3 * x->font_size;
    }
  if (NULL == x->file.filename)
    x->file.filename = xstrdup ("pspp.ps");

  if (x->font_dir == NULL)
    x->font_dir = xstrdup ("devps");
  if (x->prologue_fn == NULL)
    x->prologue_fn = xstrdup ("ps-prologue");
  if (x->desc_fn == NULL)
    x->desc_fn = xstrdup ("DESC");
  if (x->encoding_fn == NULL)
    x->encoding_fn = xstrdup ("ps-encodings");

  if (x->prop_family == NULL)
    x->prop_family = xstrdup ("H");
  if (x->fixed_family == NULL)
    x->fixed_family = xstrdup ("C");

  read_ps_encodings (this);

  x->family = NULL;
  x->size = PSUS / 6;

  if (this->length / x->font_size < 15)
    {
      msg (SE, _("PostScript driver: The defined page is not long "
		 "enough to hold margins and headers, plus least 15 "
		 "lines of the default fonts.  In fact, there's only "
		 "room for %d lines of each font at the default size "
		 "of %d.%03d points."),
	   this->length / x->font_size,
	   x->font_size / 1000, x->font_size % 1000);
      return 0;
    }

  this->driver_open = 1;
  msg (VM (2), _("%s: Initialization complete."), this->name);

  return 1;
}

static int
ps_close_driver (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;
  
  int i;

  assert (this->driver_open == 1);
  msg (VM (2), _("%s: Beginning closing..."), this->name);
  
  fn_close_ext (&x->file);
  free (x->file.filename);
  free (x->font_dir);
  free (x->prologue_fn);
  free (x->desc_fn);
  free (x->encoding_fn);
  free (x->prop_family);
  free (x->fixed_family);
  free (x->family);
  for (i = 0; i < n_line_types; i++)
    hsh_destroy (x->lines[i]);
  hsh_destroy (x->encodings);
  hsh_destroy (x->combos);
  hsh_destroy (x->loaded);
  free (x);
  
  this->driver_open = 0;
  msg (VM (3), _("%s: Finished closing."), this->name);

  return 1;
}

/* font_entry comparison function for hash tables. */
static int
compare_font_entry (const void *a, const void *b, void *foobar UNUSED)
{
  return strcmp (((struct font_entry *) a)->dit, ((struct font_entry *) b)->dit);
}

/* font_entry hash function for hash tables. */
static unsigned
hash_font_entry (const void *fe_, void *foobar UNUSED)
{
  const struct font_entry *fe = fe_;
  return hsh_hash_string (fe->dit);
}

/* font_entry destructor function for hash tables. */
static void
free_font_entry (void *pa, void *foo UNUSED)
{
  struct font_entry *a = pa;
  free (a->dit);
  free (a);
}

/* Generic option types. */
enum
{
  boolean_arg = -10,
  pos_int_arg,
  dimension_arg,
  string_arg,
  nonneg_int_arg
};

/* All the options that the PostScript driver supports. */
static struct outp_option option_tab[] =
{
  /* *INDENT-OFF* */
  {"output-file",		1,		0},
  {"paper-size",		2,		0},
  {"orientation",		3,		0},
  {"color",			boolean_arg,	0},
  {"data",			4,		0},
  {"auto-encode",		boolean_arg,	5},
  {"headers",			boolean_arg,	1},
  {"left-margin",		pos_int_arg,	0},
  {"right-margin",		pos_int_arg,	1},
  {"top-margin",		pos_int_arg,	2},
  {"bottom-margin",		pos_int_arg,	3},
  {"font-dir",			string_arg,	0},
  {"prologue-file",		string_arg,	1},
  {"device-file",		string_arg,	2},
  {"encoding-file",		string_arg,	3},
  {"prop-font-family",		string_arg,	5},
  {"fixed-font-family",		string_arg,	6},
  {"font-size",			pos_int_arg,	4},
  {"optimize-text-size",	nonneg_int_arg,	0},
  {"optimize-line-size",	nonneg_int_arg,	1},
  {"max-fonts-simult",		nonneg_int_arg,	2},
  {"line-ends",			6,              0},
  {"line-style",		7,		0},
  {"line-width",		dimension_arg,	2},
  {"line-gutter",		dimension_arg,	3},
  {"line-width",		dimension_arg,	4},
  {"line-width-thick",		dimension_arg,	5},
  {"", 0, 0},
  /* *INDENT-ON* */
};
static struct outp_option_info option_info;

static void
ps_option (struct outp_driver *this, const char *key, const struct string *val)
{
  struct ps_driver_ext *x = this->ext;
  int cat, subcat;
  char *value = ds_c_str (val);

  cat = outp_match_keyword (key, option_tab, &option_info, &subcat);

  switch (cat)
    {
    case 0:
      msg (SE, _("Unknown configuration parameter `%s' for PostScript device "
	   "driver."), key);
      break;
    case 1:
      free (x->file.filename);
      x->file.filename = xstrdup (value);
      break;
    case 2:
      outp_get_paper_size (value, &this->width, &this->length);
      break;
    case 3:
      if (!strcmp (value, "portrait"))
	x->orientation = OTN_PORTRAIT;
      else if (!strcmp (value, "landscape"))
	x->orientation = OTN_LANDSCAPE;
      else
	msg (SE, _("Unknown orientation `%s'.  Valid orientations are "
	     "`portrait' and `landscape'."), value);
      break;
    case 4:
      if (!strcmp (value, "clean7bit") || !strcmp (value, "Clean7Bit"))
	x->data = ODA_CLEAN7BIT;
      else if (!strcmp (value, "clean8bit")
	       || !strcmp (value, "Clean8Bit"))
	x->data = ODA_CLEAN8BIT;
      else if (!strcmp (value, "binary") || !strcmp (value, "Binary"))
	x->data = ODA_BINARY;
      else
	msg (SE, _("Unknown value for `data'.  Valid values are `clean7bit', "
	     "`clean8bit', and `binary'."));
      break;
    case 6:
      if (!strcmp (value, "lf"))
	strcpy (x->eol, "\n");
      else if (!strcmp (value, "crlf"))
	strcpy (x->eol, "\r\n");
      else
	msg (SE, _("Unknown value for `line-ends'.  Valid values are `lf' and "
		   "`crlf'."));
      break;
    case 7:
      if (!strcmp (value, "thick"))
	x->output_options &= ~OPO_DOUBLE_LINE;
      else if (!strcmp (value, "double"))
	x->output_options |= OPO_DOUBLE_LINE;
      else
	msg (SE, _("Unknown value for `line-style'.  Valid values are `thick' "
		   "and `double'."));
      break;
    case boolean_arg:
      {
	int setting;
	int mask;

	if (!strcmp (value, "on") || !strcmp (value, "true")
	    || !strcmp (value, "yes") || atoi (value))
	  setting = 1;
	else if (!strcmp (value, "off") || !strcmp (value, "false")
		 || !strcmp (value, "no") || !strcmp (value, "0"))
	  setting = 0;
	else
	  {
	    msg (SE, _("Boolean value expected for %s."), key);
	    return;
	  }
	switch (subcat)
	  {
	  case 0:
	    mask = OPO_COLOR;
	    break;
	  case 1:
	    mask = OPO_HEADERS;
	    break;
	  case 2:
	    mask = OPO_MIRROR_HORZ;
	    break;
	  case 3:
	    mask = OPO_MIRROR_VERT;
	    break;
	  case 4:
	    mask = OPO_ROTATE_180;
	    break;
	  case 5:
	    mask = OPO_AUTO_ENCODE;
	    break;
	  default:
	    assert (0);
            abort ();
	  }
	if (setting)
	  x->output_options |= mask;
	else
	  x->output_options &= ~mask;
      }
      break;
    case pos_int_arg:
      {
	char *tail;
	int arg;

	errno = 0;
	arg = strtol (value, &tail, 0);
	if (arg < 1 || errno == ERANGE || *tail)
	  {
	    msg (SE, _("Positive integer required as value for `%s'."), key);
	    break;
	  }
	if ((subcat == 4 || subcat == 5) && arg < 1000)
	  {
	    msg (SE, _("Default font size must be at least 1 point (value "
		 "of 1000 for key `%s')."), key);
	    break;
	  }
	switch (subcat)
	  {
	  case 0:
	    x->left_margin = arg;
	    break;
	  case 1:
	    x->right_margin = arg;
	    break;
	  case 2:
	    x->top_margin = arg;
	    break;
	  case 3:
	    x->bottom_margin = arg;
	    break;
	  case 4:
	    x->font_size = arg;
	    break;
	  default:
	    assert (0);
	  }
      }
      break;
    case dimension_arg:
      {
	int dimension = outp_evaluate_dimension (value, NULL);

	if (dimension <= 0)
	  {
	    msg (SE, _("Value for `%s' must be a dimension of positive "
		 "length (i.e., `1in')."), key);
	    break;
	  }
	switch (subcat)
	  {
	  case 2:
	    x->line_width = dimension;
	    break;
	  case 3:
	    x->line_gutter = dimension;
	    break;
	  case 4:
	    x->line_width = dimension;
	    break;
	  case 5:
	    x->line_width_thick = dimension;
	    break;
	  default:
	    assert (0);
	  }
      }
      break;
    case string_arg:
      {
	char **dest;
	switch (subcat)
	  {
	  case 0:
	    dest = &x->font_dir;
	    break;
	  case 1:
	    dest = &x->prologue_fn;
	    break;
	  case 2:
	    dest = &x->desc_fn;
	    break;
	  case 3:
	    dest = &x->encoding_fn;
	    break;
	  case 5:
	    dest = &x->prop_family;
	    break;
	  case 6:
	    dest = &x->fixed_family;
	    break;
	  default:
	    assert (0);
            abort ();
	  }
	if (*dest)
	  free (*dest);
	*dest = xstrdup (value);
      }
      break;
    case nonneg_int_arg:
      {
	char *tail;
	int arg;

	errno = 0;
	arg = strtol (value, &tail, 0);
	if (arg < 0 || errno == ERANGE || *tail)
	  {
	    msg (SE, _("Nonnegative integer required as value for `%s'."), key);
	    break;
	  }
	switch (subcat)
	  {
	  case 0:
	    x->text_opt = arg;
	    break;
	  case 1:
	    x->line_opt = arg;
	    break;
	  case 2:
	    x->max_fonts = arg;
	    break;
	  default:
	    assert (0);
	  }
      }
      break;
    default:
      assert (0);
    }
}

/* Looks for a PostScript font file or config file in all the
   appropriate places.  Returns the filename on success, NULL on
   failure. */
/* PORTME: Filename operations. */
static char *
find_ps_file (struct outp_driver *this, const char *name)
{
  struct ps_driver_ext *x = this->ext;
  char *cp;

  /* x->font_dir + name: "devps/ps-encodings". */
  char *basename;

  /* Usually equal to groff_font_path. */
  char *pathname;

  /* Final filename. */
  char *fn;

  /* Make basename. */
  basename = local_alloc (strlen (x->font_dir) + 1 + strlen (name) + 1);
  cp = stpcpy (basename, x->font_dir);
  *cp++ = DIR_SEPARATOR;
  strcpy (cp, name);

  /* Decide on search path. */
  {
    const char *pre_pathname;
    
    pre_pathname = getenv ("STAT_GROFF_FONT_PATH");
    if (pre_pathname == NULL)
      pre_pathname = getenv ("GROFF_FONT_PATH");
    if (pre_pathname == NULL)
      pre_pathname = groff_font_path;
    pathname = fn_tilde_expand (pre_pathname);
  }

  /* Search all possible places for the file. */
  fn = fn_search_path (basename, pathname, NULL);
  if (fn == NULL)
    fn = fn_search_path (basename, config_path, NULL);
  if (fn == NULL)
    fn = fn_search_path (name, pathname, NULL);
  if (fn == NULL)
    fn = fn_search_path (name, config_path, NULL);
  free (pathname);
  local_free (basename);

  return fn;
}

/* Encodings. */

/* Hash table comparison function for ps_encoding's. */
static int
compare_ps_encoding (const void *pa, const void *pb, void *foo UNUSED)
{
  const struct ps_encoding *a = pa;
  const struct ps_encoding *b = pb;

  return strcmp (a->filename, b->filename);
}

/* Hash table hash function for ps_encoding's. */
static unsigned
hash_ps_encoding (const void *pa, void *foo UNUSED)
{
  const struct ps_encoding *a = pa;

  return hsh_hash_string (a->filename);
}

/* Hash table free function for ps_encoding's. */
static void
free_ps_encoding (void *pa, void *foo UNUSED)
{
  struct ps_encoding *a = pa;

  free (a->filename);
  free (a);
}

/* Iterates through the list of encodings used for this driver
   instance, reads each of them from disk, and writes them as
   PostScript code to the output file. */
static void
output_encodings (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;

  struct hsh_iterator iter;
  struct ps_encoding *pe;

  struct string line, buf;

  ds_init (&line, 128);
  ds_init (&buf, 128);
  for (pe = hsh_first (x->encodings, &iter); pe != NULL;
       pe = hsh_next (x->encodings, &iter)) 
    {
      FILE *f;

      msg (VM (1), _("%s: %s: Opening PostScript font encoding..."),
	   this->name, pe->filename);
      
      f = fopen (pe->filename, "r");
      if (!f)
	{
	  msg (IE, _("PostScript driver: Cannot open encoding file `%s': %s.  "
	       "Substituting ISOLatin1Encoding for missing encoding."),
	       pe->filename, strerror (errno));
	  fprintf (x->file.file, "/E%x ISOLatin1Encoding def%s",
		   pe->index, x->eol);
	}
      else
	{
	  struct file_locator where;
	  
	  const char *tab[256];

	  char *pschar;
	  char *code;
	  int code_val;
	  char *fubar;

	  const char *notdef = ".notdef";

	  int i;

	  for (i = 0; i < 256; i++)
	    tab[i] = notdef;

	  where.filename = pe->filename;
	  where.line_number = 0;
	  err_push_file_locator (&where);

	  while (ds_get_config_line (f, &buf, &where))
	    {
	      char *sp;	

	      if (buf.length == 0) 
		continue;

	      pschar = strtok_r (ds_c_str (&buf), " \t\r\n", &sp);
	      code = strtok_r (NULL, " \t\r\n", &sp);
	      if (*pschar == 0 || *code == 0)
		continue;
	      code_val = strtol (code, &fubar, 0);
	      if (*fubar)
		{
		  msg (IS, _("PostScript driver: Invalid numeric format."));
		  continue;
		}
	      if (code_val < 0 || code_val > 255)
		{
		  msg (IS, _("PostScript driver: Codes must be between 0 "
			     "and 255.  (%d is not allowed.)"), code_val);
		  break;
		}
	      tab[code_val] = local_alloc (strlen (pschar) + 1);
	      strcpy ((char *) (tab[code_val]), pschar);
	    }
	  err_pop_file_locator (&where);

	  ds_clear (&line);
	  ds_printf (&line, "/E%x[", pe->index);
	  for (i = 0; i < 257; i++)
	    {
	      char temp[288];

	      if (i < 256)
		{
		  quote_ps_name (temp, tab[i]);
		  if (tab[i] != notdef)
		    local_free (tab[i]);
		}
	      else
		strcpy (temp, "]def");
	      
	      if (ds_length (&line) + strlen (temp) > 70)
		{
		  ds_puts (&line, x->eol);
		  fputs (ds_c_str (&line), x->file.file);
		  ds_clear (&line);
		}
	      ds_puts (&line, temp);
	    }
	  ds_puts (&line, x->eol);
	  fputs (ds_c_str (&line), x->file.file);

	  if (fclose (f) == EOF)
	    msg (MW, _("PostScript driver: Error closing encoding file `%s'."),
		 pe->filename);

	  msg (VM (2), _("%s: PostScript font encoding read successfully."),
	       this->name);
	}
    }
  ds_destroy (&line);
  ds_destroy (&buf);
}

/* Finds the ps_encoding in THIS that corresponds to the file with
   name NORM_FILENAME, which must have previously been normalized with
   normalize_filename(). */
static struct ps_encoding *
get_encoding (struct outp_driver *this, const char *norm_filename)
{
  struct ps_driver_ext *x = this->ext;
  struct ps_encoding *pe;

  pe = (struct ps_encoding *) hsh_find (x->encodings, (void *) &norm_filename);
  return pe;
}

/* Searches the filesystem for an encoding file with name FILENAME;
   returns its malloc'd, normalized name if found, otherwise NULL. */
static char *
find_encoding_file (struct outp_driver *this, char *filename)
{
  char *cp, *temp;

  if (filename == NULL)
    return NULL;
  while (isspace ((unsigned char) *filename))
    filename++;
  for (cp = filename; *cp && !isspace ((unsigned char) *cp); cp++)
    ;
  if (cp == filename)
    return NULL;
  *cp = 0;

  temp = find_ps_file (this, filename);
  if (temp == NULL)
    return NULL;

  filename = fn_normalize (temp);
  assert (filename != NULL);
  free (temp);

  return filename;
}

/* Adds the encoding represented by the not-necessarily-normalized
   file FILENAME to the list of encodings, if it exists and is not
   already in the list. */
static void
add_encoding (struct outp_driver *this, char *filename)
{
  struct ps_driver_ext *x = this->ext;
  struct ps_encoding **pe;

  filename = find_encoding_file (this, filename);
  if (!filename)
    return;

  pe = (struct ps_encoding **) hsh_probe (x->encodings, &filename);
  if (*pe)
    {
      free (filename);
      return;
    }
  *pe = xmalloc (sizeof **pe);
  (*pe)->filename = filename;
  (*pe)->index = x->next_encoding++;
}

/* Finds the file on disk that contains the list of encodings to
   include in the output file, then adds those encodings to the list
   of encodings. */
static void
read_ps_encodings (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;

  /* Encodings file. */
  char *encoding_fn;		/* `ps-encodings' filename. */
  FILE *f;

  struct string line;
  struct file_locator where;

  /* It's okay if there's no list of encodings; not everyone cares. */
  encoding_fn = find_ps_file (this, x->encoding_fn);
  if (encoding_fn == NULL)
    return;
  free (encoding_fn);

  msg (VM (1), _("%s: %s: Opening PostScript encoding list file."),
       this->name, encoding_fn);
  f = fopen (encoding_fn, "r");
  if (!f)
    {
      msg (IE, _("Opening %s: %s."), encoding_fn, strerror (errno));
      return;
    }

  where.filename = encoding_fn;
  where.line_number = 0;
  err_push_file_locator (&where);

  ds_init (&line, 128);
    
  for (;;)
    {
      if (!ds_get_config_line (f, &line, &where))
	{
	  if (ferror (f))
	    msg (ME, _("Reading %s: %s."), encoding_fn, strerror (errno));
	  break;
	}

      add_encoding (this, line.string);
    }

  ds_destroy (&line);
  err_pop_file_locator (&where);
  
  if (-1 == fclose (f))
    msg (MW, _("Closing %s: %s."), encoding_fn, strerror (errno));

  msg (VM (2), _("%s: PostScript encoding list file read successfully."), this->name);
}

/* Creates a default encoding for driver D that can be substituted for
   an unavailable encoding. */
struct ps_encoding *
default_encoding (struct outp_driver *d)
{
  struct ps_driver_ext *x = d->ext;
  static struct ps_encoding *enc;

  if (!enc)
    {
      enc = xmalloc (sizeof *enc);
      enc->filename = xstrdup (_("<<default encoding>>"));
      enc->index = x->next_encoding++;
    }
  return enc;
}

/* Basic file operations. */

/* Variables for the prologue. */
struct ps_variable
  {
    const char *key;
    const char *value;
  };

static struct ps_variable *ps_var_tab;

/* Searches ps_var_tab for a ps_variable with key KEY, and returns the
   associated value. */
static const char *
ps_get_var (const char *key)
{
  struct ps_variable *v;

  for (v = ps_var_tab; v->key; v++)
    if (!strcmp (key, v->key))
      return v->value;
  return NULL;
}

/* Writes the PostScript prologue to file F. */
static int
postopen (struct file_ext *f)
{
  static struct ps_variable dict[] =
  {
    {"bounding-box", 0},
    {"creator", 0},
    {"date", 0},
    {"data", 0},
    {"orientation", 0},
    {"user", 0},
    {"host", 0},
    {"prop-font", 0},
    {"fixed-font", 0},
    {"scale-factor", 0},
    {"paper-width", 0},
    {"paper-length", 0},
    {"left-margin", 0},
    {"top-margin", 0},
    {"line-width", 0},
    {"line-width-thick", 0},
    {"title", 0},
    {"source-file", 0},
    {0, 0},
  };
  char boundbox[INT_DIGITS * 4 + 4];
#if HAVE_UNISTD_H
  char host[128];
#endif
  char scaling[INT_DIGITS + 5];
  time_t curtime;
  struct tm *loctime;
  char *p, *cp;
  char paper_width[INT_DIGITS + 1];
  char paper_length[INT_DIGITS + 1];
  char left_margin[INT_DIGITS + 1];
  char top_margin[INT_DIGITS + 1];
  char line_width[INT_DIGITS + 1];
  char line_width_thick[INT_DIGITS + 1];

  struct outp_driver *this = f->param;
  struct ps_driver_ext *x = this->ext;

  char *prologue_fn = find_ps_file (this, x->prologue_fn);
  FILE *prologue_file;

  char *buf = NULL;
  size_t buf_size = 0;

  x->loaded = hsh_create (31, compare_font_entry, hash_font_entry,
			  free_font_entry, NULL);
  
  {
    char *font_name = local_alloc (2 + max (strlen (x->prop_family),
					    strlen (x->fixed_family)));
    
    strcpy (stpcpy (font_name, x->prop_family), "R");
    x->prop = load_font (this, font_name);

    strcpy (stpcpy (font_name, x->fixed_family), "R");
    x->fixed = load_font (this, font_name);

    local_free(font_name);
  }

  x->current = x->prop;
  x->family = xstrdup (x->prop_family);
  x->size = x->font_size;
  
  {
    int *h = this->horiz_line_width, *v = this->vert_line_width;
    
    this->cp_x = this->cp_y = 0;
    this->font_height = x->font_size;
    {
      struct char_metrics *metric;

      metric = font_get_char_metrics (x->prop->font, '0');
      this->prop_em_width = ((metric
			      ? metric->width : x->prop->font->space_width)
			     * x->font_size / 1000);

      metric = font_get_char_metrics (x->fixed->font, '0');
      this->fixed_width = ((metric
			    ? metric->width : x->fixed->font->space_width)
			   * x->font_size / 1000);
    }
        
    h[0] = v[0] = 0;
    h[1] = v[1] = 2 * x->line_gutter + x->line_width;
    if (x->output_options & OPO_DOUBLE_LINE)
      h[2] = v[2] = 2 * x->line_gutter + 2 * x->line_width + x->line_space;
    else
      h[2] = v[2] = 2 * x->line_gutter + x->line_width_thick;
    h[3] = v[3] = 2 * x->line_gutter + x->line_width;
    
    {
      int i;
      
      for (i = 0; i < (1 << OUTP_L_COUNT); i++)
	{
	  int bit;

	  /* Maximum width of any line type so far. */
	  int max = 0;

	  for (bit = 0; bit < OUTP_L_COUNT; bit++)
	    if ((i & (1 << bit)) && h[bit] > max)
	      max = h[bit];
	  this->horiz_line_spacing[i] = this->vert_line_spacing[i] = max;
	}
    }
  }

  if (x->output_options & OPO_AUTO_ENCODE)
    {
      /* It's okay if this is done more than once since add_encoding()
         is idempotent over identical encodings. */
      add_encoding (this, x->prop->font->encoding);
      add_encoding (this, x->fixed->font->encoding);
    }

  x->file_page_number = 0;

  errno = 0;
  if (prologue_fn == NULL)
    {
      msg (IE, _("Cannot find PostScript prologue.  The use of `-vv' "
		 "on the command line is suggested as a debugging aid."));
      return 0;
    }

  msg (VM (1), _("%s: %s: Opening PostScript prologue..."),
       this->name, prologue_fn);
  prologue_file = fopen (prologue_fn, "rb");
  if (prologue_file == NULL)
    {
      fclose (prologue_file);
      free (prologue_fn);
      msg (IE, "%s: %s", prologue_fn, strerror (errno));
      goto error;
    }

  sprintf (boundbox, "0 0 %d %d",
	   x->w / (PSUS / 72) + (x->w % (PSUS / 72) > 0),
	   x->l / (PSUS / 72) + (x->l % (PSUS / 72) > 0));
  dict[0].value = boundbox;

  dict[1].value = (char *) version;

  curtime = time (NULL);
  loctime = localtime (&curtime);
  dict[2].value = asctime (loctime);
  cp = strchr (dict[2].value, '\n');
  if (cp)
    *cp = 0;

  switch (x->data)
    {
    case ODA_CLEAN7BIT:
      dict[3].value = "Clean7Bit";
      break;
    case ODA_CLEAN8BIT:
      dict[3].value = "Clean8Bit";
      break;
    case ODA_BINARY:
      dict[3].value = "Binary";
      break;
    default:
      assert (0);
    }

  if (x->orientation == OTN_PORTRAIT)
    dict[4].value = "Portrait";
  else
    dict[4].value = "Landscape";

  /* PORTME: Determine username, net address. */
#if HAVE_UNISTD_H
  dict[5].value = getenv ("LOGNAME");
  if (!dict[5].value)
    dict[5].value = getlogin ();
  if (!dict[5].value)
    dict[5].value = _("nobody");

  if (gethostname (host, 128) == -1)
    {
      if (errno == ENAMETOOLONG)
	host[127] = 0;
      else
	strcpy (host, _("nowhere"));
    }
  dict[6].value = host;
#else /* !HAVE_UNISTD_H */
  dict[5].value = _("nobody");
  dict[6].value = _("nowhere");
#endif /* !HAVE_UNISTD_H */

  cp = stpcpy (p = local_alloc (288), "font ");
  quote_ps_string (cp, x->prop->font->internal_name);
  dict[7].value = p;

  cp = stpcpy (p = local_alloc (288), "font ");
  quote_ps_string (cp, x->fixed->font->internal_name);
  dict[8].value = p;

  sprintf (scaling, "%.3f", PSUS / 72.0);
  dict[9].value = scaling;

  sprintf (paper_width, "%g", x->w / (PSUS / 72.0));
  dict[10].value = paper_width;

  sprintf (paper_length, "%g", x->l / (PSUS / 72.0));
  dict[11].value = paper_length;

  sprintf (left_margin, "%d", x->left_margin);
  dict[12].value = left_margin;

  sprintf (top_margin, "%d", x->top_margin);
  dict[13].value = top_margin;

  sprintf (line_width, "%d", x->line_width);
  dict[14].value = line_width;

  sprintf (line_width, "%d", x->line_width_thick);
  dict[15].value = line_width_thick;
  
  getl_location (&dict[17].value, NULL);
  if (dict[17].value == NULL)
    dict[17].value = "<stdin>";

  if (!outp_title)
    {
      dict[16].value = cp = local_alloc (strlen (dict[17].value) + 30);
      sprintf (cp, "PSPP (%s)", dict[17].value);
    }
  else
    {
      dict[16].value = local_alloc (strlen (outp_title) + 1);
      strcpy ((char *) (dict[16].value), outp_title);
    }
  
  ps_var_tab = dict;
  while (-1 != getline (&buf, &buf_size, prologue_file))
    {
      char *cp;
      char *buf2;
      int len;

      cp = strstr (buf, "!eps");
      if (cp)
	{
	  if (this->class->magic == MAGIC_PS)
	    continue;
	  else
	    *cp = '\0';
	}
      else
	{
	  cp = strstr (buf, "!ps");
	  if (cp)
	    {
	      if (this->class->magic == MAGIC_EPSF)
		continue;
	      else
		*cp = '\0';
	    } else {
	      if (strstr (buf, "!!!"))
		continue;
	    }
	}

      if (!strncmp (buf, "!encodings", 10))
	output_encodings (this);
      else
	{
	  char *beg;
	  beg = buf2 = fn_interp_vars (buf, ps_get_var);
	  len = strlen (buf2);
	  while (isspace (*beg))
	    beg++, len--;
	  if (beg[len - 1] == '\n')
	    len--;
	  if (beg[len - 1] == '\r')
	    len--;
	  fwrite (beg, len, 1, f->file);
	  fputs (x->eol, f->file);
	  free (buf2);
	}
    }
  if (ferror (f->file))
    msg (IE, _("Reading `%s': %s."), prologue_fn, strerror (errno));
  fclose (prologue_file);

  free (prologue_fn);
  free (buf);

  local_free (dict[7].value);
  local_free (dict[8].value);
  local_free (dict[16].value);

  if (ferror (f->file))
    goto error;

  msg (VM (2), _("%s: PostScript prologue read successfully."), this->name);
  return 1;

error:
  msg (VM (1), _("%s: Error reading PostScript prologue."), this->name);
  return 0;
}

/* Writes the string STRING to buffer DEST (of at least 288
   characters) as a PostScript name object.  Returns a pointer
   to the null terminator of the resultant string. */
static char *
quote_ps_name (char *dest, const char *string)
{
  const char *sp;

  for (sp = string; *sp; sp++)
    switch (*(unsigned char *) sp)
      {
      case 'a':
      case 'f':
      case 'k':
      case 'p':
      case 'u':
      case 'b':
      case 'g':
      case 'l':
      case 'q':
      case 'v':
      case 'c':
      case 'h':
      case 'm':
      case 'r':
      case 'w':
      case 'd':
      case 'i':
      case 'n':
      case 's':
      case 'x':
      case 'e':
      case 'j':
      case 'o':
      case 't':
      case 'y':
      case 'z':
      case 'A':
      case 'F':
      case 'K':
      case 'P':
      case 'U':
      case 'B':
      case 'G':
      case 'L':
      case 'Q':
      case 'V':
      case 'C':
      case 'H':
      case 'M':
      case 'R':
      case 'W':
      case 'D':
      case 'I':
      case 'N':
      case 'S':
      case 'X':
      case 'E':
      case 'J':
      case 'O':
      case 'T':
      case 'Y':
      case 'Z':
      case '@':
      case '^':
      case '_':
      case '|':
      case '!':
      case '$':
      case '&':
      case ':':
      case ';':
      case '.':
      case ',':
      case '-':
      case '+':
	break;
      default:
	{
	  char *dp = dest;

	  *dp++ = '<';
	  for (sp = string; *sp && dp < &dest[256]; sp++)
	    {
	      sprintf (dp, "%02x", *(unsigned char *) sp);
	      dp += 2;
	    }
	  return stpcpy (dp, ">cvn");
	}
      }
  dest[0] = '/';
  return stpcpy (&dest[1], string);
}

/* Adds the string STRING to buffer DEST as a PostScript quoted
   string; returns a pointer to the null terminator added.  Will not
   add more than 235 characters. */
static char *
quote_ps_string (char *dest, const char *string)
{
  const char *sp = string;
  char *dp = dest;

  *dp++ = '(';
  for (; *sp && dp < &dest[235]; sp++)
    if (*sp == '(')
      dp = stpcpy (dp, "\\(");
    else if (*sp == ')')
      dp = stpcpy (dp, "\\)");
    else if (*sp < 32 || *((unsigned char *) sp) > 127)
      dp = spprintf (dp, "\\%3o", *sp);
    else
      *dp++ = *sp;
  return stpcpy (dp, ")");
}

/* Writes the PostScript epilogue to file F. */
static int
preclose (struct file_ext *f)
{
  struct outp_driver *this = f->param;
  struct ps_driver_ext *x = this->ext;
  struct hsh_iterator iter;
  struct font_entry *fe;

  fprintf (f->file,
	   ("%%%%Trailer%s"
	    "%%%%Pages: %d%s"
	    "%%%%DocumentNeededResources:%s"),
	   x->eol, x->file_page_number, x->eol, x->eol);

  for (fe = hsh_first (x->loaded, &iter); fe != NULL;
       fe = hsh_next (x->loaded, &iter)) 
    {
      char buf[256], *cp;

      cp = stpcpy (buf, "%%+ font ");
      cp = quote_ps_string (cp, fe->font->internal_name);
      strcpy (cp, x->eol);
      fputs (buf, f->file);
    }

  hsh_destroy (x->loaded);
  x->loaded = NULL;
  hsh_destroy (x->combos);
  x->combos = NULL;
  x->last_font = NULL;
  x->next_combo = 0;

  fprintf (f->file, "%%EOF%s", x->eol);
  if (ferror (f->file))
    return 0;
  return 1;
}

static int
ps_open_page (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;

  assert (this->driver_open && !this->page_open);
      
  x->page_number++;
  if (!fn_open_ext (&x->file))
    {
      if (errno)
	msg (ME, _("PostScript output driver: %s: %s"), x->file.filename,
	     strerror (errno));
      return 0;
    }
  x->file_page_number++;

  hsh_destroy (x->combos);
  x->combos = hsh_create (31, compare_ps_combo, hash_ps_combo,
			  free_ps_combo, NULL);
  x->last_font = NULL;
  x->next_combo = 0;

  fprintf (x->file.file,
	   "%%%%Page: %d %d%s"
	   "%%%%BeginPageSetup%s"
	   "/pg save def 0.001 dup scale%s",
	   x->page_number, x->file_page_number, x->eol,
	   x->eol,
	   x->eol);

  if (x->orientation == OTN_LANDSCAPE)
    fprintf (x->file.file,
	     "%d 0 translate 90 rotate%s",
	     x->w, x->eol);

  if (x->bottom_margin != 0 || x->left_margin != 0)
    fprintf (x->file.file,
	     "%d %d translate%s",
	     x->left_margin, x->bottom_margin, x->eol);

  fprintf (x->file.file,
	   "/LW %d def/TW %d def %d setlinewidth%s"
	   "%%%%EndPageSetup%s",
	   x->line_width, x->line_width_thick, x->line_width, x->eol,
	   x->eol);

  if (!ferror (x->file.file))
    {
      this->page_open = 1;
      if (x->output_options & OPO_HEADERS)
	draw_headers (this);
    }

  this->cp_y = 0;

  return !ferror (x->file.file);
}

static int
ps_close_page (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;

  assert (this->driver_open && this->page_open);
  
  if (x->line_opt)
    dump_lines (this);

  fprintf (x->file.file,
	   "%%PageTrailer%s"
	   "EP%s",
	   x->eol, x->eol);

  this->page_open = 0;
  return !ferror (x->file.file);
}

static void
ps_submit (struct outp_driver *this UNUSED, struct som_entity *s)
{
  switch (s->type) 
    {
    case SOM_CHART:
      break;
    default:
      assert(0);
      break;
    }
}

/* Lines. */

/* qsort() comparison function for int tuples. */
static int
int_2_compare (const void *a_, const void *b_)
{
  const int *a = a_;
  const int *b = b_;

  return *a < *b ? -1 : *a > *b;
}

/* Hash table comparison function for cached lines. */
static int
compare_line (const void *a_, const void *b_, void *foo UNUSED)
{
  const struct line_form *a = a_;
  const struct line_form *b = b_;

  return a->ind < b->ind ? -1 : a->ind > b->ind;
}

/* Hash table hash function for cached lines. */
static unsigned
hash_line (const void *pa, void *foo UNUSED)
{
  const struct line_form *a = pa;

  return a->ind;
}

/* Hash table free function for cached lines. */
static void
free_line (void *pa, void *foo UNUSED)
{
  free (pa);
}

/* Writes PostScript code to draw a line from (x1,y1) to (x2,y2) to
   the output file. */
#define dump_line(x1, y1, x2, y2)			\
	fprintf (ext->file.file, "%d %d %d %d L%s", 	\
		 x1, YT (y1), x2, YT (y2), ext->eol)

/* Write PostScript code to draw a thick line from (x1,y1) to (x2,y2)
   to the output file. */
#define dump_thick_line(x1, y1, x2, y2)			\
	fprintf (ext->file.file, "%d %d %d %d TL%s",	\
		 x1, YT (y1), x2, YT (y2), ext->eol)

/* Writes a line of type TYPE to THIS driver's output file.  The line
   (or its center, in the case of double lines) has its independent
   axis coordinate at IND; it extends from DEP1 to DEP2 on the
   dependent axis. */
static void
dump_fancy_line (struct outp_driver *this, int type, int ind, int dep1, int dep2)
{
  struct ps_driver_ext *ext = this->ext;
  int ofs = ext->line_space / 2 + ext->line_width / 2;

  switch (type)
    {
    case horz:
      dump_line (dep1, ind, dep2, ind);
      break;
    case dbl_horz:
      if (ext->output_options & OPO_DOUBLE_LINE)
	{
	  dump_line (dep1, ind - ofs, dep2, ind - ofs);
	  dump_line (dep1, ind + ofs, dep2, ind + ofs);
	}
      else
	dump_thick_line (dep1, ind, dep2, ind);
      break;
    case spl_horz:
      assert (0);
    case vert:
      dump_line (ind, dep1, ind, dep2);
      break;
    case dbl_vert:
      if (ext->output_options & OPO_DOUBLE_LINE)
	{
	  dump_line (ind - ofs, dep1, ind - ofs, dep2);
	  dump_line (ind + ofs, dep1, ind + ofs, dep2);
	}
      else
	dump_thick_line (ind, dep1, ind, dep2);
      break;
    case spl_vert:
      assert (0);
    default:
      assert (0);
    }
}

#undef dump_line

/* Writes all the cached lines to the output file, then clears the
   cache. */
static void
dump_lines (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;

  struct hsh_iterator iter;
  int type;

  for (type = 0; type < n_line_types; type++)
    {
      struct line_form *line;

      if (x->lines[type] == NULL) 
        continue;

      for (line = hsh_first (x->lines[type], &iter); line != NULL;
           line = hsh_next (x->lines[type], &iter)) 
        {
	  int i;
	  int lo = INT_MIN, hi;

	  qsort (line->dep, line->ndep, sizeof *line->dep, int_2_compare);
	  lo = line->dep[0][0];
	  hi = line->dep[0][1];
	  for (i = 1; i < line->ndep; i++)
	    if (line->dep[i][0] <= hi + 1)
	      {
		int min_hi = line->dep[i][1];
		if (min_hi > hi)
		  hi = min_hi;
	      }
	    else
	      {
		dump_fancy_line (this, type, line->ind, lo, hi);
		lo = line->dep[i][0];
		hi = line->dep[i][1];
	      }
	  dump_fancy_line (this, type, line->ind, lo, hi);
	}

      hsh_destroy (x->lines[type]);
      x->lines[type] = NULL;
    }
}

/* (Same args as dump_fancy_line()).  Either dumps the line directly
   to the output file, or adds it to the cache, depending on the
   user-selected line optimization mode. */
static void
line (struct outp_driver *this, int type, int ind, int dep1, int dep2)
{
  struct ps_driver_ext *ext = this->ext;
  struct line_form **f;

  assert (dep2 >= dep1);
  if (ext->line_opt == 0)
    {
      dump_fancy_line (this, type, ind, dep1, dep2);
      return;
    }

  if (ext->lines[type] == NULL)
    ext->lines[type] = hsh_create (31, compare_line, hash_line,
				   free_line, NULL);
  f = (struct line_form **) hsh_probe (ext->lines[type], &ind);
  if (*f == NULL)
    {
      *f = xmalloc (sizeof **f + sizeof (int[15][2]));
      (*f)->ind = ind;
      (*f)->mdep = 16;
      (*f)->ndep = 1;
      (*f)->dep[0][0] = dep1;
      (*f)->dep[0][1] = dep2;
      return;
    }
  if ((*f)->ndep >= (*f)->mdep)
    {
      (*f)->mdep += 16;
      *f = xrealloc (*f, (sizeof **f + sizeof (int[2]) * ((*f)->mdep - 1)));
    }
  (*f)->dep[(*f)->ndep][0] = dep1;
  (*f)->dep[(*f)->ndep][1] = dep2;
  (*f)->ndep++;
}

static void
ps_line_horz (struct outp_driver *this, const struct rect *r,
	      const struct color *c UNUSED, int style)
{
  /* Must match output.h:OUTP_L_*. */
  static const int types[OUTP_L_COUNT] =
  {-1, horz, dbl_horz, spl_horz};

  int y = (r->y1 + r->y2) / 2;

  assert (this->driver_open && this->page_open);
  assert (style >= 0 && style < OUTP_L_COUNT);
  style = types[style];
  if (style != -1)
    line (this, style, y, r->x1, r->x2);
}

static void
ps_line_vert (struct outp_driver *this, const struct rect *r,
	      const struct color *c UNUSED, int style)
{
  /* Must match output.h:OUTP_L_*. */
  static const int types[OUTP_L_COUNT] =
  {-1, vert, dbl_vert, spl_vert};

  int x = (r->x1 + r->x2) / 2;

  assert (this->driver_open && this->page_open);
  assert (style >= 0 && style < OUTP_L_COUNT);
  style = types[style];
  if (style != -1)
    line (this, style, x, r->y1, r->y2);
}

#define L (style->l != OUTP_L_NONE)
#define R (style->r != OUTP_L_NONE)
#define T (style->t != OUTP_L_NONE)
#define B (style->b != OUTP_L_NONE)

static void
ps_line_intersection (struct outp_driver *this, const struct rect *r,
		      const struct color *c UNUSED,
		      const struct outp_styles *style)
{
  struct ps_driver_ext *ext = this->ext;

  int x = (r->x1 + r->x2) / 2;
  int y = (r->y1 + r->y2) / 2;
  int ofs = (ext->line_space + ext->line_width) / 2;
  int x1 = x - ofs, x2 = x + ofs;
  int y1 = y - ofs, y2 = y + ofs;

  assert (this->driver_open && this->page_open);
  assert (!((style->l != style->r && style->l != OUTP_L_NONE
	     && style->r != OUTP_L_NONE)
	    || (style->t != style->b && style->t != OUTP_L_NONE
		&& style->b != OUTP_L_NONE)));

  switch ((style->l | style->r) | ((style->t | style->b) << 8))
    {
    case (OUTP_L_SINGLE) | (OUTP_L_SINGLE << 8):
    case (OUTP_L_SINGLE) | (OUTP_L_NONE << 8):
    case (OUTP_L_NONE) | (OUTP_L_SINGLE << 8):
      if (L)
	line (this, horz, y, r->x1, x);
      if (R)
	line (this, horz, y, x, r->x2);
      if (T)
	line (this, vert, x, r->y1, y);
      if (B)
	line (this, vert, x, y, r->y2);
      break;
    case (OUTP_L_SINGLE) | (OUTP_L_DOUBLE << 8):
    case (OUTP_L_NONE) | (OUTP_L_DOUBLE << 8):
      if (L)
	line (this, horz, y, r->x1, x1);
      if (R)
	line (this, horz, y, x2, r->x2);
      if (T)
	line (this, dbl_vert, x, r->y1, y);
      if (B)
	line (this, dbl_vert, x, y, r->y2);
      if ((L && R) && !(T && B))
	line (this, horz, y, x1, x2);
      break;
    case (OUTP_L_DOUBLE) | (OUTP_L_SINGLE << 8):
    case (OUTP_L_DOUBLE) | (OUTP_L_NONE << 8):
      if (L)
	line (this, dbl_horz, y, r->x1, x);
      if (R)
	line (this, dbl_horz, y, x, r->x2);
      if (T)
	line (this, vert, x, r->y1, y);
      if (B)
	line (this, vert, x, y, r->y2);
      if ((T && B) && !(L && R))
	line (this, vert, x, y1, y2);
      break;
    case (OUTP_L_DOUBLE) | (OUTP_L_DOUBLE << 8):
      if (L)
	line (this, dbl_horz, y, r->x1, x);
      if (R)
	line (this, dbl_horz, y, x, r->x2);
      if (T)
	line (this, dbl_vert, x, r->y1, y);
      if (B)
	line (this, dbl_vert, x, y, r->y2);
      if (T && B && !L)
	line (this, vert, x1, y1, y2);
      if (T && B && !R)
	line (this, vert, x2, y1, y2);
      if (L && R && !T)
	line (this, horz, y1, x1, x2);
      if (L && R && !B)
	line (this, horz, y2, x1, x2);
      break;
    default:
      assert (0);
    }
}

static void
ps_box (struct outp_driver *this UNUSED, const struct rect *r UNUSED,
	const struct color *bord UNUSED, const struct color *fill UNUSED)
{
  assert (this->driver_open && this->page_open);
}

static void 
ps_polyline_begin (struct outp_driver *this UNUSED,
		   const struct color *c UNUSED)
{
  assert (this->driver_open && this->page_open);
}
static void 
ps_polyline_point (struct outp_driver *this UNUSED, int x UNUSED, int y UNUSED)
{
  assert (this->driver_open && this->page_open);
}
static void 
ps_polyline_end (struct outp_driver *this UNUSED)
{
  assert (this->driver_open && this->page_open);
}

/* Returns the width of string S for THIS driver. */
static int
text_width (struct outp_driver *this, char *s)
{
  struct outp_text text;

  text.options = OUTP_T_JUST_LEFT;
  ls_init (&text.s, s, strlen (s));
  this->class->text_metrics (this, &text);
  return text.h;
}

/* Write string S at location (X,Y) with width W for THIS driver. */
static void
out_text_plain (struct outp_driver *this, char *s, int x, int y, int w)
{
  struct outp_text text;

  text.options = OUTP_T_JUST_LEFT | OUTP_T_HORZ | OUTP_T_VERT;
  ls_init (&text.s, s, strlen (s));
  text.h = w;
  text.v = this->font_height;
  text.x = x;
  text.y = y;
  this->class->text_draw (this, &text);
}

/* Draw top of page headers for THIS driver. */
static void
draw_headers (struct outp_driver *this)
{
  struct ps_driver_ext *ext = this->ext;
  
  struct font_entry *old_current = ext->current;
  char *old_family = xstrdup (ext->family); /* FIXME */
  int old_size = ext->size;

  int fh = this->font_height;
  int y = -3 * fh;

  fprintf (ext->file.file, "%d %d %d %d GB%s",
	   0, YT (y), this->width, YT (y + 2 * fh + ext->line_gutter),
	   ext->eol);
  this->class->text_set_font_family (this, "T");

  y += ext->line_width + ext->line_gutter;
  
  {
    int rh_width;
    char buf[128];

    sprintf (buf, _("%s - Page %d"), curdate, ext->page_number);
    rh_width = text_width (this, buf);

    out_text_plain (this, buf, this->width - this->prop_em_width - rh_width,
		    y, rh_width);

    if (outp_title && outp_subtitle)
      out_text_plain (this, outp_title, this->prop_em_width, y,
		      this->width - 3 * this->prop_em_width - rh_width);

    y += fh;
  }
  
  {
    int rh_width;
    char buf[128];
    char *string = outp_subtitle ? outp_subtitle : outp_title;

    sprintf (buf, "%s - %s", version, host_system);
    rh_width = text_width (this, buf);
    
    out_text_plain (this, buf, this->width - this->prop_em_width - rh_width,
		    y, rh_width);

    if (string)
      out_text_plain (this, string, this->prop_em_width, y,
		      this->width - 3 * this->prop_em_width - rh_width);

    y += fh;
  }

  ext->current = old_current;
  free (ext->family);
  ext->family = old_family;
  ext->size = old_size;
}


/* Text. */

static void
ps_text_set_font_by_name (struct outp_driver *this, const char *dit)
{
  struct ps_driver_ext *x = this->ext;
  struct font_entry *fe;

  assert (this->driver_open && this->page_open);
  
  /* Short-circuit common fonts. */
  if (!strcmp (dit, "PROP"))
    {
      x->current = x->prop;
      x->size = x->font_size;
      return;
    }
  else if (!strcmp (dit, "FIXED"))
    {
      x->current = x->fixed;
      x->size = x->font_size;
      return;
    }

  /* Find font_desc corresponding to Groff name dit. */
  fe = hsh_find (x->loaded, &dit);
  if (fe == NULL)
    fe = load_font (this, dit);
  x->current = fe;
}

static void
ps_text_set_font_by_position (struct outp_driver *this, int pos)
{
  struct ps_driver_ext *x = this->ext;
  char *dit;

  assert (this->driver_open && this->page_open);

  /* Determine font name by suffixing position string to font family
     name. */
  {
    char *cp;

    dit = local_alloc (strlen (x->family) + 3);
    cp = stpcpy (dit, x->family);
    switch (pos)
      {
      case OUTP_F_R:
	*cp++ = 'R';
	break;
      case OUTP_F_I:
	*cp++ = 'I';
	break;
      case OUTP_F_B:
	*cp++ = 'B';
	break;
      case OUTP_F_BI:
	*cp++ = 'B';
	*cp++ = 'I';
	break;
      default:
	assert(0);
      }
    *cp++ = 0;
  }
  
  /* Find font_desc corresponding to Groff name dit. */
  {
    struct font_entry *fe = hsh_find (x->loaded, &dit);
    if (fe == NULL)
      fe = load_font (this, dit);
    x->current = fe;
  }

  local_free (dit);
}

static void
ps_text_set_font_family (struct outp_driver *this, const char *s)
{
  struct ps_driver_ext *x = this->ext;

  assert (this->driver_open && this->page_open);
  
  free(x->family);
  x->family = xstrdup (s);
}

static const char *
ps_text_get_font_name (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;

  assert (this->driver_open && this->page_open);
  return x->current->font->name;
}

static const char *
ps_text_get_font_family (struct outp_driver *this)
{
  struct ps_driver_ext *x = this->ext;
  
  assert (this->driver_open && this->page_open);
  return x->family;
}

static int
ps_text_set_size (struct outp_driver *this, int size)
{
  struct ps_driver_ext *x = this->ext;

  assert (this->driver_open && this->page_open);
  x->size = PSUS / 72000 * size;
  return 1;
}

static int
ps_text_get_size (struct outp_driver *this, int *em_width)
{
  struct ps_driver_ext *x = this->ext;

  assert (this->driver_open && this->page_open);
  if (em_width)
    *em_width = (x->current->font->space_width * x->size) / 1000;
  return x->size / (PSUS / 72000);
}

/* An output character. */
struct output_char
  {
    struct font_entry *font;	/* Font of character. */
    int size;			/* Size of character. */
    int x, y;			/* Location of character. */
    unsigned char ch;		/* Character. */
    char separate;		/* Must be separate from previous char. */
  };

/* Hash table comparison function for ps_combo structs. */
static int
compare_ps_combo (const void *pa, const void *pb, void *foo UNUSED)
{
  const struct ps_font_combo *a = pa;
  const struct ps_font_combo *b = pb;

  return !((a->font == b->font) && (a->size == b->size));
}

/* Hash table hash function for ps_combo structs. */
static unsigned
hash_ps_combo (const void *pa, void *foo UNUSED)
{
  const struct ps_font_combo *a = pa;
  unsigned name_hash = hsh_hash_string (a->font->font->internal_name);
  return name_hash ^ hsh_hash_int (a->size);
}

/* Hash table free function for ps_combo structs. */
static void
free_ps_combo (void *a, void *foo UNUSED)
{
  free (a);
}

/* Causes PostScript code to be output that switches to the font
   CP->FONT and font size CP->SIZE.  The first time a particular
   font/size combination is used on a particular page, this involves
   outputting PostScript code to load the font. */
static void
switch_font (struct outp_driver *this, const struct output_char *cp)
{
  struct ps_driver_ext *ext = this->ext;
  struct ps_font_combo srch, **fc;

  srch.font = cp->font;
  srch.size = cp->size;

  fc = (struct ps_font_combo **) hsh_probe (ext->combos, &srch);
  if (*fc)
    {
      fprintf (ext->file.file, "F%x%s", (*fc)->index, ext->eol);
    }
  else
    {
      char *filename;
      struct ps_encoding *encoding;
      char buf[512], *bp;

      *fc = xmalloc (sizeof **fc);
      (*fc)->font = cp->font;
      (*fc)->size = cp->size;
      (*fc)->index = ext->next_combo++;

      filename = find_encoding_file (this, cp->font->font->encoding);
      if (filename)
	{
	  encoding = get_encoding (this, filename);
	  free (filename);
	}
      else
	{
	  msg (IE, _("PostScript driver: Cannot find encoding `%s' for "
	       "PostScript font `%s'."), cp->font->font->encoding,
	       cp->font->font->internal_name);
	  encoding = default_encoding (this);
	}

      if (cp->font != ext->fixed && cp->font != ext->prop)
	{
	  bp = stpcpy (buf, "%%IncludeResource: font ");
	  bp = quote_ps_string (bp, cp->font->font->internal_name);
	  bp = stpcpy (bp, ext->eol);
	}
      else
	bp = buf;

      bp = spprintf (bp, "/F%x E%x %d", (*fc)->index, encoding->index,
		     cp->size);
      bp = quote_ps_name (bp, cp->font->font->internal_name);
      sprintf (bp, " SF%s", ext->eol);
      fputs (buf, ext->file.file);
    }
  ext->last_font = *fc;
}

/* (write_text) Writes the accumulated line buffer to the output
   file. */
#define output_line()				\
	do					\
	  {					\
            lp = stpcpy (lp, ext->eol);		\
	    *lp = 0;				\
	    fputs (line, ext->file.file);	\
	    lp = line;				\
	  }					\
        while (0)

/* (write_text) Adds the string representing number X to the line
   buffer, flushing the buffer to disk beforehand if necessary. */
#define put_number(X)				\
	do					\
	  {					\
	    int n = nsprintf (number, "%d", X);	\
	    if (n + lp > &line[75])		\
	      output_line ();			\
	    lp = stpcpy (lp, number);		\
	  }					\
	while (0)

/* Outputs PostScript code to THIS driver's output file to display the
   characters represented by the output_char's between CP and END,
   using the associated outp_text T to determine formatting.  WIDTH is
   the width of the output region; WIDTH_LEFT is the amount of the
   WIDTH that is not taken up by text (so it can be used to determine
   justification). */
static void
write_text (struct outp_driver *this,
	    const struct output_char *cp, const struct output_char *end,
	    struct outp_text *t, int width UNUSED, int width_left)
{
  struct ps_driver_ext *ext = this->ext;
  int ofs;

  int last_y;

  char number[INT_DIGITS + 1];
  char line[80];
  char *lp;

  switch (t->options & OUTP_T_JUST_MASK)
    {
    case OUTP_T_JUST_LEFT:
      ofs = 0;
      break;
    case OUTP_T_JUST_RIGHT:
      ofs = width_left;
      break;
    case OUTP_T_JUST_CENTER:
      ofs = width_left / 2;
      break;
    default:
      assert (0);
      abort ();
    }

  lp = line;
  last_y = INT_MIN;
  while (cp < end)
    {
      int x = cp->x + ofs;
      int y = cp->y + (cp->font->font->ascent * cp->size / 1000);

      if (ext->last_font == NULL
	  || cp->font != ext->last_font->font
	  || cp->size != ext->last_font->size)
	switch_font (this, cp);

      *lp++ = '(';
      do
	{
	  /* PORTME! */
	  static unsigned char literal_chars[ODA_COUNT][32] =
	  {
	    {0x00, 0x00, 0x00, 0xf8, 0xff, 0xfc, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
	     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	    },
	    {0x00, 0x00, 0x00, 0xf8, 0xff, 0xfc, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	    },
	    {0x7e, 0xd6, 0xff, 0xfb, 0xff, 0xfc, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	    }
	  };

	  if (TEST_BIT (literal_chars[ext->data], cp->ch))
	    *lp++ = cp->ch;
	  else
	    switch (cp->ch)
	      {
	      case '(':
		lp = stpcpy (lp, "\\(");
		break;
	      case ')':
		lp = stpcpy (lp, "\\)");
		break;
	      default:
		lp = spprintf (lp, "\\%03o", cp->ch);
		break;
	      }
	  cp++;
	}
      while (cp < end && lp < &line[70] && cp->separate == 0);
      *lp++ = ')';

      put_number (x);

      if (y != last_y)
	{
	  *lp++ = ' ';
	  put_number (YT (y));
	  *lp++ = ' ';
	  *lp++ = 'S';
	  last_y = y;
	}
      else
	{
	  *lp++ = ' ';
	  *lp++ = 'T';
	}

      if (lp >= &line[70])
	output_line ();
    }
  if (lp != line)
    output_line ();
}

#undef output_line
#undef put_number

/* Displays the text in outp_text T, if DRAW is nonzero; or, merely
   determine the text metrics, if DRAW is zero. */
static void
text (struct outp_driver *this, struct outp_text *t, int draw)
{
  struct ps_driver_ext *ext = this->ext;

  /* Output. */
  struct output_char *buf;	/* Output buffer. */
  struct output_char *buf_end;	/* End of output buffer. */
  struct output_char *buf_loc;	/* Current location in output buffer. */

  /* Saved state. */
  struct font_entry *old_current = ext->current;
  char *old_family = xstrdup (ext->family); /* FIXME */
  int old_size = ext->size;

  /* Input string. */
  char *cp, *end;

  /* Current location. */
  int x, y;

  /* Keeping track of what's left over. */
  int width;			/* Width available for characters. */
  int width_left, height_left;	/* Width, height left over. */
  int max_height;		/* Tallest character on this line so far. */

  /* Previous character. */
  int prev_char;

  /* Information about location of previous space. */
  char *space_char;		/* Character after space. */
  struct output_char *space_buf_loc; /* Buffer location after space. */
  int space_width_left;		/* Width of characters before space. */

  /* Name of the current character. */
  const char *char_name;
  char local_char_name[2] = {0, 0};

  local_char_name[0] = local_char_name[1] = 0;

  buf = local_alloc (sizeof *buf * 128);
  buf_end = &buf[128];
  buf_loc = buf;

  assert (!ls_null_p (&t->s));
  cp = ls_c_str (&t->s);
  end = ls_end (&t->s);
  if (draw)
    {
      x = t->x;
      y = t->y;
    }
  else
    x = y = 0;
  width = width_left = (t->options & OUTP_T_HORZ) ? t->h : INT_MAX;
  height_left = (t->options & OUTP_T_VERT) ? t->v : INT_MAX;
  max_height = 0;
  prev_char = -1;
  space_char = NULL;
  space_buf_loc = NULL;
  space_width_left = 0;
  

  if (!width || !height_left)
    goto exit;

  while (cp < end)
    {
      struct char_metrics *metric;
      int cur_char;
      int kern_amt;
      int char_width;
      int separate = 0;

      /* Set char_name to the name of the character or ligature at
         *cp. */
      local_char_name[0] = *cp;
      char_name = local_char_name;
      if (ext->current->font->ligatures && *cp == 'f')
	{
	  int lig = 0;
          char_name = NULL;

	  if (cp < end - 1)
	    switch (cp[1])
	      {
	      case 'i':
		lig = LIG_fi, char_name = "fi";
		break;
	      case 'l':
		lig = LIG_fl, char_name = "fl";
		break;
	      case 'f':
		if (cp < end - 2)
		  switch (cp[2])
		    {
		    case 'i':
		      lig = LIG_ffi, char_name = "ffi";
		      goto got_ligature;
		    case 'l':
		      lig = LIG_ffl, char_name = "ffl";
		      goto got_ligature;
		    }
		lig = LIG_ff, char_name = "ff";
	      got_ligature:
		break;
	      }
	  if ((lig & ext->current->font->ligatures) == 0)
	    {
	      local_char_name[0] = *cp;	/* 'f' */
	      char_name = local_char_name;
	    }
	}
      else if (*cp == '\n')
	{
	  if (draw)
	    {
	      write_text (this, buf, buf_loc, t, width, width_left);
	      buf_loc = buf;
	      x = t->x;
	      y += max_height;
	    }

	  width_left = width;
	  height_left -= max_height;
	  max_height = 0;
	  kern_amt = 0;
	  separate = 1;
	  cp++;

	  /* FIXME: when we're page buffering it will be necessary to
	     set separate to 1. */
	  continue;
	}
      cp += strlen (char_name);

      /* Figure out what size this character is, and what kern
         adjustment we need. */
      cur_char = font_char_name_to_index (char_name);
      metric = font_get_char_metrics (ext->current->font, cur_char);
      if (!metric)
	{
	  static struct char_metrics m;
	  metric = &m;
	  m.width = ext->current->font->space_width;
	  m.code = *char_name;
	}
      kern_amt = font_get_kern_adjust (ext->current->font, prev_char,
				       cur_char);
      if (kern_amt)
	{
	  kern_amt = (kern_amt * ext->size / 1000);
	  separate = 1;
	}
      char_width = metric->width * ext->size / 1000;

      /* Record the current status if this is a space character. */
      if (cur_char == space_index && buf_loc > buf)
	{
	  space_char = cp;
	  space_buf_loc = buf_loc;
	  space_width_left = width_left;
	}

      /* Drop down to a new line if there's no room left on this
         line. */
      if (char_width + kern_amt > width_left)
	{
	  /* Regress to previous space, if any. */
	  if (space_char)
	    {
	      cp = space_char;
	      width_left = space_width_left;
	      buf_loc = space_buf_loc;
	    }

	  if (draw)
	    {
	      write_text (this, buf, buf_loc, t, width, width_left);
	      buf_loc = buf;
	      x = t->x;
	      y += max_height;
	    }

	  width_left = width;
	  height_left -= max_height;
	  max_height = 0;
	  kern_amt = 0;

	  if (space_char)
	    {
	      space_char = NULL;
	      prev_char = -1;
	      /* FIXME: when we're page buffering it will be
	         necessary to set separate to 1. */
	      continue;
	    }
	  separate = 1;
	}
      if (ext->size > max_height)
	max_height = ext->size;
      if (max_height > height_left)
	goto exit;

      /* Actually draw the character. */
      if (draw)
	{
	  if (buf_loc >= buf_end)
	    {
	      int buf_len = buf_end - buf;

	      if (buf_len == 128)
		{
		  struct output_char *new_buf;

		  new_buf = xmalloc (sizeof *new_buf * 256);
		  memcpy (new_buf, buf, sizeof *new_buf * 128);
		  buf_loc = new_buf + 128;
		  buf_end = new_buf + 256;
		  local_free (buf);
		  buf = new_buf;
		}
	      else
		{
		  buf = xrealloc (buf, sizeof *buf * buf_len * 2);
		  buf_loc = buf + buf_len;
		  buf_end = buf + buf_len * 2;
		}
	    }

	  x += kern_amt;
	  buf_loc->font = ext->current;
	  buf_loc->size = ext->size;
	  buf_loc->x = x;
	  buf_loc->y = y;
	  buf_loc->ch = metric->code;
	  buf_loc->separate = separate;
	  buf_loc++;
	  x += char_width;
	}

      /* Prepare for next iteration. */
      width_left -= char_width + kern_amt;
      prev_char = cur_char;
    }
  height_left -= max_height;
  if (buf_loc > buf && draw)
    write_text (this, buf, buf_loc, t, width, width_left);

exit:
  if (!(t->options & OUTP_T_HORZ))
    t->h = INT_MAX - width_left;
  if (!(t->options & OUTP_T_VERT))
    t->v = INT_MAX - height_left;
  else
    t->v -= height_left;
  if (buf_end - buf == 128)
    local_free (buf);
  else
    free (buf);
  ext->current = old_current;
  free (ext->family);
  ext->family = old_family;
  ext->size = old_size;
}

static void
ps_text_metrics (struct outp_driver *this, struct outp_text *t)
{
  assert (this->driver_open && this->page_open);
  text (this, t, 0);
}

static void
ps_text_draw (struct outp_driver *this, struct outp_text *t)
{
  assert (this->driver_open && this->page_open);
  text (this, t, 1);
}

/* Font loader. */

/* Translate a filename to a font. */
struct filename2font
  {
    char *filename;		/* Normalized filename. */
    struct font_desc *font;
  };

/* Table of `filename2font's. */
static struct hsh_table *ps_fonts;

/* Hash table comparison function for filename2font structs. */
static int
compare_filename2font (const void *a, const void *b, void *param UNUSED)
{
  return strcmp (((struct filename2font *) a)->filename,
		 ((struct filename2font *) b)->filename);
}

/* Hash table hash function for filename2font structs. */
static unsigned
hash_filename2font (const void *f2f_, void *param UNUSED)
{
  const struct filename2font *f2f = f2f_;
  return hsh_hash_string (f2f->filename);
}

/* Initializes the global font list by creating the hash table for
   translation of filenames to font_desc structs. */
static void
init_fonts (void)
{
  ps_fonts = hsh_create (31, compare_filename2font, hash_filename2font,
			 NULL, NULL);
}

static void
done_fonts (void)
{
 hsh_destroy (ps_fonts);
}

/* Loads the font having Groff name DIT into THIS driver instance.
   Specifically, adds it into the THIS driver's `loaded' hash
   table. */
static struct font_entry *
load_font (struct outp_driver *this, const char *dit)
{
  struct ps_driver_ext *x = this->ext;
  char *filename1, *filename2;
  void **entry;
  struct font_entry *fe;

  filename1 = find_ps_file (this, dit);
  if (!filename1)
    filename1 = xstrdup (dit);
  filename2 = fn_normalize (filename1);
  free (filename1);

  entry = hsh_probe (ps_fonts, &filename2);
  if (*entry == NULL)
    {
      struct filename2font *f2f;
      struct font_desc *f = groff_read_font (filename2);

      if (f == NULL)
	{
	  if (x->fixed)
	    f = x->fixed->font;
	  else
	    f = default_font ();
	}
      
      f2f = xmalloc (sizeof *f2f);
      f2f->filename = filename2;
      f2f->font = f;
      *entry = f2f;
    }
  else
    free (filename2);

  fe = xmalloc (sizeof *fe);
  fe->dit = xstrdup (dit);
  fe->font = ((struct filename2font *) * entry)->font;
  *hsh_probe (x->loaded, &dit) = fe;

  return fe;
}

static void
ps_chart_initialise (struct outp_driver *this UNUSED, struct chart *ch)
{
#ifdef NO_CHARTS
  ch->lp = NULL;
#else
  struct ps_driver_ext *x = this->ext;
  char page_size[128];
  int size;
  int x_origin, y_origin;

  ch->file = tmpfile ();
  if (ch->file == NULL) 
    {
      ch->lp = NULL;
      return;
    }
  
  size = this->width < this->length ? this->width : this->length;
  x_origin = x->left_margin + (size - this->width) / 2;
  y_origin = x->bottom_margin + (size - this->length) / 2;

  snprintf (page_size, sizeof page_size,
            "a,xsize=%.3f,ysize=%.3f,xorigin=%.3f,yorigin=%.3f",
            (double) size / PSUS, (double) size / PSUS,
            (double) x_origin / PSUS, (double) y_origin / PSUS);

  ch->pl_params = pl_newplparams ();
  pl_setplparam (ch->pl_params, "PAGESIZE", page_size);
  ch->lp = pl_newpl_r ("ps", NULL, ch->file, stderr, ch->pl_params);
#endif
}

static void 
ps_chart_finalise (struct outp_driver *this UNUSED, struct chart *ch UNUSED)
{
#ifndef NO_CHARTS
  struct ps_driver_ext *x = this->ext;
  char buf[BUFSIZ];
  static int doc_num = 0;

  if (this->page_open) 
    {
      this->class->close_page (this);
      this->page_open = 0; 
    }
  this->class->open_page (this);
  fprintf (x->file.file,
           "/sp save def%s"
           "%d %d translate 1000 dup scale%s"
           "userdict begin%s"
           "/showpage { } def%s"
           "0 setgray 0 setlinecap 1 setlinewidth%s"
           "0 setlinejoin 10 setmiterlimit [ ] 0 setdash newpath clear%s"
           "%%%%BeginDocument: %d%s",
           x->eol,
           -x->left_margin, -x->bottom_margin, x->eol,
           x->eol,
           x->eol,
           x->eol,
           x->eol,
           doc_num++, x->eol);

  rewind (ch->file);
  while (fwrite (buf, 1, fread (buf, 1, sizeof buf, ch->file), x->file.file))
    continue;
  fclose (ch->file);

  fprintf (x->file.file,
           "%%%%EndDocument%s"
           "end%s"
           "sp restore%s",
           x->eol,
           x->eol,
           x->eol);
  this->class->close_page (this);
  this->page_open = 0;
#endif
}

/* PostScript driver class. */
struct outp_class postscript_class =
{
  "postscript",
  MAGIC_PS,
  0,

  ps_open_global,
  ps_close_global,
  ps_font_sizes,

  ps_preopen_driver,
  ps_option,
  ps_postopen_driver,
  ps_close_driver,

  ps_open_page,
  ps_close_page,

  ps_submit,

  ps_line_horz,
  ps_line_vert,
  ps_line_intersection,

  ps_box,
  ps_polyline_begin,
  ps_polyline_point,
  ps_polyline_end,

  ps_text_set_font_by_name,
  ps_text_set_font_by_position,
  ps_text_set_font_family,
  ps_text_get_font_name,
  ps_text_get_font_family,
  ps_text_set_size,
  ps_text_get_size,
  ps_text_metrics,
  ps_text_draw,

  ps_chart_initialise,
  ps_chart_finalise
};

/* EPSF driver class.  FIXME: Probably doesn't work right. */
struct outp_class epsf_class =
{
  "epsf",
  MAGIC_EPSF,
  0,

  ps_open_global,
  ps_close_global,
  ps_font_sizes,

  ps_preopen_driver,
  ps_option,
  ps_postopen_driver,
  ps_close_driver,

  ps_open_page,
  ps_close_page,

  ps_submit,

  ps_line_horz,
  ps_line_vert,
  ps_line_intersection,

  ps_box,
  ps_polyline_begin,
  ps_polyline_point,
  ps_polyline_end,

  ps_text_set_font_by_name,
  ps_text_set_font_by_position,
  ps_text_set_font_family,
  ps_text_get_font_name,
  ps_text_get_font_family,
  ps_text_set_size,
  ps_text_get_size,
  ps_text_metrics,
  ps_text_draw,

  ps_chart_initialise,
  ps_chart_finalise

};

#endif /* NO_POSTSCRIPT */
