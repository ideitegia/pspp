/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011 Free Software Foundation, Inc.

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

static void SENTINEL (0)
add_encodings (GtkTreeStore *store, const char *category, ...)
{
  const char *encodings[16];
  va_list args;
  int n;

  /* Count encoding arguments. */
  va_start (args, category);
  n = 0;
  while ((encodings[n] = va_arg (args, const char *)) != NULL)
    {
      const char *encoding = encodings[n];
      if (!strcmp (encoding, "Auto") || is_encoding_supported (encoding))
        n++;
    }
  assert (n < sizeof encodings / sizeof *encodings);
  va_end (args);

  if (n == 0)
    return;

  va_start (args, category);
  if (n == 1)
    {
      char *description;

      if (strcmp (encodings[0], "Auto"))
        description = xasprintf ("%s (%s)", category, encodings[0]);
      else
        description = xstrdup (category);

      gtk_tree_store_insert_with_values (
        store, NULL, NULL, G_MAXINT,
        COL_DESCRIPTION, description,
        COL_ENCODING, encodings[0],
        -1);

      free (description);
    }
  else
    {
      GtkTreeIter head;
      int i;

      gtk_tree_store_insert_with_values (
        store, &head, NULL, G_MAXINT,
        COL_DESCRIPTION, category,
        -1);

      for (i = 0; i < n; i++)
        gtk_tree_store_insert_with_values (
          store, NULL, &head, G_MAXINT,
          COL_DESCRIPTION, encodings[i],
          COL_ENCODING, encodings[i],
          -1);
    }
  va_end (args);
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
  GtkCellRenderer *renderer;
  GtkWidget *hbox;
  GtkWidget *combo_box;
  GtkTreeStore *store;

  store = gtk_tree_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

  if (allow_auto)
    add_encodings (store, _("Automatically Detect"), "Auto", NULL_SENTINEL);
  add_encodings (store, _("Locale Encoding"), locale_charset (),
                 NULL_SENTINEL);
  add_encodings (store, "Unicode", "UTF-8", "UTF-16", "UTF-16BE", "UTF-16LE",
                 "UTF-32", "UTF-32BE", "UTF-32LE", NULL_SENTINEL);
  add_encodings (store, _("Arabic"), "IBM864", "ISO-8859-6", "Windows-1256",
                 NULL_SENTINEL);
  add_encodings (store, _("Armenian"), "ARMSCII-8", NULL_SENTINEL);
  add_encodings (store, _("Baltic"), "ISO-8859-13", "ISO-8859-4",
                 "Windows-1257", NULL_SENTINEL);
  add_encodings (store, _("Celtic"), "ISO-8859-14", NULL_SENTINEL);
  add_encodings (store, _("Central European"), "IBM852", "ISO-8859-2",
                 "Mac-CentralEurope", "Windows-1250", NULL_SENTINEL);
  add_encodings (store, _("Chinese Simplified"), "GB18030", "GB2312", "GBK",
                 "HZ-GB-2312", "ISO-2022-CN", NULL_SENTINEL);
  add_encodings (store, _("Chinese Traditional"), "Big5", "Big5-HKSCS",
                 "EUC-TW", NULL_SENTINEL);
  add_encodings (store, _("Croatian"), "MacCroatian", NULL_SENTINEL);
  add_encodings (store, _("Cyrillic"), "IBM855", "ISO-8859-5", "ISO-IR-111",
                 "KOI8-R", "MacCyrillic", NULL_SENTINEL);
  add_encodings (store, _("Cyrillic/Russian"), "IBM866", NULL_SENTINEL);
  add_encodings (store, _("Cyrillic/Ukrainian"), "KOI8-U", "MacUkrainian",
                 NULL_SENTINEL);
  add_encodings (store, _("Georgian"), "GEOSTD8", NULL_SENTINEL);
  add_encodings (store, _("Greek"), "ISO-8859-7", "MacGreek", NULL_SENTINEL);
  add_encodings (store, _("Gujarati"), "MacGujarati", NULL_SENTINEL);
  add_encodings (store, _("Gurmukhi"), "MacGurmukhi", NULL_SENTINEL);
  add_encodings (store, _("Hebrew"), "IBM862", "ISO-8859-8-I", "Windows-1255",
                 NULL_SENTINEL);
  add_encodings (store, _("Hebrew Visual"), "ISO-8859-8", NULL_SENTINEL);
  add_encodings (store, _("Hindi"), "MacDevangari", NULL_SENTINEL);
  add_encodings (store, _("Icelandic"), "MacIcelandic", NULL_SENTINEL);
  add_encodings (store, _("Japanese"), "EUC-JP", "ISO-2022-JP", "Shift_JIS",
                 NULL_SENTINEL);
  add_encodings (store, _("Korean"), "EUC-KR", "ISO-2022-KR", "JOHAB", "UHC",
                 NULL_SENTINEL);
  add_encodings (store, _("Nordic"), "ISO-8859-10", NULL_SENTINEL);
  add_encodings (store, _("Romanian"), "ISO-8859-16", "MacRomanian",
                 NULL_SENTINEL);
  add_encodings (store, _("South European"), "ISO-8859-3", NULL_SENTINEL);
  add_encodings (store, _("Thai"), "ISO-8859-11", "TIS-620", "Windows-874",
                 NULL_SENTINEL);
  add_encodings (store, _("Turkish"), "IBM857", "ISO-8859-9", "Windows-1254",
                 NULL_SENTINEL);
  add_encodings (store, _("Vietnamese"), "TVCN", "VISCII", "VPS",
                 "Windows-1258", NULL_SENTINEL);
  add_encodings (store, _("Western European"), "ISO-8859-1", "ISO-8859-15",
                 "Windows-1252", "IBM850", "MacRoman", NULL_SENTINEL);

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
  return encoding;
}
