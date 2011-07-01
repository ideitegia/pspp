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

#include <stdio.h>
#include <stdlib.h>

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Declarations. */

static int do_value_labels (struct lexer *,
			    const struct dictionary *dict, bool);
static void erase_labels (struct variable **vars, size_t var_cnt);
static int get_label (struct lexer *, struct variable **vars, size_t var_cnt,
                      const char *dict_encoding);

/* Stubs. */

int
cmd_value_labels (struct lexer *lexer, struct dataset *ds)
{
  return do_value_labels (lexer, dataset_dict (ds), true);
}

int
cmd_add_value_labels (struct lexer *lexer, struct dataset *ds)
{
  return do_value_labels (lexer, dataset_dict (ds), false);
}

/* Do it. */

static int
do_value_labels (struct lexer *lexer, const struct dictionary *dict, bool erase)
{
  struct variable **vars; /* Variable list. */
  size_t var_cnt;         /* Number of variables. */
  int parse_err=0;        /* true if error parsing variables */

  lex_match (lexer, T_SLASH);

  while (lex_token (lexer) != T_ENDCMD)
    {
      parse_err = !parse_variables (lexer, dict, &vars, &var_cnt,
				    PV_SAME_WIDTH);
      if (var_cnt < 1)
	{
	  free(vars);
	  return CMD_FAILURE;
	}
      if (erase)
        erase_labels (vars, var_cnt);
      while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
	if (!get_label (lexer, vars, var_cnt, dict_get_encoding (dict)))
          goto lossage;

      if (lex_token (lexer) != T_SLASH)
	{
          free (vars);
          break;
	}

      lex_get (lexer);

      free (vars);
    }

  return parse_err ? CMD_FAILURE : CMD_SUCCESS;

 lossage:
  free (vars);
  return CMD_FAILURE;
}

/* Erases all the labels for the VAR_CNT variables in VARS. */
static void
erase_labels (struct variable **vars, size_t var_cnt)
{
  size_t i;

  /* Erase old value labels if desired. */
  for (i = 0; i < var_cnt; i++)
    var_clear_value_labels (vars[i]);
}

/* Parse all the labels for the VAR_CNT variables in VARS and add
   the specified labels to those variables.  */
static int
get_label (struct lexer *lexer, struct variable **vars, size_t var_cnt,
           const char *dict_encoding)
{
  /* Parse all the labels and add them to the variables. */
  do
    {
      enum { MAX_LABEL_LEN = 255 };
      int width = var_get_width (vars[0]);
      union value value;
      struct string label;
      size_t trunc_len;
      size_t i;

      /* Set value. */
      value_init (&value, width);
      if (!parse_value (lexer, &value, vars[0]))
        {
          value_destroy (&value, width);
          return 0;
        }
      lex_match (lexer, T_COMMA);

      /* Set label. */
      if (lex_token (lexer) != T_ID && !lex_force_string (lexer))
        {
          value_destroy (&value, width);
          return 0;
        }

      ds_init_substring (&label, lex_tokss (lexer));

      trunc_len = utf8_encoding_trunc_len (ds_cstr (&label), dict_encoding,
                                           MAX_LABEL_LEN);
      if (ds_length (&label) > trunc_len)
	{
	  msg (SW, _("Truncating value label to %d bytes."), MAX_LABEL_LEN);
	  ds_truncate (&label, trunc_len);
	}

      for (i = 0; i < var_cnt; i++)
        var_replace_value_label (vars[i], &value, ds_cstr (&label));

      ds_destroy (&label);
      value_destroy (&value, width);

      lex_get (lexer);
      lex_match (lexer, T_COMMA);
    }
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD);

  return 1;
}
