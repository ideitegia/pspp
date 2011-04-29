/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009, 2010, 2011  Free Software Foundation

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

#include "ui/gui/executor.h"

#include "data/dataset.h"
#include "data/lazy-casereader.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/cast.h"
#include "output/driver.h"
#include "ui/gui/psppire-data-store.h"
#include "ui/gui/psppire-output-window.h"

/* Lazy casereader callback function used by execute_syntax. */
static struct casereader *
create_casereader_from_data_store (void *data_store_)
{
  PsppireDataStore *data_store = data_store_;
  return psppire_data_store_get_reader (data_store);
}

gboolean
execute_syntax (PsppireDataWindow *window, struct lex_reader *lex_reader)
{
  struct lexer *lexer;
  gboolean retval = TRUE;

  struct casereader *reader;
  const struct caseproto *proto;
  casenumber case_cnt;
  unsigned long int lazy_serial;

  /* When the user executes a number of snippets of syntax in a
     row, none of which read from the active dataset, the GUI becomes
     progressively less responsive.  The reason is that each syntax
     execution encapsulates the active dataset data in another
     datasheet layer.  The cumulative effect of having a number of
     layers of datasheets wastes time and space.

     To solve the problem, we use a "lazy casereader", a wrapper
     around the casereader obtained from the data store, that
     only actually instantiates that casereader when it is
     needed.  If the data store casereader is never needed, then
     it is reused the next time syntax is run, without wrapping
     it in another layer. */
  proto = psppire_data_store_get_proto (window->data_store);
  case_cnt = psppire_data_store_get_case_count (window->data_store);
  reader = lazy_casereader_create (proto, case_cnt,
                                   create_casereader_from_data_store,
                                   window->data_store, &lazy_serial);
  dataset_set_source (window->dataset, reader);

  g_return_val_if_fail (dataset_has_source (window->dataset), FALSE);

  lexer = lex_create ();
  psppire_set_lexer (lexer);
  lex_append (lexer, lex_reader);

  for (;;)
    {
      enum cmd_result result = cmd_parse (lexer, window->dataset);

      if ( cmd_result_is_failure (result))
	{
	  retval = FALSE;
	  if ( lex_get_error_mode (lexer) == LEX_ERROR_STOP )
	    break;
	}

      if ( result == CMD_EOF || result == CMD_FINISH)
	break;
    }

  proc_execute (window->dataset);

  psppire_dict_replace_dictionary (window->data_store->dict,
				   dataset_dict (window->dataset));

  reader = dataset_steal_source (window->dataset);
  if (!lazy_casereader_destroy (reader, lazy_serial))
    psppire_data_store_set_reader (window->data_store, reader);

  /* Destroy the lexer only after obtaining the dataset, because the dataset
     might depend on the lexer, if the casereader specifies inline data.  (In
     such a case then we'll always get an error message--the inline data is
     missing, otherwise it would have been parsed in the loop above.) */
  lex_destroy (lexer);
  psppire_set_lexer (NULL);

  output_flush ();

  return retval;
}

/* Executes null-terminated string SYNTAX as syntax.
   Returns SYNTAX. */
gchar *
execute_syntax_string (PsppireDataWindow *window, gchar *syntax)
{
  execute_const_syntax_string (window, syntax);
  return syntax;
}

/* Executes null-terminated string SYNTAX as syntax. */
void
execute_const_syntax_string (PsppireDataWindow *window, const gchar *syntax)
{
  execute_syntax (window, lex_reader_for_string (syntax));
}
