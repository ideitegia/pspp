/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "libpspp/argv-parser.h"

#include <limits.h>

#include "libpspp/assertion.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

struct argv_option_plus
  {
    struct argv_option base;
    void (*cb) (int id, void *aux);
    void *aux;
  };

struct argv_parser
  {
    struct argv_option_plus *options;
    size_t n_options, allocated_options;
  };

/* Creates and returns a new argv_parser that initially is not
   configured to parse any command-line options. */
struct argv_parser *
argv_parser_create (void)
{
  struct argv_parser *ap = xzalloc (sizeof *ap);
  return ap;
}

/* Destroys AP. */
void
argv_parser_destroy (struct argv_parser *ap)
{
  if (ap != NULL)
    {
      free (ap->options);
      free (ap);
    }
}

/* Adds the N options in OPTIONS to AP.  When argv_parser_run is
   later called for AP, each of the options in OPTIONS will be
   handled by passing the option's 'id' member to CB along with
   AUX.  For an option that has an argument, the 'optarg' global
   variable will be set to point to it before calling CB;
   otherwise 'optarg' will be set to NULL. */
void
argv_parser_add_options (struct argv_parser *ap,
                         const struct argv_option *options, size_t n,
                         void (*cb) (int id, void *aux), void *aux)
{
  const struct argv_option *src;
  for (src = options; src < &options[n]; src++)
    {
      struct argv_option_plus *dst;

      if (ap->n_options >= ap->allocated_options)
        ap->options = x2nrealloc (ap->options, &ap->allocated_options,
                                  sizeof *ap->options);

      assert (src->long_name != NULL || src->short_name != 0);
      dst = &ap->options[ap->n_options++];
      dst->base = *src;
      dst->cb = cb;
      dst->aux = aux;
    }
}

/* Parses all ARGC command-line arguments in ARGV according to
   the options configured in AP with argv_parser_add_options.
   Returns true if all the command-line arguments were parsed
   successfully, false if there was an error.  Upon failure
   return, if the external variable 'opterr' is nonzero (which is
   the default), an error message will also be printed.  Upon
   successful return, external variable 'optind' will be set to
   the index of the first non-option argument. */
bool
argv_parser_run (struct argv_parser *ap, int argc, char **argv)
{
  enum { LONGOPT_VAL_BASE = UCHAR_MAX + 1 };
  const struct argv_option_plus *shortopt_ptrs[UCHAR_MAX + 1];
  struct string shortopts;
  struct option *longopts;
  size_t n_longopts;
  bool retval;
  size_t i;

  memset (shortopt_ptrs, 0, sizeof shortopt_ptrs);
  ds_init_empty (&shortopts);
  longopts = xmalloc ((ap->n_options + 1) * sizeof *longopts);
  n_longopts = 0;
  for (i = 0; i < ap->n_options; i++)
    {
      const struct argv_option_plus *aop = &ap->options[i];

      if (aop->base.long_name != NULL)
        {
          struct option *o = &longopts[n_longopts++];
          o->name = aop->base.long_name;
          o->has_arg = aop->base.has_arg;
          o->flag = NULL;
          o->val = i + LONGOPT_VAL_BASE;
        }

      if (aop->base.short_name != 0)
        {
          unsigned char c = aop->base.short_name;
          if (shortopt_ptrs[c] == NULL)
            {
              shortopt_ptrs[c] = aop;
              ds_put_byte (&shortopts, aop->base.short_name);
              if (aop->base.has_arg != no_argument)
                ds_put_byte (&shortopts, ':');
              if (aop->base.has_arg == optional_argument)
                ds_put_byte (&shortopts, ':');
            }
          else
            {
              if (opterr)
                fprintf (stderr, "option -%c multiply defined",
                         aop->base.short_name);
              retval = false;
              goto exit;
            }
        }
    }
  memset (&longopts[n_longopts], 0, sizeof *longopts);

  for (;;)
    {
      int indexptr;
      int c = getopt_long (argc, argv, ds_cstr (&shortopts),
                           longopts, &indexptr);

      if (c == -1)
        {
          retval = true;
          break;
        }
      else if (c == '?')
        {
          retval = false;
          break;
        }
      else if (c >= LONGOPT_VAL_BASE && c < LONGOPT_VAL_BASE + n_longopts + 1)
        {
          struct argv_option_plus *aop = &ap->options[c - LONGOPT_VAL_BASE];
          aop->cb (aop->base.id, aop->aux);
        }
      else if (c >= SCHAR_MIN && c <= UCHAR_MAX)
        {
          const struct argv_option_plus *aop = shortopt_ptrs[(unsigned char) c];
          aop->cb (aop->base.id, aop->aux);
        }
      else
        NOT_REACHED ();
    }

exit:
  ds_destroy (&shortopts);
  free (longopts);
  return retval;
}
