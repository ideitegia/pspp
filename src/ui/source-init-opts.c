/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

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
#include <argp.h>
#include "source-init-opts.h"
#include <stdbool.h>
#include <xalloc.h>
#include <string.h>
#include <output/output.h>
#include <data/file-name.h>
#include <libpspp/getl.h>
#include <language/syntax-file.h>
#include <stdlib.h>
#include <libpspp/llx.h>
#include <data/por-file-reader.h>
#include <data/sys-file-reader.h>
#include <libpspp/message.h>
#include <ui/syntax-gen.h>
#include <language/syntax-string-source.h>
#include <data/file-name.h>
#include <data/settings.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static const struct argp_option post_init_options [] = {
  {"algorithm", 'a', "{compatible|enhanced}", 0, N_("set to `compatible' if you want output calculated from broken algorithms"), 0},
  {"include", 'I', "DIR", 0, N_("Append DIR to include path"), 0},
  {"no-include", 'I', 0, 0, N_("Clear include path"), 0},
  {"no-statrc", 'r', 0, 0, N_("Disable execution of .pspp/rc at startup"), 0},
  {"config-dir", 'B', "DIR", 0, N_("Set configuration directory to DIR"), 0},
  {"safer", 's', 0, 0,  N_("Don't allow some unsafe operations"), 0},
  {"syntax", 'x', "{compatible|enhanced}", 0, N_("Set to `compatible' if you want only to accept SPSS compatible syntax"), 0},
  { 0, 0, 0, 0, 0, 0 }
};

static error_t
parse_post_init_opts (int key, char *arg, struct argp_state *state)
{
  struct source_init
  {
    bool process_statrc;
  };

  struct source_init *sip = state->hook;

  struct source_stream *ss = state->input;

  if ( state->input == NULL)
    return 0;

  switch (key)
    {
    case ARGP_KEY_INIT:
      state->hook = sip = xzalloc (sizeof (struct source_init));
      sip->process_statrc = true;
      break;
    case ARGP_KEY_FINI:
      free (sip);
      break;
    case  'a':
      if ( 0 == strcmp (arg, "compatible") )
	settings_set_algorithm (COMPATIBLE);
      else if ( 0 == strcmp (arg, "enhanced"))
	settings_set_algorithm (ENHANCED);
      else
	{
	  argp_failure (state, 1, 0, _("Algorithm must be either \"compatible\" or \"enhanced\"."));
	}
      break;
    case 'B':
      config_path = arg;
      break;
    case 'I':
      if (arg == NULL || !strcmp (arg, "-"))
	getl_clear_include_path (ss);
      else
	getl_add_include_dir (ss, arg);
      break;
    case 'r':
      sip->process_statrc = false;
      break;
    case ARGP_KEY_SUCCESS:
      if (sip->process_statrc)
	{
	  char *pspprc_fn = fn_search_path ("rc", config_path);
	  if (pspprc_fn != NULL)
	    {
	      getl_append_source (ss,
				  create_syntax_file_source (pspprc_fn),
				  GETL_BATCH,
				  ERRMODE_CONTINUE
				  );

	      free (pspprc_fn);
	    }
	}
      break;
    case 's':
      settings_set_safer_mode ();
      break;
    case 'x':
      if ( 0 == strcmp (arg, "compatible") )
	settings_set_syntax (COMPATIBLE);
      else if ( 0 == strcmp (arg, "enhanced"))
	settings_set_syntax (ENHANCED);
      else
	{
	  argp_failure (state, 1, 0, _("Syntax must be either \"compatible\" or \"enhanced\"."));
	}
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

const struct argp post_init_argp =
  {post_init_options, parse_post_init_opts, 0, 0, 0, 0, 0};

