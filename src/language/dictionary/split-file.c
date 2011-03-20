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

#include <stdlib.h>

#include "data/case.h"
#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/dictionary/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "output/tab.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_split_file (struct lexer *lexer, struct dataset *ds)
{
  if (lex_match_id (lexer, "OFF"))
    dict_set_split_vars (dataset_dict (ds), NULL, 0);
  else
    {
      struct variable **v;
      size_t n;

      /* For now, ignore SEPARATE and LAYERED. */
      (void) ( lex_match_id (lexer, "SEPARATE") || lex_match_id (lexer, "LAYERED") );

      lex_match (lexer, T_BY);
      if (!parse_variables (lexer, dataset_dict (ds), &v, &n, PV_NO_DUPLICATE))
	return CMD_CASCADING_FAILURE;

      dict_set_split_vars (dataset_dict (ds), v, n);
      free (v);
    }

  return CMD_SUCCESS;
}

/* Dumps out the values of all the split variables for the case C. */
void
output_split_file_values (const struct dataset *ds, const struct ccase *c)
{
  const struct dictionary *dict = dataset_dict (ds);
  const struct variable *const *split;
  struct tab_table *t;
  size_t split_cnt;
  int i;

  split_cnt = dict_get_split_cnt (dict);
  if (split_cnt == 0)
    return;

  t = tab_create (3, split_cnt + 1);
  tab_vline (t, TAL_GAP, 1, 0, split_cnt);
  tab_vline (t, TAL_GAP, 2, 0, split_cnt);
  tab_text (t, 0, 0, TAB_NONE, _("Variable"));
  tab_text (t, 1, 0, TAB_LEFT, _("Value"));
  tab_text (t, 2, 0, TAB_LEFT, _("Label"));
  split = dict_get_split_vars (dict);
  for (i = 0; i < split_cnt; i++)
    {
      const struct variable *v = split[i];
      char *s;
      const char *val_lab;
      const struct fmt_spec *print = var_get_print_format (v);

      tab_text_format (t, 0, i + 1, TAB_LEFT, "%s", var_get_name (v));

      s = data_out (case_data (c, v), dict_get_encoding (dict), print);

      tab_text_format (t, 1, i + 1, 0, "%.*s", print->w, s);

      free (s);
      
      val_lab = var_lookup_value_label (v, case_data (c, v));
      if (val_lab)
	tab_text (t, 2, i + 1, TAB_LEFT, val_lab);
    }
  tab_submit (t);
}
