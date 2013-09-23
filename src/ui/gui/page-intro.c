/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012, 2013  Free Software Foundation

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

#include "page-intro.h"

#include "ui/gui/text-data-import-dialog.h"

#include <errno.h>
#include <fcntl.h>
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
#include "ui/gui/psppire-scanf.h"
#include "ui/syntax-gen.h"

#include "gl/intprops.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct import_assistant;



/* The "intro" page of the assistant. */

/* The introduction page of the assistant. */
struct intro_page
  {
    GtkWidget *page;
    GtkWidget *all_cases_button;
    GtkWidget *n_cases_button;
    GtkWidget *n_cases_spin;
    GtkWidget *percent_button;
    GtkWidget *percent_spin;
  };

static void on_intro_amount_changed (struct intro_page *);

/* Initializes IA's intro substructure. */
struct intro_page *
intro_page_create (struct import_assistant *ia)
{
  GtkBuilder *builder = ia->asst.builder;
  struct string s;
  GtkWidget *hbox_n_cases ;
  GtkWidget *hbox_percent ;
  GtkWidget *table ;

  struct intro_page *p = xzalloc (sizeof (*p));

  p->n_cases_spin = gtk_spin_button_new_with_range (0, INT_MAX, 100);

  hbox_n_cases = psppire_scanf_new (_("Only the first %4d cases"), &p->n_cases_spin);

  table  = get_widget_assert (builder, "button-table");

  gtk_table_attach_defaults (GTK_TABLE (table), hbox_n_cases,
		    1, 2,
		    1, 2);

  p->percent_spin = gtk_spin_button_new_with_range (0, 100, 10);

  hbox_percent = psppire_scanf_new (_("Only the first %3d %% of file (approximately)"), &p->percent_spin);

  gtk_table_attach_defaults (GTK_TABLE (table), hbox_percent,
			     1, 2,
			     2, 3);

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "Intro"),
                                   GTK_ASSISTANT_PAGE_INTRO);

  p->all_cases_button = get_widget_assert (builder, "import-all-cases");

  p->n_cases_button = get_widget_assert (builder, "import-n-cases");

  p->percent_button = get_widget_assert (builder, "import-percent");

  g_signal_connect_swapped (p->all_cases_button, "toggled",
                    G_CALLBACK (on_intro_amount_changed), p);
  g_signal_connect_swapped (p->n_cases_button, "toggled",
                    G_CALLBACK (on_intro_amount_changed), p);
  g_signal_connect_swapped (p->percent_button, "toggled",
                    G_CALLBACK (on_intro_amount_changed), p);

  on_intro_amount_changed (p);

  ds_init_empty (&s);
  ds_put_cstr (&s, _("This assistant will guide you through the process of "
                     "importing data into PSPP from a text file with one line "
                     "per case,  in which fields are separated by tabs, "
                     "commas, or other delimiters.\n\n"));
  if (ia->file.total_is_exact)
    ds_put_format (
      &s, ngettext ("The selected file contains %zu line of text.  ",
                    "The selected file contains %zu lines of text.  ",
                    ia->file.line_cnt),
      ia->file.line_cnt);
  else if (ia->file.total_lines > 0)
    {
      ds_put_format (
        &s, ngettext (
          "The selected file contains approximately %lu line of text.  ",
          "The selected file contains approximately %lu lines of text.  ",
          ia->file.total_lines),
        ia->file.total_lines);
      ds_put_format (
        &s, ngettext (
          "Only the first %zu line of the file will be shown for "
          "preview purposes in the following screens.  ",
          "Only the first %zu lines of the file will be shown for "
          "preview purposes in the following screens.  ",
          ia->file.line_cnt),
        ia->file.line_cnt);
    }
  ds_put_cstr (&s, _("You may choose below how much of the file should "
                     "actually be imported."));
  gtk_label_set_text (GTK_LABEL (get_widget_assert (builder, "intro-label")),
                      ds_cstr (&s));
  ds_destroy (&s);

  return p;
}

/* Resets IA's intro page to its initial state. */
void
reset_intro_page (struct import_assistant *ia)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->intro->all_cases_button),
                                true);
}

/* Called when one of the radio buttons is clicked. */
static void
on_intro_amount_changed (struct intro_page *p)
{
  gtk_widget_set_sensitive (p->n_cases_spin,
                            gtk_toggle_button_get_active (
                              GTK_TOGGLE_BUTTON (p->n_cases_button)));

  gtk_widget_set_sensitive (p->percent_spin,
                            gtk_toggle_button_get_active (
                              GTK_TOGGLE_BUTTON (p->percent_button)));
}


void
intro_append_syntax (const struct intro_page *p, struct string *s)
{
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (p->n_cases_button)))
    ds_put_format (s, "  /IMPORTCASES=FIRST %d\n",
		   gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (p->n_cases_spin)));
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (p->percent_button)))
    ds_put_format (s, "  /IMPORTCASES=PERCENT %d\n",
		   gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (p->percent_spin)));
  else
    ds_put_cstr (s, "  /IMPORTCASES=ALL\n");
}
