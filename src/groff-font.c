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
#include "font.h"
#include "error.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#include "alloc.h"
#include "error.h"
#include "filename.h"
#include "getline.h"
#include "hash.h"
#include "pool.h"
#include "str.h"
#include "version.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

int font_number_to_index (int);

int space_index;

static int font_msg (int, const char *,...)
     PRINTF_FORMAT (2, 3);
static void scan_badchars (char *, int);
static void dup_char_metric (struct font_desc * font, int dest, int src);
static void add_char_metric (struct font_desc * font, struct char_metrics *metrics,
			     int code);
static void add_kern (struct font_desc * font, int ch1, int ch2, int adjust);

/* Typical whitespace characters for tokenizing. */
static const char whitespace[] = " \t\n\r\v";

/* Some notes on the groff_font manpage:

   DESC file format: A typical PostScript `res' would be 72000, with
   `hor' and `vert' set to 1 to indicate that all those positions are
   valid.  `sizescale' of 1000 would indicate that a scaled point is
   1/1000 of a point (which is 1/72000 of an inch, the same as the
   number of machine units per inch indicated on `res').  `unitwidth'
   of 1000 would indicate that font files are set up for fonts with
   point size of 1000 scaled points, which would equal 1/72 inch or 1
   point (this would tell Groff's postprocessor that it needs to scale
   the font 12 times larger to get a 12-point font). */

/* Reads a Groff font description file and converts it to a usable
   binary format in memory.  Installs the binary format in the global
   font table.  See groff_font for a description of the font
   description format supported.  Returns nonzero on success. */
struct font_desc *
groff_read_font (const char *fn)
{
  struct char_metrics *metrics;

  /* Pool created for font, font being created, font file. */
  struct pool *font_pool = NULL;
  struct font_desc *font = NULL;
  FILE *f = NULL;

  /* Current line, size of line buffer, length of line. */
  char *line = NULL;
  size_t size;
  int len;

  /* Tokenization saved pointer. */
  char *sp;
  
  /* First token on line. */
  char *key;

  /* 0=kernpairs section, 1=charset section. */
  int charset = 0;

  /* Index for previous line. */
  int prev_index = -1;

  /* Current location in file, used for error reporting. */
  struct file_locator where;

#ifdef unix
  fn = fn_tilde_expand (fn);
#endif

  msg (VM (1), _("%s: Opening Groff font file..."), fn);

  where.filename = fn;
  where.line_number = 1;
  err_push_file_locator (&where);

  f = fopen (fn, "r");
  if (!f)
    goto file_lossage;

  font_pool = pool_create ();
  font = pool_alloc (font_pool, sizeof *font);
  font->owner = font_pool;
  font->name = NULL;
  font->internal_name = NULL;
  font->encoding = NULL;
  font->space_width = 0;
  font->slant = 0.0;
  font->ligatures = 0;
  font->special = 0;
  font->deref = NULL;
  font->deref_size = 0;
  font->metric = NULL;
  font->metric_size = 0;
  font->metric_used = 0;
  font->kern = NULL;
  font->kern_size = 8;
  font->kern_used = 0;
  font->kern_max_used = 0;

  /* Parses first section of font file. */
  for (;;)
    {
      /* Location of '#' in line. */
      char *p;

      len = getline (&line, &size, f);
      if (len == -1)
	break;
      
      scan_badchars (line, len);
      p = strchr (line, '#');
      if (p)
	*p = '\0';		/* Reject comments. */

      key = strtok_r (line, whitespace, &sp);
      if (!key)
	goto next_iteration;

      if (!strcmp (key, "internalname"))
	{
	  font->internal_name = strtok_r (NULL, whitespace, &sp);
	  if (font->internal_name == NULL)
	    {
	      font_msg (SE, _("Missing font name."));
	      goto lose;
	    }
	  font->internal_name = pool_strdup (font_pool, font->internal_name);
	}
      else if (!strcmp (key, "encoding"))
	{
	  font->encoding = strtok_r (NULL, whitespace, &sp);
	  if (font->encoding == NULL)
	    {
	      font_msg (SE, _("Missing encoding filename."));
	      goto lose;
	    }
	  font->encoding = pool_strdup (font_pool, font->encoding);
	}
      else if (!strcmp (key, "spacewidth"))
	{
	  char *n = strtok_r (NULL, whitespace, &sp);
	  char *tail;
	  if (n)
	    font->space_width = strtol (n, &tail, 10);
	  if (n == NULL || tail == n)
	    {
	      font_msg (SE, _("Bad spacewidth value."));
	      goto lose;
	    }
	}
      else if (!strcmp (key, "slant"))
	{
	  char *n = strtok_r (NULL, whitespace, &sp);
	  char *tail;
	  if (n)
	    font->slant = strtod (n, &tail);
	  if (n == NULL || tail == n)
	    {
	      font_msg (SE, _("Bad slant value."));
	      goto lose;
	    }
	}
      else if (!strcmp (key, "ligatures"))
	{
	  char *lig;

	  for (;;)
	    {
	      lig = strtok_r (NULL, whitespace, &sp);
	      if (!lig || !strcmp (lig, "0"))
		break;
	      else if (!strcmp (lig, "ff"))
		font->ligatures |= LIG_ff;
	      else if (!strcmp (lig, "ffi"))
		font->ligatures |= LIG_ffi;
	      else if (!strcmp (lig, "ffl"))
		font->ligatures |= LIG_ffl;
	      else if (!strcmp (lig, "fi"))
		font->ligatures |= LIG_fi;
	      else if (!strcmp (lig, "fl"))
		font->ligatures |= LIG_fl;
	      else
		{
		  font_msg (SE, _("Unknown ligature `%s'."), lig);
		  goto lose;
		}
	    }
	}
      else if (!strcmp (key, "special"))
	font->special = 1;
      else if (!strcmp (key, "charset") || !strcmp (key, "kernpairs"))
	break;

      where.line_number++;
    }
  if (ferror (f))
    goto file_lossage;

  /* Parses second section of font file (metrics & kerning data). */
  do
    {
      key = strtok_r (line, whitespace, &sp);
      if (!key)
	goto next_iteration;

      if (!strcmp (key, "charset"))
	charset = 1;
      else if (!strcmp (key, "kernpairs"))
	charset = 0;
      else if (charset)
	{
	  struct char_metrics *metrics = pool_alloc (font_pool,
						     sizeof *metrics);
	  char *m, *type, *code, *tail;

	  m = strtok_r (NULL, whitespace, &sp);
	  if (!m)
	    {
	      font_msg (SE, _("Unexpected end of line reading character "
			      "set."));
	      goto lose;
	    }
	  if (!strcmp (m, "\""))
	    {
	      if (!prev_index)
		{
		  font_msg (SE, _("Can't use ditto mark for first character."));
		  goto lose;
		}
	      if (!strcmp (key, "---"))
		{
		  font_msg (SE, _("Can't ditto into an unnamed character."));
		  goto lose;
		}
	      dup_char_metric (font, font_char_name_to_index (key), prev_index);
	      where.line_number++;
	      goto next_iteration;
	    }

	  if (m)
	    {
	      metrics->code = metrics->width
		= metrics->height = metrics->depth = 0;
	    }
	  
	  if (m == NULL || 1 > sscanf (m, "%d,%d,%d", &metrics->width,
				       &metrics->height, &metrics->depth))
	    {
	      font_msg (SE, _("Missing metrics for character `%s'."), key);
	      goto lose;
	    }

	  type = strtok_r (NULL, whitespace, &sp);
	  if (type)
	    metrics->type = strtol (type, &tail, 10);
	  if (!type || tail == type)
	    {
	      font_msg (SE, _("Missing type for character `%s'."), key);
	      goto lose;
	    }

	  code = strtok_r (NULL, whitespace, &sp);
	  if (code)
	    metrics->code = strtol (code, &tail, 0);
	  if (tail == code)
	    {
	      font_msg (SE, _("Missing code for character `%s'."), key);
	      goto lose;
	    }

	  if (strcmp (key, "---"))
	    prev_index = font_char_name_to_index (key);
	  else
	    prev_index = font_number_to_index (metrics->code);
	  add_char_metric (font, metrics, prev_index);
	}
      else
	{
	  char *c1 = key;
	  char *c2 = strtok_r (NULL, whitespace, &sp);
	  char *n, *tail;
	  int adjust;

	  if (c2 == NULL)
	    {
	      font_msg (SE, _("Malformed kernpair."));
	      goto lose;
	    }

	  n = strtok_r (NULL, whitespace, &sp);
	  if (!n)
	    {
	      font_msg (SE, _("Unexpected end of line reading kernpairs."));
	      goto lose;
	    }
	  adjust = strtol (n, &tail, 10);
	  if (tail == n || *tail)
	    {
	      font_msg (SE, _("Bad kern value."));
	      goto lose;
	    }
	  add_kern (font, font_char_name_to_index (c1),
		    font_char_name_to_index (c2), adjust);
	}

    next_iteration:
      where.line_number++;

      len = getline (&line, &size, f);
    }
  while (len != -1);
  
  if (ferror (f))
    goto file_lossage;
  if (fclose (f) == EOF)
    {
      f = NULL;
      goto file_lossage;
    }
  free (line);
#ifdef unix
  free ((char *) fn);
#endif

  /* Get font ascent and descent. */
  metrics = font_get_char_metrics (font, font_char_name_to_index ("d"));
  font->ascent = metrics ? metrics->height : 0;
  metrics = font_get_char_metrics (font, font_char_name_to_index ("p"));
  font->descent = metrics ? metrics->depth : 0;

  msg (VM (2), _("Font read successfully with internal name %s."),
       font->internal_name == NULL ? "<none>" : font->internal_name);
  
  err_pop_file_locator (&where);

  return font;

  /* Come here on a file error. */
file_lossage:
  msg (ME, "%s: %s", fn, strerror (errno));

  /* Come here on any error. */
lose:
  if (f != NULL)
    fclose (f);
  pool_destroy (font_pool);
#ifdef unix
  free ((char *) fn);
#endif
  err_pop_file_locator (&where);

  msg (VM (1), _("Error reading font."));
  return NULL;
}

/* Prints a font error on stderr. */
static int
font_msg (int class, const char *format,...)
{
  va_list args;

  va_start (args, format);
  tmsg (class, format, args, _("installation error: Groff font error: "));
  va_end (args);

  return 0;
}

/* Scans string LINE of length LEN (not incl. null terminator) for bad
   characters, converts to spaces; reports warnings on file FN. */
static void
scan_badchars (char *line, int len)
{
  unsigned char *cp = line;

  /* Same bad characters as Groff. */
  static unsigned char badchars[32] =
  {
    0x01, 0xe8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  for (; len--; cp++)
    if (badchars[*cp >> 3] & (1 << (*cp & 7)))
      {
	font_msg (SE, _("Bad character \\%3o."), *cp);
	*cp = ' ';
      }
}

/* Character name hashing. */

/* Associates a character index with a character name. */
struct index_hash
  {
    char *name;
    int index;
  };

/* Character index hash table. */
static struct
  {
    int size;			/* Size of table (must be power of 2). */
    int used;			/* Number of full entries. */
    int next_index;		/* Next index to allocate. */
    struct index_hash *tab;	/* Hash table proper. */
    struct pool *ar;		/* Pool for names. */
  }
hash;

void
groff_init (void)
{
  space_index = font_char_name_to_index ("space");
}

void
groff_done (void)
{
  free (hash.tab) ;
  pool_destroy(hash.ar);
}


/* Searches for NAME in the global character code table, returns the
   index if found; otherwise inserts NAME and returns the new
   index. */
int
font_char_name_to_index (const char *name)
{
  int i;

  if (name[0] == ' ')
    return space_index;
  if (name[0] == '\0' || name[1] == '\0')
    return name[0];
  if (0 == strncmp (name, "char", 4))
    {
      char *tail;
      int x = strtol (name + 4, &tail, 10);
      if (tail != name + 4 && *tail == 0 && x >= 0 && x <= 255)
	return x;
    }

  if (!hash.tab)
    {
      hash.size = 128;
      hash.used = 0;
      hash.next_index = 256;
      hash.tab = xmalloc (sizeof *hash.tab * hash.size);
      hash.ar = pool_create ();
      for (i = 0; i < hash.size; i++)
	hash.tab[i].name = NULL;
    }

  for (i = hsh_hash_string (name) & (hash.size - 1); hash.tab[i].name; )
    {
      if (!strcmp (hash.tab[i].name, name))
	return hash.tab[i].index;
      if (++i >= hash.size)
	i = 0;
    }

  hash.used++;
  if (hash.used >= hash.size / 2)
    {
      struct index_hash *old_tab = hash.tab;
      int old_size = hash.size;
      int i, j;

      hash.size *= 2;
      hash.tab = xmalloc (sizeof *hash.tab * hash.size);
      for (i = 0; i < hash.size; i++)
	hash.tab[i].name = NULL;
      for (i = 0; i < old_size; i++)
	if (old_tab[i].name)
	  {
	    for (j = hsh_hash_string (old_tab[i].name) & (hash.size - 1);
                 hash.tab[j].name;)
	      if (++j >= hash.size)
		j = 0;
	    hash.tab[j] = old_tab[i];
	  }
      free (old_tab);
    }

  hash.tab[i].name = pool_strdup (hash.ar, name);
  hash.tab[i].index = hash.next_index;
  return hash.next_index++;
}

/* Returns an index for a character that has only a code, not a
   name. */
int
font_number_to_index (int x)
{
  char name[INT_DIGITS + 2];

  /* Note that space is the only character that can't appear in a
     character name.  That makes it an excellent choice for a name
     that won't conflict. */
  sprintf (name, " %d", x);
  return font_char_name_to_index (name);
}

/* Font character metric entries. */

/* Ensures room for at least MIN_SIZE metric indexes in deref of
   FONT. */
static void
check_deref_space (struct font_desc *font, int min_size)
{
  if (min_size >= font->deref_size)
    {
      int i = font->deref_size;

      font->deref_size = min_size + 16;
      if (font->deref_size < 256)
	font->deref_size = 256;
      font->deref = pool_realloc (font->owner, font->deref,
				  sizeof *font->deref * font->deref_size);
      for (; i < font->deref_size; i++)
	font->deref[i] = -1;
    }
}

/* Inserts METRICS for character with code CODE into FONT. */
static void
add_char_metric (struct font_desc *font, struct char_metrics *metrics, int code)
{
  check_deref_space (font, code);
  if (font->metric_used >= font->metric_size)
    {
      font->metric_size += 64;
      font->metric = pool_realloc (font->owner, font->metric,
				   sizeof *font->metric * font->metric_size);
    }
  font->metric[font->metric_used] = metrics;
  font->deref[code] = font->metric_used++;
}

/* Copies metric in FONT from character with code SRC to character
   with code DEST. */
static void
dup_char_metric (struct font_desc *font, int dest, int src)
{
  check_deref_space (font, dest);
  assert (font->deref[src] != -1);
  font->deref[dest] = font->deref[src];
}

/* Kerning. */

/* Returns a hash value for characters with codes CH1 and CH2. */
#define hash_kern(CH1, CH2)			\
	((unsigned) (((CH1) << 16) ^ (CH2)))

/* Adds an ADJUST-size kern to FONT between characters with codes CH1
   and CH2. */
static void
add_kern (struct font_desc *font, int ch1, int ch2, int adjust)
{
  int i;

  if (font->kern_used >= font->kern_max_used)
    {
      struct kern_pair *old_kern = font->kern;
      int old_kern_size = font->kern_size;
      int j;

      font->kern_size *= 2;
      font->kern_max_used = font->kern_size / 2;
      font->kern = pool_malloc (font->owner,
				sizeof *font->kern * font->kern_size);
      for (i = 0; i < font->kern_size; i++)
	font->kern[i].ch1 = -1;

      if (old_kern)
        {
          for (i = 0; i < old_kern_size; i++)
            {
              if (old_kern[i].ch1 == -1)
                continue;

              j = (hash_kern (old_kern[i].ch1, old_kern[i].ch2)
                   & (font->kern_size - 1));
              while (font->kern[j].ch1 != -1)
                if (0 == j--)
                  j = font->kern_size - 1;
              font->kern[j] = old_kern[i];
            }
          pool_free (font->owner, old_kern);
        }
    }

  for (i = hash_kern (ch1, ch2) & (font->kern_size - 1);
       font->kern[i].ch1 != -1; )
    if (0 == i--)
      i = font->kern_size - 1;
  font->kern[i].ch1 = ch1;
  font->kern[i].ch2 = ch2;
  font->kern[i].adjust = adjust;
  font->kern_used++;
}

/* Finds a font file corresponding to font NAME for device DEV. */
static char *
find_font_file (const char *dev, const char *name)
{
  char *basename = xmalloc (3 + strlen (dev) + 1 + strlen (name) + 1);
  char *cp;
  char *filename;
  char *path;

  cp = stpcpy (basename, "dev");
  cp = stpcpy (cp, dev);
  *cp++ = DIR_SEPARATOR;
  strcpy (cp, name);

  /* Search order:
     1. $STAT_GROFF_FONT_PATH
     2. $GROFF_FONT_PATH
     3. GROFF_FONT_PATH from pref.h
     4. config_path
   */
  if ((path = getenv ("STAT_GROFF_FONT_PATH")) != NULL
      && (filename = fn_search_path (basename, path, NULL)) != NULL)
    goto win;

  if ((path = getenv ("GROFF_FONT_PATH")) != NULL
      && (filename = fn_search_path (basename, path, NULL)) != NULL)
    goto win;

  if ((filename = fn_search_path (basename, groff_font_path, NULL)) != NULL)
    goto win;

  if ((filename = fn_search_path (basename, config_path, NULL)) != NULL)
    goto win;

  msg (IE, _("Groff font error: Cannot find \"%s\"."), basename);

win:
  free (basename);
  return filename;
}

/* Finds a font for device DEV with name NAME, reads it with
   groff_read_font(), and returns the resultant font. */
struct font_desc *
groff_find_font (const char *dev, const char *name)
{
  char *filename = find_font_file (dev, name);
  struct font_desc *fd;

  if (!filename)
    return NULL;
  fd = groff_read_font (filename);
  free (filename);
  return fd;
}

/* Reads a DESC file for device DEV and sets the appropriate fields in
   output driver *DRIVER, which must be previously allocated.  Returns
   nonzero on success. */
int
groff_read_DESC (const char *dev_name, struct groff_device_info * dev)
{
  char *filename;		/* Full name of DESC file. */
  FILE *f;			/* DESC file. */

  char *line = NULL;		/* Current line. */
  int line_len;			/* Number of chars in current line. */
  size_t line_size = 0;		/* Number of chars allocated for line. */

  char *token;			/* strtok()'d token inside line. */

  unsigned found = 0;		/* Bitmask showing what settings
				   have been encountered. */

  int m_sizes = 0;		/* Number of int[2] items that
				   can fit in driver->sizes. */

  char *sp;			/* Tokenization string pointer. */
  struct file_locator where;

  int i;

  dev->horiz = 1;
  dev->vert = 1;
  dev->size_scale = 1;
  dev->n_sizes = 0;
  dev->sizes = NULL;
  dev->family = NULL;
  for (i = 0; i < 4; i++)
    dev->font_name[i] = NULL;

  filename = find_font_file (dev_name, "DESC");
  if (!filename)
    return 0;

  where.filename = filename;
  where.line_number = 0;
  err_push_file_locator (&where);

  msg (VM (1), _("%s: Opening Groff description file..."), filename);
  f = fopen (filename, "r");
  if (!f)
    goto file_lossage;

  while ((line_len = getline (&line, &line_size, f)) != -1)
    {
      where.line_number++;

      token = strtok_r (line, whitespace, &sp);
      if (!token)
	continue;

      if (!strcmp (token, "sizes"))
	{
	  if (found & 0x10000)
	    font_msg (SW, _("Multiple `sizes' declarations."));
	  for (;;)
	    {
	      char *tail;
	      int lower, upper;

	      for (;;)
		{
		  token = strtok_r (NULL, whitespace, &sp);
		  if (token)
		    break;

		  where.line_number++;
		  if ((line_len = getline (&line, &line_size, f)) != -1)
		    {
		      if (ferror (f))
			goto file_lossage;
		      font_msg (SE, _("Unexpected end of file.  "
				"Missing 0 terminator to `sizes' command?"));
		      goto lossage;
		    }
		}

	      if (!strcmp (token, "0"))
		break;

	      errno = 0;
	      if (0 == (lower = strtol (token, &tail, 0)) || errno == ERANGE)
		{
		  font_msg (SE, _("Bad argument to `sizes'."));
		  goto lossage;
		}
	      if (*tail == '-')
		{
		  if (0 == (upper = strtol (&tail[1], &tail, 0)) || errno == ERANGE)
		    {
		      font_msg (SE, _("Bad argument to `sizes'."));
		      goto lossage;
		    }
		  if (lower < upper)
		    {
		      font_msg (SE, _("Bad range in argument to `sizes'."));
		      goto lossage;
		    }
		}
	      else
		upper = lower;
	      if (*tail)
		{
		  font_msg (SE, _("Bad argument to `sizes'."));
		  goto lossage;
		}

	      if (dev->n_sizes + 2 >= m_sizes)
		{
		  m_sizes += 1;
		  dev->sizes = xrealloc (dev->sizes,
					 m_sizes * sizeof *dev->sizes);
		}
	      dev->sizes[dev->n_sizes++][0] = lower;
	      dev->sizes[dev->n_sizes][1] = upper;

	      found |= 0x10000;
	    }
	}
      else if (!strcmp (token, "family"))
	{
	  token = strtok_r (NULL, whitespace, &sp);
	  if (!token)
	    {
	      font_msg (SE, _("Family name expected."));
	      goto lossage;
	    }
	  if (found & 0x20000)
	    {
	      font_msg (SE, _("This command already specified."));
	      goto lossage;
	    }
	  dev->family = xstrdup (token);
	}
      else if (!strcmp (token, "charset"))
	break;
      else
	{
	  static const char *id[]
	    = {"res", "hor", "vert", "sizescale", "unitwidth", NULL};
	  const char **cp;
	  int value;

	  for (cp = id; *cp; cp++)
	    if (!strcmp (token, *cp))
	      break;
	  if (*cp == NULL)
	    continue;		/* completely ignore unrecognized lines */
	  if (found & (1 << (cp - id)))
	    font_msg (SW, _("%s: Device characteristic already defined."), *cp);

	  token = strtok_r (NULL, whitespace, &sp);
	  errno = 0;
	  if (!token || (value = strtol (token, NULL, 0)) <= 0 || errno == ERANGE)
	    {
	      font_msg (SE, _("%s: Invalid numeric format."), *cp);
	      goto lossage;
	    }
	  found |= (1 << (cp - id));
	  switch (cp - id)
	    {
	    case 0:
	      dev->res = value;
	      break;
	    case 1:
	      dev->horiz = value;
	      break;
	    case 2:
	      dev->vert = value;
	      break;
	    case 3:
	      dev->size_scale = value;
	      break;
	    case 4:
	      dev->unit_width = value;
	      break;
	    default:
	      assert (0);
	    }
	}
    }
  if (ferror (f))
    goto file_lossage;
  if ((found & 0x10011) != 0x10011)
    {
      font_msg (SE, _("Missing `res', `unitwidth', and/or `sizes' line(s)."));
      goto lossage;
    }

  /* Font name = family name + suffix. */
  {
    static const char *suffix[4] =
      {"R", "I", "B", "BI"};	/* match OUTP_F_* */
    int len;			/* length of family name */
    int i;

    if (!dev->family)
      dev->family = xstrdup ("");
    len = strlen (dev->family);
    for (i = 0; i < 4; i++)
      {
	char *cp;
	dev->font_name[i] = xmalloc (len + strlen (suffix[i]) + 1);
	cp = stpcpy (dev->font_name[i], dev->family);
	strcpy (cp, suffix[i]);
      }
  }

  dev->sizes[dev->n_sizes][0] = 0;
  dev->sizes[dev->n_sizes][1] = 0;

  msg (VM (2), _("Description file read successfully."));
  
  err_pop_file_locator (&where);
  free (filename);
  free (line);
  return 1;

  /* Come here on a file error. */
file_lossage:
  msg (ME, "%s: %s", filename, strerror (errno));

  /* Come here on any error. */
lossage:
  fclose (f);
  free (line);
  free (dev->family);
  dev->family = NULL;
  free (filename);
  free (dev->sizes);
  dev->sizes = NULL;
  dev->n_sizes = 0;
#if 0				/* at the moment, no errors can come here when dev->font_name[*] are
				   nonzero. */
  for (i = 0; i < 4; i++)
    {
      free (dev->font_name[i]);
      dev->font_name[i] = NULL;
    }
#endif

  err_pop_file_locator (&where);
  
  msg (VM (1), _("Error reading description file."));
  
  return 0;
}

/* Finds character with index CH (as returned by name_to_index() or
   number_to_index()) in font FONT and returns the associated metrics.
   Nonexistent characters have width 0. */
struct char_metrics *
font_get_char_metrics (const struct font_desc *font, int ch)
{
  short index;

  if (ch < 0 || ch >= font->deref_size)
    return 0;

  index = font->deref[ch];
  if (index == -1)
    return 0;

  return font->metric[index];
}

/* Finds kernpair consisting of CH1 and CH2, in that order, in font
   FONT and returns the associated kerning adjustment. */
int
font_get_kern_adjust (const struct font_desc *font, int ch1, int ch2)
{
  unsigned i;

  if (!font->kern)
    return 0;
  for (i = hash_kern (ch1, ch2) & (font->kern_size - 1);
       font->kern[i].ch1 != -1;)
    {
      if (font->kern[i].ch1 == ch1 && font->kern[i].ch2 == ch2)
	return font->kern[i].adjust;
      if (0 == i--)
	i = font->kern_size - 1;
    }
  return 0;
}

/* Returns a twelve-point fixed-pitch font that can be used as a
   last-resort fallback. */
struct font_desc *
default_font (void)
{
  struct pool *font_pool;
  static struct font_desc *font;

  if (font)
    return font;
  font_pool = pool_create ();
  font = pool_alloc (font_pool, sizeof *font);
  font->owner = font_pool;
  font->name = NULL;
  font->internal_name = pool_strdup (font_pool, _("<<fallback>>"));
  font->encoding = pool_strdup (font_pool, "text.enc");
  font->space_width = 12000;
  font->slant = 0.0;
  font->ligatures = 0;
  font->special = 0;
  font->ascent = 8000;
  font->descent = 4000;
  font->deref = NULL;
  font->deref_size = 0;
  font->metric = NULL;
  font->metric_size = 0;
  font->metric_used = 0;
  font->kern = NULL;
  font->kern_size = 8;
  font->kern_used = 0;
  font->kern_max_used = 0;
  return font;
}
