/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2013  Free Software Foundation

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

#include "ui/gui/text-data-import-dialog.h"

#include <errno.h>
#include <fcntl.h>
#include <gtk-contrib/psppire-sheet.h>
#include <gtk/gtk.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "data/data-in.h"
#include "data/data-out.h"
#include "data/format-guesser.h"
#include "data/value-labels.h"
#include "language/data-io/data-parser.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/line-reader.h"
#include "libpspp/message.h"
#include "ui/gui/checkbox-treeview.h"
#include "ui/gui/dialog-common.h"
#include "ui/gui/executor.h"
#include "ui/gui/helper.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog.h"
#include "ui/gui/psppire-encoding-selector.h"
#include "ui/gui/psppire-empty-list-store.h"
#include "ui/gui/psppire-var-sheet.h"
#include "ui/gui/psppire-var-store.h"
#include "ui/gui/psppire-scanf.h"
#include "ui/syntax-gen.h"

#include "gl/error.h"
#include "gl/intprops.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct import_assistant;



/* The "sheet-spec" page of the assistant. */


/* Initializes IA's sheet_spec substructure. */
void
init_sheet_spec_page (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  struct sheet_spec_page *p = &ia->sheet_spec;

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "Sheet"),
                                   GTK_ASSISTANT_PAGE_INTRO);

}

/* Resets IA's sheet_spec page to its initial state. */
void
reset_sheet_spec_page (struct import_assistant *ia)
{
}

