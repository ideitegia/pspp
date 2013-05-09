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
#include "npar-two-sample-related.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum test
  {
    NT_WILCOXON,
    NT_SIGN,
    NT_MCNEMAR,
    n_Tests
  };

struct ts_test
{
  GtkWidget *button;
  char syntax[16];
};


static void
refresh (void *aux)
{
  int i;
  struct ts_test *tst = aux;

  for (i = 0 ; i < n_Tests; ++i)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tst[i].button), FALSE);
    }
}


static gboolean
valid (void *aux)
{
  int i;
  struct ts_test *tst = aux;

  for (i = 0 ; i < n_Tests; ++i)
    {
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (tst[i].button)))
	return TRUE;
    }

  return FALSE;
}



static gchar *
generate_syntax (struct paired_samples_dialog *psd, const struct ts_test *test)
{
  int i;
  gchar *text = NULL;
  GString *str = g_string_new ("NPAR TEST");

  for (i = 0 ; i < n_Tests; ++i)
  {
    if (! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (test[i].button)))
      continue;

    g_string_append (str, "\n\t");
    g_string_append (str, test[i].syntax);

    psppire_var_view_append_names (PSPPIRE_VAR_VIEW (psd->pairs_treeview), 0, str);

    g_string_append (str, " WITH ");

    psppire_var_view_append_names (PSPPIRE_VAR_VIEW (psd->pairs_treeview), 1, str);

    g_string_append (str, " (PAIRED)");
  }

  g_string_append (str, ".\n");

  text = str->str;
  g_string_free (str, FALSE);

  return text;
}

/* Pops up the dialog box */
void
two_related_dialog (PsppireDataWindow *de)
{
  gint response;
  struct ts_test nts[n_Tests];
  struct paired_samples_dialog *tt_d = two_sample_dialog_create (de);

  GtkWidget *frame = gtk_frame_new (_("Test Type"));
  GtkWidget *bb = gtk_vbutton_box_new ();

  strcpy (nts[NT_WILCOXON].syntax, "/WILCOXON");
  strcpy (nts[NT_SIGN].syntax, "/SIGN");
  strcpy (nts[NT_MCNEMAR].syntax, "/MCNEMAR");

  nts[NT_WILCOXON].button = gtk_check_button_new_with_mnemonic (_("_Wilcoxon"));
  nts[NT_SIGN].button = gtk_check_button_new_with_mnemonic (_("_Sign"));
  nts[NT_MCNEMAR].button = gtk_check_button_new_with_mnemonic (_("_McNemar"));

  gtk_box_pack_start (GTK_BOX (bb), nts[NT_WILCOXON].button, FALSE, FALSE, 5);
  gtk_box_pack_start (GTK_BOX (bb), nts[NT_SIGN].button,     FALSE, FALSE, 5);
  gtk_box_pack_start (GTK_BOX (bb), nts[NT_MCNEMAR].button,  FALSE, FALSE, 5);

  gtk_container_add (GTK_CONTAINER (frame), bb);

  gtk_widget_show_all (frame);
  two_sample_dialog_add_widget (tt_d, frame);

  tt_d->refresh = refresh;
  tt_d->valid = valid;
  tt_d->aux = nts;

  gtk_window_set_title (GTK_WINDOW (tt_d->dialog), _("Two-Related-Samples Tests"));

  response = psppire_dialog_run (PSPPIRE_DIALOG (tt_d->dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (de, generate_syntax (tt_d, nts)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (tt_d, nts)));
      break;
    default:
      break;
    }

  two_sample_dialog_destroy (tt_d);
}
