/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2012, 2014 Free Software Foundation, Inc.

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

#include "ui/gui/psppire-encoding-selector.h"

#include <assert.h>
#include <stdlib.h>

#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"

#include "gl/c-strcase.h"
#include "gl/localcharset.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum
  {
    COL_DESCRIPTION,
    COL_ENCODING
  };

static void
add_encodings (GtkTreeStore *store, struct encoding_category *cat)
{
  if (cat->n_encodings == 1)
    {
      char *description;

      if (strcmp (cat->encodings[0], "Auto"))
        description = xasprintf ("%s (%s)", cat->category, cat->encodings[0]);
      else
        description = xstrdup (cat->category);

      gtk_tree_store_insert_with_values (
        store, NULL, NULL, G_MAXINT,
        COL_DESCRIPTION, description,
        COL_ENCODING, cat->encodings[0],
        -1);

      free (description);
    }
  else
    {
      GtkTreeIter head;
      int i;

      gtk_tree_store_insert_with_values (
        store, &head, NULL, G_MAXINT,
        COL_DESCRIPTION, cat->category,
        -1);

      for (i = 0; i < cat->n_encodings; i++)
        gtk_tree_store_insert_with_values (
          store, NULL, &head, G_MAXINT,
          COL_DESCRIPTION, cat->encodings[i],
          COL_ENCODING, cat->encodings[i],
          -1);
    }
}

static void
set_sensitive (GtkCellLayout   *cell_layout,
               GtkCellRenderer *cell,
               GtkTreeModel    *tree_model,
               GtkTreeIter     *iter,
               gpointer         data)
{
  gboolean sensitive;

  sensitive = !gtk_tree_model_iter_has_child (tree_model, iter);
  g_object_set (cell, "sensitive", sensitive, NULL);
}

struct find_default_encoding_aux
  {
    const char *default_encoding;
    GtkTreeIter iter;
  };

static gboolean
find_default_encoding (GtkTreeModel *model,
                       GtkTreePath *path,
                       GtkTreeIter *iter,
                       gpointer aux_)
{
  struct find_default_encoding_aux *aux = aux_;
  gchar *encoding;
  gboolean found;

  gtk_tree_model_get (model, iter, COL_ENCODING, &encoding, -1);
  found = encoding != NULL && !c_strcasecmp (encoding, aux->default_encoding);
  if (found)
    aux->iter = *iter;
  g_free (encoding);
  return found;
}

GtkWidget *
psppire_encoding_selector_new (const char *default_encoding,
                               gboolean allow_auto)
{
  struct find_default_encoding_aux aux;
  struct encoding_category *categories;
  struct encoding_category locale_cat;
  const char *locale_encoding;
  GtkCellRenderer *renderer;
  GtkWidget *hbox;
  GtkWidget *combo_box;
  GtkTreeStore *store;
  size_t n_categories;
  size_t i;

  store = gtk_tree_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

  if (allow_auto)
    {
      struct encoding_category auto_cat;
      const char *encoding = "Auto";

      auto_cat.category = _("Automatically Detect");
      auto_cat.encodings = &encoding;
      auto_cat.n_encodings = 1;
      add_encodings (store, &auto_cat);
    }

  locale_encoding = locale_charset ();
  locale_cat.category = _("Locale Encoding");
  locale_cat.encodings = &locale_encoding;
  locale_cat.n_encodings = 1;
  add_encodings (store, &locale_cat);

  categories = get_encoding_categories ();
  n_categories = get_n_encoding_categories ();
  for (i = 0; i < n_categories; i++)
    add_encodings (store, &categories[i]);

  combo_box = gtk_combo_box_new_with_model (GTK_TREE_MODEL (store));

  aux.default_encoding = default_encoding ? default_encoding : "Auto";
  gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &aux.iter);
  gtk_tree_model_foreach (GTK_TREE_MODEL (store), find_default_encoding, &aux);
  gtk_combo_box_set_active_iter (GTK_COMBO_BOX (combo_box), &aux.iter);

  g_object_unref (store);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
                                  "text", COL_DESCRIPTION,
                                  NULL);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combo_box),
                                      renderer, set_sensitive,
                                      NULL, NULL);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox),
                      gtk_label_new (_("Character Encoding: ")),
                      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), combo_box, FALSE, TRUE, 0);
  gtk_widget_show_all (hbox);

  return hbox;
}

gchar *
psppire_encoding_selector_get_encoding (GtkWidget *selector)
{
  gchar *encoding = NULL;
  GList *list, *pos;

  list = gtk_container_get_children (GTK_CONTAINER (selector));
  for (pos = list; pos; pos = pos->next)
    {
      GtkWidget *widget = pos->data;
      if (GTK_IS_COMBO_BOX (widget))
        {
          GtkTreeModel *model;
          GtkTreeIter iter;

          if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter))
            break;

          model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
          gtk_tree_model_get (model, &iter, COL_ENCODING, &encoding, -1);
          break;
        }
    }
  g_list_free (list);

  return encoding && !strcmp (encoding, "Auto") ? NULL : encoding;
}
