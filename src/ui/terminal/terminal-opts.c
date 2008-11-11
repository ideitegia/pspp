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
#include <stdbool.h>
#include <xalloc.h>
#include <stdlib.h>
#include <data/settings.h>
#include <output/output.h>
#include "msg-ui.h"
#include <ui/command-line.h>
#include <libpspp/verbose-msg.h>
#include <libpspp/llx.h>
#include <data/file-name.h>
#include "terminal-opts.h"
#include <libpspp/getl.h>
#include <language/syntax-file.h>
#include "read-line.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static const struct argp_option test_options [] =
  {
    {"verbose", 'v', 0, 0, N_("Increase diagnostic verbosity level"), 0},
    {"testing-mode", 'T', 0, OPTION_HIDDEN, 0, 0},

    { 0, 0, 0, 0, 0, 0 }
  };

static error_t
parse_test_opts (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'T':
      settings_set_testing_mode (true);
      break;
    case 'v':
      verbose_increment_level ();
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

static const struct argp_option io_options [] =
  {
    {"error-file", 'e', "FILE", 0,
     N_("Send error messages to FILE (appended)"), 0},

    {"device", 'o', "DEVICE", 0,
     N_("Select output driver DEVICE and disable defaults"), 0},

    {"list", 'l', 0, 0,
     N_("Print a list of known driver classes, then exit"), 0},

    {"interactive", 'i', 0, 0, N_("Start an interactive session"), 0},

    { 0, 0, 0, 0, 0, 0 }
  };


static error_t
parse_io_opts (int key, char *arg, struct argp_state *state)
{
  struct source_init
  {
    struct llx_list file_list;
    bool cleared_device_defaults;
    bool interactive;
  };

  struct fn_element {
    struct ll ll;
    const char *fn;
  };

  struct source_init *sip = state->hook;

  struct source_stream *ss = state->input;

  struct command_line_processor *clp = get_subject (state);

  switch (key)
    {
    case ARGP_KEY_INIT:
      state->hook = sip = xzalloc (sizeof (struct source_init));
      llx_init (&sip->file_list);
      break;
    case ARGP_KEY_ARG:
      if (strchr (arg, '='))
	outp_configure_macro (arg);
      else
	{
	  llx_push_tail (&sip->file_list, arg, &llx_malloc_mgr);
	}
      break;
    case ARGP_KEY_SUCCESS:
      {
      struct llx *llx = llx_null (&sip->file_list);
      while ((llx = llx_next (llx)) != llx_null (&sip->file_list))
	{
	  const char *fn = llx_data (llx);
	  /* Assume it's a syntax file */
	  getl_append_source (ss,
			      create_syntax_file_source (fn),
			      GETL_BATCH,
			      ERRMODE_CONTINUE
			      );

	}

      if (sip->interactive || llx_is_empty (&sip->file_list))
	{
	  getl_append_source (ss, create_readln_source (),
			      GETL_INTERACTIVE,
			      ERRMODE_CONTINUE
			      );

	  if (!sip->cleared_device_defaults)
	    outp_configure_add ("interactive");
	}
      }
      break;
    case ARGP_KEY_FINI:
      free (sip);
      break;
    case 'e':
      msg_ui_set_error_file (arg);
      break;
    case 'i':
      sip->interactive = true;
      break;
    case 'l':
      outp_list_classes ();
      break;
    case 'o':
      if (! sip->cleared_device_defaults)
	{
	  outp_configure_clear ();
	  sip->cleared_device_defaults = true;
	}
      outp_configure_add (arg);
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

const struct argp io_argp =  {io_options, parse_io_opts, 0, 0, 0, 0, 0};
const struct argp test_argp =  {test_options, parse_test_opts, 0, 0, 0, 0, 0};

#if 0
static const struct argp_child children [] =
  {
    {&io_argp, 0, N_("Options affecting input and output locations:"), 0},
    {&test_argp, 0, N_("Diagnostic options:"), 0},
    {0, 0, 0, 0}
  };


static error_t
propagate_aux (int key, char *arg, struct argp_state *state)
{
  if ( key == ARGP_KEY_INIT)
    {
      int i;
      for (i = 0 ; i < sizeof (children) / sizeof (children[0]) - 1 ; ++i)
	state->child_inputs[i] = state->input;
    }

  return ARGP_ERR_UNKNOWN;
}

const struct argp terminal_argp =  {NULL, propagate_aux, 0, 0, children, 0, 0};

#endif
