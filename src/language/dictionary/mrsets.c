/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/mrset.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/hmap.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/stringi-map.h"
#include "libpspp/stringi-set.h"
#include "output/tab.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static bool parse_group (struct lexer *, struct dictionary *, enum mrset_type);
static bool parse_delete (struct lexer *, struct dictionary *);
static bool parse_display (struct lexer *, struct dictionary *);

int
cmd_mrsets (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);

  while (lex_match (lexer, T_SLASH))
    {
      bool ok;

      if (lex_match_id (lexer, "MDGROUP"))
        ok = parse_group (lexer, dict, MRSET_MD);
      else if (lex_match_id (lexer, "MCGROUP"))
        ok = parse_group (lexer, dict, MRSET_MC);
      else if (lex_match_id (lexer, "DELETE"))
        ok = parse_delete (lexer, dict);
      else if (lex_match_id (lexer, "DISPLAY"))
        ok = parse_display (lexer, dict);
      else
        {
          ok = false;
          lex_error (lexer, NULL);
        }

      if (!ok)
        return CMD_FAILURE;
    }

  return CMD_SUCCESS;
}

static bool
parse_group (struct lexer *lexer, struct dictionary *dict,
             enum mrset_type type)
{
  const char *subcommand_name = type == MRSET_MD ? "MDGROUP" : "MCGROUP";
  struct mrset *mrset;
  bool labelsource_varlabel;
  bool has_value;

  mrset = xzalloc (sizeof *mrset);
  mrset->type = type;
  mrset->cat_source = MRSET_VARLABELS;

  labelsource_varlabel = false;
  has_value = false;
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
    {
      if (lex_match_id (lexer, "NAME"))
        {
          if (!lex_force_match (lexer, T_EQUALS) || !lex_force_id (lexer)
              || !mrset_is_valid_name (lex_tokcstr (lexer),
                                       dict_get_encoding (dict), true))
            goto error;

          free (mrset->name);
          mrset->name = xstrdup (lex_tokcstr (lexer));
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "VARIABLES"))
        {
          if (!lex_force_match (lexer, T_EQUALS))
            goto error;

          free (mrset->vars);
          if (!parse_variables (lexer, dict, &mrset->vars, &mrset->n_vars,
                                PV_SAME_TYPE | PV_NO_SCRATCH))
            goto error;

          if (mrset->n_vars < 2)
            {
              msg (SE, _("VARIABLES specified only variable %s on %s, but "
                         "at least two variables are required."),
                   var_get_name (mrset->vars[0]), subcommand_name);
              goto error;
            }
        }
      else if (lex_match_id (lexer, "LABEL"))
        {
          if (!lex_force_match (lexer, T_EQUALS) || !lex_force_string (lexer))
            goto error;

          free (mrset->label);
          mrset->label = ss_xstrdup (lex_tokss (lexer));
          lex_get (lexer);
        }
      else if (type == MRSET_MD && lex_match_id (lexer, "LABELSOURCE"))
        {
          if (!lex_force_match (lexer, T_EQUALS)
              || !lex_force_match_id (lexer, "VARLABEL"))
            goto error;

          labelsource_varlabel = true;
        }
      else if (type == MRSET_MD && lex_match_id (lexer, "VALUE"))
        {
          if (!lex_force_match (lexer, T_EQUALS))
            goto error;

          has_value = true;
          if (lex_is_number (lexer))
            {
              if (!lex_is_integer (lexer))
                {
                  msg (SE, _("Numeric VALUE must be an integer."));
                  goto error;
                }
              value_destroy (&mrset->counted, mrset->width);
              mrset->counted.f = lex_integer (lexer);
              mrset->width = 0;
            }
          else if (lex_is_string (lexer))
            {
              size_t width;
              char *s;

              s = recode_string (dict_get_encoding (dict), "UTF-8",
                                 lex_tokcstr (lexer), -1);
              width = strlen (s);

              /* Trim off trailing spaces, but don't trim the string until
                 it's empty because a width of 0 is a numeric type. */
              while (width > 1 && s[width - 1] == ' ')
                width--;

              value_destroy (&mrset->counted, mrset->width);
              value_init (&mrset->counted, width);
              memcpy (value_str_rw (&mrset->counted, width), s, width);
              mrset->width = width;

              free (s);
            }
          else
            {
              lex_error (lexer, NULL);
              goto error;
            }
          lex_get (lexer);
        }
      else if (type == MRSET_MD && lex_match_id (lexer, "CATEGORYLABELS"))
        {
          if (!lex_force_match (lexer, T_EQUALS))
            goto error;

          if (lex_match_id (lexer, "VARLABELS"))
            mrset->cat_source = MRSET_VARLABELS;
          else if (lex_match_id (lexer, "COUNTEDVALUES"))
            mrset->cat_source = MRSET_COUNTEDVALUES;
          else
            {
              lex_error (lexer, NULL);
              goto error;
            }
        }
      else
        {
          lex_error (lexer, NULL);
          goto error;
        }
    }

  if (mrset->name == NULL)
    {
      lex_spec_missing (lexer, subcommand_name, "NAME");
      goto error;
    }
  else if (mrset->n_vars == 0)
    {
      lex_spec_missing (lexer, subcommand_name, "VARIABLES");
      goto error;
    }

  if (type == MRSET_MD)
    {
      /* Check that VALUE is specified and is valid for the VARIABLES. */
      if (!has_value)
        {
          lex_spec_missing (lexer, subcommand_name, "VALUE");
          goto error;
        }
      else if (var_is_alpha (mrset->vars[0]))
        {
          if (mrset->width == 0)
            {
              msg (SE, _("MDGROUP subcommand for group %s specifies a string "
                         "VALUE, but the variables specified for this group "
                         "are numeric."),
                   mrset->name);
              goto error;
            }
          else {
            const struct variable *shortest_var;
            int min_width;
            size_t i;

            shortest_var = NULL;
            min_width = INT_MAX;
            for (i = 0; i < mrset->n_vars; i++)
              {
                int width = var_get_width (mrset->vars[i]);
                if (width < min_width)
                  {
                    shortest_var = mrset->vars[i];
                    min_width = width;
                  }
              }
            if (mrset->width > min_width)
              {
                msg (SE, _("VALUE string on MDGROUP subcommand for group "
                           "%s is %d bytes long, but it must be no longer "
                           "than the narrowest variable in the group, "
                           "which is %s with a width of %d bytes."),
                     mrset->name, mrset->width,
                     var_get_name (shortest_var), min_width);
                goto error;
              }
          }
        }
      else
        {
          if (mrset->width != 0)
            {
              msg (SE, _("MDGROUP subcommand for group %s specifies a string "
                         "VALUE, but the variables specified for this group "
                         "are numeric."),
                   mrset->name);
              goto error;
            }
        }

      /* Implement LABELSOURCE=VARLABEL. */
      if (labelsource_varlabel)
        {
          if (mrset->cat_source != MRSET_COUNTEDVALUES)
            msg (SW, _("MDGROUP subcommand for group %s specifies "
                       "LABELSOURCE=VARLABEL but not "
                       "CATEGORYLABELS=COUNTEDVALUES.  "
                       "Ignoring LABELSOURCE."),
                 mrset->name);
          else if (mrset->label)
            msg (SW, _("MDGROUP subcommand for group %s specifies both LABEL "
                       "and LABELSOURCE, but only one of these subcommands "
                       "may be used at a time.  Ignoring LABELSOURCE."),
                 mrset->name);
          else
            {
              size_t i;

              mrset->label_from_var_label = true;
              for (i = 0; mrset->label == NULL && i < mrset->n_vars; i++)
                {
                  const char *label = var_get_label (mrset->vars[i]);
                  if (label != NULL)
                    {
                      mrset->label = xstrdup (label);
                      break;
                    }
                }
            }
        }

      /* Warn if categories cannot be distinguished in output. */
      if (mrset->cat_source == MRSET_VARLABELS)
        {
          struct stringi_map seen;
          size_t i;

          stringi_map_init (&seen);
          for (i = 0; i < mrset->n_vars; i++)
            {
              const struct variable *var = mrset->vars[i];
              const char *name = var_get_name (var);
              const char *label = var_get_label (var);
              if (label != NULL)
                {
                  const char *other_name = stringi_map_find (&seen, label);

                  if (other_name == NULL)
                    stringi_map_insert (&seen, label, name);
                  else
                    msg (SW, _("Variables %s and %s specified as part of "
                               "multiple dichotomy group %s have the same "
                               "variable label.  Categories represented by "
                               "these variables will not be distinguishable "
                               "in output."),
                         other_name, name, mrset->name);
                }
            }
          stringi_map_destroy (&seen);
        }
      else
        {
          struct stringi_map seen;
          size_t i;

          stringi_map_init (&seen);
          for (i = 0; i < mrset->n_vars; i++)
            {
              const struct variable *var = mrset->vars[i];
              const char *name = var_get_name (var);
              const struct val_labs *val_labs;
              union value value;
              const char *label;

              value_clone (&value, &mrset->counted, mrset->width);
              value_resize (&value, mrset->width, var_get_width (var));

              val_labs = var_get_value_labels (var);
              label = val_labs_find (val_labs, &value);
              if (label == NULL)
                msg (SW, _("Variable %s specified as part of multiple "
                           "dichotomy group %s (which has "
                           "CATEGORYLABELS=COUNTEDVALUES) has no value label "
                           "for its counted value.  This category will not "
                           "be distinguishable in output."),
                     name, mrset->name);
              else
                {
                  const char *other_name = stringi_map_find (&seen, label);

                  if (other_name == NULL)
                    stringi_map_insert (&seen, label, name);
                  else
                    msg (SW, _("Variables %s and %s specified as part of "
                               "multiple dichotomy group %s (which has "
                               "CATEGORYLABELS=COUNTEDVALUES) have the same "
                               "value label for the group's counted "
                               "value.  These categories will not be "
                               "distinguishable in output."),
                         other_name, name, mrset->name);
                }
            }
          stringi_map_destroy (&seen);
        }
    }
  else                          /* MCGROUP. */
    {
      /* Warn if categories cannot be distinguished in output. */
      struct category
        {
          struct hmap_node hmap_node;
          union value value;
          int width;
          const char *label;
          const char *var_name;
          bool warned;
        };

      struct category *c, *next;
      struct hmap categories;
      size_t i;

      hmap_init (&categories);
      for (i = 0; i < mrset->n_vars; i++)
        {
          const struct variable *var = mrset->vars[i];
          const char *name = var_get_name (var);
          int width = var_get_width (var);
          const struct val_labs *val_labs;
          const struct val_lab *vl;

          val_labs = var_get_value_labels (var);
          for (vl = val_labs_first (val_labs); vl != NULL;
               vl = val_labs_next (val_labs, vl))
            {
              const union value *value = val_lab_get_value (vl);
              const char *label = val_lab_get_label (vl);
              unsigned int hash = value_hash (value, width, 0);

              HMAP_FOR_EACH_WITH_HASH (c, struct category, hmap_node,
                                       hash, &categories)
                {
                  if (width == c->width
                      && value_equal (value, &c->value, width))
                    {
                      if (!c->warned && utf8_strcasecmp (c->label, label))
                        {
                          char *s = data_out (value, var_get_encoding (var),
                                              var_get_print_format (var));
                          c->warned = true;
                          msg (SW, _("Variables specified on MCGROUP should "
                                     "have the same categories, but %s and %s "
                                     "(and possibly others) in multiple "
                                     "category group %s have different "
                                     "value labels for value %s."),
                               c->var_name, name, mrset->name, s);
                          free (s);
                        }
                      goto found;
                    }
                }

              c = xmalloc (sizeof *c);
              value_clone (&c->value, value, width);
              c->width = width;
              c->label = label;
              c->var_name = name;
              c->warned = false;
              hmap_insert (&categories, &c->hmap_node, hash);

            found: ;
            }
        }

      HMAP_FOR_EACH_SAFE (c, next, struct category, hmap_node, &categories)
        {
          value_destroy (&c->value, c->width);
          hmap_delete (&categories, &c->hmap_node);
          free (c);
        }
      hmap_destroy (&categories);
    }

  dict_add_mrset (dict, mrset);
  return true;

error:
  mrset_destroy (mrset);
  return false;
}

static bool
parse_mrset_names (struct lexer *lexer, struct dictionary *dict,
                   struct stringi_set *mrset_names)
{
  if (!lex_force_match_id (lexer, "NAME")
      || !lex_force_match (lexer, T_EQUALS))
    return false;

  stringi_set_init (mrset_names);
  if (lex_match (lexer, T_LBRACK))
    {
      while (!lex_match (lexer, T_RBRACK))
        {
          if (!lex_force_id (lexer))
            return false;
          if (dict_lookup_mrset (dict, lex_tokcstr (lexer)) == NULL)
            {
              msg (SE, _("No multiple response set named %s."),
                   lex_tokcstr (lexer));
              stringi_set_destroy (mrset_names);
              return false;
            }
          stringi_set_insert (mrset_names, lex_tokcstr (lexer));
          lex_get (lexer);
        }
    }
  else if (lex_match (lexer, T_ALL))
    {
      size_t n_sets = dict_get_n_mrsets (dict);
      size_t i;

      for (i = 0; i < n_sets; i++)
        stringi_set_insert (mrset_names, dict_get_mrset (dict, i)->name);
    }

  return true;
}

static bool
parse_delete (struct lexer *lexer, struct dictionary *dict)
{
  const struct stringi_set_node *node;
  struct stringi_set mrset_names;
  const char *name;

  if (!parse_mrset_names (lexer, dict, &mrset_names))
    return false;

  STRINGI_SET_FOR_EACH (name, node, &mrset_names)
    dict_delete_mrset (dict, name);
  stringi_set_destroy (&mrset_names);

  return true;
}

static bool
parse_display (struct lexer *lexer, struct dictionary *dict)
{
  struct string details, var_names;
  struct stringi_set mrset_names_set;
  char **mrset_names;
  struct tab_table *table;
  size_t i, n;

  if (!parse_mrset_names (lexer, dict, &mrset_names_set))
    return false;

  n = stringi_set_count (&mrset_names_set);
  if (n == 0)
    {
      if (dict_get_n_mrsets (dict) == 0)
        msg (SN, _("The active dataset dictionary does not contain any "
                   "multiple response sets."));
      stringi_set_destroy (&mrset_names_set);
      return true;
    }

  table = tab_create (3, n + 1);
  tab_headers (table, 0, 0, 1, 0);
  tab_box (table, TAL_1, TAL_1, TAL_1, TAL_1, 0, 0, 2, n);
  tab_hline (table, TAL_2, 0, 2, 1);
  tab_title (table, "%s", _("Multiple Response Sets"));
  tab_text (table, 0, 0, TAB_EMPH | TAB_LEFT, _("Name"));
  tab_text (table, 1, 0, TAB_EMPH | TAB_LEFT, _("Variables"));
  tab_text (table, 2, 0, TAB_EMPH | TAB_LEFT, _("Details"));

  ds_init_empty (&details);
  ds_init_empty (&var_names);
  mrset_names = stringi_set_get_sorted_array (&mrset_names_set);
  for (i = 0; i < n; i++)
    {
      const struct mrset *mrset = dict_lookup_mrset (dict, mrset_names[i]);
      const int row = i + 1;
      size_t j;

      /* Details. */
      ds_clear (&details);
      ds_put_format (&details, "%s\n", (mrset->type == MRSET_MD
                                        ? _("Multiple dichotomy set")
                                        : _("Multiple category set")));
      if (mrset->label != NULL)
        ds_put_format (&details, "%s: %s\n", _("Label"), mrset->label);
      if (mrset->type == MRSET_MD)
        {
          if (mrset->label != NULL || mrset->label_from_var_label)
            ds_put_format (&details, "%s: %s\n", _("Label source"),
                           (mrset->label_from_var_label
                            ? _("First variable label among variables")
                            : _("Provided by user")));
          ds_put_format (&details, "%s: ", _("Counted value"));
          if (mrset->width == 0)
            ds_put_format (&details, "%.0f\n", mrset->counted.f);
          else
            {
              const uint8_t *raw = value_str (&mrset->counted, mrset->width);
              char *utf8 = recode_string ("UTF-8", dict_get_encoding (dict),
                                          CHAR_CAST (const char *, raw),
                                          mrset->width);
              ds_put_format (&details, "`%s'\n", utf8);
              free (utf8);
            }
          ds_put_format (&details, "%s: %s\n", _("Category label source"),
                         (mrset->cat_source == MRSET_VARLABELS
                          ? _("Variable labels")
                          : _("Value labels of counted value")));
        }

      /* Variable names. */
      ds_clear (&var_names);
      for (j = 0; j < mrset->n_vars; j++)
        ds_put_format (&var_names, "%s\n", var_get_name (mrset->vars[j]));

      tab_text (table, 0, row, TAB_LEFT, mrset_names[i]);
      tab_text (table, 1, row, TAB_LEFT, ds_cstr (&var_names));
      tab_text (table, 2, row, TAB_LEFT, ds_cstr (&details));
    }
  free (mrset_names);
  ds_destroy (&var_names);
  ds_destroy (&details);
  stringi_set_destroy (&mrset_names_set);

  tab_submit (table);

  return true;
}
