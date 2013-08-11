/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2010  Free Software Foundation

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

#include "source-init-opts.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "data/file-name.h"
#include "data/por-file-reader.h"
#include "data/settings.h"
#include "data/sys-file-reader.h"
#include "language/lexer/include-path.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/argv-parser.h"
#include "libpspp/llx.h"
#include "libpspp/message.h"
#include "ui/syntax-gen.h"

#include "gl/error.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum
  {
    OPT_ALGORITHM,
    OPT_INCLUDE,
    OPT_NO_INCLUDE,
    OPT_SAFER,
    OPT_SYNTAX,
    N_SOURCE_INIT_OPTIONS
  };

static const struct argv_option source_init_options[N_SOURCE_INIT_OPTIONS] =
  {
    {"algorithm", 'a', required_argument, OPT_ALGORITHM},
    {"include", 'I', required_argument, OPT_INCLUDE},
    {"no-include", 0, no_argument, OPT_NO_INCLUDE},
    {"safer", 's', no_argument, OPT_SAFER},
    {"syntax", 'x', required_argument, OPT_SYNTAX},
  };

static void
source_init_option_callback (int id, void *aux UNUSED)
{
  switch (id)
    {
    case OPT_ALGORITHM:
      if (!strcmp (optarg, "compatible"))
	settings_set_algorithm (COMPATIBLE);
      else if (!strcmp (optarg, "enhanced"))
	settings_set_algorithm (ENHANCED);
      else
        error (1, 0,
               _("Algorithm must be either `%s' or `%s'."), "compatible", "enhanced");
      break;

    case OPT_INCLUDE:
      if (!strcmp (optarg, "-"))
        include_path_clear ();
      else
        include_path_add (optarg);
      break;

    case OPT_NO_INCLUDE:
      include_path_clear ();
      break;

    case OPT_SAFER:
      settings_set_safer_mode ();
      break;

    case OPT_SYNTAX:
      if (!strcmp (optarg, "compatible") )
	settings_set_syntax (COMPATIBLE);
      else if (!strcmp (optarg, "enhanced"))
	settings_set_syntax (ENHANCED);
      else
        error (1, 0,
               _("Syntax must be either `%s' or `%s'."), "compatible", "enhanced");
      break;

    default:
      NOT_REACHED ();
    }
}

void
source_init_register_argv_parser (struct argv_parser *ap)
{
  argv_parser_add_options (ap, source_init_options, N_SOURCE_INIT_OPTIONS,
                           source_init_option_callback, NULL);
}
