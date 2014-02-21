/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011 Free Software Foundation, Inc.

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
#include <math.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/data-in.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

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
    enum val_type src_type;     /* src_vars[*] type. */
    enum val_type dst_type;     /* dst_vars[*] type. */

    /* Variables. */
    const struct variable **src_vars;	/* Source variables. */
    const struct variable **dst_vars;	/* Destination variables. */
    const struct dictionary *dst_dict;  /* Dictionary of dst_vars */
    char **dst_names;		/* Name of dest variables, if they're new. */
    size_t var_cnt;             /* Number of variables. */

    /* Mappings. */
    struct mapping *mappings;   /* Value mappings. */
    size_t map_cnt;             /* Number of mappings. */
    int max_src_width;          /* Maximum width of src_vars[*]. */
    int max_dst_width;          /* Maximum width of any map_out in mappings. */
  };

static bool parse_src_vars (struct lexer *, struct recode_trns *, const struct dictionary *dict);
static bool parse_mappings (struct lexer *, struct recode_trns *,
                            const char *dict_encoding);
static bool parse_dst_vars (struct lexer *, struct recode_trns *, const struct dictionary *dict);

static void add_mapping (struct recode_trns *,
                         size_t *map_allocated, const struct map_in *);

static bool parse_map_in (struct lexer *lexer, struct map_in *, struct pool *,
                          enum val_type src_type, size_t max_src_width,
                          const char *dict_encoding);
static void set_map_in_generic (struct map_in *, enum map_in_type);
static void set_map_in_num (struct map_in *, enum map_in_type, double, double);
static void set_map_in_str (struct map_in *, struct pool *,
                            struct substring, size_t width,
                            const char *dict_encoding);

static bool parse_map_out (struct lexer *lexer, struct pool *, struct map_out *);
static void set_map_out_num (struct map_out *, double);
static void set_map_out_str (struct map_out *, struct pool *,
                             struct substring);

static bool enlarge_dst_widths (struct recode_trns *);
static void create_dst_vars (struct recode_trns *, struct dictionary *);

static trns_proc_func recode_trns_proc;
static trns_free_func recode_trns_free;

/* Parser. */

/* Parses the RECODE transformation. */
int
cmd_recode (struct lexer *lexer, struct dataset *ds)
{
  do
    {
      struct dictionary *dict = dataset_dict (ds);
      struct recode_trns *trns
        = pool_create_container (struct recode_trns, pool);

      /* Parse source variable names,
         then input to output mappings,
         then destintation variable names. */
      if (!parse_src_vars (lexer, trns, dict)
          || !parse_mappings (lexer, trns, dict_get_encoding (dict))
          || !parse_dst_vars (lexer, trns, dict))
        {
          recode_trns_free (trns);
          return CMD_FAILURE;
        }

      /* Ensure that all the output strings are at least as wide
         as the widest destination variable. */
      if (trns->dst_type == VAL_STRING)
	{
	  if ( ! enlarge_dst_widths (trns))
	    {
	      recode_trns_free (trns);
	      return CMD_FAILURE;
	    }
	}

      /* Create destination variables, if needed.
         This must be the final step; otherwise we'd have to
         delete destination variables on failure. */
      trns->dst_dict = dict;
      if (trns->src_vars != trns->dst_vars)
	create_dst_vars (trns, dict);

      /* Done. */
      add_transformation (ds,
			  recode_trns_proc, recode_trns_free, trns);
    }
  while (lex_match (lexer, T_SLASH));

  return CMD_SUCCESS;
}

/* Parses a set of variables to recode into TRNS->src_vars and
   TRNS->var_cnt.  Sets TRNS->src_type.  Returns true if
   successful, false on parse error. */
static bool
parse_src_vars (struct lexer *lexer,
		struct recode_trns *trns, const struct dictionary *dict)
{
  if (!parse_variables_const (lexer, dict, &trns->src_vars, &trns->var_cnt,
                        PV_SAME_TYPE))
    return false;
  pool_register (trns->pool, free, trns->src_vars);
  trns->src_type = var_get_type (trns->src_vars[0]);
  return true;
}

/* Parses a set of mappings, which take the form (input=output),
   into TRNS->mappings and TRNS->map_cnt.  Sets TRNS->dst_type.
   Returns true if successful, false on parse error. */
static bool
parse_mappings (struct lexer *lexer, struct recode_trns *trns,
                const char *dict_encoding)
{
  size_t map_allocated;
  bool have_dst_type;
  size_t i;

  /* Find length of longest source variable. */
  trns->max_src_width = var_get_width (trns->src_vars[0]);
  for (i = 1; i < trns->var_cnt; i++)
    {
      size_t var_width = var_get_width (trns->src_vars[i]);
      if (var_width > trns->max_src_width)
        trns->max_src_width = var_width;
    }

  /* Parse the mappings in parentheses. */
  trns->mappings = NULL;
  trns->map_cnt = 0;
  map_allocated = 0;
  have_dst_type = false;
  if (!lex_force_match (lexer, T_LPAREN))
    return false;
  do
    {
      enum val_type dst_type;

      if (!lex_match_id (lexer, "CONVERT"))
        {
          struct map_out out;
          size_t first_map_idx;
          size_t i;

          first_map_idx = trns->map_cnt;

          /* Parse source specifications. */
          do
            {
              struct map_in in;

              if (!parse_map_in (lexer, &in, trns->pool,
                                 trns->src_type, trns->max_src_width,
                                 dict_encoding))
                return false;
              add_mapping (trns, &map_allocated, &in);
              lex_match (lexer, T_COMMA);
            }
          while (!lex_match (lexer, T_EQUALS));

          if (!parse_map_out (lexer, trns->pool, &out))
            return false;

	  if (out.copy_input)
	    dst_type = trns->src_type;
	  else
	    dst_type = val_type_from_width (out.width);
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
          set_map_out_num (&trns->mappings[trns->map_cnt - 1].out, 0.0);

          dst_type = VAL_NUMERIC;
          if (trns->src_type != VAL_STRING
              || (have_dst_type && trns->dst_type != VAL_NUMERIC))
            {
              msg (SE, _("CONVERT requires string input values and "
                         "numeric output values."));
              return false;
            }
        }
      trns->dst_type = dst_type;
      have_dst_type = true;

      if (!lex_force_match (lexer, T_RPAREN))
        return false;
    }
  while (lex_match (lexer, T_LPAREN));

  return true;
}

/* Parses a mapping input value into IN, allocating memory from
   POOL.  The source value type must be provided as SRC_TYPE and,
   if string, the maximum width of a string source variable must
   be provided in MAX_SRC_WIDTH.  Returns true if successful,
   false on parse error. */
static bool
parse_map_in (struct lexer *lexer, struct map_in *in, struct pool *pool,
              enum val_type src_type, size_t max_src_width,
              const char *dict_encoding)
{

  if (lex_match_id (lexer, "ELSE"))
    set_map_in_generic (in, MAP_ELSE);
  else if (src_type == VAL_NUMERIC)
    {
      if (lex_match_id (lexer, "MISSING"))
        set_map_in_generic (in, MAP_MISSING);
      else if (lex_match_id (lexer, "SYSMIS"))
        set_map_in_generic (in, MAP_SYSMIS);
      else
        {
          double x, y;
          if (!parse_num_range (lexer, &x, &y, NULL))
            return false;
          set_map_in_num (in, x == y ? MAP_SINGLE : MAP_RANGE, x, y);
        }
    }
  else
    {
      if (lex_match_id (lexer, "MISSING"))
        set_map_in_generic (in, MAP_MISSING);
      else if (!lex_force_string (lexer))
        return false;
      else 
	{
	  set_map_in_str (in, pool, lex_tokss (lexer), max_src_width,
                          dict_encoding);
	  lex_get (lexer);
	  if (lex_token (lexer) == T_ID
	      && lex_id_match (ss_cstr ("THRU"), lex_tokss (lexer)))
	    {
	      msg (SE, _("%s is not allowed with string variables."), "THRU");
	      return false;
	    }
	}
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
                struct substring string, size_t width,
                const char *dict_encoding)
{
  char *s = recode_string (dict_encoding, "UTF-8",
                           ss_data (string), ss_length (string));
  in->type = MAP_SINGLE;
  value_init_pool (pool, &in->x, width);
  value_copy_buf_rpad (&in->x, width,
                       CHAR_CAST (uint8_t *, s), strlen (s), ' ');
  free (s);
}

/* Parses a mapping output value into OUT, allocating memory from
   POOL.  Returns true if successful, false on parse error. */
static bool
parse_map_out (struct lexer *lexer, struct pool *pool, struct map_out *out)
{
  if (lex_is_number (lexer))
    {
      set_map_out_num (out, lex_number (lexer));
      lex_get (lexer);
    }
  else if (lex_match_id (lexer, "SYSMIS"))
    set_map_out_num (out, SYSMIS);
  else if (lex_is_string (lexer))
    {
      set_map_out_str (out, pool, lex_tokss (lexer));
      lex_get (lexer);
    }
  else if (lex_match_id (lexer, "COPY")) 
    {
      out->copy_input = true;
      out->width = 0; 
    }
  else
    {
      lex_error (lexer, _("expecting output value"));
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
                 const struct substring value)
{
  const char *string = ss_data (value);
  size_t length = ss_length (value);

  if (length == 0)
    {
      /* A length of 0 will yield a numeric value, which is not
         what we want. */
      string = " ";
      length = 1;
    }

  out->copy_input = false;
  value_init_pool (pool, &out->value, length);
  memcpy (value_str_rw (&out->value, length), string, length);
  out->width = length;
}

/* Parses a set of target variables into TRNS->dst_vars and
   TRNS->dst_names. */
static bool
parse_dst_vars (struct lexer *lexer, struct recode_trns *trns,
		const struct dictionary *dict)
{
  size_t i;

  if (lex_match_id (lexer, "INTO"))
    {
      size_t name_cnt;
      size_t i;

      if (!parse_mixed_vars_pool (lexer, dict, trns->pool,
				  &trns->dst_names, &name_cnt,
                                  PV_NONE))
        return false;

      if (name_cnt != trns->var_cnt)
        {
          msg (SE, _("%zu variable(s) cannot be recoded into "
                     "%zu variable(s).  Specify the same number "
                     "of variables as source and target variables."),
               trns->var_cnt, name_cnt);
          return false;
        }

      trns->dst_vars = pool_nalloc (trns->pool,
                                    trns->var_cnt, sizeof *trns->dst_vars);
      for (i = 0; i < trns->var_cnt; i++)
        {
          const struct variable *v;
          v = trns->dst_vars[i] = dict_lookup_var (dict, trns->dst_names[i]);
          if (v == NULL && trns->dst_type == VAL_STRING)
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
               trns->src_type == VAL_NUMERIC ? _("numeric") : _("string"),
               trns->dst_type == VAL_NUMERIC ? _("numeric") : _("string"));
          return false;
        }
    }

  for (i = 0; i < trns->var_cnt; i++)
    {
      const struct variable *v = trns->dst_vars[i];
      if (v != NULL && var_get_type (v) != trns->dst_type)
        {
          msg (SE, _("Type mismatch.  Cannot store %s data in "
                     "%s variable %s."),
               trns->dst_type == VAL_STRING ? _("string") : _("numeric"),
               var_is_alpha (v) ? _("string") : _("numeric"),
               var_get_name (v));
          return false;
        }
    }

  return true;
}

/* Ensures that all the output values in TRNS are as wide as the
   widest destination variable. */
static bool
enlarge_dst_widths (struct recode_trns *trns)
{
  size_t i;
  const struct variable *narrow_var = NULL;
  int min_dst_width = INT_MAX;
  trns->max_dst_width = 0;
  
  for (i = 0; i < trns->var_cnt; i++)
    {
      const struct variable *v = trns->dst_vars[i];
      if (var_get_width (v) > trns->max_dst_width)
        trns->max_dst_width = var_get_width (v);

      if (var_get_width (v) < min_dst_width)
	{
	  min_dst_width = var_get_width (v);
	  narrow_var = v;
	}
    }

  for (i = 0; i < trns->map_cnt; i++)
    {
      struct map_out *out = &trns->mappings[i].out;
      if (!out->copy_input)
	{
	  if (out->width > min_dst_width)
	    {
	      msg (ME, 
		   _("Cannot recode because the variable %s would require a width of %d bytes or greater, but it has a width of only %d bytes."),
		   var_get_name (narrow_var), out->width, min_dst_width);
	      return false;
	    }
	    
	  value_resize_pool (trns->pool, &out->value,
			     out->width, trns->max_dst_width);
	}
    }

  return true;
}

/* Creates destination variables that don't already exist. */
static void
create_dst_vars (struct recode_trns *trns, struct dictionary *dict)
{
  size_t i;

  for (i = 0; i < trns->var_cnt; i++)
    {
      const struct variable **var = &trns->dst_vars[i];
      const char *name = trns->dst_names[i];

      *var = dict_lookup_var (dict, name);
      if (*var == NULL)
        *var = dict_create_var_assert (dict, name, 0);
      assert (var_get_type (*var) == trns->dst_type);
    }
}

/* Data transformation. */

/* Returns the output mapping in TRNS for an input of VALUE on
   variable V, or a null pointer if there is no mapping. */
static const struct map_out *
find_src_numeric (struct recode_trns *trns, double value, const struct variable *v)
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
          match = var_is_num_missing (v, value, MV_ANY);
          break;
        case MAP_RANGE:
          match = value >= in->x.f && value <= in->y.f;
          break;
        case MAP_SYSMIS:
          match = value == SYSMIS;
          break;
        case MAP_ELSE:
          match = true;
          break;
        default:
          NOT_REACHED ();
        }

      if (match)
        return out;
    }

  return NULL;
}

/* Returns the output mapping in TRNS for an input of VALUE with
   the given WIDTH, or a null pointer if there is no mapping. */
static const struct map_out *
find_src_string (struct recode_trns *trns, const uint8_t *value,
                 const struct variable *src_var)
{
  const char *encoding = dict_get_encoding (trns->dst_dict);
  int width = var_get_width (src_var);
  struct mapping *m;

  for (m = trns->mappings; m < trns->mappings + trns->map_cnt; m++)
    {
      const struct map_in *in = &m->in;
      struct map_out *out = &m->out;
      bool match;

      switch (in->type)
        {
        case MAP_SINGLE:
          match = !memcmp (value, value_str (&in->x, trns->max_src_width),
                           width);
          break;
        case MAP_ELSE:
          match = true;
          break;
        case MAP_CONVERT:
          {
            union value uv;
            char *error;

            error = data_in (ss_buffer (CHAR_CAST_BUG (char *, value), width),
                             C_ENCODING, FMT_F, &uv, 0, encoding);
            match = error == NULL;
            free (error);

            out->value.f = uv.f;
            break;
          }
	case MAP_MISSING:
	  match = var_is_str_missing (src_var, value, MV_ANY);
	  break;
        default:
          NOT_REACHED ();
        }

      if (match)
        return out;
    }

  return NULL;
}

/* Performs RECODE transformation. */
static int
recode_trns_proc (void *trns_, struct ccase **c, casenumber case_idx UNUSED)
{
  struct recode_trns *trns = trns_;
  size_t i;

  *c = case_unshare (*c);
  for (i = 0; i < trns->var_cnt; i++)
    {
      const struct variable *src_var = trns->src_vars[i];
      const struct variable *dst_var = trns->dst_vars[i];
      const struct map_out *out;

      if (trns->src_type == VAL_NUMERIC)
        out = find_src_numeric (trns, case_num (*c, src_var), src_var);
      else
        out = find_src_string (trns, case_str (*c, src_var), src_var);

      if (trns->dst_type == VAL_NUMERIC)
        {
          double *dst = &case_data_rw (*c, dst_var)->f;
          if (out != NULL)
            *dst = !out->copy_input ? out->value.f : case_num (*c, src_var);
          else if (trns->src_vars != trns->dst_vars)
            *dst = SYSMIS;
        }
      else
        {
          char *dst = CHAR_CAST_BUG (char *, case_str_rw (*c, dst_var));
          if (out != NULL)
            {
              if (!out->copy_input)
                memcpy (dst, value_str (&out->value, trns->max_dst_width),
                        var_get_width (dst_var));
              else if (trns->src_vars != trns->dst_vars)
                {
                  union value *dst_data = case_data_rw (*c, dst_var);
                  const union value *src_data = case_data (*c, src_var);
                  value_copy_rpad (dst_data, var_get_width (dst_var),
                                   src_data, var_get_width (src_var), ' ');
                }
            }
          else if (trns->src_vars != trns->dst_vars)
            memset (dst, ' ', var_get_width (dst_var));
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
