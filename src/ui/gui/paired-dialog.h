#ifndef PAIRED_DIALOG_H
#define PAIRED_DIALOG_H 1

#include  "psppire-data-window.h"
#include  "psppire-dict.h"

#include <gtk/gtk.h>

typedef void refresh_f (void *aux);
typedef gboolean valid_f (void *aux);

struct paired_samples_dialog
{
  PsppireDict *dict;
  GtkWidget *pairs_treeview;
  GtkTreeModel *list_store;
  GtkWidget *dialog;
  GtkBuilder *xml;

  refresh_f *refresh;
  valid_f *valid;
  void *aux;
};


struct paired_samples_dialog *two_sample_dialog_create (PsppireDataWindow *de);
void two_sample_dialog_destroy (struct paired_samples_dialog *psd);
void two_sample_dialog_add_widget (struct paired_samples_dialog *psd, GtkWidget *w);


#endif
