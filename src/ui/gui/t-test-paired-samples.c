/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2012  Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include <gtk/gtk.h>

#include "t-test-options.h"
#include "t-test-paired-samples.h"

#include "psppire-data-window.h"
#include "psppire-selector.h"
#include "psppire-var-view.h"

#include "psppire-dict.h"

#include "dialog-common.h"
#include "psppire-dialog.h"

#include "executor.h"

#include "helper.h"

#include "psppire-var-ptr.h"

#include "paired-dialog.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void
refresh (void *aux)
{
}


static gboolean
valid (void *aux)
{
  return TRUE;
}

static gchar *
generate_syntax (const struct paired_samples_dialog *d, const struct tt_options_dialog *opt)
{
  gchar *text = NULL;
  GString *str =   g_string_new ("T-TEST \n\tPAIRS = ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (d->pairs_treeview), 0, str);

  g_string_append (str, " WITH ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (d->pairs_treeview), 1, str);

  g_string_append (str, " (PAIRED)");
  g_string_append (str, "\n");

  tt_options_dialog_append_syntax (opt, str);

  g_string_append (str, ".\n");

  text = str->str;
  g_string_free (str, FALSE);

  return text;
}

/* Pops up the dialog box */
void
t_test_paired_samples_dialog (PsppireDataWindow *de)
{
  gint response;
  struct paired_samples_dialog *tt_d = two_sample_dialog_create (de);
  struct tt_options_dialog *opts = tt_options_dialog_create (GTK_WINDOW (de));

  GtkWidget *bb = gtk_hbutton_box_new ();
  GtkWidget *opt = gtk_button_new_with_mnemonic (_("O_ptions..."));
  gtk_box_pack_start (GTK_BOX (bb), opt, TRUE, TRUE, 5);

  gtk_widget_show_all (bb);
  two_sample_dialog_add_widget (tt_d, bb);
  
  g_signal_connect_swapped (opt, "clicked", G_CALLBACK (tt_options_dialog_run), opts);

  tt_d->refresh = refresh;
  tt_d->valid = valid;
  tt_d->aux = opts;

  gtk_window_set_title (GTK_WINDOW (tt_d->dialog), _("Paired Samples T Test"));

  response = psppire_dialog_run (PSPPIRE_DIALOG (tt_d->dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (de, generate_syntax (tt_d, opts)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (tt_d, opts)));
      break;
    default:
      break;
    }

  two_sample_dialog_destroy (tt_d);
  tt_options_dialog_destroy (opts);
}
