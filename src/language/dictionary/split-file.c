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

#include <stdlib.h>

#include <data/case.h>
#include <data/dictionary.h>
#include <data/format.h>
#include <data/procedure.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/dictionary/split-file.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/message.h>
#include <libpspp/str.h>
#include <output/manager.h>
#include <output/table.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_split_file (void)
{
  if (lex_match_id ("OFF"))
    dict_set_split_vars (dataset_dict (current_dataset), NULL, 0);
  else
    {
      struct variable **v;
      size_t n;

      /* For now, ignore SEPARATE and LAYERED. */
      (void) ( lex_match_id ("SEPARATE") || lex_match_id ("LAYERED") );
      
      lex_match (T_BY);
      if (!parse_variables (dataset_dict (current_dataset), &v, &n, PV_NO_DUPLICATE))
	return CMD_CASCADING_FAILURE;

      dict_set_split_vars (dataset_dict (current_dataset), v, n);
      free (v);
    }

  return lex_end_of_command ();
}

/* Dumps out the values of all the split variables for the case C. */
void
output_split_file_values (const struct ccase *c)
{
  struct variable *const *split;
  struct tab_table *t;
  size_t split_cnt;
  int i;

  split_cnt = dict_get_split_cnt (dataset_dict (current_dataset));
  if (split_cnt == 0)
    return;

  t = tab_create (3, split_cnt + 1, 0);
  tab_dim (t, tab_natural_dimensions);
  tab_vline (t, TAL_GAP, 1, 0, split_cnt);
  tab_vline (t, TAL_GAP, 2, 0, split_cnt);
  tab_text (t, 0, 0, TAB_NONE, _("Variable"));
  tab_text (t, 1, 0, TAB_LEFT, _("Value"));
  tab_text (t, 2, 0, TAB_LEFT, _("Label"));
  split = dict_get_split_vars (dataset_dict (current_dataset));
  for (i = 0; i < split_cnt; i++)
    {
      struct variable *v = split[i];
      char temp_buf[80];
      const char *val_lab;

      assert (v->type == NUMERIC || v->type == ALPHA);
      tab_text (t, 0, i + 1, TAB_LEFT | TAT_PRINTF, "%s", v->name);
      
      data_out (temp_buf, &v->print, case_data (c, v->fv));
      
      temp_buf[v->print.w] = 0;
      tab_text (t, 1, i + 1, TAT_PRINTF, "%.*s", v->print.w, temp_buf);

      val_lab = val_labs_find (v->val_labs, *case_data (c, v->fv));
      if (val_lab)
	tab_text (t, 2, i + 1, TAB_LEFT, val_lab);
    }
  tab_flags (t, SOMF_NO_TITLE);
  tab_submit (t);
}
