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
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include "alloc.h"
#include "error.h"
#include "filename.h"
#include "main.h"
#include "misc.h"
#include "output.h"
#include "pool.h"
#include "version.h"

/* ASCII driver options: (defaults listed first)

   output-file="pspp.list"
   char-set=ascii|latin1
   form-feed-string="\f"        Written as a formfeed.
   newline-string=default|"\r\n"|"\n"   
                                Written as a newline.
   paginate=on|off              Formfeeds are desired?
   tab-width=8                  Width of a tab; 0 to not use tabs.
   init=""                      Written at beginning of output.
   done=""                      Written at end of output.
   
   headers=on|off               Put headers at top of page?
   length=66
   width=130
   lpi=6                        Only used to determine font size.
   cpi=10                       
   squeeze=off|on               Squeeze multiple newlines into exactly one.

   left-margin=0
   right-margin=0
   top-margin=2
   bottom-margin=2

   box[x]="strng"               Sets box character X (X in base 4: 0-3333).
   italic-on=overstrike|"strng" Turns on italic (underline).
   italic-off=""|"strng"        Turns off italic; ignored for overstrike.
   bold-on=overstrike|"strng"   Turns on bold.
   bold-off=""|"strng"          Turns off bold; ignored for overstrike.
   bold-italic-on=overstrike|"strng" Turns on bold-italic.
   bold-italic-off=""|"strng"   Turns off bold-italic; ignored for overstrike.
   overstrike-style=single|line Can we print a whole line then BS over it, or
   must we go char by char, as on a terminal?
   carriage-return-style=bs|cr  Must we return the carriage with a sequence of
   BSes, or will a single CR do it?
 */

/* Disable messages by failed range checks. */
/*#define SUPPRESS_WARNINGS 1 */

/* Character set. */
enum
  {
    CHS_ASCII,			/* 7-bit ASCII */
    CHS_LATIN1			/* Latin 1; not really supported at the moment */
  };

/* Overstrike style. */
enum
  {
    OVS_SINGLE,			/* Overstrike each character: "a\b_b\b_c\b_" */
    OVS_LINE			/* Overstrike lines: "abc\b\b\b___" (or if
				   newline is "\r\n", then "abc\r___").  Easier
				   on the printer, doesn't work on a tty. */
  };

/* Basic output strings. */
enum
  {
    OPS_INIT,			/* Document initialization string. */
    OPS_DONE,			/* Document uninit string. */
    OPS_FORMFEED,		/* Formfeed string. */
    OPS_NEWLINE,		/* Newline string. */

    OPS_COUNT			/* Number of output strings. */
  };

/* Line styles bit shifts. */
enum
  {
    LNS_TOP = 0,
    LNS_LEFT = 2,
    LNS_BOTTOM = 4,
    LNS_RIGHT = 6,

    LNS_COUNT = 256
  };

/* Carriage return style. */
enum
  {
    CRS_BS,			/* Multiple backspaces. */
    CRS_CR			/* Single carriage return. */
  };

/* Assembles a byte from four taystes. */
#define TAYSTE2BYTE(T, L, B, R)			\
	(((T) << LNS_TOP)			\
	 | ((L) << LNS_LEFT)			\
	 | ((B) << LNS_BOTTOM)			\
	 | ((R) << LNS_RIGHT))

/* Extract tayste with shift value S from byte B. */
#define BYTE2TAYSTE(B, S) 			\
	(((B) >> (S)) & 3)

/* Font style; take one of the first group |'d with one of the second group. */
enum
  {
    FSTY_ON = 000,		/* Turn font on. */
    FSTY_OFF = 001,		/* Turn font off. */

    FSTY_ITALIC = 0,		/* Italic font. */
    FSTY_BOLD = 2,		/* Bold font. */
    FSTY_BOLD_ITALIC = 4,	/* Bold-italic font. */

    FSTY_COUNT = 6		/* Number of font styles. */
  };

/* ASCII output driver extension record. */
struct ascii_driver_ext
  {
    /* User parameters. */
    int char_set;		/* CHS_ASCII/CHS_LATIN1; no-op right now. */
    int headers;		/* 1=print headers at top of page. */
    int page_length;		/* Page length in lines. */
    int page_width;		/* Page width in characters. */
    int lpi;			/* Lines per inch. */
    int cpi;			/* Characters per inch. */
    int left_margin;		/* Left margin in characters. */
    int right_margin;		/* Right margin in characters. */
    int top_margin;		/* Top margin in lines. */
    int bottom_margin;		/* Bottom margin in lines. */
    int paginate;		/* 1=insert formfeeds. */
    int tab_width;		/* Width of a tab; 0 not to use tabs. */
    struct len_string ops[OPS_COUNT]; /* Basic output strings. */
    struct len_string box[LNS_COUNT]; /* Line & box drawing characters. */
    struct len_string fonts[FSTY_COUNT]; /* Font styles; NULL=overstrike. */
    int overstrike_style;	/* OVS_SINGLE or OVS_LINE. */
    int carriage_return_style;	/* Carriage return style. */
    int squeeze_blank_lines;    /* 1=squeeze multiple blank lines into one. */

    /* Internal state. */
    struct file_ext file;	/* Output file. */
    int page_number;		/* Current page number. */
    unsigned short *page;	/* Page content. */
    int page_size;		/* Number of bytes allocated for page, attr. */
    int *line_len;		/* Length of each line in page, attr. */
    int line_len_size;		/* Number of ints allocated for line_len. */
    int w, l;			/* Actual width & length w/o margins, etc. */
    int n_output;		/* Number of lines output so far. */
    int cur_font;		/* Current font by OUTP_F_*. */
#if GLOBAL_DEBUGGING
    int debug;			/* Set by som_text_draw(). */
#endif
  };

static struct pool *ascii_pool;

static int postopen (struct file_ext *);
static int preclose (struct file_ext *);

static int
ascii_open_global (struct outp_class *this UNUSED)
{
  ascii_pool = pool_create ();
  return 1;
}

static int
ascii_close_global (struct outp_class *this UNUSED)
{
  pool_destroy (ascii_pool);
  return 1;
}

static int *
ascii_font_sizes (struct outp_class *this UNUSED, int *n_valid_sizes)
{
  static int valid_sizes[] = {12, 12, 0, 0};

  assert (n_valid_sizes);
  *n_valid_sizes = 1;
  return valid_sizes;
}

static int
ascii_preopen_driver (struct outp_driver *this)
{
  struct ascii_driver_ext *x;
  int i;
  
  assert (this->driver_open == 0);
  msg (VM (1), _("ASCII driver initializing as `%s'..."), this->name);
  this->ext = x = xmalloc (sizeof (struct ascii_driver_ext));
  x->char_set = CHS_ASCII;
  x->headers = 1;
  x->page_length = 66;
  x->page_width = 79;
  x->lpi = 6;
  x->cpi = 10;
  x->left_margin = 0;
  x->right_margin = 0;
  x->top_margin = 2;
  x->bottom_margin = 2;
  x->paginate = 1;
  x->tab_width = 8;
  for (i = 0; i < OPS_COUNT; i++)
    ls_null (&x->ops[i]);
  for (i = 0; i < LNS_COUNT; i++)
    ls_null (&x->box[i]);
  for (i = 0; i < FSTY_COUNT; i++)
    ls_null (&x->fonts[i]);
  x->overstrike_style = OVS_SINGLE;
  x->carriage_return_style = CRS_BS;
  x->squeeze_blank_lines = 0;
  x->file.filename = NULL;
  x->file.mode = "wb";
  x->file.file = NULL;
  x->file.sequence_no = &x->page_number;
  x->file.param = x;
  x->file.postopen = postopen;
  x->file.preclose = preclose;
  x->page_number = 0;
  x->page = NULL;
  x->page_size = 0;
  x->line_len = NULL;
  x->line_len_size = 0;
  x->n_output = 0;
  x->cur_font = OUTP_F_R;
#if GLOBAL_DEBUGGING
  x->debug = 0;
#endif
  return 1;
}

static int
ascii_postopen_driver (struct outp_driver *this)
{
  struct ascii_driver_ext *x = this->ext;
  
  assert (this->driver_open == 0);
  
  if (NULL == x->file.filename)
    x->file.filename = xstrdup ("pspp.list");
  
  x->w = x->page_width - x->left_margin - x->right_margin;
  x->l = (x->page_length - (x->headers ? 3 : 0) - x->top_margin
	  - x->bottom_margin - 1);
  if (x->w < 59 || x->l < 15)
    {
      msg (SE, _("ascii driver: Area of page excluding margins and headers "
		 "must be at least 59 characters wide by 15 lines long.  Page as "
		 "configured is only %d characters by %d lines."), x->w, x->l);
      return 0;
    }
  
  this->res = x->lpi * x->cpi;
  this->horiz = x->lpi;
  this->vert = x->cpi;
  this->width = x->w * this->horiz;
  this->length = x->l * this->vert;
  
  if (ls_null_p (&x->ops[OPS_FORMFEED]))
    ls_create (ascii_pool, &x->ops[OPS_FORMFEED], "\f");
  if (ls_null_p (&x->ops[OPS_NEWLINE])
      || !strcmp (ls_value (&x->ops[OPS_NEWLINE]), "default"))
    {
      ls_create (ascii_pool, &x->ops[OPS_NEWLINE], "\n");
      x->file.mode = "wt";
    }
  
  {
    int i;
    
    for (i = 0; i < LNS_COUNT; i++)
      {
	char c[2];
	c[1] = 0;
	if (!ls_null_p (&x->box[i]))
	  continue;
	switch (i)
	  {
	  case TAYSTE2BYTE (0, 0, 0, 0):
	    c[0] = ' ';
	    break;

	  case TAYSTE2BYTE (0, 1, 0, 0):
	  case TAYSTE2BYTE (0, 1, 0, 1):
	  case TAYSTE2BYTE (0, 0, 0, 1):
	    c[0] = '-';
	    break;

	  case TAYSTE2BYTE (1, 0, 0, 0):
	  case TAYSTE2BYTE (1, 0, 1, 0):
	  case TAYSTE2BYTE (0, 0, 1, 0):
	    c[0] = '|';
	    break;

	  case TAYSTE2BYTE (0, 3, 0, 0):
	  case TAYSTE2BYTE (0, 3, 0, 3):
	  case TAYSTE2BYTE (0, 0, 0, 3):
	  case TAYSTE2BYTE (0, 2, 0, 0):
	  case TAYSTE2BYTE (0, 2, 0, 2):
	  case TAYSTE2BYTE (0, 0, 0, 2):
	    c[0] = '=';
	    break;

	  case TAYSTE2BYTE (3, 0, 0, 0):
	  case TAYSTE2BYTE (3, 0, 3, 0):
	  case TAYSTE2BYTE (0, 0, 3, 0):
	  case TAYSTE2BYTE (2, 0, 0, 0):
	  case TAYSTE2BYTE (2, 0, 2, 0):
	  case TAYSTE2BYTE (0, 0, 2, 0):
	    c[0] = '#';
	    break;

	  default:
	    if (BYTE2TAYSTE (i, LNS_LEFT) > 1
		|| BYTE2TAYSTE (i, LNS_TOP) > 1
		|| BYTE2TAYSTE (i, LNS_RIGHT) > 1
		|| BYTE2TAYSTE (i, LNS_BOTTOM) > 1)
	      c[0] = '#';
	    else
	      c[0] = '+';
	    break;
	  }
	ls_create (ascii_pool, &x->box[i], c);
      }
  }
  
  {
    int i;
    
    this->cp_x = this->cp_y = 0;
    this->font_height = this->vert;
    this->prop_em_width = this->horiz;
    this->fixed_width = this->horiz;

    this->horiz_line_width[0] = 0;
    this->vert_line_width[0] = 0;
    
    for (i = 1; i < OUTP_L_COUNT; i++)
      {
	this->horiz_line_width[i] = this->vert;
	this->vert_line_width[i] = this->horiz;
      }
    
    for (i = 0; i < (1 << OUTP_L_COUNT); i++)
      {
	this->horiz_line_spacing[i] = (i & ~1) ? this->vert : 0;
	this->vert_line_spacing[i] = (i & ~1) ? this->horiz : 0;
      }
  }
  
  this->driver_open = 1;
  msg (VM (2), _("%s: Initialization complete."), this->name);

  return 1;
}

static int
ascii_close_driver (struct outp_driver *this)
{
  struct ascii_driver_ext *x = this->ext;
  
  assert (this->driver_open == 1);
  msg (VM (2), _("%s: Beginning closing..."), this->name);
  
  x = this->ext;
  free (x->page);
  free (x->line_len);
  fn_close_ext (&x->file);
  free (x->file.filename);
  free (x);
  
  this->driver_open = 0;
  msg (VM (3), _("%s: Finished closing."), this->name);
  
  return 1;
}

/* Generic option types. */
enum
  {
    pos_int_arg = -10,
    nonneg_int_arg,
    string_arg,
    font_string_arg,
    boolean_arg
  };

static struct outp_option option_tab[] =
  {
    {"headers", boolean_arg, 0},
    {"output-file", 1, 0},
    {"char-set", 2, 0},
    {"length", pos_int_arg, 0},
    {"width", pos_int_arg, 1},
    {"lpi", pos_int_arg, 2},
    {"cpi", pos_int_arg, 3},
    {"init", string_arg, 0},
    {"done", string_arg, 1},
    {"left-margin", nonneg_int_arg, 0},
    {"right-margin", nonneg_int_arg, 1},
    {"top-margin", nonneg_int_arg, 2},
    {"bottom-margin", nonneg_int_arg, 3},
    {"paginate", boolean_arg, 1},
    {"form-feed-string", string_arg, 2},
    {"newline-string", string_arg, 3},
    {"italic-on", font_string_arg, 0},
    {"italic-off", font_string_arg, 1},
    {"bold-on", font_string_arg, 2},
    {"bold-off", font_string_arg, 3},
    {"bold-italic-on", font_string_arg, 4},
    {"bold-italic-off", font_string_arg, 5},
    {"overstrike-style", 3, 0},
    {"tab-width", nonneg_int_arg, 4},
    {"carriage-return-style", 4, 0},
    {"squeeze", boolean_arg, 2},
    {"", 0, 0},
  };
static struct outp_option_info option_info;

static void
ascii_option (struct outp_driver *this, const char *key,
	      const struct string *val)
{
  struct ascii_driver_ext *x = this->ext;
  int cat, subcat;
  const char *value;

  value = ds_value (val);
  if (!strncmp (key, "box[", 4))
    {
      char *tail;
      int indx = strtol (&key[4], &tail, 4);
      if (*tail != ']' || indx < 0 || indx > LNS_COUNT)
	{
	  msg (SE, _("Bad index value for `box' key: syntax is box[INDEX], "
	       "0 <= INDEX < %d decimal, with INDEX expressed in base 4."),
	       LNS_COUNT);
	  return;
	}
      if (!ls_null_p (&x->box[indx]))
	msg (SW, _("Duplicate value for key `%s'."), key);
      ls_create (ascii_pool, &x->box[indx], value);
      return;
    }

  cat = outp_match_keyword (key, option_tab, &option_info, &subcat);
  switch (cat)
    {
    case 0:
      msg (SE, _("Unknown configuration parameter `%s' for ascii device driver."),
	   key);
      break;
    case 1:
      free (x->file.filename);
      x->file.filename = xstrdup (value);
      break;
    case 2:
      if (!strcmp (value, "ascii"))
	x->char_set = CHS_ASCII;
      else if (!strcmp (value, "latin1"))
	x->char_set = CHS_LATIN1;
      else
	msg (SE, _("Unknown character set `%s'.  Valid character sets are "
	     "`ascii' and `latin1'."), value);
      break;
    case 3:
      if (!strcmp (value, "single"))
	x->overstrike_style = OVS_SINGLE;
      else if (!strcmp (value, "line"))
	x->overstrike_style = OVS_LINE;
      else
	msg (SE, _("Unknown overstrike style `%s'.  Valid overstrike styles "
	     "are `single' and `line'."), value);
      break;
    case 4:
      if (!strcmp (value, "bs"))
	x->carriage_return_style = CRS_BS;
      else if (!strcmp (value, "cr"))
	x->carriage_return_style = CRS_CR;
      else
	msg (SE, _("Unknown carriage return style `%s'.  Valid carriage "
	     "return styles are `cr' and `bs'."), value);
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
	switch (subcat)
	  {
	  case 0:
	    x->page_length = arg;
	    break;
	  case 1:
	    x->page_width = arg;
	    break;
	  case 2:
	    x->lpi = arg;
	    break;
	  case 3:
	    x->cpi = arg;
	    break;
	  default:
	    assert (0);
	  }
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
	    msg (SE, _("Zero or positive integer required as value for `%s'."),
		 key);
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
	    x->tab_width = arg;
	    break;
	  default:
	    assert (0);
	  }
      }
      break;
    case string_arg:
      {
	struct len_string *s;
	switch (subcat)
	  {
	  case 0:
	    s = &x->ops[OPS_INIT];
	    break;
	  case 1:
	    s = &x->ops[OPS_DONE];
	    break;
	  case 2:
	    s = &x->ops[OPS_FORMFEED];
	    break;
	  case 3:
	    s = &x->ops[OPS_NEWLINE];
	    break;
	  default:
	    assert (0);
	  }
	ls_create (ascii_pool, s, value);
      }
      break;
    case font_string_arg:
      {
	if (!strcmp (value, "overstrike"))
	  {
	    ls_destroy (ascii_pool, &x->fonts[subcat]);
	    return;
	  }
	ls_create (ascii_pool, &x->fonts[subcat], value);
      }
      break;
    case boolean_arg:
      {
	int setting;
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
	    x->headers = setting;
	    break;
	  case 1:
	    x->paginate = setting;
	    break;
          case 2:
            x->squeeze_blank_lines = setting;
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

int
postopen (struct file_ext *f)
{
  struct ascii_driver_ext *x = f->param;
  struct len_string *s = &x->ops[OPS_INIT];

  if (!ls_empty_p (s) && fwrite (ls_value (s), ls_length (s), 1, f->file) < 1)
    {
      msg (ME, _("ASCII output driver: %s: %s"),
	   f->filename, strerror (errno));
      return 0;
    }
  return 1;
}

int
preclose (struct file_ext *f)
{
  struct ascii_driver_ext *x = f->param;
  struct len_string *d = &x->ops[OPS_DONE];

  if (!ls_empty_p (d) && fwrite (ls_value (d), ls_length (d), 1, f->file) < 1)
    {
      msg (ME, _("ASCII output driver: %s: %s"),
	   f->filename, strerror (errno));
      return 0;
    }
  return 1;
}

static int
ascii_open_page (struct outp_driver *this)
{
  struct ascii_driver_ext *x = this->ext;
  int req_page_size;

  assert (this->driver_open && !this->page_open);
  x->page_number++;
  if (!fn_open_ext (&x->file))
    {
      msg (ME, _("ASCII output driver: %s: %s"), x->file.filename,
	   strerror (errno));
      return 0;
    }

  req_page_size = x->w * x->l;
  if (req_page_size > x->page_size || req_page_size / 2 < x->page_size)
    {
      x->page_size = req_page_size;
      x->page = xrealloc (x->page, sizeof *x->page * req_page_size);
    }

  if (x->l > x->line_len_size)
    {
      x->line_len_size = x->l;
      x->line_len = xrealloc (x->line_len,
			      sizeof *x->line_len * x->line_len_size);
    }

  memset (x->line_len, 0, sizeof *x->line_len * x->l);

  this->page_open = 1;
  return 1;
}

/* Ensures that at least the first L characters of line I in the
   driver identified by struct ascii_driver_ext *X have been cleared out. */
static inline void
expand_line (struct ascii_driver_ext *x, int i, int l)
{
  int limit = i * x->w + l;
  int j;

  for (j = i * x->w + x->line_len[i]; j < limit; j++)
    x->page[j] = ' ';
  x->line_len[i] = l;
}

/* Puts line L at (H,K) in the current output page.  Assumes
   struct ascii_driver_ext named `ext'. */
#define draw_line(H, K, L) 				\
	ext->page[ext->w * (K) + (H)] = (L) | 0x800

/* Line styles for each position. */
#define T(STYLE) (STYLE<<LNS_TOP)
#define L(STYLE) (STYLE<<LNS_LEFT)
#define B(STYLE) (STYLE<<LNS_BOTTOM)
#define R(STYLE) (STYLE<<LNS_RIGHT)

static void
ascii_line_horz (struct outp_driver *this, const struct rect *r,
		 const struct color *c UNUSED, int style)
{
  struct ascii_driver_ext *ext = this->ext;
  int x1 = r->x1 / this->horiz;
  int x2 = r->x2 / this->horiz;
  int y1 = r->y1 / this->vert;
  int x;

  assert (this->driver_open && this->page_open);
  if (x1 == x2)
    return;
#if GLOBAL_DEBUGGING
  if (x1 > x2
      || x1 < 0 || x1 >= ext->w
      || x2 <= 0 || x2 > ext->w
      || y1 < 0 || y1 >= ext->l)
    {
#if !SUPPRESS_WARNINGS
      printf (_("ascii_line_horz: bad hline (%d,%d),%d out of (%d,%d)\n"),
	      x1, x2, y1, ext->w, ext->l);
#endif
      return;
    }
#endif

  if (ext->line_len[y1] < x2)
    expand_line (ext, y1, x2);

  for (x = x1; x < x2; x++)
    draw_line (x, y1, (style << LNS_LEFT) | (style << LNS_RIGHT));
}

static void
ascii_line_vert (struct outp_driver *this, const struct rect *r,
		 const struct color *c UNUSED, int style)
{
  struct ascii_driver_ext *ext = this->ext;
  int x1 = r->x1 / this->horiz;
  int y1 = r->y1 / this->vert;
  int y2 = r->y2 / this->vert;
  int y;

  assert (this->driver_open && this->page_open);
  if (y1 == y2)
    return;
#if GLOBAL_DEBUGGING
  if (y1 > y2
      || x1 < 0 || x1 >= ext->w
      || y1 < 0 || y1 >= ext->l
      || y2 < 0 || y2 > ext->l)
    {
#if !SUPPRESS_WARNINGS
      printf (_("ascii_line_vert: bad vline %d,(%d,%d) out of (%d,%d)\n"),
	      x1, y1, y2, ext->w, ext->l);
#endif
      return;
    }
#endif

  for (y = y1; y < y2; y++)
    if (ext->line_len[y] <= x1)
      expand_line (ext, y, x1 + 1);

  for (y = y1; y < y2; y++)
    draw_line (x1, y, (style << LNS_TOP) | (style << LNS_BOTTOM));
}

static void
ascii_line_intersection (struct outp_driver *this, const struct rect *r,
			 const struct color *c UNUSED,
			 const struct outp_styles *style)
{
  struct ascii_driver_ext *ext = this->ext;
  int x = r->x1 / this->horiz;
  int y = r->y1 / this->vert;
  int l;

  assert (this->driver_open && this->page_open);
#if GLOBAL_DEBUGGING
  if (x < 0 || x >= ext->w || y < 0 || y >= ext->l)
    {
#if !SUPPRESS_WARNINGS
      printf (_("ascii_line_intersection: bad intsct (%d,%d) out of (%d,%d)\n"),
	      x, y, ext->w, ext->l);
#endif
      return;
    }
#endif

  l = ((style->l << LNS_LEFT) | (style->r << LNS_RIGHT)
       | (style->t << LNS_TOP) | (style->b << LNS_BOTTOM));

  if (ext->line_len[y] <= x)
    expand_line (ext, y, x + 1);
  draw_line (x, y, l);
}

/* FIXME: Later we could set this up so that for certain devices it
   performs shading? */
static void
ascii_box (struct outp_driver *this UNUSED, const struct rect *r UNUSED,
	   const struct color *bord UNUSED, const struct color *fill UNUSED)
{
  assert (this->driver_open && this->page_open);
}

/* Polylines not supported. */
static void
ascii_polyline_begin (struct outp_driver *this UNUSED, const struct color *c UNUSED)
{
  assert (this->driver_open && this->page_open);
}
static void
ascii_polyline_point (struct outp_driver *this UNUSED, int x UNUSED, int y UNUSED)
{
  assert (this->driver_open && this->page_open);
}
static void
ascii_polyline_end (struct outp_driver *this UNUSED)
{
  assert (this->driver_open && this->page_open);
}

static void
ascii_text_set_font_by_name (struct outp_driver * this, const char *s)
{
  struct ascii_driver_ext *x = this->ext;
  int len = strlen (s);

  assert (this->driver_open && this->page_open);
  x->cur_font = OUTP_F_R;
  if (len == 0)
    return;
  if (s[len - 1] == 'I')
    {
      if (len > 1 && s[len - 2] == 'B')
	x->cur_font = OUTP_F_BI;
      else
	x->cur_font = OUTP_F_I;
    }
  else if (s[len - 1] == 'B')
    x->cur_font = OUTP_F_B;
}

static void
ascii_text_set_font_by_position (struct outp_driver *this, int pos)
{
  struct ascii_driver_ext *x = this->ext;
  assert (this->driver_open && this->page_open);
  x->cur_font = pos >= 0 && pos < 4 ? pos : 0;
}

static void
ascii_text_set_font_by_family (struct outp_driver *this UNUSED, const char *s UNUSED)
{
  assert (this->driver_open && this->page_open);
}

static const char *
ascii_text_get_font_name (struct outp_driver *this)
{
  struct ascii_driver_ext *x = this->ext;

  assert (this->driver_open && this->page_open);
  switch (x->cur_font)
    {
    case OUTP_F_R:
      return "R";
    case OUTP_F_I:
      return "I";
    case OUTP_F_B:
      return "B";
    case OUTP_F_BI:
      return "BI";
    default:
      assert (0);
    }
  abort ();
}

static const char *
ascii_text_get_font_family (struct outp_driver *this UNUSED)
{
  assert (this->driver_open && this->page_open);
  return "";
}

static int
ascii_text_set_size (struct outp_driver *this, int size)
{
  assert (this->driver_open && this->page_open);
  return size == this->vert;
}

static int
ascii_text_get_size (struct outp_driver *this, int *em_width)
{
  assert (this->driver_open && this->page_open);
  if (em_width)
    *em_width = this->horiz;
  return this->vert;
}

static void text_draw (struct outp_driver *this, struct outp_text *t);

/* Divides the text T->S into lines of width T->H.  Sets T->V to the
   number of lines necessary.  Actually draws the text if DRAW is
   nonzero.

   You probably don't want to look at this code. */
static void
delineate (struct outp_driver *this, struct outp_text *t, int draw)
{
  /* Width we're fitting everything into. */
  int width = t->h / this->horiz;

  /* Maximum `y' position we can write to. */
  int max_y;

  /* Current position in string, character following end of string. */
  const char *s = ls_value (&t->s);
  const char *end = ls_end (&t->s);

  /* Temporary struct outp_text to pass to low-level function. */
  struct outp_text temp;

#if GLOBAL_DEBUGGING && 0
  if (!ext->debug)
    {
      ext->debug = 1;
      printf (_("%s: horiz=%d, vert=%d\n"), this->name, this->horiz, this->vert);
    }
#endif

  if (!width)
    {
      t->h = t->v = 0;
      return;
    }

  if (draw)
    {
      temp.options = t->options;
      ls_shallow_copy (&temp.s, &t->s);
      temp.h = t->h / this->horiz;
      temp.x = t->x / this->horiz;
    }
  else
    t->y = 0;
  temp.y = t->y / this->vert;

  if (t->options & OUTP_T_VERT)
    max_y = (t->v / this->vert) + temp.y - 1;
  else
    max_y = INT_MAX;
  
  while (end - s > width)
    {
      const char *beg = s;
      const char *space;

      /* Find first space before &s[width]. */
      space = &s[width];
      for (;;)
	{
	  if (space > s)
	    {
	      if (!isspace ((unsigned char) space[-1]))
		{
		  space--;
		  continue;
		}
	      else
		s = space;
	    }
	  else
	    s = space = &s[width];
	  break;
	}

      /* Draw text. */
      if (draw)
	{
	  ls_init (&temp.s, beg, space - beg);
	  temp.w = space - beg;
	  text_draw (this, &temp);
	}
      if (++temp.y > max_y)
	return;

      /* Find first nonspace after space. */
      while (s < end && isspace ((unsigned char) *s))
	s++;
    }
  if (s < end)
    {
      if (draw)
	{
	  ls_init (&temp.s, s, end - s);
	  temp.w = end - s;
	  text_draw (this, &temp);
	}
      temp.y++;
    }

  t->v = (temp.y * this->vert) - t->y;
}

static void
ascii_text_metrics (struct outp_driver *this, struct outp_text *t)
{
  assert (this->driver_open && this->page_open);
  if (!(t->options & OUTP_T_HORZ))
    {
      t->v = this->vert;
      t->h = ls_length (&t->s) * this->horiz;
    }
  else
    delineate (this, t, 0);
}

static void
ascii_text_draw (struct outp_driver *this, struct outp_text *t)
{
  /* FIXME: orientations not supported. */
  assert (this->driver_open && this->page_open);
  if (!(t->options & OUTP_T_HORZ))
    {
      struct outp_text temp;

      temp.options = t->options;
      temp.s = t->s;
      temp.h = temp.v = 0;
      temp.x = t->x / this->horiz;
      temp.y = t->y / this->vert;
      text_draw (this, &temp);
      ascii_text_metrics (this, t);
      
      return;
    }
  delineate (this, t, 1);
}

static void
text_draw (struct outp_driver *this, struct outp_text *t)
{
  struct ascii_driver_ext *ext = this->ext;
  unsigned attr = ext->cur_font << 8;

  int x = t->x;
  int y = t->y * ext->w;

  char *s = ls_value (&t->s);

  /* Expand the line with the assumption that S takes up LEN character
     spaces (sometimes it takes up less). */
  int min_len;

  assert (this->driver_open && this->page_open);
  switch (t->options & OUTP_T_JUST_MASK)
    {
    case OUTP_T_JUST_LEFT:
      break;
    case OUTP_T_JUST_CENTER:
      x -= (t->h - t->w) / 2;	/* fall through */
    case OUTP_T_JUST_RIGHT:
      x += (t->h - t->w);
      break;
    default:
      assert (0);
    }

  if (!(t->y < ext->l && x < ext->w))
    return;
  min_len = min (x + ls_length (&t->s), ext->w);
  if (ext->line_len[t->y] < min_len)
    expand_line (ext, t->y, min_len);

  {
    int len = ls_length (&t->s);

    if (len + x > ext->w)
      len = ext->w - x;
    while (len--)
      ext->page[y + x++] = *s++ | attr;
  }
}

/* ascii_close_page () and support routines. */

#define LINE_BUF_SIZE 1024
static unsigned char *line_buf;
static unsigned char *line_p;

static inline int
commit_line_buf (struct outp_driver *this)
{
  struct ascii_driver_ext *x = this->ext;
  
  if ((int) fwrite (line_buf, 1, line_p - line_buf, x->file.file)
      < line_p - line_buf)
    {
      msg (ME, _("Writing `%s': %s"), x->file.filename, strerror (errno));
      return 0;
    }

  line_p = line_buf;
  return 1;
}

/* Writes everything from BP to EP exclusive into line_buf, or to
   THIS->output if line_buf overflows. */
static inline void
output_string (struct outp_driver *this, const char *bp, const char *ep)
{
  if (LINE_BUF_SIZE - (line_p - line_buf) >= ep - bp)
    {
      memcpy (line_p, bp, ep - bp);
      line_p += ep - bp;
    }
  else
    while (bp < ep)
      {
	if (LINE_BUF_SIZE - (line_p - line_buf) <= 1 && !commit_line_buf (this))
	  return;
	*line_p++ = *bp++;
      }
}

/* Writes everything from BP to EP exclusive into line_buf, or to
   THIS->output if line_buf overflows.  Returns 1 if additional passes
   over the line are required.  FIXME: probably could do a lot of
   optimization here. */
static inline int
output_shorts (struct outp_driver *this,
	       const unsigned short *bp, const unsigned short *ep)
{
  struct ascii_driver_ext *ext = this->ext;
  size_t remaining = LINE_BUF_SIZE - (line_p - line_buf);
  int result = 0;

  for (; bp < ep; bp++)
    {
      if (*bp & 0x800)
	{
	  struct len_string *box = &ext->box[*bp & 0xff];
	  size_t len = ls_length (box);

	  if (remaining >= len)
	    {
	      memcpy (line_p, ls_value (box), len);
	      line_p += len;
	      remaining -= len;
	    }
	  else
	    {
	      if (!commit_line_buf (this))
		return 0;
	      output_string (this, ls_value (box), ls_end (box));
	      remaining = LINE_BUF_SIZE - (line_p - line_buf);
	    }
	}
      else if (*bp & 0x0300)
	{
	  struct len_string *on;
	  char buf[5];
	  int len;

	  switch (*bp & 0x0300)
	    {
	    case OUTP_F_I << 8:
	      on = &ext->fonts[FSTY_ON | FSTY_ITALIC];
	      break;
	    case OUTP_F_B << 8:
	      on = &ext->fonts[FSTY_ON | FSTY_BOLD];
	      break;
	    case OUTP_F_BI << 8:
	      on = &ext->fonts[FSTY_ON | FSTY_BOLD_ITALIC];
	      break;
	    default:
	      assert (0);
	    }
	  if (!on)
	    {
	      if (ext->overstrike_style == OVS_SINGLE)
		switch (*bp & 0x0300)
		  {
		  case OUTP_F_I << 8:
		    buf[0] = '_';
		    buf[1] = '\b';
		    buf[2] = *bp;
		    len = 3;
		    break;
		  case OUTP_F_B << 8:
		    buf[0] = *bp;
		    buf[1] = '\b';
		    buf[2] = *bp;
		    len = 3;
		    break;
		  case OUTP_F_BI << 8:
		    buf[0] = '_';
		    buf[1] = '\b';
		    buf[2] = *bp;
		    buf[3] = '\b';
		    buf[4] = *bp;
		    len = 5;
		    break;
		  default:
		    assert (0);
		  }
	      else
		{
		  buf[0] = *bp;
		  result = len = 1;
		}
	    }
	  else
	    {
	      buf[0] = *bp;
	      len = 1;
	    }
	  output_string (this, buf, &buf[len]);
	}
      else if (remaining)
	{
	  *line_p++ = *bp;
	  remaining--;
	}
      else
	{
	  if (!commit_line_buf (this))
	    return 0;
	  remaining = LINE_BUF_SIZE - (line_p - line_buf);
	  *line_p++ = *bp;
	}
    }

  return result;
}

/* Writes CH into line_buf N times, or to THIS->output if line_buf
   overflows. */
static inline void
output_char (struct outp_driver *this, int n, int ch)
{
  if (LINE_BUF_SIZE - (line_p - line_buf) >= n)
    {
      memset (line_p, ch, n);
      line_p += n;
    }
  else
    while (n--)
      {
	if (LINE_BUF_SIZE - (line_p - line_buf) <= 1 && !commit_line_buf (this))
	  return;
	*line_p++ = ch;
      }
}

/* Advance the carriage from column 0 to the left margin. */
static void
advance_to_left_margin (struct outp_driver *this)
{
  struct ascii_driver_ext *ext = this->ext;
  int margin;

  margin = ext->left_margin;
  if (margin == 0)
    return;
  if (ext->tab_width && margin >= ext->tab_width)
    {
      output_char (this, margin / ext->tab_width, '\t');
      margin %= ext->tab_width;
    }
  if (margin)
    output_char (this, margin, ' ');
}

/* Move the output file carriage N_CHARS left, to the left margin. */
static void
return_carriage (struct outp_driver *this, int n_chars)
{
  struct ascii_driver_ext *ext = this->ext;

  switch (ext->carriage_return_style)
    {
    case CRS_BS:
      output_char (this, n_chars, '\b');
      break;
    case CRS_CR:
      output_char (this, 1, '\r');
      advance_to_left_margin (this);
      break;
    default:
      assert (0);
    }
}

/* Writes COUNT lines from the line buffer in THIS, starting at line
   number FIRST. */
static void
output_lines (struct outp_driver *this, int first, int count)
{
  struct ascii_driver_ext *ext = this->ext;
  int line_num;

  struct len_string *newline = &ext->ops[OPS_NEWLINE];

  int n_chars;
  int n_passes;

  if (NULL == ext->file.file)
    return;

  /* Iterate over all the lines to be output. */
  for (line_num = first; line_num < first + count; line_num++)
    {
      unsigned short *p = &ext->page[ext->w * line_num];
      unsigned short *end_p = p + ext->line_len[line_num];
      unsigned short *bp, *ep;
      unsigned short attr = 0;

      assert (end_p >= p);

      /* Squeeze multiple blank lines into a single blank line if
         requested. */
      if (ext->squeeze_blank_lines
          && line_num > first
          && ext->line_len[line_num] == 0
          && ext->line_len[line_num - 1] == 0)
        continue;

      /* Output every character in the line in the appropriate
         manner. */
      n_passes = 1;
      bp = ep = p;
      n_chars = 0;
      advance_to_left_margin (this);
      for (;;)			
	{
	  while (ep < end_p && attr == (*ep & 0x0300))
	    ep++;
	  if (output_shorts (this, bp, ep))
	    n_passes = 2;
	  n_chars += ep - bp;
	  bp = ep;

	  if (bp >= end_p)
	    break;

	  /* Turn off old font. */
	  if (attr != (OUTP_F_R << 8))
	    {
	      struct len_string *off;

	      switch (attr)
		{
		case OUTP_F_I << 8:
		  off = &ext->fonts[FSTY_OFF | FSTY_ITALIC];
		  break;
		case OUTP_F_B << 8:
		  off = &ext->fonts[FSTY_OFF | FSTY_BOLD];
		  break;
		case OUTP_F_BI << 8:
		  off = &ext->fonts[FSTY_OFF | FSTY_BOLD_ITALIC];
		  break;
		default:
		  assert (0);
		}
	      if (off)
		output_string (this, ls_value (off), ls_end (off));
	    }

	  /* Turn on new font. */
	  attr = (*bp & 0x0300);
	  if (attr != (OUTP_F_R << 8))
	    {
	      struct len_string *on;

	      switch (attr)
		{
		case OUTP_F_I << 8:
		  on = &ext->fonts[FSTY_ON | FSTY_ITALIC];
		  break;
		case OUTP_F_B << 8:
		  on = &ext->fonts[FSTY_ON | FSTY_BOLD];
		  break;
		case OUTP_F_BI << 8:
		  on = &ext->fonts[FSTY_ON | FSTY_BOLD_ITALIC];
		  break;
		default:
		  assert (0);
		}
	      if (on)
		output_string (this, ls_value (on), ls_end (on));
	    }

	  ep = bp + 1;
	}
      if (n_passes > 1)
	{
	  unsigned char ch;

	  return_carriage (this, n_chars);
	  n_chars = 0;
	  bp = ep = p;
	  for (;;)
	    {
	      while (ep < end_p && (*ep & 0x0300) == (OUTP_F_R << 8))
		ep++;
	      if (ep >= end_p)
		break;
	      output_char (this, ep - bp, ' ');

	      switch (*ep & 0x0300)
		{
		case OUTP_F_I << 8:
		  ch = '_';
		  break;
		case OUTP_F_B << 8:
		  ch = *ep;
		  break;
		case OUTP_F_BI << 8:
		  ch = *ep;
		  n_passes = 3;
		  break;
		}
	      output_char (this, 1, ch);
	      n_chars += ep - bp + 1;
	      bp = ep + 1;
	      ep = bp;
	    }
	}
      if (n_passes > 2)
	{
	  return_carriage (this, n_chars);
	  bp = ep = p;
	  for (;;)
	    {
	      while (ep < end_p && (*ep & 0x0300) != (OUTP_F_BI << 8))
		ep++;
	      if (ep >= end_p)
		break;
	      output_char (this, ep - bp, ' ');
	      output_char (this, 1, '_');
	      bp = ep + 1;
	      ep = bp;
	    }
	}

      output_string (this, ls_value (newline), ls_end (newline));
    }
}

static int
ascii_close_page (struct outp_driver *this)
{
  static unsigned char *s;
  static int s_len;

  struct ascii_driver_ext *x = this->ext;
  int nl_len, ff_len, total_len;
  unsigned char *cp;
  int i;

  assert (this->driver_open && this->page_open);
  
  if (!line_buf)
    line_buf = xmalloc (LINE_BUF_SIZE);
  line_p = line_buf;

  nl_len = ls_length (&x->ops[OPS_NEWLINE]);
  if (x->top_margin)
    {
      total_len = x->top_margin * nl_len;
      if (s_len < total_len)
	{
	  s_len = total_len;
	  s = xrealloc (s, s_len);
	}
      for (cp = s, i = 0; i < x->top_margin; i++)
	{
	  memcpy (cp, ls_value (&x->ops[OPS_NEWLINE]), nl_len);
	  cp += nl_len;
	}
      output_string (this, s, &s[total_len]);
    }
  if (x->headers)
    {
      int len;

      total_len = nl_len + x->w;
      if (s_len < total_len + 1)
	{
	  s_len = total_len + 1;
	  s = xrealloc (s, s_len);
	}
      
      memset (s, ' ', x->w);

      {
	char temp[40];

	snprintf (temp, 80, _("%s - Page %d"), curdate, x->page_number);
	memcpy (&s[x->w - strlen (temp)], temp, strlen (temp));
      }

      if (outp_title && outp_subtitle)
	{
	  len = min ((int) strlen (outp_title), x->w);
	  memcpy (s, outp_title, len);
	}
      memcpy (&s[x->w], ls_value (&x->ops[OPS_NEWLINE]), nl_len);
      output_string (this, s, &s[total_len]);

      memset (s, ' ', x->w);
      len = strlen (version) + 3 + strlen (host_system);
      if (len < x->w)
	sprintf (&s[x->w - len], "%s - %s" , version, host_system);
      if (outp_subtitle || outp_title)
	{
	  char *string = outp_subtitle ? outp_subtitle : outp_title;
	  len = min ((int) strlen (string), x->w);
	  memcpy (s, string, len);
	}
      memcpy (&s[x->w], ls_value (&x->ops[OPS_NEWLINE]), nl_len);
      output_string (this, s, &s[total_len]);
      output_string (this, &s[x->w], &s[total_len]);
    }
  if (line_p != line_buf && !commit_line_buf (this))
    return 0;

  output_lines (this, x->n_output, x->l - x->n_output);

  ff_len = ls_length (&x->ops[OPS_FORMFEED]);
  total_len = x->bottom_margin * nl_len + ff_len;
  if (s_len < total_len)
    s = xrealloc (s, total_len);
  for (cp = s, i = 0; i < x->bottom_margin; i++)
    {
      memcpy (cp, ls_value (&x->ops[OPS_NEWLINE]), nl_len);
      cp += nl_len;
    }
  memcpy (cp, ls_value (&x->ops[OPS_FORMFEED]), ff_len);
  if ( x->paginate ) 
	  output_string (this, s, &s[total_len]);
  if (line_p != line_buf && !commit_line_buf (this))
    return 0;

  x->n_output = 0;
  
  this->page_open = 0;
  return 1;
}

struct outp_class ascii_class =
{
  "ascii",
  0,
  0,

  ascii_open_global,
  ascii_close_global,
  ascii_font_sizes,

  ascii_preopen_driver,
  ascii_option,
  ascii_postopen_driver,
  ascii_close_driver,

  ascii_open_page,
  ascii_close_page,

  NULL,

  ascii_line_horz,
  ascii_line_vert,
  ascii_line_intersection,

  ascii_box,
  ascii_polyline_begin,
  ascii_polyline_point,
  ascii_polyline_end,

  ascii_text_set_font_by_name,
  ascii_text_set_font_by_position,
  ascii_text_set_font_by_family,
  ascii_text_get_font_name,
  ascii_text_get_font_family,
  ascii_text_set_size,
  ascii_text_get_size,
  ascii_text_metrics,
  ascii_text_draw,
};
