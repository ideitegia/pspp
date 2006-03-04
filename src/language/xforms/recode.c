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
#include "message.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include "alloc.h"
#include "case.h"
#include "command.h"
#include "compiler.h"
#include "data-in.h"
#include "dictionary.h"
#include "message.h"
#include "lexer.h"
#include "magic.h"
#include "pool.h"
#include "range-parser.h"
#include "str.h"
#include "variable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Definitions. */

/* Type of source value for RECODE. */
enum map_in_type
  {
    MAP_SINGLE,			/* Specific value. */
    MAP_RANGE,			/* Range of values. */
    MAP_SYSMIS,                 /* System missing value. */
    MAP_MISSING,                /* Any missing value. */
    MAP_ELSE,			/* Any value. */
    MAP_CONVERT			/* "123" => 123. */
  };

/* Describes input values to be mapped. */
struct map_in
  {
    enum map_in_type type;      /* One of MAP_*. */
    union value x, y;           /* Source values. */
  };

/* Describes the value used as output from a mapping. */
struct map_out 
  {
    bool copy_input;            /* If true, copy input to output. */
    union value value;          /* If copy_input false, recoded value. */
    int width;                  /* If copy_input false, output value width. */ 
  };

/* Describes how to recode a single value or range of values into a
   single value.  */
struct mapping 
  {
    struct map_in in;           /* Input values. */
    struct map_out out;         /* Output value. */
  };

/* RECODE transformation. */
struct recode_trns
  {
    struct pool *pool;

    /* Variable types, for convenience. */
    enum var_type src_type;     /* src_vars[*]->type. */
    enum var_type dst_type;     /* dst_vars[*]->type. */

    /* Variables. */
    struct variable **src_vars;	/* Source variables. */
    struct variable **dst_vars;	/* Destination variables. */
    char **dst_names;		/* Name of dest variables, if they're new. */
    size_t var_cnt;             /* Number of variables. */

    /* Mappings. */
    struct mapping *mappings;   /* Value mappings. */
    size_t map_cnt;             /* Number of mappings. */
  };

static bool parse_src_vars (struct recode_trns *);
static bool parse_mappings (struct recode_trns *);
static bool parse_dst_vars (struct recode_trns *);

static void add_mapping (struct recode_trns *,
                         size_t *map_allocated, const struct map_in *);

static bool parse_map_in (struct map_in *, struct pool *,
                          enum var_type src_type, size_t max_src_width);
static void set_map_in_generic (struct map_in *, enum map_in_type);
static void set_map_in_num (struct map_in *, enum map_in_type, double, double);
static void set_map_in_str (struct map_in *, struct pool *,
                            const struct string *, size_t width);

static bool parse_map_out (struct pool *, struct map_out *);
static void set_map_out_num (struct map_out *, double);
static void set_map_out_str (struct map_out *, struct pool *,
                             const struct string *);

static void enlarge_dst_widths (struct recode_trns *);
static void create_dst_vars (struct recode_trns *);

static trns_proc_func recode_trns_proc;
static trns_free_func recode_trns_free;

/* Parser. */

/* Parses the RECODE transformation. */
int
cmd_recode (void)
{
  do
    {
      struct recode_trns *trns
        = pool_create_container (struct recode_trns, pool);

      /* Parse source variable names,
         then input to output mappings,
         then destintation variable names. */
      if (!parse_src_vars (trns)
          || !parse_mappings (trns)
          || !parse_dst_vars (trns))
        {
          recode_trns_free (trns);
          return CMD_PART_SUCCESS;
        }

      /* Ensure that all the output strings are at least as wide
         as the widest destination variable. */
      if (trns->dst_type == ALPHA)
        enlarge_dst_widths (trns);

      /* Create destination variables, if needed.
         This must be the final step; otherwise we'd have to
         delete destination variables on failure. */
      if (trns->src_vars != trns->dst_vars)
        create_dst_vars (trns);

      /* Done. */
      add_transformation (recode_trns_proc, recode_trns_free, trns);
    }
  while (lex_match ('/'));
  
  return lex_end_of_command ();
}

/* Parses a set of variables to recode into TRNS->src_vars and
   TRNS->var_cnt.  Sets TRNS->src_type.  Returns true if
   successful, false on parse error. */
static bool
parse_src_vars (struct recode_trns *trns) 
{
  if (!parse_variables (default_dict, &trns->src_vars, &trns->var_cnt,
                        PV_SAME_TYPE))
    return false;
  pool_register (trns->pool, free, trns->src_vars);
  trns->src_type = trns->src_vars[0]->type;
  return true;
}

/* Parses a set of mappings, which take the form (input=output),
   into TRNS->mappings and TRNS->map_cnt.  Sets TRNS->dst_type.
   Returns true if successful, false on parse error. */
static bool
parse_mappings (struct recode_trns *trns) 
{
  size_t max_src_width;
  size_t map_allocated;
  bool have_dst_type;
  size_t i;
  
  /* Find length of longest source variable. */
  max_src_width = trns->src_vars[0]->width;
  for (i = 1; i < trns->var_cnt; i++) 
    {
      size_t var_width = trns->src_vars[i]->width;
      if (var_width > max_src_width)
        max_src_width = var_width;
    }
      
  /* Parse the mappings in parentheses. */
  trns->mappings = NULL;
  trns->map_cnt = 0;
  map_allocated = 0;
  have_dst_type = false;
  if (!lex_force_match ('('))
    return false;
  do
    {
      enum var_type dst_type;

      if (!lex_match_id ("CONVERT")) 
        {
          struct map_out out;
          size_t first_map_idx;
          size_t i;

          first_map_idx = trns->map_cnt;

          /* Parse source specifications. */
          do
            {
              struct map_in in;
              if (!parse_map_in (&in, trns->pool,
                                 trns->src_type, max_src_width))
                return false;
              add_mapping (trns, &map_allocated, &in);
              lex_match (',');
            }
          while (!lex_match ('='));

          if (!parse_map_out (trns->pool, &out))
            return false;
          dst_type = out.width == 0 ? NUMERIC : ALPHA;
          if (have_dst_type && dst_type != trns->dst_type)
            {
              msg (SE, _("Inconsistent target variable types.  "
                         "Target variables "
                         "must be all numeric or all string."));
              return false;
            }
              
          for (i = first_map_idx; i < trns->map_cnt; i++)
            trns->mappings[i].out = out;
        }
      else 
        {
          /* Parse CONVERT as a special case. */
          struct map_in in;
          set_map_in_generic (&in, MAP_CONVERT);
          add_mapping (trns, &map_allocated, &in);
              
          dst_type = NUMERIC;
          if (trns->src_type != ALPHA
              || (have_dst_type && trns->dst_type != NUMERIC)) 
            {
              msg (SE, _("CONVERT requires string input values and "
                         "numeric output values."));
              return false;
            }
        }
      trns->dst_type = dst_type;
      have_dst_type = true;

      if (!lex_force_match (')'))
        return false; 
    }
  while (lex_match ('('));

  return true;
}

/* Parses a mapping input value into IN, allocating memory from
   POOL.  The source value type must be provided as SRC_TYPE and,
   if string, the maximum width of a string source variable must
   be provided in MAX_SRC_WIDTH.  Returns true if successful,
   false on parse error. */
static bool
parse_map_in (struct map_in *in, struct pool *pool,
              enum var_type src_type, size_t max_src_width)
{
  if (lex_match_id ("ELSE"))
    set_map_in_generic (in, MAP_ELSE);
  else if (src_type == NUMERIC)
    {
      if (lex_match_id ("MISSING"))
        set_map_in_generic (in, MAP_MISSING);
      else if (lex_match_id ("SYSMIS"))
        set_map_in_generic (in, MAP_SYSMIS);
      else 
        {
          double x, y;
          if (!parse_num_range (&x, &y, NULL))
            return false;
          set_map_in_num (in, x == y ? MAP_SINGLE : MAP_RANGE, x, y);
        }
    }
  else
    {
      if (!lex_force_string ())
        return false;
      set_map_in_str (in, pool, &tokstr, max_src_width);
      lex_get ();
    }

  return true;
}

/* Adds IN to the list of mappings in TRNS.
   MAP_ALLOCATED is the current number of allocated mappings,
   which is updated as needed. */
static void
add_mapping (struct recode_trns *trns,
             size_t *map_allocated, const struct map_in *in)
{
  struct mapping *m;
  if (trns->map_cnt >= *map_allocated)
    trns->mappings = pool_2nrealloc (trns->pool, trns->mappings,
                                     map_allocated,
                                     sizeof *trns->mappings);
  m = &trns->mappings[trns->map_cnt++];
  m->in = *in;
}

/* Sets IN as a mapping of the given TYPE. */
static void
set_map_in_generic (struct map_in *in, enum map_in_type type) 
{
  in->type = type;
}

/* Sets IN as a numeric mapping of the given TYPE,
   with X and Y as the two numeric values. */
static void
set_map_in_num (struct map_in *in, enum map_in_type type, double x, double y) 
{
  in->type = type;
  in->x.f = x;
  in->y.f = y;
}

/* Sets IN as a string mapping, with STRING as the string,
   allocated from POOL.  The string is padded with spaces on the
   right to WIDTH characters long. */
static void
set_map_in_str (struct map_in *in, struct pool *pool,
                const struct string *string, size_t width) 
{
  in->type = MAP_SINGLE;
  in->x.c = pool_alloc_unaligned (pool, width);
  buf_copy_rpad (in->x.c, width, ds_data (string), ds_length (string));
}

/* Parses a mapping output value into OUT, allocating memory from
   POOL.  Returns true if successful, false on parse error. */
static bool
parse_map_out (struct pool *pool, struct map_out *out)
{
  if (lex_is_number ())
    {
      set_map_out_num (out, lex_number ());
      lex_get ();
    }
  else if (lex_match_id ("SYSMIS"))
    set_map_out_num (out, SYSMIS);
  else if (token == T_STRING)
    {
      set_map_out_str (out, pool, &tokstr);
      lex_get ();
    }
  else if (lex_match_id ("COPY"))
    out->copy_input = true;
  else 
    {
      lex_error (_("expecting output value"));
      return false;
    }
  return true; 
}

/* Sets OUT as a numeric mapping output with the given VALUE. */
static void
set_map_out_num (struct map_out *out, double value) 
{
  out->copy_input = false;
  out->value.f = value;
  out->width = 0;
}

/* Sets OUT as a string mapping output with the given VALUE. */
static void
set_map_out_str (struct map_out *out, struct pool *pool,
                 const struct string *value)
{
  const char *string = ds_data (value);
  size_t length = ds_length (value);

  out->copy_input = false;
  out->value.c = pool_alloc_unaligned (pool, length);
  memcpy (out->value.c, string, length);
  out->width = length;
}

/* Parses a set of target variables into TRNS->dst_vars and
   TRNS->dst_names. */
static bool
parse_dst_vars (struct recode_trns *trns) 
{
  size_t i;
  
  if (lex_match_id ("INTO"))
    {
      size_t name_cnt;
      size_t i;

      if (!parse_mixed_vars_pool (trns->pool, &trns->dst_names, &name_cnt,
                                  PV_NONE))
        return false;

      if (name_cnt != trns->var_cnt)
        {
          msg (SE, _("%u variable(s) cannot be recoded into "
                     "%u variable(s).  Specify the same number "
                     "of variables as source and target variables."),
               (unsigned) trns->var_cnt, (unsigned) name_cnt);
          return false;
        }

      trns->dst_vars = pool_nalloc (trns->pool,
                                    trns->var_cnt, sizeof *trns->dst_vars);
      for (i = 0; i < trns->var_cnt; i++)
        {
          struct variable *v;
          v = trns->dst_vars[i] = dict_lookup_var (default_dict,
                                                  trns->dst_names[i]);
          if (v == NULL && trns->dst_type == ALPHA) 
            {
              msg (SE, _("There is no variable named "
                         "%s.  (All string variables specified "
                         "on INTO must already exist.  Use the "
                         "STRING command to create a string "
                         "variable.)"),
                   trns->dst_names[i]);
              return false;
            }
        }
    }
  else 
    {
      trns->dst_vars = trns->src_vars;
      if (trns->src_type != trns->dst_type)
        {
          msg (SE, _("INTO is required with %s input values "
                     "and %s output values."),
               var_type_adj (trns->src_type),
               var_type_adj (trns->dst_type));
          return false;
        }
    }

  for (i = 0; i < trns->var_cnt; i++)
    {
      struct variable *v = trns->dst_vars[i];
      if (v != NULL && v->type != trns->dst_type)
        {
          msg (SE, _("Type mismatch.  Cannot store %s data in "
                     "%s variable %s."),
               trns->dst_type == ALPHA ? _("string") : _("numeric"),
               v->type == ALPHA ? _("string") : _("numeric"),
               v->name);
          return false;
        }
    }

  return true;
}

/* Ensures that all the output values in TRNS are as wide as the
   widest destination variable. */
static void
enlarge_dst_widths (struct recode_trns *trns) 
{
  size_t max_dst_width;
  size_t i;

  max_dst_width = 0;
  for (i = 0; i < trns->var_cnt; i++)
    {
      struct variable *v = trns->dst_vars[i];
      if (v->width > max_dst_width)
        max_dst_width = v->width;
    }

  for (i = 0; i < trns->map_cnt; i++)
    {
      struct map_out *out = &trns->mappings[i].out;
      if (!out->copy_input && out->width < max_dst_width) 
        {
          char *s = pool_alloc_unaligned (trns->pool, max_dst_width + 1);
          str_copy_rpad (s, max_dst_width + 1, out->value.c);
          out->value.c = s;
        }
    }
}

/* Creates destination variables that don't already exist. */
static void
create_dst_vars (struct recode_trns *trns)
{
  size_t i;

  for (i = 0; i < trns->var_cnt; i++) 
    {
      struct variable **var = &trns->dst_vars[i];
      const char *name = trns->dst_names[i];
          
      *var = dict_lookup_var (default_dict, name);
      if (*var == NULL)
        *var = dict_create_var_assert (default_dict, name, 0);
      assert ((*var)->type == trns->dst_type);
    }
}

/* Data transformation. */

/* Returns the output mapping in TRNS for an input of VALUE on
   variable V, or a null pointer if there is no mapping. */
static const struct map_out *
find_src_numeric (struct recode_trns *trns, double value, struct variable *v)
{
  struct mapping *m;

  for (m = trns->mappings; m < trns->mappings + trns->map_cnt; m++)
    {
      const struct map_in *in = &m->in;
      const struct map_out *out = &m->out;
      bool match;
      
      switch (in->type)
        {
        case MAP_SINGLE:
          match = value == in->x.f;
          break;
        case MAP_MISSING:
          match = mv_is_num_user_missing (&v->miss, value);
          break;
        case MAP_RANGE:
          match = value >= in->x.f && value <= in->y.f;
          break;
        case MAP_ELSE:
          match = true;
          break;
        default:
          abort ();
        }

      if (match)
        return out;
    }

  return NULL;
}

/* Returns the output mapping in TRNS for an input of VALUE with
   the given WIDTH, or a null pointer if there is no mapping. */
static const struct map_out *
find_src_string (struct recode_trns *trns, const char *value, int width)
{
  struct mapping *m;

  for (m = trns->mappings; m < trns->mappings + trns->map_cnt; m++)
    {
      const struct map_in *in = &m->in;
      struct map_out *out = &m->out;
      bool match;
      
      switch (in->type)
        {
        case MAP_SINGLE:
          match = !memcmp (value, in->x.c, width);
          break;
        case MAP_ELSE:
          match = true;
          break;
        case MAP_CONVERT:
          {
            struct data_in di;

            di.s = value;
            di.e = value + width;
            di.v = &out->value;
            di.flags = DI_IGNORE_ERROR;
            di.f1 = di.f2 = 0;
            di.format.type = FMT_F;
            di.format.w = width;
            di.format.d = 0;
            match = data_in (&di);
            break;
          }
        default:
          abort ();
        }

      if (match)
        return out;
    }

  return NULL;
}

/* Performs RECODE transformation. */
static int
recode_trns_proc (void *trns_, struct ccase *c, int case_idx UNUSED)
{
  struct recode_trns *trns = trns_;
  size_t i;

  for (i = 0; i < trns->var_cnt; i++) 
    {
      struct variable *src_var = trns->src_vars[i];
      struct variable *dst_var = trns->dst_vars[i];

      const union value *src_data = case_data (c, src_var->fv);
      union value *dst_data = case_data_rw (c, dst_var->fv);

      const struct map_out *out;

      if (trns->src_type == NUMERIC) 
          out = find_src_numeric (trns, src_data->f, src_var);
      else
          out = find_src_string (trns, src_data->s, src_var->width);

      if (trns->dst_type == NUMERIC) 
        {
          if (out != NULL)
            dst_data->f = !out->copy_input ? out->value.f : src_data->f; 
          else if (trns->src_vars != trns->dst_vars)
            dst_data->f = SYSMIS;
        }
      else 
        {
          if (out != NULL)
            {
              if (!out->copy_input) 
                memcpy (dst_data->s, out->value.c, dst_var->width); 
              else if (trns->src_vars != trns->dst_vars)
                buf_copy_rpad (dst_data->s, dst_var->width,
                               src_data->s, src_var->width); 
            }
          else if (trns->src_vars != trns->dst_vars)
            memset (dst_data->s, ' ', dst_var->width);
        }
    }

  return TRNS_CONTINUE;
}

/* Frees a RECODE transformation. */
static bool
recode_trns_free (void *trns_)
{
  struct recode_trns *trns = trns_;
  pool_destroy (trns->pool);
  return true;
}
